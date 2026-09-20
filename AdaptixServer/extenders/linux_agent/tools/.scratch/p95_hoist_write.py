# -*- coding: utf-8 -*-
"""P9-9.5-①：把 ax_scan 的 bpf_probe_write_user **提出 p 循环**（改整块一次性写回）。

机制（本次 dmesg 实测查清，同时写进 redact.c 头注 §已知边界）：
  告警来自 bpf_get_probe_write_proto()，由 **verifier** 在解析该 CALL 指令的原型时
  打，走 pr_warn_ratelimited（突发上限 10 行 / 5 秒）⇒ 行数 ∝ **到达该 CALL 的
  verifier 状态数**，且**被截顶**。实测：redact 的调用点埋在 96 次 p 循环内 ⇒
  Δ=10 行 & 同窗口 suppressed 10718；bpf_filter 直线代码、同样 1 个调用点 ⇒ Δ=2 行。

改法：命中时把抹平后的 raw **就地累积**进栈副本 tmp（经 helper，断溯源），
      循环外**唯一**一次 bpf_probe_write_user(dst, tmp, n)。
      未命中区段 = read 分支刚读进来的原值 ⇒ 写回幂等；
      命中区段已在 tmp 里抹成空格。副产品：ISS-083 那三行尾块回钳**退役**。

语义等价（两条，已写进头注）：
  ① raw 未命中的字节与 tmp[p..p+32) 逐字节相同（mm=0 ⇒ raw[j]=c）⇒ 幂等；
  ② 就地抹平后后面的窗口读到空格（32 < 33 ⇒ run 当场归零）⇒ 只会**缩短**候选词，
     绝造不出假命中；真命中也不丢：已抹平区段永远落在**同一个词**内（词 W 起于 q
     时，覆盖到 q 的窗口 p 必有 p < q < r，即 q 在 W 内部 ⇒ q 本就不是词首）。
  ⇒ 匹配集合与写回结果逐字节不变。

每处替换都断言唯一命中，否则不写盘。
"""
import io
import re
import sys

F = "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bpf/redact.c"
src = io.open(F, encoding="utf-8").read()
n_ok = 0

# ── 1) ax_scan：写回块 → 就地累积 + 循环外唯一写回 ──────────────────────────
# 用正则吃掉中间那段长注释（改法会连带退役它），锚点取块首块尾的代码行。
pat = re.compile(
    r"\n        if \(res && \(__u32\)p < \(__u32\)n\) \{.*?"
    r"bpf_probe_write_user\(dst \+ p, \(__u8 \*\)raw, w\);"
    r"\n        \}\n    \}\n\}\n", re.S)

NEW1 = '''
        res = bpf_map_lookup_elem(&hide_names, kb);
        /* ★★★ 9.5-①：命中后**不在这里写用户内存**，就地累积进栈副本 tmp。
         *
         * 为什么值得改 —— dmesg 那行告警的机制（实测查清，见头注 §已知边界）：
         *   它来自 **bpf_get_probe_write_proto()**，由 **verifier** 在**解析该 CALL
         *   指令的函数原型时**打，走 pr_warn_ratelimited（突发上限 10 行/5 秒）。
         *   所以行数 ∝ **到达这个 CALL 的 verifier 状态数**，与运行期调用次数
         *   **无关**。调用点埋在 96 次 p 循环里 ⇒ 状态多 ⇒ 实测 Δ=10 行（同窗口
         *   suppressed 10718 ⇒ 真实次数被 ratelimit 截顶）；直线代码的 bpf_filter
         *   同样只有 1 个调用点，Δ 只有 2 行。把 CALL 提到循环外 ⇒ 状态收敛成
         *   1 个 ⇒ 行数与 suppressed 一起降。
         *
         * 为什么用 helper 而不是直接 `tmp[p + j] = raw[j]`：
         *   本文件已有一条**同形**先例（下面 kb ← km 那一行）—— helper 写栈无
         *   寄存器溯源，map key 的精确链断在 helper 处（头注点 7）。helper 的
         *   dst 是 ARG_PTR_TO_UNINIT_MEM，只需证明 [tmp+p, tmp+p+32) 落在栈内，
         *   而 p <= AX_SCAN-AX_CHUNK-1 = 95 由循环上界直接给出 —— 可证。
         *
         * ★ 语义等价（两条，都写进头注 §已知边界）：
         *   ① raw 里**未命中**的字节与 tmp[p..p+32) 逐字节相同（mm=0 ⇒ raw[j]=c）
         *      ⇒ 这次拷贝对未命中区段是幂等的；
         *   ② 就地抹平后，后面的窗口读到的是空格 —— 空格(32) < 33 ⇒ run 当场归零
         *      ⇒ 只会**缩短**候选词，绝造不出假命中；真命中也不会丢：已抹平的区段
         *      永远落在**同一个词**内（词 W 起于 q 时，覆盖到 q 的窗口 p 必有
         *      p < q < r，即 q 在 W 内部 ⇒ q 本来就不是词首）。
         *   两条合起来 ⇒ 匹配集合与写回的**逐字节结果**都不变。 */
        if (res && (__u32)p < (__u32)n) {
            bpf_probe_read_kernel((void *)(tmp + p), AX_CHUNK, (const void *)raw);
            hit = 1;
        }
    }

    /* ★★★ 9.5-①：循环外**唯一**一次写回，整块 [0, n)。
     * 未命中区段就是 read 分支刚读进来的原值 ⇒ 写回幂等；命中区段已在 tmp 里
     * 被抹成空格。这同时**退役了 ISS-083 那三行尾块回钳** —— 写回长度现在由 n
     * 本身给出，天然落在**已返回的**字节内，不再存在 `dst + p` 越界的可能。
     *
     * ★ 栈边界对 verifier 可证：AX_TMP == AX_SCAN == 128（见其定义处两条不变量），
     *   而 n 在 read 分支已钳到 AX_SCAN ⇒ umax(wn) = 128 = sizeof(tmp)。
     * ★ `wn < 1` 与原 `w < 1u` 同族：**运行期死代码**（read 分支 `n <= 0` 早退
     *   已保证 n >= 1），唯一作用是把 umin 递给 helper 的 ARG_CONST_SIZE。
     *   ★ 这一格 clang 看不见 —— 能编译不等于能加载，build_bpf.sh 的 verifier
     *   门禁一步都不能省。 */
    if (hit) {
        __u32 wn = (__u32)n;

        if (wn > AX_TMP)
            wn = AX_TMP;
        if (wn < 1u)
            wn = 1u;
        bpf_probe_write_user(dst, (__u8 *)tmp, wn);
    }
}
'''
cnt = len(pat.findall(src))
print("1) ax_scan 写回块命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 1（未命中或不唯一）")
src = pat.sub(lambda _m: NEW1, src, count=1)
n_ok += 1

