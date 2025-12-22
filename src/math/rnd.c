/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file rnd.c
 * @brief Random Number Generator - Exact 8080 Algorithm
 *
 * This implements the EXACT RND algorithm from Altair 8K BASIC 4.0,
 * traced directly from 8kbas_src.mac lines 4401-4499.
 *
 * The algorithm is a two-stage table-based generator:
 * 1. Multiply current seed by MULTIPLIER_TABLE[index]
 * 2. Add ADDEND_TABLE[index]
 * 3. Apply byte scrambling (swap bytes, XOR with 0x4F)
 * 4. Normalize with special exponent handling
 *
 * Three counters control table indexing and provide additional scrambling
 * when counter1 wraps at 0xAB (171).
 */

#include "basic/mbf.h"
#include "basic/basic.h"
#include <string.h>

/*
 * Multiplier table (8 entries from D1846-D1865)
 * Each entry is 4 bytes in MBF format (little-endian).
 */
static const mbf_t RND_MULTIPLIERS[8] = {
    {.byte_array = {0x35, 0x4A, 0xCA, 0x99}},  /* Entry 0: D1846 */
    {.byte_array = {0x39, 0x1C, 0x76, 0x98}},  /* Entry 1: D184A */
    {.byte_array = {0x22, 0x95, 0xB3, 0x98}},  /* Entry 2: D184E */
    {.byte_array = {0x0A, 0xDD, 0x47, 0x98}},  /* Entry 3: D1852 */
    {.byte_array = {0x53, 0xD1, 0x99, 0x99}},  /* Entry 4: D1856 */
    {.byte_array = {0x0A, 0x1A, 0x9F, 0x98}},  /* Entry 5: D185A */
    {.byte_array = {0x65, 0xBC, 0xCD, 0x98}},  /* Entry 6: D185E */
    {.byte_array = {0xD6, 0x77, 0x3E, 0x98}},  /* Entry 7: D1862 */
};

/*
 * Addend table (4 entries from D1866-D1875)
 * Entry 0 is also the initial seed value.
 */
static const mbf_t RND_ADDENDS[4] = {
    {.byte_array = {0x52, 0xC7, 0x4F, 0x80}},  /* Entry 0: D1866 - initial seed */
    {.byte_array = {0x68, 0xB1, 0x46, 0x68}},  /* Entry 1: D186A */
    {.byte_array = {0x99, 0xE9, 0x92, 0x69}},  /* Entry 2: D186E */
    {.byte_array = {0x10, 0xD1, 0x75, 0x68}},  /* Entry 3: D1872 */
};

/*
 * RND-specific normalization.
 *
 * The 8080 normalize function mixes the OLD exponent into the mantissa
 * during the shift loop. This is a critical part of the RND scrambling.
 *
 * From 8kbas_src.mac lines 3339-3365 and 4439-4450:
 * - Before normalization, B holds the old exponent
 * - L is set to B (old exponent) in normalize
 * - The dad h instruction rotates HL together, mixing exponent bits in
 */
static mbf_t rnd_normalize(mbf_t a, uint8_t old_exp) {
    if (a.bytes.exponent == 0) {
        return a;
    }

    /* If mantissa is all zero, result is zero */
    uint8_t c = a.bytes.mantissa_hi;
    uint8_t d = a.bytes.mantissa_mid;
    uint8_t h = a.bytes.mantissa_lo;

    if (c == 0 && d == 0 && h == 0) {
        return MBF_ZERO;
    }

    /*
     * Emulate the 8080 shift loop:
     * L = old_exp (this is key - old exponent bits get mixed in!)
     * H = mantissa_lo
     *
     * The loop does:
     *   dad h    - HL = HL + HL (rotate left with carry out)
     *   ral D    - rotate D left through carry
     *   adc C    - rotate C left through carry (C = C + C + carry)
     */
    uint8_t l = old_exp;
    uint8_t new_exp = a.bytes.exponent;

    while ((c & 0x80) == 0) {
        /* dad h: HL = HL + HL */
        uint32_t hl = ((uint32_t)h << 8) | l;
        hl = hl + hl;
        uint8_t carry = (hl >> 16) & 1;
        h = (hl >> 8) & 0xFF;
        l = hl & 0xFF;

        /* ral: rotate D left through carry */
        uint8_t new_carry = (d >> 7) & 1;
        d = (uint8_t)((d << 1) | carry);
        carry = new_carry;

        /* adc a: C = C + C + carry */
        uint16_t c2 = (uint16_t)c + (uint16_t)c + carry;
        c = (uint8_t)c2;

        new_exp--;
        if (new_exp == 0) {
            return MBF_ZERO;
        }
    }

    /*
     * Rounding: if bit 7 of L is set, round up
     * From lines 3381-3395
     */
    if (l & 0x80) {
        h++;
        if (h == 0) {
            d++;
            if (d == 0) {
                c++;
                if (c == 0) {
                    /* Overflow to next power of 2 */
                    c = 0x80;
                    new_exp++;
                }
            }
        }
    }

    a.bytes.mantissa_hi = c & 0x7F;  /* Clear sign bit (RND always positive) */
    a.bytes.mantissa_mid = d;
    a.bytes.mantissa_lo = h;
    a.bytes.exponent = new_exp;

    return a;
}

/*
 * Initialize RND state with default seed.
 */
