package connector

import (
	"AdaptixServer/core/utils/krypt"
	"AdaptixServer/core/utils/logs"
	"AdaptixServer/core/utils/token"
	"context"
	"errors"
	"net/http"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
)

const (
	ClientTypeUI  uint8 = 1
	ClientTypeWEB uint8 = 2
	ClientTypeCLI uint8 = 3
)

type Credentials struct {
	Username string `json:"username"`
	Password string `json:"password"`
	Version  string `json:"version"`
}

type AccessJWT struct {
	AccessToken string `json:"access_token"`
}

type SubscribeRequest struct {
	Categories      []string `json:"categories"`
	ConsoleTeamMode *bool    `json:"console_team_mode,omitempty"`
}

func (tc *TsConnector) tcLogin(ctx *gin.Context) {
	var (
		creds Credentials
		err   error
	)

	err = ctx.ShouldBindJSON(&creds)
	if err != nil {
		// Do not use ctx.Error() here: default404Middleware would replace the body with a fake HTML 404,
		// and the Qt client only parses JSON (no access_token → login failure with no useful feedback).
		ctx.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "invalid_request", "message": "invalid JSON body"})
		return
	}

	recvHash := krypt.SHA256([]byte(creds.Password))

	if tc.OnlyHash {
		if recvHash != tc.Hash {
			ctx.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "unauthorized", "message": "incorrect password"})
			return
		}
	} else {
		if recvHash != tc.Operators[creds.Username] {
			ctx.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "unauthorized", "message": "unknown user or incorrect password"})
			return
		}
	}

	accessToken, err := token.GenerateAccessToken(creds.Username, creds.Version)
	if err != nil {
		ctx.AbortWithStatusJSON(http.StatusInternalServerError, gin.H{"error": "server_error", "message": "could not generate access token"})
		return
	}

	refreshToken, err := token.GenerateRefreshToken(creds.Username, creds.Version)
	if err != nil {
		ctx.AbortWithStatusJSON(http.StatusInternalServerError, gin.H{"error": "server_error", "message": "could not generate refresh token"})
		return
	}

	ctx.JSON(http.StatusOK, gin.H{"access_token": accessToken, "refresh_token": refreshToken})
}

func (tc *TsConnector) beginPendingConnect(username string) {
	tc.pendingConnectMu.Lock()
	if tc.pendingConnect == nil {
		tc.pendingConnect = make(map[string]*pendingConnectEntry)
	}
	if entry, ok := tc.pendingConnect[username]; ok {
		entry.count++
	} else {
		tc.pendingConnect[username] = &pendingConnectEntry{
			count: 1,
			done:  make(chan struct{}),
		}
	}
	tc.pendingConnectMu.Unlock()
}

func (tc *TsConnector) endPendingConnect(username string) {
	tc.pendingConnectMu.Lock()
	if entry, ok := tc.pendingConnect[username]; ok {
		entry.count--
		if entry.count <= 0 {
			close(entry.done)
			delete(tc.pendingConnect, username)
		}
	}
	tc.pendingConnectMu.Unlock()
}

func (tc *TsConnector) waitPendingConnect(ctx context.Context, username string) {
	tc.pendingConnectMu.Lock()
	entry, ok := tc.pendingConnect[username]
	var done chan struct{}
	if ok {
		done = entry.done
	}
	tc.pendingConnectMu.Unlock()
	if !ok {
		return
	}

	timer := time.NewTimer(5 * time.Second)
	defer timer.Stop()
	select {
	case <-done:
	case <-ctx.Done():
	case <-timer.C:
	}
}

func (tc *TsConnector) tcWebsocketReadLoop(connId string, username string, wsConn *websocket.Conn) {
	for {
		_, _, err := wsConn.ReadMessage()
		if err == nil {
			continue
		}

		if connId != "" {
			tc.teamserver.TsClientDisconnectConn(connId, username)
		} else {
			tc.teamserver.TsClientDisconnect(username)
		}
		break
	}
}

func (tc *TsConnector) tcSync(ctx *gin.Context) {
	value, exists := ctx.Get("username")
	if !exists {
		ctx.JSON(http.StatusOK, gin.H{"message": "Server error: username not found in context", "ok": false})
		return
	}
	username, ok := value.(string)
	if !ok {
		ctx.JSON(http.StatusOK, gin.H{"message": "Server error: invalid username type in context", "ok": false})
		return
	}

	tc.waitPendingConnect(ctx.Request.Context(), username)
	go tc.teamserver.TsClientSync(username)

	ctx.JSON(http.StatusOK, gin.H{"message": "sync started", "ok": true})
}

