package main

// 批次 2-β 判据1（B-B1 硬门）：构建后传输指纹断言 —— 用**真函数**、在**真产物**上跑。
//
// ★ 为什么不在测试里复刻一遍扫描逻辑：那样只能证明"我复刻对了"，与被测的
//   axVerifyTransportTag 是否真的会被 /agent/generate 调用到无关（第⑩种假绿）。
//
// ★ 假绿防线（缺一不可）：
//   A. 五个分支**逐个**验证，其中四个是"必须报错"的：只验正样本的话，一个永远
//      返回 nil 的空函数也能全过（归档第⑤种：两个空串也相等）。
//   B. 两个产物必须是**真的 ELF**（不是空文件/被截断的文件）—— 否则"扫描没找到"
//      可能只是因为文件是空的。这正是第⑨种假绿：负向观测必须先证明观测通道有效。
//   C. 断言覆盖"两枚指纹都没有"这一态：它对应"扫描通道本身失效"，必须被显式报错，
//      而不是被当成"没有 libp2p，正常"。
//
// 用法（需先构建两个变体产物，见 Makefile 的 agent / libp2p 目标）：
//
//	go test -run TestAxVerifyTransportTag -v .

import (
	"os"
	"strings"
	"testing"
)

const (
	probeStub = "/root/gp5probe/tmp/m8build/agent_stub" // 不带 -tags libp2p
	probeL2p  = "/root/gp5probe/tmp/m8build/agent_l2p"  // 带 -tags libp2p
)

// readElf 读产物并确认它是真的 ELF。★ 缺了这一步，"扫描没找到指纹"与"文件是空的"
// 无法区分 —— 而后者会让所有"必须报错"的分支**假绿**。
func readElf(t *testing.T, path string) []byte {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("读不到产物 %s: %v（先构建两个变体再跑本测试；不要用 Skip 蒙混）", path, err)
	}
	if len(b) < 1<<20 {
		t.Fatalf("%s 只有 %d B，不像一个真产物 ⇒ 观测通道无效", path, len(b))
	}
	if string(b[:4]) != "\x7fELF" {
		t.Fatalf("%s magic=%q，不是 ELF ⇒ 观测通道无效", path, b[:4])
	}
	return b
}

func TestAxVerifyTransportTag(t *testing.T) {
	stub, l2p := readElf(t, probeStub), readElf(t, probeL2p)

	// 前置：两枚指纹在两个产物里必须**互斥**。这一条红了就说明构建/tag 本身有问题，
	// 后面的分支验证都没意义。
	if !strings.Contains(string(l2p), axTagLibp2p) || strings.Contains(string(l2p), axTagStub) {
		t.Fatalf("带 tag 的产物指纹不对：应含 %q 且不含 %q", axTagLibp2p, axTagStub)
	}
	if !strings.Contains(string(stub), axTagStub) || strings.Contains(string(stub), axTagLibp2p) {
		t.Fatalf("默认产物指纹不对：应含 %q 且不含 %q", axTagStub, axTagLibp2p)
	}
	t.Logf("前置成立：stub %d B 含 disabled 指纹；l2p %d B 含 enabled 指纹", len(stub), len(l2p))

	cases := []struct {
		name       string
		path       string
		wantLibp2p bool
		wantErr    string // 为空表示必须返回 nil
	}{
		{"① 带 tag 产物 + profile 要 libp2p ⇒ 放行", probeL2p, true, ""},
		{"② 默认产物 + profile 不要 libp2p ⇒ 放行", probeStub, false, ""},
		// ↓ 四个"必须报错"，逐个盯住一条分支
		{"③ 默认产物 + profile 要 libp2p ⇒ 必须拦下（tag 没生效）", probeStub, true, "did not take effect"},
		{"④ 带 tag 产物 + profile 不要 libp2p ⇒ 必须拦下（多编了）", probeL2p, false, "no listener profile asked for libp2p"},
		{"⑤ 文件不存在 ⇒ 必须报错（不能当成「没有 libp2p」而放行）", "/root/gp5probe/tmp/m8build/does-not-exist", false, "read"},
		{"⑥ 空文件 ⇒ 必须报错（两枚指纹都没有 = 扫描通道本身失效）", "/dev/null", false, "byte scan itself is not working"},
	}
	for _, c := range cases {
		err := axVerifyTransportTag(c.path, c.wantLibp2p)
		switch {
		case c.wantErr == "" && err != nil:
			t.Errorf("%s：期望放行，实得错误 %v", c.name, err)
		case c.wantErr != "" && err == nil:
			t.Errorf("%s：期望报错（含 %q），实得 nil ⇒ 这条分支没有判别力", c.name, c.wantErr)
		case c.wantErr != "" && !strings.Contains(err.Error(), c.wantErr):
			t.Errorf("%s：报错了但理由不对\n  期望含: %q\n  实得: %v", c.name, c.wantErr, err)
		default:
			t.Logf("%s：%v", c.name, err)
		}
	}
}

// TestAxVerifyTransportTagScanChannelIsLive 单独证明"扫描通道有效"。
// ★ 用一个**临时文件**装进真产物的一小片字节：它含 enabled 指纹 ⇒ want=true 必须放行。
// 这条把"扫描逻辑正确"与"读的是哪个文件"解耦 —— 若上一个测试全绿只是因为它总在读
// 那两个恰好正确的文件，这一条会红。
func TestAxVerifyTransportTagScanChannelIsLive(t *testing.T) {
	l2p := readElf(t, probeL2p)
	// 截一小段含指纹的字节，制造一个"迷你产物"
	i := strings.Index(string(l2p), axTagLibp2p)
	if i < 0 {
		t.Fatal("产物里找不到指纹 ⇒ 观测通道无效")
	}
	// ★ 刻意**不用 t.TempDir()**：它默认落在 /tmp，而本交付物的 P17 T6 判据是
	//   「/tmp 时间窗内新增文件」—— 在 /tmp 写文件会造成**假红**。scratch 一律落
	//   /root/gp5probe/tmp（白名单内）。
	tmp := "/root/gp5probe/tmp/m8build/mini_scan_probe.bin"
	if err := os.WriteFile(tmp, l2p[i-16:i+len(axTagLibp2p)+16], 0o644); err != nil {
		t.Fatalf("写临时文件失败: %v", err)
	}
	defer func() { _ = os.Remove(tmp) }()
	if err := axVerifyTransportTag(tmp, true); err != nil {
		t.Errorf("含指纹的小文件 + want=true 应当放行，实得: %v", err)
	}
	if err := axVerifyTransportTag(tmp, false); err == nil {
		t.Error("同一个文件 + want=false 必须报错（它含 enabled 指纹）")
	}
}
