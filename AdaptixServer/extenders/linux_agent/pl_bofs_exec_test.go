package main

/* 批次21 验收测试：/api/bofs/exec 的**候选选择**判据。
 *
 * 这一批修的是「候选清单从哪来、按什么选」，两个缺陷：
 *
 *   A. 根目录是猜的（三个相对 ModuleDir 的路径）。换部署就猜不中 ⇒ 静默空清单。
 *      正解：问服务端 TsAxScriptDirs（就是 profile 里 axscripts 加载出来的目录）。
 *      本文件用 TestBofsExecRoots* 三条钉死：优先问、问不到才兜底、兜底必须自称兜底。
 *
 *   D. 候选按 a_os 选。但 BOF 能力是 **agent 的属性**，不是 OS 的 ——
 *      同一台 Linux 上 linux agent 有 BOF、gopher agent 没有。
 *      按 os=2 发 linux_agent 的 .o 给 Linux gopher，那条命令必然失败。
 *      本文件用 TestBofsExecAgentSelectsDialect 一条表驱动钉死全部分支。
 *
 * ★ 与代码同源：判据打的是 bofsHandleExec 本人，不在测试里另抄一份 agent→清单 表。
 * ★ 临时目录纪律沿用 pl_bofs_api_test.go：不用 t.TempDir()（见该文件头注释）。
 */

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// ---------------------------------------------------------------- 新脚手架

// bofsUseAxScriptDirs 替身服务端的 TsAxScriptDirs。
// 存成函数值而不是把 Ts 存下来再断言，就是为了让这里能直接换掉（见 pl_main.go 注释）。
func bofsUseAxScriptDirs(t *testing.T, dirs []string) {
	t.Helper()
	old := AxScriptDirs
	AxScriptDirs = func() []string { return dirs }
	t.Cleanup(func() { AxScriptDirs = old })
}

// bofsNoAxScriptDirs 模拟「老服务端没有 TsAxScriptDirs」——断言失败后 AxScriptDirs 保持 nil。
func bofsNoAxScriptDirs(t *testing.T) {
	t.Helper()
	old := AxScriptDirs
	AxScriptDirs = nil
	t.Cleanup(func() { AxScriptDirs = old })
}

func bofsCallExec(t *testing.T, body string) (int, map[string]any) {
	t.Helper()
	code, raw := bofsHandleExec("tester", []byte(body))
	return code, bofsDecode(t, raw)
}

// bofsExecLabels 取出候选的 label（= 弹层里显示的那一列）。
func bofsExecLabels(t *testing.T, resp map[string]any) []string {
	t.Helper()
	raw, ok := resp["entries"].([]any)
	if !ok {
		t.Fatalf("响应里 entries 不是数组：%#v", resp["entries"])
	}
	out := make([]string, 0, len(raw))
	for _, e := range raw {
		m, ok := e.(map[string]any)
		if !ok {
			t.Fatalf("entries 元素不是对象：%#v", e)
		}
		s, _ := m["label"].(string)
		out = append(out, s)
	}
	return out
}

func bofsHasLabel(labels []string, want string) bool {
	for _, l := range labels {
		if l == want {
			return true
		}
	}
	return false
}

// bofsSetMTime 显式设定 mtime。
// ★ 不靠「先写 .o 再写 .c 所以 .c 更新」—— 文件系统时间戳粒度不可依赖，
//   一次巧合的相等就会让 stale 判据红/绿地随机跳。这里把时间钉死。
func bofsSetMTime(t *testing.T, path string, ts time.Time) {
	t.Helper()
	if err := os.Chtimes(path, ts, ts); err != nil {
		t.Fatalf("chtimes %s: %v", path, err)
	}
}

// ---------------------------------------------------------------- D. 按 agent 选

