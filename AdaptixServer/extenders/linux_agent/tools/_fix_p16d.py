#!/usr/bin/env python3
"""redact.c 返工 3：尾部分量判据的两个**自我失效** bug。

① `AX_PL_WLEN 14` —— "ld.so.preload" 是 **13** 字符。手写长度的失效是**静默**的：
   编译通过、verifier 通过，判据却永远不命中，症状和"没修"一模一样。
   → 长度一律由 sizeof(W)-1 算。

② `if (n == (WL)) return 1;` 排在逐字节比较**之前** —— 它 return 1 时根本没比过内容，
   **任何**长度等于 WL 的路径都被当成目标：
       WL=14 时  → libnss_real.so（14 字符）被当 preload
       WL 修成 13 → /etc/hostname（**也是 13 字符**，与 "ld.so.preload" 等长）被误伤
   后者正是 p15 T4「非目标不误伤」守的那条线。
   → 比较完再判「整串就是它」。

手法：宏块由**行表重建**（"\\\n" 拼接），不从 raw string 转写 —— 上一版就栽在
`...\\'''`（raw string 里反斜杠紧贴结束符会吞掉它）以及宏注释行漏续行符上。
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

# ── ① 词长：手写 → sizeof
V = sub(V, '''#define AX_PL_WORD "ld.so.preload"
#define AX_PL_WLEN 14
#define AX_MD_WORD "modules"
#define AX_MD_WLEN 7
''', '''#define AX_PL_WORD "ld.so.preload"
#define AX_MD_WORD "modules"
/* ★ 长度**不许手写**。第一版把 "ld.so.preload"（13 字符）写成 14 —— 编译通过、
 * verifier 通过，判据却**永远不命中**（`n == 14` 只对 14 字符的路径生效，而真身是
 * 13）。手写长度的失效是**静默**的：症状与"根本没改"逐字节相同。 */
#define AX_PL_WLEN ((__u32)(sizeof(AX_PL_WORD) - 1))
#define AX_MD_WLEN ((__u32)(sizeof(AX_MD_WORD) - 1))
''', "redact/defs")

# ── ② 宏体：整块重建（比较前置）
V = sub(V, ''' * 全程定长展开 + 位或累积，无分支（点 5）；长度用有界累加求、不用提前 return
 * —— n 个 return 就是 n 条 verifier 路径（点 7）。 */''',
        ''' * 全程定长展开 + 位或累积，无分支（点 5）。
 *
 * ★ 两个顺序 / 来源问题，都会让判据**静默失效**（编译过、verifier 过、就是不生效）：
 *   · 词长由 `sizeof(W)-1` 算，不手写（手写 14 vs 真值 13 的教训见上）；
 *   · 「整串就是它」（相对路径）那条**必须排在逐字节比较之后** —— 排在前面等于不比
 *     内容就认账，凡与目标等长的路径全中招（`/etc/hostname` 与 "ld.so.preload" 同为
 *     13 字符）。 */''', "redact/note")

L = V.split("\n")
i0 = next(i for i, l in enumerate(L) if l.startswith("#define AX_TAIL_FN"))
i1 = next(i for i, l in enumerate(L) if i > i0 and l.lstrip().startswith("}") and "前一个字符" in l)

BODY = [
    "#define AX_TAIL_FN(fn, W, WL)",
    "static __always_inline __u32 fn(const char *s, __u32 n)",
    "{",
    "    __u32 d = 0;",
    "    if (n < (WL))",
    "        return 0;",
    '    _Pragma("unroll")',
    "    for (int i = 0; i < (int)(WL); i++)",
    "        d |= (__u32)(__u8)s[n - (WL) + i] ^ (__u32)(__u8)(W)[i];",
    "    if (!ax_iszero(d))",
    "        return 0;                    /* 尾部不是那个词 */",
    "    if (n == (WL))                   /* ★ 比较之后才认「整串就是它」 */",
    "        return 1;                    /* 相对路径（cwd=/etc 时的裸文件名） */",
    "    if (n < (WL) + 1u)",
    "        return 0;                    /* 恒假；只为给 verifier 下界 n>=WL+1 */",
    "    return ax_iszero((__u32)(__u8)s[n - (WL) - 1] ^ (__u32)'/');",
    "}                                    /* 前一个字符是 '/' → 是完整分量 */",
]
NEW = "".join(x.ljust(74) + " \\\n" for x in BODY[:-1]) + BODY[-1]

L[i0:i1 + 1] = NEW.split("\n")
V = "\n".join(L)
wr(P, V)

print("\n---- 宏块现在长这样 ----")
for l in NEW.split("\n"):
    print(l)
