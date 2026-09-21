package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

func localProxyAddr(port int) string {
	host := Config.BindHost
	if host == "" || host == "0.0.0.0" || host == "::" {
		host = "127.0.0.1"
	}
	return net.JoinHostPort(host, strconv.Itoa(port))
}

func proxyPortFromRequest(r *http.Request) (int, error) {
	sessionId := r.URL.Query().Get("session")
	if sessionId != "" {
		session, err := getSession(sessionId)
		if err != nil {
			return 0, err
		}
		return session.LocalPort, nil
	}

	local := r.URL.Query().Get("local")
	if local == "" {
		return 0, fmt.Errorf("session or local parameter required")
	}

	port, err := strconv.Atoi(local)
	if err != nil {
		return 0, fmt.Errorf("invalid local port: %s", local)
	}
	if port < 1 || port > 65535 {
		return 0, fmt.Errorf("local port must be from 1 to 65535")
	}
	return port, nil
}

// streamDialTimeout returns the configured dial timeout for the upstream
// /stream connection, falling back to 10s when unset or non-positive.
func streamDialTimeout() time.Duration {
	if Config.StreamDialTimeoutSec > 0 {
		return time.Duration(Config.StreamDialTimeoutSec) * time.Second
	}
	return 10 * time.Second
}

// streamHeaderTimeout returns the configured upstream /stream response
// header read timeout, falling back to 60s when unset or non-positive.
// This must accommodate the beacon sleep interval, because every byte of
// the HTTP handshake crosses a beacon-polled tunnel.
func streamHeaderTimeout() time.Duration {
	if Config.StreamHeaderTimeoutSec > 0 {
		return time.Duration(Config.StreamHeaderTimeoutSec) * time.Second
	}
	return 60 * time.Second
}

// streamConnectRetryWindow 是「agent 侧 vnc_stream 还没起来」时的自愈窗口。
//
// ★ 为什么必须重试：VncPanel「一键启动」只固定等 6s 就打开 iframe，而 beacon 上
//   `vnc start` 是**任务**，要走 beacon 的 check-in：
//     第 1 次 check-in → 收 COMMAND_UPLOAD，落盘 vnc_stream.exe
//     第 2 次 check-in → 收 COMMAND_PS_RUN，PowerShell 再花几百毫秒才 bind 端口
//   而 TsTaskGetAvailableAll 先取 HostedTasks 再取 HostedTunnelTasks —— 隧道
//   connect 命令和 PS_RUN 落在**同一批**里。于是观看端常常在 vnc_stream 起来
//   **之前**就建好隧道通道，agent 连 127.0.0.1:<targetPort> 得到
//   WSAECONNREFUSED(10061) → PackProxyStatus → 服务端 TsTunnelConnectionHalt
//   → 对端 RST。线上日志正是
//     `header parse error after 5.704560701s ... connection reset by peer`
//   （5.7s ≈ 一个 beacon 周期）。旧代码在这里直接 502 ⇒ 用户看到的「无画面」。
// ★ 为什么只重试「一个响应头字节都没收到」的情况：收到任何响应字节都说明
//   vnc_stream 活着、画面已开始，绝不能重连去打断它。
// ★ 为什么写失败也要重试：RST 可能比我们的 64 字节 GET 更早到达内核，此时
//   失败暴露在 conn.Write 上（本地复现即 `write: connection reset by peer`），
//   而不是随后的 ReadResponse（线上抓到的 `header parse error` 是 RST 晚到的
//   那一支）。同一个竞态、同样的「零响应字节」，必须同样对待 —— 只重试读会让
//   修复在快 RST 的场景下退化成原来的立即 502。
// ★ 为什么不重试超时：超时已花掉整个 header_timeout（默认 60s），再等一个窗口
//   没有意义 —— 那类故障是「agent 连上了但 vnc_stream 不出帧」，不是没起来。
// ★ 为什么不重试 dial 失败：TsTunnelStart 是**同步** net.Listen，`vnc start`
//   一旦成功监听就已就绪；dial 失败只可能是「隧道压根不存在」，重试不会变好，
//   保持原来的快速报错。
func streamConnectRetryWindow() time.Duration {
	if Config.StreamConnectRetrySec > 0 {
		return time.Duration(Config.StreamConnectRetrySec) * time.Second
	}
	return 45 * time.Second
}

// streamConnectRetryInterval 是两次尝试之间的间隔。1s 足够跟上 sleep 0 的 beacon
// （交互加速后一个往返通常不到 1s），又不会把 HostedTunnelTasks 灌爆。
const streamConnectRetryInterval = 1 * time.Second

