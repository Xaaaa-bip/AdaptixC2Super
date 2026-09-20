#!/usr/bin/env python3
"""gap11_session_soak.py — Phase 4 缺口 11：完整 operator 会话 60 分钟零掉线。

验收（LIVE_TEST_CHECKLIST.md 缺口 11）：
  C1 全程时长 ≥ N 分钟
  C2 零掉线（tick age 恒 < 60s，/agent/list 不消失）
  C3 操作零丢失（每轮 7 条操作全部拿到 107 回执）
  C4 至少一次连接轮换（AX_TEST_LOG 里的 rotate 事件）
  C5 轮换期间无掉线（有 rotate 且 C2 成立）

轮换证据 = agent 自带的 AX_TEST_LOG（timing.c 把 connect/rotate/reconnect 追加成
"<monotonic_ms> <event>" 行）。AX_ROTATE_MS 是 timing.c 里的测试旋钮（生产失效），
checklist 明确允许"临时设 120000"。

★ 回执配对必须用 **a_task_id**（ISS-023）。106 行（已下发）带 a_cmdline + a_task_id；
  107 行（完成）带同一 a_task_id。曾经按"Id > base 且见到 107 就算一条"配对，并在命中后
  `base = max_id()` —— 一跳就跨过同批其它行，于是每轮只认下第一条、其余全判"丢失"。
  那不是 agent 掉操作，是观测口径错（实测同批 11 条全部 107 到齐）。

关于 5 类操作的口径（诚实记录，不假装）：
  任务命令 getuid ............ /agent/command/raw
  终端 shell 会话 ............ **本构建不支持**（agent 无 COMMAND_SHELL/pty），
                              用 `ls /tmp` + `cat /etc/hostname` 替代，结论标注 substitute
  BOF 执行 ................... execute bof host_recon.o
  文件下载 ................... download /etc/hostname（回执"下载已开始"）
  隧道开/关 .................. link/unlink 127.0.0.1 9
                              ※ 已知：二者 107 回执的 a_message/a_text 会串成上一个 BOF 的输出
                                （本脚本只断言"回执存在"，不断言隧道语义，见 doc 归档 ISS-024）

用法: gap11_session_soak.py [--listener TestHTTP] [--minutes 60]
                            [--rotate-ms 120000] [--round-sec 240]
"""
import json, os, sqlite3, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e

BOF_DIR = "/root/cc/run/extenders/linux_agent/bofs"
TIMING_LOG = "/tmp/g11_timing.log"

# 每轮 7 条：覆盖 任务命令 / 终端替代×2 / 文件下载 / BOF / 隧道开+关
# 全部语法经 sweep 实测有效（`ps` 无可用子命令、`id/hostname/sysinfo/sleep/jobs` 不存在，
#  `link target=… port=…` 是错语法 —— 详见 doc/问题归档.md ISS-024）
OPS = ["getuid",
       "ls /tmp",
       "cat /etc/hostname",
       "download /etc/hostname",
       "execute bof %s/host_recon.o" % BOF_DIR,
       "link 127.0.0.1 9",
       "unlink 127.0.0.1 9"]


# ── Consoles 观测 ────────────────────────────────────────────────────────
def _rows(aid, base):
    """返回 Id>base 的全部 Consoles 行（已解析）。"""
    try:
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        r = con.execute("SELECT Id, CAST(Packet AS TEXT) FROM Consoles "
                        "WHERE AgentId=? AND Id>? ORDER BY Id", (aid, base)).fetchall()
        con.close()
    except Exception:
        return []
    out = []
    for rid, pkt in r:
        try:
            d = json.loads(pkt)
        except Exception:
            continue
        d["_id"] = rid
        out.append(d)
    return out


def max_id(aid):
    try:
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        v = con.execute("SELECT COALESCE(MAX(Id),0) FROM Consoles WHERE AgentId=?",
                        (aid,)).fetchone()[0]
        con.close()
        return v
    except Exception:
        return 0


def do_round(aid, base, timeout=180):
    """下发 OPS，按 task_id 等 107 回执。

    返回 (got, sent, misses, notes)：got=拿到回执的条数；sent=服务端接受下发的条数；
    misses=有 106 无 107 的命令列表；notes=被服务端拒绝的命令。
    """
    notes, accepted = [], []
    for c in OPS:
        try:
            e2e.send_raw(aid, c)
            accepted.append(c)
        except Exception as e:
            notes.append("%s 被拒: %s" % (c.split()[0], str(e)[:60]))
    # 下发后立刻取本批 106 行 → 建 cmdline→task_id 映射（同批可能重复命令，按序配对）
    tid_of = {}
    t0 = time.time()
    while len(tid_of) < len(accepted) and time.time() - t0 < 60:
        for d in _rows(aid, base):
            if d.get("type") == 106 and d.get("a_cmdline") in accepted:
                tid_of.setdefault(d["a_cmdline"], d.get("a_task_id"))
        if len(tid_of) < len(accepted):
            time.sleep(2)
    want = set(t for t in tid_of.values() if t)
    done = set()
    t0 = time.time()
    while len(done) < len(want) and time.time() - t0 < timeout:
        for d in _rows(aid, base):
            if d.get("type") == 107 and d.get("a_task_id") in want:
                done.add(d["a_task_id"])
        if len(done) < len(want):
            time.sleep(3)
    misses = [c for c in accepted if tid_of.get(c) not in done]
    return len(done), len(accepted), misses, notes


