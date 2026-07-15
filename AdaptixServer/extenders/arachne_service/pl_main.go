package main

import (
	"encoding/json"
	"fmt"
	"strings"
	"sync"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
	"gopkg.in/yaml.v3"
)

const (
	ServiceName  = "arachne"
	ExtenderName = "arachne_service"
	configKey    = "config"
)

type Teamserver interface {
	TsExtenderDataSave(extenderName string, key string, value []byte) error
	TsExtenderDataLoad(extenderName string, key string) ([]byte, error)
	TsExtenderDataDelete(extenderName string, key string) error
	TsExtenderDataKeys(extenderName string) ([]string, error)

	TsServiceSendDataAll(service string, data string)
	TsServiceSendDataClient(operator string, service string, data string)
}

type Config struct {
	Enabled bool   `json:"enabled" yaml:"enabled"`
	Mode    string `json:"mode" yaml:"mode"`
	RepoURL string `json:"repo_url" yaml:"repo_url"`
	DataDir string `json:"data_dir" yaml:"data_dir"`
	Notes   string `json:"notes" yaml:"notes"`
}

type Status struct {
	Loaded       bool      `json:"loaded"`
	Enabled      bool      `json:"enabled"`
	Mode         string    `json:"mode"`
	RepoURL      string    `json:"repo_url"`
	DataDir      string    `json:"data_dir"`
	Runtime      string    `json:"runtime"`
	StartedAt    time.Time `json:"started_at"`
	Capabilities []string  `json:"capabilities"`
	Limitations  []string  `json:"limitations"`
}

type Service struct {
	ts        Teamserver
	moduleDir string
	startedAt time.Time
	mu        sync.RWMutex
	config    Config
}

var current *Service

func InitPlugin(ts any, moduleDir string, serviceConfig string) adaptix.PluginService {
	svc := &Service{
		ts:        ts.(Teamserver),
		moduleDir: moduleDir,
		startedAt: time.Now(),
		config:    defaultConfig(),
	}

	if err := svc.loadConfig(serviceConfig); err != nil {
		fmt.Printf("[Arachne] config error: %v; using defaults\n", err)
	}

	current = svc
	fmt.Printf("[Arachne] service bridge loaded in %s (mode=%s enabled=%v)\n", moduleDir, svc.config.Mode, svc.config.Enabled)
	return svc
}

func defaultConfig() Config {
	return Config{
		Enabled: false,
		Mode:    "bridge",
		RepoURL: "https://github.com/portbuster1337/ArachneC2",
		DataDir: "data/arachne",
		Notes:   "Bridge skeleton only. It does not start Arachne, build implants, or dispatch commands.",
	}
}

func (s *Service) loadConfig(serviceConfig string) error {
	cfg := defaultConfig()
	if strings.TrimSpace(serviceConfig) != "" {
		if err := yaml.Unmarshal([]byte(serviceConfig), &cfg); err != nil {
			return err
		}
	}
	normalizeConfig(&cfg)

	if saved, err := s.ts.TsExtenderDataLoad(ExtenderName, configKey); err == nil && len(saved) > 0 {
		if err := json.Unmarshal(saved, &cfg); err != nil {
			return fmt.Errorf("load saved config: %w", err)
		}
		normalizeConfig(&cfg)
	}

	s.mu.Lock()
	s.config = cfg
	s.mu.Unlock()
	return nil
}

func normalizeConfig(cfg *Config) {
	cfg.Mode = strings.TrimSpace(strings.ToLower(cfg.Mode))
	if cfg.Mode == "" {
		cfg.Mode = "bridge"
	}
	if cfg.RepoURL == "" {
		cfg.RepoURL = "https://github.com/portbuster1337/ArachneC2"
	}
	if cfg.DataDir == "" {
		cfg.DataDir = "data/arachne"
	}
}

func validateConfig(cfg Config) error {
	switch cfg.Mode {
	case "bridge", "disabled":
	default:
		return fmt.Errorf("unsupported mode %q; allowed values: bridge, disabled", cfg.Mode)
	}
	if strings.TrimSpace(cfg.RepoURL) == "" {
		return fmt.Errorf("repo_url is required")
	}
	if strings.TrimSpace(cfg.DataDir) == "" {
		return fmt.Errorf("data_dir is required")
	}
	return nil
}

