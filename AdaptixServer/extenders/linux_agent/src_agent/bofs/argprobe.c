/// argprobe.c — BOF: 参数编解码探针（**零副作用**）
///
/// 用途：判定 BOF 参数 blob 的形状。存在的理由是 ISS-005 —— console 明文分词
/// 回退分支曾经**漏写 4 字节 count 头**，而 `BeaconDataParse()` 恰好会跳过这 4 字节，
/// 于是每一个字段整体错位 4 字节。这类错位**不报错**：BOF 照常跑、照常打回执，
/// 只是拿到的全是垃圾（第一个 token 的长度被当成"参数个数"）。
///
/// 所以需要一个只读、只打印的探针，把 blob 的**原始形状**打出来，对拍两条通道：
///   A. `/agent/command/execute` 的 `param_data`(base64)  —— 调用方自己打包
///   B. console 明文分词回退（param_data 不是合法 base64）—— **服务器打包**
/// A 正确而 B 错位 = ISS-005 在运行态**未修**（源码修了但没重编/重启）。
///
/// 不碰任何系统状态：不写文件、不加载 eBPF、不改进程属性。

#include "bof_api.h"

void go(char *args, int args_len) {
    /* ★ 前置防御：`args == NULL` 且 `args_len > 0` 是**畸形**输入。
     * `param_data` 缺失走的是 `args = NULL, args_len = 0` 那条**合法**路径，
     * 不受这里影响。但 msgpack 截断（见 msgpack.c 的 F4）能造出 NULL + 巨大长度，
     * 那时下面第一句 `args[0]` 就是空指针解引用。探针不该是唯一防线，
     * 但它是**离崩溃最近**的一处，挡住它能把"崩溃"降级成"一行报错"。 */
    if (args_len > 0 && !args) {
        BeaconPrintf(CALLBACK_ERROR, "[probe] MALFORMED args=NULL args_len=%d\n", args_len);
        return;
    }
    unsigned h0 = args_len > 0 ? (unsigned char)args[0] : 0;
    unsigned h1 = args_len > 1 ? (unsigned char)args[1] : 0;
    unsigned h2 = args_len > 2 ? (unsigned char)args[2] : 0;
    unsigned h3 = args_len > 3 ? (unsigned char)args[3] : 0;
    unsigned head = h0 | (h1 << 8) | (h2 << 16) | (h3 << 24);
    BeaconPrintf(CALLBACK_OUTPUT, "[probe] blob=%d head4=%u (%02x %02x %02x %02x)\n",
                 args_len, head, h0, h1, h2, h3);

    datap parser;
    BeaconDataParse(&parser, args, args_len);

    for (int i = 0; i < 6; i++) {
        int len = 0;
        char *p = BeaconDataExtract(&parser, &len);
        if (!p) { BeaconPrintf(CALLBACK_OUTPUT, "[probe] tok[%d]=<null>\n", i); break; }
        char buf[80];
        /* ★★ 三分支（原为两分支 `(len > 0 && len < 79) ? len : 79`）：
         *   len <= 0 → n = 0（**空字段**，什么都不拷）
         *   0 < len < 79 → n = len
         *   len >= 79 → n = 79（截断）
         * 原来 `len == 0` 掉进 else 分支 ⇒ **n = 79** ⇒ AxMemcpy 从裸指针
         * 越界读 79 字节。这不是理论问题：p23 只查"指针/NUL 维度"（空字段返回的
         * 是非 NULL 指针，p23 判它 SAFE），查不出"**长度维度**"—— 活体证据是
         * Consoles Id=2327 打出 `len=1213221953` 而 agent 仍存活（越界读未必
         * 立刻崩，读到的可能是映射内的邻页）。判据的覆盖面必须覆盖**每个维度**。 */
        int n = (len <= 0) ? 0 : (len < 79 ? len : 79);
        if (n > 0) AxMemcpy(buf, p, n);
        buf[n] = 0;                     /* n <= 79，buf[80] 内 */
        for (int k = 0; k < n; k++) if (buf[k] < 0x20 || buf[k] > 0x7e) buf[k] = '.';
        BeaconPrintf(CALLBACK_OUTPUT, "[probe] tok[%d] len=%d '%s'\n", i, len, buf);
    }
}
