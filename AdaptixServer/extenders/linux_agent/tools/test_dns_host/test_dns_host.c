/* dns.c 宿主单元测试：桩掉 syscall，响应由真实发出的 query 现造（txid/QNAME 自动一致） */
#include <stdio.h>
#include <string.h>
#include <stdint.h>


static void build_response_from_query(const uint8_t *q);

/* ── crt 桩 ── */
void *ax_memset(void *s, int c, size_t n) { return memset(s, c, n); }
void *ax_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
size_t ax_strlen(const char *s) { return strlen(s); }
int ax_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
char *ax_strchr(const char *s, int c) { return strchr(s, c); }
int ax_random_bytes(void *buf, size_t len) { memset(buf, 0xAB, len); return 0; }
uint64_t ax_monotonic_ms(void) { static uint64_t t = 1000; return t += 100; }

/* ── syscall 桩 ── */
static uint8_t written_query[512]; static size_t written_len;
static uint8_t canned_resp[512]; static size_t canned_len;
static const char *resolv_content = "nameserver 192.168.1.1\noptions timeout:2\nnameserver 10.0.0.53\n";
static int g_dbg = 1;

int sys_open(const char *path, int flags, int mode) { if (g_dbg) fprintf(stderr, "[dbg] open %s\n", path); (void)flags; (void)mode; return 7; }
int sys_close(int fd) { (void)fd; return 0; }
long sys_read(int fd, void *buf, size_t count) {
    (void)fd; if (g_dbg) fprintf(stderr, "[dbg] read fd=%d count=%zu\n", fd, count);
    if (canned_len) { size_t n = canned_len < count ? canned_len : count; memcpy(buf, canned_resp, n); canned_len = 0; return (long)n; }
    size_t avail = strlen(resolv_content); if (avail == 0) return 0;
    size_t n = avail < count ? avail : count; memcpy(buf, resolv_content, n); resolv_content = ""; return (long)n;
}
long sys_write(int fd, const void *buf, size_t count) {
    (void)fd; memcpy(written_query, buf, count); written_len = count;
    build_response_from_query(written_query);
    if (g_dbg) fprintf(stderr, "[dbg] write fd=%d count=%zu\n", fd, count);
    return (long)count;
}
int sys_socket(int domain, int type, int protocol) { if (g_dbg) fprintf(stderr, "[dbg] socket d=%d t=%d p=%d\n", domain, type, protocol); return 3; }
int sys_connect(int fd, const void *addr, unsigned int addrlen) { if (g_dbg) fprintf(stderr, "[dbg] connect fd=%d len=%u\n", fd, addrlen); (void)addr; return 0; }
int sys_pselect6(int nfds, void *r, void *w, void *e, void *ts, void *sig) { if (g_dbg) fprintf(stderr, "[dbg] pselect6 nfds=%d\n", nfds); (void)r;(void)w;(void)e;(void)ts;(void)sig; return 1; }

/* 用真实 query 现造响应：question 原样回显，answers = CNAME(压缩指针) + A(1.2.3.4) */
static void build_response_from_query(const uint8_t *q) {
    uint16_t txid = ((uint16_t)q[0] << 8) | q[1];
    size_t off = 12;
    while (q[off] != 0) off += 1 + q[off];
    size_t qend = off + 5;   /* 尾零 + QTYPE(2) + QCLASS(2) */

    uint8_t *p = canned_resp; size_t n = 0;
    p[n++] = (uint8_t)(txid >> 8); p[n++] = (uint8_t)txid;
    p[n++] = 0x81; p[n++] = 0x80;
    p[n++] = 0x00; p[n++] = 0x01;
    p[n++] = 0x00; p[n++] = 0x02;
    p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 0;
    memcpy(p + n, q + 12, qend - 12); n += qend - 12;

    p[n++] = 0xC0; p[n++] = 0x0C;                     /* name → 指针 */
    p[n++] = 0x00; p[n++] = 0x05; p[n++] = 0x00; p[n++] = 0x01;   /* CNAME IN */
    p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 0x3C;
    p[n++] = 0x00; p[n++] = 0x06;   /* rdata = 03 'cdn' c0 0c = 6 字节 */
    p[n++] = 3; memcpy(p + n, "cdn", 3); n += 3; p[n++] = 0xC0; p[n++] = 0x0C;

    p[n++] = 0xC0; p[n++] = 0x0C;
    p[n++] = 0x00; p[n++] = 0x01; p[n++] = 0x00; p[n++] = 0x01;   /* A IN */
    p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 0x3C;
    p[n++] = 0x00; p[n++] = 0x04;
    p[n++] = 1; p[n++] = 2; p[n++] = 3; p[n++] = 4;
    canned_len = n;
}

