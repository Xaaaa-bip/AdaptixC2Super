# -*- coding: utf-8 -*-
"""P9-9.5-① 回滚：把 ax_scan 的写回恢复到"循环内写用户内存"。

verifier 硬证据（build_bpf.sh 门禁实测，见 .scratch/p95_build.log）：
  就地累积（dst = 栈 tmp、偏移随 p 变）⇒ 每个迭代写到的栈槽位不同 ⇒
  栈状态指纹逐迭代不同 ⇒ 循环头状态永不合并 ⇒
  `BPF program is too large. Processed 1000001 insn`
  （max_states_per_insn 48 / total_states 19587 / peak_states 2671）

原写法能过**恰恰因为** dst 是用户指针（ARG_ANYTHING）⇒ verifier 不跟踪。

回滚范围：只回滚 ax_scan 的函数体（改动 1 + 2）。
头注的文档收获（改动 3 + 4）**保留** —— 那是本次真正的产出。
"""
import io
import re
import sys

F = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bpf/redact.c"
src = io.open(F, encoding="utf-8").read()

# ── 1) 就地累积块 → 恢复循环内写回（带 9.5-① 判定结论）─────────────────────
pat = re.compile(r"\n        /\* ★★★ 9\.5-①：命中后.*?\n    \}\n\}\n", re.S)

NEW1 = '''
        /* ★★★ 9.5-①（**判定：不做**，2026-09-16）—— "把写回提出循环 / 就地累积"
         * 这条路在 verifier 上是**死的**，所以本处保持原样。硬证据（build_bpf.sh
         * 的 verifier 门禁实测）：
         *   · 原写法能过，**恰恰因为** bpf_probe_write_user 的 dst 是**用户指针**
         *     （ARG_ANYTHING）⇒ verifier **完全不跟踪**它 ⇒ 96 次迭代的循环头
         *     状态能合并。
         *   · 换成"把抹平后的 raw 就地累积进栈副本 tmp"（dst = 栈、偏移随 p 变）
         *     后，每个迭代**写到的栈槽位不同** ⇒ 栈状态指纹逐迭代不同 ⇒ 循环头
         *     状态永不合并 ⇒ `Processed 1000001 insn`
         *     （max_states_per_insn 48 / total_states 19587 / peak_states 2671）。
         *   · 收益侧也量过了：dmesg 那行告警由 bpf_get_probe_write_proto() 按
         *     **到达该 CALL 的状态数**打、且被 pr_warn_ratelimited 截顶（10 行
         *     / 5 秒）⇒ 10 行降到 ~2 行，在本机**已有的 1840 行**同类告警里
         *     **不可见**；而 taint 位与调用点本身都还在 ⇒ **检测面一个字节没变**。
         * ⇒ 真实对策是**减少加载次数**（redact 一次部署只加载一次），不是压行数。
         *   两条立此存照：**这里的 dst 必须是用户指针，换成栈就 1000001**。 */
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
            /* ★★ verifier **不认识 `p < n` 这个"关系"** —— 它只追每个标量各自
             * 的取值范围，不做两个标量之间的减法推理。所以上面那道守卫虽然
             * 逻辑上保证了 `w >= 1`，verifier 眼里 `n - p` 仍可能是 0。首轮
             * PROG_LOAD 就是这么拒的：
             *     R3 invalid zero-sized read: u64=[0,31]
             * umin 下不到 1 就过不了 helper 的 ARG_CONST_SIZE。
             * 下面这条 `w < 1` 在**运行期是死代码**（守卫 ⇒ w ≥ 1），它的唯一
             * 作用是把 umin 递给 verifier。★ 这一格 clang 看不见 —— 能编译
             * 不等于能加载，所以 build_bpf.sh 的 verifier 门禁一步都不能省。 */
            if (w > AX_CHUNK)
                w = AX_CHUNK;
            if (w < 1u)
                w = 1u;
            bpf_probe_write_user(dst + p, (__u8 *)raw, w);
        }
    }
}
'''
cnt = len(pat.findall(src))
print("1) 就地累积块命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 1（未命中或不唯一）")
src = pat.sub(lambda _m: NEW1, src, count=1)

# ── 2) 撤掉 hit 声明 ────────────────────────────────────────────────────
OLD2 = '''    /* ★ 9.5-①：本次扫描有没有命中过。纯寄存器标量，**不在** map key 的精确链上
     * （不进 kb/km），所以不会引入头注点 7 那种"每个位置精度位都不同 ⇒ 循环头
     * 状态永不合并"的维度 —— 那正是 ISS-084 两次 1000001 insn 的成因。 */
    int hit = 0;

'''
cnt = src.count(OLD2)
print("2) hit 声明命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 2（未命中或不唯一）")
src = src.replace(OLD2, "", 1)

# ── 3) 头注：把"就地抹平"那条改成"判定不做 + 硬证据" ──────────────────────
OLD3 = '''/// · **ax_scan 的抹平是就地的**（9.5-①）：命中区段在**栈副本** tmp 里累积，循环外
///   一次性写回 [0, n)。未命中区段是原值 ⇒ 写回幂等；就地抹平只会让后面的窗口读到
///   空格（32 < 33 ⇒ run 立刻归零）⇒ 造不出假命中，也丢不掉真命中（已抹平的区段
///   永远落在**同一个词**内）。★ 代价：一次读里只要有命中，就多一次 helper 调用
///   （栈→栈 probe_read_kernel）；无命中时一次都不多。'''
NEW3 = '''/// · **9.5-① 判定：不做**（"把写回提出 p 循环 / 就地累积"）。这条路在 verifier 上
///   是**死的**，且收益在有界侧：
///   ① **verifier 硬证据**：原写法能过恰恰因为 bpf_probe_write_user 的 dst 是
///      **用户指针**（ARG_ANYTHING）⇒ verifier 不跟踪 ⇒ 96 次迭代的循环头状态能
///      合并；改成写**栈副本 tmp**（偏移随 p 变）后，每迭代写到的栈槽位不同 ⇒
///      栈状态指纹逐迭代不同 ⇒ 循环头状态永不合并 ⇒ 实测
///      `Processed 1000001 insn`（max_states_per_insn 48 / total_states 19587）。
///      ⇒ **这条纪律比本次改动值钱：ax_scan 里任何"写内存"的目标都必须留在
///      用户侧；换成栈（尤其变偏移）就是 1000001。**
///   ② **收益侧**：10 行 → ~2 行，而本机已有 1840 行同类告警 + suppressed 上万
///      ⇒ 信噪比上不可见；taint 位与调用点本身都还在 ⇒ 检测面不变。
///   ⇒ 真实对策是**减少加载次数**（redact 一次部署只加载一次），不是压行数。'''
cnt = src.count(OLD3)
print("3) 头注就地抹平条命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 3（未命中或不唯一）")
src = src.replace(OLD3, NEW3, 1)

io.open(F, "w", encoding="utf-8").write(src)
print("OK 3/3 已回滚函数体、保留头注收获")
