#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批次9c：反向 rportfwd 数据面 b'' 的**根因修复**（零 core 改动）

    python3 l2p_rportfwd3.py status|check|apply|revert|verify|all

★★★ 根因（逐行读 core 得到，不是推测）

  现象（rpf_dial.py 现场实测）：经反向隧道连靶机 —— 靶机日志逐字节正确
  （`ACCEPT`/`RECV 14 B`/`SENT 14 B`），但 client 侧只拿到 b''。

  机制：core 里 `relaySocketToTunnel` 只有**两个**调用点，direct 取值恰好相反：

    正向  ts_tunnels.go:654  relaySocketToTunnel(..., ioDirect)   ← ioDirect 由**插件**给
          · handleTunChannelCreate(:740) **自己不启 relay**，通道建好后等
            gopher_listener_tcp/pl_transport.go:587 的
            `TsTunnelConnectionResume(agentId, ch, true)`
          · direct=true ⇒ 那个「打包回主连接」的 goroutine **不启动**（:921 `if !direct`）
          ⇒ listener 独占 prSrv。**正向一直是通的。**

    反向  ts_tunnels.go:870  relaySocketToTunnel(..., false)      ← **硬编码 false**
          · handlerReverseAccept(:856) 在**建通道的同时**就启 relay
          · direct=false ⇒ 那个 G2 启动，从**和 listener 同一条 prSrv** 上读
          ⇒ 两个读者瓜分同一条 io.Pipe（Go 允许多 waiter，数据按唤醒顺序被分掉）
          ⇒ 被 G2 抢走的那半交给 tunnel.Callbacks.WriteTCP，而 gopher agent 的
            `TunnelMessageWriteTCP` 至今是空壳（pl_main.go:169-179）
          ⇒ **永久丢失**。这就是 b''。

  ★ 为什么不能直接把 :870 的 false 改成 true：
    `TsTunnelConnectionResume(..., false)` 的**唯一**调用者是
    beacon/linux_agent/kharon 的 **agent 插件**（:3310/:2374/:2672 等），
    它们没有独立数据通道 ⇒ G2 是**它们唯一**的数据通路，false 对它们是对的。
    而 handlerReverseAccept 是 core 里**共享**的反向入口（beacon 反向也走它，
    且 beacon 侧没有第二个 relay）⇒ 改它就会打断 beacon 的反向。
    ⇒ 判据必须落在「谁提供数据通道」这一侧 —— 也就是**本插件**这一侧。

★★ 修法（零 core 改动）

  core 里**已有** `TsTunnelPause(channelId int)`（ts_tunnels.go:676 →
  TunnelManager.PauseChannel → `entry.Channel.paused.Store(true)`），**全树零调用者**
  —— upstream 留的钩子。它唯一的效果就是让那两个 G2 在自旋里让路（:935 / :1016
  每轮检查一次 paused）。⇒ 本插件在 `TsTunnelConnectionResume(..., true)` 之前
  把自己的通道 pause 掉，G2 就再也不碰 prSrv，listener 独占。

  ★ 无损性（可复算）：`paused` 的读者**只有**两处 relay 的 G2；
    入口方向用的是**另一个**字段 `flowPaused`（mgr_tunnel.go:314，背压流控）
    ⇒ client→target 方向与 ingressPump 完全不受影响。
  ★ 副作用（已知、接受、记录）：那个 G2 会常驻在 paused 自旋里不退出
    ⇒ 每条反向通道多 1 个 goroutine（50ms 唤醒一次）。通道本身仍由
    :587 那次 direct 的 relay 负责 finish()，**不会漏关通道**。

★★ 判据
  W1 锚点全 done            W2 lint 复检（old 唯一 + old ⊆ new）
  W3 ★跨层签名一致          插件接口声明的 TsTunnelPause 与 core *Teamserver 上的
                             实现逐字一致 —— 防 `Ts = ts.(Teamserver)` 运行时断言 panic
  W4 调用点恰好 2 处         且都在 TUNNEL_PACK 分支内
  W5 ★顺序判据              行号：Accept < Pause#1 < 复检 < Pause#2 < Resume
  W6 gofmt 零新增
  W7 ★回滚点是可正向重放的 pre-image
  W8 ★正向零副作用           core 的 handleTunChannelCreate 函数体内**不含**
                             relaySocketToTunnel ⇒ 正向不存在被 pause 的那个 G2
  W9 ★根因锚仍在（红在哪）   core 里 `relaySocketToTunnel(..., false)` 恰好 1 处
  W10 ★paused 读者恰好 2 处  支持「无损性」论证，不是我的断言
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys

