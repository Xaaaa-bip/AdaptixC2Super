package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
	"gopkg.in/yaml.v3"
)

/*
 * relay —— axrelay（libp2p 中转）进程的 WebUI 控制面。
 *
 * 为什么需要它：axrelay 是**独立于 teamserver 的进程**。它死了 WebUI 完全不知道
 * （页面照常开、监听器照常列着），而 libp2p 载荷会静默失联。
 * 2026-09-20 实测就撞上过一次：relay 已经没了、4446 零监听、连日志文件都不存在，
 * 没人发现，而且它**永远不会自己回来**。
 *
 * ★ 为什么是「转给 systemctl」而不是「插件自己 fork 一个」：
 *   relay 的生死只能有**一个 owner**。若 systemd 管着它（axrelay.service，
 *   Restart=always），插件再 fork 一个就会有两个进程抢 4446，表现为
 *   「时好时坏」——这正是最难查的一类故障。所以本插件只做控制面。
 *
 * ★ 安全：本插件**不接受任何来自请求体的参数**。unit 名、路径全部是配置常量，
 *   body 一律忽略 ⇒ 没有命令注入面。不要为了「灵活」把 unit 名做成请求参数。
 */

const extenderName = "relay"

type PluginService struct{}

var (
	Ts        Teamserver
	ModuleDir string
	Config    RelayConfig
)

// Teamserver 是本插件用到的那一小撮方法（写法同 extenders/topology）。
// 方法签名必须与 core/server 上的实现逐字一致。
type Teamserver interface {
	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
}

type RelayConfig struct {
	ApiRoute string `yaml:"api_route" json:"api_route"`
	UnitName string `yaml:"unit_name" json:"unit_name"`
	Listen   string `yaml:"relay_listen" json:"relay_listen"`
	LogFile  string `yaml:"log_file" json:"log_file"`
}

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = moduleDir

	Config = RelayConfig{
		ApiRoute: "/relay",
		UnitName: "axrelay.service",
		Listen:   "/ip4/0.0.0.0/tcp/4446",
		LogFile:  "/home/ad/dist/axrelay.log",
	}
	if serviceConfig != "" {
		if err := yaml.Unmarshal([]byte(serviceConfig), &Config); err != nil {
			fmt.Printf("[RELAY] Error parsing config: %v\n", err)
		}
	}

	// 注册失败必须出声：静默失败的表象是「前端 404 而服务端一切正常」，
	// 404 又会被当成前端拼错 URL，很难查回这里。
	routes := []struct {
		method  string
		suffix  string
		handler func(string, []byte) (int, []byte)
	}{
		{"GET", "/status", handleStatus},
		{"POST", "/start", actionHandler("start")},
		{"POST", "/stop", actionHandler("stop")},
		{"POST", "/restart", actionHandler("restart")},
	}
	for _, r := range routes {
		if err := Ts.TsEndpointRegister(r.method, Config.ApiRoute+r.suffix, r.handler); err != nil {
			fmt.Printf("[RELAY] Failed to register %s %s%s: %v\n", r.method, Config.ApiRoute, r.suffix, err)
		}
	}

	fmt.Printf("[RELAY] Service initialized. API: %s (unit %s)\n", Config.ApiRoute, Config.UnitName)
	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {}

/// systemd 交互

// systemctlRun 执行一条 systemctl 并回传 (合并输出, 退出码)。
// 退出码 -1 表示「命令没能执行」（不是 systemctl 报错，而是根本没跑起来）。
func systemctlRun(args ...string) (string, int) {
	ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancel()

	out, err := exec.CommandContext(ctx, "systemctl", args...).CombinedOutput()
	s := strings.TrimSpace(string(out))
	if err != nil {
		var ee *exec.ExitError
		if errors.As(err, &ee) {
			return s, ee.ExitCode()
		}
		// 超时或 systemctl 不存在
		if ctx.Err() != nil {
			return "systemctl 超时", -1
		}
		return err.Error(), -1
	}
	return s, 0
}

// unitFile 在 systemd 的标准目录里找 unit 文件。
// 用 os.Stat 而不是 `systemctl cat`：前者不依赖 systemd 是否活着。
func unitFile(name string) string {
	dirs := []string{
		"/etc/systemd/system",
		"/run/systemd/system",
		"/usr/lib/systemd/system",
		"/lib/systemd/system",
	}
	for _, d := range dirs {
		p := d + "/" + name
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p
		}
	}
	return ""
}

// showProps 解析 `systemctl show -p K1 -p K2` 的 KEY=VALUE 输出。
func showProps(unit string, props ...string) map[string]string {
	args := []string{"show", "--no-pager"}
	for _, p := range props {
		args = append(args, "-p", p)
	}
	args = append(args, unit)

	out, code := systemctlRun(args...)
	m := map[string]string{}
	if code != 0 && out == "" {
		return m
	}
	for _, line := range strings.Split(out, "\n") {
		if i := strings.IndexByte(line, '='); i > 0 {
			m[line[:i]] = line[i+1:]
		}
	}
	return m
}

var peerIDRe = regexp.MustCompile(`12D3Koo[1-9A-HJ-NP-Za-km-z]{20,}`)

