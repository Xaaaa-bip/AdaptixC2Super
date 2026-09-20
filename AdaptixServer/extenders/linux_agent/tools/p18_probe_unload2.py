#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p18_probe_unload2.py — 定位：unload 后 prog 为何不释放

p18_probe_unload 已确证：unload 后 prog 一直存活，只有 agent 死才消失（H2）。
ebpf_unload.c 的设计是「① 读 <pin>_map6 里的 perf fd 逐个 close（=当场 detach）
                    ② unlink 全部 <pin>_{prog,link,map0..7}」。
所以要取的是 §1 到底关掉几个 fd：回执里的 `closed N perf fd(s)`。
N=0 → §1 空转（map6 空/fd 号无效），这才是 prog 不释放的根因。

输出：map6 dump 原文 + unload 回执全文。
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                    # noqa: E402
import gap10_rootkit_verify as G              # noqa: E402

PIN = "axg18f"


def main():
    for p in sorted(x for x in G.pins() if x.startswith("axg18")):
        G.sh("rm -f /sys/fs/bpf/%s" % p)
    base = set(G.prog_ids())
    print("[probe2] base n=%d" % len(base), flush=True)

    pid = aid = None
    try:
        binp = e2e.gen_agent("TestHTTP", "p18probe2")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered("TestHTTP", pid, timeout=90)
        print("[probe2] aid=%s pid=%s" % (aid, pid), flush=True)

        G.load(aid, "bpf_filter", 5, 1, "syscalls/sys_enter_bpf", pin=PIN)
        G.wait_tasks(aid, 1, 120, "load")
        print("[probe2] --- map6(trace_fds) 内容 ---", flush=True)
        print(G.sh("bpftool map dump pinned /sys/fs/bpf/%s_map6 2>&1" % PIN), flush=True)
        print("[probe2] pins=%s" % sorted(x for x in G.pins() if x.startswith("axg18")),
              flush=True)

        G.send_bof(aid, "ebpf_unload", G.pack_unload(PIN))
        G.wait_tasks(aid, 1, 120, "unload")
        time.sleep(1)
        print("[probe2] --- unload 回执全文 ---", flush=True)
        G.show_bof(aid, 2)
        time.sleep(4)
        res = sorted(set(G.prog_ids()) - base)
        print("[probe2] 卸载后 pins=%s 残留 prog=%s" %
              (sorted(x for x in G.pins() if x.startswith("axg18")), res), flush=True)
        if res:
            print("[probe2] 残留 prog 明细:", flush=True)
            print(G.sh("bpftool prog list 2>/dev/null | grep -E '^%s:|^%s:'" %
                       (res[0], res[-1])), flush=True)

        os.kill(pid, 9)
        pid = None
        time.sleep(3)
        print("[probe2] agent 死后残留=%s" %
              sorted(set(G.prog_ids()) - base), flush=True)
    finally:
        if pid:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        for p in sorted(x for x in G.pins() if x.startswith("axg18")):
            G.sh("rm -f /sys/fs/bpf/%s" % p)


main()
