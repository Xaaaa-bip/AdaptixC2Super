#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次16 ★C2 级真链路：relay 预约 → circuit listener → agent 现编 → 经 relay 上线。

    L2P_API_PORT=39012 python3 l2p_relay_c2_e2e.py create|verify|clean
    # 建完再用 l2p_e2e.py 生成 + 跑 agent（那一步才是"链路真通"的判据）
    python3 l2p_e2e.py l2p3 --port 39012 --timeout 300

★★ 为什么还缺这一环（本脚本存在的理由）

  批次5 的 relay 验收 `l2p_relay_e2e.py` 跑在 **Go 测试进程内**（真 relay + 真 C2 host，
  **不经 teamserver**）；`l2p_l2pdemo.py` 建的又是**直连** listener（l_agent_addr =
  `/ip4/…/tcp/…/p2p/<c2id>`，不含 `/p2p-circuit`）⇒ **C2 级的 relay 路径从未跑过**。
  本脚本补的就是这一环：多给一个键 `l2p_relays`。

★ 与 l2p_l2pdemo.py 的差别只有三处

  1. 多一个键 `l2p_relays = "<relay 的 multiaddr，含 /p2p/<relayPeerID>>"`
  2. 名字/端口换成 l2p3 / 39017（不覆盖 rig 上已有的 l2p1、l2p2）
  3. relay 地址是**从 relay 日志里读出来的**（`NODE-ADDR`），不是手抄的
     —— relaynode/README.md 明写"让 operator 手抄 52 字符的 peer id 是最容易错的一步"。

★ 判据（每条都能红，且红必须红在真理由上）

  J1 `l_protocol == "libp2p"`                     存在性 + 权威字段
  J2 `l_data.l2p_addrs` 含 `/p2p-circuit/p2p/`    ★ 这条量的是**agent 侧唯一地址来源**：
       pl_main.go 的 `case "libp2p"` 里 `l2p_addrs` 一旦非空就**覆盖** callback_addresses。
       而 `l_data` 是 listener 侧 `Create` 里 `axFillLibp2pRelayAddrs` **之后**才
       `json.Marshal` 出去的（pl_main.go:127-133）⇒ 它含 circuit 段，就说明补全真跑了。
  J3 circuit 之前的 relay 前缀 == relay 日志里的 `NODE-ADDR`
       （没有这条，一个"写死别的 relay 地址"的实现也能让 J2 全绿）
  J4 rig 日志里出现 `[relay] 预约成功 relay=<id>` 且限额 ≥ 1h / 1 GiB
       ★ 非回显：这行只在 `relayclient.Reserve()` **真成功**之后才打（pl_libp2p.go:559-573）
  J5 `l_status == "Listen"`
  J6 :39017 真在 LISTEN **且占用者 cwd == rig 目录**
       （DB 自述不算数 —— 已归档教训：本机多实例共享 DB 时 `l_status` 是 DB 视图，
       真正持有端口的是另一个 pid）
  J7 红侧：clean 之后 :39017 释放 ⇒ 证 J6 的观测确实来自**本** listener

  ⚠⚠ **`l_agent_addr` 不是判据载体，只作 INFO**：它是**直连通告地址**
     （= `callback_addresses` + `/p2p/<id>`，见 pl_main.go:112-113 与 :195），
     配了 relay 时**不是** agent 要拨的地址。本批次第一版拿它当 J2/J3 ⇒ **假红**，
     实测 `l_agent_addr=/ip4/127.0.0.1/tcp/39017/p2p/<c2id>`（无 circuit）而 relay
     预约**已经成功**。归档为坑27「判据载体 ≠ 功能载体」。

★ **relay 侧没有独立判据**：实测 axrelay 只在启动时打 `NODE-*`（relaynode/main.go:109-138），
  预约事件**不落日志** ⇒ relay 真的被用到的决定性证据在**下游**：agent 经 circuit 拨入
  并在 `/agent/list` 上线（`l2p_e2e.py` 的判据），**加上负向对照**
  —— 停掉 relay 后同一个 agent 必须**连不上**（否则说明它手里还有直连地址，
  "隐藏 C2 IP"就是假的）。

★ 环境：只在探针 rig（39012）上做，**绝不碰交付运行态 38212**；
  scratch 一律 `/root/gp5probe/**`（/tmp 会触发 p17 T6 的时间窗假红）。
