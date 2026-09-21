#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次9c 判据订正：把 l2p_rportfwd_e2e.py 的 J1/J1b 换成**有判别力**的判据

    python3 l2p_rportfwd4.py status|check|apply|revert|verify|all

★ 上一版（v1）apply 后 py_compile 失败，值得留档 —— 错误是**区间边界**引发的：

    pre-image 的真实结构（16 空格 = 在 `if hit:` 体内）：
        420  if hit:
        421-428  # ★★ 这里绝不能拿 p_active 当判据 ……（旧注释 8 行）
        429-430  print("  %s 观察项（不计入 PASS/FAIL）：…")          ← 可执行语句
        431-464  # ★ J1b 的**真**判据 ……（旧判据整块，也在 if hit: 体内）
        466  # J3 + J4：数据面                                          ← 8 空格，循环外

    v1 把 A2 写成「a=注释首行 … b='# ★ J1b 的真判据'」的区间替换
    ⇒ 区间**连 429-430 的 print 一起吞掉** ⇒ `if hit:` 后面只剩注释 ⇒
      `IndentationError: expected an indented block after 'if' statement`（第 431 行）。
    同时新 J1b 被按 8 空格写进 `for d in tunnels:` 循环体内 ⇒ 每条隧道都会重跑一遍。

    ★ 教训（本项目第 12 种假形态：「**区间吞掉语句体**」）：用「两行注释当边界」做区间
      替换时，**注释之间的可执行语句会被静默删掉**，而 Python 的报错位置指向**下游**
      （这里指 431 行，真正的错在 421-430 这段被吃掉的语句），
      极易被误读成「新插入的代码写坏了」。⇒ 边界必须落在**同一种句法角色**上
      （注释对注释、语句对语句），且**改完必须立刻 py_compile**（静态锚点判据全绿也发现不了）。

★ 为什么必须改判据（两条都是「判据本身的错」，不是实现的错）

  ① J1b 旧版**不可达**（恒假红）：它查 /agent/task/list 里有没有
     `Reverse port forward '<lport>' to '<t>:<p>'`。那条消息由 Type-1 的
     `case COMMAND_TUNNEL_REVERSE` 写进 task.Message，再经 TsTaskUpdate →
     TaskManager.Update（core/server/mgr_task.go:181）。而 Update 的**第一件事**是
     `agent.RunningTasks.Get(updateData.TaskId)`，**未命中即静默 return**（:188-191）。
     隧道任务走 extractTunnelTasks（ts_tasks.go:81-99），**从不**登记 RunningTasks
     ⇒ 消息永远进不了任务列表 ⇒ 无论链路好坏都红。
     ⇒ 这是「不可达判据」：不是假绿，是恒假红 —— 会诱使人去修本来正确的代码。

  ② J1 的注释里有**事实错误**：它写「REVERSE 的 p_active 恒为 false」。
     实测代数：`tunnel.Active` 建时 false（ts_tunnels.go:449），而
     `tunnel.Active = true` 在 `TsTunnelStart` 的 **if/else 之外**（:346）
     ⇒ REVERSE 同样被置位，而且是在**仅下发任务之后、agent 回答之前**。
     ⇒ 它是**乐观置位**，对「监听起没起」零判别力（既不能当判据，也不能说它恒 false）。

★ 新 J1b（负向可观测，非重言式）
  先占住 LPORT_C ⇒ agent 的 net.Listen 必然失败 ⇒ 回 Result:0
  ⇒ Type-1 case 调 TsTunnelUpdateRportfwd(..., false)  ⇒ core **DeleteTunnel**（:493）
  ⇒ 隧道从 /tunnel/list **消失**。
  ★ 前置正判据：脚本必须先证明自己**占得住**那个端口，否则「消失」与实现无关。
