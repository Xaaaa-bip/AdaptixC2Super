#ifndef NOISE_H
#define NOISE_H

/// Behavior-noise generator: occasionally (randomized cadence, ~3-20 s)
/// performs benign-looking direct-syscall activity — /proc reads,
/// clock/getpid/getuid queries, small mmap/munmap cycles — so the
/// process's syscall profile stops being "network-only" and lands inside
/// the distribution of ordinary programs. Self-gating: call it every
/// poll-loop iteration; it decides internally whether to act.
void ax_behavior_noise(void);

#endif // NOISE_H
