# -*- coding: utf-8 -*-
"""l2p_rportfwd7.py —— 批次9d：反向 rportfwd 数据面 b'' 的**根因**修复（竞态结构化消除）

★ 结论（本批次的全部依据，均可复算）：
  现象 = 同一条隧道、同一份代码，首连绿、次连红（实测 A 绿 / B 红 / A 再红）。
  ⇒ 不是确定性缺陷，是**竞态**。竞态的两方是**同一条 io.Pipe 的两个读者**：

    读者1（正确）：gopher_listener_tcp 的 `io.Copy(encWriter, prSrv)`（direct=true 路径）
    读者2（抢走即丢）：core relaySocketToTunnel 里 `if !direct` 启动的 G2
                       → 读 prSrv → tunnel.Callbacks.WriteTCP
                       → gopher 的 TunnelMessageWriteTCP 是**上游填空占位**（空壳）
                       ⇒ 被抢走的字节**永久丢失**（b'' 就是这么来的）

  ★ 为什么上一版（批次9c 的 TsTunnelPause）救不了：
    core 启 G2 的地方是 `TsTunnelConnectionAccept → handlerReverseAccept(:856)`，
    而它的**首要调用者是 gopher_agent/pl_main.go 的 COMMAND_TUNNEL_ACCEPT 分支**
    （agent 通过**主连接**通告「有人连进来了」）。
    listener 插件要把通道 pause 掉，得等 agent **另开一条 TCP 连接**发来 TUNNEL_PACK
    （pl_transport.go:562）—— 中间隔**一整个往返**。G2 早就在 prSrv.Read 上等着了，
    目标机（或 banner 型服务）首包一到就被它拿走。
    ⇒ pause 只是把窗口从「必然」压到「经常」，**关不死**；单发探测还有 ~50% 假绿。

  ★ 修法：窗口只能靠「**不启动** G2」消除，不能靠事后关闭。
    core 新增 TsTunnelConnectionAcceptDirect：同 TsTunnelConnectionAccept，但 ioDirect=true
    ⇒ handlerReverseAccept **不启** relay，数据通道所有权交给调用方插件
    （插件紧跟 TsTunnelConnectionResume(..., true) ⇒ 只启 G1、独占 prSrv）。
    ⇒ 反向从此与**正向**同构（正向 handleTunChannelCreate 本就不启 relay）。
    ★ beacon/kharon/macos 继续走旧方法（false）—— 它们的 TunnelMessageWriteTCP 是**真实现**，
      relay 正是它们唯一的数据通路；本次改动对它们**零影响**。
    ★ 插件侧不再需要 pause ⇒ 两处 TsTunnelPause 与接口声明一并撤下（不留死钩子）。
    ★ agent 交付载荷（src_gopher）**一个字节都不动**，老 agent 也无需重新生成。

模式：status|check|apply|revert|verify|all
回滚：BAKDIR 下 <basename>.bak-rpf7（本层 pre-image）；REVERT 段与 APPLY 段写在一起。
"""
import io
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = "/root/cc/AdaptixServer"
BAKDIR = "/root/gp5probe/bak"
SUF = ".bak-rpf7"
GOFMT = "/usr/local/go/bin/gofmt"
E2E = "extenders/gopher_listener_tcp/patches/l2p_rportfwd_e2e.py"

OK = "[ok  ]"
BAD = "[BAD ]"
INFO = "[info]"


def rd(p):
    return io.open(p, encoding="utf-8").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="\n").write(s)


