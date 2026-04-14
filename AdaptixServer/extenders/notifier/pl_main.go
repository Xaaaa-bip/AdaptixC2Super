package main

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
)

type PluginService struct{}

var (
	Ts              Teamserver
	ModuleDir       string
	Config          NotifierConfig
	HooksRegistered bool
)

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = moduleDir

	if err := loadConfig(serviceConfig); err != nil {
		fmt.Printf("[Notifier] Config error: %v, using defaults\n", err)
		return &PluginService{}
	}

	updateHooks()

	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {
	switch function {
	case "get_config":
		getConfigHandler(operator)
	case "set_config":
		setConfigHandler(operator, args)
	case "test_message":
		testMessageHandler(operator)
	}
}

func getConfigHandler(operator string) {
	events := Config.Events
	if events == nil {
		events = make(map[string]EventConfig)
	}
	response := ConfigResponse{
		Action:   "config",
		Enabled:  Config.Enabled,
		Services: Config.Services,
		Events:   events,
	}
	data, _ := json.Marshal(response)
	Ts.TsServiceSendDataClient(operator, "notifier", string(data))
}

func setConfigHandler(operator string, args string) {
	var newConfig NotifierConfig
	if err := json.Unmarshal([]byte(args), &newConfig); err != nil {
		response := ResultResponse{Action: "save_result", Success: false, Error: err.Error()}
		data, _ := json.Marshal(response)
		Ts.TsServiceSendDataClient(operator, "notifier", string(data))
		return
	}

	Config.Enabled = newConfig.Enabled
	Config.Services = newConfig.Services
	if newConfig.Events != nil {
		Config.Events = newConfig.Events
	}

	updateHooks()

	if err := saveConfig(); err != nil {
		response := ResultResponse{Action: "save_result", Success: false, Error: err.Error()}
		data, _ := json.Marshal(response)
		Ts.TsServiceSendDataClient(operator, "notifier", string(data))
		return
	}

	response := ResultResponse{Action: "save_result", Success: true}
	data, _ := json.Marshal(response)
	Ts.TsServiceSendDataClient(operator, "notifier", string(data))

	configUpdate := ConfigResponse{
		Action:   "config_update",
		Enabled:  Config.Enabled,
		Services: Config.Services,
		Events:   Config.Events,
	}
	updateData, _ := json.Marshal(configUpdate)
	Ts.TsServiceSendDataAll("notifier", string(updateData))
}

func testMessageHandler(operator string) {
	var response ResultResponse
	if !hasAnyService() {
		response = ResultResponse{Action: "test_result", Success: false, Error: "No notification services configured"}
	} else {
		sendMessage("🔔 测试消息\nNotifier / 钉钉 配置正常")
		response = ResultResponse{Action: "test_result", Success: true}
	}
	data, _ := json.Marshal(response)
	Ts.TsServiceSendDataClient(operator, "notifier", string(data))
}

func hasAnyService() bool {
	if Config.Services.Telegram.Enabled && Config.Services.Telegram.BotToken != "" && len(Config.Services.Telegram.ChatIDs) > 0 {
		return true
	}
	if Config.Services.Slack.Enabled && Config.Services.Slack.WebhookURL != "" {
		return true
	}
	if Config.Services.Discord.Enabled && Config.Services.Discord.WebhookURL != "" {
		return true
	}
	if Config.Services.RocketChat.Enabled && Config.Services.RocketChat.WebhookURL != "" {
		return true
	}
	if Config.Services.DingTalk.Enabled && strings.TrimSpace(Config.Services.DingTalk.WebhookURL) != "" {
		return true
	}
	if Config.Services.Webhook.Enabled && Config.Services.Webhook.URL != "" {
		return true
	}
	return false
}

func registerHook(eventType string) {
	Ts.TsEventHookRegister(eventType, "notifier", HookPost, 50, func(event any) error {
		go handleEvent(eventType, event)
		return nil
	})
}

func updateHooks() {
	shouldBeRegistered := Config.Enabled && hasAnyService()

	if shouldBeRegistered && !HooksRegistered {
		for _, eventType := range NotifyEventTypes {
			registerHook(eventType)
		}
		HooksRegistered = true
		sendMessage("🟢 *AdaptixC2 Notifier started*\n ")
	} else if !shouldBeRegistered && HooksRegistered {
		Ts.TsEventHookUnregisterByName("notifier")
		HooksRegistered = false
	}
}

