package main

import (
	"bytes"
	"crypto/rc4"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"

	adaptix "github.com/Adaptix-Framework/axc2"
)

type Teamserver interface {
	TsAgentIsExists(agentId string) bool
	TsAgentCreate(agentCrc string, agentId string, beat []byte, listenerName string, ExternalIP string, Async bool) (adaptix.AgentData, error)
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
		customdData  []byte
		conf         TransportConfig
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

		conf.Prepend = unescapeString(conf.Prepend)

		conf.Protocol = "bind_tcp"
	} else {
		err = json.Unmarshal(customData, &conf)
		if err != nil {
			return nil, listenerData, customdData, err
		}
	}

	transport := &TransportTCP{
		Name:   name,
		Config: conf,
		Active: false,
	}

	listenerData = adaptix.ListenerData{
		BindHost:  "",
		BindPort:  "",
		AgentAddr: fmt.Sprintf("0.0.0.0:%d", transport.Config.Port),
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

	l.transport.Active = true
	return nil

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

	conf.Prepend = unescapeString(conf.Prepend)
	l.transport.Config.Port = conf.Port
	l.transport.Config.Prepend = conf.Prepend
	l.transport.Config.EncryptKey = conf.EncryptKey
	l.transport.Config.Protocol = conf.Protocol

	listenerData = adaptix.ListenerData{
		BindHost:  "",
		BindPort:  "",
		AgentAddr: fmt.Sprintf("0.0.0.0:%d", l.transport.Config.Port),
		Status:    "Listen",
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

	l.transport.Active = false
	return nil

	/// END CODE HERE
}

func (l *Listener) GetProfile() ([]byte, error) {
	var buffer bytes.Buffer

	/// START CODE HERE

	err := json.NewEncoder(&buffer).Encode(l.transport.Config)
	if err != nil {
		return nil, err
	}

	/// END CODE HERE

	return buffer.Bytes(), nil
}

func (l *Listener) InternalHandler(data []byte) (string, error) {
	var agentId = ""

	/// START CODE HERE

	// Wire from ConnectorTCP::SendData: [4-byte LE total length][beat...] then beat = [4-byte BE listener WM][RC4 ciphertext...].
	// InternalHandler must receive only the beat portion (no outer length prefix). If you pass the full SendData buffer,
	// the first 4 bytes are mis-read as listener WM and RC4 will never match.
	if len(data) >= 8 {
		outerLen := int(uint32(data[0]) | uint32(data[1])<<8 | uint32(data[2])<<16 | uint32(data[3])<<24)
		if outerLen >= 8 && outerLen <= len(data)-4 && outerLen == len(data)-4 {
			fmt.Printf("[listener %s] TCP InternalHandler: stripping outer 4-byte frame length (%d); pass beat-only to InternalHandler from your connector to avoid this\n", l.transport.Name, outerLen)
			data = data[4:]
		}
	}

	// Agent BuildBeat (BEACON_TCP): 4-byte plaintext listener_type, then RC4 ciphertext (agent_type, agent_id, ...).
	if len(data) < 4+8 {
		fmt.Printf("[listener %s] TCP InternalHandler: beat too short (%d bytes, need >= 12)\n", l.transport.Name, len(data))
		return "", fmt.Errorf("beat too short")
	}
	ciphertext := data[4:]

	encKey, err := hex.DecodeString(l.transport.Config.EncryptKey)
	if err != nil {
		fmt.Printf("[listener %s] TCP InternalHandler: encrypt_key decode error: %v\n", l.transport.Name, err)
		return "", err
	}
	rc4crypt, err := rc4.NewCipher(encKey)
	if err != nil {
		return "", err
	}

	agentInfo := make([]byte, len(ciphertext))
	rc4crypt.XORKeyStream(agentInfo, ciphertext)

	agentType := fmt.Sprintf("%08x", uint(binary.BigEndian.Uint32(agentInfo[:4])))
	agentInfo = agentInfo[4:]
	agentId = fmt.Sprintf("%08x", uint(binary.BigEndian.Uint32(agentInfo[:4])))
	agentInfo = agentInfo[4:]

	if !Ts.TsAgentIsExists(agentId) {
		_, err = Ts.TsAgentCreate(agentType, agentId, agentInfo, l.transport.Name, "", false)
		if err != nil {
			fmt.Printf("[listener %s] TCP InternalHandler: TsAgentCreate id=%s type=%s failed: %v\n", l.transport.Name, agentId, agentType, err)
			return agentId, err
		}
	}

	/// END CODE HERE

	return agentId, nil
}

/// UTILS

func unescapeString(s string) string {
	var result []byte
	i := 0
	for i < len(s) {
		if s[i] == '\\' && i+1 < len(s) {
			switch s[i+1] {
			case 'n':
				result = append(result, '\n')
				i += 2
			case 'r':
				result = append(result, '\r')
				i += 2
			case 't':
				result = append(result, '\t')
				i += 2
			case '\\':
				result = append(result, '\\')
				i += 2
			case '0':
				result = append(result, 0)
				i += 2
			case 'x':
				if i+3 < len(s) {
					hexStr := s[i+2 : i+4]
					if b, err := hex.DecodeString(hexStr); err == nil {
						result = append(result, b...)
						i += 4
						continue
					}
				}
				result = append(result, s[i])
				i++
			default:
				result = append(result, s[i])
				i++
			}
		} else {
			result = append(result, s[i])
			i++
		}
	}
	return string(result)
}
