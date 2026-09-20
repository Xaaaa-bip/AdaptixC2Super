//go:build libp2p

package main

// 批次 8 判据：identify 协议报出的 agentVersion（= libp2p 的 user-agent）必须是中性值。
//
// ## 为什么必须有这条判据（实测，不是推测）
//
// libp2p v0.36.5 的默认 user-agent **不是**固定串，而是
// `fmt.Sprintf("%s@%s", bi.Main.Path, bi.Main.Version|revision)`
// （p2p/protocol/identify/user_agent.go:20 / :39），其中 `bi.Main` 是**编译该二进制的
// 模块本身**。本项目 `bi.Main.Version` 恒为 "(devel)"、仓库又不是 git（vcs.revision
// 缺失）⇒ revision 恒为空串 ⇒ 默认值 = **`<Main.Path>@`**。
//
// ★★★ 关键：`Main.Path` **随构建方式而变**，于是同一个项目的三个部件报出三个
// **互不相同**的自曝串 —— 对端据此甚至能区分对方是哪个部件：
//
//	交付的 agent（pl_main.go 从 dist/src_gopher 现编，模块 "gopher"）
//	  Main.Path=gopher                    → **"gopher@"**（实测，= 下面红侧打印的值）
//	agent_gopher.so（builder 插件，teamserver 侧 —— **不是**交付给目标机的那枚）
//	  Main.Path=adaptix_agent_gopher      → "adaptix_agent_gopher@"
//	listener_gopher_tcp.so（-buildmode=plugin，直接对文件列表编译）
//	  Main.Path=command-line-arguments    → "command-line-arguments@"
//	本测试自身（go test：Main.Path 是**被测包的导入路径**）
//	  → **实测 "gopher@"**（就是下面红侧打印的那个值）
//
// 四个都是**以 `@` 结尾的畸形串**（生态里不存在），且都直接写出产品名或构建方式。
// 即：不设 UserAgent，就等于每次 identify 都向对端自报家门，并留下永久指纹。
//
// ★ 这条"默认值依构建方式而变"是**实测出来的、不是推出来的**：静态读
// `go version -m agent_gopher.so` 只能得到那**一种**构建方式下的值（我最初据此
// 预测红侧会打印 adaptix_agent_gopher@，实测打印的是 gopher@）。
// 结论不变且更强：**每一种构建方式都自曝，且各不相同**。
//
// ## ★★ 红绿同帧（本测试的设计核心）
//
// 在**同一条连接、同一时刻、同一个观测通道**上读两个方向：
//
//	H_agent = 走**生产代码** libp2pHost()  ⇒ 必须等于中性值
//	H_peer  = **裸 host**，opts 与 libp2pHost 只差 UserAgent 与监听声明两处
//	          （监听是必需的：生产代码是 NoListenAddrs 的**拨号方**）
//	          ⇒ 必须**不等于**中性值，并把实测自曝串打印出来
//
// H_peer 一举两得：① 它是"不修会怎样"的**活体物证**（不依赖任何推测）；
// ② 它是**观测通道判别力的自证** —— 若通道失效（两个 host 混了、peerstore 读不到），
// 两个方向会读到同一个值，判据自己报错，而**不会安静地 PASS**。
//
// ★ 这比"临时删掉源码那一行再重新 build"更强，且**不需要破坏源码**。
//   观测手法与上游 `p2p/protocol/identify/id_test.go:518 TestUserAgent` 同构。
//
// ## ★★★ 第 11 形态假绿：判据引用被测对象自己的定义
//
// 若本文件断言 `got == axLibp2pUserAgent`（那个**生产常量**），那么把常量改成
// `"adaptix/x"` 时测试**照样 PASS** —— 判据与被告同源，成了重言式。
// 因此这里用**独立字面量** axUAWant 锚定，并**另行**断言生产常量 == axUAWant。
// 两条互补：前者管"实际报出去的串"，后者管"锚点没被改"。

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/libp2p/go-libp2p"
	"github.com/libp2p/go-libp2p/core/host"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/p2p/muxer/yamux"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/libp2p/go-libp2p/p2p/transport/tcp"

	"gopher/utils"
)

// axUAWant 是**判据锚点**：中性值必须长这样。
// ★ 刻意写成字面量、**不引用生产常量**（引用了就是重言式，见文件头）。
const axUAWant = "go-libp2p"

// axUABadSubs 是**不得出现在对端收到的串里**的子串（大小写不敏感）。
// 前三个是本项目的自我标识；第四个是 plugin 构建的自曝值。
var axUABadSubs = []string{"adaptix", "gopher", "axc2", "command-line-arguments"}

// axReadUA 从 h 的 peerstore 读**对端**（p）报来的 agentVersion。
//
// ★ key 用字面量 "AgentVersion"：identify 以
// `Peerstore().Put(p, "AgentVersion", av)` 写入（p2p/protocol/identify/id.go:839），
// 上游自己的测试也用这个字面量读（id_test.go:544）。刻意**不**在本包定义一个同名
// 常量去"封装"它 —— 那会与 peerstore 的约定脱钩，写错了反而看不出来。
//
// ★ 读不到 ⇒ **Fatal**：这是"观测通道失效"，与"值是中性值"是两回事，绝不能混成 PASS。
func axReadUA(t *testing.T, h host.Host, p peer.ID) string {
	t.Helper()
	v, err := h.Peerstore().Get(p, "AgentVersion")
	if err != nil {
		t.Fatalf("观测通道失效：读不到 %s 的 AgentVersion（%v）⇒ 本判据的 PASS 不成立", p, err)
	}
	s, ok := v.(string)
	if !ok {
		t.Fatalf("观测通道失效：AgentVersion 不是字符串而是 %T（%v）", v, v)
	}
	if s == "" {
		t.Fatalf("观测通道失效：AgentVersion 是空串 ⇒ '不是自曝串'会凭空成立，PASS 不算数")
	}
	return s
}

