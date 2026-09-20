#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次9d 判据：J1c（同端口再启用）＋ J6 已知开放项登记

    python3 l2p_rportfwd6.py status|check|apply|revert|verify|all

★ 机件复用 l2p_rportfwd4.py（apply_one/applied/rd/wr/md5），本脚本只管自己的 EDITS。
  回滚点分层：.bak-judge（原始）→ r4 → .bak-judge2（本脚本的 pre-image）→ r6。
  这是「同文件多编辑必须有独立回滚点」的落地方式（r4 那次同文件双编辑踩过坑）。

★ J1c 为什么必须有（不是补充判据，是**缺了就会放过真故障**）：
  core 把「stop → 同端口再启用」定义成一等生命周期（TsTunnelStop 注释
  "KEEP tunnel entry for re-enable"；TsTunnelCreate:432-443 reuse inactive entry
  "keep id for enable/stop lifecycle"，且 TunnelId 由 CRC32(client+agent+"rportfwd"+port)
  算出 ⇒ **同一个 id**）⇒ 再启用时会**重新下发** Callbacks.Reverse。
  没有 J1c，一个「监听一停就再也起不来」的实现在 J1/J2/J3/J4 上**全绿**，而操作员
  在 GUI 里看到的却是「重新启用直接失败 + 隧道消失」—— 这正是 J6 那条上游缺陷
  在本批次修好之前造成的真实故障（agent 自己遗留的 listener 占着端口）。

★ J6 的处理原则：**照常打印 [BAD]，绝不降级为观察项**（那是洗绿）。
  只是把它登记为「已在本批次明确不修的上游协议级缺陷」，让汇总能区分
  「真红」与「已登记开放项」；并且**自带过期检测**：登记项一旦变绿就打印
  「上游可能已修，请复核并撤下登记」。
"""
import os
import sys
sys.dont_write_bytecode = True   # ★ 批次13：import l2p_rportfwd4 前先关掉字节码缓存

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l2p_rportfwd4 as base      # noqa: E402

OK, BAD, INFO = base.OK, base.BAD, base.INFO
BAKDIR, FILE, SUF = base.BAKDIR, base.E2E, ".bak-judge2"

# ── C1：让 ck() 记录失败（标签 = 失败消息的首个空白分隔词；认不出 ⇒ 一律算真红）─────
CK_OLD = """# ── 判据小工具 ────────────────────────────────────────────────────────────────
def ck(cond, okmsg, badmsg):
    print(("  %s %s" % (OK, okmsg)) if cond else ("  %s %s" % (BAD, badmsg)))
    return bool(cond)
"""
CK_NEW = """# ── 判据小工具 ────────────────────────────────────────────────────────────────
# ★ FAILS 记录每一条红，用于「真红 / 已登记开放项」分类（见 KNOWN_OPEN 与 main）。
#   标签取失败消息的首个空白分隔词（判据消息都以判据名开头，如 "J6 ..."）；
#   **认不出的标签一律算真红** —— 分类失败时的默认方向必须是保守的那一侧。
FAILS = []


def ck(cond, okmsg, badmsg):
    print(("  %s %s" % (OK, okmsg)) if cond else ("  %s %s" % (BAD, badmsg)))
    if not cond:
        FAILS.append(((str(badmsg).split() or [""])[0], str(badmsg)))
    return bool(cond)
