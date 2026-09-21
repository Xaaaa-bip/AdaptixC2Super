#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 端到端验收：**临时**给 listener 插桩，定位 agent 握手失败在哪一步。

    python3 l2p_diag.py check|apply|verify|revert

★★ 为什么需要它：agent 已经能连上（libp2p 侧 ESTAB 稳定、tcp1 侧连上后被服务端关），
  且 agent 侧实测 `bytes_sent:1132 bytes_received:1216` —— **双向都有数据**，说明不是
   传输问题。服务端 `pl_transport.go` 的 `handleConnection` 里失败一律 `goto ERR`，
   而 `ERR:` 只做两件事：

       ERR:
           _ = sendMsg(conn, []byte(t.Config.ErrorAnswer))   // ← agent 收到的 1216 字节
           _ = conn.Close()

   ⇒ **失败是静默的**：不打印、不记日志（`-debug` 也一样），agent 侧只看到一份
   「错误应答」伪装页。`/api/agent/list` 恒为 `null`，观测面上没有任何线索。

★★ 插桩点（3 处，都落在**语义关键行**上，不做宽泛子串替换）：
   ① handleConnection 入口 —— 服务端看到的 proto / encrypt_key / remote
   ② ERR 标签 —— 把 `recvData` 长度、`initMsg.Type`、以及**最后一次 err** 打出来
      （这一条最关键：`lastErr` 是 nil ⇒ 解密+解析都过了，失败在 TsAgentCreate；
       是 decrypt 错 ⇒ 密钥不匹配；recvLen==0 ⇒ 5 秒读超时）
   ③ TsAgentCreate 之后 —— 明确区分「建 agent 失败」与「建了但列表不显示」

