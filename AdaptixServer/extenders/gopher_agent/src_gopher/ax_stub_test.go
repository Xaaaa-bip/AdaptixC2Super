//go:build !libp2p

package main

// 批次 2-β 判据5 的**替身变体**分支：不带 `-tags libp2p` 时，profile 说 libp2p 必须
// **立刻**报错（而不是 panic，也不是静默重试）。
//
// ★ 必须带 `!libp2p` 约束：带 tag 的变体里 dialLibp2p 是真的 libp2p 实现，会去真拨号，
//   这条判据在那一侧根本不成立（那一侧由判据3/4 覆盖）。
// ★ 它同时是 B-B1 在**运行期**的对照：证明替身路径真的会说出一句可诊断的话，而且
//   那句话里带着构建指纹 —— 这是 pl_main.go 产物扫描能成立的前提。缺了这句话，
//   产物扫描查的就是一个运行期从不出现的字符串（第⑨种假绿的镜像）。

import (
	"strings"
	"testing"
	"time"

	"gopher/utils"
)

func TestDialProfileStubTransportFailsFast(t *testing.T) {
	p := utils.Profile{Transport: "libp2p"}
	t0 := time.Now()
	conn, err := DialProfile(p, "127.0.0.1:1")
	dt := time.Since(t0)

	if conn != nil {
		_ = conn.Close()
		t.Fatalf("替身变体不该建立任何连接")
	}
	if err == nil {
		t.Fatal("替身变体应当返回错误")
	}
	// 必须是**立刻**失败：走的是编译期的替身分支，不该有任何网络等待。
	if dt > 2*time.Second {
		t.Errorf("耗时 %v ⇒ 不是立刻失败", dt.Round(time.Millisecond))
	}
	// 错误串里必须带构建指纹 —— pl_main.go 的产物扫描就查这个字面量。
	if !strings.Contains(err.Error(), "axc2:l2p:disabled:v1") {
		t.Errorf("错误串里没有构建指纹 axc2:l2p:disabled:v1，B-B1 的产物扫描将无法成立: %q", err.Error())
	}
	t.Logf("替身变体：%v", err)
}