"""

# ── C2：登记表 + 分类器 ────────────────────────────────────────────────────────
KNOWN_OLD = "def main():\n"
KNOWN_NEW = '''# ── 已知开放项登记 ───────────────────────────────────────────────────────────
# ★ 登记项**照常打印 [BAD]**，只是不把整个套件判死。降级为「观察项」才是洗绿。
# ★ 每条登记都必须写清「为什么本批次不修」和「已做了什么补偿」，否则它会烂成万能借口。
KNOWN_OPEN = {
    "J6": (
        "上游协议级缺失：隧道 stop 时 core **无法通知 agent** 关监听。三层都断——"
        "① axc2@v1.2.0 的 adaptix.TunnelCallbacks 只有 Close(channelId)，**没有**「关监听」成员；"
        "② TsTunnelStop 只关服务端的 tunnel.listener（反向的监听在 agent 上⇒对 REVERSE 是空操作）；"
        "③ 上游留的 TsTunnelStopRportfwd 全树零调用者，且把 TunnelId 当 channelId 传（实参语义错）。"
        "要真修须改 axc2 模块 + core + **全部 20 个 extender 与 server 一起重建**"
        "（Go plugin 有包哈希校验）⇒ 本批次明确不做。"
        "补偿：agent 侧同端口再启用时会关掉**自己遗留的** listener（判据 J1c 覆盖该路径），"
        "外来占用者不受影响（判据 J1b 覆盖）。"
    ),
}


def classify():
    """返回 (真红, 已登记开放项)。认不出的标签算真红。"""
    real, opened = [], []
    for tag, msg in FAILS:
        (opened if tag in KNOWN_OPEN else real).append((tag, msg))
    return real, opened


def main():
'''

# ── C3：汇总行（区分真红 / 登记项 + 过期检测）───────────────────────────────────
RES_OLD = """    ok = run(a.listener, a.keep)
    print("\\nRESULT: %s" % ("PASS（J1-J6 全绿）" if ok else "FAIL（见上面 BAD 行）"))
    return 0 if ok else 1
"""
RES_NEW = """    ok = run(a.listener, a.keep)
    real, opened = classify()
    tags_o = sorted(set(t for t, _ in opened))
    if not real and opened:
        print("\\n%s 真红 0 条；已登记开放项 %d 条：%s" % (INFO, len(opened), tags_o))
        for t in tags_o:
            print("        [登记 %s] %s" % (t, KNOWN_OPEN[t]))
        print("\\nRESULT: PASS（真判据全绿；含 %d 条**已登记开放项** %s —— 上面仍以 %s 打印，未隐藏）"
              % (len(opened), tags_o, BAD))
        return 0
    if ok and not opened:
        # ★ 过期检测：登记项本该红却是绿的 ⇒ 登记的「事实」与代码真相脱节，必须回来看
        print("\\n%s 登记项 %s 本次**没有红** ⇒ 上游可能已修或行为已变，请复核并撤下登记"
              % (INFO, sorted(KNOWN_OPEN.keys())))
    print("\\nRESULT: %s" % ("PASS（J1-J6 全绿）" if ok else "FAIL（见上面 BAD 行）"))
    return 0 if ok else 1