// TestBofsExecAgentSelectsDialect —— 本批最核心的一条。
//
// 表里每一行都对应一个**实测过的** extender 行为（批次21 代码定位）：
//   beacon / kharon  的 execute 分支无 OS 守卫            ⇒ 任何 os 都给 Extension-Kit
//   gopher           只在 Os==WINDOWS 下处理 execute       ⇒ 非 Windows 给 0 条 + 说明
//   linux_agent      运行期编译自己的 .o                   ⇒ 给 bofs/ 下那份
//   macos_agent      整个 switch 里没有 execute 分支        ⇒ 0 条 + 说明
func TestBofsExecAgentSelectsDialect(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)

	// linux_agent 自己的产物
	bofsMustWrite(t, filepath.Join(dir, "bofs", "mybof.o"), []byte("OBJ"))
	// Extension-Kit 形态的一棵根：<根>/<分类>-BOF/_bin/<名字>.<arch>.o
	kit := filepath.Join(dir, "Extension-Kit")
	bofsMustWrite(t, filepath.Join(kit, "SAL-BOF", "_bin", "sc_enum.x64.o"), []byte("COFF"))
	bofsUseAxScriptDirs(t, []string{kit})

	cases := []struct {
		agent    string
		os       int
		want     string // 期望出现且**只出现**这一条
		wantNote string // 非空 ⇒ note 必须包含该子串
		// wantCapable = 服务端回答的「这个 agent 有没有 BOF 加载器」。
		// ★ 它与「有几条候选」是**两个问题**，所以和 want 分开断言：
		//   linux 这里恰好有 1 条产物，但即使一条都没有，capable 也必须是 true
		//   —— 否则新部署（只有 .c 没 .o）会把操作员挡在**用来编译**的页面外面。
		//   下面 TestBofsExecCapableIndependentOfEntries 专门钉这一点。
		wantCapable bool
	}{
		{"linux", 2, "mybof.o", "", true},
		{"beacon", 1, "sc_enum.x64.o", "", true},
		{"kharon", 1, "sc_enum.x64.o", "", true},
		{"gopher", 1, "sc_enum.x64.o", "", true},
		// ★ 核心判据。修前这里是 26 条 linux_agent 的 .o —— 那台主机一条也用不了。
		{"gopher", 2, "", "只在 Windows 上有 BOF 加载器", false},
		{"gopher", 3, "", "只在 Windows 上有 BOF 加载器", false},
		{"macos", 3, "", "没有 execute 分支", false},
		{"never_seen_before", 1, "", "未知 agent", false},
	}

	for _, c := range cases {
		body := `{"agent":"` + c.agent + `","os":` + itoa(c.os) + `,"arch":"x64"}`
		code, resp := bofsCallExec(t, body)
		if code != 200 {
			t.Errorf("[%s/os=%d] HTTP %d，期望 200；resp=%v", c.agent, c.os, code, resp)
			continue
		}
		labels := bofsExecLabels(t, resp)

		if c.want == "" {
			if len(labels) != 0 {
				t.Errorf("[%s/os=%d] 期望 0 条候选，实际 %d 条：%v", c.agent, c.os, len(labels), labels)
			}
		} else {
			if len(labels) != 1 || labels[0] != c.want {
				t.Errorf("[%s/os=%d] 期望恰好 [%s]，实际 %v", c.agent, c.os, c.want, labels)
			}
		}

		note, _ := resp["note"].(string)
		if c.wantNote != "" && !strings.Contains(note, c.wantNote) {
			t.Errorf("[%s/os=%d] ★ 不支持时必须给出说明而不是静默空；期望 note 含 %q，实际 %q",
				c.agent, c.os, c.wantNote, note)
		}
		if c.wantNote == "" && note != "" {
			t.Errorf("[%s/os=%d] 有候选时不该同时给 note，实际 %q", c.agent, c.os, note)
		}

		// capable 必须是**明确的布尔值**，不能缺字段：前端拿不到就只能当 false，
		// 于是「服务端忘了发」与「这台确实不支持」在界面上同形（BOF 入口消失）。
		got, ok := resp["capable"].(bool)
		if !ok {
			t.Errorf("[%s/os=%d] ★ 响应缺少布尔 capable 字段（前端无法区分「没发」与「不支持」）：%v",
				c.agent, c.os, resp["capable"])
		} else if got != c.wantCapable {
			t.Errorf("[%s/os=%d] capable=%v，应为 %v", c.agent, c.os, got, c.wantCapable)
		}
	}
}

