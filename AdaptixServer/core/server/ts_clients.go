package server

import (
	"AdaptixServer/core/eventing"
	"strings"
	"sync"

	"github.com/gorilla/websocket"
)

const SMALL_VERSION = "v1.2"

func (ts *Teamserver) TsClientExists(username string) bool {
	return ts.Broker.ClientExists(username)
}

var InitialSyncCategories = []string{
	SyncCategoryExtenders,
	SyncCategoryScripts,
	SyncCategoryListeners,
	SyncCategoryAgents,
	SyncCategoryPivots,
}

// TsClientConnect returns connection id for multi-session disconnect/sync.
func (ts *Teamserver) TsClientConnect(username string, version string, socket *websocket.Conn, clientType uint8, consoleTeamMode bool, subscriptions []string) string {
	// --- PRE HOOK ---
	preEvent := &eventing.EventDataClientConnect{Username: username}
	if !ts.EventManager.Emit(eventing.EventClientConnect, eventing.HookPre, preEvent) {
		return ""
	}
	// ----------------

	supportsBatchSync := version == SMALL_VERSION

	client := NewClientHandler(username, socket, supportsBatchSync, ts.Broker, clientType, consoleTeamMode)
	client.Start()

	requested := make(map[string]struct{}, len(subscriptions))
	for _, cat := range subscriptions {
		requested[cat] = struct{}{}
	}
	_, hasOnlyActive := requested[SyncCategoryAgentsOnlyActive]
	_, hasAgents := requested[SyncCategoryAgents]
	useOnlyActiveAgents := hasOnlyActive && !hasAgents

	for _, cat := range InitialSyncCategories {
		// Web UI (clientType == 2) fetches command schema via REST /api/axscript/commands
		// and does not consume raw AX extender/script blobs over WS; skipping them keeps
		// initial sync fast so real-time AGENT_TICK heartbeats start immediately.
		if clientType == 2 && (cat == SyncCategoryExtenders || cat == SyncCategoryScripts) {
			continue
		}
		if cat == SyncCategoryAgents && useOnlyActiveAgents {
			client.Subscribe(SyncCategoryAgentsOnlyActive)
			continue
		}
		client.Subscribe(cat)
	}
	for _, cat := range subscriptions {
		client.Subscribe(cat)
	}

	ts.Broker.Register(client)

	// --- POST HOOK ---
	postEvent := &eventing.EventDataClientConnect{Username: username}
	ts.EventManager.EmitAsync(eventing.EventClientConnect, postEvent)
	// -----------------
	return client.ConnId()
}

// TsClientDisconnectConn ends one WS session; tunnels/hooks only when last session of user leaves.
func (ts *Teamserver) TsClientDisconnectConn(connId string, username string) {
	// --- PRE HOOK ---
	preEvent := &eventing.EventDataClientDisconnect{Username: username}
	ts.EventManager.Emit(eventing.EventClientDisconnect, eventing.HookPre, preEvent)
	// ----------------

	ts.Broker.Unregister(connId)

	// only cleanup user-scoped resources when no more sessions for this username
	if !ts.Broker.ClientExists(username) {
		ts.TsNotifyClient(false, username)

		var tunnelIds []string
		ts.TunnelManager.ForEachTunnel(func(key string, tunnel *Tunnel) bool {
			if tunnel.Data.Client == username {
				tunnelIds = append(tunnelIds, tunnel.Data.TunnelId)
			}
			return true
		})
		for _, id := range tunnelIds {
			_ = ts.TsTunnelStop(id)
		}

		ts.TsProcessHookJobsForDisconnectedClient(username)
	}

	// --- POST HOOK ---
	postEvent := &eventing.EventDataClientDisconnect{Username: username}
	ts.EventManager.EmitAsync(eventing.EventClientDisconnect, postEvent)
	// -----------------
}

// TsClientDisconnect legacy: drop ALL sessions of username
func (ts *Teamserver) TsClientDisconnect(username string) {
	for _, c := range ts.Broker.GetClients(username) {
		ts.TsClientDisconnectConn(c.ConnId(), username)
	}
}

func (ts *Teamserver) TsClientSync(username string) {
	clients := ts.Broker.GetClients(username)
	if len(clients) == 0 {
		return
	}

	// Sync each unsynced connection concurrently so a half-open/stalled session
	// from before F5 cannot block a freshly opened session of the same user.
	var wg sync.WaitGroup
	for _, client := range clients {
		if client.IsSynced() || client.IsClosed() {
			continue
		}
		wg.Add(1)
		go func(c *ClientHandler) {
			defer wg.Done()
			c.syncMu.Lock()
			defer c.syncMu.Unlock()

			if c.IsSynced() || c.IsClosed() {
				return
			}
			categories := c.GetSubscriptions()
			ts.TsSyncCategories(c, categories)

			const maxDrainPasses = 3
			for pass := 0; pass < maxDrainPasses; pass++ {
				if c.IsClosed() {
					return
				}
				buffered, stateBuffered, done := c.DrainBufferOrMarkSynced()
				if done {
					return
				}
				for _, v := range buffered {
					if c.IsClosed() {
						return
					}
					if data, ok := v.([]byte); ok {
						c.SendSync(data)
					}
				}
				for _, data := range stateBuffered {
					if c.IsClosed() {
						return
					}
					c.SendSync(data)
				}
			}

			// Final bounded handoff: mark synced atomically so continuous 800ms
			// agent ticks cannot livelock the drain loop, then flush leftovers.
			if !c.IsClosed() {
				buffered, stateBuffered := c.MarkSyncedAndDrain()
				for _, v := range buffered {
					if c.IsClosed() {
						return
					}
					if data, ok := v.([]byte); ok {
						c.SendSync(data)
					}
				}
				for _, data := range stateBuffered {
					if c.IsClosed() {
						return
					}
					c.SendSync(data)
				}
			}
		}(client)
	}
	wg.Wait()

	ts.TsNotifyClient(true, username)
}

func (ts *Teamserver) TsClientSubscribe(username string, categories []string, consoleTeamMode *bool) {
	clients := ts.Broker.GetClients(username)
	if len(clients) == 0 {
		return
	}

	for _, client := range clients {
		func(c *ClientHandler) {
			c.syncMu.Lock()
			defer c.syncMu.Unlock()

			if c.IsClosed() {
				return
			}
			if consoleTeamMode != nil {
				c.SetConsoleTeamMode(*consoleTeamMode)
			}

			seen := make(map[string]struct{}, len(categories))
			var newCategories []string
			for _, raw := range categories {
				category := strings.TrimSpace(raw)
				if category == "" {
					continue
				}
				if _, exists := seen[category]; exists {
					continue
				}
				seen[category] = struct{}{}

				if !c.IsSubscribed(category) {
					c.Subscribe(category)
					switch category {
					case SyncCategoryTasksManager,
						SyncCategoryTasksOnlyJobs,
						SyncCategoryChatRealtime,
						SyncCategoryDownloadsRealtime,
						SyncCategoryScreenshotRealtime,
						SyncCategoryCredentialsRealtime,
						SyncCategoryTargetsRealtime:
					default:
						newCategories = append(newCategories, category)
					}
				}
			}

			if len(newCategories) > 0 {
				ts.TsSyncCategories(c, newCategories)
			} else {
				ts.sendSyncPackets(c, nil)
			}
		}(client)
	}
}
