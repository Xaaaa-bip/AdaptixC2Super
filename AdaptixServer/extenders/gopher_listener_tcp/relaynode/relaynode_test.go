// relaynode 的门 —— 三条判据，每条都驱动**真配置**（newRelayHost / newResources /
// loadOrCreateKey），不是测试里再抄一份选项。
//
//	T1 真 relay host 能接受别人的预约，且协商出的限额 == 节点设的限额
//	   能红：把 newRelayHost 里的 libp2p.ForceReachabilityPublic() 删掉 —— relaysvc
//	   只在 ReachabilityPublic 事件上建 relay，删了之后进程照样监听、不报错，
//	   但**一台预约都不接受**。这正是本节点最贵的那个坑。
//
//	T2 约定限额必须高于上游默认（1<<17 / 2m）
//	   能红：把 defLimitData 改回 1<<17。
//
//	T3 身份持久化：同一个 key 文件两次载入必须是同一个 peer id，且 0600
//	   能红：loadOrCreateKey 忘了写文件（或写失败被吞）。
//
// ★ 本文件**不带** build tag：relaynode 是独立可执行文件，没有"不带 libp2p 的变体"。
package main

import (
	"context"
	"crypto/rand"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/libp2p/go-libp2p"
	libcrypto "github.com/libp2p/go-libp2p/core/crypto"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/p2p/muxer/yamux"
	relayclient "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/client"
	relayv2 "github.com/libp2p/go-libp2p/p2p/protocol/circuitv2/relay"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/libp2p/go-libp2p/p2p/transport/tcp"
)

func TestRelayNodeAcceptsReservation(t *testing.T) {
	sk, _, err := libcrypto.GenerateEd25519Key(rand.Reader)
	if err != nil {
		t.Fatalf("生成 relay 身份失败: %v", err)
	}
	res := newResources(defLimitData, defLimitDuration, defMaxCircuits, defMaxReservations, defReservationTTL)
	h, err := newRelayHost([]string{"/ip4/127.0.0.1/tcp/0"}, sk, res)
	if err != nil {
		t.Fatalf("newRelayHost: %v", err)
	}
	defer h.Close()
	if len(h.Addrs()) == 0 {
		t.Fatal("relay host 没有任何监听地址 ⇒ 后面「预约失败」分不清是配置还是没监听")
	}

	// 预约方：一个只拨号的最小 host（与 agent 侧同构）。
	ch, err := libp2p.New(
		libp2p.NoListenAddrs,
		libp2p.NoTransports,
		libp2p.EnableRelay(),
		libp2p.Transport(tcp.NewTCPTransport),
		libp2p.Security(noise.ID, noise.New),
		libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
	)
	if err != nil {
		t.Fatalf("建预约方 host: %v", err)
	}
	defer ch.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	rsv, err := relayclient.Reserve(ctx, ch, peer.AddrInfo{ID: h.ID(), Addrs: h.Addrs()})
	if err != nil {
		t.Fatalf("对 relaynode 预约失败: %v —— relay 服务没起来？"+
			"（十有八九是 newRelayHost 里的 libp2p.ForceReachabilityPublic() 丢了："+
			"没有它 relaysvc 永远不建 relay，而进程照样在监听、不报任何错）", err)
	}
	if rsv.LimitData != uint64(res.Limit.Data) {
		t.Errorf("协商到的 limitData=%d，期望节点设的 %d ⇒ 限额设置没生效",
			rsv.LimitData, res.Limit.Data)
	}
	if rsv.LimitDuration != res.Limit.Duration {
		t.Errorf("协商到的 limitDuration=%s，期望 %s", rsv.LimitDuration, res.Limit.Duration)
	}
}

func TestRelayNodeLimitsAreRaised(t *testing.T) {
	d := relayv2.DefaultLimit()
	if defLimitData <= d.Data {
		t.Errorf("约定数据限额 %d 没有高于上游默认 %d ⇒ C2 会话会被掐断"+
			"（实测：默认档传 200000 B 得 read=0 err=stream reset）", defLimitData, d.Data)
	}
	if defLimitDuration <= d.Duration {
		t.Errorf("约定时长限额 %s 没有高于上游默认 %s", defLimitDuration, d.Duration)
	}
}

func TestRelayNodeIdentityPersists(t *testing.T) {
	// ★ TMPDIR 必须指到白名单目录（门脚本设的是 /root/gp5probe/tmp）：本项目有判据
	//   把 /tmp 出现新文件当异常。没设时在这里明确失败，而不是默默往 /tmp 写。
	if td := os.TempDir(); strings.HasPrefix(td, "/tmp") {
		t.Fatalf("TMPDIR=%s 指向 /tmp —— 本项目判据禁止在 /tmp 落文件，请先设 TMPDIR", td)
	}
	p := filepath.Join(t.TempDir(), "relaynode.key")

	sk1, gen1, err := loadOrCreateKey(p)
	if err != nil {
		t.Fatalf("首次 loadOrCreateKey: %v", err)
	}
	if !gen1 {
		t.Error("首次调用没有报告「新生成」⇒ 下面的对比失去意义（可能读到了陈旧文件）")
	}
	sk2, gen2, err := loadOrCreateKey(p)
	if err != nil {
		t.Fatalf("二次 loadOrCreateKey: %v", err)
	}
	if gen2 {
		t.Error("二次调用又生成了一份新身份 ⇒ 文件没被复用")
	}
	id1, err := peer.IDFromPrivateKey(sk1)
	if err != nil {
		t.Fatalf("算 peer id: %v", err)
	}
	id2, err := peer.IDFromPrivateKey(sk2)
	if err != nil {
		t.Fatalf("算 peer id: %v", err)
	}
	if id1 != id2 {
		t.Fatalf("身份没持久化：%s → %s ⇒ relay 重启即换 peer id，"+
			"C2 的 l2p_relays 与所有已发出的 agent 地址会一起失效", id1, id2)
	}
	fi, err := os.Stat(p)
	if err != nil {
		t.Fatalf("私钥文件不见了: %v", err)
	}
	if fi.Mode().Perm() != 0600 {
		t.Errorf("私钥文件权限是 %v，期望 0600", fi.Mode().Perm())
	}
}
