#!/usr/bin/env python3
"""redact.c 返工：open(2) 的接入方式从"抽取 inline 函数"改为"合并分支"。

原因：ax_note_open 被 openat / open 两个调用点 inline 展开 → clang 为每个展开点
分配独立栈槽，而 redact.c 的栈本来就在 512 上限边缘（read 分支的 tmp[AX_TMP=128]
还要展开）→ "BPF stack limit is exceeded"。

新结构：`if (nr == AX_NR_OPENAT || nr == AX_NR_OPEN)` 一个块，
        · 判据只有一份（不会再出现"改了一条忘了另一条"）
        · 缓冲 char p[32] 只有一份 → 栈布局与改动前一致
        · arm64 没有 open(2) → AX_NR_OPEN 就定义成 AX_NR_OPENAT，那边退化成单分支
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAK = "/tmp/axfix_bak"


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


# 从备份恢复 redact.c，重新来
P = "src_agent/bpf/redact.c"
with io.open(os.path.join(BAK, "redact.c"), encoding="utf-8") as f:
    V = f.read()

V = sub(V, """# define AX_NR_OPENAT  257
# define AX_NR_ACCESS  21      /* access(path,mode)：path 在 di */
# define AX_ACCESS_REG di
#elif defined(__TARGET_ARCH_arm64)
# define AX_NR_READ    63
# define AX_NR_CLOSE   57
# define AX_NR_OPENAT  56
# define AX_NR_ACCESS  48      /* arm64 无 access：走 faccessat(dirfd,path,mode)，path 在 si */
# define AX_ACCESS_REG si
""", """# define AX_NR_OPENAT  257
# define AX_NR_OPEN    2       /* ★ x86_64 独有（ISS-054）：`open` 与 `openat` 是**两条
                                *   不同的 syscall**，只挂 openat 时直调 open(2) 的程序
                                *   （静态链接 / musl / 直调 syscall / 我们自己的
                                *   AxReadFileToBuffer）读 preload 完全不被改写 */
# define AX_NR_ACCESS  21      /* access(path,mode)：path 在 di */
# define AX_ACCESS_REG di
# define AX_OPEN_PATH_REG  di  /* open(path,flags,mode) 与 openat(dirfd,path,...) 的
                                *   参数位置不同：open 的 path 在 di、flags 在 si */
# define AX_OPEN_FLAGS_REG si
#elif defined(__TARGET_ARCH_arm64)
# define AX_NR_READ    63
# define AX_NR_CLOSE   57
# define AX_NR_OPENAT  56
# define AX_NR_OPEN    AX_NR_OPENAT   /* arm64 **没有** open(2)：sys_open 就是 openat。
                                       * 定义成同一个号 → 下面那段可以无条件共用，
                                       * 在 arm64 上它退化成原来的单分支 */
