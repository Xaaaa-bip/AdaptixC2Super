#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p18_bpftool_hide_verify.py — P5 / ISS-007 验收：bpftool 枚举对抗 + 加载顺序无关性

为什么单开一个脚本（而不是塞进 gap10）：
  gap10 T1 能证明"4 模块加载后 bpftool 看不见"，但它**不控制加载顺序** ——
  实测发送顺序是 hide→tc→knock→filter，实际完成顺序却是 filter→knock→tc→hide
  （Adaptix 的 BOF 任务不按发送序执行，见 T2 注释）。于是 gap10 只覆盖了一种
  **偶然**的顺序，而 ISS-007 的修法（扫 pin 清单 → 扇出写入每一个 *_map1）
  声称的是**顺序无关**。本脚本每条加载后都等回执，把顺序变成确定的输入。

验收项：
  T1  基线 + 阳性对照（非我方 map 必须可见，否则阴性断言没有意义）
  T2  顺序 B：只加载 bpf_filter → 它必须能藏住**自己**（自登记路径）
  T3  ★ 关键：再加载 hide。filter 加载时 hide 还不存在，filter 的 map1 里
      不可能预置 hide 的 id —— 所以 hide 能藏住，只可能来自"**后加载者**扫 pin
      清单并扇出写入**所有** *_map1"这条路径。这是顺序无关性的实证。
  T4  agent fd 表里没有 BPF 匿名 inode（门 S2 的 lsof 项；ISS-060 的探测器）
  T5  边界量化：单独卸载 bpf_filter 后 hide 是否重新可见（机制固有边界，INFO）
  T6  全部卸载 → pin 清空 + prog 回落基线
  T7  收尾：孤儿载荷清理动作有效（P4 教训：断言"清理动作有效"，不是"环境本来就干净"）

用法: p18_bpftool_hide_verify.py [--listener TestHTTP]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                    # noqa: E402
import gap10_rootkit_verify as G              # noqa: E402  复用观测原语与 BOF 打包

# 阳性对照 map。刻意不用 axg18 前缀：T6 靠 startswith("axg18") 数 pin。
CTRL = "ctrlg18"
PIN_F, PIN_H = "axg18f", "axg18h"
OURS = set(["hide_names", "hidden_bpf_ids", "knock_key", "knock_seen",
            "events", "hidden_ports", "trace_fds", "redact_fds"])
R = []


def rec(item, status, note=""):
    R.append((item, status, note))
    print("  [%-4s] %-46s %s" % (status, item, note), flush=True)


def our_pins():
    """本套件的 pin。★ 必须把名字喂给 G.pins(extra=...) —— P8 之后我们的 pin 名
    进了 hide_names，`ls /sys/fs/bpf` 列不出来，只认目录枚举会永远得到空集：
    T6 的"pin 清空"变成恒真、第 107 行的前置守卫变成恒假（守卫失效 = 脏现场开跑）。"""
    return sorted(p for p in G.pins(extra=(PIN_F, PIN_H)) if p.startswith("axg18"))


def cleanup18(pid=None):
    if pid:
        try:
            os.kill(pid, 9)
        except OSError:
            pass
    sh = G.sh
    sh("pgrep -f 'gap10_watchdo[g]' | while read p; do kill -9 $p 2>/dev/null; done")
    sh("rm -f %s" % G.WATCHDOG)
    for p in our_pins():
        sh("bpftool link detach pinned /sys/fs/bpf/%s 2>/dev/null" % p)
        sh("rm -f /sys/fs/bpf/%s" % p)
    sh("rm -f /sys/fs/bpf/%s" % CTRL)


def ctrl_visible():
    """建一个**不经过 ebpf_load** 的 map —— 它永远不在 hidden_bpf_ids 里。

    `bpftool map create` 必须显式给 name：裸形态直接报 `Error: map name not
    specified`，而被 2>/dev/null 吞掉后就变成"对照建不出来 → T1 假 FAIL"。
    名字也不能带 axg18 前缀，否则混进 T6 的 pin 计数里造成假绿。
    """
    G.sh("rm -f /sys/fs/bpf/%s 2>/dev/null" % CTRL)
    G.sh("bpftool map create /sys/fs/bpf/%s type hash key 4 value 1 entries 1 "
         "name %s 2>/dev/null" % (CTRL, CTRL))
    return CTRL in G.map_names()


