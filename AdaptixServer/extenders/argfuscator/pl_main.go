/*
 * argfuscator —— PowerShell 命令行参数混淆（wietze/Invoke-ArgFuscator 的服务端插件）。
 *
 * 为什么跑在服务端而不是前端：混淆质量取决于模块里的 models/*.json（156 个命令
 * 的参数表），那是几 MB 的数据 + 一套 PowerShell 运行时。塞进浏览器意味着把
 * 整套模型发给每个操作员，而且每个操作员手里的版本还会各自漂移。
 *
 * ★ 用户文本**从不**进命令行，只走环境变量。
 *   `pwsh -Command "…$userText…"` 会让分号 / 管道 / $() / 反引号以 teamserver
 *   权限任意执行 —— 这是本插件唯一致命的一条，所以 argv 是 100% 静态字面量，
 *   命令文本经 cmd.Env 递给一个**静态随附**的包装脚本（vendor_ps/argfuscate.ps1）。
 *   同理也不经 sh -c / cmd /c：exec.Command 直接走 argv，Go 从不启 shell。
 *
 * 为什么包装脚本要自己拼分隔符：模块每个变体调一次 Write-Output，而**一个变体
 * 可能是多行的**（多行 PowerShell 合法）。按行 split 会把 1 个多行变体读成 2 个
 * 变体 —— 一个不会报错的错误答案。所以包装脚本用 U+001E 显式拼装，Go 侧按它切。
 */
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
	"gopkg.in/yaml.v3"
)

type PluginService struct{}

var (
	Ts        Teamserver
	ModuleDir string
	Config    ArgFuscatorConfig

	// pwsh 是单进程、~100MB RSS、每次 1.5s 起的重活。串行化把 teamserver 上的
	// 并发进程数钉在 1 —— 与 bofs 编译同一套取舍（宁可排队，不要被打爆）。
	pwshLock sync.Mutex

	// 启动时探一次就缓存：/status 是操作员判断「插件到底能不能用」的入口，
	// 让它每次都为读个版本号去 fork 一个 pwsh，是把便宜问题做成了贵问题。
	probe     PwshProbe
	modelList []string
)

// 变体分隔符。U+001E（记录分隔符）在命令行里不可能出现 —— 而且见 validateCommand，
// 真出现了也会被**拒绝**，不是转义：那是在伪造我们自己的编码边界。
const variantSep = "\x1e"

// 命令长度上限。环境变量装得下更多，但一条 4KB 的 PowerShell 命令已经不是
// 「混淆一个命令行」而是别的东西了；给个明确的天花板好过让它慢慢吃掉内存。
const maxCommandBytes = 4096

type Teamserver interface {
	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
}

type ArgFuscatorConfig struct {
	// 空 = 用 PATH 里的 pwsh。填了就用填的（便于钉死某个版本）。
	PwshPath string `yaml:"pwsh_path" json:"pwsh_path"`
	// 一次请求最多生成几个变体。每个变体是内存里一行字符串，无上限就是
	// teamserver 上的 CPU/内存放大器。
	MaxVariants int `yaml:"max_variants" json:"max_variants"`
	TimeoutSec  int `yaml:"timeout_sec" json:"timeout_sec"`
}

type PwshProbe struct {
	Available bool   `json:"available"`
	Bin       string `json:"bin"`
	Version   string `json:"version"`
	Reason    string `json:"reason"`
}