# ── 2) 声明 hit（就在 p 循环前）────────────────────────────────────────────
OLD2 = '''    #pragma clang loop unroll(disable)
    for (int p = 0; p < AX_SCAN - AX_CHUNK; p++) {'''
NEW2 = '''    /* ★ 9.5-①：本次扫描有没有命中过。纯寄存器标量，**不在** map key 的精确链上
     * （不进 kb/km），所以不会引入头注点 7 那种"每个位置精度位都不同 ⇒ 循环头
     * 状态永不合并"的维度 —— 那正是 ISS-084 两次 1000001 insn 的成因。 */
    int hit = 0;

    #pragma clang loop unroll(disable)
    for (int p = 0; p < AX_SCAN - AX_CHUNK; p++) {'''
cnt = src.count(OLD2)
print("2) hit 声明锚点命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 2（未命中或不唯一）")
src = src.replace(OLD2, NEW2, 1)
n_ok += 1

# ── 3) 头注 §已知边界：修掉过期的那条（ISS-083 已放开门槛）──────────────────
OLD3 = '/// · read 返回值 n < AX_CHUNK(32) 时整窗放弃（写回必须落在**已返回的**字节内）'
NEW3 = '''/// · read 返回值 n <= 0 时才整窗放弃 —— ISS-083 把门槛从 `n < AX_CHUNK(32)` 放开：
///   `/etc/ld.so.preload` 最常见的形态就是"只有我们写的一行短路径"（19 字节），
///   旧门槛让内容层对**最需要它的那个形态** 100% 不设防。写回长度现在由 n 本身
///   给出（9.5-① 之后是整块写回 [0, n)），天然落在**已返回的**字节内'''
cnt = src.count(OLD3)
print("3) 过期边界行命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 3（未命中或不唯一）")
src = src.replace(OLD3, NEW3, 1)
n_ok += 1

# ── 4) 头注 §已知边界：新增两条（就地抹平的等价性 / dmesg 告警的真实性质）────
OLD4 = '/// · 跨扫描窗口边界的残词匹配不到（窗口内完整成词的才处理）'
NEW4 = OLD4 + '''
/// · **ax_scan 的抹平是就地的**（9.5-①）：命中区段在**栈副本** tmp 里累积，循环外
///   一次性写回 [0, n)。未命中区段是原值 ⇒ 写回幂等；就地抹平只会让后面的窗口读到
///   空格（32 < 33 ⇒ run 立刻归零）⇒ 造不出假命中，也丢不掉真命中（已抹平的区段
///   永远落在**同一个词**内）。★ 代价：一次读里只要有命中，就多一次 helper 调用
///   （栈→栈 probe_read_kernel）；无命中时一次都不多。
/// · **dmesg 那行告警是本模块最硬的 IOC 之一，但两条性质必须读对**：
///   ① 来自 `bpf_get_probe_write_proto()`，由 **verifier** 在解析该 CALL 的原型时
///      打，`pr_warn_ratelimited`（突发 **10 行 / 5 秒**）⇒ 行数 ∝ **到达该 CALL 的
///      状态数**、且**被截顶**，不能当"调用次数"读。实测：redact（调用点在 96 次
///      循环内）Δ=10 行 & 同窗口 `suppressed 10718`；bpf_filter（直线代码，同样
///      1 个调用点）Δ=2 行。9.5-① 把调用点提出循环就是压这个数。
///   ② 它印的是 `current->comm` ⇒ 落到我们**伪装过的 argv 名**上（实测
///      `kworker/0:0H-e` / `rcu_tasks_rude`），**不泄漏真实载荷名**。
///   ★ 三条**做不到**（不是"没做"）：`taint` 位不可清除（实测 12288 恒定）；
///   `bpf_probe_write_user` 在内核里**无替代品**；ring buffer 本体在内核内存里，
///   而这个 helper 只写**用户**内存 ⇒ 抹不掉。⇒ 唯一真实对策是**减少加载次数**。'''
cnt = src.count(OLD4)
print("4) 新边界插入锚点命中: %d" % cnt)
if cnt != 1:
    sys.exit("ABORT 4（未命中或不唯一）")
src = src.replace(OLD4, NEW4, 1)
n_ok += 1

io.open(F, "w", encoding="utf-8").write(src)
print("OK 4/4 已写入: %s（%d → %d 字节）" % (F, len(io.open(F, encoding="utf-8").read()) - 0, 0) if False else "OK %d/4 已写入: %s" % (n_ok, F))
