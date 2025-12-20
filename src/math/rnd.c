/*
 * rnd.c - Random Number Generator
 *
 * This implements the EXACT RND algorithm from 8K BASIC 4.0
 * (8kbas_src.mac lines 4401-4499).
 *
 * The algorithm is a two-stage table-based generator:
 * 1. Multiply seed by one of 8 constants (indexed by counter3 & 7)
 * 2. Add one of 4 constants (indexed by counter2 & 3)
 * 3. Scramble bytes: swap lo and hi, XOR with 0x4F
 * 4. Set exponent to 0x80 (forces value between 0.5 and 1.0)
 * 5. Normalize the result
 *
 * Counter1 wraps at 0xAB (171) and triggers extra scrambling.
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