"""
import glob
import json
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l2p_e2e as E  # noqa: E402  ★ 复用 call/login，不重写

NAME = "l2p3"
REG = "GopherTCP"
PORT = 39017
IP = "127.0.0.1"
RIG = "/root/gp5probe/l2prun"
RELAY_LOG_GLOB = "/root/gp5probe/axrelay*.log"
RIG_LOG_GLOB = "/root/gp5probe/l2prun/server.log*"

# ★ 被测实例的 API 端口必须**显式钉死并打印**（l2p_l2pdemo.py:88-96 的血泪：
#   不钉端口 ⇒ 建到了别的实例上，拿到一份"能用"的结果而**测的根本不是被测实例**）。
APIPORT = int(os.environ.get("L2P_API_PORT", "39012"))
E.BASE = "https://127.0.0.1:%d/api" % APIPORT

OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"


def ck(cond, okmsg, badmsg):
    if cond:
        print("  %s %s" % (OK, okmsg))
    else:
        print("  %s %s" % (BAD, badmsg))
    return bool(cond)


def relay_addr():
    """从 relay 日志里读 NODE-ADDR（不手抄 peer id）。

    取不到返回 None，由调用方**明确失败** —— 不猜、不回退到硬编码的旧 peer id。
    """
    for p in sorted(glob.glob(RELAY_LOG_GLOB), key=os.path.getmtime, reverse=True):
        try:
            with open(p, errors="replace") as f:
                for ln in f:
                    if ln.startswith("NODE-ADDR /"):
                        return ln.split(None, 1)[1].strip(), p
        except Exception:
            continue
    return None, None


def as_str(v):
    """StringOrArray 取出来的可能是字符串也可能是列表；统一成字符串。"""
    if isinstance(v, list):
        return ", ".join(str(x) for x in v)
    return v or ""


def relay_reservation(relay_id):
    """从 rig 日志里读 `[relay] 预约成功` 那一行。

    ★ 非回显：这行只在 `relayclient.Reserve()` **真成功**之后才打
      （pl_libp2p.go:559-573），operator 无法"输入"它。
    取不到返回 None ⇒ 由调用方判红（不把"没检查"当放过）。
    """
    for p in sorted(glob.glob(RIG_LOG_GLOB), key=os.path.getmtime, reverse=True):
        try:
            with open(p, errors="replace") as f:
                for ln in f:
                    if "[relay] 预约成功" in ln and relay_id in ln:
                        return ln.strip(), p
        except Exception:
            continue
    return None, None


def limit_secs(txt):
    """'1h0m0s' → 3600。"""
    n = 0
    for v, u in re.findall(r"(\d+)([hms])", txt):
        n += int(v) * {"h": 3600, "m": 60, "s": 1}[u]
    return n


def ls_list(tok):
    st, body = E.call("/listener/list", None, tok, "GET", timeout=60)
    if st != 200:
        print("%s /listener/list -> %s %s" % (BAD, st, body[:200]))
        return None
    try:
        v = json.loads(body)
    except Exception as e:
        print("%s /listener/list 不是 JSON: %s" % (BAD, e))
        return None
    if not isinstance(v, list) or not v:
        # ★ 假绿形态「两个空串也相等」：空列表会让"该 listener 不存在"凭空成立。
        print("%s /listener/list 空列表 ⇒ 观测通道失效（不能据此说'不存在'）" % BAD)
        return None
    return v


def find(tok, name):
    v = ls_list(tok)
    if v is None:
        raise RuntimeError("listener 列表观测通道失效 ⇒ 拒绝给出'存在/不存在'的结论")
    for x in v:
        if x.get("l_name") == name:
            return x
    return None


def listening(port):
    out = subprocess.run(["ss", "-ltn"], capture_output=True, text=True).stdout
    return (":%d " % port) in out.replace("\n", " ")


def port_owner(port):
    """`:PORT` 的占用者 pid + cwd。

    ★ `ss -ltnp` 把 pid **裹在** `users:(("adaptixserver",pid=N,fd=M))` 这一个 token 里
      ⇒ `startswith("pid=")` **恒假**（l2p_l2pdemo.py:128 已归档的同款坑）。
    """
    out = subprocess.run(["ss", "-ltnp"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) < 5 or not f[3].endswith(":%d" % port):
            continue
        for t in f[4:]:
            i = t.find("pid=")
            if i >= 0:
                pid = t[i + 4:].split(",")[0].strip(")")
                cwd = ""
                try:
                    cwd = os.path.realpath("/proc/%s/cwd" % pid)
                except Exception:
                    pass
                return pid, cwd
    return None, ""


def template(tok):
    """取一个 **tcp 型 GopherTCP** 条目当模板；取不到就明确失败，不猜字段。

    ★ 不写死名字：按 `l_reg_name` + `l_protocol` 找才稳（l2p_l2pdemo.py:137-155 同款）。
    """
    for x in ls_list(tok):
        if x.get("l_reg_name") == REG and x.get("l_protocol") == "tcp":
            try:
                d = json.loads(x.get("l_data") or "")
            except Exception as e:
                print("%s 模板 %s 的 l_data 不是 JSON: %s" % (BAD, x.get("l_name"), e))
                return None
            print("%s 模板取 %r" % (INFO, x.get("l_name")))
            return d
    print("%s rig 上找不到 tcp 型 %s 当模板 ⇒ 不猜字段，拒绝构造" % (BAD, REG))
    return None


def do_create():
    print("== create @ :%d（L2P_API_PORT 钉死；rig 环境）==" % APIPORT)
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    rl, src = relay_addr()
    if not rl:
        print("%s 读不到 relay 的 NODE-ADDR（%s）⇒ 不猜 peer id，拒绝继续" % (BAD, RELAY_LOG_GLOB))
        print("RESULT: FAIL")
        return 1
    print("%s relay（从 %s 读）：%s" % (INFO, os.path.basename(src), rl))
    if find(tok, NAME):
        print("%s %s 已存在（幂等：不重复建）" % (INFO, NAME))
        print("RESULT: PASS（已存在）")
        return 0
    if listening(PORT):
        print("%s %d 已被占用 ⇒ 拒绝建（会撞端口）" % (BAD, PORT))
        print("RESULT: FAIL")
        return 1
    cfg = template(tok)
    if cfg is None:
        print("RESULT: FAIL")
        return 1
    before = dict(cfg)
    cfg["protocol"] = "libp2p"
    cfg["port_bind"] = PORT
    cfg["callback_addresses"] = "/ip4/%s/tcp/%d" % (IP, PORT)
    # ★ 协议 id 必须显式给：模板是 tcp 型，没有这个键；留空时两侧各用各的默认值，
    #   一旦不一致就是"流开不了"，而症状是 agent 永远不上线（与网络不通无法区分）。
    #   取值与 rig 上已验证可用的 l2p1/l2p2 一致。
    cfg["l2p_protocol"] = "/gopher/1.0.0"
    cfg["l2p_relays"] = rl          # ★ 本脚本与 l2p_l2pdemo 的唯一实质差别
    for k in ("l2p_privkey", "l2p_peer_id"):
        cfg.pop(k, None)            # 都不写：私钥由 listener 生成，peer id 由 listener 自报
    print("%s 改动：protocol %r→'libp2p'；port_bind %r→%d；callback_addresses %r→%r；"
          "l2p_protocol→%r；+l2p_relays=%r"
          % (INFO, before.get("protocol"), before.get("port_bind"), PORT,
             before.get("callback_addresses"), cfg["callback_addresses"],
             cfg["l2p_protocol"], rl))
    st, body = E.call("/listener/create",
                      {"name": NAME, "type": REG, "config": json.dumps(cfg)}, tok)
    print("%s /listener/create -> %s %s" % (INFO, st, body[:400]))
    if st != 200 or '"ok":true' not in body.replace(" ", ""):
        print("%s 建失败（后端原话在上）" % BAD)
        print("RESULT: FAIL")
        return 1
    x = find(tok, NAME)
    if x is None:
        print("%s ok=true 但 /listener/list 里没有 %s ⇒ 不认这个成功" % (BAD, NAME))
        print("RESULT: FAIL")
        return 1
    print("%s 已建：l_protocol=%s l_status=%s" % (INFO, x.get("l_protocol"), x.get("l_status")))
    print("%s l_agent_addr=%s" % (INFO, x.get("l_agent_addr")))
    print("RESULT: PASS")
    return 0


def do_verify():
    print("== verify @ :%d（L2P_API_PORT 钉死；rig 环境）==" % APIPORT)
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    rl, src = relay_addr()
    x = find(tok, NAME)
    if x is None:
        print("%s %s 不在 /listener/list ⇒ 后面的判据无从谈起" % (BAD, NAME))
        print("RESULT: FAIL")
        return 1
    try:
        cfg = json.loads(x.get("l_data") or "")
    except Exception as e:
        print("%s l_data 不是 JSON（%s）⇒ 读不到 l2p_addrs，J2/J3 不能评" % (BAD, e))
        print("RESULT: FAIL")
        return 1
    addrs = as_str(cfg.get("l2p_addrs"))
    rid = rl.rsplit("/p2p/", 1)[1] if rl and "/p2p/" in rl else ""
    line, rlog = relay_reservation(rid)

    print("%s 被测 %s：l_protocol=%s l_status=%s" % (INFO, NAME, x.get("l_protocol"), x.get("l_status")))
    print("%s l_data.l2p_addrs=%r  ← agent 侧唯一地址来源" % (INFO, addrs))
    print("%s ⚠ l_agent_addr=%r（直连通告地址，**不是** agent 要拨的地址 ⇒ 不作为判据）"
          % (INFO, x.get("l_agent_addr")))
    print("%s relay 日志（%s）：%s" % (INFO, os.path.basename(src) if src else "无", rl))
    print("%s rig 日志（%s）：%s" % (INFO, os.path.basename(rlog) if rlog else "无", line))

    ok = True
    ok &= ck(x.get("l_protocol") == "libp2p", "J1 协议 = libp2p",
             "J1 协议是 %r（应为 libp2p）" % x.get("l_protocol"))
    ok &= ck("/p2p-circuit/p2p/" in addrs,
             "J2 l2p_addrs 含 /p2p-circuit/p2p/ ⇒ 补全跑过、agent 只会拨 circuit 地址",
             "J2 l2p_addrs 里没有 /p2p-circuit ⇒ 配了 l2p_relays 却没换成 circuit 地址：%r" % addrs)
    pre = addrs.split("/p2p-circuit/")[0].strip() if "/p2p-circuit/" in addrs else ""
    ok &= ck(bool(rl) and pre == rl,
             "J3 circuit 前缀 == relay 日志里的 NODE-ADDR ⇒ 用的就是这台 relay",
             "J3 circuit 前缀 %r != relay 日志 %r" % (pre, rl))
    if line:
        m = re.search(r"限额=([0-9hms]+)/(\d+)", line)
        secs = limit_secs(m.group(1)) if m else 0
        byts = int(m.group(2)) if m else 0
        ok &= ck(secs >= 3600 and byts >= 1073741824,
                 "J4 预约成功且限额 %ss/%d 字节（≥1h / ≥1GiB）" % (secs, byts),
                 "J4 预约行的限额偏低：%ss/%d 字节（应 ≥1h / ≥1GiB）" % (secs, byts))
    else:
        ok &= ck(False, "", "J4 rig 日志里没有 `[relay] 预约成功 relay=%s` ⇒ 预约没发生（或日志被轮转）" % rid)
    ok &= ck(x.get("l_status") == "Listen", "J5 l_status = Listen",
             "J5 l_status = %r" % x.get("l_status"))
    pid, cwd = port_owner(PORT)
    ok &= ck(pid is not None and cwd == RIG,
             "J6 :%d 占用者 pid=%s cwd=%s（落定到进程，不信 DB 自述）" % (PORT, pid, cwd),
             "J6 :%d 占用者 pid=%s cwd=%s（应为 rig %s）" % (PORT, pid, cwd, RIG))
    print("RESULT: %s" % ("PASS（J1–J6 全绿）" if ok else "FAIL（见上）"))
    if ok:
        print("下一步（链路真通的判据）：python3 %s/l2p_e2e.py %s --port %d --timeout 300"
              % (os.path.dirname(os.path.abspath(__file__)), NAME, APIPORT))
    return 0 if ok else 1


def do_clean():
    print("== clean @ :%d（红侧：停掉后端口必须释放）==" % APIPORT)
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    x = find(tok, NAME)
    if x is None:
        print("%s %s 不存在（无副作用）" % (INFO, NAME))
    else:
        st, body = E.call("/listener/stop", {"name": NAME, "type": REG}, tok)
        print("%s /listener/stop -> %s %s" % (INFO, st, body[:200]))
    ok = ck(not listening(PORT),
            "J7 :%d 已释放 ⇒ J6 的观测确实来自本 listener" % PORT,
            "J7 :%d 仍在 LISTEN ⇒ 停止没生效（或占用者是别的进程）" % PORT)
    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    m = sys.argv[1] if len(sys.argv) > 1 else "verify"
    if m == "create":
        return do_create()
    if m == "verify":
        return do_verify()
    if m == "clean":
        return do_clean()
    print("用法: L2P_API_PORT=39012 %s create|verify|clean" % sys.argv[0])
    return 2


if __name__ == "__main__":
    sys.exit(main())
