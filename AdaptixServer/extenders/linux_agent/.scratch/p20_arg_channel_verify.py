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
        print("[p20] WARN 本轮没采到任何 [probe] 行（超时 %ss）" % timeout, flush=True)
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

        # ── T1 基准：A 通道（调用方自己打包）──
        (h, bl, toks), _ = run_probe(aid, blob(*EXPECT), use_b64=True)
        ok1 = (h == len(EXPECT) and toks[:3] == EXPECT)
        rec("T1 基准 · A 通道(base64) head4=3 且 3 个 token 逐字", "PASS" if ok1 else "FAIL",
            "head4=%s len=%s toks=%s" % (h, bl, toks[:3]))

        # ── T2 ★ 判据：B 通道（服务器明文分词回退）──
        (h2, bl2, toks2), _ = run_probe(aid, " ".join(EXPECT), use_b64=False)
        ok2 = (h2 == len(EXPECT) and toks2[:3] == EXPECT)
        rec("T2 ★ B 通道(明文) 与 A 通道逐字一致", "PASS" if ok2 else "FAIL",
            "head4=%s len=%s toks=%s" % (h2, bl2, toks2[:3]))

        # ── T3 ★ 分辨力对照：手工去掉 count 头，探针**必须**报错位 ──
        # 少了 count 头 → head4 变成第一个 token 的长度（3），且第 2 个字段起全是垃圾。
        (h3, bl3, toks3), _ = run_probe(aid, misaligned(*EXPECT), use_b64=True)
        ok3 = (h3 == len(EXPECT[0]) and h3 != len(EXPECT) and toks3[:3] != EXPECT)
        rec("T3 ★ 分辨力对照 · 无 count 头时探针**报出**错位", "PASS" if ok3 else "FAIL",
            "head4=%s(应为 %d=len('%s')) toks=%s" % (h3, len(EXPECT[0]), EXPECT[0], toks3[:3]))

        # ── T4 边界：明文通道按空白分词，含空格的参数传不过去 ──
        (h4, bl4, toks4), _ = run_probe(aid, "A B C", use_b64=False)
        rec("T4 边界 · 明文通道按空白分词（含空格的参数只能用 base64）", "INFO",
            "输入 'A B C' → head4=%s toks=%s（3 段，非 2 段）" % (h4, toks4[:3]))

        # ── T5 边界：两种"没有参数"的形状**不一样** ──
        #   键缺失 → params 仍是 nil → blob **0 字节**（连 count 头都没有）
        #   空串   → 明文回退 → Fields("")=[] → blob **4 字节**，count=0
        # 两者都必须能被 BOF 安全消费（探针用 head4=0 / tok[0]=<null> 收场）。
        (h5a, bl5a, t5a), _ = run_probe(aid, "", use_b64=False, omit=True, timeout=90)
        (h5b, bl5b, t5b), _ = run_probe(aid, "", use_b64=False, timeout=90)
        ok5 = (bl5a == 0 and bl5b == 4 and h5a == 0 and h5b == 0)
        rec("T5 边界 · 键缺失=0 字节 / 空串=4 字节(count=0)，都不崩",
            "PASS" if ok5 else "FAIL",
            "缺失: len=%s head4=%s | 空串: len=%s head4=%s" % (bl5a, h5a, bl5b, h5b))

        print("", flush=True)
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