# define AX_NR_ACCESS  48      /* arm64 无 access：走 faccessat(dirfd,path,mode)，path 在 si */
# define AX_ACCESS_REG si
# define AX_OPEN_PATH_REG  si
# define AX_OPEN_FLAGS_REG dx
""", "redact/arch")

PRED = r'''/* ── ★ 路径判据：比较**尾部分量**，而不是整串字面（ISS-055）──
 *
 * 字面比较（`ax_streq(p, "/etc/ld.so.preload", 19)`）有一个廉价到离谱的绕过：
 *
 *     /etc/ld.so.preload      ← 只有这一种写法和它对得上
 *     /etc//ld.so.preload     ← 多一个斜杠 → 字面不等 → 不记表 → 读到真内容
 *     /etc/./ld.so.preload    ← 同上
 *     ld.so.preload           ← cwd=/etc 时的相对写法，同上
 *
 * 这四种写法**解析后是同一个文件**。实测（tools/p16_alias_probe.py：同一次运行、
 * 同一个进程、同一个文件）：规范路径被抹成空格，其余四种**全部读到真 preload**。
 * 判据再准也没用 —— 对手只要多敲一个字符就绕过去了。
 *
 * 新判据 = 「路径的最后一个分量 == 目标名」：
 *   · 中间段**完全不看** → `//`、`/./`、`/../`、相对路径全部自动命中；
 *   · 代价是 `/tmp/ld.so.preload` 这类"别处的同名文件"也会被改写 —— 那恰恰是
 *     对手把文件拷出来再读的常见形态，认了比漏了强；这个文件名系统里唯一。
 *
 * 全程定长展开 + 位或累积，无分支（点 5）；长度用有界累加求、不用提前 return
 * —— n 个 return 就是 n 条 verifier 路径（点 7）。 */
#define AX_PL_WORD "ld.so.preload"
#define AX_PL_WLEN 14
#define AX_MD_WORD "modules"
#define AX_MD_WLEN 7

/* 有界求长。调用点的缓冲是 char[32] 且**已清零** → NUL 必在 [0,31]，`& 31` 因此
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

#define AX_TAIL_FN(fn, W, WL)                                              \
static __always_inline __u32 fn(const char *s, __u32 n)                    \
{                                                                          \
    __u32 d = 0;                                                           \
    if (n < (WL))                                                          \
        return 0;                                                          \
    if (n == (WL))                                                         \
        return 1;                    /* 整串就是它（相对路径） */            \
    if (n < (WL) + 1)                                                      \
        return 0;                    /* 恒假；只为给 verifier 下界 n>=WL+1 */\
    _Pragma("unroll")                                                      \
    for (int i = 0; i < (WL); i++)                                         \
        d |= (__u32)(__u8)s[n - (WL) + i] ^ (__u32)(__u8)(W)[i];           \
    if (!ax_iszero(d))                                                     \
        return 0;                    /* 尾部不是那个词 */                   \
    return ax_iszero((__u32)(__u8)s[n - (WL) - 1] ^ (__u32)'/');           \
}                                    /* 前一个字符是 '/' → 是完整分量 */

AX_TAIL_FN(ax_tail_preload, AX_PL_WORD, AX_PL_WLEN)
AX_TAIL_FN(ax_tail_modules, AX_MD_WORD, AX_MD_WLEN)

'''
V = sub(V, "/* Address of the current task's pt_regs (top of its kernel stack). */",
        PRED + "/* Address of the current task's pt_regs (top of its kernel stack). */",
        "redact/pred")

L = V.split("\n")
i0 = next(i for i, l in enumerate(L) if l.strip() == "if (nr == AX_NR_OPENAT) {")
depth, i1 = 0, None
for i in range(i0, len(L)):
    depth += L[i].count("{") - L[i].count("}")
    if depth == 0:
        i1 = i
        break
assert i1, "openat 分支未配对"
old_block = "\n".join(L[i0:i1 + 1])
assert V.count(old_block) == 1, "openat 分支不唯一"

NEW = '''    /* ── openat(2) / open(2)：同一个动作的两种写法，**合并处理** ──
     * ★ ISS-054：x64 上这是两条不同的 syscall（open=2 / openat=257）。只挂
     * openat 时，任何直调 `open(2)` 的程序（静态链接 / musl / 直调 syscall /
     * **我们自己的 AxReadFileToBuffer**）读 preload **完全不被改写** —— 判据再准
     * 也没用，因为那条路径根本没进这个函数（p16 实测：同一次运行里规范路径被
     * 抹成空格，open(2) 读到全文）。arm64 没有 open(2)，AX_NR_OPEN 就等于
     * AX_NR_OPENAT，这一段在那边退化成原来的单分支。
     *
     * 参数位置不同：openat = path@si/flags@dx，open = path@di/flags@si。
     * 判据与缓冲**只有一份** —— 抽取成 inline 函数会让 clang 为两个调用点各
     * 分配一套栈槽，而本文件的栈本来就贴着 BPF 的 512 上限。 */
    if (nr == AX_NR_OPENAT || nr == AX_NR_OPEN) {
        __u64 pathp = 0, flags = 0;
        long fd = args->ret;
        char p[32];
        __u32 pn;
        __u8 kind;

        if (fd < 0)
            return 0;
        if (nr == AX_NR_OPENAT) {
            if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->si) != 0)
                return 0;
            if (bpf_probe_read_kernel(&flags, sizeof(flags), &regs->dx) != 0)
                return 0;
        } else {
            if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->AX_OPEN_PATH_REG) != 0)
                return 0;
            if (bpf_probe_read_kernel(&flags, sizeof(flags), &regs->AX_OPEN_FLAGS_REG) != 0)
                return 0;
        }
        /* 先清零：read_user_str 只写到 NUL 为止，其余字节是栈上残留（ISS-043 同族），
         * 后面的定长比较不先清就会拿残留去比。 */
        __builtin_memset(p, 0, sizeof(p));
        if (bpf_probe_read_user_str(p, sizeof(p), (void *)pathp) < 0)
            return 0;
        pn = ax_len32(p);

        if (ax_tail_preload(p, pn)) {
            __u64 pkey = ((__u64)(__u32)(id >> 32) << 32) | AX_PROBE_FD;
            __u8 *flagp = bpf_map_lookup_elem(&redact_fds, &pkey);

            /* ★ 判据 = 「本进程刚探测过它」**AND**「这条 open 带 O_CLOEXEC」。
             * 只看 O_CLOEXEC 分不开加载器和工具 —— coreutils / python 的 open
             * 一样带它（实测 strace：os.open 与 ld.so 逐字节同形）；只看探测又
             * 挡不住 access()+open() 这种工具写法。两条同时成立才放行。 */
            if (flagp && *flagp == AX_KIND_PROBE && (flags & AX_O_CLOEXEC)) {
                bpf_map_delete_elem(&redact_fds, &pkey);   /* 一次性：消费掉 */
                return 0;                                  /* 不记表 = 不改写 */
            }
            kind = AX_KIND_PRELOAD;
        } else if (ax_tail_modules(p, pn)) {
            kind = AX_KIND_MODULES;     /* /proc/modules 没有豁免问题 */
        } else {
            return 0;
        }

        key = ((__u64)(id >> 32) << 32) | (__u32)fd;
        bpf_map_update_elem(&redact_fds, &key, &kind, BPF_ANY);
        return 0;
    }'''
V = V.replace(old_block, NEW)
wr(P, V)
print("\n[OK] redact.c 已按「合并分支」返工")