// tailLines 读文件末尾 maxBytes 字节并按行切开，最多回 maxLines 行。
// 文件不存在不是错误（relay 从没跑过时就是这样），回空。
func tailLines(path string, maxBytes int, maxLines int) []string {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()

	st, err := f.Stat()
	if err != nil {
		return nil
	}
	size := st.Size()
	off := int64(0)
	if size > int64(maxBytes) {
		off = size - int64(maxBytes)
	}
	buf := make([]byte, size-off)
	if len(buf) > 0 {
		if _, err := f.ReadAt(buf, off); err != nil && !errors.Is(err, io.EOF) {
			return nil
		}
	}

	lines := strings.Split(strings.TrimRight(string(buf), "\r\n"), "\n")
	if len(lines) == 1 && lines[0] == "" {
		return nil
	}
	if len(lines) > maxLines {
		lines = lines[len(lines)-maxLines:]
	}
	// 截断后第一行可能是半行（我们从中间切进去的），标记出来而不是假装它是完整行。
	if off > 0 && len(lines) > 0 {
		lines[0] = "… " + lines[0]
	}
	return lines
}

/// 状态

type statusResp struct {
	Ok          bool     `json:"ok"`
	Unit        string   `json:"unit"`
	UnitFile    string   `json:"unit_file"`
	UnitPresent bool     `json:"unit_present"`
	Systemctl   bool     `json:"systemctl_present"`
	Active      string   `json:"active"`
	SubState    string   `json:"sub_state"`
	Enabled     string   `json:"enabled"`
	MainPID     string   `json:"main_pid"`
	NRestarts   string   `json:"n_restarts"`
	Since       string   `json:"since"`
	Listen      string   `json:"listen"`
	PeerID      string   `json:"peer_id"`
	LogFile     string   `json:"log_file"`
	LogTail     []string `json:"log_tail"`
	Hint        string   `json:"hint,omitempty"`
}

func collectStatus() statusResp {
	r := statusResp{
		Unit:     Config.UnitName,
		Listen:   Config.Listen,
		LogFile:  Config.LogFile,
		Active:   "unknown",
		Enabled:  "unknown",
		SubState: "unknown",
	}

	if _, err := exec.LookPath("systemctl"); err == nil {
		r.Systemctl = true
	}
	r.UnitFile = unitFile(Config.UnitName)
	r.UnitPresent = r.UnitFile != ""

	if !r.Systemctl {
		r.Hint = "本机没有 systemctl —— relay 只能手工启动"
		return r
	}
	if !r.UnitPresent {
		r.Hint = fmt.Sprintf("unit 文件不存在：/etc/systemd/system/%s —— relay 无法被托管", Config.UnitName)
		return r
	}

	// is-active / is-enabled 用退出码表达状态，所以用 is-enabled 的输出文本而非码。
	if s, _ := systemctlRun("is-active", Config.UnitName); s != "" {
		r.Active = s
	}
	if s, _ := systemctlRun("is-enabled", Config.UnitName); s != "" {
		r.Enabled = s
	}

	props := showProps(Config.UnitName,
		"SubState", "MainPID", "NRestarts", "ActiveEnterTimestamp", "ExecMainStartTimestamp")
	if v, ok := props["SubState"]; ok {
		r.SubState = v
	}
	if v := props["MainPID"]; v != "" && v != "0" {
		r.MainPID = v
	}
	if v := props["NRestarts"]; v != "" {
		r.NRestarts = v
	}
	if v := props["ActiveEnterTimestamp"]; v != "" && v != "n/a" {
		r.Since = v
	}
	if r.Since == "" {
		if v := props["ExecMainStartTimestamp"]; v != "" && v != "n/a" {
			r.Since = v
		}
	}

	// peer id 从日志里抓 —— 它由 -key 决定，是「已发出去的 agent 地址」的一部分，
	// 换 key 就换 id，所以显示出来有实际意义（能一眼看出 key 有没有被换过）。
	tail := tailLines(Config.LogFile, 64*1024, 60)
	r.LogTail = tail
	for _, line := range tail {
		if m := peerIDRe.FindString(line); m != "" {
			r.PeerID = m
		}
	}

	r.Ok = true
	return r
}

func handleStatus(username string, body []byte) (int, []byte) {
	out, err := json.Marshal(collectStatus())
	if err != nil {
		return 500, jsonErr("marshal failed: " + err.Error())
	}
	return 200, out
}

/// 动作

// actionHandler 只放行白名单里的动词。body 一律忽略（见文件头「安全」）。
func actionHandler(verb string) func(string, []byte) (int, []byte) {
	return func(username string, body []byte) (int, []byte) {
		out, code := systemctlRun(verb, Config.UnitName)

		// 动作后一律回最新状态：前端一次往返就能刷新，不用再轮询一次。
		st := collectStatus()
		st.Ok = code == 0
		if code != 0 {
			st.Hint = fmt.Sprintf("systemctl %s %s 退出码 %d：%s", verb, Config.UnitName, code, out)
		}

		raw, err := json.Marshal(st)
		if err != nil {
			return 500, jsonErr("marshal failed: " + err.Error())
		}
		if code != 0 {
			return 500, raw
		}
		return 200, raw
	}
}

func jsonErr(msg string) []byte {
	data, _ := json.Marshal(map[string]interface{}{"ok": false, "message": msg})
	return data
}