func (s *Service) Call(operator string, function string, args string) {
	switch function {
	case "get_status", "status":
		s.sendClient(operator, "status", s.status())
	case "get_config", "config":
		s.sendClient(operator, "config", s.getConfig())
	case "set_config":
		s.handleSetConfig(operator, args)
	case "enable":
		s.setEnabled(operator, true)
	case "disable":
		s.setEnabled(operator, false)
	default:
		s.sendError(operator, fmt.Sprintf("unknown arachne service function: %s", function))
	}
}

func (s *Service) getConfig() Config {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.config
}

func (s *Service) status() Status {
	cfg := s.getConfig()
	runtime := "inactive"
	if cfg.Enabled && cfg.Mode == "bridge" {
		runtime = "bridge-ready"
	}
	if cfg.Mode == "disabled" {
		runtime = "disabled"
	}

	return Status{
		Loaded:    true,
		Enabled:   cfg.Enabled,
		Mode:      cfg.Mode,
		RepoURL:   cfg.RepoURL,
		DataDir:   cfg.DataDir,
		Runtime:   runtime,
		StartedAt: s.startedAt,
		Capabilities: []string{
			"persist Arachne bridge configuration",
			"report integration status to Adaptix clients",
			"provide a stable service entry point for future bridge work",
		},
		Limitations: []string{
			"does not start or embed the Arachne operator runtime",
			"does not build implants",
			"does not dispatch commands or process agent sessions",
		},
	}
}

func (s *Service) handleSetConfig(operator string, args string) {
	var cfg Config
	if err := json.Unmarshal([]byte(args), &cfg); err != nil {
		s.sendError(operator, "invalid config JSON: "+err.Error())
		return
	}
	normalizeConfig(&cfg)
	if err := validateConfig(cfg); err != nil {
		s.sendError(operator, err.Error())
		return
	}

	s.mu.Lock()
	s.config = cfg
	s.mu.Unlock()

	if err := s.persistConfig(cfg); err != nil {
		s.sendError(operator, "failed to persist config: "+err.Error())
		return
	}

	s.sendClient(operator, "config", cfg)
	s.broadcast("status", s.status())
}

func (s *Service) setEnabled(operator string, enabled bool) {
	cfg := s.getConfig()
	cfg.Enabled = enabled
	if !enabled {
		cfg.Mode = "disabled"
	} else if cfg.Mode == "disabled" {
		cfg.Mode = "bridge"
	}
	if err := validateConfig(cfg); err != nil {
		s.sendError(operator, err.Error())
		return
	}

	s.mu.Lock()
	s.config = cfg
	s.mu.Unlock()

	if err := s.persistConfig(cfg); err != nil {
		s.sendError(operator, "failed to persist config: "+err.Error())
		return
	}

	s.sendClient(operator, "config", cfg)
	s.broadcast("status", s.status())
}

func (s *Service) persistConfig(cfg Config) error {
	data, err := json.Marshal(cfg)
	if err != nil {
		return err
	}
	return s.ts.TsExtenderDataSave(ExtenderName, configKey, data)
}

func (s *Service) sendClient(operator string, msgType string, data interface{}) {
	payload := map[string]interface{}{
		"type":   msgType,
		"action": msgType,
		"data":   data,
	}
	raw, err := json.Marshal(payload)
	if err != nil {
		return
	}
	s.ts.TsServiceSendDataClient(operator, ServiceName, string(raw))
}

func (s *Service) broadcast(msgType string, data interface{}) {
	payload := map[string]interface{}{
		"type":   msgType,
		"action": msgType,
		"data":   data,
	}
	raw, err := json.Marshal(payload)
	if err != nil {
		return
	}
	s.ts.TsServiceSendDataAll(ServiceName, string(raw))
}

func (s *Service) sendError(operator string, message string) {
	payload := map[string]interface{}{
		"type":    "error",
		"action":  "error",
		"message": message,
	}
	raw, err := json.Marshal(payload)
	if err != nil {
		return
	}
	s.ts.TsServiceSendDataClient(operator, ServiceName, string(raw))
}
