//go:build libp2p

// Phase5 批次5 —— relay（C2 IP 隐藏）的端到端判据。
//
// ## 为什么是**包内测试**，不是再写一个探针二进制
//
// 上一段的机制探针（/root/gp5probe/relaytest/main.go）用的是**手抄**的 host 选项：
// 它证明了链路可行，但抄错一个字它也不会知道 —— 这正是本项目归档过的
// 「探针只打印变量、从不构造 host ⇒ 拿到假数字」那一类。本文件在**同一个包里**
// 直呼真函数 `libp2pHost` / `dialLibp2p`，被测的就是要交付的那段代码本身。
//
// ## 每条判据都必须能红（不能红的不是门）
//
//	T1 真 dialLibp2p 在 circuit 地址上开流并 echo 通
//	   红法：把 transport_libp2p.go 的 `network.WithAllowLimitedConn(ctx, …)` 改回裸
//	   `ctx` —— circuit 连接被标记为 Limited，NewStream 会去等一条**永远不会来的直连**
//	   （swarm.go:508-515），挂到 ctx 超时。症状与"网络慢"完全同形。
//
//	T2 限额：默认档必须**失败**、放宽档必须**成功**，且直接读 `rsv.LimitData` 当标尺
//	   —— 只测"放宽就通"是没有判别力的：那样"限额根本没生效"也会通过。
//
//	T3 identify：C2 **不装** AddrsFactory ⇒ agent 的 peerstore 里**能**看到 C2 的
//	   真实网卡地址（这一半证明观测通道有效，否则另一半的"看不到"是假绿 ——
//	   「两个空集也相等」）；装了 ⇒ 看不到。
//
//	T4 阴性：relay 停掉后（C2 装 AddrsFactory，即交付形态）dialLibp2p 必须失败。
//	   它同时证明「隐藏是真的」：不做 factory 时，relay 断了还能靠 identify 送来的
//	   真 IP 直连兜底 —— 那样"经 relay 就藏住了 IP"这个说法不成立。
//	   本条**先证阳性基线再断言阴性**：否则"从来就不通"也会让它通过。
//
// C2 侧与 relay 侧的选项逐项照抄 pl_libp2p.go 的 axLibp2pHost / axStartRelay 的对应段
// （含 `EnableRelay()` 必须排在 `NoTransports` **之后**）；跨 module 无法直接 import，
// 由 patches/l2p_relay_e2e.py 的**同构判据**在文本层面对齐两边，防止这份抄件漂移。
package main

import (
	"context"
	"crypto/rand"
	"io"
	"net"
	"strings"
	"testing"
	"time"

	"gopher/utils"

	"github.com/libp2p/go-libp2p"
	libcrypto "github.com/libp2p/go-libp2p/core/crypto"
	"github.com/libp2p/go-libp2p/core/host"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/p2p/muxer/yamux"
	"github.com/libp2p/go-libp2p/p2p/net/gostream"
	relayclient "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/client"
	relayv2 "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/libp2p/go-libp2p/p2p/transport/tcp"
	ma "github.com/multiformats/go-multiaddr"
)

const (
	// axRelayPay 必须**大于** relayv2 的默认单 circuit 限额（1<<17 = 128 KiB），
	// 否则 T2 的"红的一半"根本红不了。
	axRelayPay = 200000
	// axRelayDialTmo 是 profile 里的拨号超时（秒）。T4 的阴性对照要等它。
	axRelayDialTmo = 6
	// axRelayIDWait 是等 identify 到达的时间。T3 两半互为对照：红的一半若在
	// 这个窗口内看到真 IP，就说明绿的一半"没看到"不是因为等得不够久。
	axRelayIDWait = 3 * time.Second
)

// ---------------- 测试台 ----------------

type axTestRelay struct {
	h host.Host
	ma string
}

