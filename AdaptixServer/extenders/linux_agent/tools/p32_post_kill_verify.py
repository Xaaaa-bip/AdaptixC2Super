#!/usr/bin/env python3
"""Gate 17 补「**杀载荷之后**」—— p01 覆盖到"spawn 完成、agent 未被阻塞"为止，
本脚本补它没覆盖的那一段。

为什么必须有（§8.1 复核后真实缺口三条之一）：
  `p01` 的 A3/A5/A6/A6b/A7 已经断言了「脱离 agent 的父子-会话-进程组」「fd 卫生」
  「载体是 tmpfs 且已 deleted」「两次载体名不同」「spawn 后 agent 未被阻塞」。
  **唯独没有"载荷死掉之后"** —— 而那一小段才是对手事后取证真正会看的东西：
    载体 inode 有没有被回收、kill 有没有留下僵尸、agent 会不会被这次 kill 波及、
    /dev/shm 里会不会冒出一个 `.m*` 残留文件。
  这些在"载荷还活着"的那一刻**全部看不出来**（活着时它们都是正常状态）。

★ 与 p01 的关系：**复用不重写**。载体反查用的是 p01 A6 已经验过的那条判据
  （`/proc/<pid>/exe` 指向 `^/dev/shm/.m[0-9a-f]{8} \\(deleted\\)$`），BOF 入口与
  回执解析直接 `from p01_detached_spawn import run_bof, run_cmd, BOF`。

★ 副作用面：一个 agent + 一个 BOF spawn。**scratch 全在 `tools/.scratch/`（不落
  `/tmp`）** —— 免得与 `p17` T6 的**时间窗**判据纠缠。

判据（每个"★"都带反空跑自证：断言读到的必须是**变化量**，不是绝对值）：
  T1  agent 上线（证明后面有真会话）
  T2  ★ 反查出载体进程（**新增**集合恰好 1 个）——顺带独立复验"载体是 deleted tmpfs"
  T3  ★ kill 前 `/dev/shm` 里**没有** `.m*` 可见项（载体确已 unlink，不是"还在但被藏"）
  T4  kill 前系统僵尸集合快照（T6 要拿它做差集）
  T5  SIGKILL 载荷
  T6  ★ `/proc/<pid>` 消失（轮询 ≤12s，给内核回收留时间）
  T7  ★ **僵尸零新增**（全系统 Z 状态差集 —— 比"这个 pid 不是僵尸"强：它还挡住
      double-fork 中间那层父进程变成僵尸的情形）
  T8  ★ kill 后 `/dev/shm` **零新增**（载体的 `.m*` 没变成残留文件）
  T9  agent 未被这次 kill 波及（kill 后 `getuid` 仍有回执）
  T10 收尾：`--keep` 的源文件**按契约还在**（这是判据，不是清理）⇒ 再由本脚本删掉
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import e2e
from p01_detached_spawn import BOF, run_bof, run_cmd

LISTENER = "TestWSS"
SCRATCH = os.path.join(HERE, ".scratch")
SRC_C = os.path.join(SCRATCH, "p32_plk.c")
PARK = os.path.join(SCRATCH, "p32_plk")        # --keep ⇒ 源文件保留（T10 判据）
CARRIER_RE = re.compile(r"^/dev/shm/\.m[0-9a-f]{8} \(deleted\)$")
SHM_RE = re.compile(r"^\.m[0-9a-f]{8}$")

PAYLOAD_C = "#include <unistd.h>\nint main(void){ for(;;) pause(); return 0; }\n"

R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-56s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def carriers():
    """当前所有"以 deleted tmpfs 文件为映像"的 pid。"""
    out = set()
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            tgt = os.readlink("/proc/%s/exe" % d)
        except OSError:
            continue
        if CARRIER_RE.match(tgt):
            out.add(int(d))
    return out


def zombies():
    """全系统处于 Z 状态的 pid。"""
    out = set()
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            st = open("/proc/%s/stat" % d).read().rsplit(")", 1)[1].split()[0]
        except (OSError, IndexError):
            continue
        if st == "Z":
            out.add(int(d))
    return out


def shm_entries():
    try:
        return set(os.listdir("/dev/shm"))
    except OSError:
        return set()


def compile_payload():
    with open(SRC_C, "w") as f:
        f.write(PAYLOAD_C)
    r = subprocess.run(["musl-gcc", "-static", "-O2", "-o", PARK, SRC_C],
                       capture_output=True, text=True, timeout=120)
    return r.returncode == 0, (r.stderr or "").strip()[:120]


def main():
    os.makedirs(SCRATCH, exist_ok=True)
    for p in (PARK,):
        try:
            os.remove(p)
        except OSError:
            pass

    miss = [t for t in ("musl-gcc",) if shutil.which(t) is None]
    rec("T0 仪器齐备（musl-gcc 静态编译载荷）", not miss, "缺: %s" % (miss or "无"))
    if miss:
        return 1

    pid = 0
    cpid = 0
    try:
        ok, err = compile_payload()
        rec("T0b 载荷编译成静态 ELF（脱离 agent 后仍能跑）", ok, err or "ok")
        if not ok:
            return 1

        binp = e2e.gen_agent(LISTENER, "p32")
        pid = e2e.launch(binp, "p32")
        aid = e2e.wait_registered(LISTENER, pid, timeout=90)
        rec("T1 agent 经 %s 上线" % LISTENER, bool(aid), "aid=%s pid=%d" % (aid, pid))

        # ── spawn 前基线 ──
        car0, zom0, shm0 = carriers(), zombies(), shm_entries()
        print("[p32] 基线: carriers=%s zombies=%s shm=%s"
              % (sorted(car0) or "∅", sorted(zom0) or "∅", sorted(shm0) or "∅"), flush=True)

        txt = run_bof(aid, "%s --keep" % PARK)
        time.sleep(1.5)
        car1 = carriers()
        new = car1 - car0
        cpid = sorted(new)[0] if len(new) == 1 else 0
        rec("T2 ★ 反查出**新增**载体进程（恰好 1 个）", len(new) == 1,
            "new=%s；a_text=%r" % (sorted(new) or "∅", (txt or "")[:70]))
        if not cpid:
            # 用 raise 而不是 `return _finish(...)`：`return` 会**先**求值再跑
            # `finally`，于是汇总行跑到收尸**之前**，而收尸的任何输出都会把
            # `RESULT:` 挤出批次的 `tail -12`（判据的载具太短 = 判据丢失）。
            raise RuntimeError("载体 pid 不可用（T3-T9 会全是空跑）")

        visible = [x for x in shm_entries() if SHM_RE.match(x)]
        rec("T3 ★ kill 前 /dev/shm 无 .m* 可见项（载体确已 unlink）", not visible,
            "可见项=%s；/dev/shm 全量=%s" % (visible or "∅", sorted(shm_entries()) or "∅"))
        try:
            exe = os.readlink("/proc/%d/exe" % cpid)
        except OSError as e:
            exe = "<%s>" % type(e).__name__
        rec("T4 ★ 载荷确实**以载体为映像**在跑（/proc/<pid>/exe）",
            bool(CARRIER_RE.match(exe)), "exe=%r" % exe)
        print("[p32] kill 前僵尸快照（T7 的差集基线）: %s" % (sorted(zom0) or "∅"), flush=True)

        # ── T5 kill ──
        try:
            os.kill(cpid, 9)
            killed = True
        except OSError as e:
            killed = False
            print("[p32] kill 失败: %s" % e, flush=True)
        rec("T5 对载体进程发 SIGKILL", killed, "pid=%d" % cpid)

        # ── T6 进程消失 ──
        t0 = time.time()
        while os.path.exists("/proc/%d" % cpid) and time.time() - t0 < 12:
            time.sleep(0.3)
        gone = not os.path.exists("/proc/%d" % cpid)
        rec("T6 ★ /proc/<pid> 消失（内核回收 ≤12s）", gone,
            "elapsed=%.1fs" % (time.time() - t0))

        # ── T7 僵尸零新增 ──
        time.sleep(1.0)
        zom1 = zombies()
        add = zom1 - zom0
        rec("T7 ★ 僵尸**零新增**（全系统 Z 差集）", not add,
            "新增=%s；当前=%s" % (sorted(add) or "∅", sorted(zom1) or "∅"))

        # ── T8 /dev/shm 零新增 ──
        add_shm = shm_entries() - shm0
        rec("T8 ★ kill 后 /dev/shm **零新增**（.m* 未变成残留文件）", not add_shm,
            "新增=%s" % (sorted(add_shm) or "∅"))

        # ── T9 agent 未被波及 ──
        el = run_cmd(aid, "getuid", timeout=45)
        rec("T9 agent 未被这次 kill 波及（仍返回回执）", el is not None,
            "回执耗时=%s" % (("%.1fs" % el) if el is not None else "无"))
    except Exception as e:
        # ★ 台面自己出事不能被算成 PASS（p31 交过的学费：`return` 落在 `finally`
        #   里会吞掉异常 ⇒ "判据根本没跑"被打成 RESULT: PASS）。
        rec("T★ 台面跑完（未被异常中断）", False, "%s: %s" % (type(e).__name__, str(e)[:60]))
    finally:
        if pid:
            try:
                e2e.kill(pid)
            except Exception as e:
                print("[warn] kill agent: %s" % e, flush=True)
        if cpid and os.path.exists("/proc/%d" % cpid):
            try:
                os.kill(cpid, 9)
            except OSError:
                pass
    return _finish(pid)


def _finish(agent_pid):
    # ── T10 --keep 契约：源文件应当**还在**（判据），随后由本脚本清理 ──
    kept = os.path.exists(PARK)
    rec("T10 `--keep` 契约：源文件仍在（随后由本脚本删除）", kept,
        "exists=%s" % kept)
    for p in (PARK, SRC_C):
        try:
            os.remove(p)
        except OSError:
            pass

    EXPECT = 11
    if len(R) < EXPECT:
        rec("T11 ★ 判据自证跑满 %d 条（没被静默跳过）" % EXPECT, False, "实收 %d 条" % len(R))
    f = sum(1 for x in R if not x)
    print("\nPASS=%d FAIL=%d" % (len(R) - f, f), flush=True)
    print("RESULT: %s" % ("PASS" if f == 0 else "FAIL"), flush=True)
    return 0 if f == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
