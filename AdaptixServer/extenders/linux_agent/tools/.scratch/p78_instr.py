# -*- coding: utf-8 -*-
"""p20 插桩：给每一步之间装"逐步验活"，把 agent 的死亡时刻定位到具体某一步。

★ 为什么验活不能用"再发一个探针"：agent 已经死了的时候，那只会得到**另一次超时**
—— 与"探针慢 / 输出迟到"在台面上**完全同形**。判据必须站在 agent 之外：
`Agents.LastTick`（服务端记的账）+ `/proc/<pid>/status`（内核的真相）。

每处替换断言唯一命中，否则不写盘。
"""
import io, sys

P = "/root/cc/AdaptixServer/extenders/linux_agent/tools/p20_arg_channel_verify.py"
s = io.open(P, encoding="utf-8").read()
R = []


def rep(tag, old, new):
    global s
    n = s.count(old)
    R.append((tag, n))
    if n != 1:
        return
    s = s.replace(old, new)


# ── P1 观测原语：插在 _probe_lines 之前 ──────────────────────────────────
rep("P1-life", '''def _probe_lines(aid, after_id, timeout=45):''',
'''def _life(aid, pid):
    """★ 逐步验活：一个**不需要 agent 配合**的活性观测面（ISS-078 的学费）。

    为什么不能用"再发一个探针"验活：agent 已经死了的时候，那只会得到**另一次
    超时** —— 与"探针慢 / 输出迟到"在台面上完全同形。判据必须站在 agent 之外：

      · `Agents.LastTick` —— 服务端每次收到心跳就更新（**整数 Unix 秒**）。agent
        只要还活着就一定会来更新它，**不需要它额外做什么**。主键列名是 `Id`，
        **不是** `AgentId`（猜错列名的代价见 ISS-078）。
      · `/proc/<pid>/status` 的 `State` —— 内核的真相。`gone` = 进程已消失；
        `Z` = 已死但没人收尸，**与"活着"是两件事**，必须分开报。

    返回 (lasttick, alive)，alive ∈ {"R/S/D", "Z", "gone", "?"}。
    """
    lt = None
    try:
        import sqlite3
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        row = con.execute("SELECT LastTick FROM Agents WHERE Id=?", (aid,)).fetchone()
        con.close()
        lt = int(row[0]) if row and row[0] is not None else None
    except Exception as e:
        print("[p20] WARN _life sqlite: %s" % e, flush=True)
    alive = "?"
    try:
        with open("/proc/%s/status" % pid) as f:
            for l in f:
                if l.startswith("State:"):
                    alive = l.split()[1]
                    break
    except OSError:
        alive = "gone"
    return lt, alive


LIFE = []          # [(tag, LastTick, alive)] —— 收尾时整表打出来


def life(tag, aid, pid):
    lt, alive = _life(aid, pid)
    now = int(time.time())
    LIFE.append((tag, lt, alive))
    print("[p20] LIFE %-12s LastTick=%s (%s秒前) proc=%s"
          % (tag, lt, ("?" if lt is None else now - lt), alive), flush=True)
    return lt, alive


def _probe_lines(aid, after_id, timeout=45):''')

# ── P2 main：起点 ─────────────────────────────────────────────────────────
rep("P2-start", '''        print("[p20] agent id=%s pid=%s" % (aid, pid), flush=True)''',
'''        print("[p20] agent id=%s pid=%s" % (aid, pid), flush=True)
        life("start", aid, pid)''')

# ── P3..P6 每步之间 ───────────────────────────────────────────────────────
rep("P3-T1", '''            "head4=%s len=%s toks=%s" % (h, bl, toks[:3]))''',
'''            "head4=%s len=%s toks=%s" % (h, bl, toks[:3]))
        life("T1", aid, pid)''')

rep("P4-T2", '''            "head4=%s len=%s toks=%s" % (h2, bl2, toks2[:3]))''',
'''            "head4=%s len=%s toks=%s" % (h2, bl2, toks2[:3]))
        life("T2", aid, pid)''')

