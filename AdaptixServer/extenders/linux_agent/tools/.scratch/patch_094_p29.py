#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ISS-094 修法：p29_dmesg_budget_verify 的 A1/B1/B2 —— 给**可观测量本身**加阳性对照。

问题（本批实测）：A1 断言 `Δ可见行 ≥ 1`、B2 断言 `Δ_redact > Δ_filter`，而
`Δ可见行` 取决于**内核 ratelimit 桶满不满**（全局共享 10 行/5 秒），与 redact 无关。
桶被抽干 ⇒ 行全被吞成 `N callbacks suppressed` ⇒ Δ 恒为 0 ⇒ **为错误的理由红**。

修法（不是改阈值）：
  ① A 相最多试 3 轮（每轮前 settle 复位桶）；全为 0 ⇒ **SKIP**（不可测），不 FAIL。
  ② B1/B2 同样：Δ=0 一律 SKIP 而不是 FAIL。
  ③ 新增 `skip()` 第三态；并加硬约束 **SKIP ≥ 3 ⇒ 整项判 FAIL**（防"全跳过=绿"）。

★ 行区间补丁（自底向上替换，行号不漂移）；每段替换前先校验原文指纹。
用法: python3 tools/.scratch/patch_094_p29.py [--check]
"""
import io
import os
import sys

P = "/root/cc/AdaptixServer/extenders/linux_agent/tools/p29_dmesg_budget_verify.py"
CHECK = "--check" in sys.argv

REC = '''def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%s] %s   %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def skip(item, note=""):
    """**不可测** —— 既不是通过也不是失败（ISS-094）。

    ★ 只允许用在「可观测量本身失效」的场合，且必须写明理由：拿 SKIP
      掩盖失败等于把门拆了。故收尾处还有一条硬约束：**SKIP ≥ 3 时整项判 FAIL**（没结论）。
    """
    R.append(None)
    print("  [SKIP] %s   %s" % (item, note), flush=True)'''

A_PHASE = '''        # ── ★★ ISS-094：桶不新 ⇒ 本窗口**不可测**，绝不当 FAIL ─────────
        #    ratelimit 桶被抽干时，加载产生的行**全部**被吞成
        #    `N callbacks suppressed` ⇒ Δ可见行 恒为 0 —— 而这是**内核的状态**、
        #    不是 redact 的属性。故：最多试 3 轮（每轮前 settle 复位桶），
        #    全为 0 ⇒ SKIP（写明理由），而不是红。
        d_r, sup_r, tries = 0, 0, 0
        for attempt in range(3):
            if attempt:
                unload(a0, PINR)
                loaded = None
                settle(8.0)
                n1 = nlines()
            ok, note = load(a0, "redact", PINR, "raw_syscalls/sys_exit",
                            (MARKER + "\\x00").encode())
            if attempt == 0:
                rec("A0 ★★ 阳性对照：redact 真的加载成功（否则下面'没涨'不作数）",
                    ok, note)
                if not ok:
                    return 1
            loaded = PINR
            time.sleep(1)
            n2 = nlines()
            d_r = n2 - n1
            sup_r = nsuppressed() - 0
            tries = attempt + 1
            if d_r >= 1:
                break
        if d_r >= 1:
            rec("A1 ★ redact 单次加载的 dmesg 行数（**预算判据的被测值**）",
                True, "Δ=%d 行（%d → %d），第 %d 轮" % (d_r, n1, n2, tries))
        else:
            skip("A1 ★ redact 单次加载的 dmesg 行数（**预算判据的被测值**）",
                 "连试 %d 轮 Δ 可见行都是 0 ⇒ **桶不新**（行被限速全吞）⇒ "
                 "本窗口不可测，非产品缺陷（ISS-094）" % tries)
        # ★ 上界同样是要断言的机制：ratelimit 突发上限 10 行/5 秒
        #   ⇒ 单次加载**至多** 10 行。若换个内核、这行不再走 ratelimit，
        #   Δ 会跳到真实状态数（redact 是几十上百）⇒ 这条会红，而那是该知道的事。
        rec("A1b ★ 单次加载 ≤ ratelimit 突发上限 10 行（= 消息确实被限速封顶）",
            d_r <= 10, "Δ=%d 行（>10 = 限速没了，dmesg 的账将按真实状态数爆）" % d_r)'''

B1 = '''            if d_f >= 1:
                rec("B1 bpf_filter 单次加载的 dmesg 行数", True,
                    "Δ=%d 行（%d → %d）" % (d_f, n4, n5))
            else:
                skip("B1 bpf_filter 单次加载的 dmesg 行数",
                     "Δ=0 ⇒ 桶不新，本窗口不可测（ISS-094）")'''

B2 = '''            if d_r < 1:
                skip("B2 ★ 判别器（叙述据实更正）：redact(循环) 的告警行数 **多于** "
                     "bpf_filter(直线)——两者源码调用点数相同（都是 1）",
                     "Δ_redact 不可测（桶不新）⇒ 判别器**无结论**；Δ_filter=%d" % d_f)
            else:
                rec("B2 ★ 判别器（叙述据实更正）：redact(循环) 的告警行数 **多于** "
                    "bpf_filter(直线)——两者源码调用点数相同（都是 1）",
                    d_r > d_f, "Δ_redact=%d vs Δ_filter=%d" % (d_r, d_f))'''

RET = '''        return 1 if any(x is False for x in R) else 0'''

SUM = '''        ns = sum(1 for x in R if x is None)
        nf = sum(1 for x in R if x is False) + (1 if ns >= 3 else 0)
        print("\\n[warn] 判据 = %d/%d 通过，%d SKIP%s"
              % (sum(1 for x in R if x), len(R), ns,
                 "  ⇒ ★ SKIP≥3 = **本项无结论**，按 FAIL 计" if ns >= 3 else ""),
              flush=True)
        print("RESULT: %s" % ("PASS" if R and nf == 0 else "FAIL"), flush=True)'''

# (起, 讫, 原文指纹子串, 新文, 标签)  —— 1-based 闭区间
HUNKS = [
    (206, 207, 'print("RESULT: %s"', SUM, "F 收尾摘要"),
    (198, 198, "return 0 if all(R) else 1", RET, "E 返回值"),
    (174, 177, 'rec("B2 ★ 判别器', B2, "D B2 条件化"),
    (170, 171, 'rec("B1 bpf_filter', B1, "C B1 条件化"),
    (134, 151, 'ok, note = load(a0, "redact"', A_PHASE, "B A 相重试+SKIP"),
    (58, 60, 'def rec(item, ok, note=""):', REC, "A skip() 第三态"),
]

src = io.open(P, encoding="utf-8", newline="").read()
lines = src.split("\n")

for a, b, fp, new, tag in HUNKS:
    seg = "\n".join(lines[a - 1:b])
    if fp not in seg:
        print("[ABORT] %s: 行 %d-%d 指纹不匹配，实际前 60 字：%s"
              % (tag, a, b, seg[:60].encode("ascii", "backslashreplace").decode()))
        sys.exit(2)
    print("[ok] %-16s 行 %d-%d（%d 行 → %d 行）"
          % (tag, a, b, b - a + 1, len(new.split("\n"))))
    lines[a - 1:b] = new.split("\n")

if not CHECK:
    io.open(P, "w", encoding="utf-8", newline="").write("\n".join(lines))
    print("\n已写入 %s" % P)
else:
    print("\n--check：未写入")