type generateReq struct {
	Command  string `json:"command"`
	Platform string `json:"platform"`
	N        int    `json:"n"`
	// 刻意**不**收 profile：在 -Command 这条路径上上游根本不读它
	// （psm1 的 FromCommand 分支只用 platform 去挑 profile，$Profile 参数
	// 全程未被引用；实测 -Profile 99 也不报错）。收一个不生效的旋钮，
	// 比不收更糟 —— 操作员会以为自己控制了变量。
}

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)

	// 绝对路径：pl_bofs_api.go:414-424 记过一次真实事故 —— 相对路径会把
	// extenders/linux_agent 拼成 extenders/linux_agent/extenders/linux_agent/…，
	// 而那时候报错信息看起来跟路径毫无关系。
	if abs, err := filepath.Abs(moduleDir); err == nil {
		ModuleDir = abs
	} else {
		ModuleDir = moduleDir
	}

	if err := yaml.Unmarshal([]byte(serviceConfig), &Config); err != nil {
		fmt.Println("[ArgFuscator] Error parsing service config:", err)
	}
	normalizeConfig(&Config)

	probe = probePwsh()
	modelList = listModels()

	// 注册失败必须**喊出来**。忽略了它，插件会照样打印「Service initialized」，
	// 而端点根本不存在 —— 从外面看只剩一句「路由没匹配上」，与「插件没装」
	// 完全同形。这条是实测踩出来的：当时只能靠对比"已注册路由被鉴权挡下返回
	// 404、未注册的落到静态兜底返回 200"来反推，绕了一大圈。
	for _, r := range []struct {
		method, path string
		h            func(string, []byte) (int, []byte)
	}{
		{"POST", "/argfuscator/status", handleStatus},
		{"POST", "/argfuscator/generate", handleGenerate},
	} {
		if err := Ts.TsEndpointRegister(r.method, r.path, r.h); err != nil {
			fmt.Printf("[ArgFuscator] ★ 端点注册失败 %s /api%s: %v\n", r.method, r.path, err)
		}
	}

	// pwsh 缺失**不**阻止插件加载：那样操作员只会拿到一个 404，分不清
	// 「插件没装」和「pwsh 没装」。加载起来、在 /status 里说实话，前端才能
	// 把横幅写得具体。
	fmt.Printf("[ArgFuscator] Service initialized. pwsh=%v version=%q models=%d max_variants=%d\n",
		probe.Available, probe.Version, len(modelList), Config.MaxVariants)

	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {}

func normalizeConfig(c *ArgFuscatorConfig) {
	c.PwshPath = strings.TrimSpace(c.PwshPath)
	if c.MaxVariants <= 0 || c.MaxVariants > 20 {
		c.MaxVariants = 5
	}
	if c.TimeoutSec <= 0 || c.TimeoutSec > 600 {
		c.TimeoutSec = 60
	}
}

func resolvePwsh() string {
	if Config.PwshPath != "" {
		return Config.PwshPath
	}
	if p, err := exec.LookPath("pwsh"); err == nil {
		return p
	}
	return ""
}

func wrapperPath() string {
	return filepath.Join(ModuleDir, "vendor_ps", "argfuscate.ps1")
}

func probePwsh() PwshProbe {
	bin := resolvePwsh()
	if bin == "" {
		return PwshProbe{Reason: "服务端 PATH 里没有 pwsh，且插件配置未指定 pwsh_path"}
	}
	if _, err := os.Stat(wrapperPath()); err != nil {
		return PwshProbe{Bin: bin, Reason: "找不到包装脚本 " + wrapperPath()}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, bin, "-NoProfile", "-NonInteractive", "-NoLogo",
		"-Command", "$PSVersionTable.PSVersion.ToString()").Output()
	if err != nil {
		return PwshProbe{Bin: bin, Reason: "pwsh 无法执行：" + err.Error()}
	}
	v := strings.TrimSpace(string(out))
	if v == "" {
		return PwshProbe{Bin: bin, Reason: "pwsh 没有报出版本号"}
	}
	return PwshProbe{Available: true, Bin: bin, Version: v}
}

// listModels 读 vendor_ps/models/*.json 的文件名（去扩展名）。
// 它同时是「哪些命令能混淆」的权威名单 —— 上游是先 tokenise 再拿第一个词去
// 拼 models/<cmd>.json，所以第一个词必须是**裸命令名**：写
// C:\Windows\System32\certutil.exe 会去找 models/C:/Windows/.../certutil.exe.json
// 然后报「command not found」。这条由 /status 的 commands 暴露给前端做提示。
func listModels() []string {
	entries, err := os.ReadDir(filepath.Join(ModuleDir, "vendor_ps", "models"))
	if err != nil {
		fmt.Println("[ArgFuscator] models dir unreadable:", err)
		return []string{}
	}
	out := make([]string, 0, len(entries))
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		out = append(out, strings.TrimSuffix(e.Name(), ".json"))
	}
	sort.Strings(out)
	return out
}

