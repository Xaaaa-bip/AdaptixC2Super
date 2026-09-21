#!/usr/bin/env python3
"""P4 验收：发布前清理（ISS-053）—— 目标机零落盘痕迹

修前：`main.c` 有 8 处**无任何守卫**的 `ax_dbg` 写点（行 843/938/947/954/966/972/974/978），
每次连接往 `/tmp/ax_dbg` **追加**会话密钥 hex（K）+ 解密后明文 hex（B/V），实测
**3860535 字节**，mtime 随每次连接刷新。**推翻门 S1（磁盘零残留）**，而 p11 只扫
`/dev/shm` 与载荷路径 → 长期漏检。

修后：8 处写点移除；服务器侧 7 处 `AXDBG` 移除（含 `pl_main.go` 打印**会话密钥**、
`utils.go` 打印**解密后明文头**）。

判据分两层：
  ① **定点**：`/tmp/ax_dbg` 不存在（修前它必然出现，且随 beacon 增长）
  ② **泛化**：agent 运行窗口内，目标机全盘无新增文件

  T0  阳性对照 · find 手段：手工建文件，`find -newermt` 必须检出（ISS-020 规则）
  T1  阳性对照 · **旧载荷**：用 `.p4bak`（修前 main.c）生成的载荷必须产出 `/tmp/ax_dbg`
      —— 同时证明"检测手段有效"**与**"修前的确在落盘"，而不是靠推断
  T2  新载荷二进制内**不含** `ax_dbg` 字面量（定点证明已清理）
  T3  基线：`/tmp/ax_dbg` 不存在
  T4  新载荷启动并注册成功（清理没有弄坏功能）
  T5  跑 3 轮 beacon 后 `/tmp/ax_dbg` **仍不存在**   ← 核心断言
  T6  全盘 `find`：agent 运行窗口内新增文件 = 0（白名单外）
  T7  `/dev/shm` 无 `.m<hex>` 载体残留（P1-b 回归）
  T8  `/tmp`、`/var/tmp` 无 `axm_*` 中转残留
  T9  `server.log` 新增行无 `AXDBG`（服务器侧清理生效）
  T10 agent 回执正常（`getuid`）—— 功能回归
"""
import atexit
import os
import re
import shutil
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e

RUN_SRC = "/root/cc/run/extenders/linux_agent/src_agent/agent/main.c"
BAK_SRC = ("/root/cc/AdaptixServer/extenders/linux_agent/"
           "src_agent/agent/main.c.p4bak")
DBG = "/tmp/ax_dbg"
KEEP = RUN_SRC + ".p17keep"
SLOG = "/root/cc/run/server.log"
LISTENER = "TestHTTP"

# ★ 载荷运行后会把执行映像搬到 tmpfs 载体（P1-b：`/dev/shm/.m<8hex>`，写完即
# unlink）并把 comm 伪装成本机**真实存在**的内核线程名（B9）→ **按 pid 杀不掉
# （spawn_detached 之后不是同一个进程），按名字也找不到**。必须按 exe 的真实
# 形状扫 /proc。实测：不扫的话历史测试留了 6 个孤儿，一直往 /tmp/ax_dbg 追加。
AGENT_EXE = re.compile(
    r"^/dev/shm/\.m[0-9a-f]{8} \(deleted\)$"
    r"|^/tmp/e2e_work/[^/]+( \(deleted\))?$")

R = []


def sweep_agents(verbose=False):
    """杀掉所有残留载荷进程，返回 [(pid, exe, comm)]。"""
    out = []
    for p in os.listdir("/proc"):
        if not p.isdigit():
            continue
        try:
            exe = os.readlink("/proc/%s/exe" % p)
        except Exception:
            continue
        if not AGENT_EXE.match(exe):
            continue
        try:
            comm = open("/proc/%s/comm" % p).read().strip()
        except Exception:
            comm = ""
        out.append((p, exe, comm))
        try:
            os.kill(int(p), signal.SIGKILL)
        except Exception:
            pass
    if verbose:
        for p, exe, comm in out:
            print("     sweep: pid=%s comm=%r exe=%s" % (p, comm, exe))
    return out