"""
import hashlib
import io
import os
import py_compile
import shutil
import sys

E2E = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp/patches/l2p_rportfwd_e2e.py"
BAKDIR = "/root/gp5probe/bak"
SUF = ".bak-judge"
OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"
PYC = "/root/gp5probe/tmp/.rpf_e2e_check.pyc"

# ── 编辑 ─────────────────────────────────────────────────────────────────────
NEW_DOC = """  J1 隧道登记       /tunnel/list 里出现 p_agent_id/p_port/p_fhost/p_fport 全对上的条目。
                    ★ 本判据**只看登记**，不看 `p_active` —— `tunnel.Active = true` 在
                      `TsTunnelStart` 的 if/else **之外**（ts_tunnels.go:346），REVERSE 同样
                      被**乐观置位**（仅下发任务之后、agent 回答之前）⇒ 对「监听到底起没起」
                      零判别力：拿它当判据是假绿，说它「恒为 false」是假红。降为观察项。
  J1b 监听失败被摘除 先由本脚本占住 LPORT_C ⇒ agent 的 net.Listen 必然 EADDRINUSE
                    ⇒ 回 Result:0 ⇒ Type-1 的 case 调 TsTunnelUpdateRportfwd(..., false)
                    ⇒ core **DeleteTunnel**（ts_tunnels.go:493）⇒ 隧道从 /tunnel/list 消失。
                    ★ 负向可观测、非重言式；且**必须带前置正判据**（脚本真占住了端口），
                      否则「隧道消失了」与实现无关，「空值即通过」。
                    ★ 旧版 J1b（查 /agent/task/list 的任务消息）**不可达**：那条消息由
                      Type-1 写进 task.Message，再经 TsTaskUpdate → TaskManager.Update，
                      而 Update 第一件事是 `agent.RunningTasks.Get(...)`，**未命中即静默
                      return**（mgr_task.go:188-191）；隧道任务走 extractTunnelTasks，
                      **从不**登记 RunningTasks ⇒ 消息永远进不了任务列表 ⇒ 恒假红。
"""

NEW_PACTIVE = """                # ★★ 这里**绝不能**拿 p_active 当判据（本注释订正过本文件里的一个**错说法**）：
                #   本文件原先写「REVERSE 的 p_active 恒为 false，是核心侧既存缺陷」——**错**。
                #   代数：`Active` 建隧道时是 false（ts_tunnels.go:449），而
                #   `tunnel.Active = true` 位于 `TsTunnelStart` 的 if/else **之外**（:346）
                #   ⇒ REVERSE 同样被置位，且时机是**仅下发任务之后、agent 回答之前**
                #   ⇒ 它是**乐观置位**：对「agent 到底有没有把监听起起来」零判别力。
                #   ★ 两种错法都在这一个字段上：当判据 ⇒ 假绿（永远 true）；
                #     说它恒 false ⇒ 假红。**错的不是代码，是判据。**
                #   ⇒ 降为**观察项**，如实记录（本批次不改 core）。