LST = "/root/cc/AdaptixServer/extenders/gopher_listener_tcp"
CORE = "/root/cc/AdaptixServer/core/server"
BAKDIR = "/root/gp5probe/bak"
SUF = ".bak-pause"
GOFMT = "/usr/local/go/bin/gofmt"

OK, BAD, INFO = "[ok  ]", "[BAD ]", "[info]"

T = "\t"


# ── 编辑表 ────────────────────────────────────────────────────────────────────
# 规则：**纯插入**（old 必须是 new 的连续子串，且 old 在文件里唯一）。
E1_OLD = T + "TsTunnelConnectionAccept(tunnelId int, channelId int)\n"
E1_NEW = E1_OLD + (
    "\n"
    + T + "// ★ 批次9c：core 里**已有** TsTunnelPause（ts_tunnels.go:676 → TunnelManager.PauseChannel），\n"
    + T + "//   全树零调用者 —— 本插件把它用起来：把「打包回主连接」侧的那个 relay goroutine\n"
    + T + "//   冻在 paused 自旋里，从而独占 prSrv。**零 core 改动**（根因与推演见\n"
    + T + "//   pl_transport.go 的调用点注释）。\n"
    + T + "//   ★ InitPlugin 里是 `Ts = ts.(Teamserver)` —— **运行时**类型断言：这里声明的方法\n"
    + T + "//     *server.Teamserver 必须真的有，否则插件加载即 panic。已核对 TsTunnelPause\n"
    + T + "//     确在 core/server/ts_tunnels.go:676，签名 (channelId int) 逐字一致（判据 W3）。\n"
    + T + "TsTunnelPause(channelId int)\n"
)

E2_OLD = T * 3 + "Ts.TsTunnelConnectionAccept(tunPack.TunnelId, tunPack.ChannelId)\n"
E2_NEW = E2_OLD + (
    T * 3 + "// ★★★ 批次9c：反向 rportfwd 数据面 b'' 的根因就在这一行之后。\n"
    + T * 3 + "//   core 的 handlerReverseAccept（ts_tunnels.go:856）接受外部连接时，在**建通道的\n"
    + T * 3 + "//   同时**就启了一个 relay，而 direct 参数**硬编码 false**（:870）。direct=false\n"
    + T * 3 + "//   ⇒ relaySocketToTunnel 里那个「把数据打包回 agent 主连接」的 goroutine 会跑起来\n"
    + T * 3 + "//   （:921 `if !direct`），它从**和本 listener 同一条 prSrv** 上读 ⇒ 两个读者瓜分\n"
    + T * 3 + "//   同一条 io.Pipe。被它抢走的那半交给 tunnel.Callbacks.WriteTCP，而 gopher agent 的\n"
    + T * 3 + "//   TunnelMessageWriteTCP 至今是空壳（gopher_agent/pl_main.go:169-179）⇒ **永久丢失**。\n"
    + T * 3 + "//   现象：靶机**确实**被拨通（G1 方向正常），但 client 只看到 b''。\n"
    + T * 3 + "//   ★ 正向为什么不坏：正向的 handleTunChannelCreate（:740）**自己不启 relay**，通道建好\n"
    + T * 3 + "//     后只等本文件 :587 的 TsTunnelConnectionResume(..., **true**)，direct=true ⇒ 那个 G2\n"
    + T * 3 + "//     根本不启动 ⇒ listener 独占 prSrv。反向缺的正是这一步「由提供数据通道的一侧\n"
    + T * 3 + "//     自己选 direct」的机会。\n"
    + T * 3 + "//   ★ 而 :870 那个 false **不能**直接改 true：TsTunnelConnectionResume(..., false) 的\n"
    + T * 3 + "//     调用者是 beacon/linux_agent/kharon 的 **agent 插件**，它们没有独立数据通道 ⇒\n"
    + T * 3 + "//     G2 是它们唯一的数据通路。改动必须落在**本插件**这一侧。\n"
    + T * 3 + "//   ⇒ 用 core **既有**的 TsTunnelPause（:676 → PauseChannel）把那个 G2 冻住\n"
    + T * 3 + "//     （:935 每轮检查一次 paused），本 listener 随即独占 prSrv。**零 core 改动**。\n"
    + T * 3 + "//   ★ 为什么必须紧跟 Accept：G2 进入 prSrv.Read 之前只检查一次 paused。放在这里 ⇒\n"
    + T * 3 + "//     两个 relay goroutine 还没被调度就被置位 ⇒ 窗口为 0；若挪到 :587 之前，中间会先\n"
    + T * 3 + "//     过一次 TsTunnelChannelExists（sync.Map.Range）⇒ 万一靶机一连上就发 banner\n"
    + T * 3 + "//     （SMTP/FTP/SSH 那类），banner 可能已被抢走。\n"
    + T * 3 + "//   ★ 无害性：paused 只被两处 relay 的 G2 读（:935 / :1016），入口方向用的是**另一个**\n"
    + T * 3 + "//     字段 flowPaused（mgr_tunnel.go:314）⇒ client→target 方向与 ingressPump 不受影响。\n"
    + T * 3 + "//   ★ 副作用（已知、记录在案）：那个 G2 常驻 paused 自旋不退出 ⇒ 每条反向通道多 1 个\n"
    + T * 3 + "//     goroutine（50ms 唤醒一次）。通道本身仍由 :587 那次 direct 的 relay finish()。\n"
    + T * 3 + "ts.TsTunnelPause(tunPack.ChannelId)\n"
)