// fail 统一错误信封。
// ★ 必须**同时**带 error 和 message：前端 http.ts:92 只抛 data.message，
// 少了它就只剩一句 "request failed"，操作员拿不到任何可行动的信息。
func fail(code int, slug string, msg string) (int, []byte) {
	resp, _ := json.Marshal(map[string]interface{}{"ok": false, "error": slug, "message": msg})
	return code, resp
}

func handleStatus(username string, body []byte) (int, []byte) {
	resp, _ := json.Marshal(map[string]interface{}{
		"ok":           true,
		"available":    probe.Available,
		"reason":       probe.Reason,
		"pwsh":         probe.Bin,
		"pwsh_version": probe.Version,
		"module_dir":   ModuleDir,
		"models":       len(modelList),
		"commands":     modelList,
		"platforms":    []string{"windows", "linux", "mac"},
		"max_variants": Config.MaxVariants,
		"timeout_sec":  Config.TimeoutSec,
	})
	return 200, resp
}

// validateCommand 是信任边界（前端的那份枚举只是便利）。
func validateCommand(cmd string) (string, string) {
	c := strings.TrimSpace(cmd)
	if c == "" {
		return "", "命令不能为空"
	}
	if len(c) > maxCommandBytes {
		return "", fmt.Sprintf("命令过长（%d 字节，上限 %d）", len(c), maxCommandBytes)
	}
	// NUL：环境变量装不下，被截断的运行时会让命令**静默变形**——
	// 混淆出一条跟操作员输入的不是同一条的命令，是本插件最坏的失败方式。
	if strings.ContainsRune(c, 0) {
		return "", "命令里含 NUL 字节，无法安全传递"
	}
	// U+001E 是我们的变体分隔符。放行它 = 让操作员伪造编码边界，
	// 把一个变体劈成两个。
	if strings.Contains(c, variantSep) {
		return "", "命令里含控制字符 U+001E（变体分隔符），请移除"
	}
	return c, ""
}

func normalizePlatform(p string) (string, bool) {
	switch strings.ToLower(strings.TrimSpace(p)) {
	case "windows", "":
		// 空 = windows，与上游 -Platform 的默认值一致
		return "windows", true
	case "linux":
		return "linux", true
	case "mac":
		return "mac", true
	}
	return "", false
}

// cleanEnv 剔掉调用方环境里已有的 AXF_* 前缀变量。
// 环境里若同时存在两份同名变量，「读哪一份」在 libc 与 .NET 之间并不一致
// （一个是线性取首个匹配，一个是建字典）。与其赌哪条路径赢，不如保证只有一份。
func cleanEnv(env []string) []string {
	out := env[:0:0]
	for _, kv := range env {
		if strings.HasPrefix(kv, "AXF_") {
			continue
		}
		out = append(out, kv)
	}
	return out
}

// firstToken 取命令的第一个词。上游就是拿它去拼 models/<cmd>.json 的
// （psm1:210-211），所以它才是「这条命令被归到哪个模型」的答案。
func firstToken(cmd string) string {
	f := strings.Fields(cmd)
	if len(f) == 0 {
		return ""
	}
	return f[0]
}

// explainPwshError 把上游几句**指向我们自己**的报错翻成人话。
//
// 最典型的是 "Cannot index into a null array"：命令确实被 models/ 收录了，
// 但模块没能解析出任何 token 时抛的。这句报错连同调用栈指向的是**我们包装
// 脚本的第 35 行** —— 操作员看到会以为插件坏了，转而来查我们，而真正的原因
// 在他的命令里。
//
// 实测的触发条件：命令**只有命令名、一个参数都没有**。ipconfig / netstat /
// arp / whoami 裸调用全部复现，各自补上参数后全部正常（4/4 一致）。
//
// 只翻译实际复现过的签名，其余原样透传 —— 猜错的「人话」比一句英文堆栈更坏。
func explainPwshError(msg string, command string) string {
	if !strings.Contains(msg, "Cannot index into a null array") {
		return msg
	}
	return fmt.Sprintf(
		"命令 '%s' 在参数模型表里，但它一个参数都没有，模块无从下手。"+
			"实测：裸 `ipconfig`/`netstat`/`arp`/`whoami` 都会这样，各自补上参数即可"+
			"（如 `ipconfig /all`）。上游原始报错：Cannot index into a null array",
		firstToken(command))
}

