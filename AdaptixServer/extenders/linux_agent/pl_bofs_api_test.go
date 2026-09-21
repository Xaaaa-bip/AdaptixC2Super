package main

/* 批次20 验收测试：/api/bofs/{list,upload,compile} 的行为判据。
 *
 * ★ 与代码同源：这些判据打的是 pl_bofs_api.go 里的**同一份实现**，
 *   不在测试里另抄一套逻辑（抄出来的第二份迟早漂移，而漂移的表现
 *   恰好就是本项目反复归档的「UI 填了但没生效」族）。
 *
 * ★★ 本文件里最重要的一条是 TestBofsCompileMatchesShippedArtifact：
 *    它把运行树的 bofs/ 与 src_agent/ 复制到临时目录，用 bofsCompileOne
 *    重编一个**现役** BOF，断言 Changed == false。
 *    Changed 的定义是「与现有 .o 比字节」，所以这条断言等价于：
 *        「本接口的 gcc 配方与生成现役产物所用的配方逐字节同源」。
 *    配方一旦漂移（少个 -fPIC、换个 -O 级别、-I 指错目录），这条立刻红。
 *    这正是本项目铁律「批量重编脚本必须与被验证配方同源」的**可执行形式**。
 *
 * ★ 临时目录纪律：**绝不使用 t.TempDir()**。它落在 $TMPDIR（默认 /tmp），
 *   而本项目 p17 T6 已归档「任何文件进 /tmp 会误触红判据」。
 *   本文件一律在 AX_BOFS_TESTDIR（默认 /root/gp5probe/tmp/boftest）下建目录。
 */

import (
	"encoding/base64"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// ---------------------------------------------------------------- 测试脚手架

func bofsTestRoot(t *testing.T) string {
	t.Helper()
	base := os.Getenv("AX_BOFS_TESTDIR")
	if base == "" {
		base = "/root/gp5probe/tmp/boftest"
	}
	if err := os.MkdirAll(base, 0o755); err != nil {
		t.Fatalf("创建测试根失败（%s）：%v", base, err)
	}
	dir, err := os.MkdirTemp(base, "case-")
	if err != nil {
		t.Fatalf("创建用例目录失败：%v", err)
	}
	t.Cleanup(func() { _ = os.RemoveAll(dir) })
	return dir
}

func bofsUseModuleDir(t *testing.T, dir string) {
	t.Helper()
	old := ModuleDir
	ModuleDir = dir
	t.Cleanup(func() { ModuleDir = old })
}

func bofsMustMkdir(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatalf("mkdir %s: %v", dir, err)
	}
}

func bofsMustWrite(t *testing.T, path string, data []byte) {
	t.Helper()
	bofsMustMkdir(t, filepath.Dir(path))
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// bofsDotFiles 列出 dir 下的点文件 —— 用于断言「原子落盘不留残渣」。
func bofsDotFiles(t *testing.T, dir string) []string {
	t.Helper()
	des, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var out []string
	for _, de := range des {
		if strings.HasPrefix(de.Name(), ".") {
			out = append(out, de.Name())
		}
	}
	return out
}

func bofsDecode(t *testing.T, b []byte) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatalf("响应不是 JSON：%v\n原文：%s", err, string(b))
	}
	return m
}

// ---------------------------------------------------------------- 名字白名单

func TestBofsNameWhitelist(t *testing.T) {
	good := []string{"host_recon", "a", "A1", "x-y_z", strings.Repeat("a", 64)}
	for _, n := range good {
		if !bofsNameRe.MatchString(n) {
			t.Errorf("合法名被拒：%q", n)
		}
	}
	bad := []string{
		"", "..", "../etc/passwd", "a/b", "/abs", "-x", "--help",
		"a b", "a\tb", "a.o", "a;id", "a$(id)", "a`id`", "a'b", `a"b`,
		"a|b", "a&b", "a\nb", strings.Repeat("a", 65), "名字", "a*b",
	}
	for _, n := range bad {
		if bofsNameRe.MatchString(n) {
			t.Errorf("★ 非法名被放行：%q —— 该名字会进入 filepath.Join 与 gcc argv", n)
		}
	}
}

// ---------------------------------------------------------------- ELF 类型

func TestBofsELFType(t *testing.T) {
	mk := func(etype int) []byte {
		b := make([]byte, 64)
		copy(b, []byte{0x7f, 'E', 'L', 'F'})
		b[16] = byte(etype)
		b[17] = byte(etype >> 8)
		return b
	}
	cases := []struct {
		name string
		data []byte
		want int
	}{
		{"ET_REL(目标文件)", mk(1), 1},
		{"ET_EXEC", mk(2), 2},
		{"ET_DYN(共享库)", mk(3), 3},
		{"非 ELF", []byte("#!/bin/sh\n"), -1},
		{"过短", []byte{0x7f, 'E'}, -1},
		{"空", nil, -1},
	}
	for _, c := range cases {
		if got := bofsELFType(c.data); got != c.want {
			t.Errorf("%s: bofsELFType=%d，期望 %d", c.name, got, c.want)
		}
	}
}

// ---------------------------------------------------------------- 原子落盘

