# -*- coding: utf-8 -*-
"""P9 修复批（ISS-078 家族）：BOF 参数通道四处加固。

★ 为什么是"四处"而不是"每个消费点各写一遍"：全量扫出 **30 个** `BeaconDataExtract`
消费点。同一条不变量落在 N 个点上时，必须是**收口点 + 断言**，不能是 N 处手写
（ISS-071/072 的教训）。所以四处里有三处是**收口点**（API 契约 / msgpack 解码 /
执行入口），只有 argprobe 那一处是"消费点自己的 bug"。

  F1 bof_api.c : BeaconDataExtract —— 出参契约单点化 + 闭合无符号下溢
  F2 argprobe.c: 长度维度三分支 + args==NULL 前置防御（唯一真正的"消费点 bug"）
  F3 elf_bof.c : entry() 前的守卫（纵深，收口点之外的最后一道）
  F4 msgpack.c : mp_read_bin/mp_read_str 截断路径收口（★ 真问题，见下）

F4 是本批唯一"新发现的可达路径"，不是理论洁癖：
    `mp_read_bin` 在 `*len > 0` 分支里，`read_bytes` 失败就直接 `return -1`，
    此时 **`*len` 已被写入**（0xC6 读出的声明长度，可以是 4GB）而 **`*data` 未被写**。
    于是调用方拿到 `args = NULL` + `args_size = 巨大值` 这对**不一致**的组合。
    这不需要攻击者精巧构造 —— **一个被截断的 msgpack 就够了**。
每处替换断言唯一命中，否则不写盘。
"""
import io, sys

ROOT = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/"
R = []
FILES = {}


def patch(fn, tag, old, new):
    s = FILES.setdefault(fn, io.open(ROOT + fn, encoding="utf-8").read())
    n = s.count(old)
    R.append((fn.split("/")[-1] + ":" + tag, n))
    if n == 1:
        FILES[fn] = s.replace(old, new)


# ══ F1 bof_api.c : BeaconDataExtract ═══════════════════════════════════════
patch("agent/bof_api.c", "F1", '''char *BeaconDataExtract(datap *parser, int *size) {
    if (!parser || parser->length < 4)
        return (char *)0;

    unsigned int length = 0;
    ax_memcpy(&length, parser->buffer, 4);
    parser->length -= 4;
    parser->buffer += 4;

    char *outdata = parser->buffer;

    parser->length -= length;
    parser->buffer += length;

    if (size)
        *size = length;
    return outdata;
}''', '''/// ★★ 出参契约（ISS-078 家族，**所有返回路径都必须满足**）：
///   `p == NULL`             ⇒ 取不出字段了（没有下一个 / 畸形）—— 调用方应当停止
///   `p != NULL, *size == 0` ⇒ **这一字段是空的**，但后面**还有**字段 —— 可以继续
///   `p != NULL, *size > 0`  ⇒ 正常字段
/// 换句话说 `*size` **永远**被写。这条契约是必需的，因为下面两种"空"都合法可达：
///   ① parser 空 / 剩余 < 4（`param_data` 缺失 ⇒ 服务器发 nil ⇒ args=NULL,size=0，
///      或空串 ⇒ 4 字节 count=0）—— 原来的代码在这里**不写 `*size`**，于是没把
///      size 初始化的调用方读到的是**栈上垃圾**，再拿它当长度去 memcpy（ISS-073 同族）。
///   ② count 头为 0 或某个字段长度声明为 0 —— 返回**非 NULL** 指针 + `*size = 0`。
/// 两者不能混：混了之后"取不出更多字段"与"这个字段是空的"就分不开了。
char *BeaconDataExtract(datap *parser, int *size) {
    if (!parser || parser->length < 4) {
        if (size)
            *size = 0;                  /* ← 早退**也必须**写 *size（契约①） */
        return (char *)0;
    }

    unsigned int length = 0;
    ax_memcpy(&length, parser->buffer, 4);

    /* ★ 畸形：声明的长度超过剩余可读字节。原代码这里直接
     *   `parser->length -= length;` —— `length` 是 **unsigned**，`parser->length`
     *   是 int，做的是无符号运算：一旦 length 大于剩余，结果**下溢**成一个巨大的
     *   正数，游标从此飞到缓冲区之外，后续每一次 Extract 都从野指针读。
     *   （`parser->length >= 4` 已由上面保证，所以 `- 4` 不会下溢。）
     * 归零 + 回 NULL ⇒ 调用方 `if (!p) break;` 自然收场，游标停在原地不再前进。 */
    if (length > (unsigned int)(parser->length - 4)) {
        if (size)
            *size = 0;
        return (char *)0;
    }

    parser->length -= 4;
    parser->buffer += 4;

    char *outdata = parser->buffer;

    parser->length -= (int)length;
    parser->buffer += (int)length;

    if (size)
        *size = (int)length;
    return outdata;                     /* length == 0 也返回指针（契约②），
                                         * 让调用方能继续往后取字段 */
}''')

