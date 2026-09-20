#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ISS-094 收尾修复：还原 nsuppressed + 汇总移出 finally。

① patch_094b 用 `def nsuppressed():` 这**一行头**当锚点，把它的**函数体**留在了
   后面 ⇒ 函数体被并进新插入的 `def settle`，`nsuppressed` 消失、`settle` 定义了
   两次。教训：锚点取"函数头一行"时，必须把**它后面紧邻的函数体首行**一起纳入
   锚点，否则替换会撕开一个函数。（快速判据：锚点跨越结构边界时，宁可多带一行。）

② 崩溃时 `finally` 里的汇总**仍然打印 `RESULT: PASS`**（只有进程 rc 还是 1）。
   而批次与人**都是读日志里的 `RESULT:`** ⇒ 这是一次真实的假绿形态，与 p31
   （`return` 落在 `finally` 里吞异常）同族。修法同 p31：异常在 `except` 里记一条
   FAIL，汇总移到 `try/finally` **之外**。

用法: python3 tools/.scratch/patch_094c_p29_final.py [--check]
"""
import io
import os
import sys

T = "/root/cc/AdaptixServer/extenders/linux_agent"
P = os.path.join(T, "tools/p29_dmesg_budget_verify.py")
CHECK = "--check" in sys.argv

S1_OLD = '''def settle(sec=6.0):
    out = sh("dmesg 2>/dev/null | grep -c 'callbacks suppressed'").strip()
'''
S1_NEW = '''def nsuppressed():
    out = sh("dmesg 2>/dev/null | grep -c 'callbacks suppressed'").strip()
'''

S2_OLD = '''        rec("C1 回归：beacon 仍通（getuid 回执）", p15.getuid_ok(a0))
        return 1 if any(x is False for x in R) else 0
    finally:
        if loaded:
            unload(a0, loaded)
        p15.kill(pid0)
        taint = sh("cat /proc/sys/kernel/tainted").strip()
        print("[env] tainted=%s（本判据不 insmod，故不应由它改变）" % taint,
              flush=True)
        ns = sum(1 for x in R if x is None)
        nf = sum(1 for x in R if x is False) + (1 if ns >= 3 else 0)
        print("\\n[warn] 判据 = %d/%d 通过，%d SKIP%s"
              % (sum(1 for x in R if x), len(R), ns,
                 "  ⇒ ★ SKIP≥3 = **本项无结论**，按 FAIL 计" if ns >= 3 else ""),
              flush=True)
        print("RESULT: %s" % ("PASS" if R and nf == 0 else "FAIL"), flush=True)
'''

S2_NEW = '''        rec("C1 回归：beacon 仍通（getuid 回执）", p15.getuid_ok(a0))
    except Exception as e:
        # ★★ 台面自己出事**绝不能**被打成 PASS。汇总原先写在 `finally` 里，于是
        #    崩溃时**仍然打印 `RESULT: PASS`**（只有进程 rc 还是 1）—— 而批次与人
        #    都是读日志里的 `RESULT:`。与 p31 同族（`return` 落在 `finally` 里吞
        #    异常），修法也同 p31：异常在此记一条 FAIL，汇总移到 try/finally 之外。
        rec("C★ 台面跑完（未被异常中断）", False,
            "%s: %s" % (type(e).__name__, str(e)[:70]))
    finally:
        if loaded:
            unload(a0, loaded)
        p15.kill(pid0)
        taint = sh("cat /proc/sys/kernel/tainted").strip()
        print("[env] tainted=%s（本判据不 insmod，故不应由它改变）" % taint,
              flush=True)

    # ── 汇总放在 try/finally **之外** ──────────────────────────────────────
    #    异常不再被吞，也不会在崩溃时打出假的 `RESULT:`。
    ns = sum(1 for x in R if x is None)
    nf = sum(1 for x in R if x is False) + (1 if ns >= 3 else 0)
    if len(R) < 9:          # ★ 条数自证：跑不满就是"判据没跑"，不是"判据通过"
        nf += 1
        print("  [FAIL] D★ 判据自证跑满 9 条（没被静默跳过）   实收 %d 条" % len(R),
              flush=True)
    print("\\n[warn] 判据 = %d/%d 通过，%d SKIP%s"
          % (sum(1 for x in R if x), len(R), ns,
             "  ⇒ ★ SKIP≥3 = **本项无结论**，按 FAIL 计" if ns >= 3 else ""),
          flush=True)
    print("RESULT: %s" % ("PASS" if R and nf == 0 else "FAIL"), flush=True)
    return 1 if nf else 0
'''

PEND = None
BAD = []


def sub(old, new, tag):
    global PEND
    if PEND is None:
        PEND = io.open(P, encoding="utf-8", newline="").read()
    c = PEND.count(old)
    if c != 1:
        if PEND.count(new) >= 1:
            print("[done] %-26s 已是目标态，跳过" % tag)
            return
        print("[BAD ] %-26s 锚点命中 %d 次（要求 1）" % (tag, c))
        BAD.append(tag)
        return
    PEND = PEND.replace(old, new)
    print("[ok  ] %-26s" % tag)


sub(S1_OLD, S1_NEW, "还原 nsuppressed")
sub(S2_OLD, S2_NEW, "汇总移出 finally")

print("\n%s" % ("（--check，未写入）" if CHECK else ""))
if BAD:
    print("未命中: %s ⇒ 一个字节都没写" % ", ".join(BAD))
    sys.exit(2)
if not CHECK:
    io.open(P, "w", encoding="utf-8", newline="").write(PEND)
    print("已写入 %s" % P)