"""

NEW_J1B = """        # ── J1b（**重设计**）：负向可观测判据 —— 监听起不来时必须把隧道摘掉 ─────────
        # ★ 旧版 J1b 为什么作废（留档：本项目第 11 种假形态 ——「**不可达判据**」）：
        #   旧版查「/agent/task/list 里出现 Reverse port forward '<lport>' to '<t>:<p>'」。
        #   那条消息由 Type-1 的 `case COMMAND_TUNNEL_REVERSE` 写进 task.Message，再经
        #   TsTaskUpdate → TaskManager.Update（core/server/mgr_task.go:181）。而 Update 的
        #   **第一件事**就是 `agent.RunningTasks.Get(updateData.TaskId)`，**未命中即静默
        #   return**（:188-191，连一条日志都没有）。隧道任务走的是 extractTunnelTasks
        #   （ts_tasks.go:81-99），它**从不**登记 RunningTasks ⇒ 该消息永远进不了任务列表
        #   ⇒ **无论链路好坏 J1b 恒红**。
        #   ⇒ 它不是假绿，而是**恒假红** —— 更阴险：它会诱使我去「修」本来正确的代码。
        #     （upstream 亦如此，是既存缺陷，不是本批次引入；beacon 侧同理。）
        # ★ 新版仍是**非重言式**，只是把「正着看」换成「负着看」：
        #   ① 本脚本先自己占住 LPORT_C（绑 0.0.0.0）
        #   ② 再请求对 LPORT_C 起 rportfwd ⇒ agent 的 net.Listen("0.0.0.0:P") 必然
        #      EADDRINUSE ⇒ taskTunnelReverse 回 Result:0
        #   ③ Type-1 的 case 调 TsTunnelUpdateRportfwd(tunnelId, false)
        #   ④ core 的 else 分支执行 **DeleteTunnel**（ts_tunnels.go:493）
        #   ⑤ ⇒ 隧道从 /tunnel/list **消失**
        #   ★ 红只有一条路：那个 case 没生效（default: continue 把结果丢掉）⇒ 不删
        #     ⇒ 隧道一直留在列表里。绿也只有一条路：core 真的执行了删除。
        #   ★ 前置正判据（必须有）：先证明本脚本**占得住**那个端口 —— 否则 agent 会监听
        #     成功、隧道本就不该消失，本判据就失去意义（「负向观测必须先证通道有效」）。
        blocker, blocker_ok = None, False
        try:
            blocker = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            blocker.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            blocker.bind(("0.0.0.0", LPORT_C))
            blocker.listen(8)
            blocker_ok = True
        except OSError as ex:
            print("  %s J1b 前置不成立：占不住 :%d（%s）" % (BAD, LPORT_C, ex))
        ok &= ck(blocker_ok,
                 "J1b 前置：本脚本已占住 :%d（0.0.0.0）⇒ agent 那边必然监听失败" % LPORT_C,
                 "J1b 前置：占不住 :%d ⇒ 本判据**无法评估**（不报绿）" % LPORT_C)
        if blocker_ok:
            st, body = E.call("/tunnel/start/rportfwd",
                              {"agent_id": agent_id, "listen": True,
                               "desc": "rpf-e2e-neg", "port": LPORT_C,
                               "t_host": "127.0.0.1", "t_port": tgt["A"]["port"]},
                              tok, timeout=120)
            print("%s tunnel/start/rportfwd(neg :%d) -> %s %s"
                  % (INFO, LPORT_C, st, body[:160]))
            gone, t0, tid_neg = False, time.time(), ""
            try:
                tid_neg = json.loads(body).get("message", "") or ""
            except Exception:
                pass
            while time.time() - t0 < 30:
                st2, b2 = E.call("/tunnel/list", None, tok, "GET", timeout=60)
                try:
                    tl2 = json.loads(b2)
                except Exception:
                    tl2 = None
                if tl2 is not None:
                    still = [x for x in tl2
                             if x.get("p_agent_id") == agent_id
                             and str(x.get("p_port")) == str(LPORT_C)]
                    if not still:
                        gone = True
                        break
                time.sleep(2)
            ok &= ck(gone,
                     "J1b 监听失败被如实上报：:%d 的隧道已从 /tunnel/list 摘除"
                     "（Type-1 case 生效 + core 真的 DeleteTunnel）" % LPORT_C,
                     "J1b :%d 的监听起不来，隧道却**仍留在** /tunnel/list 里 ⇒ Type-1 的"
                     " case COMMAND_TUNNEL_REVERSE 没生效（结果被 default: continue 丢掉）"
                     % LPORT_C)
            if not gone and tid_neg:
                # 判据已经红了，这里只是不留脏现场（best-effort）
                for path in ("/tunnel/stop", "/tunnel/delete"):
                    try:
                        E.call(path, {"p_tunnel_id": tid_neg}, tok, timeout=30)
                    except Exception:
                        pass
        if blocker is not None:
            try:
                blocker.close()
            except Exception:
                pass
