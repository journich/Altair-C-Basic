/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * test_memory.c - Unit tests for variables, arrays, and strings
 */

#include "test_harness.h"
#include "basic/basic.h"
#include <string.h>

/* Helper to create an initialized interpreter state for testing */
static basic_state_t *create_test_state(void) {
    basic_config_t config = {
        .memory_size = 16384,  /* 16K for testing */
        .terminal_width = 72,
        .want_trig = false,
        .input = stdin,
        .output = stdout
    };
    return basic_init(&config);
}

/* ======== Variable Tests ======== */

TEST(test_var_create_numeric) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    uint8_t *var = var_create(state, "A");
    ASSERT(var != NULL);
    ASSERT_EQ_INT(var_count(state), 1);

    basic_free(state);
}

TEST(test_var_create_string) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    uint8_t *var = var_create(state, "A$");
    ASSERT(var != NULL);
    ASSERT(var_is_string("A$"));
    ASSERT(!var_is_string("A"));
    ASSERT_EQ_INT(var_count(state), 1);

    basic_free(state);
}

TEST(test_var_set_get_numeric) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    mbf_t value = mbf_from_int16(42);
    ASSERT(var_set_numeric(state, "X", value));

    mbf_t result = var_get_numeric(state, "X");
    ASSERT_MBF_EQ(result, value);

    basic_free(state);
}

TEST(test_var_set_get_string) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t desc = string_create(state, "HELLO");
    ASSERT(var_set_string(state, "S$", desc));

    string_desc_t result = var_get_string(state, "S$");
    ASSERT_EQ_INT(result.length, 5);

    const char *data = string_get_data(state, result);
    ASSERT(data != NULL);
    ASSERT(memcmp(data, "HELLO", 5) == 0);

    basic_free(state);
}

TEST(test_var_find_and_create) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    /* Variable doesn't exist yet */
    uint8_t *var = var_find(state, "Z");
    ASSERT(var == NULL);

    /* Create it */
    var = var_get_or_create(state, "Z");
    ASSERT(var != NULL);

    /* Now it exists */
    uint8_t *found = var_find(state, "Z");
    ASSERT(found == var);

    basic_free(state);
}

TEST(test_var_two_char_names) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    var_set_numeric(state, "AB", mbf_from_int16(1));
    var_set_numeric(state, "CD", mbf_from_int16(2));

    bool overflow;
    mbf_t ab = var_get_numeric(state, "AB");
    mbf_t cd = var_get_numeric(state, "CD");

    ASSERT_EQ_INT(mbf_to_int16(ab, &overflow), 1);
    ASSERT_EQ_INT(mbf_to_int16(cd, &overflow), 2);
    ASSERT_EQ_INT(var_count(state), 2);

    basic_free(state);
}

TEST(test_var_clear_all) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    var_set_numeric(state, "A", mbf_from_int16(1));
    var_set_numeric(state, "B", mbf_from_int16(2));
    ASSERT_EQ_INT(var_count(state), 2);

    var_clear_all(state);
    ASSERT_EQ_INT(var_count(state), 0);

    basic_free(state);
}

/* ======== Array Tests ======== */

TEST(test_array_create_1d) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    /* DIM A(10) */
    uint8_t *arr = array_create(state, "A", 10, -1);
    ASSERT(arr != NULL);

    /* Check dimensions stored correctly */
    ASSERT_EQ_INT(arr[2], 1);  /* 1 dimension */
    ASSERT_EQ_INT(arr[3], 10); /* dim1 = 10 */
    ASSERT_EQ_INT(arr[4], 0);  /* high byte of dim1 */

    basic_free(state);
}

TEST(test_array_create_2d) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    /* DIM B(5,3) */
    uint8_t *arr = array_create(state, "B", 5, 3);
    ASSERT(arr != NULL);

    /* Check dimensions stored correctly */
    ASSERT_EQ_INT(arr[2], 2);  /* 2 dimensions */
    ASSERT_EQ_INT(arr[3], 5);  /* dim1 = 5 */
    ASSERT_EQ_INT(arr[5], 3);  /* dim2 = 3 */

    basic_free(state);
}

TEST(test_array_set_get_1d) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    array_create(state, "A", 10, -1);

    /* A(5) = 42 */
    mbf_t value = mbf_from_int16(42);
    ASSERT(array_set_numeric(state, "A", 5, -1, value));

    /* Check A(5) */
    mbf_t result = array_get_numeric(state, "A", 5, -1);
    ASSERT_MBF_EQ(result, value);

    basic_free(state);
}

TEST(test_array_set_get_2d) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    array_create(state, "M", 3, 3);

    /* M(1,2) = 100 */
    mbf_t value = mbf_from_int16(100);
    ASSERT(array_set_numeric(state, "M", 1, 2, value));

    /* Check M(1,2) */
    mbf_t result = array_get_numeric(state, "M", 1, 2);
    ASSERT_MBF_EQ(result, value);

    /* Check M(0,0) is still zero */
    mbf_t zero = array_get_numeric(state, "M", 0, 0);
    ASSERT(mbf_is_zero(zero));

    basic_free(state);
}

TEST(test_array_auto_create) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    /* Access undimensioned array - should auto-create with DIM 10 */
    mbf_t value = mbf_from_int16(7);
    ASSERT(array_set_numeric(state, "X", 5, -1, value));

    mbf_t result = array_get_numeric(state, "X", 5, -1);
    ASSERT_MBF_EQ(result, value);

    basic_free(state);
}

TEST(test_array_bounds_check) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    array_create(state, "A", 5, -1);

    /* Valid access */
    uint8_t *elem = array_get_element(state, "A", 3, -1);
    ASSERT(elem != NULL);

    /* Out of bounds should return NULL */
    elem = array_get_element(state, "A", 10, -1);
    ASSERT(elem == NULL);

    basic_free(state);
}

