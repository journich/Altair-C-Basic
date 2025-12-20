/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * test_mbf.c - Unit tests for MBF floating-point operations
 */

#include "test_harness.h"
#include "basic/mbf.h"

/* Test MBF zero representation */
TEST(test_mbf_zero) {
    mbf_t zero = MBF_ZERO;
    ASSERT(mbf_is_zero(zero));
    ASSERT_EQ_INT(mbf_sign(zero), 0);
    ASSERT_EQ_HEX(zero.raw, 0);
}

/* Test MBF one representation */
TEST(test_mbf_one) {
    mbf_t one = MBF_ONE;
    ASSERT(!mbf_is_zero(one));
    ASSERT_EQ_INT(mbf_sign(one), 1);
    ASSERT_EQ_INT(one.bytes.exponent, 0x81);  /* Bias + 0 = 129 */
}

/* Test integer conversion: positive */
TEST(test_mbf_from_int16_positive) {
    mbf_t result = mbf_from_int16(1);
    ASSERT_EQ_INT(mbf_sign(result), 1);
    ASSERT(!mbf_is_zero(result));

    /* 1 should have exponent 129 (2^0) */
    ASSERT_EQ_INT(result.bytes.exponent, 0x81);
}

/* Test integer conversion: negative */
TEST(test_mbf_from_int16_negative) {
    mbf_t result = mbf_from_int16(-1);
    ASSERT_EQ_INT(mbf_sign(result), -1);
    ASSERT(mbf_is_negative(result));
}

