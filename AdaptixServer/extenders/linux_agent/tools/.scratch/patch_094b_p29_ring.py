#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ISS-094 的第二层修法：**改观测量本身**（清环），而不只是改断言语义。

第一层（patch_094_p29.py）已把 A1/B1/B2 从 FAIL 改成 SKIP 并加了 SKIP 封顶。
但实测暴露了更根本的毛病：`Δ_filter = -1` —— **计数出现负增长**。
环形缓冲满时，新行会**挤掉**旧行 ⇒ `grep -c` 的"加载前后相减"测的
**从来不是"新增了几行"，而是"环里恰好还剩几行"**。这个量在环满的机器上
不是可观测量（可以恒为 0、甚至为负）。

⇒ 修法：每次测量前 `dmesg -C` 清环。清环后 Δ 精确、非负、单调，A1/B1/B2
   才重新变成**可判别**的断言（否则 p29 会永远 SKIP≥3 ⇒ 恒 FAIL = 死门，
   正是 ISS-093 的形态）。

★ 副作用（明确记账）：本判据会**清空内核环形缓冲**。这是为了测量而付的代价，
  已写进文档；p17 的 T6 用的是**文件时间戳**、不读 dmesg，故不受影响。

用法: python3 tools/.scratch/patch_094b_p29_ring.py [--check]
"""
import io
import os
import sys

T = "/root/cc/AdaptixServer/extenders/linux_agent"
P = os.path.join(T, "tools/p29_dmesg_budget_verify.py")
CHECK = "--check" in sys.argv

CLEAR = '''def clear_log():
    """清空内核环形缓冲 —— **测量前的必需步骤**（ISS-094）。

    ★ 为什么必须清：dmesg 环是**有界**的，满时新行会挤掉旧行 ⇒ `grep -c`
      的**行数差恒为 ~0**，实测还出现过 **-1**。也就是说，在环满的机器上
      "加载前/后各数一次再相减"这个做法**从根上就不成立**：被测量的从来
      不是"新增了几行"，而是"环里恰好还剩几行"。清环之后 Δ 才等于真实新增，
      且非负、单调。
    """
    try:
        subprocess.run("dmesg -C", shell=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=15)
    except Exception:
        pass


def settle(sec=6.0):'''

A_HEAD_OLD = '''        d_r, sup_r, tries = 0, 0, 0
        for attempt in range(3):
            if attempt:
                unload(a0, PINR)
                loaded = None
                settle(8.0)
                n1 = nlines()
'''
A_HEAD_NEW = '''        d_r, sup_r, tries = 0, 0, 0
        for attempt in range(3):
            if attempt:
                unload(a0, PINR)
                loaded = None
            # ★★ ISS-094 更根本的一层：dmesg **环形缓冲会翻转**（环满时新行挤掉
            #    旧行）⇒ 行数差恒为 ~0、甚至为负（实测 Δ_filter=-1）。行数差在这
            #    种状态下**根本不是可观测量**。故每次测量前先 `dmesg -C` 清环：
            #    之后的 Δ 才是"真实新增"，且非负、单调、可判别。
            #    顺序要紧：先 settle 让**限速桶**复位（管"会不会被吞"），
            #    再 clear_log 让**环**归零（管"数得准不准"），两件事互不替代。
            settle(8.0)
            clear_log()
            n1 = nlines()
'''

B_OLD = '''        settle()          # ★ 同上：B 相若吃 A 相剩下的桶，判别器就废了
        n4 = nlines()
'''
B_NEW = '''        settle()          # ★ 同上：B 相若吃 A 相剩下的桶，判别器就废了
        clear_log()       # ★ 同 A 相：不清环的话行数差被"翻转"吃掉（ISS-094）
        n4 = nlines()
'''

HOOK = '''def nsuppressed():'''

PEND = None
BAD = []


def sub(old, new, tag):
    global PEND
    if PEND is None:
        PEND = io.open(P, encoding="utf-8", newline="").read()
    c = PEND.count(old)
    if c != 1:
        if PEND.count(new) >= 1:
            print("[done] %-28s 已是目标态，跳过" % tag)
            return
        print("[BAD ] %-28s 锚点命中 %d 次（要求 1）" % (tag, c))
        BAD.append(tag)
        return
    PEND = PEND.replace(old, new)
    print("[ok  ] %-28s" % tag)


sub(HOOK, CLEAR, "加 clear_log()")
sub(A_HEAD_OLD, A_HEAD_NEW, "A 相：每轮清环")
sub(B_OLD, B_NEW, "B 相：清环")

print("\n%s" % ("（--check，未写入）" if CHECK else ""))
if BAD:
    print("未命中: %s ⇒ 一个字节都没写" % ", ".join(BAD))
    sys.exit(2)
if not CHECK:
    io.open(P, "w", encoding="utf-8", newline="").write(PEND)
    print("已写入 %s" % P)