func TestBofsAtomicWriteNoResidue(t *testing.T) {
	dir := bofsTestRoot(t)
	dst := filepath.Join(dir, "x.o")
	for i, payload := range [][]byte{[]byte("first"), []byte("second-longer")} {
		if err := bofsAtomicWrite(dst, payload, 0o644); err != nil {
			t.Fatalf("第 %d 次写入失败：%v", i+1, err)
		}
		got, err := os.ReadFile(dst)
		if err != nil {
			t.Fatalf("读回失败：%v", err)
		}
		if string(got) != string(payload) {
			t.Fatalf("第 %d 次内容不符：%q", i+1, got)
		}
		if d := bofsDotFiles(t, dir); len(d) != 0 {
			t.Fatalf("★ 原子落盘留下残渣：%v", d)
		}
	}
}

// ---------------------------------------------------------------- 清单

func TestBofsScanClassification(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustWrite(t, filepath.Join(dir, "bofs", "alpha.o"), []byte("OBJ-A"))
	bofsMustWrite(t, filepath.Join(dir, "bofs", "beta.o"), []byte("OBJ-B"))
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "alpha.c"), []byte("SRC-A"))
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "gamma.c"), []byte("SRC-G"))
	// 干扰项：非模块文件、点文件、名字不合规的文件
	bofsMustWrite(t, filepath.Join(dir, "bofs", "README.txt"), []byte("x"))
	bofsMustWrite(t, filepath.Join(dir, "bofs", ".alpha.o.tmp123"), []byte("x"))
	bofsMustWrite(t, filepath.Join(dir, "bofs", "bad name.o"), []byte("x"))

	entries, notes := bofsScan()
	byName := map[string]bofEntry{}
	for _, e := range entries {
		byName[e.Name] = e
	}

	if len(entries) != 3 {
		names := []string{}
		for _, e := range entries {
			names = append(names, e.Name)
		}
		t.Fatalf("模块数=%d，期望 3（alpha/beta/gamma）；实际 %v", len(entries), names)
	}
	if e := byName["alpha"]; !e.Obj || !e.Src {
		t.Errorf("alpha 应同时有 obj 与 src，实际 obj=%v src=%v", e.Obj, e.Src)
	}
	if e := byName["beta"]; !e.Obj || e.Src {
		t.Errorf("beta 应只有 obj，实际 obj=%v src=%v", e.Obj, e.Src)
	}
	if e := byName["gamma"]; e.Obj || !e.Src {
		t.Errorf("gamma 应只有 src，实际 obj=%v src=%v", e.Obj, e.Src)
	}
	// beta/gamma 缺对侧 ⇒ 不算 stale（stale 的定义是「有源且产物旧/缺」）
	if byName["beta"].Stale {
		t.Errorf("beta 无源码，不应判 stale")
	}
	if !byName["gamma"].Stale {
		t.Errorf("gamma 有源码但无产物，应判 stale")
	}

	joined := strings.Join(notes, " | ")
	if !strings.Contains(joined, "README.txt") {
		t.Errorf("★ 非模块文件必须被显式列出而非静默吞掉；notes=%v", notes)
	}
	if !strings.Contains(joined, "bad name.o") {
		t.Errorf("★ 名字不合规的文件必须被显式列出；notes=%v", notes)
	}
	if strings.Contains(joined, ".alpha.o.tmp123") {
		t.Errorf("点文件是原子落盘的中间态，不应出现在 notes；notes=%v", notes)
	}
}

// ---------------------------------------------------------------- 上传守卫