// TestBofsExecCapableIndependentOfEntries —— **能力**与**产物**必须分开回答。
//
// 这是本批最容易写错、且错了最难查的一处：全新部署时 linux_agent 只有 .c
// 没有 .o（本项目已归档：`n_obj=0`），候选是 0 条。若前端用「候选为空 ⇒ 不支持」
// 去隐藏 BOF 入口，操作员就再也进不去那个**用来编译**的页面 —— 一个自己把自己
// 锁在门外的死循环，而且界面上什么都不报。
func TestBofsExecCapableIndependentOfEntries(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	// 故意**什么都不放**：bofs/ 下既没有 .c 也没有 .o。
	if err := os.MkdirAll(filepath.Join(dir, "bofs"), 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	bofsUseAxScriptDirs(t, nil)

	code, resp := bofsCallExec(t, `{"agent":"linux","os":2,"arch":"x64"}`)
	if code != 200 {
		t.Fatalf("HTTP %d，期望 200", code)
	}
	if n := len(bofsExecLabels(t, resp)); n != 0 {
		t.Fatalf("本用例前提是「没有产物」，实际却有 %d 条候选", n)
	}
	if cap, ok := resp["capable"].(bool); !ok || !cap {
		t.Errorf("★ 零产物时 capable 仍须为 true（能力与产物是两个问题）；实际 %v", resp["capable"])
	}
	// 并且必须解释「为什么是空的」，否则前端只能显示一个空的弹层。
	if note, _ := resp["note"].(string); !strings.Contains(note, "没有 .o 产物") {
		t.Errorf("零产物时必须给出说明，实际 note=%q", note)
	}
}

// TestBofsExecLinuxCandidateIsAbsoluteAndUsable —— 候选的 name 必须是**能用**的整串。
// 这正是原始 bug 的另一半：旧补全给裸文件名，agent 侧解析不出路径 ⇒ 静默无任务。
func TestBofsExecLinuxCandidateIsAbsoluteAndUsable(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustWrite(t, filepath.Join(dir, "bofs", "mybof.o"), []byte("OBJ"))
	bofsNoAxScriptDirs(t)

	_, resp := bofsCallExec(t, `{"agent":"linux","os":2}`)
	raw, _ := resp["entries"].([]any)
	if len(raw) != 1 {
		t.Fatalf("期望 1 条候选，实际 %d", len(raw))
	}
	m := raw[0].(map[string]any)
	name, _ := m["name"].(string)
	label, _ := m["label"].(string)

	if !filepath.IsAbs(name) {
		t.Errorf("★ name 必须是绝对路径（相对路径会随 cwd 漂移，失败后静默落进 base64 分支）：%q", name)
	}
	if _, err := os.Stat(name); err != nil {
		t.Errorf("★ name 必须指向真实存在的文件：%q → %v", name, err)
	}
	if strings.Contains(label, string(filepath.Separator)) {
		t.Errorf("label 是给人看的 basename，不该含路径分隔符：%q", label)
	}
}

// ---------------------------------------------------------------- C. 陈旧只标注

// TestBofsExecLinuxStaleSharesScanRecipe —— 「同一事实不许有两个答案」。
//
// exec 候选的 Stale 必须与 bofsScan 逐项相等。若哪天有人在 exec 侧另写一份
// mtime 比较，这条会红 —— 那时两个界面就会对「这个 BOF 要不要重编」给出不同结论。
func TestBofsExecLinuxStaleSharesScanRecipe(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsNoAxScriptDirs(t)

	base := time.Now().Add(-time.Hour)

	// fresh：产物比源码新 ⇒ 不 stale
	bofsMustWrite(t, filepath.Join(dir, "bofs", "fresh.o"), []byte("NEW"))
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "fresh.c"), []byte("SRC"))
	bofsSetMTime(t, filepath.Join(dir, "src_agent", "bofs", "fresh.c"), base)
	bofsSetMTime(t, filepath.Join(dir, "bofs", "fresh.o"), base.Add(time.Minute))

	// old：源码比产物新 ⇒ stale
	bofsMustWrite(t, filepath.Join(dir, "bofs", "old.o"), []byte("OLD"))
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "old.c"), []byte("SRC"))
	bofsSetMTime(t, filepath.Join(dir, "bofs", "old.o"), base)
	bofsSetMTime(t, filepath.Join(dir, "src_agent", "bofs", "old.c"), base.Add(time.Minute))

	// orphan：只有源码没有产物 ⇒ bofsScan 判 stale，但**不进候选**（不可执行）
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "orphan.c"), []byte("SRC"))
	// bare：只有产物没有源码 ⇒ 不 stale，进候选
	bofsMustWrite(t, filepath.Join(dir, "bofs", "bare.o"), []byte("OBJ"))

	scanned, _ := bofsScan()
	scanStale := map[string]bool{}
	for _, e := range scanned {
		scanStale[e.Name] = e.Stale
	}

	_, resp := bofsCallExec(t, `{"agent":"linux","os":2}`)
	labels := bofsExecLabels(t, resp)

	if bofsHasLabel(labels, "orphan.o") {
		t.Errorf("★ 只有源码没有产物的模块不可执行，不该进候选；实际 %v", labels)
	}
	if !bofsHasLabel(labels, "bare.o") {
		t.Errorf("只有产物没有源码是正常形态（Extension-Kit 移过来的），应进候选；实际 %v", labels)
	}

	raw, _ := resp["entries"].([]any)
	for _, e := range raw {
		m := e.(map[string]any)
		label, _ := m["label"].(string)
		gotStale, _ := m["stale"].(bool)
		name := strings.TrimSuffix(label, ".o")
		if want, ok := scanStale[name]; ok && gotStale != want {
			t.Errorf("★ %s：exec 判 stale=%v，bofsScan 判 %v —— 同一事实出现两个答案",
				name, gotStale, want)
		}
	}

	// 正向确认判据有区分度：old 必须 stale、fresh 必须不 stale。
	// 若两者同值，说明 mtime 没钉住，上面那条一致性断言就是恒真的。
	byLabel := map[string]map[string]any{}
	for _, e := range raw {
		m := e.(map[string]any)
		byLabel[m["label"].(string)] = m
	}
	if m, ok := byLabel["old.o"]; !ok || m["stale"] != true {
		t.Errorf("★ old.o（源码比产物新）必须标 stale，实际 %v（若为 false 说明 mtime 未生效）", m)
	}
	if m, ok := byLabel["fresh.o"]; !ok || m["stale"] == true {
		t.Errorf("★ fresh.o（产物比源码新）不该标 stale，实际 %v", m)
	}
}

