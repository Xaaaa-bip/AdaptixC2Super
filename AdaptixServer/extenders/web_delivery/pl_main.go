package main

import (
	"encoding/json"
	"fmt"

	adaptix "github.com/Adaptix-Framework/axc2"
)

// Teamserver interface for Service plugins.
type Teamserver interface {
	TsExtenderDataSave(extenderName string, key string, value []byte) error
	TsExtenderDataLoad(extenderName string, key string) ([]byte, error)
	TsExtenderDataDelete(extenderName string, key string) error
	TsExtenderDataKeys(extenderName string) ([]string, error)

	TsServiceSendDataAll(service string, data string)
	TsServiceSendDataClient(operator string, service string, data string)
}

// PluginService implements adaptix.PluginService.
type PluginService struct{}

var (
	Ts             Teamserver
	ModuleDir      string
	AgentWatermark string

	server  *DeliveryServer
	builder *AgentBuilder
	config  DeliveryConfig
)

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = moduleDir

	// Default watermark, same as gopher_agent
	AgentWatermark = "904e5493"

	// Load saved config
	loadSavedConfig()

	// Initialize builder
	builder = NewAgentBuilder(moduleDir, AgentWatermark)

	fmt.Println("[WebDelivery] Plugin initialized")
	return &PluginService{}
}

// Call dispatches client commands.
func (p *PluginService) Call(operator string, function string, args string) {
	switch function {
	case "start":
		handleStart(operator, args)
	case "stop":
		handleStop(operator)
	case "status":
		handleStatus(operator)
	case "get_config":
		handleGetConfig(operator)
	case "set_config":
		handleSetConfig(operator, args)
	case "generate_key":
		handleGenerateKey(operator)
	case "generate_token":
		handleGenerateToken(operator)
	case "get_oneliners":
		handleGetOneliners(operator)
	case "get_logs":
		handleGetLogs(operator)
	default:
		sendError(operator, fmt.Sprintf("unknown function: %s", function))
	}
}

func handleStart(operator string, args string) {
	if server != nil && server.IsActive() {
		sendError(operator, "delivery server is already running")
		return
	}

	var cfg DeliveryConfig
	if err := json.Unmarshal([]byte(args), &cfg); err != nil {
		sendError(operator, fmt.Sprintf("invalid config: %v", err))
		return
	}

	if cfg.PortBind < 1 || cfg.PortBind > 65535 {
		sendError(operator, "port_bind must be 1-65535")
		return
	}
	if cfg.EncryptKey == "" || len(cfg.EncryptKey) != 32 {
		sendError(operator, "encrypt_key must be 32 hex characters")
		return
	}
	if cfg.CallbackAddresses == "" {
		sendError(operator, "callback_addresses is required")
		return
	}
	if cfg.DeliveryToken == "" {
		token, err := GenerateRandomToken()
		if err != nil {
			sendError(operator, fmt.Sprintf("failed to generate token: %v", err))
			return
		}
		cfg.DeliveryToken = token
	}
	if len(cfg.Platforms) == 0 {
		cfg.Platforms = []string{"windows/amd64", "linux/amd64"}
	}
	if cfg.DefaultPlatform == "" {
		cfg.DefaultPlatform = "windows/amd64"
	}
	if cfg.ReconnectTimeout == "" {
		cfg.ReconnectTimeout = "30s"
	}
	if cfg.ReconnectCount == 0 {
		cfg.ReconnectCount = 10
	}
	if cfg.ResponseHeaders == nil {
		cfg.ResponseHeaders = make(map[string]string)
	}

	builder = NewAgentBuilder(ModuleDir, AgentWatermark)

	if !builder.HasSource() {
		sendError(operator, "gopher agent source not found (src_gopher directory missing)")
		return
	}

	sendAction(operator, "build_progress", map[string]interface{}{
		"status": "building",
	})

	err := builder.BuildAll(cfg, func(platform string, status string) {
		data, _ := json.Marshal(map[string]interface{}{
			"action":   "build_progress",
			"platform": platform,
			"status":   status,
		})
		Ts.TsServiceSendDataAll("web_delivery", string(data))
	})

	if err != nil {
		sendError(operator, fmt.Sprintf("build failed: %v", err))
		return
	}

	server = NewDeliveryServer(cfg, builder)
	if err := server.Start(); err != nil {
		sendError(operator, fmt.Sprintf("failed to start server: %v", err))
		return
	}

	config = cfg
	config.Active = true
	saveConfig()

	scheme := "http"
	if cfg.UseSSL {
		scheme = "https"
	}
	dlURL := fmt.Sprintf("%s://%s:%d/dl/%s", scheme, cfg.HostBind, cfg.PortBind, cfg.DeliveryToken)

	data, _ := json.Marshal(map[string]interface{}{
		"action":    "start_result",
		"success":   true,
		"url":       dlURL,
		"oneliners": server.GetOneLiners(),
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))

	broadcastStatus()
}

