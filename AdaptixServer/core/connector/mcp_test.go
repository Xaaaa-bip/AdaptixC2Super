package connector

/* MCP 端点本地测试（不依赖插件/网络/teamserver 实体）。
 *
 * Teamserver 是 interface —— mock 通过**嵌入接口**实现：没被调用的方法
 * 保持 nil-panic（不会走到），只实现本测试触达的几个。
 * 覆盖：initialize 版本协商 / notifications 202 / tools/list 数量与 schema /
 *       tools/call（server_status、list_agents、agent_command、未知 tool 报错）/
 *       ping。 */

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
)

// ---- mock ----

type mockTeamserver struct {
	Teamserver // 嵌入接口：未实现的方法调用即 panic（测试不应触达）
	agents     string
	listeners  string
}

func (m *mockTeamserver) TsAgentList() (string, error) {
	return m.agents, nil
}

func (m *mockTeamserver) TsListenerList() (string, error) {
	return m.listeners, nil
}

func (m *mockTeamserver) TsAgentIsExists(agentId string) bool { return agentId == "aabbccdd" }

func (m *mockTeamserver) TsAxScriptParseAndExecute(agentId, username, cmdline string) error {
	return nil
}

func (m *mockTeamserver) TsTaskListCompleted(agentId string, limit, offset int) ([]byte, error) {
	return []byte(`[{"a_cmdline":"whoami","a_completed":true,"a_text":"root"}]`), nil
}

func (m *mockTeamserver) TsAxScriptCommands() (string, error) {
	return `[]`, nil
}

func (m *mockTeamserver) TsTunnelList() (string, error) { return `[]`, nil }

func newMcpTestServer() *gin.Engine {
	gin.SetMode(gin.TestMode)
	tc := &TsConnector{teamserver: &mockTeamserver{
		agents:    `[{"a_id":"aabbccdd","a_computer":"TEST-PC","a_os":1,"a_last_tick":0}]`,
		listeners: `[{"l_name":"L1","l_reg_name":"BeaconHTTP","l_status":"Listen"}]`,
	}}
	r := gin.New()
	r.POST("/api/mcp", tc.TcMcpPost)
	r.GET("/api/mcp", tc.TcMcpGet)
	return r
}

func mcpPost(t *testing.T, r *gin.Engine, body string) (int, map[string]any) {
	req := httptest.NewRequest(http.MethodPost, "/api/mcp", bytes.NewBufferString(body))
	req.Header.Set("Content-Type", "application/json")
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	if w.Code == http.StatusAccepted {
		return w.Code, nil // notification：空 body
	}
	var out map[string]any
	if err := json.Unmarshal(w.Body.Bytes(), &out); err != nil {
		t.Fatalf("response not JSON (%d): %s", w.Code, w.Body.String())
	}
	return w.Code, out
}

// ---- tests ----

func TestMcpInitialize(t *testing.T) {
	r := newMcpTestServer()
	code, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}`)
	if code != 200 {
		t.Fatalf("initialize status %d", code)
	}
	res := out["result"].(map[string]any)
	if res["protocolVersion"] != "2025-06-18" {
		t.Fatalf("version echo failed: %v", res["protocolVersion"])
	}
	info := res["serverInfo"].(map[string]any)
	if info["name"] != "adaptixc2" {
		t.Fatalf("serverInfo wrong: %v", info)
	}
}

func TestMcpInitializeLegacyVersion(t *testing.T) {
	r := newMcpTestServer()
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26"}}`)
	res := out["result"].(map[string]any)
	if res["protocolVersion"] != "2025-03-26" {
		t.Fatalf("legacy version should echo: %v", res["protocolVersion"])
	}
	// 未知版本 → 服务器自己的
	_, out = mcpPost(t, r, `{"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"1999-01-01"}}`)
	res = out["result"].(map[string]any)
	if res["protocolVersion"] != "2025-06-18" {
		t.Fatalf("unknown version should fall back: %v", res["protocolVersion"])
	}
}

func TestMcpNotification(t *testing.T) {
	r := newMcpTestServer()
	code, _ := mcpPost(t, r, `{"jsonrpc":"2.0","method":"notifications/initialized"}`)
	if code != http.StatusAccepted {
		t.Fatalf("notification should 202, got %d", code)
	}
}

func TestMcpPing(t *testing.T) {
	r := newMcpTestServer()
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":7,"method":"ping"}`)
	if _, ok := out["result"]; !ok {
		t.Fatalf("ping should return result: %v", out)
	}
}