E3_OLD = T * 2 + "ts.TsTunnelConnectionResume(agentId, tunPack.ChannelId, true)\n"
E3_NEW = (
    T * 2 + "// ★ 批次9c：再置一次 paused（幂等）。紧跟 Accept 的那次已把窗口压到 0；这一次覆盖\n"
    + T * 2 + "//   「通道在本门之前就已存在」的路径（例如将来 agent 又改回先发 COMMAND_TUNNEL_ACCEPT）\n"
    + T * 2 + "//   ⇒ 使本修复不依赖那道门是否被触发。之后才 Resume ⇒ listener 接管 prSrv。\n"
    + T * 2 + "ts.TsTunnelPause(tunPack.ChannelId)\n"
    + E3_OLD
)

# ★ 改动前记录的权威 md5（唯一可信证据）。回滚点必须**等于**它 —— 否则那个「回滚点」
#   只是「某个中间态」，回滚会回到一个从来没存在过的版本。（本批次真踩到，见 cmd_apply。）
PRE_MD5 = {
    "pl_main.go":      "144bb2eb0432d8cec26df37fb4b1d9fa",
    "pl_transport.go": "0bfe415a33e6306150cb4b56f5a3cae3",
}

EDITS = [
    dict(tag="E1 接口声明", f=LST + "/pl_main.go", old=E1_OLD, new=E1_NEW),
    dict(tag="E2 门内紧跟 Accept", f=LST + "/pl_transport.go", old=E2_OLD, new=E2_NEW),
    dict(tag="E3 Resume 前幂等", f=LST + "/pl_transport.go", old=E3_OLD, new=E3_NEW),
]


def rd(p):
    return io.open(p, encoding="utf-8").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="\n").write(s)


def md5(p):
    return hashlib.md5(io.open(p, "rb").read()).hexdigest()


def bak(p):
    d = os.path.join(BAKDIR, os.path.basename(os.path.dirname(p)) + "." + os.path.basename(p) + SUF)
    os.makedirs(BAKDIR, exist_ok=True)
    shutil.copy2(p, d)
    return d


def cmd_status():
    print("== 现状 ==")
    for e in EDITS:
        s = rd(e["f"])
        print("  %-22s 已应用=%-5s  出现次数 old=%d" %
              (e["tag"], e["new"] in s, s.count(e["old"])))
    for p in (LST + "/pl_main.go", LST + "/pl_transport.go"):
        print("  %s  md5=%s size=%d" % (os.path.basename(p), md5(p), os.path.getsize(p)))


def cmd_check():
    print("== LINT + 待办 ==")
    ok = True
    for e in EDITS:
        s = rd(e["f"])
        n = s.count(e["old"])
        good = (n == 1) and (e["old"] in e["new"])
        ok &= good
        print("  %s %-22s old 唯一=%s old⊆new=%s" %
              (OK if good else BAD, e["tag"], n == 1, e["old"] in e["new"]))
    print("  %s LINT: %s" % (OK if ok else BAD, "OK" if ok else "FAIL"))
    todo = sum(1 for e in EDITS if e["new"] not in rd(e["f"]))
    print("  %s %d/%d todo" % (INFO, todo, len(EDITS)))
    return ok


