#!/usr/bin/env python3
"""redact.c 返工 2：删掉 ax_len32，改用 bpf_probe_read_user_str 的返回值取路径长度。

clang 报错首行直指 redact.c:233:22（ax_len32 的循环体）—— 32 步展开的活跃变量
（n / done / z）把寄存器打满，溢出到栈，而本文件的栈本来就贴着 BPF 的 512 上限
（read 分支还有 tmp[AX_TMP=128] 要展开）。

其实长度**本来就在手里**：`bpf_probe_read_user_str(dst, size, src)` 的返回值就是
"含 NUL 的写入长度"（负值才是错误），原代码只判了 `< 0` 就把它扔了 —— 于是要另外
用 32 步循环重新数一遍。直接用它，零额外开销、零寄存器压力。
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
    print("  [ok] %s → %d 行" % (p, s.count("\n") + 1))


def sub(V, old, new, tag):
    c = V.count(old)
    assert c == 1, "[%s] 锚点命中 %d 次（要求 1）" % (tag, c)
    return V.replace(old, new)


P = "src_agent/bpf/redact.c"
V = rd(P)

V = sub(V, """/* 有界求长。调用点的缓冲是 char[32] 且**已清零** → NUL 必在 [0,31]，`& 31` 因此
 * 无损；它唯一的用处是给 verifier 一个明确上界（否则下面 s[n-WL+i] 的变长索引
 * 它证不出来）。 */
static __always_inline __u32 ax_len32(const char *s)
{
    __u32 n = 0, done = 0;

    #pragma unroll
    for (int i = 0; i < 32; i++) {
        __u32 z = ax_iszero((__u32)(__u8)s[i]);
        n += (~done) & (z ^ 1u);
        done |= z;
    }
    return n & 31u;
}

""", """/* 路径长度**不去数**：`bpf_probe_read_user_str` 的返回值就是"含 NUL 的写入长度"
 * （只有负值才是错误）。最早写过一个 32 步的零分支 ax_len32 去自己数，clang 直接
 * 报 "BPF stack limit is exceeded" 并把行号指到它的循环体上 —— 32 步展开的活跃
 * 变量（n / done / z）把寄存器打满、溢出到栈，而本文件的栈本来就贴着 512 上限
 * （read 分支还有 tmp[AX_TMP=128] 要展开）。
 *
 * 于是判据函数的入参 n 由调用点从 helper 返回值取，并**显式钳到 [0,31]**：
 * `pn > 31u` 那条既是真检查（超过缓冲就没有"尾部"可言），也是 verifier 唯一
 * 能拿到 s[n-WL+i] 变长索引上界的地方。 */

""", "redact/del-len32")

V = sub(V, """        __u64 pathp = 0, flags = 0;
        long fd = args->ret;
        char p[32];
        __u32 pn;
        __u8 kind;
""", """        __u64 pathp = 0, flags = 0;
        long fd = args->ret;
        char p[32];
        int slen;
        __u32 pn;
        __u8 kind;
""", "redact/decl")

V = sub(V, """        __builtin_memset(p, 0, sizeof(p));
        if (bpf_probe_read_user_str(p, sizeof(p), (void *)pathp) < 0)
            return 0;
        pn = ax_len32(p);
""", """        __builtin_memset(p, 0, sizeof(p));
        slen = (int)bpf_probe_read_user_str(p, sizeof(p), (void *)pathp);
        if (slen < 0)
            return 0;
        pn = (__u32)(slen - 1);     /* helper 返回含 NUL 的长度 */
        if (pn > 31u)               /* 无符号比较：同时排掉 slen==0，并给 verifier 上界 */
            return 0;
""", "redact/call")

wr(P, V)
print("\n[OK] ax_len32 已删除，改用 helper 返回值（期望 insn 数比改动前还少）")
