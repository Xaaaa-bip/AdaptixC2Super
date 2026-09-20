#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""4445 libp2p listener 演示 —— 在**运行态**上真建一个 `protocol=libp2p` 的 listener。

## 为什么需要它

`l2p_e2e.py` 已经能对**已存在**的 listener 做端到端注册验收，并且按 `l_protocol`
切判据（`libp2p` ⇒ 外部身份必须是 `12D3KooW…` peer id）。缺的那一环只是
**"把 libp2p listener 建出来"**。本脚本补这一环，其余全部复用 `l2p_e2e`。

## 建法：**复制 tcp listener 的 config，只改三个键**

手工构造一份 config 会漏字段（`pl_transport.go` 的 Config 有十几个键，
`GetProfile()` 直接 `Encode(t.Config)`）⇒ 建出一个缺字段的坏 listener 比建不起来更糟。
所以：拿现成 `gopher`（tcp:4444）的完整 `l_data`，**只改**：

    protocol          : "tcp" → "libp2p"
    port_bind         : 4444  → 4445
    callback_addresses: "154.198.162.34:4444" → "/ip4/154.198.162.34/tcp/4445"

`/p2p/<peerID>` **不写** —— 由 listener 侧 `axPrepareLibp2p` 补
（批次4 `l2p_order.py` 修的就是"校验在前、补全在后"：原版 operation 按 UI 提示
只填 `/ip4/<ip>/tcp/<port>` 时 **listener 永远建不起来**，实测
`{"message":"Invalid multiaddr (need /p2p/<peerID>)..."}`）。
`l2p_privkey` 也**不写** ⇒ 由 listener 生成。

## ★ 判据设计：刻意避开"回显"

后端 `/listener/list` 回的 `l_data` 里可能有**回显输入**的字段。凡是我输入过的值，
拿它当判据就是自证。所以五条判据里**只认我输入不了的东西**：

  ① `GopherL2P` 出现在 `/listener/list`                        ← 存在性
  ② `l_protocol == "libp2p"`                                  ← 权威字段（实测取值 libp2p/tcp）
  ③ `l_status == "Listen"`                                    ← 真在监听
  ④ `l_agent_addr` 含 `/p2p/12D3KooW`                         ← ★ 我不输入 peer id ⇒
       非空只可能来自"私钥生成 + `axFillLibp2pAgentAddr` 跑过"（批次4 那条修法）
  ⑤ 4445 真在 LISTEN（`ss` 实测，不看 l_status 的自述）

## 红侧（`clean`）

`/listener/stop` 之后 4445 **必须**释放 —— 证明 ⑤ 那个观测确实来自本脚本建的
listener，而不是 4445 上本来就有的东西（否则 ⑤ 是假绿）。
★ API 没有 remove 端点（`create/edit/stop/pause/resume`，见 teamserver connector.go:376-380）
⇒ `clean` = stop（端口释放、DB 记录保留）。演示完**必须 stop**，否则 4445 常开。

用法：
    l2p_l2pdemo.py check     # 只读：现在有没有、状态如何（不碰运行态）
    l2p_l2pdemo.py create    # 建（幂等：已存在则只报告）
    l2p_l2pdemo.py verify    # 五条判据
    l2p_l2pdemo.py clean     # stop + 确认端口释放（红侧）