// axStartRelay 起一个**真的** circuit v2 relay 节点。
// raised=false 时用上游默认限额（T2 红的一半靠它）。
func axStartRelay(t *testing.T, raised bool) *axTestRelay {
	t.Helper()
	res := relayv2.DefaultResources()
	if raised {
		// 只改限额，其余（MaxReservations/MaxCircuits/ReservationTTL…）沿用默认 ——
		// 从零值构造 Resources 会让 MaxReservations=0 ⇒ 谁都预约不上，且**不报错**。
		res.Limit = &relayv2.RelayLimit{Duration: time.Hour, Data: 1 << 30}
	}
	h, err := libp2p.New(
		libp2p.ListenAddrStrings("/ip4/127.0.0.1/tcp/0"),
		libp2p.NoTransports,
		libp2p.Transport(tcp.NewTCPTransport),
		libp2p.Security(noise.ID, noise.New),
		libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
		// ★ 不加这句 relaysvc 永远不启动：它只在 ReachabilityPublic 事件上建 relay。
		libp2p.ForceReachabilityPublic(),
		libp2p.EnableRelayService(relayv2.WithResources(res)),
	)
	if err != nil {
		t.Fatalf("起 relay 节点失败: %v", err)
	}
	return &axTestRelay{h: h, ma: h.Addrs()[0].String() + "/p2p/" + h.ID().String()}
}

type axTestC2 struct {
	h       host.Host
	ln      net.Listener
	circuit string
	rsv     *relayclient.Reservation
}