// TestBofsExecStaleIsAnnotatedNotHidden —— 用户取舍：陈旧只标注，仍可选。
func TestBofsExecStaleIsAnnotatedNotHidden(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsNoAxScriptDirs(t)

	base := time.Now().Add(-time.Hour)
	bofsMustWrite(t, filepath.Join(dir, "bofs", "staleone.o"), []byte("OLD"))
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "staleone.c"), []byte("SRC"))
	bofsSetMTime(t, filepath.Join(dir, "bofs", "staleone.o"), base)
	bofsSetMTime(t, filepath.Join(dir, "src_agent", "bofs", "staleone.c"), base.Add(time.Minute))

	_, resp := bofsCallExec(t, `{"agent":"linux","os":2}`)
	raw, _ := resp["entries"].([]any)
	if len(raw) != 1 {
		t.Fatalf("★ 陈旧的 BOF 仍然必须可选（用户取舍：只标注不隐藏），实际候选 %d 条", len(raw))
	}
	m := raw[0].(map[string]any)
	if m["stale"] != true {
		t.Errorf("应标 stale=true，实际 %v", m["stale"])
	}
	reason, _ := m["stale_reason"].(string)
	if strings.TrimSpace(reason) == "" {
		t.Errorf("★ 标了就要说清为什么 —— stale_reason 为空等于只给了个红点")
	}
	if _, ok := m["mtime"]; !ok {
		t.Errorf("候选应带 mtime，便于人工判断新旧")
	}
}

// ---------------------------------------------------------------- A. 根目录动态

