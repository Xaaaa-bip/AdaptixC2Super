//go:build !libp2p

package main

// Phase5 §4.2：**默认产物**（不带 `-tags libp2p`）里 libp2p 的替身。
//
// 存在的唯一理由是让 `transport.go` 里那句 `profile.Transport == "libp2p"` 能编译 ——
// 而默认产物**一个 libp2p 字节都不含**（这是 §4.2 那个决策的全部收益）。
//
// ★ 返回错误而不是 panic：一个把 transport 配错成 libp2p 的 profile 应该让**这一次
//   连接尝试**失败（调用点会按各自的语义 `continue`/`return` 重试），而不是把 agent
//   整个打死 —— 后者在 operator 眼里只是"上线失败"，查不出原因。

import (
	"errors"
	"net"

	"gopher/utils"
)

func dialLibp2p(profile utils.Profile, addr string) (net.Conn, error) {
	return nil, errors.New("this build has no libp2p support (rebuild the agent with -tags libp2p)")
}
