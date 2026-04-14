package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"math/rand/v2"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	adaptix "github.com/Adaptix-Framework/axc2"
)

// 仅允许纯文件名（服务端 bofs/ 目录下），防止路径穿越
var bofNamePat = regexp.MustCompile(`(?i)^[a-z0-9][a-z0-9._-]*\.(o|obj)$`)

type PluginService struct{}

// 与 notifier、beacon_agent 等扩展一致：框架传入的 moduleDir 为「.so 所在目录」
// （见 AdaptixServer/core/extender/extender.go LoadPluginService: filepath.Dir(plugin_path)）
var (
	Ts        Teamserver
	ModuleDir string
)

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = filepath.Clean(moduleDir)
	_ = serviceConfig
	return &PluginService{}
}

type runBofArgs struct {
	AgentID   string `json:"agent_id"`
	BofBase64 string `json:"bof_base64"`
	Async     bool   `json:"async"`
}

type runServerBofArgs struct {
	AgentID string `json:"agent_id"`
	BofName string `json:"bof_name"`
	Async   bool   `json:"async"`
}

type resultResponse struct {
	Action  string `json:"action"`
	Success bool   `json:"success"`
	Error   string `json:"error,omitempty"`
	TaskID  string `json:"task_id,omitempty"`
	AgentID string `json:"agent_id,omitempty"`
}

func (p *PluginService) Call(operator string, function string, args string) {
	switch function {
	case "run_bof":
		runBofHandler(operator, args)
	case "run_server_bof":
		runServerBofHandler(operator, args)
	default:
	}
}

func sendRunResult(operator string, inAgentID string, ok bool, errMsg string, taskID string) {
	r := resultResponse{Action: "run_result", Success: ok, AgentID: inAgentID, TaskID: taskID}
	if !ok {
		r.Error = errMsg
	}
	b, _ := json.Marshal(r)
	Ts.TsServiceSendDataClient(operator, "pillager_bridge", string(b))
}

func queueBofOnAgent(agentID string, raw []byte, async bool, operator string) (taskID string, err error) {
	taskID = fmt.Sprintf("%08x", rand.Uint32())

	params := []byte{0, 0, 0, 0}
	// The packing order MUST match src_beacon/beacon/Commander.cpp:Commander_CmdExecBof
	// Order: COMMAND_ID, async (byte), entry (string/bytes), bof (bytes), args (bytes)
	// TaskId is automatically appended by the Teamserver core in TsTaskCreate
	inner, err := PackArray([]interface{}{
		COMMAND_EXEC_BOF,
		async,
		"go",
		int(len(raw)),
		raw,
		int(len(params)),
		params,
	})
	if err != nil {
		return "", err
	}

	task := adaptix.TaskData{
		Type:    adaptix.TASK_TYPE_JOB,
		TaskId:  taskID,
		AgentId: agentID,
		Sync:    false,
		Data:    inner,
	}

	Ts.TsTaskCreate(agentID, "", "", task)
	Ts.TsAgentConsoleOutput(agentID, adaptix.MESSAGE_INFO, "pillager_bridge",
		fmt.Sprintf("[+] Pillager Bridge: BOF 已排队 task=%s async=%v (%d bytes)\n"+
			"[*] 文本输出见本 Beacon 控制台；大文件回传请在团队端「下载」查看。",
			taskID, async, len(raw)), true)
	return taskID, nil
}

func runBofHandler(operator string, args string) {
	var in runBofArgs
	if err := json.Unmarshal([]byte(args), &in); err != nil {
		sendRunResult(operator, "", false, "invalid JSON: "+err.Error(), "")
		return
	}
	if in.AgentID == "" {
		sendRunResult(operator, "", false, "agent_id is required", "")
		return
	}
	raw, err := base64.StdEncoding.DecodeString(in.BofBase64)
	if err != nil || len(raw) == 0 {
		sendRunResult(operator, in.AgentID, false, "bof_base64: invalid or empty", "")
		return
	}
	taskID, err := queueBofOnAgent(in.AgentID, raw, in.Async, operator)
	if err != nil {
		sendRunResult(operator, in.AgentID, false, err.Error(), "")
		return
	}
	sendRunResult(operator, in.AgentID, true, "", taskID)
}

func runServerBofHandler(operator string, args string) {
	var in runServerBofArgs
	if err := json.Unmarshal([]byte(args), &in); err != nil {
		sendRunResult(operator, "", false, "invalid JSON: "+err.Error(), "")
		return
	}
	if in.AgentID == "" {
		sendRunResult(operator, "", false, "agent_id is required", "")
		return
	}
	base := filepath.Base(strings.TrimSpace(in.BofName))
	if base == "." || base == string(filepath.Separator) {
		sendRunResult(operator, in.AgentID, false, "bof_name is required", "")
		return
	}
	if !bofNamePat.MatchString(base) {
		sendRunResult(operator, in.AgentID, false, "bof_name 只能是简单文件名，例如 pillager_browser.x64.o", "")
		return
	}
	if ModuleDir == "" {
		sendRunResult(operator, in.AgentID, false, "module dir unknown", "")
		return
	}
	full := filepath.Join(ModuleDir, "bofs", base)
	st, err := os.Stat(full)
	if err != nil || st.IsDir() {
		sendRunResult(operator, in.AgentID, false,
			"服务端无此 BOF：extenders/pillager_bridge/bofs/"+base+
				"（请在构建机执行 make extenders 以从 beacon_agent 打包 .o）", "")
		return
	}
	raw, err := os.ReadFile(full)
	if err != nil || len(raw) == 0 {
		sendRunResult(operator, in.AgentID, false, "read BOF: "+err.Error(), "")
		return
	}
	taskID, err := queueBofOnAgent(in.AgentID, raw, in.Async, operator)
	if err != nil {
		sendRunResult(operator, in.AgentID, false, err.Error(), "")
		return
	}
	sendRunResult(operator, in.AgentID, true, "", taskID)
}