def t2(s):
    """把 4 空格缩进组转成 tab（Go 源码用；注释/字符串里不留 4 空格前缀）。"""
    out = []
    for ln in s.split("\n"):
        i = 0
        while ln[i:i + 4] == "    ":
            i += 4
        out.append("\t" * (i // 4) + ln[i:])
    return "\n".join(out)


# ─────────────────────────── E1：core 新增 AcceptDirect ───────────────────────────
OLD_ACCEPT = t2("""func (ts *Teamserver) TsTunnelConnectionAccept(tunnelId int, channelId int) {
    tunId := fmt.Sprintf("%08x", tunnelId)
    tunnel, ok := ts.TunnelManager.GetTunnel(tunId)
    if !ok {
        return
    }

    agent, err := ts.getAgent(tunnel.Data.AgentId)
    if err != nil {
        return
    }

    if tunnel.Data.Client == "" {
        handlerReverseAccept(ts.TunnelManager, agent, tunnel, channelId)
    } else {
        // TODO: reverse proxy to client
    }
}""")

NEW_ACCEPT = t2("""func (ts *Teamserver) TsTunnelConnectionAccept(tunnelId int, channelId int) {
    ts.tunnelConnectionAccept(tunnelId, channelId, false)
}

// TsTunnelConnectionAcceptDirect 与 TsTunnelConnectionAccept 同构，唯一差别是
// **ioDirect=true**：明确声明数据通道由调用方插件自己提供，core 因此**不启动**
// relaySocketToTunnel 里那个「读 prSrv 并打包成任务回传 agent」的 goroutine。

// 为什么必须由 core 区分，而不是让插件事后去关它：
//   relay 一旦启动就开始和插件抢读**同一条 prSrv**；而 agent 的 ACCEPT 走主连接、
//   插件要等的 TUNNEL_PACK 走**另一条** TCP 连接，插件能执行「关掉它」时早已过去
//   一个往返 ⇒ 目标机 banner/首包通常已被抢走并交给 Callbacks.WriteTCP
//   （gopher 与 macos 的 TunnelMessageWriteTCP 是上游 /// START CODE HERE 占位、空壳）
//   ⇒ 字节**永久丢失**，表现为 client 侧 b''。窗口只能靠**不启动**消除，事后关闭只是压低概率。

// 对照组（reuse）：正向 lportfwd/socks 的 handleTunChannelCreate 本就不启 relay，
// 通道建好只等插件自己 TsTunnelConnectionResume(..., ioDirect=true)。本方法把反向补齐成同一形状。

// 对其它 agent 类型的零影响：beacon/kharon 等没有独立数据通道，继续用
// TsTunnelConnectionAccept（ioDirect=false）—— 它们的 TunnelMessageWriteTCP 是**真实现**，
// relay 正是它们唯一的数据通路，行为逐字节不变。
func (ts *Teamserver) TsTunnelConnectionAcceptDirect(tunnelId int, channelId int) {
    ts.tunnelConnectionAccept(tunnelId, channelId, true)
}

func (ts *Teamserver) tunnelConnectionAccept(tunnelId int, channelId int, ioDirect bool) {
    tunId := fmt.Sprintf("%08x", tunnelId)
    tunnel, ok := ts.TunnelManager.GetTunnel(tunId)
    if !ok {
        return
    }

    agent, err := ts.getAgent(tunnel.Data.AgentId)
    if err != nil {
        return
    }

    if tunnel.Data.Client == "" {
        handlerReverseAccept(ts.TunnelManager, agent, tunnel, channelId, ioDirect)
    } else {
        // TODO: reverse proxy to client
    }
}""")

# ─────────────────────────── E3：handlerReverseAccept 加 ioDirect ───────────────────────────
OLD_TAIL = t2("""    tm.RegisterChannel(tunnel.Data.TunnelId, tunnel, tunChannel)

    relaySocketToTunnel(tm, agent, tunnel, tunChannel, false)
}""")

NEW_TAIL = t2("""    tm.RegisterChannel(tunnel.Data.TunnelId, tunnel, tunChannel)

    // ★ 批次9d：ioDirect=true ⇒ **不启** relay。数据通道所有权交给调用方插件
    //   （它紧跟一次 TsTunnelConnectionResume(..., true)，那时才启 G1 并独占 prSrv）。
    //   详见 TsTunnelConnectionAcceptDirect 的注释：窗口只能靠「不启动」消除。
    //   ioDirect=false 保持原样 —— beacon/kharon 的 WriteTCP 是真实现，relay 是它们唯一通路。
    if !ioDirect {
        relaySocketToTunnel(tm, agent, tunnel, tunChannel, false)
    }
}""")

# ─────────────────────────── E5：agent 插件调用点 ───────────────────────────
NEW_CALL = t2("""                // ★ 批次9d：走 **AcceptDirect**（core 只拨号+登记，**不启 relay**）。
                //   本 agent 的 listener 插件提供独立加密数据通道，core 若在这里启
                //   relaySocketToTunnel(..., false)，那个 G2 就会和 listener 抢读同一条
                //   prSrv，而被抢走的一半交给 gopher 的 TunnelMessageWriteTCP ——
                //   上游占位空壳 ⇒ 首包永久丢失（b''）。窗口期 = agent 这条 ACCEPT
                //   到 listener 收到 TUNNEL_PACK 的**整个往返**，事后 pause 关不死。
                //   ★ exists 守卫：TUNNEL_PACK 可能先到（listener 侧那时已建好同名通道），
                //     此时再 Accept 会**二次拨号 + 二次注册**，必须跳过。
                if !Ts.TsTunnelChannelExists(params.ChannelId) {
                    Ts.TsTunnelConnectionAcceptDirect(params.TunnelId, params.ChannelId)
                }""")

# ─────────────────────────── E12/E13：e2e 判据升级（重复探测） ───────────────────────────
OLD_J34 = """        data_ok = {"A": False, "B": False}
        for name in ("A", "B"):
            d = tgt[name]
            if "lport" not in d:
                continue
            got = probe(d["lport"], timeout=8)
            data_ok[name] = (got == d["token"])
            ok &= ck(got == d["token"],
                     "J3/J4 隧道 %s：连 :%d 拿回**靶机 %s 的** token（数据面通）"
                     % (name, d["lport"], name),
                     "J3/J4 隧道 %s：连 :%d 拿回 %r，期望 %r ⇒ 数据面不通或串到了别的靶机"
                     % (name, d["lport"], got, d["token"]))"""

NEW_J34 = """        # ★★ 批次9d：单发探测**抓不住竞态型 b''** —— 实测同一条隧道「首连绿、次连红」
        #    （两个读者抢同一条 io.Pipe，谁先读到谁拿走；被 G2 拿走的字节永久丢失）。
        #    单发判据在这种缺陷下有 ~50% 概率假绿 ⇒ 改为**连探 REPEATS 次、次次必须命中**。
        REPEATS = 5
        data_ok = {"A": False, "B": False}
        for name in ("A", "B"):
            d = tgt[name]
            if "lport" not in d:
                continue
            hits = [probe(d["lport"], timeout=8) for _ in range(REPEATS)]
            good = sum(1 for h in hits if h == d["token"])
            data_ok[name] = (good == REPEATS)
            ok &= ck(good == REPEATS,
                     "J3/J4 隧道 %s：连 :%d %d/%d 次都拿回**靶机 %s 的** token（数据面稳定）"
                     % (name, d["lport"], good, REPEATS, name),
                     "J3/J4 隧道 %s：连 :%d 只有 %d/%d 次拿到 token（首次 %r）⇒ 数据面**不稳定**"
                     % (name, d["lport"], good, REPEATS, hits[0]))"""

OLD_J4X = """        if "lport" in tgt["A"] and "lport" in tgt["B"]:
            ga = probe(LPORT_A, timeout=8)
            ok &= ck(ga == tgt["A"]["token"],
                     "J4x 两条隧道不串：:%d 只返回 A 的 token" % LPORT_A,
                     "J4x 两条隧道串了：:%d 返回 %r（A 的应是 %r）⇒ t_host/t_port 没被真正使用"
                     % (LPORT_A, ga, tgt["A"]["token"]))"""

NEW_J4X = """        if "lport" in tgt["A"] and "lport" in tgt["B"]:
            ga_hits = [probe(LPORT_A, timeout=8) for _ in range(REPEATS)]
            ga_ok = sum(1 for h in ga_hits if h == tgt["A"]["token"])
            ok &= ck(ga_ok == REPEATS,
                     "J4x 两条隧道不串：:%d 连续 %d 次都只返回 A 的 token" % (LPORT_A, REPEATS),
                     "J4x 两条隧道串了/不稳：:%d 只有 %d/%d 次返回 A 的 token（首次 %r）"
                     "⇒ t_host/t_port 没被真正使用"
                     % (LPORT_A, ga_ok, REPEATS, ga_hits[0]))"""

EDITS = [
    dict(tag="E1 core 新增 TsTunnelConnectionAcceptDirect（复用同一实现）",
         f="core/server/ts_tunnels.go", kind="sub1", a=OLD_ACCEPT, new=NEW_ACCEPT,
         fp="func (ts *Teamserver) TsTunnelConnectionAcceptDirect(tunnelId int, channelId int) {"),
    dict(tag="E2 handlerReverseAccept 加 ioDirect 形参",
         f="core/server/ts_tunnels.go", kind="sub1",
         a="func handlerReverseAccept(tm *TunnelManager, agent *Agent, tunnel *Tunnel, channelId int) {",
         new="func handlerReverseAccept(tm *TunnelManager, agent *Agent, tunnel *Tunnel, channelId int, ioDirect bool) {",
         fp="tunnel *Tunnel, channelId int, ioDirect bool) {"),
    dict(tag="E3 relay 启动改由 ioDirect 决定（窗口结构化消除）",
         f="core/server/ts_tunnels.go", kind="sub1", a=OLD_TAIL, new=NEW_TAIL,
         fp="\tif !ioDirect {\n\t\trelaySocketToTunnel(tm, agent, tunnel, tunChannel, false)\n\t}"),
    dict(tag="E4 agent 插件接口 +AcceptDirect +ChannelExists",
         f="extenders/gopher_agent/pl_main.go", kind="sub1",
         a="\tTsTunnelConnectionAccept(tunnelId int, channelId int)\n",
         new=t2("""    TsTunnelConnectionAccept(tunnelId int, channelId int)

    // ★ 批次9d：与上面同名方法配对 —— 见调用点（COMMAND_TUNNEL_ACCEPT）的说明。
    //   ★ InitPlugin 是 `Ts = ts.(Teamserver)` 的**运行时**断言：core 必须真有这两个
    //     方法，否则插件加载即 panic。已核对 core/server/ts_tunnels.go 新增
    //     TsTunnelConnectionAcceptDirect(tunnelId, channelId int)，以及既有的
    //     TsTunnelChannelExists(channelId int) bool（签名逐字一致）。
    TsTunnelConnectionAcceptDirect(tunnelId int, channelId int)
    TsTunnelChannelExists(channelId int) bool
"""),
         fp="\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n"),
    dict(tag="E5 agent 插件 ACCEPT 改走 AcceptDirect + exists 守卫",
         f="extenders/gopher_agent/pl_main.go", kind="sub1",
         a="\t\t\t\tTs.TsTunnelConnectionAccept(params.TunnelId, params.ChannelId)\n",
         new=NEW_CALL + "\n",
         fp="Ts.TsTunnelConnectionAcceptDirect(params.TunnelId, params.ChannelId)"),
    dict(tag="E6 listener 侧 !exists 分支改走 AcceptDirect",
         f="extenders/gopher_listener_tcp/pl_transport.go", kind="sub1",
         a="\t\t\tTs.TsTunnelConnectionAccept(tunPack.TunnelId, tunPack.ChannelId)\n",
         new=t2("""            // ★ 批次9d：AcceptDirect（core 只拨号+登记，**不启 relay**）。
            //   本 listener 下面用 Resume(..., true) 接管 prSrv；core 若在这里多启一个
            //   relay，就会与本插件抢读同一条 io.Pipe。详见 core 侧该方法的注释。
            Ts.TsTunnelConnectionAcceptDirect(tunPack.TunnelId, tunPack.ChannelId)
"""),
         fp="Ts.TsTunnelConnectionAcceptDirect(tunPack.TunnelId, tunPack.ChannelId)"),
    dict(tag="E7 撤下批次9c 的长注释 + 第一处 pause",
         f="extenders/gopher_listener_tcp/pl_transport.go", kind="region_del",
         a="// ★★★ 批次9c：反向 rportfwd 数据面 b'' 的根因就在这一行之后。",
         b="ts.TsTunnelPause(tunPack.ChannelId)", new="", fp=None),
    dict(tag="E8 撤下第二处 pause + 其注释（Resume 保留）",
         f="extenders/gopher_listener_tcp/pl_transport.go", kind="region_del",
         a="// ★ 批次9c：再置一次 paused（幂等）。", b="ts.TsTunnelPause(tunPack.ChannelId)",
         new="", fp=None),
    dict(tag="E9 Resume 前补一行说明（为什么不再需要 pause）",
         f="extenders/gopher_listener_tcp/pl_transport.go", kind="ins_before",
         a="\t\tts.TsTunnelConnectionResume(agentId, tunPack.ChannelId, true)\n",
         new=t2("""        // ★ 批次9d：这里**不再需要** pause 补丁 —— 反向通道上的 G2 已从源头不存在
        //   （agent 插件走 AcceptDirect）。Resume(..., true) ⇒ relaySocketToTunnel(direct=true)
        //   ⇒ 只启 G1（target → pwSrv），prSrv 的读取端由本插件独占。
"""), fp="★ 批次9d：这里**不再需要** pause 补丁"),
    dict(tag="E10 listener 接口撤下 TsTunnelPause 及其注释",
         f="extenders/gopher_listener_tcp/pl_main.go", kind="region_del",
         a="// ★ 批次9c：core 里**已有** TsTunnelPause", b="TsTunnelPause(channelId int)",
         new="", fp=None),
    dict(tag="E11 listener 接口 +AcceptDirect",
         f="extenders/gopher_listener_tcp/pl_main.go", kind="sub1",
         a="\tTsTunnelConnectionAccept(tunnelId int, channelId int)\n",
         new=t2("""    TsTunnelConnectionAccept(tunnelId int, channelId int)

    // ★ 批次9d：core 侧与上面配对的方法 —— 只拨号+登记，**不启 relay**，把 prSrv 的
    //   读取端留给本插件（紧跟的 TsTunnelConnectionResume(..., true) 只启 G1）。
    //   ★ InitPlugin 的运行时断言要求 core 真的实现了它，否则插件加载即 panic。
    TsTunnelConnectionAcceptDirect(tunnelId int, channelId int)
"""),
         fp="\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n"),
    dict(tag="E12 e2e J3/J4 改重复探测（抓竞态）",
         f=E2E, kind="sub1", a=OLD_J34, new=NEW_J34, fp="REPEATS = 5"),
    dict(tag="E13 e2e J4x 改重复探测",
         f=E2E, kind="sub1", a=OLD_J4X, new=NEW_J4X, fp="ga_ok == REPEATS"),
    dict(tag="E14 抹平 region_del 留下的双空行（否则 gofmt 会折叠 ⇒ Z8 假红）",
         f="extenders/gopher_listener_tcp/pl_main.go", kind="sub1",
         a="\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n\n\n\tTsAgentTerminalCloseChannel",
         new="\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n\n\tTsAgentTerminalCloseChannel",
         fp="\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n\n\tTsAgentTerminalCloseChannel"),
]

# ★ 各文件**本层 pre-image 的 md5**（apply 之前的实测值）。这是最硬的一道闸门：
#   回滚点一旦被串名/污染/重建成错的字节，Z12 立刻红 —— 比「重放能对上」更早发现问题。
PRE_MD5 = {
    "core/server/ts_tunnels.go": "d2ccf7e76e9e7373408d50f760b93cd4",
    "extenders/gopher_agent/pl_main.go": "e22e799d4d28901e3b2f13b35725292f",
    "extenders/gopher_listener_tcp/pl_main.go": "7c33595d0ab2f5a9d8dcfa4cace153f2",
    "extenders/gopher_listener_tcp/pl_transport.go": "e5a478e70e60c16b2305f0c939624d51",
    E2E: "1b571b14935e2490a62eb5a9a1efcf65",
}

FILES = ["core/server/ts_tunnels.go", "extenders/gopher_agent/pl_main.go",
         "extenders/gopher_listener_tcp/pl_main.go",
         "extenders/gopher_listener_tcp/pl_transport.go", E2E]


def bak_of(f):
    # ★ 陷阱（批次9d 实测踩到，见 fix_bak7.py）：只取 basename ⇒ 两个 pl_main.go
    #   （gopher_agent 与 gopher_listener_tcp）**共用同一个回滚点路径**：先创建的赢，
    #   后者静默**没有回滚点**；而 revert 会把前者的 pre-image **写进后者的文件**
    #   ⇒ 跨文件污染、且两道判据都不会喊。⇒ 同名时用「父目录名__basename」消歧。
    base = os.path.basename(f)
    if len([g for g in FILES if os.path.basename(g) == base]) > 1:
        return os.path.join(BAKDIR, os.path.basename(os.path.dirname(f)) + "__" + base + SUF)
    return os.path.join(BAKDIR, base + SUF)


def applied(e, s):
    if e["kind"] == "region_del":
        # ★ 起点标记是**每条编辑独有**的；终点标记可能被两条编辑共用（E7/E8 都以
        #   ts.TsTunnelPause 结尾）⇒ 只认起点，否则先应用的那条会被判成「未应用」而重放报错。
        return e["a"] not in s
    if e["kind"] == "ins_before":
        return s.count(e["fp"] if e["fp"] else e["new"]) == 1 and e["a"] in s
    if e["fp"]:
        return s.count(e["fp"]) == 1
    return e["a"] not in s


def apply_one(e, s):
    if e["kind"] == "sub1":
        if s.count(e["a"]) != 1:
            raise SystemExit("%s: 锚点出现 %d 次（要求恰好 1 次）" % (e["tag"], s.count(e["a"])))
        return s.replace(e["a"], e["new"], 1)
    if e["kind"] == "ins_before":
        if s.count(e["a"]) != 1:
            raise SystemExit("%s: 锚点出现 %d 次（要求恰好 1 次）" % (e["tag"], s.count(e["a"])))
        return s.replace(e["a"], e["new"] + e["a"], 1)
    if e["kind"] == "region_del":
        lines = s.split("\n")
        si = [i for i, l in enumerate(lines) if e["a"] in l]
        if len(si) != 1:
            raise SystemExit("%s: 起点标记 %d 次" % (e["tag"], len(si)))
        s0 = si[0]
        ei = [i for i in range(s0, len(lines)) if e["b"] in lines[i]]
        if not ei:
            raise SystemExit("%s: 终点标记不存在" % e["tag"])
        return "\n".join(lines[:s0] + lines[ei[0] + 1:])
    raise SystemExit("未知 kind: %s" % e["kind"])


def gofmt_delta(p, orig):
    d = subprocess.run([GOFMT, "-d", p], capture_output=True, text=True)
    o = subprocess.run([GOFMT, "-d", "/dev/stdin"], input=orig, capture_output=True, text=True)
    return len(o.stdout.split("\n")), len(d.stdout.split("\n")), d.stdout


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    fails = []

    def ck(cond, okmsg, badmsg):
        print(("  %s %s" % (OK, okmsg)) if cond else ("  %s %s" % (BAD, badmsg)))
        if not cond:
            fails.append(badmsg)
        return bool(cond)

    if mode in ("status", "check", "all"):
        print("== 现状 ==")
        for e in EDITS:
            s = rd(os.path.join(ROOT, e["f"]))
            print("  %s %s" % ("done " if applied(e, s) else "todo ", e["tag"]))
        for f in FILES:
            p = bak_of(f)
            print("  %s %s" % (INFO, os.path.basename(p) + (" 存在" if os.path.exists(p) else " 不存在")))

    if mode in ("check", "all"):
        n = sum(1 for e in EDITS if applied(e, rd(os.path.join(ROOT, e["f"]))))
        print("\nTODO: %d/%d 处待改" % (len(EDITS) - n, len(EDITS)))

    if mode in ("apply", "all"):
        print("\n== APPLY ==")
        if not os.path.isdir(BAKDIR):
            os.makedirs(BAKDIR)
        for f in FILES:
            p = os.path.join(ROOT, f)
            b = bak_of(f)
            if not os.path.exists(b):
                shutil.copyfile(p, b)
                print("  %s 回滚点 %s" % (INFO, b))
            cur = rd(p)
            for e in EDITS:
                if e["f"] != f or applied(e, cur):
                    continue
                cur = apply_one(e, cur)
                print("  %s %s" % (OK, e["tag"]))
            wr(p, cur)

    if mode in ("revert", "all"):
        print("\n== REVERT ==")
        for f in FILES:
            b = bak_of(f)
            if os.path.exists(b):
                shutil.copyfile(b, os.path.join(ROOT, f))
                print("  %s 还原 %s" % (OK, f))

    if mode in ("verify", "all"):
        print("\n== VERIFY ==")
        # Z1 编辑到位
        for e in EDITS:
            s = rd(os.path.join(ROOT, e["f"]))
            ck(applied(e, s), "%s 已应用" % e["tag"], "%s 未应用" % e["tag"])
        core = rd(os.path.join(ROOT, "core/server/ts_tunnels.go"))
        ag = rd(os.path.join(ROOT, "extenders/gopher_agent/pl_main.go"))
        ln = rd(os.path.join(ROOT, "extenders/gopher_listener_tcp/pl_transport.go"))
        lm = rd(os.path.join(ROOT, "extenders/gopher_listener_tcp/pl_main.go"))
        e2 = rd(os.path.join(ROOT, E2E))
        # Z11 回滚点路径唯一（同名不同目录必须消歧 —— 见 bak_of 的陷阱说明）
        _p = [bak_of(f) for f in FILES]
        ck(len(set(_p)) == len(_p), "Z11 五个回滚点路径互不相同",
           "Z11 回滚点路径冲突: %r" % _p)
        # Z12 回滚点字节 == 记录的原始 pre-image（md5 闸门）
        for f in FILES:
            b = bak_of(f)
            if not os.path.exists(b):
                ck(False, "", "Z12 回滚点不存在 %s" % b)
                continue
            got = hashlib.md5(io.open(b, "rb").read()).hexdigest()
            ck(got == PRE_MD5[f],
               "Z12 %s 回滚点 == 原始字节（%s）" % (os.path.basename(f), got[:12]),
               "Z12 %s 回滚点 md5 %s ≠ 记录值 %s（串名/污染）"
               % (f, got[:12], PRE_MD5[f][:12]))
        # Z2 回滚点可正向重放
        for f in FILES:
            b = bak_of(f)
            if not os.path.exists(b):
                ck(False, "", "回滚点缺失 %s" % b)
                continue
            pre = rd(b)
            cur = pre
            for e in EDITS:
                if e["f"] == f:
                    cur = apply_one(e, cur)
            ck(cur == rd(os.path.join(ROOT, f)), "回滚点可重放 %s" % os.path.basename(f),
               "回滚点无法重放出当前态 %s" % os.path.basename(f))
        # Z3 语法
        for f in FILES:
            if f.endswith(".go"):
                r = subprocess.run([GOFMT, "-e", os.path.join(ROOT, f)], capture_output=True, text=True)
                ck(r.returncode == 0 and not r.stderr.strip(),
                   "gofmt 解析通过 %s" % os.path.basename(f),
                   "gofmt 解析失败 %s: %s" % (os.path.basename(f), r.stderr.strip()[:200]))
            else:
                r = subprocess.run([sys.executable, "-m", "py_compile", os.path.join(ROOT, f)],
                                   capture_output=True, text=True)
                ck(r.returncode == 0, "py_compile 通过 %s" % os.path.basename(f),
                   "py_compile 失败: %s" % r.stderr.strip()[:200])
        # Z4 relay 启动受 ioDirect 约束（窗口结构化消除）
        ck(core.count("if !ioDirect {\n\t\trelaySocketToTunnel(tm, agent, tunnel, tunChannel, false)\n\t}") == 1,
           "Z4 relay 启动已由 ioDirect 约束（ioDirect=true 时完全不启 G2）",
           "Z4 relay 启动仍是无条件的 ⇒ 窗口没消除")
        ck(core.count("TsTunnelConnectionAcceptDirect(tunnelId int, channelId int) {") == 1
           and core.count("ts.tunnelConnectionAccept(tunnelId, channelId, true)") == 1,
           "Z4b AcceptDirect 已导出且复用同一实现",
           "Z4b AcceptDirect 缺失或未复用实现")
        # Z5 两插件里不再有 pause（不留死钩子）
        ck(lm.count("TsTunnelPause") == 0 and ln.count("TsTunnelPause") == 0,
           "Z5 listener 插件已彻底撤下 TsTunnelPause（无死钩子）",
           "Z5 listener 插件里仍有 TsTunnelPause 残留")
        ck(ag.count("\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n") == 1
           and lm.count("\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n") == 1
           and ag.count("Ts.TsTunnelConnectionAcceptDirect(params.TunnelId, params.ChannelId)") == 1
           and ln.count("Ts.TsTunnelConnectionAcceptDirect(tunPack.TunnelId, tunPack.ChannelId)") == 1,
           "Z5b 两个插件各 1 处接口声明，agent 与 listener 各 1 处调用",
           "Z5b 声明/调用数不对（agent 声明=%d listener 声明=%d agent 调用=%d listener 调用=%d）"
           % (ag.count("\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n"),
              lm.count("\tTsTunnelConnectionAcceptDirect(tunnelId int, channelId int)\n"),
              ag.count("Ts.TsTunnelConnectionAcceptDirect(params.TunnelId, params.ChannelId)"),
              ln.count("Ts.TsTunnelConnectionAcceptDirect(tunPack.TunnelId, tunPack.ChannelId)")))
        # Z6 反向 relay 仍会由插件启动
        ck(ln.count("ts.TsTunnelConnectionResume(agentId, tunPack.ChannelId, true)") == 1,
           "Z6 反向 relay 仍由插件 Resume(..., true) 启动（数据面没被拆掉）",
           "Z6 Resume(true) 不见了 ⇒ 反向通道将无人搬数据")
        # Z7 agent 插件 exists 守卫
        ck(ag.count("if !Ts.TsTunnelChannelExists(params.ChannelId) {") == 1,
           "Z7 agent 插件已加 exists 守卫（防二次拨号/二次注册）",
           "Z7 exists 守卫缺失")
        # Z8 gofmt 零新增
        for f in FILES:
            if not f.endswith(".go"):
                continue
            p = os.path.join(ROOT, f)
            pre, post, diff = gofmt_delta(p, rd(bak_of(f)))
            ck(pre == post, "Z8 gofmt delta=0 %s（pre=%d post=%d）" % (os.path.basename(f), pre, post),
               "Z8 gofmt 有新增 %s（pre=%d post=%d）\n%s" % (os.path.basename(f), pre, post, diff[:1200]))
        # Z9 其它插件零波及
        r = subprocess.run(["grep", "-rl", "TsTunnelConnectionAcceptDirect",
                            os.path.join(ROOT, "extenders"), "--include=*.go"],
                           capture_output=True, text=True)
        hit = sorted(x.replace(ROOT + "/", "") for x in r.stdout.split() if "/dist/" not in x)
        ck(hit == ["extenders/gopher_agent/pl_main.go", "extenders/gopher_listener_tcp/pl_main.go",
                   "extenders/gopher_listener_tcp/pl_transport.go"],
           "Z9 只有 gopher 两个插件被波及（beacon/kharon/macos 零改动）",
           "Z9 波及面超出预期: %r" % hit)
        for f in ("beacon_agent/pl_main.go", "agent_kharon/src_server/pl_agent.go"):
            s = rd(os.path.join(ROOT, "extenders", f))
            ck(s.count("TsTunnelConnectionAccept(") >= 1, "Z9b %s 仍用旧入口（其 WriteTCP 是真实现）" % f,
               "Z9b %s 的旧入口被误改" % f)
        # Z10 e2e 判据升级
        ck(e2.count("REPEATS = 5") == 1 and e2.count("ok &= ck(good == REPEATS,") == 1
           and e2.count("ok &= ck(ga_ok == REPEATS,") == 1 and e2.count("\n            hits = [probe(") == 1
           and e2.count("ga_hits = [probe(") == 1,
           "Z10 e2e 已升级为重复探测（5 次全中，单发探测已被 REPEATS 取代）",
           "Z10 e2e 重复探测判据不完整（REPEATS=%d good=%d ga_ok=%d hits=%d ga_hits=%d）"
           % (e2.count("REPEATS = 5"), e2.count("ok &= ck(good == REPEATS,"),
              e2.count("ok &= ck(ga_ok == REPEATS,"), e2.count("hits = [probe("),
              e2.count("ga_hits = [probe(")))
        ck("got = probe(d[\"lport\"], timeout=8)" not in e2,
           "Z10b 旧的单发探测已撤下（竞态缺陷下它会假绿）",
           "Z10b 旧单发探测仍在")

        print("\nRESULT: %s（%d 条判据，%d 条红）" % ("PASS" if not fails else "FAIL",
                                                    len(EDITS) + 12, len(fails)))
        for x in fails:
            print("  %s %s" % (BAD, str(x)[:300]))
        return 0 if not fails else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
