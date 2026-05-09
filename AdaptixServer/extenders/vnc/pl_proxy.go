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

	// Dial the upstream tunnel port (lportfwd listener on this server)
	fmt.Printf("[VNC] Stream proxy: dialing %s (timeout=%s)\n", targetAddr, dialTimeout)
	dialStart := time.Now()
	dialer := net.Dialer{Timeout: dialTimeout}
	conn, err := dialer.Dial("tcp", targetAddr)
	if err != nil {
		fmt.Printf("[VNC] Stream proxy: failed to connect to %s for local_port=%d: %v\n", targetAddr, localPort, err)
		http.Error(w, fmt.Sprintf("Failed to connect to VNC stream on local port %d: %v", localPort, err), 502)
		return
	}
	defer func() {
		fmt.Printf("[VNC] Stream proxy: upstream connection to %s (local_port=%d) closed\n", targetAddr, localPort)
		conn.Close()
	}()
	fmt.Printf("[VNC] Stream proxy: dial ok %s (local_port=%d, elapsed=%s)\n", targetAddr, localPort, time.Since(dialStart))

	// Send GET /stream to the upstream (vnc_stream agent via tunnel)
	upReq, _ := http.NewRequest("GET", "/stream", nil)
	var reqBuf bytes.Buffer
	_ = upReq.Write(&reqBuf)
	reqBytes, err := conn.Write(reqBuf.Bytes())
	if err != nil {
		fmt.Printf("[VNC] Stream proxy: failed to write GET /stream to %s (local_port=%d): %v\n", targetAddr, localPort, err)
		http.Error(w, fmt.Sprintf("Failed to send request to VNC stream on local port %d: %v", localPort, err), 502)
		return
	}
	fmt.Printf("[VNC] Stream proxy: GET /stream written to %s (local_port=%d, bytes=%d)\n", targetAddr, localPort, reqBytes)

	// Wait for upstream HTTP response headers
	fmt.Printf("[VNC] Stream proxy: waiting for headers from %s (local_port=%d, timeout=%s)\n", targetAddr, localPort, headerTimeout)
	headerStart := time.Now()
	_ = conn.SetReadDeadline(time.Now().Add(headerTimeout))
	resp, err := http.ReadResponse(bufio.NewReader(conn), upReq)
	_ = conn.SetReadDeadline(time.Time{})
	if err != nil {
		elapsed := time.Since(headerStart)
		timedOut := isTimeoutErr(err)
		if timedOut {
			fmt.Printf("[VNC] Stream proxy: header timeout after %s (configured=%s) from %s (local_port=%d): %v\n",
				elapsed, headerTimeout, targetAddr, localPort, err)
		} else {
			fmt.Printf("[VNC] Stream proxy: header parse error after %s from %s (local_port=%d): %v\n",
				elapsed, targetAddr, localPort, err)
		}
		reason := "parse error"
		if timedOut {
			reason = "timeout"
		}
		errBody := fmt.Sprintf(
			"VNC stream upstream error on local_port=%d: %s after %s (configured stream_header_timeout_sec=%d). "+
				"Raise stream_header_timeout_sec (currently %d) or lower the beacon sleep. Error: %v",
			localPort, reason, elapsed.Round(time.Millisecond), Config.StreamHeaderTimeoutSec,
			Config.StreamHeaderTimeoutSec, err)
		http.Error(w, errBody, 502)
		return
	}
	fmt.Printf("[VNC] Stream proxy: response received from %s (local_port=%d, status=%d, elapsed=%s)\n",
		targetAddr, localPort, resp.StatusCode, time.Since(headerStart))

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