func TestBofsUploadGuards(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustMkdir(t, filepath.Join(dir, "bofs"))
	bofsMustMkdir(t, filepath.Join(dir, "src_agent", "bofs"))

	rel := make([]byte, 64)
	copy(rel, []byte{0x7f, 'E', 'L', 'F'})
	rel[16] = 1 // ET_REL

	post := func(body string) (int, map[string]any) {
		code, raw := bofsHandleUpload("tester", []byte(body))
		return code, bofsDecode(t, raw)
	}

	// 1) 非法名
	if code, m := post(`{"name":"../evil","kind":"o","content_b64":"AAAA"}`); code != 400 {
		t.Errorf("非法名应 400，得 %d（%v）", code, m)
	}
	// 2) kind 非法
	if code, _ := post(`{"name":"ok","kind":"x","content_b64":"AAAA"}`); code != 400 {
		t.Errorf("非法 kind 应 400，得 %d", code)
	}
	// 3) 非 base64
	if code, _ := post(`{"name":"ok","kind":"o","content_b64":"!!!not base64!!!"}`); code != 400 {
		t.Errorf("非 base64 应 400，得 %d", code)
	}
	// 4) 空内容
	if code, _ := post(`{"name":"ok","kind":"o","content_b64":""}`); code != 400 {
		t.Errorf("空内容应 400，得 %d", code)
	}
	// 5) kind=o 但不是 ELF
	notELF := base64.StdEncoding.EncodeToString([]byte("#!/bin/sh\necho hi\n"))
	if code, m := post(`{"name":"ok","kind":"o","content_b64":"` + notELF + `"}`); code != 400 {
		t.Errorf("非 ELF 应 400，得 %d（%v）", code, m)
	}
	// 6) kind=o 是 ET_DYN（不是可重定位）⇒ 必须拒，且错误里要说清类型
	dyn := make([]byte, 64)
	copy(dyn, []byte{0x7f, 'E', 'L', 'F'})
	dyn[16] = 3
	b64dyn := base64.StdEncoding.EncodeToString(dyn)
	if code, m := post(`{"name":"ok","kind":"o","content_b64":"` + b64dyn + `"}`); code != 400 {
		t.Errorf("ET_DYN 应 400，得 %d（%v）", code, m)
	} else if !strings.Contains(m["error"].(string), "ET_REL") {
		t.Errorf("ET_DYN 的错误文案应点明 ET_REL 要求，实际：%v", m["error"])
	}
	// 7) 正常落 .o
	b64rel := base64.StdEncoding.EncodeToString(rel)
	code, m := post(`{"name":"good","kind":"o","content_b64":"` + b64rel + `"}`)
	if code != 200 || m["ok"] != true {
		t.Fatalf("正常上传应 200/ok，得 %d %v", code, m)
	}
	if _, err := os.Stat(filepath.Join(dir, "bofs", "good.o")); err != nil {
		t.Fatalf("产物未落盘：%v", err)
	}
	if d := bofsDotFiles(t, filepath.Join(dir, "bofs")); len(d) != 0 {
		t.Fatalf("★ 上传留下临时残渣：%v", d)
	}
	// 8) 重名必须要求显式 overwrite（本项目无 git，静默覆盖不可接受）
	if code, m := post(`{"name":"good","kind":"o","content_b64":"` + b64rel + `"}`); code != 409 {
		t.Errorf("重名应 409，得 %d（%v）", code, m)
	}
	// 9) 带 overwrite 则放行
	if code, _ := post(`{"name":"good","kind":"o","content_b64":"` + b64rel + `","overwrite":true}`); code != 200 {
		t.Errorf("overwrite=true 应 200，得 %d", code)
	}
	// 10) 超大内容（.c 上限 1MiB）⇒ 413
	huge := base64.StdEncoding.EncodeToString(make([]byte, bofsMaxSrcBytes+1))
	if code, _ := post(`{"name":"huge","kind":"c","content_b64":"` + huge + `"}`); code != 413 {
		t.Errorf("超限应 413，得 %d", code)
	}
}

// ---------------------------------------------------------------- list 形状

func TestBofsListShape(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustWrite(t, filepath.Join(dir, "bofs", "m1.o"), []byte("OBJ"))

	code, raw := bofsHandleList("tester", nil)
	if code != 200 {
		t.Fatalf("list 应 200，得 %d", code)
	}
	m := bofsDecode(t, raw)
	if m["ok"] != true {
		t.Fatalf("ok != true：%v", m)
	}
	base, _ := m["base"].(string)
	if !strings.HasSuffix(base, "/") {
		t.Errorf("base 必须以 / 结尾（前端按此拼接）：%q", base)
	}
	if want := filepath.Join(dir, "bofs") + "/"; base != want {
		t.Errorf("base=%q，期望 %q", base, want)
	}
	entries, _ := m["entries"].([]any)
	if len(entries) != 1 {
		t.Fatalf("entries 数=%d，期望 1", len(entries))
	}
	if cnt, _ := m["count"].(float64); int(cnt) != len(entries) {
		t.Errorf("count=%v 与 entries 数 %d 不一致", m["count"], len(entries))
	}
	if m["user"] != "tester" {
		t.Errorf("user 应回显登录名，得 %v", m["user"])
	}
}

// TestBofsErrEnvelopeCarriesMessage —— 前端契约判据（不是后端自娱自乐）。
//
// AdaptixWeb/src/api/http.ts:92 在 `data.ok === false` 时抛的是 **data.message**，
// 没有 message 就退化成 `'request failed'`。所以「错误信封里必须有 message」
// 是**前端能不能看见真错因**的充要条件 —— 后端每条错误路径都得满足它。
//
// 这条判据的存在理由：本项目的「UI 填了但没生效」族，有一半是后端回了话而前端
// 听不见。只测后端的 code/ok 永远抓不到这类问题，必须把**前端读哪个键**钉进测试。
func TestBofsErrEnvelopeCarriesMessage(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustMkdir(t, filepath.Join(dir, "bofs"))

	type hdl func(string, []byte) (int, []byte)
	cases := []struct {
		label string
		code  int
		h     hdl
		body  string
	}{
		{"upload 名字不合规", 400, bofsHandleUpload, `{"name":"../etc","kind":"o","content_b64":"AAAA"}`},
		{"upload 非 base64", 400, bofsHandleUpload, `{"name":"ok","kind":"o","content_b64":"!!!not base64!!!"}`},
		{"compile 名字不合规", 400, bofsHandleCompile, `{"name":"-x"}`},
		{"compile 非法 JSON", 400, bofsHandleCompile, `{`},
	}
	for _, c := range cases {
		code, body := c.h("tester", []byte(c.body))
		if code != c.code {
			t.Errorf("%s：HTTP 码=%d，期望 %d（body=%s）", c.label, code, c.code, string(body))
		}
		m := bofsDecode(t, body)
		msg, _ := m["message"].(string)
		errs, _ := m["error"].(string)
		if msg == "" {
			t.Errorf("%s：错误信封缺 message ⇒ 前端只会显示 'request failed'，真错因被吞（http.ts:92）。body=%s",
				c.label, string(body))
		}
		if msg != errs {
			t.Errorf("%s：message 与 error 不同值（%q vs %q）—— 不引入第二套语义", c.label, msg, errs)
		}
	}
}

