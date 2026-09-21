// mbedtls_glue.c — zero-libc wiring for the embedded mbedtls (H1 WSS)
#include "crt.h"
#if defined(ARCH_X86_64)
#include "syscalls_x64.h"
#elif defined(ARCH_AARCH64)
#include "syscalls_aarch64.h"
#endif

// ── allocation: mbedtls → agent heap ──
void *ax_calloc(size_t n, size_t size) {
    size_t total = n * size;
    if (n && total / n != size) return 0;
    void *p = ax_malloc(total);
    if (p) ax_memset(p, 0, total);
    return p;
}

// ── entropy: hardware poll via getrandom(2) ──
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    long ret = sys_getrandom(output, len, 0);
    if (ret < 0) return -1;
    *olen = (size_t)ret;
    return 0;
}

// ── fatal exit (no libc exit) ──
void ax_mbedtls_exit(int status) {
    (void)status;
    for (;;) __builtin_trap();
}

// ── entropy fallback (entropy.c references it on the non-HW path) ──
int mbedtls_platform_entropy_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    return mbedtls_hardware_poll(data, output, len, olen);
}

// ── inet_pton (x509 SAN IP parsing; minimal AF_INET/AF_INET6) ──
int inet_pton(int af, const char *src, void *dst) {
    if (!src || !dst) return 0;
    if (af == 2) { // AF_INET
        unsigned char *out = (unsigned char *)dst;
        unsigned int oct[4]; int i = 0; const char *p = src;
        while (i < 4) {
            unsigned int v = 0; int digits = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned int)(*p - '0'); p++; digits++; }
            if (digits == 0 || v > 255) return 0;
            oct[i++] = v;
            if (i == 4) break;
            if (*p != '.') return 0;
            p++;
        }
        if (*p) return 0;
        for (int k = 0; k < 4; k++) out[k] = (unsigned char)oct[k];
        return 1;
    }
    if (af == 10) { // AF_INET6 — support ::, ::ffff:a.b.c.d, hex groups
        unsigned char *out = (unsigned char *)dst;
        ax_memset(out, 0, 16);
        const char *p = src;
        int group = 0, groups_filled = 0;
        int dc_start = -1; // index of :: compression
        // handle IPv4-mapped tail
        const char *dot = 0;
        for (const char *q = src; *q; q++) if (*q == '.') { dot = q; break; }
        if (dot) {
            char v4[16]; const char *tail = dot;
            while (tail > src && *(tail - 1) != ':') tail--;
            size_t vl = 0;
            for (const char *q = tail; *q; q++) v4[vl++] = *q;
            v4[vl] = 0;
            if (!inet_pton(2, v4, out + 12)) return 0;
            groups_filled = 2;
            p = tail - 1; // step back over the ':'
        }
        while (*p) {
            if (*p == ':') {
                if (p[1] == ':') { // compression
                    if (dc_start >= 0) return 0;
                    dc_start = group;
                    p += 2;
                    continue;
                }
                if (group >= 8 || (group == 0 && dc_start < 0 && groups_filled == 0)) { }
                p++;
                continue;
            }
            unsigned int v = 0; int digits = 0;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                char c = *p;
                v = v * 16 + (unsigned int)((c >= '0' && c <= '9') ? c - '0' : ((c | 0x20) - 'a' + 10));
                p++; digits++;
            }
            if (digits == 0 || v > 0xFFFF || group >= 8) return 0;
            out[group * 2] = (unsigned char)(v >> 8);
            out[group * 2 + 1] = (unsigned char)v;
            group++;
            if (group >= 8 && *p) return 0;
        }
        if (dc_start >= 0 && group < 8 && groups_filled == 0) {
            int shift = group - dc_start;
            for (int g = group - 1; g >= dc_start; g--) {
                out[(g + (8 - group)) * 2] = out[g * 2];
                out[(g + (8 - group)) * 2 + 1] = out[g * 2 + 1];
            }
            for (int g = dc_start; g < dc_start + (8 - group); g++) {
                out[g * 2] = 0; out[g * 2 + 1] = 0;
            }
        }
        return 1;
    }
    return 0;
}

// ── std string symbols mbedtls calls directly (not routed via MBEDTLS_PLATFORM_C) ──
int memcmp(const void *a, const void *b, size_t n)  { return ax_memcmp(a, b, n); }
size_t strlen(const char *s)                       { return ax_strlen(s); }
char *strcpy(char *d, const char *s) {
    char *p = d; while ((*p++ = *s++)); return d;
}
char *strncpy(char *d, const char *s, size_t n)    { return ax_strncpy(d, s, n); }
char *strcat(char *d, const char *s) {
    char *p = d + ax_strlen(d); while ((*p++ = *s++)); return d;
}
int strcmp(const char *a, const char *b)           { return ax_strcmp(a, b); }
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
char *strchr(const char *s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return 0;
}
char *strstr(const char *hay, const char *needle) {
    size_t n = ax_strlen(needle);
    if (!n) return (char *)hay;
    for (; *hay; hay++) if (!strncmp(hay, needle, n)) return (char *)hay;
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return 0;
}
