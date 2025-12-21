/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file string.c
 * @brief String BASIC Functions
 *
 * Implements all built-in functions that operate on or return strings.
 * Note: Low-level string operations are in src/memory/strings.c.
 * These are the BASIC interpreter function wrappers.
 *
 * ## String Query Functions
 * - LEN(s$) - Returns length of string (0-255)
 * - ASC(s$) - Returns ASCII value of first character (FC error if empty)
 *
 * ## Substring Functions
 * - LEFT$(s$, n) - Returns leftmost n characters
 * - RIGHT$(s$, n) - Returns rightmost n characters
 * - MID$(s$, start, n) - Returns n characters starting at position start
 *
 * ## String Search
 * - INSTR([start,] main$, search$) - Find substring, returns 1-based position
 *
 * ## String/Number Conversion
 * - STR$(n) - Convert number to string (with leading space for positive)
 * - VAL(s$) - Convert string to number (stops at first non-numeric)
 * - HEX$(n) - Convert number to hexadecimal string
 * - OCT$(n) - Convert number to octal string
 *
 * ## String Generation
 * - CHR$(n) - Single character from ASCII code (0-255)
 * - SPACE$(n) - String of n spaces
 * - STRING$(n, c) - String of n copies of character c
 *
 * ## Return Value Conventions
 *
 * All functions returning strings return a string_desc_t (4-byte descriptor).
 * A descriptor with length=0 indicates an empty string or error condition.
 * The descriptor points to string data in the string heap (see strings.c).
 *
 * ## Index Conventions
 *
 * All position arguments (MID$ start, INSTR start) are 1-based to match
 * BASIC conventions. Internal processing converts to 0-based indexing.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include <string.h>

/*
 * LEN function - returns length of string.
 */
mbf_t fn_len(string_desc_t str) {
    return mbf_from_int16((int16_t)string_len(str));
}

/*
 * LEFT$ function - returns leftmost n characters.
 */
string_desc_t fn_left(basic_state_t *state, string_desc_t str, mbf_t n) {
    bool overflow;
    int16_t count = mbf_to_int16(n, &overflow);

    if (overflow || count < 0) {
        return (string_desc_t){0, 0, 0};
    }

    return string_left(state, str, (uint8_t)count);
}

/*
 * RIGHT$ function - returns rightmost n characters.
 */
string_desc_t fn_right(basic_state_t *state, string_desc_t str, mbf_t n) {
    bool overflow;
    int16_t count = mbf_to_int16(n, &overflow);

    if (overflow || count < 0) {
        return (string_desc_t){0, 0, 0};
    }

    return string_right(state, str, (uint8_t)count);
}

/*
 * MID$ function - returns substring.
 * start is 1-based, n is length (0 = rest of string).
 */
string_desc_t fn_mid(basic_state_t *state, string_desc_t str, mbf_t start, mbf_t n) {
    bool overflow1, overflow2;
    int16_t s = mbf_to_int16(start, &overflow1);
    int16_t len = mbf_to_int16(n, &overflow2);

    if (overflow1 || overflow2 || s < 1 || len < 0) {
        return (string_desc_t){0, 0, 0};
    }

    return string_mid(state, str, (uint8_t)s, (uint8_t)len);
}

/*
 * ASC function - returns ASCII value of first character.
 */
mbf_t fn_asc(basic_state_t *state, string_desc_t str) {
    if (str.length == 0) {
        /* FC error in original for empty string */
        mbf_set_error(MBF_DOMAIN);
        return MBF_ZERO;
    }

    return mbf_from_int16((int16_t)string_asc(state, str));
}

/*
 * CHR$ function - returns single-character string.
 */
string_desc_t fn_chr(basic_state_t *state, mbf_t code) {
    bool overflow;
    int16_t c = mbf_to_int16(code, &overflow);

    if (overflow || c < 0 || c > 255) {
        return (string_desc_t){0, 0, 0};
    }

    return string_chr(state, (uint8_t)c);
}