def cmd_apply():
    print("== APPLY ==")
    # ★ 每个文件**只在本次首次改动前**备份一次 —— 否则同文件的多处编辑会让后一处
    #   覆盖前一处的回滚点，回滚点就退化成「中间态」而不是 pre-image。
    #   （本批次真踩到：E2/E3 同文件 ⇒ pl_transport.go 的回滚点一度是 post-E2 态。
    #     靠「按纯插入可逆地撤掉 E2 + 与改动前记录的权威 md5 比对」救回。
    #     ⇒ md5 是唯一可信证据，不是「备份文件存在」。）
    seen = set()
    for e in EDITS:
        s = rd(e["f"])
        if e["new"] in s:
            print("  %s %-22s 已是目标态，跳过" % (INFO, e["tag"]))
            continue
        if s.count(e["old"]) != 1:
            print("  %s %-22s 锚点不唯一（%d）⇒ 拒绝" % (BAD, e["tag"], s.count(e["old"])))
            return False
        if e["f"] not in seen:
            seen.add(e["f"])
            b = bak(e["f"])
            print("  %s %-22s 已改（回滚点 %s）" % (OK, e["tag"], os.path.basename(b)))
        else:
            print("  %s %-22s 已改（回滚点 = 本文件本次首个备份）" % (OK, e["tag"]))
        wr(e["f"], s.replace(e["old"], e["new"], 1))
    print("  %s APPLY 完成：%d 处" % (OK, len(EDITS)))
    return True


def cmd_revert():
    print("== REVERT ==")
    for e in EDITS:
        b = os.path.join(BAKDIR, os.path.basename(os.path.dirname(e["f"])) + "." +
                         os.path.basename(e["f"]) + SUF)
        if not os.path.exists(b):
            print("  %s %-22s 缺回滚点 %s" % (BAD, e["tag"], b))
            return False
        shutil.copy2(b, e["f"])
        print("  %s %-22s 已还原自 %s" % (OK, e["tag"], os.path.basename(b)))
    return True


def _body(src, fname):
    """取出某个顶层函数的函数体文本（按大括号配平）。"""
    i = src.find(fname)
    if i < 0:
        return None
    j = src.find("{", i)
    d = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            d += 1
        elif src[k] == "}":
            d -= 1
            if d == 0:
                return src[j:k + 1]
    return None


