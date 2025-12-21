/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file rnd.c
 * @brief Random Number Generator
 *
 * This implements the EXACT RND algorithm from Altair 8K BASIC 4.0.
 * The algorithm must be exactly reproduced because many classic BASIC
 * programs rely on specific RND sequences for gameplay (e.g., the ship
 * placement in Super Star Trek).
 *
 * ## Algorithm Overview
 *
 * This is a two-stage table-based linear congruential generator:
 *
 * ```
 *   Stage 1: result = seed * MULTIPLIER_TABLE[counter3 & 7]
 *   Stage 2: result = result + ADDEND_TABLE[counter2 & 3]
 *   Scramble: swap bytes, XOR with 0x4F, set exponent to 0x80
 *   Normalize the result
 * ```
 *
 * ## RND Function Behavior
 *
 * - RND(X) where X > 0: Returns next random number (0 < result < 1)
 * - RND(0): Returns the last random number generated
 * - RND(X) where X < 0: Reseeds the generator and returns first number
 *
 * Note: Negative argument resets counters but does NOT use the argument
 * value as the seed. This matches the original behavior.
 *
 * ## Counter System
 *
 * Three counters control the sequence:
 * - counter1: Wraps at 0xAB (171), triggers extra byte scrambling
 * - counter2: Increments each call, mod 4 selects addend
 * - counter3: Set from (seed_lo + counter3) & 7, selects multiplier
 *
 * ## Original Source Reference
 *
 * From 8kbas_src.mac lines 4401-4499:
 * - Lines 4401-4420: RND entry point and argument handling
 * - Lines 4421-4450: Two-stage multiplication and addition
 * - Lines 4451-4470: Byte scrambling
 * - Lines 4471-4499: Normalization and return
 *
 * ## Lookup Tables
 *
 * The lookup tables are from addresses D1846-D1875 in the original ROM:
 * - 8 multiplier constants (D1846-D1865)
 * - 4 addend constants (D1866-D1875)
 */

#include "basic/mbf.h"
#include "basic/basic.h"
#include <string.h>

/*
 * Multiplier table (8 entries from D1846-D1865)
 * These are MBF format constants stored in little-endian order.
 */
static const mbf_t RND_MULTIPLIERS[8] = {
    {.byte_array = {0x00, 0x35, 0x4A, 0xCA}},  /* Entry 0 */
    {.byte_array = {0x99, 0x39, 0x1C, 0x76}},  /* Entry 1 */
    {.byte_array = {0x98, 0x22, 0x95, 0xB3}},  /* Entry 2 */
    {.byte_array = {0x98, 0x0A, 0xDD, 0x47}},  /* Entry 3 */
    {.byte_array = {0x98, 0x53, 0xD1, 0x99}},  /* Entry 4 */
    {.byte_array = {0x99, 0x0A, 0x1A, 0x9F}},  /* Entry 5 */
    {.byte_array = {0x98, 0x65, 0xBC, 0xCD}},  /* Entry 6 */
    {.byte_array = {0x98, 0xD6, 0x77, 0x3E}},  /* Entry 7 */
};

/*
 * Addend table (4 entries from D1866-D1875)
 * First entry is also used as initial seed.
 */
static const mbf_t RND_ADDENDS[4] = {
    {.byte_array = {0x52, 0xC7, 0x4F, 0x80}},  /* Entry 0 - also initial seed */
    {.byte_array = {0x68, 0xB1, 0x46, 0x68}},  /* Entry 1 */
    {.byte_array = {0x99, 0xE9, 0x92, 0x69}},  /* Entry 2 */
    {.byte_array = {0x10, 0xD1, 0x75, 0x68}},  /* Entry 3 */
};

/*
 * Initialize RND state with default seed.
 */
void rnd_init(rnd_state_t *state) {
    state->counter1 = 0;
    state->counter2 = 0;
    state->counter3 = 0;
    state->last_value = RND_ADDENDS[0];  /* Initial seed */
}

/*
 * Reseed the RNG (called when RND receives negative argument).
 * Note: The original ignores the seed value - it just resets counters!
 */
void rnd_reseed(rnd_state_t *state) {
    state->counter1 = 0;
    state->counter2 = 0;
    state->counter3 = 0;
    /* Seed is NOT changed - this matches original behavior */
}

/*
 * Generate next random number.
 *
 * RND(X) behavior:
 * - X < 0: Reseed (reset counters, restart sequence)
 * - X = 0: Return last value
 * - X > 0: Generate next random number (0 < result < 1)
 */
mbf_t rnd_next(rnd_state_t *state, mbf_t arg) {
    int arg_sign = mbf_sign(arg);

    /* RND(negative) - reseed and return first value */
    if (arg_sign < 0) {
        rnd_reseed(state);
        /* Fall through to generate */
    }
    /* RND(0) - return last value */
    else if (arg_sign == 0) {
        return state->last_value;
    }
    /* RND(positive) - generate next */

    /*
     * Stage 1: Multiply by table entry
     * The index is based on combining the seed with counter3
     */
    mbf_t seed = state->last_value;
    uint8_t mult_index = (seed.bytes.mantissa_lo + state->counter3) & 0x07;
    state->counter3 = mult_index;

    mbf_t multiplier = RND_MULTIPLIERS[mult_index];
    mbf_t result = mbf_mul(seed, multiplier);

    /*
     * Stage 2: Add table entry
     * Index is counter2 mod 4
     */
    state->counter2++;
    uint8_t add_index = state->counter2 & 0x03;

    /* Skip index 0 since that's the seed storage location */
    /* Actually the original adds the entry AT that index which includes seed */
    /* But for counter2 values 1-3, it uses the constants */
    mbf_t addend;
    if (add_index == 0) {
        addend = state->last_value;  /* Use current seed */
    } else {
        addend = RND_ADDENDS[add_index];
    }

    result = mbf_add(result, addend);

    /*
     * Byte scrambling (from lines 4432-4449):
     * - Swap mantissa_lo and mantissa_hi (before sign handling)
     * - XOR with 0x4F
     * - Set exponent to 0x80
     */
    uint8_t temp_lo = result.bytes.mantissa_lo;
    uint8_t temp_hi = result.bytes.mantissa_hi & 0x7F;  /* Clear sign */

    result.bytes.mantissa_lo = temp_hi ^ 0x4F;
    result.bytes.mantissa_hi = temp_lo & 0x7F;  /* Clear sign bit - result is always positive */
    result.bytes.exponent = 0x80;

    /*
     * Counter1 handling (lines 4441-4449):
     * Increment counter1, and when it reaches 0xAB (171), wrap to 0
     * and do additional scrambling.
     */
    state->counter1++;
    if (state->counter1 >= 0xAB) {
        state->counter1 = 0;
        /* Additional scrambling when counter wraps */
        result.bytes.mantissa_hi++;
        result.bytes.mantissa_mid--;
        result.bytes.mantissa_lo++;
    }

    /* Normalize the result */
    result = mbf_normalize(result);

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