# 无论如何退出都要收尸：异常、SIGTERM（外层 timeout）都算 —— 否则留下的孤儿
# 会污染**下一次**运行（实测：一次被中断的运行留下 4 个，下一个脚本全被拖慢）。
atexit.register(sweep_agents)
signal.signal(signal.SIGTERM, lambda *a: (sweep_agents(), sys.exit(1)))


def rec(name, ok, note=""):
    R.append(bool(ok))
    print("%s %s%s" % ("PASS" if ok else "FAIL", name,
                       (" | " + note) if note else ""), flush=True)


def sh(c):
    return subprocess.run(["bash", "-lc", c], capture_output=True,
                          text=True).stdout.strip()


def new_files_since(ts):
    """窗口 ts 之后新增的**普通文件**。根 fs 用 -xdev，tmpfs 单列（-xdev 会漏掉它们）。"""
    out = []
    for cmd in (
        "find / -xdev -newermt @%d -type f -not -path '/proc/*' "
        "-not -path '/sys/*' -not -path '/dev/*' 2>/dev/null" % ts,
        "find /dev/shm /tmp /var/tmp /run -newermt @%d -type f 2>/dev/null" % ts,
    ):
        out += [l for l in sh(cmd).splitlines() if l.strip()]
    return sorted(set(out))


# 只排除「C2 服务器自己」的写入面。agent 是**独立进程**，它落盘不会落在这些
# 路径下。已知代价：若 agent 蓄意往 server.log / data/ 写，会被这条白名单掩盖。
WL = re.compile(
    r"^(/root/cc/run/server\.log"
    r"|/root/cc/run/data/"
    r"|/tmp/(reg|gap10_watchdog|dbg)\.log"   # ★ 跑批自己的日志：run_regress.sh 每项都往
                                             # /tmp/reg.log 追加 → mtime 落进 agent 窗口
                                             # → T6 **在 run_regress.sh 里永远假红**（实测）
    # ★★ ISS-066 的**第二个实例**，而且更隐蔽：漏的不是"路径"而是"**命名族**"。
    #   run_regress.sh 给每项另开 `/tmp/reg_<item>.out`（> /tmp/reg_$t.out），它只在
    #   **该项跑的那几分钟里**被追加 —— 于是 p17 自己那一项必红（实测 T6：非白名单
    #   = /tmp/reg_p17_no_artifact_verify.out）。上一条按**名字**豁免了 reg.log，却
    #   没料到同一族还会长出 `reg_*.out`：凡 harness 副产物都要按族豁免。
    #   仍锚定两端（reg…\.(out|log)$），**不写成 `/tmp/`** —— 否则判据当场失效
    #   （"判据一旦开始误报就会被当噪音绕过"）。/tmp/reg.lock 只由批次开头那一次
    #   `exec 9>` 打开，mtime 落不在任何项的窗口里，故不豁免。
    r"|/tmp/reg[^/]*\.(out|log)$"
    r"|/tmp/p17"
    r"|/root/\.cache/"
    r"|/var/log/"
    r"|/var/lib/systemd/"          # systemd-timesyncd 周期更新 clock（实测噪声）
    # ★ 开发工作树整体排除（不再只是 p17 自己）：本脚本断的是"**agent** 不落盘"，
    #   而 `src_agent/`、`tools/` 是**构建输入**，agent 运行期从不写它们（它写
    #   /tmp、/dev/shm、/etc、pin、~/.ssh）。实测被误伤**两次**：跑批时改了
    #   `src_agent/bofs/ko_load.c`、单跑时改写了 `tools/run_regress.sh` —— 两次都
    #   落在 T6 的观测窗口里，报成"非白名单新增"（一次 12/1、一次 12/1 且**点名**）。
    #   与 ISS-066（`/tmp/reg.log`）同型：**验收工具的副产物与被测对象的产物落在
    #   同一时间窗口，判据分不开**。已知代价：若 agent 蓄意往构建树写，会被这条掩盖。
    r"|/root/cc/AdaptixServer/extenders/linux_agent/"
    r"|/root/\.bash_history"
    r"|/root/\.ssh/known_hosts)"
)


