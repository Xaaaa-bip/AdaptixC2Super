#!/usr/bin/env python3
"""redact.c 返工 4：让 verifier 通过（← 真正的拦路虎）。

verifier 原话（bpftool prog load）：
    310: (0f) r1 += r2     ; R1=fp(off=-312, var_off=(0x0; 0xffffffff))
    311: (71) r1 = *(u8 *)(r1 +0)
    invalid unbounded variable-offset read from stack R1

根因：**变长偏移索引栈缓冲，verifier 一律拒绝**。clang 为零扩展 32 位索引生成的
`<<=32; >>=32` 把 [0,31] 的界丢成 [0,0xffffffff]，越过 BPF_MAX_VAR_SIZ。

修法：把变长偏移**留在用户指针（scalar）上** —— 用户地址随便加，落进**定长**栈槽，
栈访问全部是常量偏移：
    bpf_probe_read_user(t, wl, s + n - wl)          ← t[] 定长、索引常量
    bpf_probe_read_user(&c, 1, s + n - wl - 1)      ← 前一个字符单独取
于是 p[32] 只剩「量长度」一个用途，不再参与比较，连清零都可以省。

教训：`build_bpf.sh` 只跑 clang，**不跑 verifier** —— "编译 PASS" 从来不代表能加载。
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def rd(p):
    with io.open(os.path.join(ROOT, p), encoding="utf-8") as f:
        return f.read()


def wr(p, s):
    with io.open(os.path.join(ROOT, p), "w", encoding="utf-8", newline="") as f:
        f.write(s)
    print("  [ok] %s -> %d 行" % (p, s.count("\n") + 1))


def sub(V, old, new, tag):
    c = V.count(old)
    assert c == 1, "[%s] 锚点命中 %d 次（要求 1）" % (tag, c)
    return V.replace(old, new)


P = "src_agent/bpf/redact.c"
V = rd(P)

# ── ① 头注：补上 verifier 这一课
V = sub(V, ''' * ★ 两个顺序 / 来源问题，都会让判据**静默失效**（编译过、verifier 过、就是不生效）：
 *   · 词长由 `sizeof(W)-1` 算，不手写（手写 14 vs 真值 13 的教训见上）；
 *   · 「整串就是它」（相对路径）那条**必须排在逐字节比较之后** —— 排在前面等于不比
 *     内容就认账，凡与目标等长的路径全中招（`/etc/hostname` 与 "ld.so.preload" 同为
 *     13 字符）。 */''',
        ''' * ★ 三个坑，全都是**静默失效**（编译过、看着对、就是不生效）：
 *   · 词长由 `sizeof(W)-1` 算，不手写（手写 14 vs 真值 13 的教训见上）；
 *   · 「整串就是它」（相对路径）那条**必须排在逐字节比较之后** —— 排在前面等于不比
 *     内容就认账，凡与目标等长的路径全中招（`/etc/hostname` 与 "ld.so.preload" 同为
 *     13 字符）；
 *   · **变长偏移不许索引栈缓冲**。第一版把整条路径读进 `p[32]` 再 `s[n-wl+i]` 去比，
 *     被 verifier 一句 `invalid unbounded variable-offset read from stack` 打回：
 *     clang 为零扩展 32 位下标生成的 `<<=32; >>=32` 把 [0,31] 的界丢成了
 *     [0,0xffffffff]，越过 BPF_MAX_VAR_SIZ。**判据本身没错，是取数方式被禁**。
 *     现在变长偏移只出现在**用户指针**上（scalar，随便加），落栈一律常量偏移。 */''',
        "redact/note")

# ── ② 宏块重建
L = V.split("\n")
i0 = next(i for i, l in enumerate(L) if l.startswith("#define AX_TAIL_FN"))
i1 = next(i for i, l in enumerate(L) if i > i0 and l.lstrip().startswith("}") and "前一个字符" in l)

BODY = [
    "#define AX_TAIL_FN(fn, W)",
    "static __always_inline __u32 fn(const void *s, __u32 n)",
    "{",
    "    const __u32 wl = (__u32)(sizeof(W) - 1);      /* 13 / 7，编译期常量 */",
    "    char t[16];                                   /* 定长栈槽，>= wl */",
    "    char c;",
    "    __u32 d = 0;",
    "    if (n < wl)",
    "        return 0;",
    "    /* ★ 变长偏移只在**用户指针**上做；落栈的每次读写都是常量偏移。",
    "     * 反过来（先读进栈、再按变长偏移索引栈）verifier 直接拒绝。 */",
    "    if (bpf_probe_read_user(t, wl, (const void *)((const char *)s + n - wl)))",
    "        return 0;",
    '    _Pragma("unroll")',
    "    for (int i = 0; i < (int)(sizeof(W) - 1); i++)",
    "        d |= (__u32)(__u8)t[i] ^ (__u32)(__u8)(W)[i];",
    "    if (!ax_iszero(d))",
    "        return 0;                    /* 尾部不是那个词 */",
    "    if (n == wl)                     /* ★ 比较之后才认「整串就是它」 */",
    "        return 1;                    /* 相对路径（cwd=/etc 时的裸文件名） */",
    "    if (bpf_probe_read_user(&c, 1, (const void *)((const char *)s + n - wl - 1)))",
    "        return 0;",
    "    return ax_iszero((__u32)(__u8)c ^ (__u32)'/');",
    "}                                    /* 前一个字符是 '/' → 是完整分量 */",
]
NEW = "".join(x.ljust(74) + " \\\n" for x in BODY[:-1]) + BODY[-1]
L[i0:i1 + 1] = NEW.split("\n")
V = "\n".join(L)

# ── ③ 调用点：传用户指针；p[32] 降级为「量长度」
V = sub(V, '''        /* 先清零：read_user_str 只写到 NUL 为止，其余字节是栈上残留（ISS-043 同族），
         * 后面的定长比较不先清就会拿残留去比。 */
        __builtin_memset(p, 0, sizeof(p));
        slen = (int)bpf_probe_read_user_str(p, sizeof(p), (void *)pathp);
        if (slen < 0)
            return 0;
        pn = (__u32)(slen - 1);     /* helper 返回含 NUL 的长度 */
        if (pn > 31u)               /* 无符号比较：同时排掉 slen==0，并给 verifier 上界 */
            return 0;

        if (ax_tail_preload(p, pn)) {''',
        '''        /* 这颗缓冲**只用来量长度**（helper 返回含 NUL 的写入长度），不再参与比较
         * —— 比较改在用户地址上按变长偏移取（见 AX_TAIL_FN 头注）。所以不必先清零：
         * 残留字节读不到了（原注释担心的 ISS-043 同族问题随之消失）。 */
        slen = (int)bpf_probe_read_user_str(p, sizeof(p), (void *)pathp);
        if (slen < 0)
            return 0;
        pn = (__u32)(slen - 1);     /* helper 返回含 NUL 的长度 */
        if (pn > 31u)               /* 无符号比较同时排掉 slen==0；
                                     * 超长（>31）路径放弃 —— 缓冲只有 32，长度已不可信 */
            return 0;

        if (ax_tail_preload((const void *)pathp, pn)) {''', "redact/call-preload")

V = sub(V, "        } else if (ax_tail_modules(p, pn)) {",
        "        } else if (ax_tail_modules((const void *)pathp, pn)) {",
        "redact/call-modules")

wr(P, V)
print("\n[OK] 变长偏移已挪到用户指针；重新编译")