func handleEvent(eventType string, eventData any) {
	eventConfig, ok := Config.Events[eventType]
	if !ok || !eventConfig.Enabled || eventConfig.Template == "" {
		return
	}

	jsonBytes, err := json.Marshal(eventData)
	if err != nil {
		return
	}

	var raw map[string]interface{}
	err = json.Unmarshal(jsonBytes, &raw)
	if err != nil {
		return
	}

	message := ""

	switch eventType {
	case "agent.new", "agent.activate", "agent.remove":
		message = handleAgentEvent(eventConfig.Template, eventType, raw)
	case "agent.terminate":
		message = handleAgentTerminate(eventConfig.Template, raw)
	case "listener.start", "listener.stop":
		message = handleListenerEvent(eventConfig.Template, raw)
	case "task.complete":
		message = handleTaskEvent(eventConfig.Template, raw)
	case "download.finish":
		message = handleDownloadEvent(eventConfig.Template, raw)
	case "tunnel.start", "tunnel.stop":
		message = handleTunnelEvent(eventConfig.Template, eventType, raw)
	case "credentials.add":
		message = handleCredentialsEvent(eventConfig.Template, raw)
	case "target.add":
		message = handleTargetsEvent(eventConfig.Template, raw)
	default:
		message = ""
	}

	if message != "" {
		sendMessage(message)
	}
}

func handleAgentEvent(template string, eventType string, raw map[string]interface{}) string {
	jsonBytes, err := json.Marshal(raw["Agent"])
	if err != nil {
		return ""
	}

	var agentData adaptix.AgentData
	err = json.Unmarshal(jsonBytes, &agentData)
	if err != nil {
		return ""
	}

	message := strings.ReplaceAll(template, "{Id}", agentData.Id)
	message = strings.ReplaceAll(message, "{Username}", agentData.Username)
	message = strings.ReplaceAll(message, "{Computer}", agentData.Computer)
	message = strings.ReplaceAll(message, "{Domain}", agentData.Domain)

	if eventType == "agent.activate" || eventType == "agent.new" {
		message = strings.ReplaceAll(message, "{ExternalIP}", agentData.ExternalIP)
		message = strings.ReplaceAll(message, "{InternalIP}", agentData.InternalIP)
		if agentData.Elevated {
			message = strings.ReplaceAll(message, "{Elevated}", "Yes")
		} else {
			message = strings.ReplaceAll(message, "{Elevated}", "No")
		}

		if eventType == "agent.new" {
			message = strings.ReplaceAll(message, "{OsDesc}", agentData.OsDesc)
			message = strings.ReplaceAll(message, "{Process}", agentData.Process)
			message = strings.ReplaceAll(message, "{Arch}", agentData.Arch)
			message = strings.ReplaceAll(message, "{Pid}", agentData.Pid)
		}
	}
	return message
}

func handleAgentTerminate(template string, raw map[string]interface{}) string {
	agentId := raw["AgentId"].(string)
	taskId := raw["TaskId"].(string)

	message := strings.ReplaceAll(template, "{Id}", agentId)
	message = strings.ReplaceAll(message, "{TaskId}", taskId)
	return message
}

func handleListenerEvent(template string, raw map[string]interface{}) string {
	listenerName := raw["ListenerName"].(string)
	listenerType := raw["ListenerType"].(string)

	message := strings.ReplaceAll(template, "{Name}", listenerName)
	message = strings.ReplaceAll(message, "{Type}", listenerType)
	return message
}