# ══ F2 argprobe.c ═══════════════════════════════════════════════════════════
patch("bofs/argprobe.c", "F2-n", '''        int n = (len > 0 && len < 79) ? len : 79;
        if (n > 0) AxMemcpy(buf, p, n);
        buf[n > 0 ? n : 0] = 0;''', '''        /* ★★ 三分支（原为两分支 `(len > 0 && len < 79) ? len : 79`）：
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
        buf[n] = 0;                     /* n <= 79，buf[80] 内 */''')

patch("bofs/argprobe.c", "F2-null", '''void go(char *args, int args_len) {
    unsigned h0 = args_len > 0 ? (unsigned char)args[0] : 0;''', '''void go(char *args, int args_len) {
    /* ★ 前置防御：`args == NULL` 且 `args_len > 0` 是**畸形**输入。
     * `param_data` 缺失走的是 `args = NULL, args_len = 0` 那条**合法**路径，
     * 不受这里影响。但 msgpack 截断（见 msgpack.c 的 F4）能造出 NULL + 巨大长度，
     * 那时下面第一句 `args[0]` 就是空指针解引用。探针不该是唯一防线，
     * 但它是**离崩溃最近**的一处，挡住它能把"崩溃"降级成"一行报错"。 */
    if (args_len > 0 && !args) {
        BeaconPrintf(CALLBACK_ERROR, "[probe] MALFORMED args=NULL args_len=%d\\n", args_len);
        return;
    }
    unsigned h0 = args_len > 0 ? (unsigned char)args[0] : 0;''')

# ══ F3 elf_bof.c : entry() 前的守卫 ════════════════════════════════════════
patch("agent/elf_bof.c", "F3", '''    // ── Step 8: Initialize BOF output, execute, collect output ──
    bof_output_init();

    entry((char *)args, (int)args_size);''', '''    // ── Step 8: Initialize BOF output, execute, collect output ──
    /* ★ 守卫（ISS-078 家族）：`args == NULL` 且 `args_size > 0` 是**畸形**输入。
     * 原来这一行**没有任何守卫** —— 而 BOF 是**不可信输入**（任意 .o 都能被下发），
     * 里面第一句 `args[0]` 就是空指针解引用，直接把 agent 打死。
     * `param_data` 缺失走的是 `args=NULL, size=0` 那条**合法**路径，不受影响。
     * 放在 `bof_output_init()` **之前**，与上面 `if (!entry)` 的收尾方式对齐
     * （不 init 就不必 cleanup）。 */
    if (!args && args_size > 0) {
        ax_free(sym_values);
        cleanup_arena(&arena);
        bof_error_response(response, BOF_ERR_PARSE, "args NULL with nonzero size");
        return -1;
    }

    bof_output_init();

    entry((char *)args, (int)args_size);''')

# ══ F4 msgpack.c : 截断路径收口 ════════════════════════════════════════════
patch("agent/msgpack.c", "F4-bin", '''    if (*len > 0) {
        if (read_bytes(r, data, *len)) return -1;
    } else {
        *data = (const uint8_t*)0;
    }
    return 0;
}''', '''    if (*len > 0) {
        /* ★★ 收口点（ISS-078 家族）：原来这里是裸的 `return -1` ——
         *   **`*len` 已经被写成声明长度，而 `*data` 一个字节都没写**。
         *   调用方于是拿到 `data = NULL` + `len = 巨大值` 这对**不一致**的组合，
         *   而这正是 `elf_bof.c` 的无守卫 `entry((char *)args, (int)args_size)`
         *   最怕的形状。触发它**不需要**攻击者精巧构造：一个被截断的 msgpack
         *   （bin 头声明 N 字节、实际只有 M < N 字节）就够了。
         * 失败时把两个出参**一起**归零，调用方看到的就是一致的空值。 */
        if (read_bytes(r, data, *len)) {
            *data = (const uint8_t*)0;
            *len = 0;
            return -1;
        }
    } else {
        *data = (const uint8_t*)0;
    }
    return 0;
}''')

patch("agent/msgpack.c", "F4-str", '''    const uint8_t* p;
    if (*len > 0) {
        if (read_bytes(r, &p, *len)) return -1;
        *str = (const char*)p;
    } else {
        *str = "";
    }
    return 0;
}''', '''    const uint8_t* p;
    if (*len > 0) {
        if (read_bytes(r, &p, *len)) {   /* ★ 同 F4-bin：出参一起归零 */
            *str = "";
            *len = 0;
            return -1;
        }
        *str = (const char*)p;
    } else {
        *str = "";
    }
    return 0;
}''')

bad = [t for t, n in R if n != 1]
print("命中:", R)
if bad:
    print("ABORT 未命中/不唯一:", bad)
    sys.exit(1)
for fn, s in FILES.items():
    io.open(ROOT + fn, "w", encoding="utf-8").write(s)
print("OK 已写入:", sorted(FILES))
