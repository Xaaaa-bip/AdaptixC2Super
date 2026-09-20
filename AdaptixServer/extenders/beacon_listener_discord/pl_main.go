package main

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"regexp"

	adaptix "github.com/Adaptix-Framework/axc2"
)

type Teamserver interface {
	TsAgentIsExists(agentId string) bool
	TsAgentCreate(agentCrc string, agentId string, beat []byte, listenerName string, ExternalIP string, Async bool) (adaptix.AgentData, error)
	TsAgentProcessData(agentId string, bodyData []byte) error
	TsAgentSetTick(agentId string, listenerName string) error
	TsAgentGetHostedAll(agentId string, maxDataSize int) ([]byte, error)
}

type PluginListener struct{}

var (
	ModuleDir       string
	ListenerDataDir string
	Ts              Teamserver
)

func InitPlugin(ts any, moduleDir string, listenerDir string) adaptix.PluginListener {
	ModuleDir = moduleDir
	ListenerDataDir = listenerDir
	Ts = ts.(Teamserver)
	return &PluginListener{}
}

func (p *PluginListener) Create(name string, config string, customData []byte) (adaptix.ExtenderListener, adaptix.ListenerData, []byte, error) {
	var (
		listener     *Listener
		listenerData adaptix.ListenerData
		conf         ConfigDiscord
		customdData  []byte
		err          error
	)

	/// START CODE HERE

	if customData == nil {
		if err = validConfig(config); err != nil {
			return nil, listenerData, customdData, err
		}

		err = json.Unmarshal([]byte(config), &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}

		conf.encryptKeyBytes, err = hex.DecodeString(conf.EncryptKey)
		if err != nil {
			return nil, listenerData, customdData, fmt.Errorf("invalid encrypt_key hex: %v", err)
		}

	} else {
		// ★ 恢复路径也必须校验。原来这里直接 Unmarshal 就用，跳过了 validConfig：
		// 操作员在编辑表单里清空「轮询间隔」→ Number('') === 0 → 0 被写进 CustomData
		// → 重启时从这里恢复 → pollLoop 的 time.NewTicker(0) **直接 panic**，
		// panic 发生在 goroutine 里 ⇒ 整个 teamserver 进程死掉。
		// 也就是说「编辑时手滑清空一个数字框」等于「下次重启必崩」，且崩在启动阶段，
		// 现场只有一行 NewTicker 的 panic，看不出跟哪个监听器有关。
		if err = validConfig(string(customData)); err != nil {
			return nil, listenerData, customdData, fmt.Errorf("stored listener config is invalid: %w", err)
		}

		err = json.Unmarshal(customData, &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}

		conf.encryptKeyBytes, err = hex.DecodeString(conf.EncryptKey)
		if err != nil {
			return nil, listenerData, customdData, fmt.Errorf("invalid encrypt_key hex: %v", err)
		}
	}

	transport := &TransportDiscord{
		Name:   name,
		Config: conf,
		Active: false,
	}

	listenerData = adaptix.ListenerData{
		BindHost:  "discord",
		BindPort:  "0",
		AgentAddr: conf.WebhookUrl,
		Protocol:  "discord",
		Status:    "Stopped",
	}

	var buffer bytes.Buffer
	err = json.NewEncoder(&buffer).Encode(transport.Config)
	if err != nil {
		return nil, listenerData, customdData, err
	}
	customdData = buffer.Bytes()

	listener = &Listener{transport: transport}

	/// END CODE HERE

	return listener, listenerData, customdData, nil
}

func (l *Listener) Start() error {

	/// START CODE HERE

	return l.transport.Start(Ts)

	/// END CODE HERE
}