// axStartC2 起 listener 侧（与 pl_libp2p.go 的 axLibp2pHost 同构），并完成预约 + 监听。
// hideC2=true 时装上 AddrsFactory —— 只对外通告 circuit 地址（批次5 的隐藏面）。
func axStartC2(t *testing.T, relayMA string, hideC2 bool) *axTestC2 {
	t.Helper()
	// ★ 私钥必须**先于** host 存在：AddrsFactory 要在 libp2p.New 之前拿到自己的
	//   peer id（真代码走的是 axLibp2pPeerID(cfg.Libp2pPrivKey) 这条路）。
	sk, _, err := libcrypto.GenerateEd25519Key(rand.Reader)
	if err != nil {
		t.Fatalf("生成 C2 私钥失败: %v", err)
	}
	self, err := peer.IDFromPrivateKey(sk)
	if err != nil {
		t.Fatalf("从私钥算 peer id 失败: %v", err)
	}
	circuit := relayMA + "/p2p-circuit/p2p/" + self.String()

	opts := []libp2p.Option{
		libp2p.ListenAddrStrings("/ip4/0.0.0.0/tcp/0"), // 真网卡 ⇒ identify 会送出真 IP
		libp2p.NoTransports,
		// ★★ 必须在 NoTransports **之后**：NoTransports 见 RelayCustom==false 会把
		//    cfg.Relay 一起关掉（options.go:436-438），而它才是装配 circuit 传输的开关。
		libp2p.EnableRelay(),
		libp2p.Transport(tcp.NewTCPTransport),
		libp2p.Security(noise.ID, noise.New),
		libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
		// ★ 批次8：与 pl_libp2p.go 同形 —— l2p_relay_e2e.py 的 check() 判
		//   "生产选项 ⊆ 抄件"，抄件漏项即翻红（实测过：漏 UserAgent 时它就红）。
		libp2p.UserAgent(axLibp2pUserAgent),
		libp2p.Identity(sk),
	}
	if hideC2 {
		m, merr := ma.NewMultiaddr(circuit)
		if merr != nil {
			t.Fatalf("circuit 地址不合法 %q: %v", circuit, merr)
		}
		opts = append(opts, libp2p.AddrsFactory(
			func([]ma.Multiaddr) []ma.Multiaddr { return []ma.Multiaddr{m} }))
	}
	h, err := libp2p.New(opts...)
	if err != nil {
		t.Fatalf("起 C2 host 失败: %v", err)
	}

	ai, err := peer.AddrInfoFromP2pAddr(ma.StringCast(relayMA))
	if err != nil {
		t.Fatalf("解析 relay 地址 %q: %v", relayMA, err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	rsv, err := relayclient.Reserve(ctx, h, *ai)
	if err != nil {
		t.Fatalf("C2 在 relay 上预约失败: %v", err)
	}

	ln, err := gostream.Listen(h, defaultLibp2pProtocol)
	if err != nil {
		t.Fatalf("gostream.Listen(%s): %v", defaultLibp2pProtocol, err)
	}
	go func() {
		for {
			c, aerr := ln.Accept()
			if aerr != nil {
				return
			}
			go func(c net.Conn) { defer c.Close(); _, _ = io.Copy(c, c) }(c)
		}
	}()
	return &axTestC2{h: h, ln: ln, circuit: circuit, rsv: rsv}
}

// axNonLoopback 取 host 对外通告里的非环回地址 —— 这正是 identify 会送给对端的东西。
func axNonLoopback(h host.Host) []string {
	var out []string
	for _, a := range h.Addrs() {
		s := a.String()
		if strings.Contains(s, "/ip4/127.") || strings.Contains(s, "/ip6/::1") {
			continue
		}
		out = append(out, s)
	}
	return out
}

// axDialEcho 走**真代码路径**（dialLibp2p）拨 circuit 地址，写 pay 字节再收回同样多。
func axDialEcho(t *testing.T, circuit string, pay int) (int, error) {
	t.Helper()
	resetHostCache()
	c, err := dialLibp2p(utils.Profile{
		Libp2pAddrs:       []string{circuit},
		Libp2pDialTimeout: axRelayDialTmo,
	}, circuit)
	if err != nil {
		return 0, err
	}
	defer c.Close()
	buf := make([]byte, pay)
	for i := range buf {
		buf[i] = byte(i)
	}
	go func() { _, _ = c.Write(buf) }()
	got := make([]byte, pay)
	return io.ReadFull(c, got)
}

// ---------------- T1：真代码能不能在 circuit 上开流 ----------------

func TestRelayE2EDialAndEcho(t *testing.T) {
	rl := axStartRelay(t, true) // 放宽限额，把"限额"这个变量排除掉，只测开流本身
	defer rl.h.Close()
	c2 := axStartC2(t, rl.ma, true)
	defer c2.h.Close()

	n, err := axDialEcho(t, c2.circuit, 4096)
	if err != nil || n != 4096 {
		t.Fatalf("真 dialLibp2p 在 circuit 地址上开流/回显失败（read=%d err=%v）—— "+
			"若 err 是 context deadline exceeded，多半是 Limited 连接没被放行"+
			"（network.WithAllowLimitedConn 丢了，或 EnableRelay 排到了 NoTransports 之前）", n, err)
	}
}

// ---------------- T2：relay 限额（红 / 绿 两级，唯一变量是限额）----------------

func TestRelayLimitDefaultTruncates(t *testing.T) {
	rl := axStartRelay(t, false) // ★ 上游默认限额
	defer rl.h.Close()
	c2 := axStartC2(t, rl.ma, true)
	defer c2.h.Close()

	want := uint64(relayv2.DefaultLimit().Data)
	if c2.rsv.LimitData != want {
		t.Fatalf("relay 给出的 limitData=%d，期望上游默认 %d ⇒ 观测通道没读到限额，"+
			"下面的'被掐断'就不是判据", c2.rsv.LimitData, want)
	}
	if axRelayPay <= int(want) {
		t.Fatalf("测试载荷 %d 没有超过默认限额 %d ⇒ 这一半永远红不了", axRelayPay, want)
	}
	n, err := axDialEcho(t, c2.circuit, axRelayPay)
	if err == nil && n == axRelayPay {
		t.Fatalf("默认限额(%d B)下 %d B 竟然完整回显 —— 限额没有生效，"+
			"那么 T2 另一半的'放宽就通'毫无判别力", want, axRelayPay)
	}
	t.Logf("默认限额按预期掐断：read=%d/%d err=%v", n, axRelayPay, err)
}

func TestRelayLimitRaisedPasses(t *testing.T) {
	rl := axStartRelay(t, true) // ★ 唯一变量：限额放宽
	defer rl.h.Close()
	c2 := axStartC2(t, rl.ma, true)
	defer c2.h.Close()

	if uint64(c2.rsv.LimitData) <= uint64(relayv2.DefaultLimit().Data) {
		t.Fatalf("放宽后 limitData=%d 仍不大于默认 %d ⇒ relay 侧没接受新限额",
			c2.rsv.LimitData, relayv2.DefaultLimit().Data)
	}
	n, err := axDialEcho(t, c2.circuit, axRelayPay)
	if err != nil || n != axRelayPay {
		t.Fatalf("放宽限额后 %d B 仍不能完整回显（read=%d err=%v）⇒ "+
			"C2 侧必须自己设 Reservation.LimitData/LimitDuration（relayv2 的限额是"+
			"**协商**出来的，光放宽 relay 不够）", axRelayPay, n, err)
	}
}

// ---------------- T3：identify 有没有泄漏 C2 的真 IP ----------------

func TestRelayIdentifyLeaksWithoutFactory(t *testing.T) {
	rl := axStartRelay(t, true)
	defer rl.h.Close()
	// ★ hideC2=false：故意复现"没装 AddrsFactory"的形态。
	c2 := axStartC2(t, rl.ma, false)
	defer c2.h.Close()

	leak := axNonLoopback(c2.h)
	if len(leak) == 0 {
		t.Skip("本机没有非环回地址 ⇒ 观测通道无效，这一条无从判定（不算通过）")
	}
	if _, err := axDialEcho(t, c2.circuit, 1024); err != nil {
		t.Fatalf("基线拨号失败，identify 泄漏也就无从观测: %v", err)
	}
	time.Sleep(axRelayIDWait)

	ai, err := peer.AddrInfoFromP2pAddr(ma.StringCast(c2.circuit))
	if err != nil {
		t.Fatalf("解析 circuit 地址: %v", err)
	}
	var seen []string
	if hostInst == nil {
		t.Fatalf("hostInst 为 nil ⇒ 没读到真代码建的 host，这条判据不成立")
	}
	for _, a := range hostInst.Peerstore().Addrs(ai.ID) {
		if !strings.Contains(a.String(), "/p2p-circuit") {
			seen = append(seen, a.String())
		}
	}
	if len(seen) == 0 {
		t.Fatalf("C2 未装 AddrsFactory，agent 却一个非 circuit 地址都没收到 ⇒ "+
			"identify 观测通道失效（%d 秒窗口内），T3 另一半的'看不到'必然是假绿；"+
			"C2 实际通告的非环回地址是 %v", int(axRelayIDWait.Seconds()), leak)
	}
	t.Logf("如预期泄漏：agent 的 peerstore 里有 %d 个 C2 直连地址，例如 %s", len(seen), seen[0])
}

func TestRelayIdentifyHiddenWithFactory(t *testing.T) {
	rl := axStartRelay(t, true)
	defer rl.h.Close()
	c2 := axStartC2(t, rl.ma, true) // ★ 交付形态：只通告 circuit 地址
	defer c2.h.Close()

	if len(axNonLoopback(c2.h)) != 0 {
		t.Fatalf("装了 AddrsFactory 的 C2 仍在通告非环回地址 %v ⇒ 隐藏没生效", axNonLoopback(c2.h))
	}
	if _, err := axDialEcho(t, c2.circuit, 1024); err != nil {
		t.Fatalf("基线拨号失败: %v", err)
	}
	time.Sleep(axRelayIDWait)

	ai, err := peer.AddrInfoFromP2pAddr(ma.StringCast(c2.circuit))
	if err != nil {
		t.Fatalf("解析 circuit 地址: %v", err)
	}
	if hostInst == nil {
		t.Fatalf("hostInst 为 nil ⇒ 没读到真代码建的 host，这条判据不成立")
	}
	for _, a := range hostInst.Peerstore().Addrs(ai.ID) {
		if !strings.Contains(a.String(), "/p2p-circuit") {
			t.Errorf("C2 已装 AddrsFactory，agent 仍拿到直连地址 %s ⇒ C2 真 IP 泄漏", a)
		}
	}
}

// ---------------- T4：阴性对照（relay 停 ⇒ 必须拨不上）----------------

func TestRelayDownFails(t *testing.T) {
	rl := axStartRelay(t, true)
	c2 := axStartC2(t, rl.ma, true) // 交付形态
	defer c2.h.Close()

	// ★ 先证阳性基线：否则"拨不上"可能只是从来就没通过，阴性判据会假绿。
	if n, err := axDialEcho(t, c2.circuit, 1024); err != nil || n != 1024 {
		rl.h.Close()
		t.Fatalf("阳性基线不成立（read=%d err=%v）⇒ 下面的阴性断言没有意义", n, err)
	}
	rl.h.Close()
	time.Sleep(time.Second)

	n, err := axDialEcho(t, c2.circuit, 1024)
	if err == nil && n == 1024 {
		t.Fatalf("relay 已关闭，真 dialLibp2p 仍完整回显 ⇒ 存在 relay 之外的兜底路径"+
			"（多半是 identify 送来的 C2 直连地址）⇒ 「经 relay 藏住 C2 IP」不成立")
	}
	t.Logf("relay 关闭后按预期拨不上：read=%d err=%v", n, err)
}
