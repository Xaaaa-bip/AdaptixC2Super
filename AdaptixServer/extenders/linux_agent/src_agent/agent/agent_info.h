#ifndef AGENT_INFO_H
#define AGENT_INFO_H

#include "msgpack.h"
#include <stdint.h>

/// System info collection for session registration.
/// Individual getters fill a caller-provided buffer, return 0 on success.
/// create_session_info() builds the full SessionInfo msgpack payload.

int agent_info_hostname(char *buf, int len);
int agent_info_username(char *buf, int len);
int agent_info_ipaddr(char *buf, int len);
int agent_info_osversion(char *buf, int len);

/// Build SessionInfo msgpack payload matching Go's utils.SessionInfo struct.
/// Also generates a random session encryption key.
///
/// When use_gopher=0 (LinuxTCP):
///   msgpack map with 11 fields, session_key = 16 bytes (AES-128 key)
/// When use_gopher=1 (GopherTCP):
///   msgpack map with 12 fields (adds transport_salt), session_key = 32 bytes (key || salt)
///
/// Returns 0 on success, fills session_key.
int create_session_info(mp_writer_t *w, uint8_t *session_key, int use_gopher);

#endif /* AGENT_INFO_H */