func (l *Listener) Edit(config string) (adaptix.ListenerData, []byte, error) {
	var (
		listenerData adaptix.ListenerData
		conf         ConfigDiscord
		customdData  []byte
		err          error
	)

	err = json.Unmarshal([]byte(config), &conf)
	if err != nil {
		return listenerData, customdData, err
	}

	// ★ 编辑同样必须校验，理由见 Create 恢复分支的注释：poll_interval=0 会让
	// 重启时的 time.NewTicker(0) panic 掉整个进程。编辑是唯一能把 0 写进去的入口。
	if err = validConfig(config); err != nil {
		return listenerData, customdData, err
	}

	/// START CODE HERE

	// ★ 提交上来的 7 个字段必须**全部**落到 listener.Config 上。
	//
	// 原来这里只同步了 WebhookUrl / PollInterval / Cleanup 三个，而 WebUI 的编辑
	// 表单（Listeners.tsx 的 BeaconDiscord 分支）暴露的是 7 个：bot_token /
	// channel_beacon / channel_tasks / webhook_url / poll_interval / cleanup /
	// encrypt_key。于是**改 bot token、换频道 ID、换加密密钥全部静默失效**：
	// 界面提示成功，配置一个字段都没变；又因为回写的 customData 也是从没变过的
	// listener.Config 序列化出来的，**重启之后依然不会变**。
	// 这与 listener_kharon_http 那个已修的 Edit 是同一类 bug。
	l.transport.Config.BotToken = conf.BotToken
	l.transport.Config.ChannelBeacon = conf.ChannelBeacon
	l.transport.Config.ChannelTasks = conf.ChannelTasks
	l.transport.Config.WebhookUrl = conf.WebhookUrl
	l.transport.Config.PollInterval = conf.PollInterval
	l.transport.Config.Cleanup = conf.Cleanup

	// encrypt_key 是 hex 字符串，真正参与 RC4 的是派生出来的 encryptKeyBytes。
	// 只改字符串不重算字节数组 ⇒ 界面显示新密钥、实际仍用旧密钥加密，
	// 而且重启后（走 Create 的 customData 分支）会用**新**字符串重算 —— 也就是
	// 「不重启是旧的、重启后是新的」，同一个监听器前后两套密钥。
	if conf.EncryptKey != "" && conf.EncryptKey != l.transport.Config.EncryptKey {
		keyBytes, err := hex.DecodeString(conf.EncryptKey)
		if err != nil {
			return listenerData, customdData, fmt.Errorf("invalid encrypt_key hex: %v", err)
		}
		l.transport.Config.EncryptKey = conf.EncryptKey
		l.transport.Config.encryptKeyBytes = keyBytes
	}

	// 轮询间隔只影响 pollLoop 里那个 ticker，而 ticker 是 Start() 时建好的 ——
	// 不重启循环的话，改了间隔要等下次重启才生效（操作员看到的又是一次"改了没用"）。
	// 这里就地重建循环，让编辑立刻生效。
	if l.transport.Active {
		if err = l.transport.Stop(); err != nil {
			return listenerData, customdData, err
		}
		if err = l.transport.Start(l.transport.ts); err != nil {
			return listenerData, customdData, err
		}
	}

	listenerData = adaptix.ListenerData{
		BindHost:  "discord",
		BindPort:  "0",
		AgentAddr: l.transport.Config.WebhookUrl,
		Protocol:  "discord",
		Status:    "Listen",
	}
	if !l.transport.Active {
		listenerData.Status = "Closed"
	}

	var buffer bytes.Buffer
	err = json.NewEncoder(&buffer).Encode(l.transport.Config)
	if err != nil {
		return listenerData, customdData, err
	}
	customdData = buffer.Bytes()

	/// END CODE HERE

	return listenerData, customdData, nil
}

func (l *Listener) Stop() error {

	/// START CODE HERE

	return l.transport.Stop()

	/// END CODE HERE
}

func (l *Listener) GetProfile() ([]byte, error) {
	var buffer bytes.Buffer

	/// START CODE HERE

	// Return only what the beacon needs: webhook URL, channel IDs, poll interval, encrypt key
	profile := map[string]any{
		"protocol":       "discord",
		"webhook_url":    l.transport.Config.WebhookUrl,
		"bot_token":      l.transport.Config.BotToken,
		"channel_beacon": l.transport.Config.ChannelBeacon,
		"channel_tasks_id": l.transport.Config.ChannelTasks,
		"poll_interval":  l.transport.Config.PollInterval,
		"encrypt_key":    l.transport.Config.EncryptKey,
		"cleanup":        l.transport.Config.Cleanup,
	}

	err := json.NewEncoder(&buffer).Encode(profile)
	if err != nil {
		return nil, err
	}
	/// END CODE HERE

	return buffer.Bytes(), nil
}

func (l *Listener) InternalHandler(data []byte) (string, error) {
	var agentId = ""

	/// START CODE HERE

	/// END CODE HERE

	return agentId, nil
}

func validConfig(config string) error {
	var conf ConfigDiscord
	err := json.Unmarshal([]byte(config), &conf)
	if err != nil {
		return err
	}

	if conf.BotToken == "" {
		return errors.New("bot_token is required")
	}

	if conf.ChannelBeacon == "" {
		return errors.New("channel_beacon is required")
	}
	matchChan, _ := regexp.MatchString("^[0-9]+$", conf.ChannelBeacon)
	if !matchChan {
		return errors.New("channel_beacon must be a numeric Discord channel ID")
	}

	if conf.ChannelTasks == "" {
		return errors.New("channel_tasks is required")
	}
	matchChan, _ = regexp.MatchString("^[0-9]+$", conf.ChannelTasks)
	if !matchChan {
		return errors.New("channel_tasks must be a numeric Discord channel ID")
	}

	if conf.WebhookUrl == "" {
		return errors.New("webhook_url is required")
	}
	matchWebhook, _ := regexp.MatchString("^https://discord\\.com/api/webhooks/", conf.WebhookUrl)
	if !matchWebhook {
		return errors.New("webhook_url must be a valid Discord webhook URL")
	}

	if conf.PollInterval < 1 || conf.PollInterval > 60 {
		return errors.New("poll_interval must be between 1 and 60 seconds")
	}

	match, _ := regexp.MatchString("^[0-9a-f]{32}$", conf.EncryptKey)
	if len(conf.EncryptKey) != 32 || !match {
		return errors.New("encrypt_key must be 32 hex characters (16 bytes for RC4)")
	}

	return nil
}