void rnd_init(rnd_state_t *state) {
    state->counter1 = 0;
    state->counter2 = 0;
    state->counter3 = 0;
    state->last_value = RND_ADDENDS[0];  /* Initial seed from D1866 */
}

/*
 * Reset the RNG to initial state.
 */
void rnd_reseed(rnd_state_t *state) {
    state->counter1 = 0;
    state->counter2 = 0;
    state->counter3 = 0;
    state->last_value = RND_ADDENDS[0];
}

/*
 * Seed the RNG from an MBF value (for negative argument or RANDOMIZE).
 *
 * When RND receives a negative argument, it resets all counters to the
 * mantissa_hi value (from RST 5), then applies the scrambling directly
 * to the argument value.
 */
void rnd_seed_from_mbf(rnd_state_t *state, mbf_t arg) {
    /*
     * RST 5 for negative numbers returns 0xFF (the sign indicator).
     * The counters are set to this value.
     */
    uint8_t rst5_val = 0xFF;
    state->counter1 = rst5_val;
    state->counter2 = rst5_val;
    state->counter3 = rst5_val;

    /* Save old exponent before scrambling */
    uint8_t old_exp = arg.bytes.exponent;

    /*
     * Scrambling (rnd1 code path, lines 4432-4449):
     * - L14AC gets FAC into registers: E=lo, D=mid, C=hi, B=exp
     * - Swap lo and hi: E gets C, original E becomes A
     * - XOR A with 0x4F
     * - C gets the XOR result
     * - Set exponent to 0x80
     */
    uint8_t temp_lo = arg.bytes.mantissa_lo;
    uint8_t temp_hi = arg.bytes.mantissa_hi;

    arg.bytes.mantissa_lo = temp_hi;
    arg.bytes.mantissa_hi = temp_lo ^ 0x4F;
    arg.bytes.exponent = 0x80;

    /* Counter1 handling: increment and check wrap at 0xAB */
    state->counter1++;
    if (state->counter1 == 0xAB) {
        state->counter1 = 0;
        arg.bytes.mantissa_hi++;
        arg.bytes.mantissa_mid--;
        arg.bytes.mantissa_lo++;
    }

    /* Normalize with old exponent mixing */
    arg = rnd_normalize(arg, old_exp);

    state->last_value = arg;
}

/*
 * Generate next random number.
 *
 * RND(X) behavior:
 * - X < 0: Reseed using argument bits and return scrambled value
 * - X = 0: Return last value
 * - X > 0: Generate next random number
 */
mbf_t rnd_next(rnd_state_t *state, mbf_t arg) {
    int arg_sign = mbf_sign(arg);

    /* RND(0) - return last value unchanged */
    if (arg_sign == 0) {
        return state->last_value;
    }

    /* RND(negative) - reseed */
    if (arg_sign < 0) {
        rnd_seed_from_mbf(state, arg);
        return state->last_value;
    }

    /* RND(positive) - generate next value */
    mbf_t seed = state->last_value;

    /*
     * Stage 1: Multiply by table entry
     *
     * RST 5 returns 1 for positive argument.
     * Index = (1 + counter3) & 7
     */
    uint8_t mult_index = (1 + state->counter3) & 0x07;
    state->counter3 = mult_index;

    mbf_t multiplier = RND_MULTIPLIERS[mult_index];
    mbf_t result = mbf_mul(seed, multiplier);

    /*
     * Stage 2: Add table entry
     *
     * The 8080 code increments counter2, masks to 0-3, then uses
     * cpi 1 / adc b to ensure index 0 is never used:
     * - If masked value < 1, add carry to make it 1
     *
     * This cycles: 1, 2, 3, 1, 2, 3, ...
     */
    uint8_t c2 = (state->counter2 + 1) & 0x03;
    if (c2 < 1) c2 = 1;  /* cpi 1 / adc b trick */
    state->counter2 = c2;

    mbf_t addend = RND_ADDENDS[state->counter2];
    result = mbf_add(result, addend);

    /*
     * Save old exponent for mixing during normalization.
     */
    uint8_t old_exp = result.bytes.exponent;

    /*
     * Byte scrambling (lines 4432-4449):
     * - Get FAC into registers: E=lo, D=mid, C=hi
     * - A = E (mantissa_lo)
     * - E = C (mantissa_hi moves to lo position)
     * - A = A XOR 0x4F
     * - C = A (XOR result moves to hi position)
     * - Set exponent to 0x80
     */
    uint8_t temp_lo = result.bytes.mantissa_lo;
    uint8_t temp_hi = result.bytes.mantissa_hi;

    result.bytes.mantissa_lo = temp_hi;
    result.bytes.mantissa_hi = temp_lo ^ 0x4F;
    result.bytes.exponent = 0x80;

    /*
     * Counter1 handling (lines 4441-4449):
     * - Increment counter1
     * - If it reaches 0xAB (171), wrap to 0 and do extra scrambling
     */
    state->counter1++;
    if (state->counter1 == 0xAB) {
        state->counter1 = 0;
        result.bytes.mantissa_hi++;
        result.bytes.mantissa_mid--;
        result.bytes.mantissa_lo++;
    }

    /* Normalize with old exponent mixing */
    result = rnd_normalize(result, old_exp);

    /* Save for next call and RND(0) */
    state->last_value = result;

    return result;
}

/*
 * Public interface function for the BASIC interpreter.
 */
mbf_t basic_rnd(basic_state_t *state, mbf_t arg) {
    return rnd_next(&state->rnd, arg);
}
