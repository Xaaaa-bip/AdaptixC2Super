package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"strconv"
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

func handleStream(w http.ResponseWriter, r *http.Request) {
	localPort, err := proxyPortFromRequest(r)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	targetAddr := localProxyAddr(localPort)

	dialer := net.Dialer{Timeout: 5 * time.Second}
	conn, err := dialer.Dial("tcp", targetAddr)
	if err != nil {
		fmt.Printf("[VNC] Stream proxy: failed to connect to %s: %v\n", targetAddr, err)
		http.Error(w, "Failed to connect to VNC stream", 502)
		return
	}
	defer conn.Close()

	req, _ := http.NewRequest("GET", "/stream", nil)
	if err := req.Write(conn); err != nil {
		fmt.Printf("[VNC] Stream proxy: failed to write request: %v\n", err)
		http.Error(w, "Failed to send request", 502)
		return
	}

	_ = conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	resp, err := http.ReadResponse(bufio.NewReader(conn), req)
	_ = conn.SetReadDeadline(time.Time{})
	if err != nil {
		fmt.Printf("[VNC] Stream proxy: local port %d connected, but target did not return /stream HTTP response: %v\n", localPort, err)
		http.Error(w, fmt.Sprintf("VNC stream target on local port %d did not return HTTP. Check the vnc_stream run task stderr/stdout and confirm the agent target port is listening.", localPort), 502)
		return
	}

	for k, vv := range resp.Header {
		for _, v := range vv {
			w.Header().Add(k, v)
		}
	}
	w.WriteHeader(resp.StatusCode)

	buf := make([]byte, 32768)
	flusher, canFlush := w.(http.Flusher)
	for {
		n, err := resp.Body.Read(buf)
		if n > 0 {
			if _, werr := w.Write(buf[:n]); werr != nil {
				return
			}
			if canFlush {
				flusher.Flush()
			}
		}
		if err != nil {
			return
		}
	}
}

func handleInput(w http.ResponseWriter, r *http.Request) {
	localPort, err := proxyPortFromRequest(r)
	if err != nil {
		http.Error(w, err.Error(), 400)
		return
	}

	body, err := io.ReadAll(r.Body)
	if err != nil {
		http.Error(w, "Failed to read body", 400)
		return
	}

	var input struct {
		Type string          `json:"type"`
		Data json.RawMessage `json:"data"`
	}
	if err := json.Unmarshal(body, &input); err != nil {
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
		http.Error(w, "Invalid input type", 400)
		return
	}

	targetAddr := localProxyAddr(localPort)

	client := &http.Client{
		Timeout: 5 * time.Second,
	}

	targetURL := fmt.Sprintf("http://%s%s", targetAddr, endpoint)
	resp, err := client.Post(targetURL, "application/json", bytes.NewReader(body))
	if err != nil {
		fmt.Printf("[VNC] Input proxy: failed to forward to %s: %v\n", targetURL, err)
		http.Error(w, "Failed to forward input", 502)
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
