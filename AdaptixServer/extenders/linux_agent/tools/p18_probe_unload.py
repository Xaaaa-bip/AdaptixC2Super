#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p18_probe_unload.py — 定性：ebpf_unload 到底释不释放 prog？

背景：p18 T5 卸载两个模块后 pin 清空但 prog 残留 [701,702]。gap10 的 T4c 之所以
PASS，是因为它在 `os.kill(pid,9)` **之后**才数 prog —— 那时 agent 一死、fd 全关，
prog 自然消失。所以"卸载有效"这件事从来没被单独验证过。

判别两种假设：
  H1 异步回收：unload 之后等几秒 prog 就没了 → p18 T5 是写法问题（该轮询）
  H2 真泄漏  ：unload 不释放 prog，只有进程死才消失 → 真实缺陷

输出一行结论，不做 PASS/FAIL 判定。
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                    # noqa: E402
import gap10_rootkit_verify as G              # noqa: E402

PIN = "axg18f"


def ids():
    return sorted(G.prog_ids())


def main():
    for p in sorted(x for x in G.pins() if x.startswith("axg18")):
        G.sh("rm -f /sys/fs/bpf/%s" % p)
    base = ids()
    print("[probe] base=%s (n=%d)" % (base, len(base)), flush=True)

    pid = aid = None
    try:
        binp = e2e.gen_agent("TestHTTP", "p18probe")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered("TestHTTP", pid, timeout=90)
        print("[probe] agent aid=%s pid=%s" % (aid, pid), flush=True)

        G.load(aid, "bpf_filter", 5, 1, "syscalls/sys_enter_bpf", pin=PIN)
        ok, _ = G.wait_tasks(aid, 1, 120, "load")
        after_load = ids()
        print("[probe] 加载后 n=%d 新增=%s（回执 %s）" %
              (len(after_load), sorted(set(after_load) - set(base)), "齐" if ok else "缺"),
              flush=True)

        G.send_bof(aid, "ebpf_unload", G.pack_unload(PIN))
        ok_u, _ = G.wait_tasks(aid, 1, 120, "unload")
        print("[probe] 卸载回执 %s；pins=%s" %
              ("齐" if ok_u else "缺", sorted(G.pins())), flush=True)

        for label, wait in (("t+2s", 2), ("t+5s", 3), ("t+15s", 10)):
            time.sleep(wait)
            n = ids()
            print("[probe] %-6s n=%d 相对 base 残留=%s" %
                  (label, len(n), sorted(set(n) - set(base))), flush=True)

        os.kill(pid, 9)
        pid = None
        time.sleep(3)
        n = ids()
        print("[probe] agent 死后 n=%d 相对 base 残留=%s" %
              (len(n), sorted(set(n) - set(base))), flush=True)
        print("[probe] 判读：若卸载后残留一直不消、只有 agent 死才消 → H2 真泄漏",
              flush=True)
    finally:
        if pid:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        for p in sorted(x for x in G.pins() if x.startswith("axg18")):
            G.sh("rm -f /sys/fs/bpf/%s" % p)


main()