// TestBofsCompileFailureCarriesCompilerOutput —— 上半条判据的**另一条路径**。
//
// bofsErr 那条只管「参数校验类」错误。编译失败走的是另一条出口
// （200/400 + ok=false + result 里带 output），如果只在 result 里给错因，
// 前端拿到的仍然只是 'request failed' —— 用户看得见「失败」看不见「为什么」。
//
// 本用例故意喂一个**语法错**的 .c，断言顶层 message 里真的带着 gcc 的报错文本。
// 这是「编译器输出能到操作员眼前」的可执行形式。
func TestBofsCompileFailureCarriesCompilerOutput(t *testing.T) {
	if _, err := exec.LookPath("gcc"); err != nil {
		t.Skip("跳过：无 gcc")
	}
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustMkdir(t, filepath.Join(dir, "bofs"))
	bofsMustMkdir(t, filepath.Join(dir, "src_agent", "agent"))
	bofsMustMkdir(t, filepath.Join(dir, "src_agent", "bofs"))
	// 最小 bof_api.h：本用例只求「能过预处理、死在语法上」，所以给个空头即可。
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "agent", "bof_api.h"), []byte("/* test */\n"))

	// 1) 源码不存在
	code, raw := bofsHandleCompile("tester", []byte(`{"name":"nosuch"}`))
	m := bofsDecode(t, raw)
	if code != 400 || m["ok"] != false {
		t.Fatalf("源码不存在应 400/ok=false，得 %d/%v（%s）", code, m["ok"], string(raw))
	}
	if s, _ := m["message"].(string); s == "" {
		t.Errorf("源码不存在：顶层缺 message ⇒ 前端只会说 'request failed'（%s）", string(raw))
	}

	// 2) 有源码但编不过 —— 这条才是重点
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "bofs", "bad.c"),
		[]byte("int f(void) { this is not C }\n"))
	code, raw = bofsHandleCompile("tester", []byte(`{"name":"bad"}`))
	m = bofsDecode(t, raw)
	if code != 400 || m["ok"] != false {
		t.Fatalf("语法错应 400/ok=false，得 %d/%v（%s）", code, m["ok"], string(raw))
	}
	msg, _ := m["message"].(string)
	if msg == "" {
		t.Fatalf("编译失败：顶层缺 message ⇒ gcc 的真实报错到不了操作员眼前（%s）", string(raw))
	}
	// gcc 对语法错必然在 stderr 里出现 "error"；这条断言 = 「输出真的被带上来了」。
	if !strings.Contains(strings.ToLower(msg), "error") {
		t.Errorf("message 里没有 gcc 的报错文本，怀疑只回了一句概括：%q", msg)
	}
	if out, _ := m["output"].(string); out == "" {
		t.Errorf("顶层缺 output（脚本消费用），body=%s", string(raw))
	}
	res, _ := m["result"].(map[string]any)
	if res == nil || res["ok"] != false {
		t.Errorf("result 结构应保留给自动化消费，body=%s", string(raw))
	}
}

// TestBofsBaseStaysAbsoluteUnderRelativeModuleDir —— 反「假绿」判据。
//
// 为什么要单独一条：上面 TestBofsListShape 里的 base 断言看似在测「base 有绝对
// 路径」，但那个用例把 ModuleDir 设成了 t.TempDir 系的**绝对**路径 —— 于是 base
// 天然是绝对的，断言恒真，**生产实况根本跑不到**。而生产实况恰恰相反：ModuleDir
// 来自 profile 挂载串，是相对路径。
//
// 直接后果（真实可复现）：base 回成 "extenders/linux_agent/bofs/"，前端拼出
// `execute bof extenders/linux_agent/bofs/x.o`；服务端 pl_main.go:1595 的
// os.Stat 按 **cwd** 解析 —— cwd=/root/cc/run 时侥幸能中，换个 cwd 就静默失效
// （os.Stat 失败 ⇒ 落进 base64 分支 ⇒ agent 收到一串**路径文本**当 BOF 字节，
// 报错还是「BOF 格式错误」，与真因相隔十万八千里）。
//
// 本用例把 ModuleDir 钉成相对路径，断言 base 仍是绝对的、且落在 cwd 之下 ——
// 也就是与 os.Stat 的解析**同源**。
func TestBofsBaseStaysAbsoluteUnderRelativeModuleDir(t *testing.T) {
	old := ModuleDir
	ModuleDir = filepath.Join("relmod_probe", "linux_agent") // ★ 故意用相对路径
	t.Cleanup(func() { ModuleDir = old })

	base := bofsBaseForClient()

	if !filepath.IsAbs(base) {
		t.Fatalf("ModuleDir 为相对路径时 base 必须仍为绝对路径（否则 execute bof 的 os.Stat 受 cwd 摆布、且静默失效），得 %q", base)
	}
	if !strings.HasSuffix(base, "/") {
		t.Errorf("base 必须以 / 结尾，得 %q", base)
	}
	// 与 os.Stat 同源：base 去掉末尾 "/" 后，必须就是「cwd 折进去」的那个目录。
	want := filepath.Join("relmod_probe", "linux_agent", "bofs")
	if !strings.HasSuffix(filepath.ToSlash(strings.TrimSuffix(base, "/")), filepath.ToSlash(want)) {
		t.Errorf("base=%q 的尾部不是 %q（未把 cwd 折进去？）", base, want)
	}
	// 反向哨兵：若有人把 filepath.Abs 改回裸拼接，上面第一条必红 —— 此处再钉一次
	// 「相对形态」这个具体错值，免得将来 Abs 的语义被换掉时判据跟着漂。
	if base == want+"/" {
		t.Errorf("base 退回了相对形态 %q —— 正是本用例要挡的回归", base)
	}
}

