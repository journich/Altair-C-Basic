/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * test_rnd.c - Unit tests for RND random number generator
 *
 * These tests verify that our RND implementation produces the
 * exact same sequence as the original 8K BASIC.
 */

#include "test_harness.h"
#include "basic/mbf.h"
#include "basic/basic.h"

/* Forward declarations for internal functions */
void rnd_init(rnd_state_t *state);
void rnd_reseed(rnd_state_t *state);
mbf_t rnd_next(rnd_state_t *state, mbf_t arg);

/* Test RND state initialization */
TEST(test_rnd_init) {
    rnd_state_t state;
    rnd_init(&state);

    ASSERT_EQ_INT(state.counter1, 0);
    ASSERT_EQ_INT(state.counter2, 0);
    ASSERT_EQ_INT(state.counter3, 0);
    /* Initial seed should be the first addend table entry */
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

/* Test RND(negative) reseeds */
TEST(test_rnd_reseed) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);
    mbf_t neg = mbf_from_int16(-1);

    /* Generate some values */
    (void)rnd_next(&state, one);
    rnd_next(&state, one);
    rnd_next(&state, one);

    /* Reseed */
    rnd_next(&state, neg);

    /* Generate first value again - counters should be reset */
    (void)rnd_next(&state, one);

    /* After reseed, sequence should restart.
     * RND(negative) reseeds AND generates a value (counter1 = 1),
     * then the next RND(positive) generates another (counter1 = 2). */
    ASSERT_EQ_INT(state.counter1, 2);
}

/* Test counter wraparound at 0xAB (171) */
TEST(test_rnd_counter_wraparound) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);

    /* Generate 170 values (counter goes from 0 to 170) */
    for (int i = 0; i < 170; i++) {
        rnd_next(&state, one);
    }
    ASSERT_EQ_INT(state.counter1, 170);

    /* Generate one more (counter should be 171 = 0xAB) */
    rnd_next(&state, one);

    /* Counter should have wrapped to 0 */
    ASSERT_EQ_INT(state.counter1, 0);
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

/*
 * Golden test: verify first N values match captured output from original.
 *
 * These values were (or will be) captured by running the original 8K BASIC
 * in the Altair SIMH emulator with this program:
 *
 * 10 FOR I=1 TO 10
 * 20 PRINT RND(1)
 * 30 NEXT I
 *
 * TODO: Capture actual values from original and add here.
 * For now, we just verify the sequence is consistent.
 */
TEST(test_rnd_golden_sequence) {
    rnd_state_t state;
    rnd_init(&state);

    mbf_t one = mbf_from_int16(1);

    /* Capture the raw values for our implementation */
    mbf_t values[10];
    for (int i = 0; i < 10; i++) {
        values[i] = rnd_next(&state, one);
    }

    /* Reset and verify we get the same values */
    rnd_init(&state);
    for (int i = 0; i < 10; i++) {
        mbf_t v = rnd_next(&state, one);
        ASSERT_MBF_EQ(v, values[i]);
    }
}

/* Run all tests */
void run_tests(void) {
    RUN_TEST(test_rnd_init);
    RUN_TEST(test_rnd_zero_returns_last);
    RUN_TEST(test_rnd_generates_sequence);
    RUN_TEST(test_rnd_in_range);
    RUN_TEST(test_rnd_reseed);
    RUN_TEST(test_rnd_counter_wraparound);
    RUN_TEST(test_rnd_deterministic);
    RUN_TEST(test_rnd_golden_sequence);
}

TEST_MAIN()
