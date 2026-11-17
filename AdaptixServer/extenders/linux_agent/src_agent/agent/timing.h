#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

/// Monotonic milliseconds since boot (CLOCK_MONOTONIC, direct syscall)
uint64_t ax_monotonic_ms(void);

/// Uniform random uint32 in [0, max) — getrandom-backed, no modulo bias
uint32_t ax_rand_below(uint32_t max);

/// Right-skewed jitter around base_ms: typical range [~0.72x, ~1.28x],
/// with a low-probability tail up to ~2.4x. Never below base_ms/2.
/// Pure integer fixed-point math (no float, no libc).
uint32_t ax_jitter_ms(uint32_t base_ms);

/// Sleep base_ms with jitter plus a slow random-walk drift (±25% band,
/// persistent process-wide state). Destroys long-window periodicity.
void ax_drifted_sleep_ms(uint32_t base_ms);

/// Randomized connection-rotation interval derived from the reconnect
/// timeout: roughly 12x..27x conn_timeout_ms, clamped to [15min, 45min].
uint64_t ax_rotation_interval_ms(uint32_t conn_timeout_ms);

/// Idle-beacon scheduler: returns 1 when an idle keepalive beacon is due.
/// Interval ~20-45 s, jittered + drifted (stateful). The caller passes a
/// persistent last-beacon timestamp (ms, 0 = never).
int ax_beacon_due(uint64_t now_ms, uint64_t *last_beacon_ms);

/* ★ 运行时心跳基准（"sleep" 命令）：默认 30000ms。
 * 0 = 交互加速（每轮轮询都 due，节拍受 idle pacing 限制 ~150ms）；
 * 上限 1 小时。set 同时清漂移历史，让新节奏立即生效。 */
void ax_beacon_base_set(uint32_t base_ms);
uint32_t ax_beacon_base_get(void);

/// Append "<monotonic_ms> <event>\n" to AX_TEST_LOG (env) if set; no-op
/// otherwise. Events: "connect", "rotate", "reconnect" (Phase 4 gap 5 feed).
void ax_test_log(const char *event);

#endif // TIMING_H
