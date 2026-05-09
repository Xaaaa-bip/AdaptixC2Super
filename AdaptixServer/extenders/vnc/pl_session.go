package main

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"time"
)

type VncSession struct {
	Id          string    `json:"id"`
	AgentId     string    `json:"agent_id"`
	TargetHost  string    `json:"target_host"`
	TargetPort  int       `json:"target_port"`
	LocalPort   int       `json:"local_port"`
	TunnelId    string    `json:"tunnel_id"`
	Status      string    `json:"status"`
	Quality     int       `json:"quality"`
	CreatedAt   time.Time `json:"created_at"`
	CreatedBy   string    `json:"created_by"`
}

func generateSessionId() string {
	b := make([]byte, 8)
	rand.Read(b)
	return hex.EncodeToString(b)
}

func createSession(agentId string, targetPort int, localPort int, operator string) (*VncSession, error) {
	sessLock.Lock()
	defer sessLock.Unlock()

	session := &VncSession{
		Id:         generateSessionId(),
		AgentId:    agentId,
		TargetHost: "127.0.0.1",
		TargetPort: targetPort,
		LocalPort:  localPort,
		Status:     "creating",
		Quality:    Config.StreamQuality,
		CreatedAt:  time.Now(),
		CreatedBy:  operator,
	}

	var err error
	session.TunnelId, err = Ts.TsTunnelCreateLportfwd(
		agentId,
		"VNC "+session.Id,
		Config.BindHost,
		localPort,
		session.TargetHost,
		targetPort,
	)
	if err != nil {
		return nil, fmt.Errorf("failed to create tunnel: %v", err)
	}

	_, err = Ts.TsTunnelStart(session.TunnelId)
	if err != nil {
		return nil, fmt.Errorf("failed to start tunnel: %v", err)
	}

	session.Status = "active"
	sessions[session.Id] = session

	fmt.Printf("[VNC] Session %s created by %s: agent=%s, tunnel=%s, local=%s:%d -> target=%s:%d\n",
		session.Id, operator, agentId, session.TunnelId,
		Config.BindHost, localPort, session.TargetHost, targetPort)

	return session, nil
}

func stopSession(sessionId string) error {
	sessLock.Lock()
	defer sessLock.Unlock()

	session, ok := sessions[sessionId]
	if !ok {
		return fmt.Errorf("session not found: %s", sessionId)
	}

	if session.TunnelId != "" {
		if err := Ts.TsTunnelStop(session.TunnelId); err != nil {
			fmt.Printf("[VNC] Warning: error stopping tunnel %s: %v\n", session.TunnelId, err)
		}
	}

	session.Status = "stopped"
	delete(sessions, sessionId)

	fmt.Printf("[VNC] Session %s stopped\n", sessionId)
	return nil
}

func getSession(sessionId string) (*VncSession, error) {
	sessLock.RLock()
	defer sessLock.RUnlock()

	session, ok := sessions[sessionId]
	if !ok {
		return nil, fmt.Errorf("session not found: %s", sessionId)
	}
	return session, nil
}