// ---------------------------------------------------------------- 编译（核心）

// bofsRealRunDir —— 现役运行树（含 bofs/*.o 与 src_agent/agent/bof_api.h）。
func bofsRealRunDir() string {
	if v := os.Getenv("AX_BOFS_REALDIR"); v != "" {
		return v
	}
	return "/root/cc/run/extenders/linux_agent"
}

// bofsFindSrcDir —— .c 源码目录的回退链：run 树 → $AX_BOFS_SRCDIR → SRC 树。
//
// ★ 这个回退**必须真的实现**：上一版只 t.Logf 了一句"改从 SRC 树取源码"却没写
//   代码，结果头号判据因"没有『既有源码又有产物』的模块"而 SKIP —— 判据空转。
//   空转的判据等于没有判据（本项目「假绿」族的孪生形态）。
//   取不到就 t.Skip，绝不返回空串让调用方拿着 "" 往下跑。
func bofsFindSrcDir(t *testing.T) string {
	t.Helper()
	cands := []string{filepath.Join(bofsRealRunDir(), "src_agent", "bofs")}
	if v := os.Getenv("AX_BOFS_SRCDIR"); v != "" {
		cands = append(cands, filepath.Join(v, "src_agent", "bofs"))
	}
	cands = append(cands, "/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bofs")
	for _, c := range cands {
		if st, err := os.Stat(c); err == nil && st.IsDir() {
			return c
		}
	}
	t.Skipf("跳过：源码目录全线不可用，试过 %v", cands)
	return ""
}

// TestBofsCompileUnderRelativeModuleDir —— ★★ 反「假绿」判据之二（生产实况）。
//
// 背景：本文件其余编译用例都用 bofsUseModuleDir(t, t.TempDir 系) 把 ModuleDir
// 设成**绝对**路径，于是 `filepath.Join(ModuleDir, "src_agent/bofs/x.c")` 天然是
// 绝对的，`cmd.Dir = ModuleDir` 这个相对基准**从不参与解析** —— 判据恒真。
//
// 而生产实况恰恰相反：ModuleDir 来自 profile 挂载串，是**相对**路径。此时
// src 是相对的、cmd.Dir 也是相对的 ⇒ gcc 按 cmd.Dir 把相对路径**二次解析**成
//   extenders/linux_agent/extenders/linux_agent/src_agent/bofs/x.c
// ⇒ `cc1: fatal error: No such file or directory`。
// 这是批次20 运行态实测到的真缺陷，不是推演。
//
// 本用例把 module 建在 **cwd 之下**、只传**相对**名，用真 gcc 跑完整链路。
// 修复（InitPlugin 里 filepath.Abs 收口）之前必红。
func TestBofsCompileUnderRelativeModuleDir(t *testing.T) {
	gcc, err := exec.LookPath("gcc")
	if err != nil {
		t.Skip("跳过：无 gcc")
	}
	real := bofsRealRunDir()
	hdr := filepath.Join(real, "src_agent", "agent", "bof_api.h")
	if _, err := os.Stat(hdr); err != nil {
		t.Skipf("跳过：缺 bof_api.h（%s）：%v", hdr, err)
	}
	srcDir := bofsFindSrcDir(t)
	cs, _ := filepath.Glob(filepath.Join(srcDir, "*.c"))
	if len(cs) == 0 {
		t.Skipf("跳过：%s 下没有 .c", srcDir)
	}

	// ★ 关键：建在 cwd 之下，拿到一个**真正相对**的 ModuleDir。
	abs, err := os.MkdirTemp(".", ".relcase-")
	if err != nil {
		t.Fatalf("在 cwd 下建临时目录失败：%v", err)
	}
	t.Cleanup(func() { _ = os.RemoveAll(abs) })
	rel, err := filepath.Rel(".", abs)
	if err != nil || filepath.IsAbs(rel) {
		t.Fatalf("拿不到相对路径：rel=%q err=%v", rel, err)
	}
	t.Logf("ModuleDir（相对）= %q，绝对 = %q", rel, abs)

	// 搭最小 module：bofs/ + src_agent/agent/bof_api.h + src_agent/bofs/<一个>.c
	bofsMustMkdir(t, filepath.Join(abs, "bofs"))
	bofsMustMkdir(t, filepath.Join(abs, "src_agent", "agent"))
	bofsMustMkdir(t, filepath.Join(abs, "src_agent", "bofs"))
	hb, err := os.ReadFile(hdr)
	if err != nil {
		t.Fatalf("读 bof_api.h：%v", err)
	}
	bofsMustWrite(t, filepath.Join(abs, "src_agent", "agent", "bof_api.h"), hb)
	cb, err := os.ReadFile(cs[0])
	if err != nil {
		t.Fatalf("读 %s：%v", cs[0], err)
	}
	name := "relprobe"
	bofsMustWrite(t, filepath.Join(abs, "src_agent", "bofs", name+".c"), cb)

	old := ModuleDir
	ModuleDir = rel // ★ 只给相对名；cwd 由 gcc 的 cmd.Dir 隐式参与
	t.Cleanup(func() { ModuleDir = old })

	code, raw := bofsHandleCompile("tester", []byte(`{"name":"`+name+`"}`))
	m := bofsDecode(t, raw)
	res, _ := m["result"].(map[string]any)
	if res == nil {
		t.Fatalf("响应缺 result：%s", string(raw))
	}
	if res["ok"] != true {
		t.Fatalf("ModuleDir 为相对路径时编译必须成功，得 ok=%v error=%v output=%v\n（生产实证：gcc 会把相对源路径按相对 cmd.Dir 二次解析）",
			res["ok"], res["error"], res["output"])
	}
	if code != 200 {
		t.Errorf("HTTP 码应 200，得 %d", code)
	}
	obj := filepath.Join(abs, "bofs", name+".o")
	st, err := os.Stat(obj)
	if err != nil {
		t.Fatalf("产物不存在（%s）：%v", obj, err)
	}
	if st.Size() == 0 {
		t.Fatalf("产物为空：%s", obj)
	}
	// 产物必须是 ELF64 可重定位（ET_REL=1），不是被 gcc 留下的半成品。
	fh, err := os.Open(obj)
	if err != nil {
		t.Fatalf("开产物：%v", err)
	}
	defer fh.Close()
	var eh [20]byte
	if _, err := io.ReadFull(fh, eh[:]); err != nil {
		t.Fatalf("读 ELF 头：%v", err)
	}
	if eh[0] != 0x7f || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F' {
		t.Fatalf("产物不是 ELF：% x", eh[:4])
	}
	if et := int(eh[16]) | int(eh[17])<<8; et != 1 {
		t.Fatalf("e_type=%d，期望 1（ET_REL）", et)
	}
	_ = gcc // LookPath 仅为「无 gcc 则跳过」把关
}