func (tc *TsConnector) tcSubscribe(ctx *gin.Context) {
	value, exists := ctx.Get("username")
	if !exists {
		ctx.JSON(http.StatusOK, gin.H{"message": "Server error: username not found in context", "ok": false})
		return
	}
	username, ok := value.(string)
	if !ok {
		ctx.JSON(http.StatusOK, gin.H{"message": "Server error: invalid username type in context", "ok": false})
		return
	}

	var req SubscribeRequest
	if err := ctx.ShouldBindJSON(&req); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{"message": "Invalid subscribe request: " + err.Error(), "ok": false})
		return
	}

	if len(req.Categories) == 0 {
		ctx.JSON(http.StatusBadRequest, gin.H{"message": "No categories specified", "ok": false})
		return
	}

	tc.waitPendingConnect(ctx.Request.Context(), username)
	go tc.teamserver.TsClientSubscribe(username, req.Categories, req.ConsoleTeamMode)

	ctx.JSON(http.StatusOK, gin.H{"message": "subscribe started", "ok": true})
}

/// OTP

func (tc *TsConnector) tcConnectOTP(ctx *gin.Context) {
	otpType, _ := ctx.Get("otpType")
	if otpType != "connect" {
		_ = ctx.Error(errors.New("invalid OTP type"))
		return
	}

	data, _ := ctx.Get("otpData")
	wsData, ok := data.(ConnectOTPData)
	if !ok {
		_ = ctx.Error(errors.New("invalid OTP data"))
		return
	}

	// Allow multi-login / multi-session under same or different usernames.
	// (Previously rejected with "Client already connected" → chat/sync isolated.)

	clientType := wsData.ClientType
	if clientType == 0 {
		clientType = ClientTypeUI
	}

	// Mark connect in-flight BEFORE Upgrade sends 101 so an immediate /api/sync
	// or /api/channel triggered by client onopen cannot race ahead of Broker.Register.
	tc.beginPendingConnect(wsData.Username)
	released := false
	releasePending := func() {
		if !released {
			released = true
			tc.endPendingConnect(wsData.Username)
		}
	}
	defer releasePending()

	var wsUpgrader websocket.Upgrader
	wsUpgrader.CheckOrigin = func(r *http.Request) bool {
		return true
	}
	wsConn, err := wsUpgrader.Upgrade(ctx.Writer, ctx.Request, nil)
	if err != nil {
		logs.Error("", "WebSocket upgrade error: "+err.Error())
		return
	}

	if wsConn == nil {
		logs.Error("", "WebSocket is nil")
		return
	}

	// Register synchronously before spawning the read loop so the session is visible
	// in Broker immediately once the pending barrier is released.
	connId := tc.teamserver.TsClientConnect(wsData.Username, wsData.Version, wsConn, clientType, wsData.ConsoleTeamMode, wsData.Subscriptions)
	releasePending()
	if connId == "" {
		_ = wsConn.Close()
		return
	}

	go tc.tcWebsocketReadLoop(connId, wsData.Username, wsConn)
}

func (tc *TsConnector) tcChannelOTP(ctx *gin.Context) {
	otpTypeVal, _ := ctx.Get("otpType")
	otpType, _ := otpTypeVal.(string)
	if !strings.HasPrefix(otpType, "channel_") {
		_ = ctx.Error(errors.New("invalid OTP type"))
		return
	}

	data, _ := ctx.Get("otpData")
	wsData, ok := data.(*ChannelOTPData)
	if !ok || wsData == nil {
		_ = ctx.Error(errors.New("invalid OTP data"))
		return
	}

	tc.waitPendingConnect(ctx.Request.Context(), wsData.Username)
	exists := tc.teamserver.TsClientExists(wsData.Username)
	if !exists {
		ctx.JSON(http.StatusNetworkAuthenticationRequired, gin.H{"message": "Server error: client not connected", "ok": false})
		return
	}

	var wsUpgrader websocket.Upgrader
	wsUpgrader.CheckOrigin = func(r *http.Request) bool {
		return true
	}
	wsConn, err := wsUpgrader.Upgrade(ctx.Writer, ctx.Request, nil)
	if err != nil {
		logs.Error("", "WebSocket upgrade error: "+err.Error())
		return
	}

	if wsConn == nil {
		logs.Error("", "WebSocket is nil")
		return
	}

	channelDataStr := string(wsData.ChannelData)

	switch otpType {
	case "channel_tunnel":
		if err := tc.teamserver.TsTunnelClientNewChannel(channelDataStr, wsConn); err != nil {
			logs.Error("", "Tunnel channel error: "+err.Error())
			wsConn.Close()
		}

	case "channel_terminal":
		if err := tc.teamserver.TsAgentTerminalCreateChannel(channelDataStr, wsConn); err != nil {
			logs.Error("", "Terminal channel error: "+err.Error())
			wsConn.Close()
		}

	case "channel_agent_build":
		go func() {
			if err := tc.teamserver.TsAgentBuildCreateChannel(channelDataStr, wsConn); err != nil {
				logs.Error("", "Agent build channel error: "+err.Error())
			}
		}()

	default:
		logs.Error("", "Unknown channel type: "+otpType)
		wsConn.Close()
	}
}