// TestUAAgentVersion 判"生产代码报出的 agentVersion 是中性值"，并同帧给出红侧物证。
func TestUAAgentVersion(t *testing.T) {
	// 判据前置①：锚点本身没被改。常量被改 ⇒ 下面所有断言的期望值都是假的。
	if axLibp2pUserAgent != axUAWant {
		t.Fatalf("判据锚点被改：生产常量 axLibp2pUserAgent = %q，期望 %q", axLibp2pUserAgent, axUAWant)
	}

	// 判据前置②：走**生产代码**取 host。先清包级缓存，免受其它测试的执行顺序影响
	//（resetHostCache 定义在同 package 的 ax_libp2p_test.go，直接复用）。
	resetHostCache()
	hAgent, err := libp2pHost(utils.Profile{})
	if err != nil {
		t.Fatalf("生产路径 libp2pHost 建 host 失败: %v", err)
	}
	defer hAgent.Close()

	// 对照 host：与 libp2pHost 的 opts **只差两处** —— UserAgent（被测量）与监听声明
	// （必需：生产路径是 NoListenAddrs 的拨号方，不监听就没法被拨）。
	hPeer, err := libp2p.New(
		libp2p.ListenAddrStrings("/ip4/127.0.0.1/tcp/0"),
		libp2p.NoTransports,
		libp2p.EnableRelay(),
		libp2p.Transport(tcp.NewTCPTransport),
		libp2p.Security(noise.ID, noise.New),
		libp2p.Muxer(yamux.ID, yamux.DefaultTransport),
	)
	if err != nil {
		t.Fatalf("建对照 host 失败: %v", err)
	}
	defer hPeer.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 3*axRelayIDWait)
	defer cancel()
	if err := hAgent.Connect(ctx, peer.AddrInfo{ID: hPeer.ID(), Addrs: hPeer.Addrs()}); err != nil {
		t.Fatalf("拨号失败（观测窗口未建立）: %v", err)
	}
	// ★ 窗口取批次5 已验收的 axRelayIDWait，不另造魔数。上游测试在 Connect 后直接读
	//   也能读到，这里留窗口是为了在慢机器/冷缓存上不变成偶发假红。
	select {
	case <-time.After(axRelayIDWait):
	case <-ctx.Done():
		t.Fatalf("等 identify 超时 ⇒ 观测窗口未建立，本判据的 PASS 不成立")
	}

	// ── 绿侧：对端（对照 host）眼里，**生产代码**报的是什么 ──
	gotProd := axReadUA(t, hPeer, hAgent.ID())
	t.Logf("绿侧：对端从**生产代码**收到的 agentVersion = %q", gotProd)

	// ── 红侧：生产代码眼里，**没设 UserAgent** 的 host 报的是什么 ──
	gotBare := axReadUA(t, hAgent, hPeer.ID())
	t.Logf("红侧：对端从**裸 host**（未设 UserAgent）收到的 agentVersion = %q", gotBare)

	// 判别力自证：两者 opts 只差一行，读到同一个值必有一真。★ 措辞要能区分两种成因 ——
	// 只说"观测通道没判别力"会把 (b) 误导成 (a)，让人去查 peerstore 而真因在 opts 里。
	if gotBare == gotProd {
		t.Fatalf("裸 host 与生产代码报出同一个 agentVersion %q ⇒ 二者必有一真：\n"+
			"  (a) 观测通道没把它们分辨开（peerstore 串了）；或\n"+
			"  (b) 生产代码那侧的 UserAgent 没生效（opts 里缺/被注释了 libp2p.UserAgent 行）——\n"+
			"      此时生产代码就在报 libp2p 的默认自曝值，正是本判据要防的事。\n"+
			"两种情况都让绿侧的 PASS 不成立。", gotBare)
	}
	if gotBare == axUAWant {
		t.Fatalf("裸 host 也报了中性值 %q ⇒ 观测对象搞错了，红侧物证不成立", gotBare)
	}

	// 判据①：生产代码必须报中性值。
	if gotProd != axUAWant {
		t.Errorf("生产代码报出的 agentVersion = %q，期望中性值 %q ⇒ 对端可据此指纹化本 agent", gotProd, axUAWant)
	}
	// 判据②：实测值里不得含本项目的自我标识（对**实际收到的串**判，比静态 grep 强）。
	low := strings.ToLower(gotProd)
	for _, bad := range axUABadSubs {
		if strings.Contains(low, bad) {
			t.Errorf("报出的 agentVersion %q 含自我标识 %q ⇒ 等于给对端发身份证", gotProd, bad)
		}
	}
	// 判据③：不得以 "@" 结尾 —— 默认值在非 git 构建下正是这个形状（`<module>@` + 空 revision）。
	if strings.HasSuffix(gotProd, "@") {
		t.Errorf("agentVersion %q 以 @ 结尾 ⇒ 是 libp2p 默认值的形状（模块名 + 空 revision）", gotProd)
	}
	// 判据④：`/` 或 `@` 之后不得紧跟数字（版本号）—— 中性值的全部意义就是
	//   不给对端留下"据此反查已知漏洞"的线索。
	for i := 0; i+1 < len(gotProd); i++ {
		if (gotProd[i] == '/' || gotProd[i] == '@') && gotProd[i+1] >= '0' && gotProd[i+1] <= '9' {
			t.Errorf("agentVersion %q 在 %q 后带版本号 ⇒ 对端可据此反查已知漏洞", gotProd, string(gotProd[i]))
		}
	}
}
