package main

import (
	"encoding/json"
	"fmt"

	"gopkg.in/yaml.v3"
)

const (
	HookPre = 0
	HookPost = 1
)

var NotifyEventTypes = []string{
	"agent.new",
	"agent.activate",
	"agent.terminate",
	"agent.remove",
	"listener.start",
	"listener.stop",
	"task.complete",
	"download.finish",
	"tunnel.start",
	"tunnel.stop",
	"credentials.add",
	"target.add",
}

const BatchThreshold = 3

type EventConfig struct {
	Enabled  bool   `yaml:"enabled" json:"enabled"`
	Template string `yaml:"template" json:"template"`
}

type TelegramService struct {
	Enabled   bool     `yaml:"enabled" json:"enabled"`
	BotToken  string   `yaml:"bot_token" json:"bot_token"`
	ChatIDs   []string `yaml:"chat_ids" json:"chat_ids"`
}

type SlackService struct {
	Enabled    bool   `yaml:"enabled" json:"enabled"`
	WebhookURL string `yaml:"webhook_url" json:"webhook_url"`
}

type DiscordService struct {
	Enabled    bool   `yaml:"enabled" json:"enabled"`
	WebhookURL string `yaml:"webhook_url" json:"webhook_url"`
}

type RocketChatService struct {
	Enabled    bool   `yaml:"enabled" json:"enabled"`
	WebhookURL string `yaml:"webhook_url" json:"webhook_url"`
}

// DingTalkService 钉钉自定义机器人：https://open.dingtalk.com/document/orgapp/custom-robot-access
// WebhookURL 填完整地址 https://oapi.dingtalk.com/robot/send?access_token=xxx 或仅 access_token
// Secret 若机器人在安全设置中启用了「加签」，填写 SEC 开头的密钥
type DingTalkService struct {
	Enabled    bool   `yaml:"enabled" json:"enabled"`
	WebhookURL string `yaml:"webhook_url" json:"webhook_url"`
	Secret     string `yaml:"secret" json:"secret"`
}

type WebhookService struct {
	Enabled bool              `yaml:"enabled" json:"enabled"`
	URL     string            `yaml:"url" json:"url"`
	Method  string            `yaml:"method" json:"method"`
	Headers map[string]string `yaml:"headers" json:"headers"`
	Data    string            `yaml:"data" json:"data"`
}

type ServicesConfig struct {
	Telegram   TelegramService   `yaml:"telegram" json:"telegram"`
	Slack      SlackService      `yaml:"slack" json:"slack"`
	Discord    DiscordService    `yaml:"discord" json:"discord"`
	RocketChat RocketChatService `yaml:"rocketchat" json:"rocketchat"`
	DingTalk   DingTalkService   `yaml:"dingtalk" json:"dingtalk"`
	Webhook    WebhookService    `yaml:"webhook" json:"webhook"`
}

type NotifierConfig struct {
	RestoreDatabase bool                      `yaml:"restore_database" json:"restore_database"`
	Enabled         bool                      `yaml:"enabled" json:"enabled"`
	Services        ServicesConfig            `yaml:"services" json:"services"`
	Events          map[string]EventConfig    `yaml:"events" json:"events"`
}

type ConfigResponse struct {
	Action  string         `json:"action"`
	Enabled bool           `json:"enabled"`
	Services ServicesConfig `json:"services"`
	Events  map[string]EventConfig `json:"events"`
}

type ResultResponse struct {
	Action  string `json:"action"`
	Success bool   `json:"success"`
	Error   string `json:"error,omitempty"`
}

type Teamserver interface {
	TsEventHookRegister(eventType string, name string, phase int, priority int, handler func(event any) error) string
	TsEventHookUnregisterByName(name string) int
	TsServiceSendDataAll(service string, data string)
	TsServiceSendDataClient(operator string, service string, data string)
	TsExtenderDataSave(extenderName string, key string, value []byte) error
	TsExtenderDataLoad(extenderName string, key string) ([]byte, error)
}

func loadConfig(serviceConfig string) error {
	if serviceConfig == "" {
		return fmt.Errorf("empty service config")
	}

	err := yaml.Unmarshal([]byte(serviceConfig), &Config)
	if err != nil {
		return err
	}

	if Config.RestoreDatabase {
		data, err := Ts.TsExtenderDataLoad("notifier", "config")
		if err == nil && data != nil {
			var dbConfig NotifierConfig
			if json.Unmarshal(data, &dbConfig) == nil {
				Config = dbConfig
				fmt.Println("[Notifier] Config restored from database")
				return nil
			}
		}
		fmt.Println("[Notifier] No saved config in database, using service config")
	}

	return nil
}

func saveConfig() error {
	data, err := json.Marshal(&Config)
	if err != nil {
		return err
	}
	return Ts.TsExtenderDataSave("notifier", "config", data)
}

func maskString(s string) string {
	if len(s) == 0 {
		return "*"
	}
	if len(s) <= 2 {
		return s
	}
	rs := []rune(s)
	for i := 1; i < len(rs)-1; i++ {
		rs[i] = '*'
	}
	return string(rs)
}