// TestBofsExecRootsPrefersServerReportedDirs
func TestBofsExecRootsPrefersServerReportedDirs(t *testing.T) {
	root := bofsTestRoot(t)
	bofsUseModuleDir(t, filepath.Join(root, "dist", "extenders", "linux_agent"))

	// 故意造一棵**兜底也能命中**的 Extension-Kit。
	// 若实现退回猜测，roots 的 Source 会是 fallback ⇒ 这条就能抓到「假装在问服务端」。
	fallbackKit := filepath.Join(root, "dist", "Extension-Kit")
	bofsMustMkdir(t, fallbackKit)

	real := filepath.Join(root, "elsewhere", "my-kit")
	bofsMustMkdir(t, real)

	bofsUseAxScriptDirs(t, []string{real})

	roots, _ := bofsExecRoots()
	if len(roots) != 1 {
		t.Fatalf("期望 1 个根，实际 %d：%v", len(roots), roots)
	}
	if roots[0].Source != "axscripts" {
		t.Errorf("★ 服务端能报出来时不许退回猜测；Source=%q", roots[0].Source)
	}
	if roots[0].Dir != real {
		t.Errorf("根=%q，期望服务端报的 %q", roots[0].Dir, real)
	}
}

// TestBofsFoldDirs —— 折叠规则本体（纯函数，直接打）。
//
// ★ 本用例里最重要的是 kit 与 kit2 那段：它们只共享前缀、**不是**父子。
//   朴素的 strings.HasPrefix("kit2", "kit") 为真，会把一棵真实的根整个折掉 ——
//   而「根凭空少了一棵」在界面上表现为「这些 BOF 怎么不见了」，极难归因。
func TestBofsFoldDirs(t *testing.T) {
	sep := string(filepath.Separator)

	cases := []struct {
		name string
		in   []string
		want []string
	}{
		{"空输入", nil, []string{}},
		{"去重", []string{"/a", "/a", "/b"}, []string{"/a", "/b"}},
		{"丢掉空与点", []string{"", ".", "/a"}, []string{"/a"}},
		{"折叠子目录", []string{"/a/b", "/a"}, []string{"/a"}},
		{"折叠深层子孙", []string{"/a/b/c/d", "/a"}, []string{"/a"}},
		{
			// ★ 只共享前缀的兄弟不能被折
			"前缀相同但非父子",
			[]string{"/x/kit", "/x/kit2"},
			[]string{"/x/kit", "/x/kit2"},
		},
		{
			"两棵独立的根都保留",
			[]string{"/home/ad/dist/Extension-Kit", "/opt/other/kit"},
			[]string{"/home/ad/dist/Extension-Kit", "/opt/other/kit"},
		},
	}

	for _, c := range cases {
		got := bofsFoldDirs(c.in)
		if len(got) != len(c.want) {
			t.Errorf("[%s] 折叠后 %v，期望 %v", c.name, got, c.want)
			continue
		}
		for i := range got {
			if got[i] != c.want[i] {
				t.Errorf("[%s] 第 %d 项 %q，期望 %q（整体 %v）", c.name, i, got[i], c.want[i], got)
				break
			}
		}
	}

	// 兜底确认分隔符比较真的生效（否则上面那条会因平台差异变成恒真）
	if !strings.HasPrefix("/x/kit"+sep+"sub", "/x/kit"+sep) {
		t.Fatalf("分隔符假设不成立，本用例无区分度")
	}
}

// TestBofsExecRootsFoldsNestedAndDedupes —— 折叠加去重必须落到真实的根解析上，
// 而不只是纯函数里成立。
func TestBofsExecRootsFoldsNestedAndDedupes(t *testing.T) {
	root := bofsTestRoot(t)
	bofsUseModuleDir(t, root)

	kit := filepath.Join(root, "kit")
	nested := filepath.Join(kit, "SAL-BOF")
	sibling := filepath.Join(root, "kit2")
	for _, d := range []string{kit, nested, sibling} {
		bofsMustMkdir(t, d)
	}

	bofsUseAxScriptDirs(t, []string{nested, kit, kit, sibling})

	roots, _ := bofsExecRoots()
	var got []string
	for _, r := range roots {
		got = append(got, r.Dir)
	}

	if len(got) != 2 {
		t.Fatalf("期望折叠后剩 2 个根（%s 与 %s），实际 %d：%v", kit, sibling, len(got), got)
	}
	joined := strings.Join(got, "|")
	if !strings.Contains(joined, kit+"|") && !strings.HasSuffix(joined, kit) {
		t.Errorf("父目录 %s 应保留，实际 %v", kit, got)
	}
	if strings.Contains(joined, nested) {
		t.Errorf("★ 子目录 %s 被父目录覆盖，应折叠掉，实际 %v", nested, got)
	}
	if !strings.Contains(joined, sibling) {
		t.Errorf("★ 只共享前缀的兄弟目录 %s 不是子目录，不该被折叠；实际 %v", sibling, got)
	}
}

