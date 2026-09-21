package main

// 批次 2-β 判据2：重连间隔抖动（M8①）。
//
// ★ 为什么调**真函数**而不是在测试里复刻一遍逻辑：复刻的测试只能证明"我复刻对了"，
//   与被测代码是否真的走这条路无关（归档里第⑩种假绿的近亲）。
// ★ 红对照**内嵌在同一文件里**（legacyReconnectDelay = 改造前的固定间隔）：同一台
//   机器、同一次运行、同一组参数下，这三条性质在旧行为上必须**失败**。缺了这一半，
//   测试通过只说明这些性质**碰巧**成立，不说明判据有判别力。

import (
	"testing"
	"time"
)

// legacyReconnectDelay 是改造前的行为，逐字照抄（main.go 原第 135 行）：
//
//	time.Sleep(time.Duration(profile.ConnTimeout) * time.Second)
func legacyReconnectDelay(base int) time.Duration {
	return time.Duration(base) * time.Second
}

func TestAxReconnectDelayHasJitter(t *testing.T) {
	const base, N = 30, 200
	seen := map[time.Duration]int{}
	var sum time.Duration
	for i := 0; i < N; i++ {
		d := axReconnectDelay(base)
		seen[d]++
		sum += d
	}

	// ① 取值必须多于 1 个 —— 固定间隔永远只有 1 个
	if len(seen) < 5 {
		t.Errorf("① 只有 %d 个不同取值（%v）⇒ 抖动没生效", len(seen), seen)
	}
	// ② 必须**有界**：抖动不能把重连拖成分钟级，也不能缩到 0
	//   ★ 单位坑：`time.Duration(30)` 是 30 **纳秒**，乘 time.Second 才是 30 秒。
	//     第一版漏了 time.Second，于是把 [20s,40s] 的实际取值拿去和 [20ns,40ns] 比。
	lo := time.Duration(base) * time.Second * 2 / 3
	hi := time.Duration(base) * time.Second * 4 / 3
	for d, n := range seen {
		if d < lo || d > hi {
			t.Errorf("② 取值 %v（出现 %d 次）越界 [%v, %v]", d, n, lo, hi)
		}
	}
	// ③ 均值不偏 —— 对称抖动**不改变**平均重连间隔，操作手感与改造前一致
	avg := sum / N
	if avg < time.Duration(base)*time.Second*9/10 || avg > time.Duration(base)*time.Second*11/10 {
		t.Errorf("③ 均值 %v 偏离 base=%ds 超过 ±10%%", avg, base)
	}
	t.Logf("① %d 个取值 ② 全部落在 [%v, %v] ③ 均值 %v（base=%ds）", len(seen), lo, hi, avg, base)
}

// TestLegacyHasNoJitter 是**红对照**：把判据①套在改造前的固定间隔上必须红。
// 它绿了 ⇒ 判据①是重言式，整条测试没有判别力。
func TestLegacyHasNoJitter(t *testing.T) {
	const base, N = 30, 200
	seen := map[time.Duration]int{}
	for i := 0; i < N; i++ {
		seen[legacyReconnectDelay(base)]++
	}
	if len(seen) >= 5 {
		t.Fatalf("红对照失效：固定间隔居然有 %d 个取值 ⇒ 判据①没有判别力", len(seen))
	}
	t.Logf("红对照成立：固定间隔只有 %d 个取值 %v", len(seen), seen)
}

// TestAxReconnectDelayEdgeCases 盯的是**零值与上界**：
//   - base<=0 必须与改造前逐字等价（返回 0 ⇒ 不睡），否则"没配 ConnTimeout"的
//     profile 会凭空开始睡 1 秒；
//   - base=1 时 amp 的钳位不能让结果变成 0（0 秒 = 忙等重连）；
//   - base 极大时 2*amp+1 不能溢出 uint16 —— 溢出会让取模退化成常数，抖动静默消失。
func TestAxReconnectDelayEdgeCases(t *testing.T) {
	for _, b := range []int{0, -1, -3600} {
		if d := axReconnectDelay(b); d != 0 {
			t.Errorf("base=%d 应返回 0（与改造前等价），实得 %v", b, d)
		}
	}

	seen := map[time.Duration]bool{}
	for i := 0; i < 200; i++ {
		d := axReconnectDelay(1)
		if d < time.Second || d > 2*time.Second {
			t.Fatalf("base=1 取值 %v 越界 [1s, 2s]（0 秒 = 忙等重连）", d)
		}
		seen[d] = true
	}
	if len(seen) < 2 {
		t.Errorf("base=1 应有 {1s,2s} 两个取值，实得 %v", seen)
	}

	const big = 65535
	lo, hi := time.Duration(big-300)*time.Second, time.Duration(big+300)*time.Second
	seenBig := map[time.Duration]bool{}
	for i := 0; i < 200; i++ {
		d := axReconnectDelay(big)
		if d < lo || d > hi {
			t.Fatalf("base=65535 取值 %v 越界 [%v, %v] ⇒ 2*amp+1 溢出", d, lo, hi)
		}
		seenBig[d] = true
	}
	if len(seenBig) < 5 {
		t.Errorf("base=65535 只有 %d 个取值 ⇒ 取模溢出、抖动被压成常数", len(seenBig))
	}
}