/* ======== String Tests ======== */

TEST(test_string_create) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "HELLO WORLD");
    ASSERT_EQ_INT(s.length, 11);
    ASSERT(s.ptr != 0);

    const char *data = string_get_data(state, s);
    ASSERT(data != NULL);
    ASSERT(memcmp(data, "HELLO WORLD", 11) == 0);

    basic_free(state);
}

TEST(test_string_empty) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "");
    ASSERT_EQ_INT(s.length, 0);

    basic_free(state);
}

TEST(test_string_concat) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t a = string_create(state, "HELLO");
    string_desc_t b = string_create(state, " WORLD");
    string_desc_t result = string_concat(state, a, b);

    ASSERT_EQ_INT(result.length, 11);
    const char *data = string_get_data(state, result);
    ASSERT(memcmp(data, "HELLO WORLD", 11) == 0);

    basic_free(state);
}

TEST(test_string_compare) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t a = string_create(state, "ABC");
    string_desc_t b = string_create(state, "ABC");
    string_desc_t c = string_create(state, "ABD");
    string_desc_t d = string_create(state, "AB");

    ASSERT_EQ_INT(string_compare(state, a, b), 0);   /* Equal */
    ASSERT_EQ_INT(string_compare(state, a, c), -1);  /* ABC < ABD */
    ASSERT_EQ_INT(string_compare(state, c, a), 1);   /* ABD > ABC */
    ASSERT_EQ_INT(string_compare(state, a, d), 1);   /* ABC > AB */
    ASSERT_EQ_INT(string_compare(state, d, a), -1);  /* AB < ABC */

    basic_free(state);
}

TEST(test_string_left) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "HELLO");
    string_desc_t result = string_left(state, s, 2);

    ASSERT_EQ_INT(result.length, 2);
    const char *data = string_get_data(state, result);
    ASSERT(memcmp(data, "HE", 2) == 0);

    basic_free(state);
}

TEST(test_string_right) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "HELLO");
    string_desc_t result = string_right(state, s, 3);

    ASSERT_EQ_INT(result.length, 3);
    const char *data = string_get_data(state, result);
    ASSERT(memcmp(data, "LLO", 3) == 0);

    basic_free(state);
}

TEST(test_string_mid) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "HELLO");

    /* MID$("HELLO", 2, 3) = "ELL" */
    string_desc_t result = string_mid(state, s, 2, 3);
    ASSERT_EQ_INT(result.length, 3);
    const char *data = string_get_data(state, result);
    ASSERT(memcmp(data, "ELL", 3) == 0);

    basic_free(state);
}

TEST(test_string_len) {
    string_desc_t s = {5, 0, 0x1000};
    ASSERT_EQ_INT(string_len(s), 5);

    string_desc_t empty = {0, 0, 0};
    ASSERT_EQ_INT(string_len(empty), 0);
}

TEST(test_string_asc_chr) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "A");
    ASSERT_EQ_INT(string_asc(state, s), 65);

    string_desc_t c = string_chr(state, 66);
    ASSERT_EQ_INT(c.length, 1);
    const char *data = string_get_data(state, c);
    ASSERT_EQ_INT(data[0], 'B');

    basic_free(state);
}

TEST(test_string_val) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    string_desc_t s = string_create(state, "123");
    mbf_t result = string_val(state, s);

    bool overflow;
    ASSERT_EQ_INT(mbf_to_int16(result, &overflow), 123);

    basic_free(state);
}

TEST(test_string_str) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    mbf_t value = mbf_from_int16(456);
    string_desc_t result = string_str(state, value);

    /* STR$ adds leading space for positive numbers */
    const char *data = string_get_data(state, result);
    ASSERT(data != NULL);
    /* First char should be space for positive */
    ASSERT_EQ_INT(data[0], ' ');
    ASSERT(memcmp(data + 1, "456", 3) == 0);

    basic_free(state);
}

TEST(test_string_free_space) {
    basic_state_t *state = create_test_state();
    ASSERT(state != NULL);

    uint16_t initial = string_free(state);

    /* Create some strings */
    string_create(state, "TEST STRING 1");
    string_create(state, "TEST STRING 2");

    uint16_t after = string_free(state);
    ASSERT(after < initial);

    basic_free(state);
}

static void run_tests(void) {
    /* Variable tests */
    RUN_TEST(test_var_create_numeric);
    RUN_TEST(test_var_create_string);
    RUN_TEST(test_var_set_get_numeric);
    RUN_TEST(test_var_set_get_string);
    RUN_TEST(test_var_find_and_create);
    RUN_TEST(test_var_two_char_names);
    RUN_TEST(test_var_clear_all);

    /* Array tests */
    RUN_TEST(test_array_create_1d);
    RUN_TEST(test_array_create_2d);
    RUN_TEST(test_array_set_get_1d);
    RUN_TEST(test_array_set_get_2d);
    RUN_TEST(test_array_auto_create);
    RUN_TEST(test_array_bounds_check);

    /* String tests */
    RUN_TEST(test_string_create);
    RUN_TEST(test_string_empty);
    RUN_TEST(test_string_concat);
    RUN_TEST(test_string_compare);
    RUN_TEST(test_string_left);
    RUN_TEST(test_string_right);
    RUN_TEST(test_string_mid);
    RUN_TEST(test_string_len);
    RUN_TEST(test_string_asc_chr);
    RUN_TEST(test_string_val);
    RUN_TEST(test_string_str);
    RUN_TEST(test_string_free_space);
}

TEST_MAIN()
