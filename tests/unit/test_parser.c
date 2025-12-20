/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * test_parser.c - Unit tests for expression parser
 *
 * Tests the expression parser and evaluator.
 */

#include "test_harness.h"
#include "basic/basic.h"
#include "basic/tokens.h"
#include <string.h>

/* Helper: tokenize and evaluate an expression */
static mbf_t eval_str(const char *expr) {
    uint8_t tokenized[256];
    size_t tok_len = tokenize_line(expr, tokenized, sizeof(tokenized));
    if (tok_len == 0) return MBF_ZERO;

    size_t consumed;
    basic_error_t error;
    return eval_expression(NULL, tokenized, tok_len, &consumed, &error);
}

/* Helper: evaluate and convert to int */
static int eval_int(const char *expr) {
    mbf_t result = eval_str(expr);
    return mbf_to_int16(result, NULL);
}

/* Test simple integer */
TEST(test_parse_integer) {
    ASSERT_EQ_INT(eval_int("42"), 42);
    ASSERT_EQ_INT(eval_int("0"), 0);
    ASSERT_EQ_INT(eval_int("12345"), 12345);
}

/* Test negative numbers */
TEST(test_parse_negative) {
    ASSERT_EQ_INT(eval_int("-1"), -1);
    ASSERT_EQ_INT(eval_int("-42"), -42);
}

/* Test addition */
TEST(test_parse_addition) {
    ASSERT_EQ_INT(eval_int("1+2"), 3);
    ASSERT_EQ_INT(eval_int("10+20+30"), 60);
    ASSERT_EQ_INT(eval_int("100+0"), 100);
}

/* Test subtraction */
TEST(test_parse_subtraction) {
    ASSERT_EQ_INT(eval_int("5-3"), 2);
    ASSERT_EQ_INT(eval_int("10-20"), -10);
    ASSERT_EQ_INT(eval_int("100-50-25"), 25);
}

/* Test multiplication */
TEST(test_parse_multiplication) {
    ASSERT_EQ_INT(eval_int("6*7"), 42);
    ASSERT_EQ_INT(eval_int("2*3*4"), 24);
    ASSERT_EQ_INT(eval_int("100*0"), 0);
}

/* Test division */
TEST(test_parse_division) {
    ASSERT_EQ_INT(eval_int("100/5"), 20);
    ASSERT_EQ_INT(eval_int("42/6"), 7);
    ASSERT_EQ_INT(eval_int("100/10/2"), 5);
}

/* Test operator precedence */
TEST(test_parse_precedence) {
    ASSERT_EQ_INT(eval_int("2+3*4"), 14);      /* * before + */
    ASSERT_EQ_INT(eval_int("10-6/2"), 7);      /* / before - */
    ASSERT_EQ_INT(eval_int("2*3+4*5"), 26);    /* left to right for same precedence */
}

/* Test parentheses */
TEST(test_parse_parentheses) {
    ASSERT_EQ_INT(eval_int("(2+3)*4"), 20);
    ASSERT_EQ_INT(eval_int("2*(3+4)"), 14);
    ASSERT_EQ_INT(eval_int("((1+2)*(3+4))"), 21);
}

/* Test exponentiation */
TEST(test_parse_power) {
    ASSERT_EQ_INT(eval_int("2^3"), 8);
    ASSERT_EQ_INT(eval_int("3^2"), 9);
    ASSERT_EQ_INT(eval_int("2^10"), 1024);
    ASSERT_EQ_INT(eval_int("10^0"), 1);
}

/* Test comparison operators */
TEST(test_parse_comparisons) {
    /* In BASIC, true = -1, false = 0 */
    ASSERT_EQ_INT(eval_int("5>3"), -1);
    ASSERT_EQ_INT(eval_int("3>5"), 0);
    ASSERT_EQ_INT(eval_int("5<3"), 0);
    ASSERT_EQ_INT(eval_int("3<5"), -1);
    ASSERT_EQ_INT(eval_int("5=5"), -1);
    ASSERT_EQ_INT(eval_int("5=3"), 0);
}

/* Test compound comparisons */
TEST(test_parse_compound_comparisons) {
    ASSERT_EQ_INT(eval_int("5>=5"), -1);
    ASSERT_EQ_INT(eval_int("5>=3"), -1);
    ASSERT_EQ_INT(eval_int("3>=5"), 0);
    ASSERT_EQ_INT(eval_int("5<=5"), -1);
    ASSERT_EQ_INT(eval_int("3<=5"), -1);
    ASSERT_EQ_INT(eval_int("5<=3"), 0);
    ASSERT_EQ_INT(eval_int("5<>3"), -1);
    ASSERT_EQ_INT(eval_int("5<>5"), 0);
}

/* Test logical operators */
TEST(test_parse_logical) {
    /* AND */
    ASSERT_EQ_INT(eval_int("-1 AND -1"), -1);
    ASSERT_EQ_INT(eval_int("-1 AND 0"), 0);
    ASSERT_EQ_INT(eval_int("0 AND -1"), 0);
    ASSERT_EQ_INT(eval_int("0 AND 0"), 0);

    /* OR */
    ASSERT_EQ_INT(eval_int("-1 OR -1"), -1);
    ASSERT_EQ_INT(eval_int("-1 OR 0"), -1);
    ASSERT_EQ_INT(eval_int("0 OR -1"), -1);
    ASSERT_EQ_INT(eval_int("0 OR 0"), 0);

    /* NOT */
    ASSERT_EQ_INT(eval_int("NOT 0"), -1);
    ASSERT_EQ_INT(eval_int("NOT -1"), 0);
}

/* Test built-in functions */
TEST(test_parse_functions) {
    ASSERT_EQ_INT(eval_int("ABS(-42)"), 42);
    ASSERT_EQ_INT(eval_int("ABS(42)"), 42);
    ASSERT_EQ_INT(eval_int("SGN(-10)"), -1);
    ASSERT_EQ_INT(eval_int("SGN(0)"), 0);
    ASSERT_EQ_INT(eval_int("SGN(10)"), 1);
    ASSERT_EQ_INT(eval_int("INT(3)"), 3);
    ASSERT_EQ_INT(eval_int("INT(-3)"), -3);
}

/* Test complex expressions */
TEST(test_parse_complex) {
    ASSERT_EQ_INT(eval_int("(1+2)*(3+4)/7"), 3);
    ASSERT_EQ_INT(eval_int("2^3+3^2"), 17);
    ASSERT_EQ_INT(eval_int("(5>3) AND (3<5)"), -1);
    ASSERT_EQ_INT(eval_int("ABS(-10)+SGN(5)*5"), 15);
}

/* Run all tests */
void run_tests(void) {
    RUN_TEST(test_parse_integer);
    RUN_TEST(test_parse_negative);
    RUN_TEST(test_parse_addition);
    RUN_TEST(test_parse_subtraction);
    RUN_TEST(test_parse_multiplication);
    RUN_TEST(test_parse_division);
    RUN_TEST(test_parse_precedence);
    RUN_TEST(test_parse_parentheses);
    RUN_TEST(test_parse_power);
    RUN_TEST(test_parse_comparisons);
    RUN_TEST(test_parse_compound_comparisons);
    RUN_TEST(test_parse_logical);
    RUN_TEST(test_parse_functions);
    RUN_TEST(test_parse_complex);
}

TEST_MAIN()
