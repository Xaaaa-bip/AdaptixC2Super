#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""补丁二：run_regress.sh —— 接进 p31（Gate 19 常态门）+ p32（Gate 17「杀载荷之后」）
+ 修收尾行的过度声明（ISS-092 同族）。

★ 一律在**批次结束后**运行（bash 是边读边执行脚本的，跑批期间改它 = 未定义行为）。
★ 锚点纪律与 patch_091_092_093.py 同：同一文件多处替换**串行累积**（PEND），
  全部命中才落盘（避免半应用）。

用法: python3 tools/.scratch/patch_093b_regress.py [--check]
"""
import io
import os
import sys

T = "/root/cc/AdaptixServer/extenders/linux_agent"
CHECK = "--check" in sys.argv

PEND = {}
OPS = []
BAD = []


def sub(path, old, new, tag):
    p = os.path.join(T, path)
    if p not in PEND:
        PEND[p] = io.open(p, encoding="utf-8", newline="").read()
    d = PEND[p]
    c = d.count(old)
    if c != 1:
        if d.count(new) >= 1:
            print("[done] %-26s %s 已是目标态，跳过" % (tag, path))
            return
        print("[BAD ] %-26s %s 锚点命中 %d 次（要求 1）" % (tag, path, c))
        BAD.append(tag)
        return
    PEND[p] = d.replace(old, new)
    OPS.append(tag)
    print("[ok  ] %-26s %s" % (tag, path))


RR = "tools/run_regress.sh"

# ── 接进 p31 / p32：副作用半径 = 各一个 agent（p31 外加 lo 抓包、p32 外加一个 tmpfs 载荷），
#    scratch 都落 tools/.scratch/（不落 /tmp）。与 p30 同级 ⇒ 紧随 p30，
#    仍留在 gap10/p22/p25 之前的"低副作用区"。
sub(RR,
    '    p30_environ_scrub_verify:400 \\\n    gap10_rootkit_verify:900 \\\n',
    '    p30_environ_scrub_verify:400 \\\n'
    '    p31_ja4_regress:700 \\\n'
    '    p32_post_kill_verify:700 \\\n'
    '    gap10_rootkit_verify:900 \\\n',
    "接入 p31 + p32")

# ── 收尾行过度声明（ISS-092 同族）：原文只点名 agent 源码 + BOF，
#    而实际闸门（修完 §1b 后）覆盖**四条轴**。声明窄于事实 ⇒ 让人以为 BPF/mbedtls 没管。
sub(RR,
    '[batch] deploy --check 通过：agent 源码 + BOF 与源码树一致',
    '[batch] deploy --check 通过：**四条轴**（agent 源码 / mbedtls / BOF / BPF 产物）与源码树一致',
    "ISS-092 收尾声明")

# ── 注释里补 p31/p32 的身份，免得后人以为它们是普通单测
sub(RR,
    '# p30 只起一个 agent（外加一个对照 sleep），副作用半径 = 一个进程 + 一个 tmpfs\n',
    '# p31 也是低副作用（一个 agent + lo 抓包，pcap 落 tools/.scratch/）；\n'
    '#   它是 **Gate 19 的常态门**（防 JA4 退化），带阴性对照自证**能红**。\n'
    '# p32 同样低副作用（一个 agent + 一个 tmpfs 载荷）；它是 **Gate 17 的\n'
    '#   "杀载荷之后"**（p01 只覆盖到 spawn 完成为止），判据是 /proc 回收 +\n'
    '#   全系统僵尸零新增 + /dev/shm 零新增。\n'
    '# p30 只起一个 agent（外加一个对照 sleep），副作用半径 = 一个进程 + 一个 tmpfs\n',
    "p31/p32 身份注释")

print("\n%d 处待应用%s" % (len(OPS), "（--check，未写入）" if CHECK else ""))
if BAD:
    print("未命中: %s" % ", ".join(BAD))
    print("⇒ 一个字节都没写")
    sys.exit(2)
if not CHECK:
    for p, txt in PEND.items():
        io.open(p, "w", encoding="utf-8", newline="").write(txt)
        print("  written %s" % p)
    print("已写入 %d 个文件" % len(PEND))