func TestMcpToolsList(t *testing.T) {
	r := newMcpTestServer()
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":2,"method":"tools/list"}`)
	tools := out["result"].(map[string]any)["tools"].([]any)
	if len(tools) < 28 {
		t.Fatalf("expected >=28 tools, got %d", len(tools))
	}
	// 每个 tool 必须有 name/description/inputSchema(object)
	seen := map[string]bool{}
	for _, raw := range tools {
		tl := raw.(map[string]any)
		name, _ := tl["name"].(string)
		if name == "" || seen[name] {
			t.Fatalf("bad/duplicate tool name: %v", name)
		}
		seen[name] = true
		if tl["description"] == nil {
			t.Fatalf("tool %s missing description", name)
		}
		schema, _ := tl["inputSchema"].(map[string]any)
		if schema == nil || schema["type"] != "object" {
			t.Fatalf("tool %s bad inputSchema: %v", name, tl["inputSchema"])
		}
	}
	for _, want := range []string{"server_status", "list_agents", "agent_command", "agent_tasks", "list_listeners", "listener_create", "listener_stop", "generate_payload", "file_list", "file_read", "file_download", "process_list", "process_kill", "bof_execute", "bofs_hint", "command_tree", "build_schema", "downloads_list", "download_get", "screenshots_list", "screenshot_get", "agent_set_tag", "agent_remove", "tunnels_list", "tunnel_create_socks5", "credentials_list", "targets_list", "chat_send"} {
		if !seen[want] {
			t.Fatalf("missing tool: %s", want)
		}
	}
}

func TestMcpCallServerStatus(t *testing.T) {
	r := newMcpTestServer()
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"server_status","arguments":{}}}`)
	res := out["result"].(map[string]any)
	if res["isError"] == true {
		t.Fatalf("server_status errored: %v", res)
	}
	text := res["content"].([]any)[0].(map[string]any)["text"].(string)
	var st map[string]any
	if err := json.Unmarshal([]byte(text), &st); err != nil {
		t.Fatalf("server_status text not JSON: %v", text)
	}
	if st["agents_total"] != float64(1) || st["listeners"] != float64(1) {
		t.Fatalf("server_status counts wrong: %v", st)
	}
}

func TestMcpCallAgentCommand(t *testing.T) {
	r := newMcpTestServer()
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"agent_command","arguments":{"agent_id":"aabbccdd","cmdline":"whoami"}}}`)
	res := out["result"].(map[string]any)
	if res["isError"] == true {
		t.Fatalf("agent_command errored: %v", res)
	}
	// 不存在的 agent → isError
	_, out = mcpPost(t, r, `{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"agent_command","arguments":{"agent_id":"ffff","cmdline":"whoami"}}}`)
	res = out["result"].(map[string]any)
	if res["isError"] != true {
		t.Fatalf("unknown agent should be isError")
	}
}

func TestMcpCallFileOps(t *testing.T) {
	r := newMcpTestServer()
	// file_list 默认路径
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"file_list","arguments":{"agent_id":"aabbccdd"}}}`)
	res := out["result"].(map[string]any)
	if res["isError"] == true {
		t.Fatalf("file_list errored: %v", res)
	}
	// 带空格路径 → 引号包裹
	_, out = mcpPost(t, r, `{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"file_list","arguments":{"agent_id":"aabbccdd","path":"C:\\Program Files"}}}`)
	res = out["result"].(map[string]any)
	text := res["content"].([]any)[0].(map[string]any)["text"].(string)
	if !bytesContains(text, `"C:\Program Files"`) {
		t.Fatalf("quoted path missing in: %v", text)
	}
	// bof_execute 拼 cmdline
	_, out = mcpPost(t, r, `{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"bof_execute","arguments":{"agent_id":"aabbccdd","bof_path":"/k/whoami.x64.o","args":"-a"}}}`)
	res = out["result"].(map[string]any)
	text = res["content"].([]any)[0].(map[string]any)["text"].(string)
	if !bytesContains(text, "execute bof /k/whoami.x64.o -a") {
		t.Fatalf("bof cmdline wrong: %v", text)
	}
}

func TestMcpCallUnknownTool(t *testing.T) {
	r := newMcpTestServer()
	_, out := mcpPost(t, r, `{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"nope","arguments":{}}}`)
	errObj, ok := out["error"].(map[string]any)
	if !ok || errObj["code"] != float64(-32602) {
		t.Fatalf("unknown tool should -32602: %v", out)
	}
}

func TestMcpGet405(t *testing.T) {
	r := newMcpTestServer()
	req := httptest.NewRequest(http.MethodGet, "/api/mcp", nil)
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("GET should 405, got %d", w.Code)
	}
}

func bytesContains(s, sub string) bool {
	return len(s) >= len(sub) && (func() bool {
		for i := 0; i+len(sub) <= len(s); i++ {
			if s[i:i+len(sub)] == sub {
				return true
			}
		}
		return false
	})()
}