// TestBofsCompileMatchesShippedArtifact —— ★★ 本文件最重要的一条。
//
// 把现役运行树的 bofs/ 与 src_agent/ 复制到临时目录，用 bofsCompileOne
// 重编一个现役 BOF，断言 **Changed == false**。
// Changed 基于字节比对，所以这条断言 = 「配方与现役产物同源」。
// 配方漂移（改 -O、丢 -fPIC、-I 指错）会立刻变红。
func TestBofsCompileMatchesShippedArtifact(t *testing.T) {
	real := bofsRealRunDir()
	if _, err := os.Stat(filepath.Join(real, "src_agent", "agent", "bof_api.h")); err != nil {
		t.Skipf("跳过：现役运行树不可用（%s）：%v", real, err)
	}
	objs, err := filepath.Glob(filepath.Join(real, "bofs", "*.o"))
	if err != nil || len(objs) == 0 {
		t.Skipf("跳过：%s/bofs 下没有 .o", real)
	}
	srcDir := bofsFindSrcDir(t)
	t.Logf("产物取自 %s/bofs，源码取自 %s", real, srcDir)

	dir := bofsTestRoot(t)
	copyDir := func(from, to string) int {
		if _, err := os.Stat(from); err != nil {
			return 0
		}
		bofsMustMkdir(t, to)
		des, err := os.ReadDir(from)
		if err != nil {
			t.Fatalf("读 %s: %v", from, err)
		}
		n := 0
		for _, de := range des {
			if de.IsDir() {
				continue
			}
			data, err := os.ReadFile(filepath.Join(from, de.Name()))
			if err != nil {
				t.Fatalf("读 %s: %v", de.Name(), err)
			}
			bofsMustWrite(t, filepath.Join(to, de.Name()), data)
			n++
		}
		return n
	}
	nObj := copyDir(filepath.Join(real, "bofs"), filepath.Join(dir, "bofs"))
	nHdr := copyDir(filepath.Join(real, "src_agent", "agent"), filepath.Join(dir, "src_agent", "agent"))
	nSrc := copyDir(srcDir, filepath.Join(dir, "src_agent", "bofs"))
	if nObj == 0 || nHdr == 0 || nSrc == 0 {
		t.Fatalf("拷贝不完整：obj=%d hdr=%d src=%d", nObj, nHdr, nSrc)
	}
	bofsUseModuleDir(t, dir)

	// 找一个「有源码且已有产物」的模块
	entries, _ := bofsScan()
	var pick string
	for _, e := range entries {
		if e.Obj && e.Src {
			pick = e.Name
			break
		}
	}
	if pick == "" {
		t.Skipf("跳过：临时树里没有「既有源码又有产物」的模块（清单 %d 项）", len(entries))
	}

	res := bofsCompileOne(pick)
	if !res.OK {
		t.Fatalf("重编 %s 失败：%s\n%s", pick, res.Error, res.Output)
	}
	if res.Changed {
		t.Fatalf("★★ %s 重编后字节与现役产物不同 —— "+
			"说明 bofsCompileOne 的 gcc 配方已与生成现役产物的配方漂移。\n"+
			"配方（必须与 tools/deploy.sh:63-64 同源）：\n"+
			"  gcc -c -o <out.o> <src.c> -include bof_api.h -Os -fPIC -fno-stack-protector -I <agent_src>\n"+
			"新产物 sha=%s size=%d", pick, res.ObjSHA256, res.ObjSize)
	}

	// 幂等：再编一次仍是 no-op
	res2 := bofsCompileOne(pick)
	if !res2.OK || res2.Changed {
		t.Fatalf("第二次重编应仍为 no-op，得 OK=%v Changed=%v", res2.OK, res2.Changed)
	}
	if res2.ObjSHA256 != res.ObjSHA256 {
		t.Fatalf("两次编译的 sha 不同：%s vs %s", res.ObjSHA256, res2.ObjSHA256)
	}
	if d := bofsDotFiles(t, filepath.Join(dir, "bofs")); len(d) != 0 {
		t.Fatalf("★ 编译留下临时残渣：%v", d)
	}
}