def tick_age(aid, pid):
    """返回 (age_seconds, alive)；用 pid 认人，避免同名 listener 上认错 agent。"""
    try:
        agents = e2e._auth_http("GET", "/agent/list")
    except Exception:
        return None, True          # 网络抖动不算掉线，下一轮再看
    if not isinstance(agents, list):
        return None, True
    for a in agents:
        if a.get("a_id") == aid and str(a.get("a_pid")) == str(pid):
            return time.time() - a.get("a_last_tick", 0), True
    return None, False             # 列表里找不到 = 掉线


def timing_events():
    ev = {}
    try:
        for line in open(TIMING_LOG):
            p = line.split()
            if len(p) >= 2:
                ev[p[1]] = ev.get(p[1], 0) + 1
    except OSError:
        pass
    return ev


def downloads():
    """服务端落盘的下载文件数（/download/list 在本构建返回 None，改数文件）。"""
    n = 0
    for root, _, files in os.walk("/root/cc/run/data"):
        if "download" in root.lower():
            n += len(files)
    return n


def main():
    argv = sys.argv[1:]
    def opt(n, d):
        return argv[argv.index(n) + 1] if n in argv else d
    listener = opt("--listener", "TestHTTP")
    minutes = float(opt("--minutes", 60))
    rotate_ms = int(opt("--rotate-ms", 120000))
    round_sec = int(opt("--round-sec", 240))

    print("[g11] listener=%s 时长=%.0fmin  轮换=%dms  轮次间隔=%ds  ops/轮=%d"
          % (listener, minutes, rotate_ms, round_sec, len(OPS)), flush=True)
    try:
        os.remove(TIMING_LOG)
    except OSError:
        pass

    binp = e2e.gen_agent(listener, "g11")
    env = dict(os.environ, AX_ROTATE_MS=str(rotate_ms), AX_TEST_LOG=TIMING_LOG)
    pid = subprocess.Popen([binp], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, env=env).pid
    aid = e2e.wait_registered(listener, pid, timeout=90)
    print("[g11] agent up id=%s pid=%d" % (aid, pid), flush=True)
    dl0 = downloads()
    t_start = time.time()

    rounds = []          # (rnd, got, sent, misses, notes)
    ages = []            # tick age 采样
    gone = 0             # 列表里找不到的次数
    deadline = t_start + minutes * 60
    rnd = 0
    try:
        while time.time() < deadline:
            rnd += 1
            base = max_id(aid)
            got, sent, misses, notes = do_round(aid, base)
            rounds.append((rnd, got, sent, misses, notes))
            print("  [round %d] %d/%d 回执  age=%.0fs  %s"
                  % (rnd, got, sent, (ages[-1] if ages else -1),
                     ("缺 %s" % ",".join(m.split()[0] for m in misses)) if misses else "",
                     ), flush=True)
            for n in notes:
                print("            ! %s" % n, flush=True)

            # 轮次之间按 5s 采样 tick age（覆盖轮换窗口）
            t_end = time.time() + round_sec
            while time.time() < t_end:
                a, alive = tick_age(aid, pid)
                if not alive:
                    gone += 1
                elif a is not None:
                    ages.append(a)
                time.sleep(5)
            if time.time() >= deadline:
                break
    finally:
        ev = timing_events()
        tail = min(ages) if ages else -1
        mx = max(ages) if ages else -1
        bad = sum(1 for a in ages if a >= 60)
        lost = sum(t[2] - t[1] for t in rounds)
        rejected = sum(len(t[4]) for t in rounds)
        dl1 = downloads()
        elapsed = time.time() - t_start

        print("\n===== gap11 结果 =====", flush=True)
        print("  实际时长        %.1f 分钟（目标 %.0f）" % (elapsed / 60.0, minutes))
        print("  轮次            %d 次（%ds/轮）" % (len(rounds), round_sec))
        print("  操作回执        %d/%d 条（被服务端拒绝 %d 条）"
              % (sum(t[1] for t in rounds), sum(t[2] for t in rounds), rejected))
        print("  tick age        采样 %d 次，min=%.0fs max=%.0fs，≥60s 的采样 %d 次"
              % (len(ages), tail, mx, bad))
        print("  掉线(列表消失)  %d 次" % gone)
        print("  轮换事件        %s" % (ev or "(无)"))
        print("  落盘下载文件    %d -> %d" % (dl0, dl1))
        checks = [
            ("C1 全程时长 ≥ %.0f 分钟" % minutes, elapsed >= minutes * 60 - 60),
            ("C2 零掉线（tick age 恒 < 60s，无消失）", bad == 0 and gone == 0 and len(ages) > 0),
            ("C3 操作零丢失（每轮全部拿到回执）", lost == 0 and rejected == 0),
            ("C4 至少一次连接轮换", ev.get("rotate", 0) >= 1),
            ("C5 轮换期间无掉线（有 rotate 且 C2 成立）",
             ev.get("rotate", 0) >= 1 and bad == 0 and gone == 0),
        ]
        for name, okc in checks:
            print("  [%s] %s" % ("PASS" if okc else "FAIL", name), flush=True)
        f = sum(1 for _, o in checks if not o)
        print("PASS=%d FAIL=%d" % (len(checks) - f, f))
        print("RESULT: %s" % ("PASS" if f == 0 else "FAIL"), flush=True)
        e2e.kill(pid)
        sys.exit(0 if f == 0 else 1)


if __name__ == "__main__":
    main()