func handleTaskEvent(template string, raw map[string]interface{}) string {
	jsonBytes, err := json.Marshal(raw["Task"])
	if err != nil {
		return ""
	}

	var taskData adaptix.TaskData
	err = json.Unmarshal(jsonBytes, &taskData)
	if err != nil {
		return ""
	}

	taskType := "Task"
	switch taskData.Type {
	case adaptix.TASK_TYPE_LOCAL:
		taskType = "Local"
	case adaptix.TASK_TYPE_TASK:
		taskType = "Task"
	case adaptix.TASK_TYPE_BROWSER:
		taskType = "Browser"
	case adaptix.TASK_TYPE_JOB:
		taskType = "Job"
	case adaptix.TASK_TYPE_TUNNEL:
		taskType = "Tunnel"
	case adaptix.TASK_TYPE_PROXY_DATA:
		taskType = "Local"
	default:
		taskType = "unknown type"
	}

	taskStatus := ""
	switch taskData.MessageType {
	case adaptix.MESSAGE_INFO:
		taskStatus = "Info"
	case adaptix.MESSAGE_ERROR:
		taskStatus = "Error"
	case adaptix.MESSAGE_SUCCESS:
		taskStatus = "Success"
	default:
		taskStatus = fmt.Sprintf("%d", taskData.MessageType)
	}

	message := strings.ReplaceAll(template, "{AgentId}", taskData.AgentId)
	message = strings.ReplaceAll(message, "{TaskId}", taskData.TaskId)
	message = strings.ReplaceAll(message, "{Computer}", taskData.Computer)
	message = strings.ReplaceAll(message, "{TaskType}", taskType)
	message = strings.ReplaceAll(message, "{Status}", taskStatus)
	message = strings.ReplaceAll(message, "{CommandLine}", taskData.CommandLine)
	message = strings.ReplaceAll(message, "{Message}", taskData.Message)

	return message
}

func handleDownloadEvent(template string, raw map[string]interface{}) string {
	canceled := raw["Canceled"].(bool)
	if canceled {
		return ""
	}

	jsonBytes, err := json.Marshal(raw["Download"])
	if err != nil {
		return ""
	}

	var downloadData adaptix.DownloadData
	err = json.Unmarshal(jsonBytes, &downloadData)
	if err != nil {
		return ""
	}

	const (
		KB = 1024
		MB = KB * 1024
		GB = MB * 1024
	)
	fileSize := ""
	switch {
	case downloadData.TotalSize >= GB:
		fileSize = fmt.Sprintf("%.2f GB", float64(downloadData.TotalSize)/float64(GB))
	case downloadData.TotalSize >= MB:
		fileSize = fmt.Sprintf("%.2f MB", float64(downloadData.TotalSize)/float64(MB))
	case downloadData.TotalSize >= KB:
		fileSize = fmt.Sprintf("%.2f KB", float64(downloadData.TotalSize)/float64(KB))
	default:
		fileSize = fmt.Sprintf("%d B", downloadData.TotalSize)
	}

	message := strings.ReplaceAll(template, "{AgentId}", downloadData.AgentId)
	message = strings.ReplaceAll(message, "{FileId}", downloadData.FileId)
	message = strings.ReplaceAll(message, "{Computer}", downloadData.Computer)
	message = strings.ReplaceAll(message, "{RemotePath}", downloadData.RemotePath)
	message = strings.ReplaceAll(message, "{Size}", fileSize)

	return message
}

func handleTunnelEvent(template string, eventType string, raw map[string]interface{}) string {
	agentId := raw["AgentId"].(string)
	tunnelId := raw["TunnelId"].(string)
	port := fmt.Sprintf("%d", int(raw["Port"].(float64)))

	message := strings.ReplaceAll(template, "{AgentId}", agentId)
	message = strings.ReplaceAll(message, "{TunnelId}", tunnelId)
	message = strings.ReplaceAll(message, "{Port}", port)

	if eventType == "tunnel.start" {
		info := raw["Info"].(string)
		tunType := ""
		switch int(raw["TunnelType"].(float64)) {
		case adaptix.TUNNEL_TYPE_SOCKS4:
			tunType = "Socks4"
		case adaptix.TUNNEL_TYPE_SOCKS5:
			tunType = "Socks5"
		case adaptix.TUNNEL_TYPE_SOCKS5_AUTH:
			tunType = "Socks5 Auth"
		case adaptix.TUNNEL_TYPE_LOCAL_PORT:
			tunType = "Local Port Forward"
		case adaptix.TUNNEL_TYPE_REVERSE:
			tunType = "Reverse Port Forward"
		default:
			tunType = "unkown type"
		}
		message = strings.ReplaceAll(message, "{TunnelType}", tunType)
		message = strings.ReplaceAll(message, "{Info}", info)
	}

	return message
}

