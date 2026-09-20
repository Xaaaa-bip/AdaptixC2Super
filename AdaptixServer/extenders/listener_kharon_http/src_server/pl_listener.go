package main

import (
	"bytes"
	"crypto/rand"
	"encoding/json"
	"strconv"

	ax "github.com/Adaptix-Framework/axc2"
	"github.com/gin-gonic/gin"
)

func (m *ModuleExtender) HandlerListenerValid(data string) error {

	/// START CODE HERE

	var (
		err  error
		conf HTTPConfig
	)

	err = json.Unmarshal([]byte(data), &conf)
	if err != nil {
		return err
	}

	/// END CODE

	return nil
}

func (m *ModuleExtender) HandlerCreateListenerDataAndStart(name string, configData string, listenerCustomData []byte) (ax.ExtenderListener, ax.ListenerData, []byte, any, error) {
	var (
		listenerData ax.ListenerData
		customdData  []byte
	)

	/// START CODE HERE

	var (
		extender ax.ExtenderListener
		listener *HTTP
		conf     HTTPConfig
		err      error
	)

	if listenerCustomData == nil {
		err = json.Unmarshal([]byte(configData), &conf)
		if err != nil {
			return extender, listenerData, customdData, listener, err
		}

		randSlice := make([]byte, 16)
		_, _ = rand.Read(randSlice)
		conf.EncryptKey = randSlice[:16]
		conf.Protocol = "http"

	} else {
		err = json.Unmarshal(listenerCustomData, &conf)
		if err != nil {
			return extender, listenerData, customdData, listener, err
		}
	}

	listener = &HTTP{
		GinEngine: gin.New(),
		Name:      name,
		Config:    conf,
		Active:    false,
	}

	err = listener.Start(m.ts)
	if err != nil {
		return extender, listenerData, customdData, listener, err
	}

	listenerData = ax.ListenerData{
		BindHost:  listener.Config.HostBind,
		BindPort:  strconv.Itoa(listener.Config.PortBind),
		AgentAddr: listener.Config.Addresses,
		Status:    "Listen",
	}

	if listener.Config.Ssl {
		listenerData.Protocol = "https"
	}

	if !listener.Active {
		listenerData.Status = "Closed"
	}

	var buffer bytes.Buffer
	err = json.NewEncoder(&buffer).Encode(listener.Config)
	if err != nil {
		return extender, listenerData, customdData, listener, nil
	}
	customdData = buffer.Bytes()

	extender = &Listener{
		transport: listener,
	}

	/// END CODE

	return extender, listenerData, customdData, listener, nil
}

func (m *ModuleExtender) HandlerEditListenerData(name string, listenerObject any, configData string) (ax.ListenerData, []byte, bool) {
	var (
		listenerData ax.ListenerData
		customdData  []byte
		ok           bool = false
	)

	/// START CODE HERE

	var (
		err  error
		conf HTTPConfig
	)

	listener := listenerObject.(*HTTP)
	if listener.Name == name {

		err = json.Unmarshal([]byte(configData), &conf)
		if err != nil {
			return listenerData, customdData, false
		}

		// ★ 提交上来的配置必须真正落到 listener.Config 上。
		//
		// 原来这里把 configData 解到局部 conf 之后就再没碰过它：返回的 listenerData
		// 和回写的 customData 全是**旧的** listener.Config。后果不是"编辑慢半拍"，
		// 而是编辑完全无效 —— 操作员在 WebUI 改绑定地址/端口/拦截 UA/profile，
		// 界面提示成功、监听器一个字段都没变；又因为回写的 customData 也是旧的，
		// **重启之后依然不会变**。其余 9 个 listener 的 Edit 都做了这次同步
		// （beacon_listener_http 等处写着 "Keep transport in sync with submitted JSON"）。
		//
		// 用逐字段赋值而不是 `listener.Config = conf`：conf 只承载表单暴露的字段，
		// 整体覆盖会把表单没有的 MaskKey / ChunkTask / Callbacks / Addresses 一并清零。
		listener.Config.HostBind = conf.HostBind
		listener.Config.PortBind = conf.PortBind
		listener.Config.BlockUserAgent = conf.BlockUserAgent
		listener.Config.ProfileContent = conf.ProfileContent
		listener.Config.DomainRotation = conf.DomainRotation
		listener.Config.Ssl = conf.Ssl
		listener.Config.SslCertPath = conf.SslCertPath
		listener.Config.SslKeyPath = conf.SslKeyPath
		listener.Config.ProxyUrl = conf.ProxyUrl
		listener.Config.ProxyUserName = conf.ProxyUserName
		listener.Config.ProxyPassword = conf.ProxyPassword
		listener.Config.TrustXForwardedHost = conf.TrustXForwardedHost
		listener.Config.AdditionalTrustedHosts = conf.AdditionalTrustedHosts
		// 证书是 base64 []byte：只有真提交了才覆盖，否则会把内存里已加载的证书抹掉。
		if len(conf.SslCert) > 0 {
			listener.Config.SslCert = conf.SslCert
		}
		if len(conf.SslKey) > 0 {
			listener.Config.SslKey = conf.SslKey
		}
		// KharonHTTP 的表单里没有 encrypt_key（Listeners.tsx 的 KharonHTTP 分支只暴露
		// 上面这些字段），编辑回传的 JSON 自然不带它。无条件覆盖会把创建时生成的密钥
		// 抹成空串 —— 监听器表面还 Listen，实际再也对接不上 agent，且重启后依旧是空的。
		if len(conf.EncryptKey) > 0 {
			listener.Config.EncryptKey = conf.EncryptKey
		}
		// 同理：创建时强制 protocol=http，表单不暴露该字段。
		if conf.Protocol != "" {
			listener.Config.Protocol = conf.Protocol
		}

		listenerData = ax.ListenerData{
			BindHost:  listener.Config.HostBind,
			BindPort:  strconv.Itoa(listener.Config.PortBind),
			AgentAddr: listener.Config.Addresses,
			Status:    "Listen",
		}
		if !listener.Active {
			listenerData.Status = "Closed"
		}

		var buffer bytes.Buffer
		err = json.NewEncoder(&buffer).Encode(listener.Config)
		if err != nil {
			return listenerData, customdData, false
		}
		customdData = buffer.Bytes()

		ok = true
	}

	/// END CODE

	return listenerData, customdData, ok
}

func (m *ModuleExtender) HandlerListenerStop(name string, listenerObject any) (bool, error) {
	var (
		err error = nil
		ok  bool  = false
	)

	/// START CODE HERE

	listener := listenerObject.(*HTTP)
	if listener.Name == name {
		err = listener.Stop()
		ok = true
	}

	/// END CODE

	return ok, err
}

func (m *ModuleExtender) HandlerListenerGetProfile(name string, listenerObject any) ([]byte, bool) {
	var (
		object bytes.Buffer
		ok     bool = false
	)

	/// START CODE HERE

	listener := listenerObject.(*HTTP)
	if listener.Name == name {
		_ = json.NewEncoder(&object).Encode(listener.Config)
		ok = true
	}

	/// END CODE

	return object.Bytes(), ok
}
