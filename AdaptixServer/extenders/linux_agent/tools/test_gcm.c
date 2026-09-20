/* test_gcm.c — cross-language AES-128-GCM checker for the 16B session-key path.
 * Standalone: stubs the ax_* zero-libc glue with deterministic versions and
 * includes the real crypt.c from src_agent/agent so the tested code is the
 * exact code the agent runs.
 *
 * Usage:
 *   ./test_gcm enc <key16hex> <plainhex>       -> prints nonce|ct|tag hex
 *   ./test_gcm dec <key16hex> <wirehex>        -> prints plaintext hex or FAIL
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- deterministic stubs (nonce = fixed counter pattern, not random) ---- */
static unsigned char g_stub_nonce[12] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b
};

int ax_random_bytes(void *buf, size_t len) {
    uint8_t *b = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) b[i] = g_stub_nonce[i % 12];
    return 0;
}

#include "../src_agent/agent/crypt.c"

/* ---- libc-backed stubs for the remaining zero-libc glue ---- */
void *ax_malloc(size_t size) { return malloc(size); }
void  ax_free(void *ptr)     { free(ptr); }
void *ax_memset(void *s, int c, size_t n) { return memset(s, c, n); }
void *ax_memcpy(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }
int   ax_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static size_t unhex(const char *s, uint8_t *out, size_t cap) {
    size_t n = 0;
    while (s[0] && s[1] && n < cap) {
        int hi = hexval(s[0]), lo = hexval(s[1]);
        if (hi < 0 || lo < 0) return 0;
        out[n++] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return n;
}
static void prhex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: test_gcm enc|dec <key16hex> <datahex>\n");
        return 1;
    }
    uint8_t key[16];
    if (unhex(argv[2], key, 16) != 16) { fprintf(stderr, "bad key\n"); return 2; }
    uint8_t buf[65536];
    size_t dlen = unhex(argv[3], buf, sizeof(buf));
    if (dlen == 0) { fprintf(stderr, "bad data\n"); return 3; }

    if (strcmp(argv[1], "enc") == 0) {
        size_t out_len = 0;
        uint8_t *out = aes128_gcm_encrypt(buf, dlen, key, &out_len);
        if (!out) { fprintf(stderr, "encrypt failed\n"); return 4; }
        prhex(out, out_len);
        free(out);
    } else if (strcmp(argv[1], "dec") == 0) {
        size_t pt_len = 0;
        uint8_t *pt = aes128_gcm_decrypt(buf, dlen, key, &pt_len);
        if (!pt) { printf("FAIL\n"); return 5; }
        prhex(pt, pt_len);
        free(pt);
    } else {
        fprintf(stderr, "bad mode\n");
        return 6;
    }
    return 0;
}