def gen_with(src, name):
    """临时把 run 的 main.c 换成 src，生成载荷，**无论如何**还原。

    src 就是 run 自己时（T2 那一路）不做替换 —— 否则 copy2 撞 SameFileError。
    """
    if os.path.samefile(src, RUN_SRC):
        return e2e.gen_agent(LISTENER, name)
    shutil.copy2(RUN_SRC, KEEP)
    try:
        shutil.copy2(src, RUN_SRC)
        return e2e.gen_agent(LISTENER, name)
    finally:
        shutil.copy2(KEEP, RUN_SRC)
        os.unlink(KEEP)


def main():
    logmark = len(open(SLOG, errors="replace").readlines())
    sh("rm -f %s" % DBG)

    # ── T0 阳性对照：find -newermt 这个手段本身有效 ──────────────────────
    ts0 = int(time.time()) - 1
    probe = "/tmp/p17_probe_%d" % os.getpid()
    open(probe, "w").write("x")
    hit = probe in new_files_since(ts0)
    os.unlink(probe)
    rec("T0 阳性对照：find -newermt 能检出新建文件", hit, "探针=%s" % probe)
    if not hit:
        print("RESULT: FAIL (检测手段无效，后续断言无意义)")
        return

    # ── T0b 前置：清掉历史孤儿载荷 ──────────────────────────────────────
    # 它们会**持续**往 /tmp/ax_dbg 追加，把 T5/T6 变成假红（也可能把真红掩盖）。
    # 注意断言的是「清理**动作**有效（复查为 0）」，不是「环境本来干净」——
    # 上一次测试留下的孤儿是常态，不是失败。
    stray = sweep_agents(verbose=True)
    again = sweep_agents()
    rec("T0b 前置清理：清理后无残留载荷", not again,
        "清掉 %d 个，复查 %d 个" % (len(stray), len(again)))

    # ── T1 阳性对照 · 旧载荷（.p4bak = 修前 main.c）─────────────────────
    if not os.path.exists(BAK_SRC):
        rec("T1 阳性对照 · 旧载荷产出 /tmp/ax_dbg", False,
            "缺 %s（无法做修前/修后对拍）" % BAK_SRC)
    else:
        oldbin = gen_with(BAK_SRC, "p17old")
        nmark = len(re.findall(r"ax_dbg", open(BAK_SRC, errors="replace").read()))
        rec("T1a 旧载荷二进制含 ax_dbg 字面量（确认是修前版本）",
            "ax_dbg" in sh("strings %s 2>/dev/null" % oldbin),
            "源里 %d 处写点" % nmark)
        opid = e2e.launch(oldbin)
        try:
            e2e.wait_registered(LISTENER, opid, timeout=90)
            time.sleep(8)          # 让它至少跑完一次 recv→decrypt 往返
        except Exception as e:
            print("[p17] 旧载荷注册异常：%r" % e)
        sz = os.path.getsize(DBG) if os.path.exists(DBG) else -1
        rec("T1b 阳性对照 · 旧载荷**确实**产出 /tmp/ax_dbg", sz > 0,
            "/tmp/ax_dbg=%s 字节" % (sz if sz >= 0 else "不存在"))
        sweep_agents(verbose=True)     # kill(pid) 杀不掉 spawn_detached 后的载荷
        sh("rm -f %s" % DBG)

    # ── T2 新载荷：二进制内不含 ax_dbg ──────────────────────────────────
    newbin = gen_with(RUN_SRC, "p17new")
    n = len(re.findall(r"ax_dbg", sh("strings %s 2>/dev/null" % newbin)))
    rec("T2 新载荷二进制不含 ax_dbg 字面量", n == 0, "命中 %d 处" % n)

    # ── T3 基线 ────────────────────────────────────────────────────────
    rec("T3 基线：/tmp/ax_dbg 不存在", not os.path.exists(DBG),
        "exists=%s" % os.path.exists(DBG))

    # ── T4 新载荷启动并注册 ─────────────────────────────────────────────
    ts_win = int(time.time()) - 2      # ★ 窗口起点：launch 之前
    pid = e2e.launch(newbin)
    try:
        aid = e2e.wait_registered(LISTENER, pid, timeout=90)
        rec("T4 新载荷启动并注册成功", bool(aid), "aid=%s pid=%d" % (aid, pid))
    except Exception as e:
        rec("T4 新载荷启动并注册成功", False, repr(e))
        aid = None

    # ── T5 核心：3 轮 beacon 后仍无 /tmp/ax_dbg ─────────────────────────
    oks = 0
    if aid:
        for i in range(3):
            try:
                r = e2e.roundtrip(LISTENER, "getuid", timeout=60, keep=True)
                oks += 1 if r else 0
            except Exception:
                pass
            time.sleep(2)
    sz = os.path.getsize(DBG) if os.path.exists(DBG) else -1
    rec("T5 3 轮 beacon 后 /tmp/ax_dbg 仍不存在", sz < 0,
        "beacon 成功 %d/3；/tmp/ax_dbg=%s"
        % (oks, "不存在" if sz < 0 else "%d 字节" % sz))

    # ── T6 全盘新增文件 ────────────────────────────────────────────────
    nf = new_files_since(ts_win)
    wl = [p for p in nf if WL.match(p)]
    bad = [p for p in nf if not WL.match(p)]
    rec("T6 agent 运行窗口内全盘新增文件 = 0（白名单外）", not bad,
        "扫描 %d 条新增；白名单 %d 条%s"
        % (len(nf), len(wl), ("；**非白名单**：" + ", ".join(bad[:5])) if bad else ""))

    # ── T7/T8 载体与中转残留 ───────────────────────────────────────────
    m = sh("ls -1 /dev/shm 2>/dev/null | grep -c '^\\.m[0-9a-f]\\{8\\}$'")
    rec("T7 /dev/shm 无 .m<8hex> 载体残留", m.strip() in ("", "0"), "命中=%s" % m)
    t = sh("ls -1 /tmp /var/tmp 2>/dev/null | grep -c '^axm_'")
    rec("T8 /tmp 与 /var/tmp 无 axm_* 中转残留", t.strip() in ("", "0"), "命中=%s" % t)

    # ── T9 server.log 新增行无 AXDBG ───────────────────────────────────
    newlines = open(SLOG, errors="replace").readlines()[logmark:]
    hits = [l for l in newlines if "AXDBG" in l]
    rec("T9 server.log 新增行无 AXDBG", not hits,
        "新增 %d 行，命中 %d" % (len(newlines), len(hits)))
    if hits:
        print("     首条：%s" % hits[0].strip()[:160])

    # ── T10 功能回归 ───────────────────────────────────────────────────
    if aid:
        try:
            r = e2e.roundtrip(LISTENER, "getuid", timeout=60, keep=True)
            rec("T10 agent 回执正常（功能回归）", bool(r),
                "getuid → %r" % (str(r)[:80],))
        except Exception as e:
            rec("T10 agent 回执正常（功能回归）", False, repr(e))

    sweep_agents(verbose=True)
    sh("rm -f %s" % DBG)
    print("\nPASS=%d FAIL=%d" % (sum(R), len(R) - sum(R)))
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"))


if __name__ == "__main__":
    main()