func handleStop(operator string) {
	if server == nil || !server.IsActive() {
		sendError(operator, "delivery server is not running")
		return
	}

	if err := server.Stop(); err != nil {
		sendError(operator, fmt.Sprintf("failed to stop server: %v", err))
		return
	}

	config.Active = false
	saveConfig()

	data, _ := json.Marshal(map[string]interface{}{
		"action":  "stop_result",
		"success": true,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))

	broadcastStatus()
}

func handleStatus(operator string) {
	status := "stopped"
	dlURL := ""
	var oneliners map[string]string

	if server != nil && server.IsActive() {
		status = "running"
		scheme := "http"
		if config.UseSSL {
			scheme = "https"
		}
		dlURL = fmt.Sprintf("%s://%s:%d/dl/%s", scheme, config.HostBind, config.PortBind, config.DeliveryToken)
		oneliners = server.GetOneLiners()
	}

	data, _ := json.Marshal(map[string]interface{}{
		"action":    "status",
		"status":    status,
		"url":       dlURL,
		"oneliners": oneliners,
		"config":    config,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func handleGetConfig(operator string) {
	data, _ := json.Marshal(map[string]interface{}{
		"action": "config",
		"config": config,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func handleSetConfig(operator string, args string) {
	var newConfig DeliveryConfig
	if err := json.Unmarshal([]byte(args), &newConfig); err != nil {
		sendError(operator, fmt.Sprintf("invalid config: %v", err))
		return
	}

	if server != nil && server.IsActive() {
		sendError(operator, "cannot change config while server is running")
		return
	}

	config = newConfig
	config.Active = false
	saveConfig()

	data, _ := json.Marshal(map[string]interface{}{
		"action":  "config_saved",
		"success": true,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func handleGenerateKey(operator string) {
	key, err := GenerateRandomHexKey()
	if err != nil {
		sendError(operator, fmt.Sprintf("failed to generate key: %v", err))
		return
	}

	data, _ := json.Marshal(map[string]interface{}{
		"action":      "key_generated",
		"encrypt_key": key,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func handleGenerateToken(operator string) {
	token, err := GenerateRandomToken()
	if err != nil {
		sendError(operator, fmt.Sprintf("failed to generate token: %v", err))
		return
	}

	data, _ := json.Marshal(map[string]interface{}{
		"action":         "token_generated",
		"delivery_token": token,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func handleGetOneliners(operator string) {
	if server == nil || !server.IsActive() {
		sendError(operator, "delivery server is not running")
		return
	}

	data, _ := json.Marshal(map[string]interface{}{
		"action":    "oneliners",
		"oneliners": server.GetOneLiners(),
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func handleGetLogs(operator string) {
	var logs []AccessLog
	if server != nil {
		logs = server.GetLogs()
	} else {
		logs = make([]AccessLog, 0)
	}

	data, _ := json.Marshal(map[string]interface{}{
		"action": "logs",
		"logs":   logs,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

// Helper functions

func sendError(operator string, message string) {
	data, _ := json.Marshal(map[string]interface{}{
		"action":  "error",
		"message": message,
	})
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func sendAction(operator string, action string, payload map[string]interface{}) {
	payload["action"] = action
	data, _ := json.Marshal(payload)
	Ts.TsServiceSendDataClient(operator, "web_delivery", string(data))
}

func broadcastStatus() {
	status := "stopped"
	dlURL := ""
	if server != nil && server.IsActive() {
		status = "running"
		scheme := "http"
		if config.UseSSL {
			scheme = "https"
		}
		dlURL = fmt.Sprintf("%s://%s:%d/dl/%s", scheme, config.HostBind, config.PortBind, config.DeliveryToken)
	}

	data, _ := json.Marshal(map[string]interface{}{
		"action": "status_update",
		"status": status,
		"url":    dlURL,
	})
	Ts.TsServiceSendDataAll("web_delivery", string(data))
}

func loadSavedConfig() {
	data, err := Ts.TsExtenderDataLoad("web_delivery", "config")
	if err == nil && data != nil {
		_ = json.Unmarshal(data, &config)
	}

	if config.HostBind == "" {
		config.HostBind = "0.0.0.0"
	}
	if config.PortBind == 0 {
		config.PortBind = 8888
	}
	if config.ReconnectTimeout == "" {
		config.ReconnectTimeout = "30s"
	}
	if config.ReconnectCount == 0 {
		config.ReconnectCount = 10
	}
	if config.DefaultPlatform == "" {
		config.DefaultPlatform = "windows/amd64"
	}
	if len(config.Platforms) == 0 {
		config.Platforms = []string{"windows/amd64", "windows/arm64", "linux/amd64", "linux/arm64", "darwin/amd64", "darwin/arm64"}
	}
	if config.ResponseHeaders == nil {
		config.ResponseHeaders = make(map[string]string)
	}
}

func saveConfig() {
	data, err := json.Marshal(&config)
	if err != nil {
		return
	}
	_ = Ts.TsExtenderDataSave("web_delivery", "config", data)
}