// TestBofsExecRootsFallbackKeepsTellingTheTruth —— 老服务端降级路径。
//
// 降级本身可以接受（总比整个功能失效强），**静默**降级不行：
// Source 必须自称 fallback，调用方才知道这份清单是猜的。
func TestBofsExecRootsFallbackKeepsTellingTheTruth(t *testing.T) {
	root := bofsTestRoot(t)
	modDir := filepath.Join(root, "dist", "extenders", "linux_agent")
	bofsMustMkdir(t, modDir)
	bofsUseModuleDir(t, modDir)

	kit := filepath.Join(root, "dist", "Extension-Kit")
	bofsMustWrite(t, filepath.Join(kit, "SAL-BOF", "_bin", "a.x64.o"), []byte("COFF"))

	bofsNoAxScriptDirs(t)

	roots, tried := bofsExecRoots()
	if len(roots) != 1 {
		t.Fatalf("兜底应命中 1 个根，实际 %d（试过：%v）", len(roots), tried)
	}
	if roots[0].Source != "fallback" {
		t.Errorf("★ 兜底命中的根必须标 source=fallback，否则降级是静默的；实际 %q", roots[0].Source)
	}
	if roots[0].Dir != kit {
		t.Errorf("兜底根=%q，期望 %q", roots[0].Dir, kit)
	}
	// 降级时要把「你在猜」这件事说出去，而不是列一堆路径了事
	if !strings.Contains(strings.Join(tried, "|"), "不支持") {
		t.Errorf("★ 降级原因必须出现在 tried 里，供排障；实际 %v", tried)
	}

	// 端到端确认兜底真的能用（不只是 roots 非空）
	_, resp := bofsCallExec(t, `{"agent":"beacon","os":1,"arch":"x64"}`)
	labels := bofsExecLabels(t, resp)
	if !bofsHasLabel(labels, "a.x64.o") {
		t.Errorf("兜底路径应仍能列出候选，实际 %v（note=%v）", labels, resp["note"])
	}
	raw, _ := resp["entries"].([]any)
	if len(raw) != 1 {
		t.Fatalf("期望 1 条，实际 %d", len(raw))
	}
	name, _ := raw[0].(map[string]any)["name"].(string)
	if !filepath.IsAbs(name) {
		t.Errorf("兜底路径下 name 仍须是绝对路径，实际 %q", name)
	}
}

// TestBofsExecRootsSurvivesUnreadableDir —— 服务端报了个当前不可读的目录时，
// 不能崩、也不能假装没事：要说出来。
func TestBofsExecRootsSurvivesUnreadableDir(t *testing.T) {
	root := bofsTestRoot(t)
	bofsUseModuleDir(t, root)

	missing := filepath.Join(root, "gone")
	bofsUseAxScriptDirs(t, []string{missing})

	roots, tried := bofsExecRoots()
	// 没有兜底可命中 ⇒ 空，但必须解释为什么
	if len(roots) != 0 {
		t.Fatalf("目录不存在时不该产出根，实际 %v", roots)
	}
	_ = tried

	code, resp := bofsCallExec(t, `{"agent":"beacon","os":1}`)
	if code != 200 {
		t.Errorf("根目录不可用不该是 500（这是部署配置问题，不是请求错误）：HTTP %d", code)
	}
	note, _ := resp["note"].(string)
	if strings.TrimSpace(note) == "" {
		t.Errorf("★ 根目录不可用时必须给出说明，实际静默空清单：%v", resp)
	}
	if !strings.Contains(note, "axscripts") {
		t.Errorf("说明里要点出动态根的出处 axscripts，否则用户不知道去哪改；实际 %q", note)
	}
}

// ---------------------------------------------------------------- 兼容与形状