func isTimeoutErr(err error) bool {
	if err == nil {
		return false
	}
	if errors.Is(err, os.ErrDeadlineExceeded) {
		return true
	}
	var ne net.Error
	if errors.As(err, &ne) && ne.Timeout() {
		return true
	}
	return false
}

func handleStream(w http.ResponseWriter, r *http.Request) {
	localPort, err := proxyPortFromRequest(r)
	if err != nil {
		fmt.Printf("[VNC] Stream proxy: bad request from %s: %v\n", r.RemoteAddr, err)
		http.Error(w, err.Error(), 400)
		return
	}

	targetAddr := localProxyAddr(localPort)
	dialTimeout := streamDialTimeout()
	headerTimeout := streamHeaderTimeout()

	fmt.Printf("[VNC] Stream proxy: request received method=%s path=%s query=%q remote=%s local_port=%d target=%s dial_timeout=%s header_timeout=%s\n",
		r.Method, r.URL.Path, r.URL.RawQuery, r.RemoteAddr, localPort, targetAddr, dialTimeout, headerTimeout)

	// Send GET /stream to the upstream (vnc_stream agent via tunnel)
	upReq, _ := http.NewRequest("GET", "/stream", nil)
	var reqBuf bytes.Buffer
	_ = upReq.Write(&reqBuf)
	reqBytes := reqBuf.Bytes()

	// 循环：每次尝试都要重新 dial（上一次的 conn 已被 agent 侧 RST 关掉），
	// 但 dial **失败**不重试、直接 502（见 streamConnectRetryWindow 的说明）。
	// 要重试的是「GET 已发出/正在发出、响应头还没回来就被 RST」—— 那正是
	// vnc_stream 还没 bind 的表现。收到任何响应头都说明已经出画，立刻退出循环。
	retryDeadline := time.Now().Add(streamConnectRetryWindow())
	var conn net.Conn
	var resp *http.Response
	var lastErr error
	var attempts int
	var timedOut bool
	var lastStage string
	var lastErrorAt time.Time
	var headerElapsed time.Duration

	for attempts = 1; ; attempts++ {
		// Dial the upstream tunnel port (lportfwd listener on this server)
		fmt.Printf("[VNC] Stream proxy: dialing %s (timeout=%s)\n", targetAddr, dialTimeout)
		dialStart := time.Now()
		dialer := net.Dialer{Timeout: dialTimeout}
		conn, lastErr = dialer.Dial("tcp", targetAddr)
		if lastErr != nil {
			fmt.Printf("[VNC] Stream proxy: failed to connect to %s for local_port=%d: %v\n", targetAddr, localPort, lastErr)
			http.Error(w, fmt.Sprintf("Failed to connect to VNC stream on local port %d: %v", localPort, lastErr), 502)
			return
		}
		fmt.Printf("[VNC] Stream proxy: dial ok %s (local_port=%d, elapsed=%s)\n", targetAddr, localPort, time.Since(dialStart))

		// 写请求。RST 可能比这次写更早到达内核，于是**同一个竞态**会在两个地方
		// 冒出来：写的时候（本机已收到 RST）或读响应头的时候（RST 在写之后到）。
		// 线上日志抓到的是后者，本地复现抓到的是前者 —— 两者都必须重试。
		lastStage = "write"
		_ = conn.SetWriteDeadline(time.Now().Add(dialTimeout))
		_, lastErr = conn.Write(reqBytes)
		_ = conn.SetWriteDeadline(time.Time{})
		if lastErr != nil {
			lastErrorAt = time.Now()
			timedOut = isTimeoutErr(lastErr)
			fmt.Printf("[VNC] Stream proxy: failed to write GET /stream to %s (local_port=%d, attempt %d, timeout=%v): %v\n",
				targetAddr, localPort, attempts, timedOut, lastErr)
		} else {
			fmt.Printf("[VNC] Stream proxy: GET /stream written to %s (local_port=%d, bytes=%d)\n", targetAddr, localPort, len(reqBytes))

			// Wait for upstream HTTP response headers
			lastStage = "header"
			fmt.Printf("[VNC] Stream proxy: waiting for headers from %s (local_port=%d, timeout=%s)\n", targetAddr, localPort, headerTimeout)
			headerStart := time.Now()
			_ = conn.SetReadDeadline(time.Now().Add(headerTimeout))
			resp, lastErr = http.ReadResponse(bufio.NewReader(conn), upReq)
			_ = conn.SetReadDeadline(time.Time{})
			if lastErr == nil {
				fmt.Printf("[VNC] Stream proxy: response received from %s (local_port=%d, status=%d, attempt=%d, elapsed=%s)\n",
					targetAddr, localPort, resp.StatusCode, attempts, time.Since(headerStart))
				break
			}
			lastErrorAt = time.Now()
			headerElapsed = time.Since(headerStart)
			timedOut = isTimeoutErr(lastErr)
			if timedOut {
				fmt.Printf("[VNC] Stream proxy: header timeout after %s (configured=%s) from %s (local_port=%d): %v\n",
					headerElapsed, headerTimeout, targetAddr, localPort, lastErr)
			} else {
				fmt.Printf("[VNC] Stream proxy: header parse error after %s from %s (local_port=%d): %v\n",
					headerElapsed, targetAddr, localPort, lastErr)
			}
		}
		_ = conn.Close()

		// 超时、客户端已离开、或窗口用尽 ⇒ 放弃
		if timedOut || r.Context().Err() != nil || !time.Now().Before(retryDeadline) {
			// 不要 Round 到毫秒：agent 侧拒绝的 RST 是亚毫秒级回来的，
			// 舍入后这条诊断信息会变成误导性的 "after 0s"。
			detail := fmt.Sprintf("%s after %s", lastStage, time.Since(lastErrorAt))
			if lastStage == "header" {
				detail = fmt.Sprintf("%s after %s", lastStage, headerElapsed)
			}
			errBody := fmt.Sprintf(
				"VNC stream upstream error on local_port=%d: %s, gave up after %d attempt(s) (configured stream_header_timeout_sec=%d, stream_connect_retry_sec=%d). "+
					"Raise stream_header_timeout_sec (currently %d) or lower the beacon sleep. Error: %v",
				localPort, detail, attempts, Config.StreamHeaderTimeoutSec, Config.StreamConnectRetrySec,
				Config.StreamHeaderTimeoutSec, lastErr)
			http.Error(w, errBody, 502)
			return
		}

		fmt.Printf("[VNC] Stream proxy: upstream not ready for local_port=%d (attempt %d): %v — vnc_stream 可能尚未启动，%s 后重试\n",
			localPort, attempts, lastErr, streamConnectRetryInterval)
		select {
		case <-r.Context().Done():
			fmt.Printf("[VNC] Stream proxy: client %s went away while waiting for local_port=%d\n", r.RemoteAddr, localPort)
			return
		case <-time.After(streamConnectRetryInterval):
		}
	}

	defer func() {
		fmt.Printf("[VNC] Stream proxy: upstream connection to %s (local_port=%d) closed\n", targetAddr, localPort)
		_ = conn.Close()
	}()

	// Copy upstream response headers to client, skipping hop-by-hop and
	// HTTP/2-invalid headers (RFC 7540 section 8.1.2.2 forbids these on HTTP/2).
	hopByHop := map[string]bool{
		"connection":          true,
		"proxy-connection":    true,
		"keep-alive":          true,
		"transfer-encoding":   true,
		"upgrade":             true,
		"te":                  true,
		"trailer":             true,
		"proxy-authenticate":  true,
		"proxy-authorization": true,
	}
	copiedHeaders := 0
	skippedHeaders := 0
	for k, vv := range resp.Header {
		if hopByHop[strings.ToLower(k)] {
			skippedHeaders++
			continue
		}
		for _, v := range vv {
			w.Header().Add(k, v)
			copiedHeaders++
		}
	}
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Cache-Control", "no-cache, no-store")
	w.Header().Set("X-Accel-Buffering", "no")
	w.WriteHeader(resp.StatusCode)

	// Flush headers immediately — critical for streaming over HTTP/2
	flusher, canFlush := w.(http.Flusher)
	if canFlush {
		flusher.Flush()
	}
	fmt.Printf("[VNC] Stream proxy: headers sent to client (local_port=%d, status=%d, canFlush=%v, copied=%d, skipped=%d)\n",
		localPort, resp.StatusCode, canFlush, copiedHeaders, skippedHeaders)

	// Stream body: copy upstream MJPEG frames to client with immediate flushing
	fmt.Printf("[VNC] Stream proxy: body copy start for local_port=%d (status=%d)\n", localPort, resp.StatusCode)
	var totalBytes int64
	var chunkCount int64
	var idleCycles int64
	buf := make([]byte, 32768)
	copyReason := "eof"
	lastLogTime := time.Now()
	copyStart := time.Now()
	const readIdleTimeout = 3 * time.Second
	for {
		// Set a short read deadline so we can log idle periods and detect a stalled agent
		_ = conn.SetReadDeadline(time.Now().Add(readIdleTimeout))
		n, rerr := resp.Body.Read(buf)
		if n > 0 {
			nw, werr := w.Write(buf[:n])
			if werr != nil {
				copyReason = fmt.Sprintf("client write error after %d bytes: %v", nw, werr)
				totalBytes += int64(nw)
				fmt.Printf("[VNC] Stream proxy: body copy write error (local_port=%d, total_bytes=%d, elapsed=%s): %v\n",
					localPort, totalBytes, time.Since(copyStart), werr)
				break
			}
			totalBytes += int64(n)
			chunkCount++
			if canFlush {
				flusher.Flush()
			}
			// Log every 2 seconds to confirm streaming is alive without flooding output
			if time.Since(lastLogTime) >= 2*time.Second {
				fmt.Printf("[VNC] Stream proxy: streaming alive (local_port=%d, total_bytes=%d, chunks=%d, elapsed=%s)\n",
					localPort, totalBytes, chunkCount, time.Since(copyStart))
				lastLogTime = time.Now()
			}
		}
		if rerr != nil {
			// Distinguish idle read (deadline exceeded, no data from agent) from a real error
			if isTimeoutErr(rerr) {
				idleCycles++
				fmt.Printf("[VNC] Stream proxy: upstream idle for %s (local_port=%d, total_bytes=%d, idle_cycles=%d) — the agent vnc_stream target port may not be producing frames (capture broken, session 0 isolation, or DXGI/GDI init failed)\n",
					readIdleTimeout, localPort, totalBytes, idleCycles)
				// Keep waiting — clear the deadline and try again.
				// The handler will only exit when the client closes or a real error occurs.
				continue
			}
			if rerr == io.EOF {
				copyReason = "eof"
			} else {
				copyReason = fmt.Sprintf("upstream read error: %v", rerr)
			}
			break
		}
	}
	_ = conn.SetReadDeadline(time.Time{})
	fmt.Printf("[VNC] Stream proxy: body copy end for local_port=%d (total_bytes=%d, chunks=%d, idle_cycles=%d, elapsed=%s, reason=%s)\n",
		localPort, totalBytes, chunkCount, idleCycles, time.Since(copyStart), copyReason)
}