/*
 * STR$ function - converts number to string.
 */
string_desc_t fn_str(basic_state_t *state, mbf_t value) {
    return string_str(state, value);
}

/*
 * VAL function - converts string to number.
 */
mbf_t fn_val(basic_state_t *state, string_desc_t str) {
    return string_val(state, str);
}

/*
 * INSTR function - finds substring in string.
 * Returns 1-based position, or 0 if not found.
 *
 * INSTR([start,] main$, search$)
 */
mbf_t fn_instr(basic_state_t *state, int start,
               string_desc_t main_str, string_desc_t search_str) {

    /* Handle edge cases */
    if (search_str.length == 0) {
        return mbf_from_int16((int16_t)start);  /* Empty search matches at start */
    }

    if (main_str.length == 0 || start < 1) {
        return MBF_ZERO;
    }

    /* Convert to 0-based index */
    int idx = start - 1;

    if (idx >= main_str.length) {
        return MBF_ZERO;
    }

    /* Get string data */
    const char *main_data = string_get_data(state, main_str);
    const char *search_data = string_get_data(state, search_str);

    if (!main_data || !search_data) {
        return MBF_ZERO;
    }

    /* Search for substring */
    for (int i = idx; i <= main_str.length - search_str.length; i++) {
        bool match = true;
        for (int j = 0; j < search_str.length; j++) {
            if (main_data[i + j] != search_data[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return mbf_from_int16((int16_t)(i + 1));  /* 1-based */
        }
    }

    return MBF_ZERO;  /* Not found */
}

/*
 * SPACE$ function - returns string of n spaces.
 */
string_desc_t fn_space(basic_state_t *state, mbf_t n) {
    bool overflow;
    int16_t count = mbf_to_int16(n, &overflow);

    if (overflow || count < 0 || count > 255) {
        return (string_desc_t){0, 0, 0};
    }

    if (count == 0) {
        return (string_desc_t){0, 0, 0};
    }

    /* Allocate space for the spaces */
    uint16_t ptr = string_alloc(state, (uint8_t)count);
    if (ptr == 0) {
        return (string_desc_t){0, 0, 0};
    }

    /* Fill with spaces */
    memset(state->memory + ptr, ' ', (size_t)count);

    string_desc_t result;
    result.length = (uint8_t)count;
    result._reserved = 0;
    result.ptr = ptr;
    return result;
}

/*
 * STRING$ function - returns string of repeated characters.
 * STRING$(n, char_code) or STRING$(n, string$)
 */
string_desc_t fn_string(basic_state_t *state, mbf_t n, uint8_t ch) {
    bool overflow;
    int16_t count = mbf_to_int16(n, &overflow);

    if (overflow || count < 0 || count > 255) {
        return (string_desc_t){0, 0, 0};
    }

    if (count == 0) {
        return (string_desc_t){0, 0, 0};
    }

    /* Allocate space */
    uint16_t ptr = string_alloc(state, (uint8_t)count);
    if (ptr == 0) {
        return (string_desc_t){0, 0, 0};
    }

    /* Fill with character */
    memset(state->memory + ptr, ch, (size_t)count);

    string_desc_t result;
    result.length = (uint8_t)count;
    result._reserved = 0;
    result.ptr = ptr;
    return result;
}

/*
 * HEX$ function - converts number to hexadecimal string.
 */
string_desc_t fn_hex(basic_state_t *state, mbf_t value) {
    bool overflow;
    int32_t n = mbf_to_int32(value, &overflow);

    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%X", (unsigned int)(n & 0xFFFF));

    return string_create_len(state, buf, (uint8_t)len);
}

/*
 * OCT$ function - converts number to octal string.
 */
string_desc_t fn_oct(basic_state_t *state, mbf_t value) {
    bool overflow;
    int32_t n = mbf_to_int32(value, &overflow);

    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%o", (unsigned int)(n & 0xFFFF));

    return string_create_len(state, buf, (uint8_t)len);
}