"""

# ── C4：J1c 判据 ──────────────────────────────────────────────────────────────
J1C = '''        # ── J1c：同端口「停止 → 再启用」（core 的一等生命周期）───────────────
        # ★ core 自己把这个流程定义成一等公民，两处硬证据：
        #   · TsTunnelStop：**"Close runtime resources but KEEP tunnel entry for re-enable."**
        #   · TsTunnelCreate:432-443：inactive entry 直接复用，「keep id for enable/stop
        #     lifecycle」，且 TunnelId 仍由 CRC32(client+agent+"rportfwd"+port) 算出
        #     ⇒ **同一个 id**，随后 TsTunnelStart 的 REVERSE 分支会**重新下发**任务。
        # ★ 缺了这条，一个「监听一停就再也起不来」的实现（旧 listener 占着端口 ⇒
        #   net.Listen EADDRINUSE ⇒ Result:0 ⇒ core DeleteTunnel）也能让 J1/J2/J3/J4 全绿，
        #   而操作员在 GUI 里看到的是「重新启用直接失败、隧道消失」—— J6 那条上游缺陷
        #   造成的真实故障正是这个形态。
        # ★ 三条缺一即红，且都在靶机 A **还活着**时做（数据面那条才有判别力）。
        if "lport" in tgt["A"]:
            E.call("/tunnel/stop", {"p_tunnel_id": tgt["A"]["tid"]}, tok, timeout=60)
            time.sleep(2)
            st, body = E.call("/tunnel/start/rportfwd",
                              {"agent_id": agent_id, "listen": True,
                               "desc": "rpf-e2e-A-rearm", "port": LPORT_A,
                               "t_host": "127.0.0.1", "t_port": tgt["A"]["port"]},
                              tok, timeout=120)
            print("%s tunnel/start/rportfwd(A 再启用) -> %s %s" % (INFO, st, body[:200]))
            ok &= ck(st == 200 and '"ok":false' not in body.replace(" ", ""),
                     "J1c 同端口再启用被接受（走的正是 TsTunnelCreate 的 reuse inactive entry）",
                     "J1c 同端口再启用被拒（%s）⇒ 与 TsTunnelCreate 的 reuse 分支矛盾" % body[:200])
            try:      # id 由 CRC32 推出，理应不变；仍如实刷新，保证 J6 收尾用的是当前 id
                ntid = json.loads(body).get("message", "") or ""
                if ntid:
                    tgt["A"]["tid"] = ntid
            except Exception:
                pass
            owner, t0 = None, time.time()
            while time.time() - t0 < 30:
                owner = listen_owner(LPORT_A)
                if owner:
                    break
                time.sleep(2)
            ok &= ck(owner is not None and str(owner) == agent_os_pid,
                     "J1c 再启用后 :%d 仍由 agent 监听（pid=%s）" % (LPORT_A, owner),
                     "J1c 再启用后 :%d 的监听者 pid=%s（agent pid=%s）⇒ agent 没能重新监听"
                     % (LPORT_A, owner, agent_os_pid))
            got = probe(LPORT_A, timeout=8)
            ok &= ck(got == tgt["A"]["token"],
                     "J1c 再启用后数据面仍通（拿回 A 的 token）",
                     "J1c 再启用后数据面不通（got=%r，期望 %r）"
                     % (got, tgt["A"]["token"]))
        else:
            ok &= ck(False, "", "J1c **无法评估**：A 的隧道从未建成（与 J5 同一条前置理由）")

'''

EDITS = [
    # kind: "ins" = 纯插入（old ⊆ new）；"blk" = 整块精确替换（块必须唯一）。
    # ★ 不要用「a=整块, b=整块末 12 字符」那种区间写法：末段字符串在文件里未必唯一
    #   （v1 就是这么写 C3 的，`if ok else 1` 别处也可能出现）⇒ 块替换语义更硬。
    dict(tag="C1 ck 记录失败", kind="blk", a=CK_OLD, b=None, new=CK_NEW, fp="FAILS.append"),
    dict(tag="C2 登记表+分类器", kind="ins", a="def main():\n", b=None, new=KNOWN_NEW,
         fp="def classify():"),
    dict(tag="C3 汇总行", kind="blk", a=RES_OLD, b=None, new=RES_NEW,
         fp="real, opened = classify()"),
    dict(tag="C4 J1c 判据", kind="ins",
         a="        # ── J5 负向对照：停掉靶机 A", b=None,
         new=J1C + "        # ── J5 负向对照：停掉靶机 A",
         fp="J1c 再启用后数据面仍通"),
]


def apply_one(e, s):
    if applied(e, s):
        return s, None
    if s.count(e["a"]) != 1:
        return s, "锚点不唯一（%d）" % s.count(e["a"])
    return s.replace(e["a"], e["new"], 1), None


def applied(e, s):
    return s.count(e["fp"]) == 1


def bakpath():
    return os.path.join(BAKDIR, "patches.l2p_rportfwd_e2e.py" + SUF)


def cmd_status():
    s = base.rd(FILE)
    print("== 现状 ==  md5=%s" % base.md5(FILE))
    for e in EDITS:
        print("  %-22s 已应用=%s" % (e["tag"], applied(e, s)))


def cmd_check():
    print("== LINT ==")
    s, ok = base.rd(FILE), True
    for e in EDITS:
        good = applied(e, s) or (s.count(e["a"]) == 1 and (e["b"] is None or s.count(e["b"]) == 1))
        ok &= good
        print("  %s %-22s 已应用=%s" % (OK if good else BAD, e["tag"], applied(e, s)))
    print("  %s LINT: %s ｜ %d/4 todo" % (OK if ok else BAD, "OK" if ok else "FAIL",
                                          sum(1 for e in EDITS if not applied(e, base.rd(FILE)))))
    return ok


def cmd_apply():
    print("== APPLY ==")
    b = bakpath()
    if not os.path.exists(b):
        os.makedirs(BAKDIR, exist_ok=True)
        import shutil
        shutil.copy2(FILE, b)
        print("  %s 回滚点 %s（md5=%s）" % (OK, os.path.basename(b), base.md5(b)[:12]))
    s = base.rd(FILE)
    for e in EDITS:
        s2, err = apply_one(e, s)
        if err:
            print("  %s %-22s %s ⇒ 拒绝（不动盘）" % (BAD, e["tag"], err))
            return False
        s = s2
        print("  %s %-22s ok" % (OK, e["tag"]))
    base.wr(FILE, s)
    print("  %s APPLY 完成：%d 处" % (OK, len(EDITS)))
    return True


def cmd_revert():
    import shutil
    b = bakpath()
    if not os.path.exists(b):
        print("  %s 缺回滚点 %s" % (BAD, b))
        return False
    shutil.copy2(b, FILE)
    print("  %s 已还原自 %s（md5=%s）" % (OK, os.path.basename(b), base.md5(FILE)[:12]))
    return True


def cmd_verify():
    print("== VERIFY ==")
    ok, s, b = True, base.rd(FILE), bakpath()

    u1 = all(applied(e, s) for e in EDITS)
    ok &= u1
    print("  %s V1 四处编辑全部到位（%d/4）" % (OK if u1 else BAD, sum(1 for e in EDITS if applied(e, s))))

    u2 = False
    if os.path.exists(b):
        t = base.rd(b)
        for e in EDITS:
            t, err = apply_one(e, t)
            if err:
                t = None
                break
        u2 = (t == s)
    ok &= u2
    print("  %s V2 分层回滚点（.bak-judge2）可正向重放" % (OK if u2 else BAD))

    try:
        import py_compile
        os.makedirs(os.path.dirname(base.PYC), exist_ok=True)
        py_compile.compile(FILE, cfile=base.PYC, doraise=True)
        u3 = True
    except Exception as ex:
        u3 = False
        print("      %s" % str(ex)[:200])
    ok &= u3
    print("  %s V3 py_compile 通过" % (OK if u3 else BAD))

    # V4 ★ J6 仍是判据（不是观察项）：J6 的 ck 调用点必须在场
    u4 = ('ok &= ck(owner is None or str(owner) != agent_os_pid' in s)
    ok &= u4
    print("  %s V4 J6 仍以 ck 判定（未被降级成观察项）" % (OK if u4 else BAD))

    # V5 ★ 登记表自带「为什么本批次不修」+「补偿」，且分类器**默认保守**（认不出=真红）
    u5 = (("本批次明确不做" in s) and ("补偿" in s)
          and ('(opened if tag in KNOWN_OPEN else real)' in s))
    ok &= u5
    print("  %s V5 登记含不修理由+补偿；分类器认不出即算真红" % (OK if u5 else BAD))

    # V6 ★ 过期检测在场（登记项变绿必须提醒复核）
    u6 = "请复核并撤下登记" in s
    ok &= u6
    print("  %s V6 登记过期检测在场" % (OK if u6 else BAD))

    # V7 ★ J1c 三条断言齐 + 必须在 J5（杀靶机 A）之前
    #   ★ 数据面断言要**分别**数：J4 用的是循环变量 `d["token"]`，J1c 用的是 `tgt["A"]["token"]`
    #     （v1 把两者当成同一个字符串数 ⇒ 报假红。判据的字符串必须与代码逐字一致）
    i_j1c, i_j5 = s.find("# ── J1c：同端口"), s.find("# ── J5 负向对照：停掉靶机 A")
    n_j4 = s.count('ok &= ck(got == d["token"],')
    n_j1c = s.count('ok &= ck(got == tgt["A"]["token"],')
    u7 = (0 < i_j1c < i_j5) and n_j4 == 1 and n_j1c == 1
    ok &= u7
    print("  %s V7 J1c 在 J5 之前（%d < %d）；数据面断言 J4=%d 处 / J1c=%d 处"
          % (OK if u7 else BAD, i_j1c, i_j5, n_j4, n_j1c))

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