func handleInput(w http.ResponseWriter, r *http.Request) {
	localPort, err := proxyPortFromRequest(r)
	if err != nil {
		fmt.Printf("[VNC] Input proxy: bad request from %s: %v\n", r.RemoteAddr, err)
		http.Error(w, err.Error(), 400)
		return
	}

	body, err := io.ReadAll(r.Body)
	if err != nil {
		fmt.Printf("[VNC] Input proxy: failed to read body (local_port=%d): %v\n", localPort, err)
		http.Error(w, "Failed to read body", 400)
		return
	}

	var input struct {
		Type string          `json:"type"`
		Data json.RawMessage `json:"data"`
	}
	if err := json.Unmarshal(body, &input); err != nil {
		fmt.Printf("[VNC] Input proxy: invalid JSON (local_port=%d): %v\n", localPort, err)
		http.Error(w, "Invalid JSON", 400)
		return
	}

	var endpoint string
	switch input.Type {
	case "mouse":
		endpoint = "/mouse"
	case "keyboard":
		endpoint = "/keyboard"
	default:
		fmt.Printf("[VNC] Input proxy: invalid input type=%q (local_port=%d)\n", input.Type, localPort)
		http.Error(w, "Invalid input type", 400)
		return
	}

	fmt.Printf("[VNC] Input proxy: request type=%s endpoint=%s local_port=%d\n", input.Type, endpoint, localPort)

	targetAddr := localProxyAddr(localPort)

	client := &http.Client{
		Timeout: 5 * time.Second,
	}

	targetURL := fmt.Sprintf("http://%s%s", targetAddr, endpoint)
	resp, err := client.Post(targetURL, "application/json", bytes.NewReader(body))
	if err != nil {
		fmt.Printf("[VNC] Input proxy: failed to forward to %s (local_port=%d): %v\n", targetURL, localPort, err)
		http.Error(w, fmt.Sprintf("Failed to forward input on local_port=%d", localPort), 502)
		return
	}
	defer resp.Body.Close()

	respBody, _ := io.ReadAll(resp.Body)
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(resp.StatusCode)
	w.Write(respBody)
}

func forwardToTarget(session *VncSession, method string, path string, body []byte) error {
	targetAddr := localProxyAddr(session.LocalPort)
	url := fmt.Sprintf("http://%s%s", targetAddr, path)

	client := &http.Client{Timeout: 5 * time.Second}

	var reqBody io.Reader
	if body != nil {
		reqBody = bytes.NewReader(body)
	}

	req, err := http.NewRequest(method, url, reqBody)
	if err != nil {
		return err
	}

	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	resp.Body.Close()
	return nil
}
