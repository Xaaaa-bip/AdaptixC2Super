#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""p20_arg_channel_verify.py — ISS-005 验收：BOF 参数两条通道是否逐字一致

背景：BOF 的参数 blob 形状是 `[4B LE count][ (4B LE len + bytes)* ]`。`BeaconDataParse()`
会**跳过前 4 字节**，所以少了 count 头 → 每个字段整体**错位 4 字节**。错位不报错：
BOF 照常跑、照常打回执，只是拿到的全是垃圾（第一个 token 的长度被当成"参数个数"）。

两条通道：

  A. `/agent/command/execute` 的 `param_data`(base64)  —— **调用方**打包（matrix_resolver.go
     与全部验收脚本走这条）
  B. console 明文分词回退（`param_data` 不是合法 base64 时）—— **服务器**打包
     （`pl_main.go` 的 fallback 分支，ISS-005 的现场）

断言设计（沿用 P4/P5 的教训）：**探针本身必须先被证明有分辨力**，否则 B 通道的
PASS 可能只是"探针太钝"。所以 T3 用**手工构造的错位 blob** 走 A 通道，探针必须看出来。

  T1  基准 · A 通道（base64）：head4 = 字段数，3 个 token 逐字
  T2  ★ 判据 · B 通道（明文）：与 T1 **逐字一致**（= ISS-005 在运行态已修）
  T3  ★ 分辨力对照 · 手工去掉 count 头 → 探针必须**报出**错位（否则 T2 无意义）
  T4  边界（INFO）：明文通道按空白分词 → 含空格的参数**无法**经 B 通道传递
  T5  边界（INFO）：`param_data` 缺失 → blob 是 **0 字节**（连 count 头都没有）

