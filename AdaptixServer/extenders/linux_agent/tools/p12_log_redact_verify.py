#!/usr/bin/env python3
"""P2-E 验收：日志处理重做（方案 §4 S6 / §5 认知误区 10）

旧 cleanlog = 把一批 /var/log/* truncate 到 0。三个问题：
  ① 文件大小突变为 0 是**强告警信号**（任何大小基线/完整性监控都能发现）；
  ② auditd 存活时清 audit.log 会立刻产生一条"日志被清"的审计事件；
  ③ journald 完全没覆盖（现代发行版日志在 journal 里）。
重做后默认行为**不破坏任何文件的完整性**。

  T0  阳性对照：大小基线手段有效（truncate 一个文件 → 能被检出）—— ISS-020 规则
  T1  默认 cleanlog 不动 /var/log 下任何文件的 size+mtime（大小归零这条路已废弃）
  T2  auditd 存活时 audit.log **逐字节未变**（存活判定与行为一致）
  T3  journald：活跃 system.journal 仍在；归档 system@*.journal 被清（若有）
  T4  等长改写：needle 消失、替换串出现、**文件 size 一个字节不变**
  T5  跨块边界（8192）：needle 横跨缓冲区边界仍命中，size 不变
  T6  相邻 needle 不重复计数、不破坏长度
  T7  回归：改写后 agent beacon 仍通
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e
from p01_detached_spawn import run_cmd
from gap11_session_soak import _rows, max_id

F1 = "/tmp/p12_f1"
F2 = "/tmp/p12_f2"
F3 = "/tmp/p12_f3"
R = []


def rec(item, ok, note=""):
    R.append(bool(ok))
    print("  [%-4s] %-46s %s" % ("PASS" if ok else "FAIL", item, note), flush=True)


def sh(c):
    return subprocess.run(["bash", "-lc", c], capture_output=True, text=True).stdout


def cl(aid, path="", pairs="", timeout=150):
    """下发 cleanlog（path/pairs 走 execute args → 服务器 msgpack），返回 a_text。

    **必须按 a_task_id 配对**：107（完成）行的 a_cmdline 是空的 —— 命令串只存在于
    106（下发）行。早先按 `"cleanlog" in a_cmdline` 去匹配 107 行，结果五次调用全部
    返回 None，把三个**本来完全正确**的回执（含 "auditd not running" 与
    "journal archived removed: N"）判成 FAIL。p03 的 run_bof 早就是按 task_id 配对的，
    这里照抄。
    """
    base = max_id(aid)
    e2e.send_execute(aid, "cleanlog",
                     {"command": "cleanlog", "path": path, "pairs": pairs})
    tid, t0 = None, time.time()
    while tid is None and time.time() - t0 < 30:
        for d in _rows(aid, base):
            if d.get("type") == 106 and "cleanlog" in str(d.get("a_cmdline") or ""):
                tid = d.get("a_task_id")
        if tid is None:
            time.sleep(1)
    if tid is None:
        return None                      # 连下发行都没有 = 命令根本没发出去
    t0 = time.time()
    while time.time() - t0 < timeout:
        for d in _rows(aid, base):
            if d.get("type") == 107 and d.get("a_task_id") == tid:
                return str(d.get("a_text") or "")
        time.sleep(1)
    return None


def varlog_snap():
    """(路径 -> size:mtime) 快照，含 /var/log 二层；journal 目录单列。"""
    d = {}
    for root, dirs, files in os.walk("/var/log"):
        if root.startswith("/var/log/journal"):
            continue
        for f in files:
            p = os.path.join(root, f)
            try:
                st = os.stat(p)
                d[p] = "%d:%d" % (st.st_size, int(st.st_mtime))
            except OSError:
                pass
    return d


def journals():
    """(活跃 *.journal 列表, 归档 @*.journal 列表)。"""
    act, arc = [], []
    for root, dirs, files in os.walk("/var/log/journal"):
        for f in files:
            if not f.endswith(".journal"):
                continue
            (arc if "@" in f else act).append(os.path.join(root, f))
    return sorted(act), sorted(arc)


def main():
    # ── T0 阳性对照：先证明"文件大小基线"这个检测手段本身有效 ──
    open(F1, "w").write("x" * 200)
    sz0 = os.path.getsize(F1)
    sh(": > %s" % F1)
    sz1 = os.path.getsize(F1)
    rec("T0 阳性对照：大小基线能检出 truncate",
        sz0 == 200 and sz1 == 0, "truncate 前=%d 后=%d（旧 cleanlog 的做法）" % (sz0, sz1))

    binp = e2e.gen_agent("TestHTTP", "p12")
    pid = e2e.launch(binp)
    aid = e2e.wait_registered("TestHTTP", pid, timeout=90)
    print("[p12] agent id=%s pid=%d" % (aid, pid), flush=True)
    time.sleep(2)

    # ── T1 + T2：默认 cleanlog 不破坏 /var/log ──
    # 判据必须**对后台日志噪声免疫**：这台机器上 syslog/sshd 一直在往 auth.log /
    # kern.log / btmp 追加，"任一文件 size+mtime 都不变"这条断言会把**系统自己写的
    # 日志**判成 cleanlog 干的（实测就是这么误伤的）。
    # 后台只会追加、从不截断 —— 所以有意义的信号是"**文件变小**"（尤其 → 0），
    # 那正是"全量 truncate 自曝"这个要防的形态。再加一个 canary 做正面对照。
    CANARY = "/var/log/p12_canary"
    CANARY_BODY = "p12 canary\n" * 16
    open(CANARY, "w").write(CANARY_BODY)
    c0 = os.stat(CANARY)
    before = varlog_snap()
    audit_before = sh("md5sum /var/log/audit/audit.log 2>/dev/null").strip()
    act0, arc0 = journals()
    t1 = cl(aid)
    after = varlog_snap()
    act1, arc1 = journals()

    def _sz(v):
        return int(v.split(":")[0])

    shrunk = sorted(k for k in set(before) & set(after)
                    if _sz(after[k]) < _sz(before[k]))
    zeroed = sorted(k for k in after
                    if _sz(after[k]) == 0 and k in before and _sz(before[k]) > 0)
    rec("T1 默认 cleanlog 不截断 /var/log（无文件变小 / 无归零）",
        not shrunk and not zeroed,
        "变小=%s 归零=%s（后台日志的\"变大\"不算变化）"
        % (shrunk[:3] or "无", zeroed[:3] or "无"))

    c1 = os.stat(CANARY)
    canary_ok = (c0.st_size == c1.st_size and int(c0.st_mtime) == int(c1.st_mtime)
                 and open(CANARY).read() == CANARY_BODY)
    rec("T1b canary 文件 size+mtime+内容 三项全不变", canary_ok,
        "size=%d mtime=%d" % (c0.st_size, int(c0.st_mtime)))
    try:
        os.unlink(CANARY)
    except OSError:
        pass

    audit_after = sh("md5sum /var/log/audit/audit.log 2>/dev/null").strip()
    alive = sh("pgrep -x auditd >/dev/null && echo yes || echo no").strip()
    if alive == "yes":
        ok = bool(audit_before) and audit_before == audit_after
        note = "auditd 存活，audit.log md5 %s" % ("未变" if ok else "变了!")
    else:
        ok = "not running" in (t1 or "")
        note = "auditd 未运行，回执明示 not running（不误报清理）"
    rec("T2 auditd 存活判定与行为一致", ok,
        "%s | 回执=%r" % (note, (t1 or "<无>").strip()[:80]))

    # ── T3：journald —— 活跃文件必须留、归档可以清 ──
    live_ok = set(act0) <= set(act1)          # 活跃 *.journal 一个都不许少
    rec("T3a journald 活跃 *.journal 未被删",
        live_ok, "活跃=%s（清前 %d → 清后 %d）" % ([os.path.basename(x) for x in act1], len(act0), len(act1)))
    if arc0:
        rec("T3b journald 归档 system@*.journal 被清",
            len(arc1) < len(arc0), "归档 %d → %d" % (len(arc0), len(arc1)))
    else:
        rec("T3b journald 归档清理（无归档可清，跳过）", True, "清前无 @*.journal 归档")

    # ── T4：等长就地改写 ──
    nd, rp = "SECRETMARKER", "REDACT"        # 12 vs 6 → 右侧补 '0' 对齐
    open(F1, "w").write("head SECRETMARKER tail\n")
    s0 = os.path.getsize(F1)
    t4 = cl(aid, F1, "%s=%s" % (nd, rp))
    body = open(F1).read()
    rec("T4 等长改写：needle 消失 + 替换串出现 + size 不变",
        os.path.getsize(F1) == s0 and nd not in body and "REDACT000000" in body,
        "size %d→%d  内容=%r  回执=%r"
        % (s0, os.path.getsize(F1), body.strip(), (t4 or "").strip()[:60]))

    # ── T5：needle 横跨 8192 缓冲边界 ──
    nd5 = "ZZSECRETZZ"                        # 10 字节
    off = 8185                                # 8185..8194 跨过 offset 8192
    blob = bytearray(b"A" * 20000)
    blob[off:off + len(nd5)] = nd5.encode()
    open(F2, "wb").write(bytes(blob))
    s0 = os.path.getsize(F2)
    t5 = cl(aid, F2, "%s=%s" % (nd5, "QQ"))
    raw = open(F2, "rb").read()
    rec("T5 跨 8192 边界命中且 size 不变",
        os.path.getsize(F2) == s0 and nd5.encode() not in raw
        and raw[off:off + len(nd5)] == b"QQ00000000",
        "size %d→%d  @%d=%r" % (s0, os.path.getsize(F2), off, raw[off:off + len(nd5)]))

    # ── T6：相邻 needle 不重复计数 / 长度不变 ──
    open(F3, "w").write("abababab")
    s0 = os.path.getsize(F3)
    t6 = cl(aid, F3, "ababab=XY")
    body3 = open(F3).read()
    m = re.search(r"redacted (\d+)", t6 or "")
    rec("T6 相邻 needle 只按非重叠计数、长度不变",
        os.path.getsize(F3) == s0 and body3 == "XY0000ab" and m and m.group(1) == "1",
        "内容=%r hits=%s" % (body3, m.group(1) if m else "?"))

    # ── T7：回归 —— 改写后 agent 仍通 ──
    lat = run_cmd(aid, "getuid", 90)
    rec("T7 日志处理后 beacon 仍通", lat is not None, "getuid 回执=%ss" % lat)

    for f in (F1, F2, F3):
        try:
            os.unlink(f)
        except OSError:
            pass
    e2e.kill(pid)

    print("\nPASS=%d FAIL=%d" % (R.count(True), R.count(False)))
    print("RESULT: %s" % ("PASS" if all(R) else "FAIL"))


if __name__ == "__main__":
    main()
