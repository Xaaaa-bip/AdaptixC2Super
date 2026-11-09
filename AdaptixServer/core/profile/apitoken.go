package profile

/* ────────────────────────────────────────────────────────────────────────
   静态 API token（2026-11-10）：
   首次启动时 Teamserver.api_token 为空 → 随机生成 64 hex 写回 profile.yaml
   （文本级替换，保留所有注释与顺序），并注入认证层。此后 REST/MCP 可直接
   Authorization: Bearer <api_token> 调用 —— 与 JWT 同权限、但跨重启稳定，
   解决「重启 teamserver 后 MCP 客户端 token 失效要重新 login」的问题。
   已有值（用户手填或上次生成）则原样使用，不覆盖。
   ──────────────────────────────────────────────────────────────────────── */

import (
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"fmt"
	"os"
	"strings"
)

var staticApiToken string

// StaticApiToken — 当前生效的静态 token（测试用；生产从 profile 注入）。
func StaticApiToken() string { return staticApiToken }

// SetStaticApiToken — 注入认证层（constant-time 比对在 ValidateAccessToken）。
func SetStaticApiToken(s string) { staticApiToken = strings.TrimSpace(s) }

// CheckStaticApiToken — constant-time 比对；空 token 视为未启用。
func CheckStaticApiToken(candidate string) bool {
	if staticApiToken == "" || candidate == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(candidate), []byte(staticApiToken)) == 1
}

// EnsureApiToken — profile 加载后调用：生成/注入静态 API token。
// 返回 (token, generated, err)：generated=true 表示本次新生成并已写回文件。
func EnsureApiToken(profilePath string, server *TsProfile) (string, bool, error) {
	if server == nil {
		return "", false, fmt.Errorf("api_token: Teamserver section missing")
	}
	if v := strings.TrimSpace(server.ApiToken); v != "" {
		SetStaticApiToken(v)
		return v, false, nil
	}

	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", false, fmt.Errorf("api_token: rand: %w", err)
	}
	gen := hex.EncodeToString(raw)

	if err := writeApiTokenToProfile(profilePath, gen); err != nil {
		return "", false, err
	}
	server.ApiToken = gen
	SetStaticApiToken(gen)
	return gen, true, nil
}

// writeApiTokenToProfile — 文本级写回（保留注释/顺序）：
// Teamserver: 段内已有 api_token: 行 → 原位替换；否则插到 Teamserver: 行后。
func writeApiTokenToProfile(profilePath, token string) error {
	data, err := os.ReadFile(profilePath)
	if err != nil {
		return fmt.Errorf("api_token: read profile: %w", err)
	}
	lines := strings.Split(string(data), "\n")

	inTeamserver := false
	replaced := false
	for i, line := range lines {
		trimmed := strings.TrimRight(line, " \t\r")
		if !strings.HasPrefix(trimmed, "#") { // 注释行不参与段定位
			if !strings.HasPrefix(line, " ") && !strings.HasPrefix(line, "\t") && strings.HasSuffix(trimmed, ":") {
				inTeamserver = strings.TrimSuffix(trimmed, ":") == "Teamserver"
				continue
			}
			if inTeamserver {
				lst := strings.TrimLeft(line, " \t")
				if strings.HasPrefix(lst, "api_token:") {
					indent := line[:len(line)-len(lst)]
					lines[i] = indent + "api_token: \"" + token + "\" # static API token for REST/MCP (Bearer)"
					replaced = true
					break
				}
			}
		}
	}

	if !replaced {
		// Teamserver: 行后插入
		inserted := false
		for i, line := range lines {
			trimmed := strings.TrimRight(line, " \t\r")
			if trimmed == "Teamserver:" {
				out := make([]string, 0, len(lines)+1)
				out = append(out, lines[:i+1]...)
				out = append(out, "  api_token: \""+token+"\" # static API token for REST/MCP (Bearer)")
				out = append(out, lines[i+1:]...)
				lines = out
				inserted = true
				break
			}
		}
		if !inserted {
			return fmt.Errorf("api_token: no 'Teamserver:' section found in %s — set api_token manually", profilePath)
		}
	}

	info, err := os.Stat(profilePath)
	mode := os.FileMode(0600)
	if err == nil {
		mode = info.Mode()
	}
	if err := os.WriteFile(profilePath, []byte(strings.Join(lines, "\n")), mode); err != nil {
		return fmt.Errorf("api_token: write profile: %w", err)
	}
	return nil
}