"""
import json
import os
import subprocess
import sys
sys.dont_write_bytecode = True   # ★ 批次13：import l2p_e2e 前先关掉字节码缓存

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l2p_e2e as E  # noqa: E402  ★ 复用 call/login/口令来源，不重写

NAME = "GopherL2P"
REG = "GopherTCP"
PORT = 4445
SRC = "gopher"          # 拿它的 config 当模板
IP = "154.198.162.34"


def ls_list(tok):
    st, body = E.call("/listener/list", None, tok, "GET", timeout=60)
    if st != 200:
        print("%s /listener/list -> %s %s" % (E.BAD, st, body[:200]))
        return None
    try:
        v = json.loads(body)
    except Exception as e:
        print("%s /listener/list 不是 JSON: %s" % (E.BAD, e))
        return None
    if not isinstance(v, list) or not v:
        # ★ 假绿形态「两个空串也相等」：空列表会让"该 listener 不存在"凭空成立。
        #   列表为空 ⇒ 观测通道失效，**不是**"不存在"。
        print("%s /listener/list 回了空列表 ⇒ 观测通道失效（不能据此说'不存在'）" % E.BAD)
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
    r = subprocess.run(["ss", "-ltn"], capture_output=True, text=True)
    return (":%d " % port) in r.stdout.replace("\n", " ")


# ★★ 被测实例的 API 端口：**必须显式钉死**，不能沿用 `l2p_e2e.BASE` 的默认值。
#   实测（本批次亲身踩到）：`l2p_e2e.py:51` 默认 `BASE` 是 **39012 = 探针 rig 实例**，
#   只有 `--port 38212` 才指向**运行态实例**（参数化在 `l2p_e2e.py:375`）。
#   第一版没走 `--port` ⇒ `create` 建到了 **rig** 上（4445 的持有者是 rig 的 pid 349928），
#   拿到一份"libp2p 可用"的结果，**测的根本不是被测实例**。
#   ⇒ 钉死 38212，并**打印出来**，让每次输出自证打的是谁。
APIPORT = int(os.environ.get("L2P_API_PORT", "38212"))
E.BASE = "https://127.0.0.1:%d/api" % APIPORT   # 覆盖模块默认值，复用 E.call/E.login


def tspid(port=None):
    """**被测实例**的 pid —— 从它的 API 端口反查。

    ★★ 实测教训（本批次，决定性）：`/listener/list` 是 **DB 视图，不是进程实况**。
      本机同时跑着两个 teamserver（`central.profile` 在 38212 / `profile.yaml` 在 38213）
      **共享同一份 DB** ⇒ DB 里 `l_status="Listen"` 的 listener，真正持有端口的可能是
      **另一个 pid**（实测：`l2p1`/`l2p2` 在 DB 里是 Listen，但 39014/39016 的持有者
      是 pid 349928，不是被测的 376319）。
      ⇒ 凡"listener 在听"的判据**必须落定到 pid**，否则是代理量。
    """
    p = port or APIPORT
    r = subprocess.run(["ss", "-ltnp"], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        f = line.split()
        if len(f) < 5 or not f[3].endswith(":%d" % p):
            continue
        for t in f[4:]:
            # ★ `ss -ltnp` 把 pid **裹在 users:(("adaptixserver",pid=N,fd=M)) 这一个 token 里**
            #   ⇒ `startswith("pid=")` **恒假**（第一版就是这么写错的：门恒假 = 假红，
            #   而它输出的 None 又长得像"观测通道失效"，差点被读成另一个结论）。
            i = t.find("pid=")
            if i >= 0:
                return t[i + 4:].split(",")[0].strip(")")
    return None


def template(tok):
    """任取一个 **tcp 型 GopherTCP** 条目当模板。取不到就**明确失败**，不猜字段。

    ★ 不写死名字：实测运行态**被第三方改过**（`gopher` 从列表消失、出现 `tcp1`/`tt1`），
      按名字找模板会让演示随时失效；按 `l_reg_name`+`l_protocol` 找才稳。
    """
    for x in ls_list(tok):
        if x.get("l_reg_name") == REG and x.get("l_protocol") == "tcp":
            raw = x.get("l_data") or ""
            try:
                d = json.loads(raw)
            except Exception as e:
                print("%s 模板 l_data 不是 JSON: %s" % (E.BAD, e))
                return None
            print("%s 模板取 %r（l_reg_name=%s l_protocol=tcp）"
                  % (E.INFO, x.get("l_name"), REG))
            return d
    print("%s 找不到任何 tcp 型 %s 条目当模板 ⇒ 不猜字段，拒绝构造" % (E.BAD, REG))
    return None


def do_create():
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    if find(tok, NAME):
        print("%s %s 已存在（幂等：不重复建）" % (E.INFO, NAME))
        print("RESULT: PASS（已存在）")
        return 0
    if listening(PORT):
        print("%s %d 已被占用 ⇒ 拒绝建（会撞端口）" % (E.BAD, PORT))
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
    # ★ 明确**不写** l2p_privkey / l2p_peer_id —— 判据④依赖这一点
    for k in ("l2p_privkey", "l2p_peer_id"):
        cfg.pop(k, None)
    print("%s 模板：protocol %r→'libp2p'  port_bind %r→%s  callback_addresses %r→%r"
          % (E.INFO, before.get("protocol"), before.get("port_bind"), PORT,
             before.get("callback_addresses"), cfg["callback_addresses"]))
    st, body = E.call("/listener/create",
                      {"name": NAME, "type": REG, "config": json.dumps(cfg)}, tok)
    print("%s /listener/create -> %s %s" % (E.INFO, st, body[:400]))
    if st != 200 or '"ok":true' not in body.replace(" ", ""):
        print("%s 建失败（后端原话在上）" % E.BAD)
        print("RESULT: FAIL")
        return 1
    x = find(tok, NAME)
    if x is None:
        print("%s ok=true 但 /listener/list 里没有 %s ⇒ 不认这个成功" % (E.BAD, NAME))
        print("RESULT: FAIL")
        return 1
    print("%s 已建：l_protocol=%s l_status=%s l_agent_addr=%s"
          % (E.INFO, x.get("l_protocol"), x.get("l_status"), x.get("l_agent_addr")))
    print("RESULT: PASS")
    return 0


def do_verify():
    ok = True
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    x = find(tok, NAME)
    # ① 存在
    if x is None:
        print("%s ① %s 不在 /listener/list 里" % (E.BAD, NAME))
        print("RESULT: FAIL")
        return 1
    print("%s ① %s 存在" % (E.OK, NAME))
    # ② 权威字段
    p = x.get("l_protocol")
    if p != "libp2p":
        print("%s ② l_protocol=%r ≠ 'libp2p'" % (E.BAD, p))
        ok = False
    else:
        print("%s ② l_protocol=%s（权威字段）" % (E.OK, p))
    # ③ 状态
    s = x.get("l_status")
    if s != "Listen":
        print("%s ③ l_status=%r ≠ 'Listen'" % (E.BAD, s))
        ok = False
    else:
        print("%s ③ l_status=%s" % (E.OK, s))
    # ④ ★ 非回显判据：peer id
    addr = x.get("l_agent_addr") or ""
    if "/p2p/12D3KooW" not in addr:
        print("%s ④ l_agent_addr=%r 里没有 /p2p/12D3KooW ⇒ 私钥没生成或补全没跑"
              "（我不输入 peer id，所以这条不是回显）" % (E.BAD, addr))
        ok = False
    else:
        pid = addr.split("/p2p/")[-1].split("/")[0]
        print("%s ④ l_agent_addr 含 peer id %s…（%d 字符，非回显）"
              % (E.OK, pid[:14], len(pid)))
    # ⑤ 端口实测
    if not listening(PORT):
        print("%s ⑤ %d 未在 LISTEN（ss 实测）" % (E.BAD, PORT))
        ok = False
    else:
        print("%s ⑤ %d 在 LISTEN（ss 实测）" % (E.OK, PORT))
    # ⑥ ★★ 端口持有者必须**就是被测实例**（本批次新增，最硬的一条）
    want, got = tspid(), tspid(PORT)
    if want is None:
        print("%s ⑥ 反查不到被测实例（API :%d）的 pid ⇒ 观测通道失效" % (E.BAD, APIPORT))
        ok = False
    elif got is None:
        print("%s ⑥ %d 拿不到 pid 持有者" % (E.BAD, PORT))
        ok = False
    elif got != want:
        print("%s ⑥ %d 的持有者是 pid %s，不是被测实例 pid %s ⇒ **别的实例**在听"
              "（DB 视图 ≠ 进程实况），不算数" % (E.BAD, PORT, got, want))
        ok = False
    else:
        print("%s ⑥ %d 持有者 pid %s == 被测实例（pid 落定，非 DB 视图）"
              % (E.OK, PORT, got))
    # 附带：config 里 libp2p 键长什么样
    try:
        d = json.loads(x.get("l_data") or "{}")
        print("%s l_data: protocol=%r l2p_peer_id=%r l2p_protocol=%r l2p_addrs=%r"
              % (E.INFO, d.get("protocol"), (d.get("l2p_peer_id") or "")[:20],
                 d.get("l2p_protocol"), d.get("l2p_addrs")))
    except Exception:
        pass
    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def do_clean():
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    if find(tok, NAME) is None:
        print("%s %s 不存在（无副作用）" % (E.INFO, NAME))
    else:
        st, body = E.call("/listener/stop", {"name": NAME, "type": REG}, tok)
        print("%s /listener/stop -> %s %s" % (E.INFO, st, body[:200]))
    # ★ 红侧：端口必须释放
    if listening(PORT):
        print("%s %d 仍在 LISTEN ⇒ 停止没生效（或占用者是别的进程）" % (E.BAD, PORT))
        subprocess.run(["ss", "-ltnp"], shell=False)
        print("RESULT: FAIL")
        return 1
    print("%s %d 已释放 ⇒ 证明 verify 的 ⑤ 确实由本脚本建的 listener 产生" % (E.OK, PORT))
    print("RESULT: PASS")
    return 0


def do_check():
    tok = E.login()
    if not tok:
        print("RESULT: FAIL（登录失败）")
        return 1
    x = find(tok, NAME)
    print("%s %s: %s" % (E.INFO, NAME,
                         "不存在" if x is None
                         else "l_protocol=%s l_status=%s l_agent_addr=%s"
                              % (x.get("l_protocol"), x.get("l_status"), x.get("l_agent_addr"))))
    print("%s %d LISTEN=%s" % (E.INFO, PORT, listening(PORT)))
    return 0


def main():
    m = sys.argv[1] if len(sys.argv) > 1 else "check"
    # ★ 每次输出自证"打的是哪个实例"：rig(39012) 与运行态(38212) 是两个独立实例、
    #   各有独立 DB 与端口段，光看 RESULT 分不出测的是谁（本批次踩过）。
    print("%s 被测实例 API=%s  pid=%s" % (E.INFO, E.BASE, tspid()))
    fn = {"check": do_check, "create": do_create,
          "verify": do_verify, "clean": do_clean}.get(m)
    if fn is None:
        print("%s 未知模式 %r" % (E.BAD, m))
        print("RESULT: FAIL")
        return 2
    return fn()


def entry():
    """★ S12：崩溃也必须留下 `RESULT:` 行（自动化框架只认它）。"""
    try:
        return main()
    except KeyboardInterrupt:
        print("\nRESULT: FAIL（被中断）")
        return 130
    except Exception:
        import traceback
        traceback.print_exc()
        print("\nRESULT: FAIL（脚本异常，见上方 traceback）")
        return 1


if __name__ == "__main__":
    sys.exit(entry())