// TestBofsCompileDetectsRealChange —— 反向判据（防「永远返回 Changed=false」的假绿）：
// 改了源码之后，Changed 必须变 true。
func TestBofsCompileDetectsRealChange(t *testing.T) {
	real := bofsRealRunDir()
	hdr, err := os.ReadFile(filepath.Join(real, "src_agent", "agent", "bof_api.h"))
	if err != nil {
		t.Skipf("跳过：拿不到 bof_api.h（%v）", err)
	}
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	bofsMustWrite(t, filepath.Join(dir, "src_agent", "agent", "bof_api.h"), hdr)
	srcPath := filepath.Join(dir, "src_agent", "bofs", "probe.c")
	bofsMustWrite(t, srcPath, []byte("int probe_a(void){return 1;}\n"))

	r1 := bofsCompileOne("probe")
	if !r1.OK {
		t.Fatalf("首编失败：%s\n%s", r1.Error, r1.Output)
	}
	if !r1.Changed {
		t.Fatalf("★ 首次编译（产物原本不存在）必须 Changed=true，否则说明 Changed 恒 false")
	}
	if r2 := bofsCompileOne("probe"); r2.Changed {
		t.Fatalf("未改源码时二次编译应 no-op，实际 Changed=true")
	}
	// 改源码 ⇒ 必须检测到
	bofsMustWrite(t, srcPath, []byte("int probe_a(void){return 2;}\nint probe_b(void){return 3;}\n"))
	r3 := bofsCompileOne("probe")
	if !r3.OK {
		t.Fatalf("改后编译失败：%s\n%s", r3.Error, r3.Output)
	}
	if !r3.Changed {
		t.Fatalf("★ 源码已改，Changed 仍为 false —— 字节比对失效")
	}
	if r3.ObjSHA256 == r1.ObjSHA256 {
		t.Fatalf("★ 源码改了但 sha 不变 —— 编译没真正生效")
	}
}

// TestBofsCompileRejectsBadName —— 名字是 argv 边界，编译口也必须挡住。
func TestBofsCompileRejectsBadName(t *testing.T) {
	dir := bofsTestRoot(t)
	bofsUseModuleDir(t, dir)
	for _, n := range []string{"../x", "a b", "-O2", "a;id", ""} {
		if r := bofsCompileOne(n); r.OK {
			t.Errorf("★ 非法名 %q 竟然编译成功", n)
		}
	}
}