rep("P5-T3", '''            "head4=%s(应为 %d=len('%s')) toks=%s" % (h3, len(EXPECT[0]), EXPECT[0], toks3[:3]))''',
'''            "head4=%s(应为 %d=len('%s')) toks=%s" % (h3, len(EXPECT[0]), EXPECT[0], toks3[:3]))
        life("T3", aid, pid)''')

rep("P6-T4", '''            "输入 'A B C' → head4=%s toks=%s（3 段，非 2 段）" % (h4, toks4[:3]))''',
'''            "输入 'A B C' → head4=%s toks=%s（3 段，非 2 段）" % (h4, toks4[:3]))
        life("T4", aid, pid)''')

# ── P7 T5 块：每次 run_probe 前后都验活 ───────────────────────────────────
rep("P7-T5block", '''        (hl0, bll0, _), _ = run_probe(aid, "ALPHA", True, timeout=45, retries=1)
        live_pre = bll0 is not None
        (h5a, bl5a, t5a), _ = run_probe(aid, "", use_b64=False, omit=True, timeout=90)
        (h5b, bl5b, t5b), _ = run_probe(aid, "", use_b64=False, timeout=90)
        (hl1, bll1, _), _ = run_probe(aid, "ALPHA", True, timeout=45, retries=1)
        live_post = bll1 is not None''',
'''        # ★★ 逐步验活：T5 里三次 run_probe 的**每一次**前后都记一笔
        #   （探针式验活只说明"探针有回执"，不说明"agent 活着" —— 两者要分开。
        #    上一批就是在这里把"agent 早已停跳"误读成"空串把 agent 打死了"。）
        life("pre-T5", aid, pid)
        (hl0, bll0, _), _ = run_probe(aid, "ALPHA", True, timeout=45, retries=1)
        live_pre = bll0 is not None
        life("after-T5pre", aid, pid)

        (h5a, bl5a, t5a), _ = run_probe(aid, "", use_b64=False, omit=True, timeout=90)
        life("after-T5a-omit", aid, pid)

        (h5b, bl5b, t5b), _ = run_probe(aid, "", use_b64=False, timeout=90)
        life("after-T5b-empty", aid, pid)

        (hl1, bll1, _), _ = run_probe(aid, "ALPHA", True, timeout=45, retries=1)
        live_post = bll1 is not None
        life("after-T5post", aid, pid)''')

# ── P8 收尾：打活性轨迹 + agent 尸检日志 ─────────────────────────────────
rep("P8-trace", '''        print("", flush=True)
        npass = sum(1 for _, s, _ in R if s == "PASS")''',
'''        # ── ★ 逐步活性轨迹 + agent 尸检（判据之外，只为定位死因）──────────
        print("", flush=True)
        print("[p20] ── 活性轨迹（LastTick 是服务端记的账，不依赖 agent 配合）──", flush=True)
        prev = None
        for tag, lt, alive in LIFE:
            d = ""
            if prev is not None and lt is not None:
                d = " (+%ds)" % (lt - prev)
            print("      %-16s LastTick=%s%s  proc=%s" % (tag, lt, d, alive), flush=True)
            if lt is not None:
                prev = lt
        log = getattr(e2e, "LAST_AGENT_LOG", None)
        if log:
            print("[p20] agent 尸检日志 %s:" % log, flush=True)
            try:
                with open(log, "rb") as f:
                    b = f.read(4096)
                print("      " + (b.decode("utf-8", "replace").strip() or "<空>"), flush=True)
            except Exception as e:
                print("      <读不到: %s>" % e, flush=True)

        npass = sum(1 for _, s, _ in R if s == "PASS")''')

bad = [t for t, n in R if n != 1]
if bad:
    print("ABORT 未命中/不唯一:", bad)
    for t, n in R:
        print("   ", t, n)
    sys.exit(1)
io.open(P, "w", encoding="utf-8").write(s)
print("OK 全部命中:", R)
