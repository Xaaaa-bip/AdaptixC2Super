package connector

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/gorilla/websocket"
)

type wsRaceMockTeamserver struct {
	Teamserver
	connectDelay time.Duration
	registered   atomic.Bool
	syncSawReg   atomic.Bool
	syncDone     chan struct{}
	syncOnce     sync.Once
}

func (m *wsRaceMockTeamserver) TsClientConnect(username string, version string, socket *websocket.Conn, clientType uint8, consoleTeamMode bool, subscriptions []string) string {
	time.Sleep(m.connectDelay)
	m.registered.Store(true)
	return username + "-conn-1"
}

func (m *wsRaceMockTeamserver) TsClientDisconnectConn(connId string, username string) {
	m.registered.Store(false)
}

func (m *wsRaceMockTeamserver) TsClientExists(username string) bool {
	return m.registered.Load()
}

func (m *wsRaceMockTeamserver) TsClientSync(username string) {
	m.syncSawReg.Store(m.registered.Load())
	m.syncOnce.Do(func() { close(m.syncDone) })
}

func TestConnectOTPSyncAndChannelWaitForRegistration(t *testing.T) {
	gin.SetMode(gin.TestMode)

	mock := &wsRaceMockTeamserver{
		connectDelay: 60 * time.Millisecond,
		syncDone:     make(chan struct{}),
	}
	tc := &TsConnector{teamserver: mock}

	r := gin.New()
	r.GET("/connect", func(ctx *gin.Context) {
		ctx.Set("otpType", "connect")
		ctx.Set("otpData", ConnectOTPData{
			Username:   "alice",
			Version:    "v1.2",
			ClientType: ClientTypeWEB,
		})
		tc.tcConnectOTP(ctx)
	})
	r.POST("/sync", func(ctx *gin.Context) {
		ctx.Set("username", "alice")
		tc.tcSync(ctx)
	})
	r.GET("/channel", func(ctx *gin.Context) {
		ctx.Set("otpType", "channel_terminal")
		ctx.Set("otpData", &ChannelOTPData{
			Username:    "alice",
			ChannelData: []byte(`{}`),
		})
		tc.tcChannelOTP(ctx)
	})

	srv := httptest.NewServer(r)
	defer srv.Close()

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http") + "/connect"
	wsConn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		t.Fatalf("dial ws failed: %v", err)
	}
	defer wsConn.Close()

	// Fire /sync immediately upon 101 Switching Protocols (simulating browser ws.onopen).
	resp, err := http.Post(srv.URL+"/sync", "application/json", nil)
	if err != nil {
		t.Fatalf("POST /sync failed: %v", err)
	}
	_ = resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("expected 200 from /sync, got %d", resp.StatusCode)
	}

	select {
	case <-mock.syncDone:
	case <-time.After(2 * time.Second):
		t.Fatalf("timed out waiting for TsClientSync")
	}

	if !mock.syncSawReg.Load() {
		t.Fatalf("TsClientSync ran before TsClientConnect finished registration")
	}
	if !mock.TsClientExists("alice") {
		t.Fatalf("expected client alice to exist immediately after sync")
	}
}