// TestBofsExecDeprecatedOsOnlyPath —— 老前端只发 os。
// 仍然要能工作，但必须把「这是推断的」标出来 —— 静默接受一个已知不精确的判据，
// 正是当初「按 os 发错清单」的成因。
func TestBofsExecDeprecatedOsOnlyPath(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	kit := filepath.Join(dir, "Extension-Kit")
	bofsMustWrite(t, filepath.Join(kit, "SAL-BOF", "_bin", "a.x64.o"), []byte("COFF"))
	bofsUseAxScriptDirs(t, []string{kit})

	code, resp := bofsCallExec(t, `{"os":1}`)
	if code != 200 {
		t.Fatalf("老前端路径应仍可用，HTTP %d", code)
	}
	if _, ok := resp["deprecated"]; !ok {
		t.Errorf("★ 缺 agent 字段时必须标 deprecated，不能静默按 os 推断")
	}
	if !bofsHasLabel(bofsExecLabels(t, resp), "a.x64.o") {
		t.Errorf("老前端路径应仍能列出候选，实际 %v", bofsExecLabels(t, resp))
	}

	// os 非法且无 agent ⇒ 400（无从推断）
	if code, _ := bofsCallExec(t, `{"os":9}`); code != 400 {
		t.Errorf("非法 os 且无 agent 应回 400，实际 %d", code)
	}
	// body 不是 JSON ⇒ 400
	if code, _ := bofsCallExec(t, `{bad json`); code != 400 {
		t.Errorf("非法 body 应回 400，实际 %d", code)
	}
}

// TestBofsExecResponseShape —— 排障字段必须在。
// roots 是本批新增的关键字段：换部署后清单为空时，第一步就看它。
func TestBofsExecResponseShape(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	kit := filepath.Join(dir, "Extension-Kit")
	bofsMustWrite(t, filepath.Join(kit, "SAL-BOF", "_bin", "a.x64.o"), []byte("COFF"))
	bofsUseAxScriptDirs(t, []string{kit})

	_, resp := bofsCallExec(t, `{"agent":"beacon","os":1,"arch":"x64"}`)

	if resp["ok"] != true {
		t.Errorf("ok 应为 true，实际 %v", resp["ok"])
	}
	if resp["agent"] != "beacon" {
		t.Errorf("应回显 agent，实际 %v", resp["agent"])
	}
	if resp["count"] != float64(1) {
		t.Errorf("count 应为 1，实际 %v", resp["count"])
	}
	roots, ok := resp["roots"].([]any)
	if !ok || len(roots) == 0 {
		t.Fatalf("★ 响应必须带 roots（实际用的根 + 来源），否则换部署后的静默失效无从排起：%v", resp["roots"])
	}
	r0 := roots[0].(map[string]any)
	if r0["dir"] != kit || r0["source"] != "axscripts" {
		t.Errorf("roots[0] 应为 {%s axscripts}，实际 %v", kit, r0)
	}
}

// TestBofsNormAgentMatchesFrontend —— 前后端归一必须同源。
// 前端 normType()（completions.ts:103）小写化并去掉 _agent 后缀，这里也一样。
// 若两边漂移，"linux_agent" 与 "linux" 会被拆成两个分支各走各的。
func TestBofsNormAgentMatchesFrontend(t *testing.T) {
	cases := map[string]string{
		"linux":         "linux",
		"Linux":         "linux",
		"linux_agent":   "linux",
		"Linux_Agent":   "linux",
		"  kharon  ":    "kharon",
		"beacon_agent":  "beacon",
		"gopher":        "gopher",
		"macos":         "macos",
		"":              "",
		"_agent":        "",
		"a_agent_agent": "a_agent", // 只剥一层，不递归
	}
	for in, want := range cases {
		if got := bofsNormAgent(in); got != want {
			t.Errorf("bofsNormAgent(%q)=%q，期望 %q", in, got, want)
		}
	}
}

// itoa 只用于拼测试请求体，避免为一个数字引入 fmt。
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var b []byte
	for n > 0 {
		b = append([]byte{byte('0' + n%10)}, b...)
		n /= 10
	}
	if neg {
		return "-" + string(b)
	}
	return string(b)
}