/* Test integer conversion: larger values */
TEST(test_mbf_from_int16_various) {
    mbf_t ten = mbf_from_int16(10);
    ASSERT_EQ_INT(mbf_sign(ten), 1);

    /* Convert back */
    bool overflow;
    int16_t back = mbf_to_int16(ten, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(back, 10);

    /* Test -100 */
    mbf_t neg100 = mbf_from_int16(-100);
    back = mbf_to_int16(neg100, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(back, -100);

    /* Test 32767 */
    mbf_t max = mbf_from_int16(32767);
    back = mbf_to_int16(max, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(back, 32767);
}

/* Test addition: positive + positive */
TEST(test_mbf_add_positive) {
    mbf_t a = mbf_from_int16(5);
    mbf_t b = mbf_from_int16(3);
    mbf_t result = mbf_add(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 8);
}

/* Test addition: positive + negative */
TEST(test_mbf_add_mixed) {
    mbf_t a = mbf_from_int16(10);
    mbf_t b = mbf_from_int16(-3);
    mbf_t result = mbf_add(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 7);
}

/* Test addition: results in zero */
TEST(test_mbf_add_to_zero) {
    mbf_t a = mbf_from_int16(42);
    mbf_t b = mbf_from_int16(-42);
    mbf_t result = mbf_add(a, b);

    ASSERT(mbf_is_zero(result));
}

/* Test subtraction */
TEST(test_mbf_sub) {
    mbf_t a = mbf_from_int16(100);
    mbf_t b = mbf_from_int16(30);
    mbf_t result = mbf_sub(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 70);
}

/* Test multiplication: simple case */
TEST(test_mbf_mul_simple) {
    mbf_t a = mbf_from_int16(6);
    mbf_t b = mbf_from_int16(7);
    mbf_t result = mbf_mul(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 42);
}

/* Test multiplication: with negative */
TEST(test_mbf_mul_negative) {
    mbf_t a = mbf_from_int16(-5);
    mbf_t b = mbf_from_int16(8);
    mbf_t result = mbf_mul(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, -40);
}

/* Test multiplication: both negative */
TEST(test_mbf_mul_both_negative) {
    mbf_t a = mbf_from_int16(-4);
    mbf_t b = mbf_from_int16(-3);
    mbf_t result = mbf_mul(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 12);
}

/* Test multiplication by zero */
TEST(test_mbf_mul_zero) {
    mbf_t a = mbf_from_int16(123);
    mbf_t b = MBF_ZERO;
    mbf_t result = mbf_mul(a, b);

    ASSERT(mbf_is_zero(result));
}

/* Test division: simple case */
TEST(test_mbf_div_simple) {
    mbf_t a = mbf_from_int16(100);
    mbf_t b = mbf_from_int16(5);
    mbf_t result = mbf_div(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 20);
}

/* Test division by zero */
TEST(test_mbf_div_by_zero) {
    mbf_t a = mbf_from_int16(100);
    mbf_t b = MBF_ZERO;

    mbf_clear_error();
    mbf_t result = mbf_div(a, b);
    (void)result;  /* Suppress unused warning */

    ASSERT_EQ(mbf_get_error(), MBF_DIV_ZERO);
}

/* Test negation */
TEST(test_mbf_neg) {
    mbf_t a = mbf_from_int16(42);
    mbf_t neg = mbf_neg(a);

    ASSERT(mbf_is_negative(neg));

    bool overflow;
    int16_t value = mbf_to_int16(neg, &overflow);
    ASSERT_EQ_INT(value, -42);

    /* Negate again */
    mbf_t pos = mbf_neg(neg);
    value = mbf_to_int16(pos, &overflow);
    ASSERT_EQ_INT(value, 42);
}

/* Test absolute value */
TEST(test_mbf_abs) {
    mbf_t a = mbf_from_int16(-42);
    mbf_t abs = mbf_abs(a);

    ASSERT(!mbf_is_negative(abs));

    bool overflow;
    int16_t value = mbf_to_int16(abs, &overflow);
    ASSERT_EQ_INT(value, 42);
}

/* Test comparison */
TEST(test_mbf_cmp) {
    mbf_t a = mbf_from_int16(10);
    mbf_t b = mbf_from_int16(20);
    mbf_t c = mbf_from_int16(10);
    mbf_t d = mbf_from_int16(-5);

    ASSERT_EQ_INT(mbf_cmp(a, b), -1);  /* a < b */
    ASSERT_EQ_INT(mbf_cmp(b, a), 1);   /* b > a */
    ASSERT_EQ_INT(mbf_cmp(a, c), 0);   /* a == c */
    ASSERT_EQ_INT(mbf_cmp(a, d), 1);   /* a > d (positive > negative) */
    ASSERT_EQ_INT(mbf_cmp(d, a), -1);  /* d < a */
}

/* Test INT function */
TEST(test_mbf_int) {
    /* Test with positive value */
    mbf_t a = mbf_from_int16(3);
    mbf_t half = mbf_from_int16(1);
    half = mbf_div(half, mbf_from_int16(2));  /* 0.5 */
    mbf_t three_point_five = mbf_add(a, half);

    mbf_t floored = mbf_int(three_point_five);
    bool overflow;
    int16_t value = mbf_to_int16(floored, &overflow);
    ASSERT_EQ_INT(value, 3);
}

/* Test sign function */
TEST(test_mbf_sign) {
    ASSERT_EQ_INT(mbf_sign(MBF_ZERO), 0);
    ASSERT_EQ_INT(mbf_sign(mbf_from_int16(42)), 1);
    ASSERT_EQ_INT(mbf_sign(mbf_from_int16(-42)), -1);
}

/* Test larger multiplication */
TEST(test_mbf_mul_large) {
    mbf_t a = mbf_from_int16(1000);
    mbf_t b = mbf_from_int16(10);
    mbf_t result = mbf_mul(a, b);

    bool overflow;
    int16_t value = mbf_to_int16(result, &overflow);
    ASSERT(!overflow);
    ASSERT_EQ_INT(value, 10000);
}

/* Run all tests */
void run_tests(void) {
    RUN_TEST(test_mbf_zero);
    RUN_TEST(test_mbf_one);
    RUN_TEST(test_mbf_from_int16_positive);
    RUN_TEST(test_mbf_from_int16_negative);
    RUN_TEST(test_mbf_from_int16_various);
    RUN_TEST(test_mbf_add_positive);
    RUN_TEST(test_mbf_add_mixed);
    RUN_TEST(test_mbf_add_to_zero);
    RUN_TEST(test_mbf_sub);
    RUN_TEST(test_mbf_mul_simple);
    RUN_TEST(test_mbf_mul_negative);
    RUN_TEST(test_mbf_mul_both_negative);
    RUN_TEST(test_mbf_mul_zero);
    RUN_TEST(test_mbf_div_simple);
    RUN_TEST(test_mbf_div_by_zero);
    RUN_TEST(test_mbf_neg);
    RUN_TEST(test_mbf_abs);
    RUN_TEST(test_mbf_cmp);
    RUN_TEST(test_mbf_int);
    RUN_TEST(test_mbf_sign);
    RUN_TEST(test_mbf_mul_large);
}

TEST_MAIN()
