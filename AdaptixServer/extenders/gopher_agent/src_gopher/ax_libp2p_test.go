//go:build libp2p

package main

// 批次 2-β 判据3（S7 地址轮转被架空）+ 判据4（M8④ host 建失败被永久缓存）。
//
// ★ 本文件带 `//go:build libp2p`：它测的两个符号只在带 tag 的变体里存在。
//   `go test`（不带 tag）不编它 —— 这是**刻意的**，替身变体里根本没有 libp2pHost。

import (
	"reflect"
	"sync"
	"testing"

	"gopher/utils"
)

// ── 判据3：axPutFirst（纯函数，表驱动 + 三个阴性样本）────────────────────

func TestAxPutFirst(t *testing.T) {
	const A, B, C = "a", "b", "c"
	cases := []struct {
		name string
		in   []string
		addr string
		want []string
	}{
		{"中间元素转到队首", []string{A, B, C}, B, []string{B, C, A}},
		{"末元素转到队首", []string{A, B, C}, C, []string{C, A, B}},
		{"已是队首则原样", []string{A, B, C}, A, []string{A, B, C}},
		// ↓ 阴性样本：后三种若"顺手"把 addr 塞进列表，等于凭空多打一个可能非法的地址。
		{"addr 为空则原样", []string{A, B, C}, "", []string{A, B, C}},
		{"addr 不在列表里则原样", []string{A, B, C}, "z", []string{A, B, C}},
		{"单元素则原样", []string{A}, B, []string{A}},
		{"空列表则原样", []string{}, A, []string{}},
	}
	for _, c := range cases {
		if got := axPutFirst(c.in, c.addr); !reflect.DeepEqual(got, c.want) {
			t.Errorf("%s: axPutFirst(%v, %q) = %v，期望 %v", c.name, c.in, c.addr, got, c.want)
		}
	}
	// 调用方（dialLibp2p）后面还要用原来的 addrs 做兜底遍历，不能被就地改序。
	in := []string{A, B, C}
	_ = axPutFirst(in, C)
	if !reflect.DeepEqual(in, []string{A, B, C}) {
		t.Errorf("axPutFirst 就地改动了入参: %v", in)
	}
}

// ── 判据4：libp2pHost 的**失败**不能被缓存 ────────────────────────────────

// resetHostCache 把包级缓存清空，让每条测试自带前置条件、不依赖执行顺序。
// ★ 必须显式重置：若并发测试先跑，hostInst 已非 nil，下面的坏私钥调用会在
//   `if hostInst != nil` 处直接返回成功 —— 判据①就变成了重言式。
func resetHostCache() {
	hostMu.Lock()
	hostInst = nil
	hostMu.Unlock()
}

func TestLibp2pHostDoesNotCacheFailure(t *testing.T) {
	resetHostCache()

	bad := utils.Profile{Libp2pPrivKey: []byte{0xff, 0xff, 0xff, 0xff}}
	h0, err0 := libp2pHost(bad)
	if err0 == nil {
		t.Fatalf("① 非法私钥应当失败，实得 host=%v", h0)
	}
	t.Logf("① 非法私钥按预期失败: %v", err0)

	// ★ 这一句是整条判据的判别力所在：旧码（sync.Once）在这里会拿回**①的同一个错误**，
	//   于是这个进程的 libp2p 通道被终生钉死，而重试永远不会再发生。
	good := utils.Profile{}
	h1, err1 := libp2pHost(good)
	if err1 != nil {
		t.Fatalf("② 前一次失败被缓存了（sync.Once 的老毛病还在）: %v", err1)
	}
	if h1 == nil {
		t.Fatal("② err==nil 但 host==nil")
	}
	t.Logf("② 紧接着用合法 profile 建 host 成功: %s", h1.ID())

	// ③ 缓存必须**生效**（成功要缓存）。缺了这一条，把锁和缓存整个删掉、每次重建
	//    也能过①② —— 那条性质就没人守了。
	h2, err2 := libp2pHost(good)
	if err2 != nil || h2 == nil {
		t.Fatalf("③ 第三次调用失败: %v", err2)
	}
	if h2.ID() != h1.ID() {
		t.Errorf("③ host 未被复用：%s -> %s ⇒ 每次都重建，缓存没生效", h1.ID(), h2.ID())
	}
}

// TestLibp2pHostConcurrent 盯的是锁：并发拨号时只应建出**一个** host。
// ★ 配 `-race` 跑才有判别力（无锁版本在这里既可能建出多个 host，也可能报数据竞争）。
func TestLibp2pHostConcurrent(t *testing.T) {
	resetHostCache()
	const N = 8
	ids := make([]string, N)
	var wg sync.WaitGroup
	for i := 0; i < N; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			h, err := libp2pHost(utils.Profile{})
			if err != nil || h == nil {
				return
			}
			ids[i] = h.ID().String()
		}(i)
	}
	wg.Wait()
	for i, id := range ids {
		if id == "" {
			t.Fatalf("goroutine %d 没拿到 host", i)
		}
		if id != ids[0] {
			t.Errorf("并发下建出了多个 host: [0]=%s [%d]=%s", ids[0], i, id)
		}
	}
	t.Logf("%d 个并发调用共用同一个 host: %s", N, ids[0])
}
