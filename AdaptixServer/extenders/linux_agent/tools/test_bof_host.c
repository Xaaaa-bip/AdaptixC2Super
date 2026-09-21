/* test_bof_host.c — host harness for BOF unit testing.
 * Implements the BOF API imports with libc equivalents, compiles the BOF
 * source INLINE with ASan, and calls go(NULL, 0) directly. Any wild pointer
 * in the BOF lights up ASan with the exact source line.
 *
 * Build (from linux_agent dir):
 *   gcc -g -O0 -fsanitize=address -include bof_api.h \
 *       -DDJB2_SEED=1U -I src_agent/agent \
 *       tools/test_bof_host.c src_agent/bofs/kernel_exploit_check.c -o /tmp/test_bof_host
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- BOF API stubs (libc-backed) ---- */
void BeaconOutput(int type, const char *data, int len) {
    (void)type;
    fwrite(data, 1, (size_t)len, stdout);
}
void BeaconPrintf(int type, const char *fmt, ...) {
    (void)type;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
int AxReadFileToBuffer(const char *path, char **out_buf, int max_size) {
    if (!path || !out_buf) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (max_size <= 0) max_size = 1048576;
    char *buf = (char *)malloc((size_t)max_size + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)max_size, f);
    fclose(f);
    buf[n] = '\0';
    *out_buf = buf;
    return (int)n;
}
void AxFree(void *ptr) { free(ptr); }
char *AxStrstr(const char *h, const char *n) { return strstr(h, n); }
int AxSnprintf(char *buf, int size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)size, fmt, ap);
    va_end(ap);
    return r;
}
int AxStrlen(const char *s) { return s ? (int)strlen(s) : 0; }
int AxStrcmp(const char *a, const char *b) { return strcmp(a, b); }

int main(void) {
    extern void go(char *, int);
    go((char *)0, 0);
    return 0;
}
