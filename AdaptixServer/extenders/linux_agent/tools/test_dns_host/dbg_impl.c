#include <stdio.h>
#include <stdint.h>
void dbg_resp(const uint8_t *r, long n) {
    fprintf(stderr, "[resp] n=%ld rid=%02x%02x flags=%02x%02x qd=%02x%02x an=%02x%02x | ",
        n, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    for (long i = 12; i < n && i < 52; i++) fprintf(stderr, "%02x ", r[i]);
    fprintf(stderr, "\n");
}