"""

EDITS = [
    dict(tag="A1 文档串", kind="rep",
         a="  J1b Type-1 真跑了  /agent/task/list 里出现任务消息",
         b="  J2 监听归属", new=NEW_DOC, fp="★ 本判据**只看登记**"),
    dict(tag="A2 p_active 注释（仅注释行）", kind="rep",
         a="                # ★★ 这里**绝不能**拿 p_active 当判据 —— 读 core 源码实测：\n",
         b="                print(\"  %s 观察项（不计入 PASS/FAIL）：隧道 :%d p_active=%r p_type=%s\"\n",
         new=NEW_PACTIVE, fp="本注释订正过本文件里的一个**错说法**"),
    dict(tag="A3 删旧 J1b（在 if hit: 内）", kind="del",
         a="                # ★ J1b 的**真**判据：Type-1 的 case 到底跑没跑 —— 去看**任务消息**。\n",
         b="\n        # J3 + J4：数据面", new="", fp=None),
    dict(tag="A4 插新 J1b（循环外）", kind="ins",
         a="        # J3 + J4：数据面\n", b=None,
         new=NEW_J1B + "        # J3 + J4：数据面\n", fp="── J1b（**重设计**）"),
    dict(tag="A5 LPORT_C 常量", kind="ins",
         a="LPORT_B = 45112\n", b=None,
         new=("LPORT_B = 45112\n"
              "LPORT_C = 45113        # J1b 负向判据专用：由本脚本自己占住，逼 agent 监听失败\n"),
         fp="LPORT_C = 45113"),
]


def rd(p):
    return io.open(p, encoding="utf-8").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="\n").write(s)


def md5(p):
    return hashlib.md5(io.open(p, "rb").read()).hexdigest()


def bakpath():
    return os.path.join(BAKDIR, "patches.l2p_rportfwd_e2e.py" + SUF)


def applied(e, s):
    """★ 用显式指纹判「已应用」，不要用 `a not in s`：
       A2 的锚 a 是 new 的前缀子串 ⇒ 那套写法会把它误判成「未应用」（v1 实测 3/4）。"""
    if e["kind"] == "del":
        return (e["a"] not in s) and (e["b"] in s)
    return (e["fp"] is not None) and (s.count(e["fp"]) == 1)


def apply_one(e, s):
    """返回 (新串, 错误)。ins 走 old⊆new；rep/del 走 a..b 区间。"""
    if applied(e, s):
        return s, None
    if e["kind"] == "ins":
        if s.count(e["a"]) != 1:
            return s, "锚点不唯一（%d）" % s.count(e["a"])
        return s.replace(e["a"], e["new"], 1), None
    if s.count(e["a"]) != 1 or s.count(e["b"]) != 1:
        return s, "锚不唯一（a=%d b=%d）" % (s.count(e["a"]), s.count(e["b"]))
    i, j = s.find(e["a"]), s.find(e["b"])
    if i < 0:
        return s, "起始锚找不到"
    if j <= i:
        return s, "结束锚在起始锚之前（区间非法 ⇒ 拒绝，避免吞语句体）"
    return s[:i] + e["new"] + s[j:], None


def cmd_status():
    s = rd(E2E)
    print("== 现状 ==  md5=%s size=%d" % (md5(E2E), os.path.getsize(E2E)))
    for e in EDITS:
        print("  %-28s 已应用=%s" % (e["tag"], applied(e, s)))


def cmd_check():
    print("== LINT + 待办 ==")
    s = rd(E2E)
    ok = True
    for e in EDITS:
        na, nb = s.count(e["a"]), (s.count(e["b"]) if e["b"] else None)
        good = applied(e, s) or (na == 1 and (nb is None or nb == 1))
        ok &= good
        print("  %s %-28s 锚(a=%d%s) 已应用=%s"
              % (OK if good else BAD, e["tag"], na,
                 "" if nb is None else " b=%d" % nb, applied(e, s)))
    print("  %s LINT: %s" % (OK if ok else BAD, "OK" if ok else "FAIL"))
    print("  %s %d/%d todo" % (INFO, sum(1 for e in EDITS if not applied(e, s)), len(EDITS)))
    return ok


def cmd_apply():
    print("== APPLY ==")
    b = bakpath()
    if not os.path.exists(b):
        os.makedirs(BAKDIR, exist_ok=True)
        shutil.copy2(E2E, b)
        print("  %s 回滚点 %s（md5=%s）" % (OK, os.path.basename(b), md5(b)[:12]))
    else:
        print("  %s 回滚点已存在，复用（md5=%s）" % (INFO, md5(b)[:12]))
    s = rd(E2E)
    for e in EDITS:
        s2, err = apply_one(e, s)
        if err:
            print("  %s %-28s %s ⇒ 拒绝（不动盘）" % (BAD, e["tag"], err))
            return False
        s = s2
        print("  %s %-28s ok" % (OK, e["tag"]))
    wr(E2E, s)
    print("  %s APPLY 完成：%d 处" % (OK, len(EDITS)))
    return True


def cmd_revert():
    b = bakpath()
    if not os.path.exists(b):
        print("  %s 缺回滚点 %s" % (BAD, b))
        return False
    shutil.copy2(b, E2E)
    print("  %s 已还原自 %s（md5=%s）" % (OK, os.path.basename(b), md5(E2E)[:12]))
    return True


def cmd_verify():
    print("== VERIFY ==")
    ok, s, b = True, rd(E2E), bakpath()

    u1 = all(applied(e, s) for e in EDITS)
    ok &= u1
    print("  %s U1 五处编辑全部到位（%d/%d）"
          % (OK if u1 else BAD, sum(1 for e in EDITS if applied(e, s)), len(EDITS)))

    if os.path.exists(b):
        t = rd(b)
        for e in EDITS:
            t, err = apply_one(e, t)
            if err:
                t = None
                break
        # ★ 分层回滚点：本文件后来又被 l2p_rportfwd6.py 改过（.bak-judge2 是那一层的
        #   pre-image）⇒ 本判据的正确语义是「我的 pre-image ＋ 我的编辑 == **下一层的
        #   pre-image**」，而不是「== 当前文件」。写成后者会在任何后续分层下报假红。
        b2 = os.path.join(BAKDIR, "patches.l2p_rportfwd_e2e.py.bak-judge2")
        u2 = (t == s) or (t is not None and os.path.exists(b2) and t == rd(b2))
    else:
        u2 = False
    ok &= u2
    print("  %s U2 回滚点可正向重放（== 当前态，或 == 下一层 pre-image .bak-judge2）"
          % (OK if u2 else BAD))

    # ★ U3 语法：v1 就是死在这一条上（区间吞了 if 体）—— 静态锚点判据全绿也发现不了
    try:
        os.makedirs(os.path.dirname(PYC), exist_ok=True)
        py_compile.compile(E2E, cfile=PYC, doraise=True)
        u3 = True
    except Exception as ex:
        u3 = False
        print("      %s" % str(ex)[:220])
    ok &= u3
    print("  %s U3 py_compile 通过（无 IndentationError）" % (OK if u3 else BAD))

    gone = ("want = \"Reverse port forward" not in s) and ("a_message" not in s)
    here = ("DeleteTunnel" in s) and ("RunningTasks.Get" in s) and ("blocker_ok" in s)
    u4 = gone and here
    ok &= u4
    print("  %s U4 旧判据已移除=%s / 新判据因果链到场=%s" % (OK if u4 else BAD, gone, here))

    u5 = "ok &= ck(blocker_ok," in s
    ok &= u5
    print("  %s U5 前置正判据已接入 ok（占不住端口 ⇒ 报红，不报绿）" % (OK if u5 else BAD))

    u6 = (s.count("LPORT_C = 45113") == 1) and (s.count("LPORT_C") >= 5)
    ok &= u6
    print("  %s U6 LPORT_C 定义 1 处、引用 %d 处" % (OK if u6 else BAD, s.count("LPORT_C")))

    # ★ U7 缩进自证：新 J1b 必须在**循环外**（8 空格），且 `if hit:` 体内不得残留它的锚
    u7 = ("\n        # ── J1b（**重设计**）" in s) and ("\n            # ── J1b（**重设计**）" not in s)
    ok &= u7
    print("  %s U7 新 J1b 位于循环外（8 空格缩进）" % (OK if u7 else BAD))

    # ★ U8 p_active 只做观察：任何 ok &= ck(…) 的**判据表达式**（第一实参）都不得含 p_active
    #   （v1 用「整行是否含 p_active」判 ⇒ 把 J1 那行误报：它的判据是 bool(hit)，
    #     p_active 只出现在**通过消息**里，是刻意的观察输出。⇒ 要量的是判据，不是整行。）
    offs, i = [], s.find("ok &= ck(")
    while i >= 0:
        offs.append(i)
        i = s.find("ok &= ck(", i + 1)
    bad8 = []
    for o in offs:
        k, depth, j = o + len("ok &= ck("), 1, o + len("ok &= ck(")
        while j < len(s) and depth > 0:
            c = s[j]
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
            elif c == "," and depth == 1:
                break
            j += 1
        cond = s[k:j]
        if "p_active" in cond:
            bad8.append(cond.strip()[:80])
    u8 = not bad8
    ok &= u8
    print("  %s U8 p_active 仍是观察项（%d 条 ok&= 判据表达式均不含 p_active）%s"
          % (OK if u8 else BAD, len(offs), "" if u8 else bad8))

    print("  %s RESULT: %s" % (OK if ok else BAD, "PASS" if ok else "FAIL"))
    return ok


def main():
    m = sys.argv[1] if len(sys.argv) > 1 else "status"
    if m == "all":
        sys.exit(0 if (cmd_check() and cmd_apply() and cmd_verify()) else 1)
    f = dict(status=cmd_status, check=cmd_check, apply=cmd_apply,
             revert=cmd_revert, verify=cmd_verify).get(m)
    if not f:
        print(__doc__)
        sys.exit(2)
    r = f()
    sys.exit(0 if (r is None or r) else 1)


if __name__ == "__main__":
    main()