def cmd_verify():
    print("== VERIFY ==")
    ok = True
    src_main, src_tr = rd(LST + "/pl_main.go"), rd(LST + "/pl_transport.go")
    core_ts, core_mgr = rd(CORE + "/ts_tunnels.go"), rd(CORE + "/mgr_tunnel.go")

    # W1 锚点全 done
    w1 = all(e["new"] in rd(e["f"]) for e in EDITS)
    ok &= w1
    print("  %s W1 锚点全 done (%d/%d)" % (OK if w1 else BAD, sum(1 for e in EDITS if e["new"] in rd(e["f"])), len(EDITS)))

    # W2 回滚点可**正向重放**到当前态。
    #   ★ 公式要按**文件**分组、按 EDITS 顺序重放该文件的**全部**编辑 —— 同文件有多处
    #     编辑时，「只重放一处」得到的当然不是当前态（本批次第一版就写错成这样）。
    files = []
    for e in EDITS:
        if e["f"] not in files:
            files.append(e["f"])
    w2 = True
    for f in files:
        b = os.path.join(BAKDIR, os.path.basename(os.path.dirname(f)) + "." +
                         os.path.basename(f) + SUF)
        base = os.path.basename(f)
        if not os.path.exists(b):
            w2 = False
            print("  %s W2 %s 缺回滚点" % (BAD, base))
            continue
        s = rd(b)
        for e in [x for x in EDITS if x["f"] == f]:
            if s.count(e["old"]) != 1:
                w2 = False
                print("  %s W2 %s：pre-image 里锚点「%s」不唯一（%d）"
                      % (BAD, base, e["tag"], s.count(e["old"])))
                s = None
                break
            s = s.replace(e["old"], e["new"], 1)
        good = (s is not None) and (s == rd(f))
        w2 &= good
        print("  %s W2 %s 回滚点按 EDITS 顺序重放 ⇒ %s"
              % (OK if good else BAD, base,
                 "与当前态逐字节相同" if good else "**与当前态不同**"))
    ok &= w2

    # W3 跨层签名一致（防 `Ts = ts.(Teamserver)` 运行时断言 panic）
    decl = T + "TsTunnelPause(channelId int)\n"
    impl = "func (ts *Teamserver) TsTunnelPause(channelId int) {"
    w3 = (decl in src_main) and (impl in core_ts)
    ok &= w3
    print("  %s W3 插件接口声明 ↔ core 实现签名逐字一致（%s）" % (OK if w3 else BAD, impl))

    # W4 调用点恰好 2 处
    n4 = src_tr.count("ts.TsTunnelPause(tunPack.ChannelId)")
    w4 = (n4 == 2)
    ok &= w4
    print("  %s W4 TsTunnelPause 调用点 = %d（期望 2）" % (OK if w4 else BAD, n4))

    # W5 顺序判据（行号）
    ln = src_tr.split("\n")

    def line_of(sub, start=0):
        for i in range(start, len(ln)):
            if sub in ln[i]:
                return i
        return -1
    i_acc = line_of("Ts.TsTunnelConnectionAccept(tunPack.TunnelId")
    i_p1 = line_of("ts.TsTunnelPause(tunPack.ChannelId)", i_acc)
    i_rc = line_of("if !Ts.TsTunnelChannelExists(tunPack.ChannelId) {", i_acc + 1)
    i_p2 = line_of("ts.TsTunnelPause(tunPack.ChannelId)", i_rc)
    i_res = line_of("ts.TsTunnelConnectionResume(agentId, tunPack.ChannelId, true)")
    w5 = (-1 < i_acc < i_p1 < i_rc < i_p2 < i_res)
    ok &= w5
    print("  %s W5 顺序 Accept(%d) < Pause#1(%d) < 复检(%d) < Pause#2(%d) < Resume(%d)"
          % (OK if w5 else BAD, i_acc + 1, i_p1 + 1, i_rc + 1, i_p2 + 1, i_res + 1))

    # W6 gofmt 零新增
    w6 = True
    for f in (LST + "/pl_main.go", LST + "/pl_transport.go"):
        b = os.path.join(BAKDIR, os.path.basename(os.path.dirname(f)) + "." +
                         os.path.basename(f) + SUF)
        pre = len(subprocess.run([GOFMT, "-d", b], capture_output=True, text=True).stdout.split("\n"))
        post = len(subprocess.run([GOFMT, "-d", f], capture_output=True, text=True).stdout.split("\n"))
        w6 &= (post <= pre)
        print("  %s W6 gofmt %-18s pre=%d post=%d" % (OK if post <= pre else BAD,
                                                      os.path.basename(f), pre, post))
    ok &= w6

    # W7 ★回滚点的 md5 必须等于改动前记录的权威值 —— 「备份文件存在」是弱判据，
    #    「备份 == 原始文件」才是回滚能成立的全部理由。
    w7 = True
    for f in files:
        p = os.path.join(BAKDIR, os.path.basename(os.path.dirname(f)) + "." +
                         os.path.basename(f) + SUF)
        want = PRE_MD5.get(os.path.basename(f), "")
        got = md5(p) if os.path.exists(p) else "(缺)"
        good = (got == want)
        w7 &= good
        print("  %s W7 回滚点 %-16s md5=%s %s"
              % (OK if good else BAD, os.path.basename(f), got[:12],
                 "== 改动前原始值" if good else "!= 原始值 " + want[:12]))
    ok &= w7

    # W8 正向零副作用：handleTunChannelCreate 体内不含 relay
    body = _body(core_ts, "func handleTunChannelCreate(")
    w8 = (body is not None) and ("relaySocketToTunnel" not in body) and \
         ("relayWebsocketToTunnel" not in body)
    ok &= w8
    print("  %s W8 handleTunChannelCreate 体内无 relay ⇒ 正向没有可被 pause 的 G2" % (OK if w8 else BAD))

    # W9 根因锚仍在
    w9 = core_ts.count("relaySocketToTunnel(tm, agent, tunnel, tunChannel, false)") == 1 and \
        core_ts.count("relaySocketToTunnel(ts.TunnelManager, agent, tunnel, tunChannel, ioDirect)") == 1
    ok &= w9
    print("  %s W9 根因锚：反向 direct 硬编码 false ×1、正向 ioDirect ×1（红在哪仍可复算）"
          % (OK if w9 else BAD))

    # W10 paused 读者恰好 2 处
    r1 = len([l for l in core_ts.split("\n") if "paused.Load()" in l])
    r2 = len([l for l in core_mgr.split("\n") if "paused.Load()" in l])
    w10 = (r1 + r2 == 2)
    ok &= w10
    print("  %s W10 paused 的读者 = %d（ts_tunnels %d + mgr_tunnel %d，期望 2）"
          % (OK if w10 else BAD, r1 + r2, r1, r2))

    print("  %s RESULT: %s" % (OK if ok else BAD, "PASS" if ok else "FAIL"))
    return ok


def main():
    m = sys.argv[1] if len(sys.argv) > 1 else "status"
    if m == "status":
        cmd_status()
    elif m == "check":
        sys.exit(0 if cmd_check() else 1)
    elif m == "apply":
        sys.exit(0 if cmd_apply() else 1)
    elif m == "revert":
        sys.exit(0 if cmd_revert() else 1)
    elif m == "verify":
        sys.exit(0 if cmd_verify() else 1)
    elif m == "all":
        cmd_check() and cmd_apply() and cmd_verify()
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