func handleCredentialsEvent(template string, raw map[string]interface{}) string {
	jsonBytes, err := json.Marshal(raw["Credentials"])
	if err != nil {
		return ""
	}

	var creds []adaptix.CredsData
	err = json.Unmarshal(jsonBytes, &creds)
	if err != nil {
		return ""
	}
	if len(creds) == 0 {
		return ""
	}
	if len(creds) > BatchThreshold {
		return fmt.Sprintf("📋 *Credentials Added*: %d items", len(creds))
	}

	message := ""
	for _, credData := range creds {
		if message != "" {
			message += "\n\n"
		}
		item := template
		item = strings.ReplaceAll(item, "{Username}", credData.Username)
		item = strings.ReplaceAll(item, "{Password}", maskString(credData.Password))
		item = strings.ReplaceAll(item, "{Realm}", credData.Realm)
		item = strings.ReplaceAll(item, "{Type}", credData.Type)
		item = strings.ReplaceAll(item, "{Tag}", credData.Tag)
		item = strings.ReplaceAll(item, "{Storage}", credData.Storage)
		item = strings.ReplaceAll(item, "{Host}", credData.Host)

		message += item
	}

	return message
}

func handleTargetsEvent(template string, raw map[string]interface{}) string {
	jsonBytes, err := json.Marshal(raw["Targets"])
	if err != nil {
		return ""
	}

	var targets []adaptix.TargetData
	err = json.Unmarshal(jsonBytes, &targets)
	if err != nil {
		return ""
	}
	if len(targets) == 0 {
		return ""
	}
	if len(targets) > BatchThreshold {
		return fmt.Sprintf("📋 *Targets Added*: %d items", len(targets))
	}

	message := ""
	for _, targetData := range targets {
		if message != "" {
			message += "\n\n"
		}
		item := template
		item = strings.ReplaceAll(item, "{Computer}", targetData.Computer)
		item = strings.ReplaceAll(item, "{Domain}", targetData.Domain)
		item = strings.ReplaceAll(item, "{Address}", targetData.Address)
		item = strings.ReplaceAll(item, "{OsDesk}", targetData.OsDesk)
		item = strings.ReplaceAll(item, "{Tag}", targetData.Tag)
		item = strings.ReplaceAll(item, "{Info}", targetData.Info)

		message += item
	}

	return message
}

func sendMessage(text string) {
	if Config.Services.Telegram.Enabled {
		sendTelegram(text)
	}
	if Config.Services.Slack.Enabled {
		sendSlack(text)
	}
	if Config.Services.Discord.Enabled {
		sendDiscord(text)
	}
	if Config.Services.RocketChat.Enabled {
		sendRocketChat(text)
	}
	if Config.Services.DingTalk.Enabled {
		sendDingTalk(text)
	}
	if Config.Services.Webhook.Enabled {
		sendWebhook(text)
	}
}

func sendTelegram(text string) {
	token := Config.Services.Telegram.BotToken
	if token == "" || len(Config.Services.Telegram.ChatIDs) == 0 {
		return
	}
	apiURL := fmt.Sprintf("https://api.telegram.org/bot%s/sendMessage", token)

	for _, chatID := range Config.Services.Telegram.ChatIDs {
		if chatID == "" {
			continue
		}
		go func(cid string) {
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()

			payload := map[string]string{
				"chat_id":    cid,
				"text":       text,
				"parse_mode": "Markdown",
			}
			data, _ := json.Marshal(payload)

			req, err := http.NewRequestWithContext(ctx, "POST", apiURL, bytes.NewBuffer(data))
			if err != nil {
				return
			}
			req.Header.Set("Content-Type", "application/json")

			resp, err := http.DefaultClient.Do(req)
			if err != nil {
				return
			}
			defer func(Body io.ReadCloser) { _ = Body.Close() }(resp.Body)
		}(chatID)
	}
}

func sendSlack(text string) {
	webhookURL := Config.Services.Slack.WebhookURL
	if webhookURL == "" {
		return
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		payload := map[string]string{"text": text}
		data, _ := json.Marshal(payload)

		req, err := http.NewRequestWithContext(ctx, "POST", webhookURL, bytes.NewBuffer(data))
		if err != nil {
			return
		}
		req.Header.Set("Content-Type", "application/json")

		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			return
		}
		defer func(Body io.ReadCloser) { _ = Body.Close() }(resp.Body)
	}()
}

