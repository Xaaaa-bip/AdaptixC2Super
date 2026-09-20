// Phase5 §4.5（noquic fork）：webtransport / webrtc 的纯 multiaddr 判定。
//
// ★ 为什么搬到这里：p2p/transport/webtransport 与 p2p/transport/webrtc 是**真传输**，
// 只要被 import 就会把 quic-go / webtransport-go 编进插件；而宿主 adaptixserver 编的是
// 另一个版本的 quic-go ⇒ plugin.Open 逐包比构建 ID 必然失败。
// 本包（libp2p.New() 必用）只用到它们的两个判定函数，与传输实现无关，故就地复刻。
//
// ★ 函数体**逐字**取自上游 go-libp2p v0.36.5，仅改为非导出名，行为不做任何改动：
// p2p/transport/webtransport/multiaddr.go:81 IsWebtransportMultiaddr
// p2p/transport/webrtc/transport.go:638 IsWebRTCDirectMultiaddr
//
// ★ 本构建里这两个判定恒为"否"也无碍 —— 没有那两个传输，就不存在 webtransport /
// webrtc-direct 地址，也不存在 certhash。保留完整语义是为了万一以后加回 quic 时，
// 不留下静默的行为差异。
package basichost

import (
	ma "github.com/multiformats/go-multiaddr"
)

// isWebtransportMultiaddr reports whether the multiaddr is a webtransport one,
// and how many trailing certhashes it carries.
func isWebtransportMultiaddr(multiaddr ma.Multiaddr) (bool, int) {
	const (
		init = iota
		foundUDP
		foundQuicV1
		foundWebTransport
	)
	state := init
	certhashCount := 0

	ma.ForEach(multiaddr, func(c ma.Component) bool {
		switch c.Protocol().Code {
		case ma.P_UDP:
			if state == init {
				state = foundUDP
			}
		case ma.P_QUIC_V1:
			if state == foundUDP {
				state = foundQuicV1
			}
		case ma.P_WEBTRANSPORT:
			if state == foundQuicV1 {
				state = foundWebTransport
			}
		case ma.P_CERTHASH:
			if state == foundWebTransport {
				certhashCount++
			}
		}
		return true
	})
	return state == foundWebTransport, certhashCount
}

// isWebRTCDirectMultiaddr reports whether the multiaddr is a webrtc-direct one,
// and how many trailing certhashes it carries.
func isWebRTCDirectMultiaddr(addr ma.Multiaddr) (bool, int) {
	var foundUDP, foundWebRTC bool
	certHashCount := 0
	ma.ForEach(addr, func(c ma.Component) bool {
		if !foundUDP {
			if c.Protocol().Code == ma.P_UDP {
				foundUDP = true
			}
			return true
		}
		if !foundWebRTC && foundUDP {
			// protocol after udp must be webrtc-direct
			if c.Protocol().Code != ma.P_WEBRTC_DIRECT {
				return false
			}
			foundWebRTC = true
			return true
		}
		if foundWebRTC {
			if c.Protocol().Code == ma.P_CERTHASH {
				certHashCount++
			} else {
				return false
			}
		}
		return true
	})
	return foundUDP && foundWebRTC, certHashCount
}