// TestBofsExecScanRootsStale —— Extension-Kit 侧的陈旧判定（缺陷 C）。
//
// 判据不是「.c 存在与否」，而是**恰好一个同分类同名的 .c 且它更新**。
// 本用例把三种「不标」的情形全部摆出来，因为它们全都是**方向性**决定：
// 漏标只是少一条提示，错标会让操作员去重编一个根本没改的 BOF。
func TestBofsExecScanRootsStale(t *testing.T) {
	root := bofsTestRoot(t)

	old := time.Now().Add(-2 * time.Hour)
	fresh := time.Now()

	// mk 落一个文件并显式设定 mtime —— 陈旧判定全靠它，不能靠写入顺序碰运气。
	mk := func(rel string, mtime time.Time) string {
		t.Helper()
		p := filepath.Join(root, rel)
		bofsMustWrite(t, p, []byte(rel))
		if err := os.Chtimes(p, mtime, mtime); err != nil {
			t.Fatalf("chtimes %s: %v", p, err)
		}
		return p
	}

	// 1) 新鲜的：源码旧、产物新 ⇒ 不标。
	mk("SAL-BOF/_bin/ok.x64.o", fresh)
	mk("SAL-BOF/ok/ok.c", old)
	// 2) 陈旧的：源码比产物新 ⇒ 必须标。
	mk("SAR-BOF/_bin/thing.x64.o", old)
	mk("SAR-BOF/thing/thing.c", fresh)
	// 3) 同名歧义：两个 .c ⇒ 判不出是哪个 ⇒ 不标。
	mk("Amb-BOF/_bin/amb.x64.o", old)
	mk("Amb-BOF/a/amb.c", fresh)
	mk("Amb-BOF/b/amb.c", fresh)
	// 4) 源码不在本树 ⇒ 无从判 ⇒ 不标。
	mk("NoSrc-BOF/_bin/only.x64.o", old)
	// 5) ★ 跨分类不借：AD-BOF 下的 whoami.x64.o 与 SAL-BOF 的 whoami.c 同名，
	//    但 AD-BOF 里没有任何 whoami.c。SAL 那份更新，**也不能**标到 AD 的产物上。
	mk("SAL-BOF/whoami/whoami.c", fresh)
	mk("AD-BOF/SQL-BOF/_bin/SQL/whoami.x64.o", old)

	entries, err := bofsExecScanRoots([]bofsExecRoot{{Dir: root, Source: "test"}}, "x64")
	if err != nil {
		t.Fatalf("scan: %v", err)
	}

	byPath := map[string]bofsExecEntry{}
	for _, e := range entries {
		byPath[e.Name] = e
	}

	// 回归：.c 自己绝不能混进候选（它是源码，不是可执行的 COFF）。
	for _, e := range entries {
		if strings.HasSuffix(e.Name, ".c") {
			t.Fatalf("★ 源码 %s 混进了候选清单", e.Name)
		}
	}
	if len(entries) != 5 {
		t.Fatalf("候选数应为 5（5 个 .x64.o），实得 %d：%v", len(entries), byPath)
	}

	cases := []struct {
		rel       string
		wantStale bool
		// 被标陈旧时，StaleReason 里必须出现的**源码路径**（相对 root）。
		wantSrc string
		why     string
	}{
		{"SAL-BOF/_bin/ok.x64.o", false, "", "产物比源码新"},
		{"SAR-BOF/_bin/thing.x64.o", true, "SAR-BOF/thing/thing.c", "源码比产物新"},
		{"Amb-BOF/_bin/amb.x64.o", false, "", "同名 .c 有两个，歧义"},
		{"NoSrc-BOF/_bin/only.x64.o", false, "", "源码不在本树"},
		{"AD-BOF/SQL-BOF/_bin/SQL/whoami.x64.o", false, "", "★ 跨分类不借源码"},
	}
	for _, c := range cases {
		e, ok := byPath[filepath.Join(root, c.rel)]
		if !ok {
			t.Errorf("候选里没有 %s", c.rel)
			continue
		}
		if e.Stale != c.wantStale {
			t.Errorf("%s：Stale=%v，应为 %v（%s）", c.rel, e.Stale, c.wantStale, c.why)
		}
		// stale 与 stale_reason 必须同生同灭：只给标志不给原因，
		// 前端就只能显示一个没有解释的 ⚠。
		if e.Stale != (e.StaleReason != "") {
			t.Errorf("%s：Stale=%v 但 StaleReason=%q —— 两者必须同时有或同时无",
				c.rel, e.Stale, e.StaleReason)
		}
		// 原因里要能看出是**哪份源码**比它新，否则操作员看到 ⚠ 还得自己去找。
		if c.wantSrc != "" && !strings.Contains(e.StaleReason, filepath.Join(root, c.wantSrc)) {
			t.Errorf("%s：StaleReason 未指出源码 %s：%q", c.rel, c.wantSrc, e.StaleReason)
		}
	}

	// 回归：加了 .c 索引之后，分组口径不能变 —— 仍是「根目录名 / 根下第一级目录」。
	// 期望值照**规则**写出来而不是抄一个常量，否则规则改了测试还绿。
	base := filepath.Base(root)
	for _, c := range []struct{ rel, wantGroup string }{
		{"AD-BOF/SQL-BOF/_bin/SQL/whoami.x64.o", base + "/AD-BOF"},
		{"SAR-BOF/_bin/thing.x64.o", base + "/SAR-BOF"},
	} {
		if g := byPath[filepath.Join(root, c.rel)].Group; g != c.wantGroup {
			t.Errorf("Group 口径变了：%s 得 %q，应为 %q", c.rel, g, c.wantGroup)
		}
	}
}

// TestBofsExecScanRootsArchWildcard —— arch="*" 时后缀退成 ".o"，此时
// 「按后缀反推 stem」会把 whoami.x64 当成名字、从而永远匹配不上 whoami.c。
// 真 stem 在收集时就记下了，所以通配也必须能判出陈旧。
func TestBofsExecScanRootsArchWildcard(t *testing.T) {
	root := bofsTestRoot(t)
	write := func(rel string, mtime time.Time) {
		t.Helper()
		p := filepath.Join(root, rel)
		bofsMustWrite(t, p, []byte(rel))
		if err := os.Chtimes(p, mtime, mtime); err != nil {
			t.Fatalf("chtimes: %v", err)
		}
	}
	write("SAL-BOF/_bin/thing.x64.o", time.Now().Add(-2*time.Hour))
	write("SAL-BOF/thing/thing.c", time.Now())

	entries, err := bofsExecScanRoots([]bofsExecRoot{{Dir: root, Source: "test"}}, "*")
	if err != nil {
		t.Fatalf("scan: %v", err)
	}
	if len(entries) != 1 {
		t.Fatalf("通配应收到 1 条，实得 %d", len(entries))
	}
	if !entries[0].Stale {
		t.Errorf("★ arch=* 时未判出陈旧 —— stem 多半是按后缀反推的，反推出了 %q",
			strings.TrimSuffix(entries[0].Label, ".o"))
	}
}