// clip 截断报错文本。pwsh 的错误里带 ANSI 转义和多行调用栈，原样回前端会
// 把对话框撑爆，而操作员需要的只是最后那句人话。
func clip(s string, n int) string {
	s = strings.TrimSpace(s)
	if len(s) <= n {
		return s
	}
	return s[:n] + "…"
}

func handleGenerate(username string, body []byte) (int, []byte) {
	var req generateReq
	if err := json.Unmarshal(body, &req); err != nil {
		return fail(400, "bad_json", "请求体不是合法 JSON："+err.Error())
	}

	command, bad := validateCommand(req.Command)
	if bad != "" {
		return fail(400, "bad_command", bad)
	}
	platform, ok := normalizePlatform(req.Platform)
	if !ok {
		return fail(400, "bad_platform", "平台只能是 windows / linux / mac")
	}
	n := req.N
	if n <= 0 {
		n = 1
	}
	// 超限**报错**而不是悄悄钳到上限：静默少给几个变体，操作员拿到的是
	// 一份与他要求不符的结果，而且没有任何地方会告诉他。
	if n > Config.MaxVariants {
		return fail(400, "bad_n", fmt.Sprintf("变体数 %d 超过上限 %d", n, Config.MaxVariants))
	}
	if !probe.Available {
		return fail(503, "pwsh_unavailable", probe.Reason)
	}

	// ★ 先拿锁再起超时。反过来的话，排在队尾的请求会把自己的超时预算
	// 耗在排队上，然后报「pwsh 超时」—— 而 pwsh 根本还没被启动。
	pwshLock.Lock()
	defer pwshLock.Unlock()

	ctx, cancel := context.WithTimeout(context.Background(),
		time.Duration(Config.TimeoutSec)*time.Second)
	defer cancel()

	start := time.Now()
	// argv 100% 静态：那台机器上的用户输入一个字都不在这里。
	// 不开 -ExecutionPolicy Bypass：Linux 上它本来就是空操作，写上去只会
	// 让人以为我们在依赖它做管控。
	cmd := exec.CommandContext(ctx, probe.Bin,
		"-NoProfile", "-NonInteractive", "-NoLogo",
		"-File", wrapperPath())
	cmd.Env = append(cleanEnv(os.Environ()),
		"AXF_CMD="+command,
		"AXF_PLATFORM="+platform,
		"AXF_N="+strconv.Itoa(n),
	)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	runErr := cmd.Run()
	ms := time.Since(start).Milliseconds()

	// ★ 先看超时再看 runErr：超时同样会让 Run 返回非 nil，顺序反了就会把
	// 「超时」报成「命令失败」，操作员转头去查自己的命令，查不出所以然。
	if ctx.Err() == context.DeadlineExceeded {
		return fail(504, "timeout",
			fmt.Sprintf("pwsh 超过 %d 秒未返回，已终止（命令越复杂、变体越多越慢）", Config.TimeoutSec))
	}
	if runErr != nil {
		msg := clip(stderr.String(), 800)
		if msg == "" {
			// 静默非零退出基本只有一个来源：包装脚本自己没解析过。把路径带上，
			// 否则操作员只拿到一句 "exit status 1"，无从下手。
			msg = runErr.Error() + "（pwsh 没有输出任何错误，包装脚本 " + wrapperPath() + " 可能未通过解析）"
		}
		msg = explainPwshError(msg, command)
		fmt.Printf("[ArgFuscator] generate failed for %s: %s\n", username, clip(msg, 300))
		return fail(400, "generate_failed", msg)
	}

	raw := strings.Trim(stdout.String(), "\r\n")
	if raw == "" {
		return fail(500, "empty_output", "pwsh 正常退出但没有任何输出")
	}
	parts := strings.Split(raw, variantSep)
	variants := make([]string, 0, len(parts))
	for _, p := range parts {
		// 只削掉变体首尾的换行；变体**内部**的换行是多行命令的一部分，必须留着
		if v := strings.Trim(p, "\r\n"); strings.TrimSpace(v) != "" {
			variants = append(variants, v)
		}
	}
	if len(variants) == 0 {
		return fail(500, "empty_output", "pwsh 输出里没有可用的变体")
	}

	resp, _ := json.Marshal(map[string]interface{}{
		"ok":       true,
		"count":    len(variants),
		"variants": variants,
		"ms":       ms,
	})
	return 200, resp
}
