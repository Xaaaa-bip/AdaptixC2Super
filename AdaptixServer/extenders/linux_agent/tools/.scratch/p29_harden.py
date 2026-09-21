# -*- coding: utf-8 -*-
"""p29 入批前的两处加固（都为"入批"服务，不改被测对象）。

① **`settle()`：等 ratelimit 窗口过去再测量。**
   那行文案走 `pr_warn_ratelimited`，而 ratelimit 状态是**按调用点**的 ——
   内核里这行只有 `bpf_get_probe_write_proto()` **一个**调用点 ⇒ **所有** BPF
   程序的这行告警**共用同一个 10 行 / 5 秒的桶**。单跑时桶是满的，所以
   `Δ_redact=10 vs Δ_filter=2` 可复现；但**入批之后**它紧跟在 p15 / p24 / p27
   后面（那三项都加载 redact）⇒ 桶是空的 ⇒ Δ 被限流吃成 1 甚至 0 ⇒ 判别器
   `d_r > d_f` 会**为错误的理由**通过或失败。`___ratelimit()` 的窗口从
   `rs->begin` 起算 `interval`(5s) 复位 ⇒ sleep 6s 后**保证**是一个满桶。

② **A1b：把"上界"也变成断言。**
   原判据只断言 `Δ ≥ 1`（防"根本没加载"），没有断言上界。而"单次加载**至多**
   10 行"这件事**正是我们要写进文档的机制**（限速在给这行文案封顶）—— 所以它
   该被断言，而不只是被叙述。若哪天换个内核、这行不走 ratelimit 了，Δ 会直接
   跳到真实状态数（redact 是几十上百）⇒ 这条会红，而那正是该知道的事。

★ 顺带据实更正文档串里那条已被实测推翻的推论（9.5-① 判定不做）。
"""
import io
import sys

F = "/root/cc/AdaptixServer/extenders/linux_agent/tools/p29_dmesg_budget_verify.py"

PATCHES = [
    (
        "① settle() 函数",
        'def load(a0, mod, pin, attach, names):',
        '''def settle(sec=6.0):
    """等 ratelimit 窗口过去再测量（入批必需）。

    ★ ratelimit 状态是**按调用点**的：内核里这行只有
      `bpf_get_probe_write_proto()` 一个调用点 ⇒ **所有** BPF 程序共用同一个
      10 行 / 5 秒的桶。单跑时桶是满的，所以 Δ_redact=10 / Δ_filter=2 可复现；
      入批后它紧跟在 p15 / p24 / p27（都加载 redact）后面 ⇒ 桶是空的 ⇒
      Δ 被限流吃掉 ⇒ 判别器 `d_r > d_f` 会**为错误的理由**通过或失败。
      `___ratelimit()` 的窗口自 `rs->begin` 起算 interval(5s) 后复位，
      故 sleep 6s 后**保证**是一个满桶。
    """
    time.sleep(sec)


def load(a0, mod, pin, attach, names):''',
    ),
    (
        "② A 相测量前 settle",
        "        n1 = nlines()\n        ok, note = load(a0, \"redact\"",
        "        settle()          # ★ 桶是全局共享的，见函数头注\n        n1 = nlines()\n        ok, note = load(a0, \"redact\"",
    ),
    (
        "③ B 相测量前 settle",
        "        n4 = nlines()\n        ok2, note2 = load(a0, \"bpf_filter\"",
        "        settle()          # ★ 同上：B 相若吃 A 相剩下的桶，判别器就废了\n        n4 = nlines()\n        ok2, note2 = load(a0, \"bpf_filter\"",
    ),
    (
        "④ A1b 上界断言",
        "        rec(\"A1 ★ redact 单次加载的 dmesg 行数（**预算判据的被测值**）\",\n"
        "            d_r >= 1, \"Δ=%d 行（%d → %d）\" % (d_r, n1, n2))",
        "        rec(\"A1 ★ redact 单次加载的 dmesg 行数（**预算判据的被测值**）\",\n"
        "            d_r >= 1, \"Δ=%d 行（%d → %d）\" % (d_r, n1, n2))\n"
        "        # ★ 上界同样是要断言的机制：ratelimit 突发上限 10 行/5 秒\n"
        "        #   ⇒ 单次加载**至多** 10 行。若换个内核、这行不再走 ratelimit，\n"
        "        #   Δ 会跳到真实状态数（redact 是几十上百）⇒ 这条会红，而那是该知道的事。\n"
        "        rec(\"A1b ★ 单次加载 ≤ ratelimit 突发上限 10 行（= 消息确实被限速封顶）\",\n"
        "            d_r <= 10, \"Δ=%d 行（>10 = 限速没了，dmesg 的账将按真实状态数爆）\" % d_r)",
    ),
    (
        "⑤ 文档串：记录机制 + 据实更正被推翻的推论",
        '  满足 —— 那正是 ISS-083 那条纪律的形态。\n"""',
        '''  满足 —— 那正是 ISS-083 那条纪律的形态。

★★ 2026-09-16 据实补记（**结论被实测推翻，判据保留、叙述更正**）：
  这条判据原本是为了决定"9.5-① 该不该做（把写回提出循环）"而立的，B2 通过
  时脚本会打印"有行数收益，值得冒一次 verifier 风险"。**实测把这个结论推翻了**：
  把写回目标从**用户指针**换成**栈副本**（偏移随 p 变）⇒ 循环头状态永不合并
  ⇒ `Processed 1000001 insn` ⇒ **9.5-① 判定不做**。
  而同一次查证还发现原推论**误读了那 9 行**：它来自
  `bpf_get_probe_write_proto()` 里的 `pr_warn_ratelimited`，那 9/10 是
  **限速的突发上限**（同窗口 `callbacks suppressed 10718` ⇒ 真实次数远大于 10），
  **不是** verifier 状态数本身。⇒ 判据本身仍然有价值（它测"这行文案的 dmesg
  预算有没有变"），但**不能再拿它推"改代码能省行数"** —— 收益为零：调用点与
  `taint` 位都在，检测面一个字节没变。
"""''',
    ),
]


def main():
    src = io.open(F, encoding="utf-8").read()
    for tag, old, new in PATCHES:
        n = src.count(old)
        print("%-24s 命中: %d" % (tag, n))
        if n != 1:
            print("ABORT：未命中或不唯一，不写盘")
            return 1
        src = src.replace(old, new)
    io.open(F, "w", encoding="utf-8").write(src)
    print("OK 已写入")
    # 眼看一遍改动区（纯文本，不做过滤）
    txt = io.open(F, encoding="utf-8").read().splitlines()
    for anchor in ("def settle", "settle()          # ★ 桶"):
        for i, ln in enumerate(txt):
            if anchor in ln:
                print("\n--- @%d ---" % (i + 1))
                for j in range(max(0, i - 1), min(i + 6, len(txt))):
                    print("%4d| %s" % (j + 1, txt[j]))
                break
    print("\nA1b 在吗: %s" % any("A1b" in l for l in txt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