用法: p20_arg_channel_verify.py [--listener TestHTTP]
"""
import base64
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e                                    # noqa: E402

BOF_DIR = "/root/cc/run/extenders/linux_agent/bofs"
PROBE = "argprobe"

R = []


def rec(item, status, note=""):
    R.append((item, status, note))
    print("  [%-4s] %-46s %s" % (status, item, note), flush=True)


def blob(*fields):
    """正确形状：[4B count][ (4B len + bytes)* ]"""
    out = struct.pack("<I", len(fields))
    for f in fields:
        b = f.encode() if isinstance(f, str) else f
        out += struct.pack("<I", len(b)) + b
    return out


def misaligned(*fields):
    """**故意错位**：漏掉 count 头（= ISS-005 修前的形状）。"""
    out = b""
    for f in fields:
        b = f.encode() if isinstance(f, str) else f
        out += struct.pack("<I", len(b)) + b
    return out


# ─────────────────────────── 观测原语 ───────────────────────────
def _console_rows(aid, after_id=0):
    """读 Consoles 表里 Id > after_id 的行（按 Id 升序），返回 [(Id, 文本行列表)]。

    ⚠ 这里**不吞异常**。第一版写了 `except: return []`，于是"表名/字段名写错"与
    "探针没输出"在观测上完全同形 —— 三条断言一起报 FAIL，看着像被测对象坏了，
    实际是**观测者坏了**。这正是 P4/P5 记下的那条：阴性断言在手段失效时会照样
    给出结论，只是结论是假的。采集函数的异常必须往上冒。
    """
    import sqlite3
    con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
    rows = con.execute(
        "SELECT Id, CAST(Packet AS TEXT) FROM Consoles "
        "WHERE AgentId=? AND Id>? ORDER BY Id ASC", (aid, after_id)).fetchall()
    con.close()
    out = []
    for cid, pkt in rows:
        # BOF 输出落在 a_text（type=107）；Message 是任务标题，不是内容。
        txt = ""
        try:
            d = json.loads(pkt)
            txt = (d.get("a_text") or d.get("a_message") or "")
        except Exception as e:
            print("[p20] WARN Consoles 第 %s 行不是 JSON: %s" % (cid, e), flush=True)
        out.append((cid, [l for l in txt.splitlines() if l.strip()]))
    return out


def _max_console_id(aid):
    import sqlite3
    con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
    row = con.execute("SELECT COALESCE(MAX(Id),0) FROM Consoles WHERE AgentId=?",
                      (aid,)).fetchone()
    con.close()
    return row[0] if row else 0


def _ptype(aid, cid):
    """单行的 Packet Type。判据用得到它：106=任务创建 / 107=任务完成。
    单独一个 106 而没有 107 = "任务行在、完成行没来"（ISS-070 的签名）。"""
    import sqlite3, json as _json, re as _re
    try:
        con = sqlite3.connect("file:%s?mode=ro" % e2e.DB, uri=True, timeout=5)
        row = con.execute("SELECT CAST(Packet AS TEXT) FROM Consoles WHERE Id=?", (cid,)).fetchone()
        con.close()
        m = _re.search(r'"\"?[Tt]ype\"?\s*:\s*(\d+)', row[0]) if row else None
        return int(m.group(1)) if m else None
    except Exception:
        return None


def _life(aid, pid):
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


def _probe_lines(aid, after_id, timeout=45):
    """等一轮探针输出落齐。

    终止条件 = 出现 `<null>` 行（探针遍历到取不出字段时打的那行，每轮恰好一条）。
    返回 (最后一个 Id, 本轮全部含 [probe] 的行文本)。
    """
    t0 = time.time()
    last = after_id
    while time.time() - t0 < timeout:
        hit = []
        for cid, lines in _console_rows(aid, after_id):
            last = cid
            hit += [l for l in lines if "[probe]" in l]
        if any("<null>" in l for l in hit):
            return last, hit
        time.sleep(1)
    if not hit:
        # ★ 失败时**必须留下现场**：现在只报 "len=None"，而
        # "agent 没回执" 与 "agent 已死" 在日志里**完全同形**。
        # 按存证序列把窗口内的原始行 + 106/107 配对情况打出来。
        rows = _console_rows(aid, after_id)
        t106 = [c for c, _ in rows if _ptype(aid, c) == 106]
        t107 = [c for c, _ in rows if _ptype(aid, c) == 107]
        print("[p20] WARN 本轮没采到任何 [probe] 行（超时 %ss）"
              "窗口内行=%d 106行=%s 107行=%s"
              % (timeout, len(rows), t106[-3:], t107[-3:]), flush=True)
        for cid, lines in rows[-4:]:
            print("        raw id=%d type=%s %s"
                  % (cid, _ptype(aid, cid), (lines[0][:90] if lines else "")), flush=True)
    return last, hit


def _parse(hit):
    """把探针输出解析成 (head4, blob_len, [tokens])。"""
    head = blen = None
    toks = []
    for l in hit:
        if "blob=" in l and "head4=" in l:
            seg = l.split("blob=")[1]
            blen = int(seg.split()[0])
            head = int(seg.split("head4=")[1].split()[0])
        elif "tok[" in l:
            toks.append(l.split("'")[1] if "'" in l else l)
    return head, blen, toks


def run_probe(aid, param_data, use_b64, omit=False, timeout=45, retries=2):
    """发一轮探针。use_b64=True 走 A 通道，False 走 B 通道（明文回退）。

    omit=True 时**完全不带** param_data 键（与"空串"是两种不同的形状：
    键缺失 → blob 0 字节；空串 → 4 字节 count=0）。

    重试是必要的：BOF 输出经 "任务创建行(type=106) → 完成行(type=107)" 两跳落到
    Consoles 表，实测偶发某一跳迟到（首轮 p20 的 T5 就是这样：任务行在、完成行
    45s 内没来，而同一个请求单独复现 3/3 都是秒回）。**观测抖动不能让断言静默**
    —— 所以这里重试，而不是把拿不到值降级成 INFO。
    """
    args = {"command": "execute", "subcommand": "bof", "bof": "%s/%s.o" % (BOF_DIR, PROBE)}
    if not omit:
        # ★ 收口点：str/bytes 都在**这里**归一，不在调用点各写一遍。
        #   补丁版第一稿在调用点写了 run_probe(aid, "ALPHA", True, ...) —— str 喂给
        #   base64.b64encode ⇒ TypeError ⇒ 整个脚本在 T5 第一步 traceback 崩掉。
        #   而它**从未被执行过**，所以"验活"这个补丁看上去装上了、实际一行没生效
        #   （与 ISS-077「改了但调用点到不了」同族）。教训：判据/台面的补丁，
        #   写完必须至少空跑一次**它自己**，否则"绿"与"根本没跑"同形。
        if isinstance(param_data, str) and use_b64:
            param_data = param_data.encode()
        args["param_data"] = (base64.b64encode(param_data).decode() if use_b64
                              else param_data)
    for attempt in range(retries + 1):
        base = _max_console_id(aid)
        e2e.send_execute(aid, "execute bof %s.o" % PROBE, args)
        last, hit = _probe_lines(aid, base, timeout=timeout)
        if hit:
            return _parse(hit), last
        if attempt < retries:
            print("[p20] 重试第 %d 次（上一轮无输出）" % (attempt + 1), flush=True)
    return _parse([]), last


# ─────────────────────────────── 主流程 ───────────────────────────────
# 字段长度**必须互不相同、且都 ≠ 字段个数**，否则"正确"与"错位"的 head4 会撞在一起
# （例如 count=3 配 token "AAA"：正确读到的 count 与错位读到的 len('AAA') 都是 3，
# 判据就失去了分辨力）。这里 count=3，而 len('ALPHA')=5。
EXPECT = ["ALPHA", "BE", "GAMMA7"]


def main():
    argv = sys.argv[1:]
    listener = argv[argv.index("--listener") + 1] if "--listener" in argv else "TestHTTP"

    if not os.path.exists("%s/%s.o" % (BOF_DIR, PROBE)):
        print("FATAL: 探针未部署（先 bash tools/deploy.sh）")
        return 2

    pid = aid = None
    try:
        binp = e2e.gen_agent(listener, "p20")
        pid = e2e.launch(binp)
        aid = e2e.wait_registered(listener, pid, timeout=90)
        print("[p20] agent id=%s pid=%s" % (aid, pid), flush=True)
        life("start", aid, pid)

        # ── T1 基准：A 通道（调用方自己打包）──
        (h, bl, toks), _ = run_probe(aid, blob(*EXPECT), use_b64=True)
        ok1 = (h == len(EXPECT) and toks[:3] == EXPECT)
        rec("T1 基准 · A 通道(base64) head4=3 且 3 个 token 逐字", "PASS" if ok1 else "FAIL",
            "head4=%s len=%s toks=%s" % (h, bl, toks[:3]))
        life("T1", aid, pid)

        # ── T2 ★ 判据：B 通道（服务器明文分词回退）──
        (h2, bl2, toks2), _ = run_probe(aid, " ".join(EXPECT), use_b64=False)
        ok2 = (h2 == len(EXPECT) and toks2[:3] == EXPECT)
        rec("T2 ★ B 通道(明文) 与 A 通道逐字一致", "PASS" if ok2 else "FAIL",
            "head4=%s len=%s toks=%s" % (h2, bl2, toks2[:3]))
        life("T2", aid, pid)

        # ── T3 ★ 分辨力对照：手工去掉 count 头，探针**必须**报错位 ──
        # 少了 count 头 → head4 变成第一个 token 的长度（3），且第 2 个字段起全是垃圾。
        (h3, bl3, toks3), _ = run_probe(aid, misaligned(*EXPECT), use_b64=True)
        ok3 = (h3 == len(EXPECT[0]) and h3 != len(EXPECT) and toks3[:3] != EXPECT)
        rec("T3 ★ 分辨力对照 · 无 count 头时探针**报出**错位", "PASS" if ok3 else "FAIL",
            "head4=%s(应为 %d=len('%s')) toks=%s" % (h3, len(EXPECT[0]), EXPECT[0], toks3[:3]))
        life("T3", aid, pid)

        # ── T4 边界：明文通道按空白分词，含空格的参数传不过去 ──
        (h4, bl4, toks4), _ = run_probe(aid, "A B C", use_b64=False)
        rec("T4 边界 · 明文通道按空白分词（含空格的参数只能用 base64）", "INFO",
            "输入 'A B C' → head4=%s toks=%s（3 段，非 2 段）" % (h4, toks4[:3]))
        life("T4", aid, pid)

        # ── T5 边界：两种"没有参数"的形状**不一样** ──
        #   键缺失 → params 仍是 nil → blob **0 字节**（连 count 头都没有）
        #   空串   → 明文回退 → Fields("")=[] → blob **4 字节**，count=0
        # 两者都必须能被 BOF 安全消费（探针用 head4=0 / tok[0]=<null> 收场）。
        # ★★ 判别力：**发空串之前先验活、发完再验活一次**。
        #   没有这两次验活，"空串没回执"与"agent 已经死了"在判据里
        #   **完全同形** —— 而两者的处置天差地别（前者是致命 BUG，后者是台面问题）。
        #   归档纪律：凡"等回执"的断言，后面都该跟一次验活。
        # ★★ 逐步验活：T5 里三次 run_probe 的**每一次**前后都记一笔
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
        life("after-T5post", aid, pid)
        ok5 = (bl5a == 0 and bl5b == 4 and h5a == 0 and h5b == 0)
        rec("T5 边界 · 键缺失=0 字节 / 空串=4 字节(count=0)，都不崩",
            "PASS" if ok5 else "FAIL",
            "缺失: len=%s head4=%s | 空串: len=%s head4=%s | 验活 前=%s 后=%s"
            % (bl5a, h5a, bl5b, h5b, live_pre, live_post))
        life("T5-done", aid, pid)

        # ── T6 ★★ 一个**空字段**（count=1，唯一那个字段长度声明为 0）──
        #   这是全台面**唯一**能压到"**长度维度**"的形状，也是本脚本原先缺的一格：
        #   T5 的两种形状（键缺失 / 空串）里 `len` 都是 0 **且 `p == NULL`**（走 break），
        #   而这里 `parser->length == 4` ⇒ Extract 走到正常路径 ⇒ 返回
        #   **非 NULL 指针 + `*size == 0`** —— 于是 argprobe 的
        #   `n = (len > 0 && len < 79) ? len : 79` 取到 **n = 79**，
        #   从裸指针越界拷 79 字节。
        #   ★ 判据是**逐字**要求 tok[0] 为空串：修复前必然打出垃圾（或崩），
        #     修复后有且只有一种结果（`''`）。p23 只查"指针/NUL 维度"，
        #     把这种"指针非空、长度为零"判成 SAFE —— 判据必须覆盖**每个维度**。
        (h6, bl6, t6), _ = run_probe(aid, blob(b""), use_b64=True, timeout=45, retries=1)
        ok6 = (h6 == 1 and bl6 == 8 and t6[:1] == [""])
        rec("T6 ★★空字段(非NULL+len=0)：探针必须逐字打出空串",
            "PASS" if ok6 else "FAIL",
            "head4=%s blob=%s tok[0]=%r（应为 1 / 8 / ''）" % (h6, bl6, t6[:1]))
        life("T6-emptyfield", aid, pid)

        # ── T7 ★ 空字段**夹在中间**：证明"空"不终止迭代 ──
        #   这条锁住的是修 F1 时的一个**设计决定**：`length == 0` 仍然返回
        #   **非 NULL** 指针，而不是归一成 NULL。归一会让 `if (!p) break;` 在第二个
        #   字段就退出，第三个字段 'C' 永远取不到 —— 合法的"某个参数为空"
        #   就变成了**参数丢失**。没有这条判据，后人"顺手把 length==0 改成回 NULL"
        #   不会有任何台面变红（判据失效比判据变红更危险）。
        (h7, bl7, t7), _ = run_probe(aid, blob(b"A", b"", b"C"), use_b64=True,
                                     timeout=45, retries=1)
        ok7 = (h7 == 3 and t7[:3] == ["A", "", "C"])
        rec("T7 ★空字段在中间：后面的字段仍取得到（空 != 结束）",
            "PASS" if ok7 else "FAIL",
            "head4=%s toks=%r（应为 3 / ['A', '', 'C']）" % (h7, t7[:3]))
        life("T7-midempty", aid, pid)

        # ── ★ 逐步活性轨迹 + agent 尸检（判据之外，只为定位死因）──────────
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

        npass = sum(1 for _, s, _ in R if s == "PASS")
        nfail = sum(1 for _, s, _ in R if s == "FAIL")
        print("PASS=%d FAIL=%d" % (npass, nfail))
        print("RESULT: %s" % ("PASS" if nfail == 0 else "FAIL"))
        return 0 if nfail == 0 else 1
    finally:
        if pid:
            try:
                e2e.kill(pid)
            except Exception:
                pass


if __name__ == "__main__":
    sys.exit(main())
