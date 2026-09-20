//go:build !libp2p

package main

// Phase5 §4.2：**默认产物**（不带 `-tags libp2p`）里 libp2p 的替身。
//
// 存在的唯一理由是让 `transport.go` 里那句 `profile.Transport == "libp2p"` 能编译 ——
// 而默认产物**一个 libp2p 字节都不含**（这是 §4.2 那个决策的全部收益）。
//
// ★ 返回错误而不是 panic：一个把 transport 配错成 libp2p 的 profile 应该让**这一次
//   连接尝试**失败（调用点会按各自的语义 `continue`/`return` 重试），而不是把 agent
//   整个打死。
// ★ 更正（M9/S5）：原文接着说"否则 operator 只看到上线失败、查不出原因" —— 这句的
//   **理由恰好是反的**：只 `continue` 而不把错误说出来，operator 看到的才真的只有
//   "不上线"；panic 至少会在 stderr 留一行。真正的补救不是 panic，而是把这条错误
//   **报出去**：见 main.go 的 axReportDialErr，以及 pl_main.go 构建后对产物字节的
//   硬断言 —— 让这种产物根本发不出去。

import (
	"fmt"
	"net"

	"gopher/utils"
)

// axLibp2pBuildTag 是**替身被编进来**的构建指纹（B-B1 的硬门），与
// transport_libp2p.go 里同名常量的取值互斥。pl_main.go 构建后按它断言。
const axLibp2pBuildTag = "axc2:l2p:disabled:v1"

func dialLibp2p(profile utils.Profile, addr string) (net.Conn, error) {
	return nil, fmt.Errorf("this build has no libp2p support (rebuild the agent with -tags libp2p) [%s]", axLibp2pBuildTag)
}