func sendDiscord(text string) {
	webhookURL := Config.Services.Discord.WebhookURL
	if webhookURL == "" {
		return
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		payload := map[string]string{"content": text}
		data, _ := json.Marshal(payload)

		req, err := http.NewRequestWithContext(ctx, "POST", webhookURL, bytes.NewBuffer(data))
		if err != nil {
			return
		}
		req.Header.Set("Content-Type", "application/json")

		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			return
		}
		defer func(Body io.ReadCloser) { _ = Body.Close() }(resp.Body)
	}()
}

func sendRocketChat(text string) {
	webhookURL := Config.Services.RocketChat.WebhookURL
	if webhookURL == "" {
		return
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		payload := map[string]string{"text": text}
		data, _ := json.Marshal(payload)

		req, err := http.NewRequestWithContext(ctx, "POST", webhookURL, bytes.NewBuffer(data))
		if err != nil {
			return
		}
		req.Header.Set("Content-Type", "application/json")

		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			return
		}
		defer func(Body io.ReadCloser) { _ = Body.Close() }(resp.Body)
	}()
}

// sendDingTalk 钉钉 text 类型消息；加签算法见官方文档
func sendDingTalk(text string) {
	raw := strings.TrimSpace(Config.Services.DingTalk.WebhookURL)
	if raw == "" {
		return
	}
	webhook := raw
	if !strings.HasPrefix(strings.ToLower(webhook), "http") {
		webhook = "https://oapi.dingtalk.com/robot/send?access_token=" + webhook
	}

	u, err := url.Parse(webhook)
	if err != nil {
		return
	}
	q := u.Query()
	if sec := strings.TrimSpace(Config.Services.DingTalk.Secret); sec != "" {
		ts := time.Now().UnixMilli()
		q.Set("timestamp", fmt.Sprintf("%d", ts))
		stringToSign := fmt.Sprintf("%d\n%s", ts, sec)
		mac := hmac.New(sha256.New, []byte(sec))
		_, _ = mac.Write([]byte(stringToSign))
		sign := url.QueryEscape(base64.StdEncoding.EncodeToString(mac.Sum(nil)))
		q.Set("sign", sign)
	}
	u.RawQuery = q.Encode()
	finalURL := u.String()

	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		payload := map[string]interface{}{
			"msgtype": "text",
			"text": map[string]string{
				"content": dingTalkStripMarkdown(text),
			},
		}
		data, err := json.Marshal(payload)
		if err != nil {
			return
		}

		req, err := http.NewRequestWithContext(ctx, "POST", finalURL, bytes.NewBuffer(data))
		if err != nil {
			return
		}
		req.Header.Set("Content-Type", "application/json; charset=utf-8")

		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			return
		}
		defer func(Body io.ReadCloser) { _ = Body.Close() }(resp.Body)
		body, _ := io.ReadAll(resp.Body)
		if resp.StatusCode < 200 || resp.StatusCode >= 300 {
			fmt.Printf("[Notifier] DingTalk HTTP %d: %s\n", resp.StatusCode, string(body))
		}
	}()
}

// dingTalkStripMarkdown 钉钉 text 不按 Markdown 渲染，去掉常见 Telegram 风格符号便于阅读
func dingTalkStripMarkdown(s string) string {
	s = strings.ReplaceAll(s, "*", "")
	s = strings.ReplaceAll(s, "`", "")
	return s
}

func sendWebhook(text string) {
	cfg := Config.Services.Webhook
	if cfg.URL == "" {
		return
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		body := text
		if cfg.Data != "" {
			body = strings.ReplaceAll(cfg.Data, "%data%", text)
		}

		method := cfg.Method
		if method == "" {
			method = "POST"
		}

		req, err := http.NewRequestWithContext(ctx, method, cfg.URL, strings.NewReader(body))
		if err != nil {
			return
		}

		for key, value := range cfg.Headers {
			req.Header.Set(key, value)
		}
		if _, hasContentType := cfg.Headers["Content-Type"]; !hasContentType {
			req.Header.Set("Content-Type", "text/plain")
		}

		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			return
		}
		defer func(Body io.ReadCloser) { _ = Body.Close() }(resp.Body)
	}()
}