★ 本脚本产出的是**诊断构建**，验收完必须 `revert`（C2 listener 不该有这些打印）。
★ 没有 git：`.bak-diag` + 本脚本是唯一回滚手段，REVERT 段与 APPLY 段写在一起。
"""
import os
import shutil
import subprocess
import sys

F = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp/pl_transport.go"
BAK = ".bak-diag"

# ---- ① 入口 ----
E_OLD = "func (t *TransportTCP) handleConnection(conn net.Conn, ts Teamserver) {\n"
E_NEW = (E_OLD +
         '\tfmt.Printf("[DIAG] ACCEPT name=%s proto=%s key=%s remote=%s ssl=%v\\n",\n'
         '\t\tt.Name, t.Config.Protocol, t.Config.EncryptKey, conn.RemoteAddr().String(), t.Config.Ssl)\n')

# ---- ② ERR 标签 ----
R_OLD = ("ERR:\n"
         "\t_ = sendMsg(conn, []byte(t.Config.ErrorAnswer))\n")
R_NEW = ("ERR:\n"
         '\tfmt.Printf("[DIAG] ERR name=%s proto=%s remote=%s recvLen=%d type=%d lastErr=%v\\n",\n'
         '\t\tt.Name, t.Config.Protocol, conn.RemoteAddr().String(), len(recvData), initMsg.Type, err)\n'
         "\t_ = sendMsg(conn, []byte(t.Config.ErrorAnswer))\n")

# ---- ③ TsAgentCreate ----
C_OLD = ('\t\t\t_, err = Ts.TsAgentCreate(agentType, agentId, initPack.Data, t.Name, ExternalIP, false)\n'
         '\t\t\tif err != nil {\n'
         '\t\t\t\tgoto ERR\n'
         '\t\t\t}\n'
         '\t\t} else {\n')
C_NEW = ('\t\t\t_, err = Ts.TsAgentCreate(agentType, agentId, initPack.Data, t.Name, ExternalIP, false)\n'
         '\t\t\tif err != nil {\n'
         '\t\t\t\tfmt.Printf("[DIAG] TsAgentCreate FAILED id=%s type=%s extIP=%s err=%v\\n",\n'
         '\t\t\t\t\tagentId, agentType, ExternalIP, err)\n'
         '\t\t\t\tgoto ERR\n'
         '\t\t\t}\n'
         '\t\t\tfmt.Printf("[DIAG] TsAgentCreate OK id=%s type=%s extIP=%s\\n", agentId, agentType, ExternalIP)\n'
         '\t\t} else {\n')

ANCHORS = (("entry", E_OLD), ("ERR", R_OLD), ("create", C_OLD))

GOFMT = None
for _c in ("/usr/local/go/bin/gofmt", "/usr/lib/go/bin/gofmt", "/usr/lib/go-1.25/bin/gofmt"):
    if os.path.exists(_c):
        GOFMT = _c
        break


def rd(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def wr(p, t):
    with open(p, "w", encoding="utf-8") as f:
        f.write(t)


def state(t):
    # ★ 判据不能拿 E_OLD 计数当"改没改"的判据：E_OLD 是 E_NEW 的**前缀**
    #   （插入点就在函数签名之后），替换后它**照样出现 1 次** ⇒ 状态永远判成 "old"。
    #   这正是归档里"判据自己算错"那一族的又一例。入口处改用**只在插桩后才存在**的
    #   判别串 `[DIAG] ACCEPT`。
    n = {k: t.count(a) for k, a in ANCHORS}
    n["diag"] = t.count("[DIAG] ACCEPT")
    if n["diag"] == 0 and n["ERR"] == 1 and n["create"] == 1:
        return "old", n
    if n["diag"] == 1 and n["ERR"] == 0 and n["create"] == 0:
        return "done", n
    return "???", n


def gofmt_check():
    if GOFMT is None:
        print("[BAD ] 找不到 gofmt —— 判据不可判，按失败处理")
        return False
    p = subprocess.run([GOFMT, "-l", F], capture_output=True, text=True)
    if p.returncode != 0:
        print("[BAD ] gofmt rc=%d stderr=%s" % (p.returncode, p.stderr.strip()[:400]))
        return False
    dirty = bool(p.stdout.strip())
    print("[%s] gofmt -l pl_transport.go %s" % ("BAD " if dirty else "ok  ", "有改动" if dirty else "干净"))
    if dirty:
        d = subprocess.run([GOFMT, "-d", F], capture_output=True, text=True)
        print("\n".join(d.stdout.split("\n")[:25]))
    return not dirty


def verify():
    t = rd(F)
    s, n = state(t)
    checks = [
        ("三处锚点全部消失（插桩已就位）", s == "done"),
        ("入口打印在（ACCEPT）", t.count("[DIAG] ACCEPT") == 1),
        ("ERR 打印在（含 lastErr）", t.count("[DIAG] ERR") == 1 and "lastErr=%v" in t),
        ("TsAgentCreate 成败都有打印",
         t.count("[DIAG] TsAgentCreate OK") == 1 and t.count("[DIAG] TsAgentCreate FAILED") == 1),
        ("ERR 仍保留错误应答（没改坏语义）",
         "_ = sendMsg(conn, []byte(t.Config.ErrorAnswer))" in t),
        ("fmt 已被使用（导入已在）", '"fmt"' in t),
        ("大括号平衡", t.count("{") == t.count("}")),
        ("无连续空行", "\n\n\n" not in t),
    ]
    for nm, ok in checks:
        print("[%s] %s" % ("ok  " if ok else "BAD ", nm))
    ok1 = gofmt_check()
    bad = sum(1 for _, ok in checks if not ok) + (0 if ok1 else 1)
    print("\nverify: %d 项不符" % bad)
    return 0 if bad == 0 else 1


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    if not os.path.exists(F):
        print("[BAD ] 找不到 %s" % F)
        return 2
    t = rd(F)
    s, n = state(t)
    print("[%-4s] entry=%d(前缀,恒1) ERR=%d create=%d diag=%d"
          % (s, n["entry"], n["ERR"], n["create"], n["diag"]))

    if mode == "check":
        for k, a in ANCHORS:
            print("  [%s] %-7s 出现 %d 次" % ("ok  " if t.count(a) == 1 else "BAD ", k, t.count(a)))
        if s == "old":
            gofmt_check()
        return 0 if s != "???" else 1

    if mode == "apply":
        if s == "???":
            print("[BAD ] 状态不明 —— 拒绝落盘")
            return 1
        if s == "done":
            print("[done] 已是目标态")
            return verify()
        for k, a in ANCHORS:
            if t.count(a) != 1:
                print("[BAD ] 锚点 %s 出现 %d 次（应 1）—— 拒绝落盘" % (k, t.count(a)))
                return 1
        shutil.copy2(F, F + BAK)
        # ★ 串行累积：每一处替换基于**已改好的**文本（归档「原子≠串行」）
        o = rd(F)
        o = o.replace(E_OLD, E_NEW, 1)
        o = o.replace(R_OLD, R_NEW, 1)
        o = o.replace(C_OLD, C_NEW, 1)
        wr(F, o)
        print("[ok  ] 已插桩（备份 pl_transport.go%s）" % BAK)
        return verify()

    if mode == "revert":
        if not os.path.exists(F + BAK):
            print("[BAD ] 缺 pl_transport.go%s —— 拒绝 revert（不许假装成功）" % BAK)
            return 1
        shutil.move(F + BAK, F)
        print("[ok  ] 已还原 pl_transport.go")
        return 0

    if mode == "verify":
        return verify()

    print("用法: %s check|apply|verify|revert" % sys.argv[0])
    return 2


if __name__ == "__main__":
    sys.exit(main())
