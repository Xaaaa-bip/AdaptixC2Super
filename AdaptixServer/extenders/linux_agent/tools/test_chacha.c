/* test_chacha.c — standalone cross-language ChaCha20 checker (C side).
 * Usage: ./test_chacha <key16hex> <nonce12hex> <plainhex>
 * Prints ciphertext hex.
 *
 * IMPORTANT: the chacha20 code below MUST stay byte-identical to the
 * implementation in src_agent/agent/crypt.c (chacha_rotl/chacha_qr/
 * chacha20_block/chacha20_xor). Compile: gcc -O2 -o test_chacha test_chacha.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t chacha_rotl(uint32_t v, int c) {
    return (v << c) | (v >> (32 - c));
}

static void chacha_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = chacha_rotl(*d, 16);
    *c += *d; *b ^= *c; *b = chacha_rotl(*b, 12);
    *a += *b; *d ^= *a; *d = chacha_rotl(*d, 8);
    *c += *d; *b ^= *c; *b = chacha_rotl(*b, 7);
}

static uint32_t load32_le(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void chacha20_block(const uint8_t key[16], const uint8_t nonce[12],
                           uint32_t counter, uint32_t out[16]) {
    uint32_t init[16], x[16];
    init[0] = 0x61707865; init[1] = 0x3320646e;   /* "expand 16-byte k" */
    init[2] = 0x79622d32; init[3] = 0x6b206574;
    for (int i = 0; i < 4; i++) {
        init[4 + i] = load32_le(key + 4 * i);
        init[8 + i] = init[4 + i];
    }
    init[12] = counter;
    for (int i = 0; i < 3; i++)
        init[13 + i] = load32_le(nonce + 4 * i);

    for (int i = 0; i < 16; i++) x[i] = init[i];
    for (int r = 0; r < 10; r++) {
        chacha_qr(&x[0], &x[4], &x[8], &x[12]);
        chacha_qr(&x[1], &x[5], &x[9], &x[13]);
        chacha_qr(&x[2], &x[6], &x[10], &x[14]);
        chacha_qr(&x[3], &x[7], &x[11], &x[15]);
        chacha_qr(&x[0], &x[5], &x[10], &x[15]);
        chacha_qr(&x[1], &x[6], &x[11], &x[12]);
        chacha_qr(&x[2], &x[7], &x[8], &x[13]);
        chacha_qr(&x[3], &x[4], &x[9], &x[14]);
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + init[i];
}

static void chacha20_xor(const uint8_t key[16], const uint8_t nonce[12],
                         const uint8_t *in, uint8_t *out, size_t len) {
    uint32_t block[16];
    uint32_t counter = 0;
    size_t off = 0;
    while (off < len) {
        chacha20_block(key, nonce, counter, block);
        const uint8_t *kb = (const uint8_t *)block;
        size_t n = len - off;
        if (n > 64) n = 64;
        for (size_t i = 0; i < n; i++)
            out[off + i] = in[off + i] ^ kb[i];
        off += n;
        counter++;
    }
}

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

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: test_chacha <key16hex> <nonce12hex> <plainhex>\n");
        return 1;
    }
    uint8_t key[16], nonce[12];
    uint8_t plain[4096];
    size_t klen = unhex(argv[1], key, 16);
    size_t nlen = unhex(argv[2], nonce, 12);
    size_t plen = unhex(argv[3], plain, sizeof(plain));
    if (klen != 16 || nlen != 12 || plen == 0) {
        fprintf(stderr, "bad input lengths\n");
        return 2;
    }
    uint8_t out[4096];
    chacha20_xor(key, nonce, plain, out, plen);
    for (size_t i = 0; i < plen; i++) printf("%02x", out[i]);
    return 0;
}