#define ARCH_X86_64   /* dns.c 同目录下的 stub syscalls_x64.h 生效 */
#include "dns.h"

int main(void) {
    int fails = 0;
    uint32_t ip = 0, ip2 = 0; uint16_t port = 0;

    /* 1. IPv4 直通 */
    if (ax_parse_endpoint("192.0.2.10:4444", &ip, &port) == 0 &&
        ip == 0x0a0200c0U && port == 0x5c11) {
        printf("  [PASS] IPv4 直通（ip=0x%08x port=0x%04x 网络序）\n", ip, port);
    } else { printf("  [FAIL] IPv4 直通 ip=%08x port=%04x\n", ip, port); fails++; }

    /* 2. 域名 → A 记录（响应含 CNAME，须跳过） */
    written_len = 0;
    if (ax_dns_resolve("example.com", &ip2) == 0 && ip2 == 0x04030201U) {
        printf("  [PASS] 域名 → A 1.2.3.4（跳过 CNAME，ip=0x%08x）\n", ip2);
    } else { printf("  [FAIL] 域名解析 ip=%08x\n", ip2); fails++; }

    { int _i; fprintf(stderr, "[q] "); for (_i = 0; _i < (int)written_len; _i++) fprintf(stderr, "%02x ", written_query[_i]); fprintf(stderr, "NEWLINE_ESC"); }
    /* 3. query 报文格式 */
    if (written_len == 29 &&
        written_query[12] == 7 && memcmp(written_query + 13, "example", 7) == 0 &&
        written_query[20] == 3 && memcmp(written_query + 21, "com", 3) == 0 &&
        written_query[24] == 0x00 && written_query[25] == 0x00 &&
        written_query[26] == 0x01 && written_query[27] == 0x00 && written_query[28] == 0x01 &&
        written_query[2] == 0x01 && written_query[3] == 0x00) {
        printf("  [PASS] DNS query 报文（QNAME 编码 + A/IN + RD）\n");
    } else { printf("  [FAIL] query 报文 len=%zu\n", written_len); fails++; }

    /* 4. 缓存：同域名再次解析不再发查询 */
    written_len = 0; canned_len = 0;
    if (ax_dns_resolve("example.com", &ip2) == 0 && ip2 == 0x04030201U && written_len == 0) {
        printf("  [PASS] 正缓存命中（零查询）\n");
    } else { printf("  [FAIL] 缓存 ip2=%08x written=%zu\n", ip2, written_len); fails++; }

    /* 5. 端点全链路（域名:端口，走缓存） */
    ip2 = 0; port = 0;
    if (ax_parse_endpoint("example.com:80", &ip2, &port) == 0 && ip2 == 0x04030201U && port == 0x5000) {
        printf("  [PASS] 域名端点解析（端口 80 → 0x5000 网络序）\n");
    } else { printf("  [FAIL] 端点解析 ip=%08x port=%04x\n", ip2, port); fails++; }

    /* 6. 非法输入 */
    if (ax_parse_endpoint("no-port", &ip2, &port) == -1 &&
        ax_parse_endpoint(":80", &ip2, &port) == -1 &&
        ax_parse_endpoint("host:0", &ip2, &port) == -1 &&
        ax_parse_endpoint("host:99999", &ip2, &port) == -1) {
        printf("  [PASS] 非法输入拒绝\n");
    } else { printf("  [FAIL] 非法输入未被拒绝\n"); fails++; }

    printf(fails ? "== FAIL %d ==\n" : "== ALL PASS ==\n", fails);
    return fails;
}
