package server

import (
	"AdaptixServer/core/utils/proxy"
	"AdaptixServer/core/utils/safe"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/Adaptix-Framework/axc2"
	"github.com/gorilla/websocket"
)

func newTestWSPair(t *testing.T) (serverConn *websocket.Conn, clientConn *websocket.Conn, cleanup func()) {
	t.Helper()
	upgrader := websocket.Upgrader{CheckOrigin: func(r *http.Request) bool { return true }}
	srvConnCh := make(chan *websocket.Conn, 1)

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		c, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			return
		}
		srvConnCh <- c
	}))

	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")
	cli, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		srv.Close()
		t.Fatalf("dial test ws: %v", err)
	}

	var sc *websocket.Conn
	select {
	case sc = <-srvConnCh:
	case <-time.After(2 * time.Second):
		_ = cli.Close()
		srv.Close()
		t.Fatalf("timeout accepting server ws")
	}

	// Drain client reads so server writes never stall on socket buffers.
	go func() {
		for {
			if _, _, err := cli.ReadMessage(); err != nil {
				return
			}
		}
	}()

	return sc, cli, func() {
		_ = sc.Close()
		_ = cli.Close()
		srv.Close()
	}
}

func TestBrokerSyncRegisterUnregisterAndDrainNoLoss(t *testing.T) {
	sc, _, cleanup := newTestWSPair(t)
	defer cleanup()

	mb := NewMessageBroker()
	ch := NewClientHandler("bob", sc, true, mb, 2, true)
	defer ch.Close()

	// Synchronous Register must be immediately visible to GetClients/ClientExists
	// even without waiting on an async goroutine turn.
	mb.Register(ch)
	if !mb.ClientExists("bob") {
		t.Fatalf("ClientExists returned false immediately after Register")
	}
	if clients := mb.GetClients("bob"); len(clients) != 1 || clients[0].ConnId() != ch.ConnId() {
		t.Fatalf("GetClients did not immediately return registered client: %+v", clients)
	}

	// Stress BufferForSync concurrently with DrainBufferOrMarkSynced:
	// every produced message must either be drained OR forwarded to sendChan.
	const total = 400
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		for i := 0; i < total; i++ {
			ch.BufferForSync([]byte("ev"), MsgTypeEvent, "")
		}
	}()

	drained := 0
	for {
		buf, _, done := ch.DrainBufferOrMarkSynced()
		drained += len(buf)
		if done {
			break
		}
	}
	wg.Wait()

	sentAfterSync := len(ch.sendChan)
	if drained+sentAfterSync != total {
		t.Fatalf("lost events across sync transition: drained=%d sentAfterSync=%d total=%d", drained, sentAfterSync, total)
	}

	// Synchronous Unregister must immediately clear ClientExists.
	mb.Unregister(ch.ConnId())
	if mb.ClientExists("bob") {
		t.Fatalf("ClientExists still true immediately after Unregister")
	}
}

func TestTunnelChannelConcurrentWSWriteAndClose(t *testing.T) {
	sc, _, cleanup := newTestWSPair(t)
	defer cleanup()

	tm := NewTunnelManager(nil)
	tunnel := &Tunnel{
		Type:        adaptix.TUNNEL_TYPE_SOCKS5,
		Data:        adaptix.TunnelData{TunnelId: "tun-1", Client: "bob"},
		connections: safe.NewMap(),
	}
	stc := NewSafeTunnelChannel(tm, 42, nil, sc, "TCP")
	tm.RegisterChannel("tun-1", tunnel, stc.TunnelChannel)

	var wg sync.WaitGroup
	// Writer 1: ingressPump via WriteToChannelByIdOnly
	wg.Add(1)
	go func() {
		defer wg.Done()
		for i := 0; i < 300; i++ {
			tm.WriteToChannelByIdOnly(42, []byte("payload"))
		}
	}()

	// Writer 2: SOCKS status replies under wsWriteMu (as in Resume/Halt)
	wg.Add(1)
	go func() {
		defer wg.Done()
		for i := 0; i < 100; i++ {
			stc.wsWriteMu.Lock()
			proxy.ReplySocks5StatusWs(stc.wsconn, adaptix.SOCKS5_SUCCESS)
			stc.wsWriteMu.Unlock()
		}
	}()

	// Concurrent closers (both TunnelManager.CloseChannel and SafeTunnelChannel.Close)
	time.Sleep(5 * time.Millisecond)
	wg.Add(2)
	go func() {
		defer wg.Done()
		tm.CloseChannel("tun-1", 42)
	}()
	go func() {
		defer wg.Done()
		stc.Close()
	}()

	wg.Wait()

	// Writing after close must return false without panicking.
	if ok := tm.WriteToChannelByIdOnly(42, []byte("after-close")); ok {
		t.Fatalf("expected WriteToChannelByIdOnly to return false after close")
	}
}

func TestSyncStartAndAgentTickCarryServerTime(t *testing.T) {
	start := CreateSpSyncStart(3, []string{"127.0.0.1"})
	if start.ServerTime <= 0 {
		t.Fatalf("expected SyncPackerStart.ServerTime > 0, got %d", start.ServerTime)
	}
	tick := CreateSpAgentTick([]string{"a1", "a2"})
	if tick.LastTick <= 0 {
		t.Fatalf("expected SyncPackerAgentTick.LastTick > 0, got %d", tick.LastTick)
	}

	// Verify atomic Tick Store/Swap does not lose a concurrent tick.
	a := &Agent{}
	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		for i := 0; i < 1000; i++ {
			a.Tick.Store(true)
		}
	}()
	swapped := 0
	go func() {
		defer wg.Done()
		for i := 0; i < 1000; i++ {
			if a.Tick.Swap(false) {
				swapped++
			}
		}
	}()
	wg.Wait()
	if a.Tick.Swap(false) {
		swapped++
	}
	if swapped == 0 {
		t.Fatalf("expected at least one swapped tick")
	}
}