def bpf_fds(pid, key="bpf"):
    """agent 进程 fd 表里指向含 key 的匿名 inode 的条目 [(fd, target)]。

    与 bpftool 正交：对象被 pin 藏住时 bpftool 看不见，但**加载期泄漏的 fd**
    在 /proc/<pid>/fd 里一览无余 —— 那正是门 S2 的 lsof 项。
    """
    out = []
    d = "/proc/%d/fd" % pid
    try:
        names = sorted(os.listdir(d), key=int)
    except OSError:
        return out
    for f in names:
        try:
            t = os.readlink(os.path.join(d, f))
        except OSError:
            continue
        if key in t:
            out.append((f, t))
    return out


def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"

    if not os.path.isdir("/sys/kernel/tracing/events/syscalls"):
        print("FATAL: /sys/kernel/tracing 未挂载")
        return 2
    if our_pins():
        print("FATAL: axg18 pin 未清（先调 cleanup18）")
        return 2

    cleanup18()
    pid = aid = None
    try:
        binp = e2e.gen_agent(listener, "p18")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered(listener, pid, timeout=90)
        print("[p18] agent id=%s pid=%s" % (aid, pid), flush=True)

        base_prog, base_map = G.prog_ids(), G.map_names()
        print("[p18] 基线 prog=%d map=%d pins=%d" %
              (len(base_prog), len(base_map), len(G.pins())), flush=True)

        # ── T1 阳性对照 ──
        cv = ctrl_visible()
        rec("T1 阳性对照 · 非我方 map 在 bpftool map list 可见",
            "PASS" if cv else "FAIL",
            "%s 可见=%s（False = 观测手段坏了，T2/T3/T5 的阴性断言全部无意义）" % (CTRL, cv))

        # ── T2 filter 先加载；每条都等回执，"顺序"才是确定的输入 ──
        # 反例：gap10 一次发 4 条，服务端实际逆序执行（filter 最先完成）。
        # 不等待就等于把顺序交给调度器，测出来的结论无法复现。
        print("[T2] 顺序 B：先加载 bpf_filter（逐条等回执）", flush=True)
        G.load(aid, "bpf_filter", 5, 1, "syscalls/sys_enter_bpf", pin=PIN_F)
        ok2, _ = G.wait_tasks(aid, 1, 120, "T2")
        G.show_bof(aid, 1)
        dp2 = sorted(G.prog_ids() - base_prog)
        dm2 = sorted(OURS & (G.map_names() - base_map))
        rec("T2 filter 自登记 · 自己也不可见（先载方向）",
            "PASS" if (ok2 and not dp2 and not dm2) else "FAIL",
            "新 prog=%s 新 map=%s（加载回执 %s）" % (dp2, dm2, "齐" if ok2 else "缺"))

        # ── T3 ★ 顺序无关性的核心断言 ──
        print("[T3] 再加 hide —— filter 此时已在运行，不可能预知 hide 的 id", flush=True)
        G.load(aid, "hide", 5, 1, "syscalls/sys_exit_getdents64", pin=PIN_H)
        ok3, _ = G.wait_tasks(aid, 1, 120, "T3")
        G.show_bof(aid, 1)
        dp3 = sorted(G.prog_ids() - base_prog)
        dm3 = sorted(OURS & (G.map_names() - base_map))
        rec("T3 ★ filter 先载时后加载模块仍不可见（顺序无关）",
            "PASS" if (ok3 and not dp3 and not dm3) else "FAIL",
            "新 prog=%s 新 map=%s（回执 %s）" % (dp3, dm3, "齐" if ok3 else "缺"))

        # ── T4 agent fd 表（门 S2 · lsof；ISS-060 的探测器）──
        # 此时两个模块都在加载态：pin 该有、fd 不该有。ebpf_load 的成功路径曾裸
        # `return;` 跳过 close，于是每模块常驻 1 个 prog fd + 最多 8 个 map fd。
        bfds = bpf_fds(pid)
        # perf_event fd 是**已知成本**：tracepoint 走 perf_event_open，必须留着
        # fd 才能在 unload 里 detach（数量 ≈ 已 attach 的 CPU 数）。所以只报告、
        # 不当判据；但打出数量，异常膨胀时一眼能看见。
        nperf = len(bpf_fds(pid, "perf_event"))
        rec("T4 agent fd 表无 BPF 匿名 inode（门 S2 · lsof）",
            "PASS" if not bfds else "FAIL",
            "命中 %d 条%s；另 perf_event fd %d 个（已知成本）"
            % (len(bfds), ("：" + str(bfds[:4])) if bfds else "", nperf))

        # ── T5 边界量化（INFO，不计入 PASS/FAIL）──
        # 这不是缺陷而是机制边界：隐藏能力**寄生在 bpf_filter 的存活上**。
        # 记成 INFO 而不是 PASS，是为了不把"卸下过滤器就露馅"写成"通过"。
        print("[T5] 单独卸载 bpf_filter，量化边界", flush=True)
        G.send_bof(aid, "ebpf_unload", G.pack_unload(PIN_F))
        ok4, _ = G.wait_tasks(aid, 1, 120, "T4")
        vis4 = G.poll_until(lambda: sorted(G.prog_ids() - base_prog),
                            lambda v: v, timeout=15)
        rec("T5 边界 · 卸下 filter 后 hide 重新可见（固有，非缺陷）",
            "INFO" if (ok4 and vis4) else "FAIL",
            "卸载回执 %s；可见 prog=%s —— 含义：过滤器一卸，先前藏起来的对象立刻"
            "回到 bpftool 视野。真实影响=「隐藏的持续性 ≤ bpf_filter 的存活期」。"
            % ("齐" if ok4 else "缺", vis4))

        # ── T6 全部卸载 ──
        print("[T6] 卸载 hide", flush=True)
        G.send_bof(aid, "ebpf_unload", G.pack_unload(PIN_H))
        ok5, _ = G.wait_tasks(aid, 1, 120, "T5")
        left = G.poll_until(our_pins, lambda v: not v, timeout=15)
        # 轮询而不是读一次：对象回收不是瞬时的（prog 由 fd/attach 引用计数决定，
        # 先关 fd 再 unlink 之后内核还要走一遍 RCU）。一次性读会假红。
        fin = G.poll_until(G.prog_ids, lambda v: v <= base_prog, timeout=20)
        rec("T6 全部卸载 · pin 清空 + prog 回落基线",
            "PASS" if (ok5 and not left and fin <= base_prog) else "FAIL",
            "残留 pin=%s prog=%d base=%d 残留 id=%s" %
            (left, len(fin), len(base_prog), sorted(fin - base_prog)))

        # ── T7 收尾 ──
        # 断言的是"**清理动作**有效"（复查为 0），不是"环境本来就干净" ——
        # 每次运行必然留下孤儿载荷（ISS-059），后者恒 FAIL（P4 已踩过）。
        os.kill(pid, 9)
        pid = None
        time.sleep(3)
        import p17_no_artifact_verify as P17
        stray = P17.sweep_agents(verbose=True)
        again = P17.sweep_agents()
        rec("T7 收尾：孤儿载荷清理动作有效（复查为 0）",
            "PASS" if not again else "FAIL",
            "清掉 %d 个，复查 %d 个" % (len(stray), len(again)))

    except Exception as e:
        rec("EXCEPTION", "FAIL", "%s: %s" % (type(e).__name__, str(e)[:200]))
    finally:
        cleanup18(pid)

    p = len([1 for _, s, _ in R if s == "PASS"])
    f = len([1 for _, s, _ in R if s == "FAIL"])
    print("\n===== p18 结果 =====")
    for item, st, note in R:
        print("  %-4s %s" % (st, item))
    print("PASS=%d FAIL=%d" % (p, f))
    print("RESULT: %s" % ("PASS" if f == 0 else "FAIL"))
    return 0 if f == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
