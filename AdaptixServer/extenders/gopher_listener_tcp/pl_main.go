package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"strconv"
	"strings"

	adaptix "github.com/Adaptix-Framework/axc2"
)

type Teamserver interface {
	TsAgentIsExists(agentId string) bool
	TsAgentCreate(agentCrc string, agentId string, beat []byte, listenerName string, ExternalIP string, Async bool) (adaptix.AgentData, error)
	TsAgentSetTick(agentId string, listenerName string) error
	TsAgentProcessData(agentId string, bodyData []byte) error
	TsAgentGetHostedAll(agentId string, maxDataSize int) ([]byte, error)
	TsAgentGetHostedTasks(agentId string, maxDataSize int) ([]byte, error)
	TsAgentUpdateDataPartial(agentId string, updateData interface{}) error

	TsTaskRunningExists(agentId string, taskId string) bool
	TsTunnelChannelExists(channelId int) bool
	TsTunnelConnectionAccept(tunnelId int, channelId int)

	// ★ 批次9d：core 侧与上面配对的方法 —— 只拨号+登记，**不启 relay**，把 prSrv 的
	//   读取端留给本插件（紧跟的 TsTunnelConnectionResume(..., true) 只启 G1）。
	//   ★ InitPlugin 的运行时断言要求 core 真的实现了它，否则插件加载即 panic。
	TsTunnelConnectionAcceptDirect(tunnelId int, channelId int)

	TsAgentTerminalCloseChannel(terminalId string, status string) error
	TsTerminalConnExists(terminalId string) bool
	TsTerminalConnResume(agentId string, terminalId string, ioDirect bool)
	TsTerminalGetPipe(AgentId string, terminalId string) (*io.PipeReader, *io.PipeWriter, error)

	TsTunnelGetPipe(AgentId string, channelId int) (*io.PipeReader, *io.PipeWriter, error)
	TsTunnelConnectionResume(AgentId string, channelId int, ioDirect bool)
	TsTunnelConnectionClose(channelId int, writeOnly bool)
	TsTunnelConnectionHalt(channelId int, errorCode byte)
	TsTunnelConnectionData(channelId int, data []byte)
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

// normalizeCallbackAddresses turns multiline / mixed separators into "host:port, host:port".
func normalizeCallbackAddresses(raw StringOrArray) StringOrArray {
	cb := strings.ReplaceAll(string(raw), " ", "")
	cb = strings.ReplaceAll(cb, "\r\n", "\n")
	cb = strings.ReplaceAll(cb, "\n", ", ")
	cb = strings.Trim(cb, ", ")
	return StringOrArray(cb)
}

// axPrepareLibp2p 把 libp2p 的 config 归一化到**可校验**的形态：补私钥、补 /p2p/<peerID>。
//
// ★★ 为什么必须**先于** validConfig 调用（Phase5 实测出的顺序缺陷）：
// validConfig -> axValidateLibp2pAddrs 要求每个 multiaddr 自带 /p2p/<peerID>
// （agent 侧走 peer.AddrInfoFromP2pAddr，没有 peer id 就没有拨号目标），
// 而 peer id 由**私钥**决定，operator 在 UI 上只填 /ip4/<ip>/tcp/<port>。
// 照"先校验后补全"的顺序写，libp2p listener **永远建不起来** —— 实测报
// `Invalid multiaddr (need /p2p/<peerID>)`，而同一入口的 tcp 正常。
//
// ★ 返回**重新序列化后的 config 字符串**（而不是只改结构体）：这样 validConfig 校验的
// 与后面 json.Unmarshal 落盘的是同一份文本 —— 校验与持久化不会漂移。
// ★ 非 libp2p、或 JSON 根本解不动时**原样返回、不报错**：把报错留给 validConfig 去报，
// 同一个错误不在这里抢着报第二遍。
func axPrepareLibp2p(config string) (string, error) {
	var pre TransportConfig
	if err := json.Unmarshal([]byte(config), &pre); err != nil {
		return config, nil
	}
	if !strings.EqualFold(pre.Protocol, "libp2p") {
		return config, nil
	}
	pre.Protocol = "libp2p"
	if pre.Libp2pPrivKey == "" {
		k, err := axGenerateLibp2pKey()
		if err != nil {
			return config, err
		}
		pre.Libp2pPrivKey = k
	}
	// ★ 地址里**已经**写了 peer id 时，它必须与私钥对得上。否则 agent 拨的是一个与
	// listener 身份不符的 peer id，握手在 Noise 阶段就失败，而 UI 上只显示"上线失败"。
	// 这一类"静默走错"正是本项目反复吃过的那一族，所以宁可在这里拒掉。
	id := axLibp2pPeerID(pre.Libp2pPrivKey)
	for _, a := range axSplitAddrs(string(pre.Callback_addresses)) {
		k := strings.Index(a, "/p2p/")
		if k < 0 {
			continue
		}
		if got := a[k+len("/p2p/"):]; id != "" && got != id {
			return config, errors.New("callback_addresses 里的 peer id 与 l2p_privkey 不符：地址 " +
				a + " 要求 " + got + "，而私钥对应 " + id +
				" —— peer id 就是 Noise 握手的身份，照此建 listener agent 永远拨不上")
		}
	}
	pre.Callback_addresses = StringOrArray(
		axFillLibp2pAgentAddr(string(pre.Callback_addresses), pre.Libp2pPrivKey))
	// ★★ 必须补 port_bind（Phase5 实测出的第二个缺陷）：libp2p 的 operator 只填
	// callback_addresses，而 validConfig 的 libp2p 分支早退、从不校验 host_bind/port_bind
	// ⇒ 两者都是零值。axLibp2pHost 用它们拼 ListenAddrStrings ⇒ 绑到
	// /ip4/0.0.0.0/tcp/0 = **内核随机端口**：状态/日志/UI 全说启动成功，而 agent 按
	// callback_addresses 里的端口永远连不上（实测 39014 无人监听）。
	// 规则是"显式值优先，缺了才从 multiaddr 推"，NAT 场景仍可用 port_bind 覆盖。
	if pre.PortBind < 1 {
		p, perr := axLibp2pBindPort(string(pre.Callback_addresses))
		if perr != nil {
			return config, perr
		}
		pre.PortBind = p
	}
	// ★ 批次5：配了 relay ⇒ 自动把 agent 该拨的 circuit 地址组合出来。
	//   这是"隐藏 C2 IP"成为**默认行为**而不是"要人记得去填"的那一步。
	if err := axFillLibp2pRelayAddrs(&pre); err != nil {
		return config, err
	}

	out, err := json.Marshal(pre)
	if err != nil {
		return config, err
	}
	return string(out), nil
}

func (p *PluginListener) Create(name string, config string, customData []byte) (adaptix.ExtenderListener, adaptix.ListenerData, []byte, error) {
	var (
		listener     *Listener
		listenerData adaptix.ListenerData
		customdData  []byte
		conf         TransportConfig
		err          error
	)

	/// START CODE HERE

	if customData == nil {
		// ★★ 顺序是硬要求（Phase5 实测）：libp2p 的归一化必须**先于** validConfig。
		// 反过来写（照 tcp 那条链的老样子），libp2p listener 永远建不起来 —— 见 axPrepareLibp2p。
		prepared, perr := axPrepareLibp2p(config)
		if perr != nil {
			return nil, listenerData, customdData, perr
		}
		config = prepared

		if err = validConfig(config); err != nil {
			return nil, listenerData, customdData, err
		}

		err = json.Unmarshal([]byte(config), &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}

		// Normalize to "host:port, host:port" for AgentAddr / gopher profile string.
		conf.Callback_addresses = normalizeCallbackAddresses(conf.Callback_addresses)

		// ★ Phase5：protocol 改为**数据驱动**（由 Web UI 的 combo 决定）。
		//   默认 tcp ⇒ 老 config 的 JSON 里没有这个键时，行为与改造前逐字相同。
		if conf.Protocol == "" {
			conf.Protocol = "tcp"
		}
	} else {
		err = json.Unmarshal(customData, &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}
	}

	transport := &TransportTCP{
		Name:          name,
		Config:        conf,
		AgentConnects: NewMap(),
		JobConnects:   NewMap(),
		Active:        false,
	}

	listenerData = adaptix.ListenerData{
		BindHost:  transport.Config.HostBind,
		BindPort:  strconv.Itoa(transport.Config.PortBind),
		AgentAddr: string(conf.Callback_addresses),
		Status:    "Stopped",
	}

	if transport.Config.Ssl {
		listenerData.Protocol = "mtls"
	}
	// ★ libp2p 覆盖上面那条：Noise 已提供加密与对端认证 —— 既不是裸 TCP 也不是 mTLS。
	if transport.Config.Protocol == "libp2p" {
		listenerData.Protocol = "libp2p"
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
		customdData  []byte
		conf         TransportConfig
		err          error
	)

	err = json.Unmarshal([]byte(config), &conf)
	if err != nil {
		return listenerData, customdData, err
	}

	/// START CODE HERE

	conf.Callback_addresses = normalizeCallbackAddresses(conf.Callback_addresses)

	// Full sync: GetProfile / decrypt use transport.Config (same pitfall as BeaconHTTP Edit).
	l.transport.Config.HostBind = conf.HostBind
	l.transport.Config.PortBind = conf.PortBind
	l.transport.Config.Callback_addresses = conf.Callback_addresses
	l.transport.Config.EncryptKey = conf.EncryptKey
	l.transport.Config.Ssl = conf.Ssl
	if len(conf.CaCert) > 0 {
		l.transport.Config.CaCert = conf.CaCert
	}
	if len(conf.ServerCert) > 0 {
		l.transport.Config.ServerCert = conf.ServerCert
	}
	if len(conf.ServerKey) > 0 {
		l.transport.Config.ServerKey = conf.ServerKey
	}
	if len(conf.ClientCert) > 0 {
		l.transport.Config.ClientCert = conf.ClientCert
	}
	if len(conf.ClientKey) > 0 {
		l.transport.Config.ClientKey = conf.ClientKey
	}
	l.transport.Config.TcpBanner = conf.TcpBanner
	l.transport.Config.ErrorAnswer = conf.ErrorAnswer
	l.transport.Config.Timeout = conf.Timeout

	// ★ Protocol 必须一起同步：Edit 是"整份 config 回灌"，漏掉它会让编辑过的
	//   listener 悄悄退回 tcp，而 UI 上仍显示原协议 —— 静默退化。
	if conf.Protocol == "" {
		conf.Protocol = "tcp"
	}
	l.transport.Config.Protocol = conf.Protocol
	l.transport.Config.Libp2pPeerID = conf.Libp2pPeerID
	l.transport.Config.Libp2pAddrs = conf.Libp2pAddrs
	l.transport.Config.Libp2pProtocol = conf.Libp2pProtocol
	l.transport.Config.Libp2pDialTimeout = conf.Libp2pDialTimeout
	if conf.Libp2pPrivKey != "" {
		l.transport.Config.Libp2pPrivKey = conf.Libp2pPrivKey
	}
	// ★ 批次5：relay 字段必须一起同步 —— 与上面 Protocol 同一个坑：
	//   漏掉它，编辑过的 listener 会**静默丢掉** relay 配置，UI 上却还显示着。
	l.transport.Config.Libp2pRelays = conf.Libp2pRelays
	if err := axFillLibp2pRelayAddrs(&l.transport.Config); err != nil {
		return listenerData, customdData, err
	}

	listenerData = adaptix.ListenerData{
		BindHost:  l.transport.Config.HostBind,
		BindPort:  strconv.Itoa(l.transport.Config.PortBind),
		AgentAddr: string(l.transport.Config.Callback_addresses),
		Status:    "Listen",
	}
	if !l.transport.Active {
		listenerData.Status = "Closed"
	}
	// ★ 与 Create 同构：libp2p 的 AgentAddr 必须带 /p2p/<peerID>，否则 agent 生成时
	//   拿到的地址拨不出去（agent 侧走 peer.AddrInfoFromP2pAddr）。
	if l.transport.Config.Protocol == "libp2p" {
		listenerData.AgentAddr = axFillLibp2pAgentAddr(
			string(l.transport.Config.Callback_addresses), l.transport.Config.Libp2pPrivKey)
		listenerData.Protocol = "libp2p"
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

	// ★ 纵深防御（Phase5 缺陷三）：本 profile 的唯一消费者是 teamserver 的
	// TsAgentBuildSyncOnce（构建 agent），listener 的私钥**绝不能**进去 ——
	// agent 一旦采用它就会"拨自己"（libp2p swarm_dial.go:246 ErrDialToSelf），
	// 且私钥会随每个 agent 二进制外泄。私钥只属于 listener（跨重启保持 peer id
	// 稳定），不进 profile。
	// ★ 不影响重启恢复：恢复走 Create(name, config, customData)，customData 来自
	// customdData = buffer.Bytes()，与本函数无关。
	cfg := l.transport.Config
	cfg.Libp2pPrivKey = ""
	err := json.NewEncoder(&buffer).Encode(cfg)
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

	return agentId, errors.New("not implemented")
}
