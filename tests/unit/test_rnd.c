/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * test_rnd.c - Unit tests for RND random number generator
 *
 * These tests verify that our RND implementation matches the exact
 * algorithm from Altair 8K BASIC 4.0, which uses a table-based approach
 * with multipliers, addends, and special normalization.
 */

#include "test_harness.h"
#include "basic/mbf.h"
#include "basic/basic.h"
#include <math.h>

/* Forward declarations for internal functions */
void rnd_init(rnd_state_t *state);
void rnd_reseed(rnd_state_t *state);
mbf_t rnd_next(rnd_state_t *state, mbf_t arg);

/* Test RND state initialization */
TEST(test_rnd_init) {
    rnd_state_t state;
    rnd_init(&state);

    /* Initial counters should be 0 */
    ASSERT_EQ_INT(state.counter1, 0);
    ASSERT_EQ_INT(state.counter2, 0);
    ASSERT_EQ_INT(state.counter3, 0);

    /* Initial seed is from addend table entry 0 */
    /* D1866: 0x52, 0xC7, 0x4F, 0x80 */
    ASSERT_EQ_HEX(state.last_value.byte_array[0], 0x52);
    ASSERT_EQ_HEX(state.last_value.byte_array[1], 0xC7);
    ASSERT_EQ_HEX(state.last_value.byte_array[2], 0x4F);
    ASSERT_EQ_HEX(state.last_value.byte_array[3], 0x80);
}

/* Test RND(0) returns last value */
TEST(test_rnd_zero_returns_last) {
    rnd_state_t state;
    rnd_init(&state);

    /* Generate a value */
    mbf_t first = rnd_next(&state, mbf_from_int16(1));

    /* RND(0) should return the same value */
    mbf_t repeat = rnd_next(&state, MBF_ZERO);
    ASSERT_MBF_EQ(first, repeat);

    /* And again */
    mbf_t repeat2 = rnd_next(&state, MBF_ZERO);
    ASSERT_MBF_EQ(first, repeat2);
}

/* Test that RND(positive) generates different values */
TEST(test_rnd_generates_sequence) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);
    mbf_t v1 = rnd_next(&state, one);
    mbf_t v2 = rnd_next(&state, one);
    mbf_t v3 = rnd_next(&state, one);

    /* Each value should be different */
    ASSERT(v1.raw != v2.raw);
    ASSERT(v2.raw != v3.raw);
    ASSERT(v1.raw != v3.raw);
}

/* Test that RND values are in range (0, 1) */
TEST(test_rnd_in_range) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);
    mbf_t zero = MBF_ZERO;
    mbf_t mbf_one = mbf_from_int16(1);

    for (int i = 0; i < 100; i++) {
        mbf_t value = rnd_next(&state, one);

        /* Value should be > 0 */
        ASSERT(mbf_cmp(value, zero) > 0);

        /* Value should be < 1 */
        ASSERT(mbf_cmp(value, mbf_one) < 0);
    }
}

/* Test RND(negative) reseeds deterministically */
TEST(test_rnd_reseed) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);
    mbf_t neg5 = mbf_from_int16(-5);

    /* Generate some values */
    (void)rnd_next(&state, one);
    (void)rnd_next(&state, one);
    (void)rnd_next(&state, one);

    /* Reseed with -5 */
    mbf_t after_seed = rnd_next(&state, neg5);

    /* Reset and reseed with same value - should get same result */
    rnd_state_t state2;
    rnd_init(&state2);
    (void)rnd_next(&state2, one);
    (void)rnd_next(&state2, one);
    mbf_t after_seed2 = rnd_next(&state2, neg5);

    ASSERT_MBF_EQ(after_seed, after_seed2);
}

/* Test that the sequence is deterministic */
TEST(test_rnd_deterministic) {
    rnd_state_t state1, state2;
    rnd_init(&state1);
    rnd_init(&state2);

    mbf_t one = mbf_from_int16(1);

    /* Generate 10 values from each state */
    for (int i = 0; i < 10; i++) {
        mbf_t v1 = rnd_next(&state1, one);
        mbf_t v2 = rnd_next(&state2, one);

        /* Should be identical */
        ASSERT_MBF_EQ(v1, v2);
    }
}

/* Test counter progression */
TEST(test_rnd_counters) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);

    /* Generate first value */
    (void)rnd_next(&state, one);

    /* Counter1 should be 1 */
    ASSERT_EQ_INT(state.counter1, 1);

    /* Counter2 cycles 1,2,3,1,2,3... */
    ASSERT_EQ_INT(state.counter2, 1);

    /* Counter3 = (1 + old_counter3) & 7 = 1 */
    ASSERT_EQ_INT(state.counter3, 1);
}

/* Run all tests */
void run_tests(void) {
    RUN_TEST(test_rnd_init);
    RUN_TEST(test_rnd_zero_returns_last);
    RUN_TEST(test_rnd_generates_sequence);
    RUN_TEST(test_rnd_in_range);
    RUN_TEST(test_rnd_reseed);
    RUN_TEST(test_rnd_deterministic);
    RUN_TEST(test_rnd_counters);
}

TEST_MAIN()
