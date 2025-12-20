/*
 * strings.c - String Space Management
 *
 * Implements string storage matching the original 8K BASIC.
 *
 * String space layout:
 *   - String space starts at string_start and grows downward toward arrays
 *   - string_end is the top of memory (highest address)
 *   - Strings are stored with data at the high end, growing down
 *   - Each string variable/array element holds a descriptor pointing to the data
 *
 * String descriptor (4 bytes):
 *   Byte 0: Length (0-255)
 *   Byte 1: Reserved
 *   Bytes 2-3: Pointer to string data (little-endian)
 *
 * Garbage collection:
 *   When string space is exhausted, collect unreferenced strings.
 *   Original uses mark-and-sweep: scan all string variables/arrays,
 *   mark referenced strings, compact string space.
 */

#include "basic/basic.h"
#include <string.h>

/*
 * Initialize string space.
 * Called at startup and by CLEAR.
 */
void string_init(basic_state_t *state) {
    if (!state) return;

    /* String space is at the top of memory */
    /* string_start marks the bottom of used string space (grows down) */
    /* string_end marks the top (fixed at memory top) */
    state->string_start = state->string_end;
}

/*
 * Allocate space for a new string.
 * Returns pointer (offset into memory) to the allocated space,
 * or 0 if out of memory (after garbage collection attempt).
 */
uint16_t string_alloc(basic_state_t *state, uint8_t length) {
    if (!state || length == 0) return 0;

    /* Check if there's room */
    if (state->string_start - length < state->array_start) {
        /* Try garbage collection */
        string_garbage_collect(state);

        /* Check again */
        if (state->string_start - length < state->array_start) {
            return 0;  /* Out of memory */
        }
    }

    /* Allocate from top of free space, growing down */
    state->string_start -= length;

    return state->string_start;
}

/*
 * Create a string in string space from a C string.
 * Returns a string descriptor.
 */
string_desc_t string_create(basic_state_t *state, const char *str) {
    string_desc_t result = {0, 0, 0};

    if (!state || !str) return result;

    size_t len = strlen(str);
    if (len > 255) len = 255;  /* Max string length */

    if (len == 0) {
        result.length = 0;
        result.ptr = 0;
        return result;
    }

    uint16_t ptr = string_alloc(state, (uint8_t)len);
    if (ptr == 0) return result;  /* Out of memory */

    /* Copy string data */
    memcpy(state->memory + ptr, str, len);

    result.length = (uint8_t)len;
    result.ptr = ptr;
    return result;
}

/*
 * Create a string from data with explicit length.
 */
string_desc_t string_create_len(basic_state_t *state, const char *data, uint8_t length) {
    string_desc_t result = {0, 0, 0};

    if (!state || !data || length == 0) return result;

    uint16_t ptr = string_alloc(state, length);
    if (ptr == 0) return result;

    memcpy(state->memory + ptr, data, length);

    result.length = length;
    result.ptr = ptr;
    return result;
}

/*
 * Get pointer to string data.
 */
const char *string_get_data(basic_state_t *state, string_desc_t desc) {
    if (!state || desc.length == 0 || desc.ptr == 0) return NULL;
    if (desc.ptr >= state->memory_size) return NULL;

    return (const char *)(state->memory + desc.ptr);
}

/*
 * Copy a string to a new location in string space.
 * Used for string assignment.
 */
string_desc_t string_copy(basic_state_t *state, string_desc_t src) {
    if (src.length == 0) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    const char *data = string_get_data(state, src);
    if (!data) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    return string_create_len(state, data, src.length);
}

/*
 * Concatenate two strings.
 */
string_desc_t string_concat(basic_state_t *state, string_desc_t a, string_desc_t b) {
    string_desc_t result = {0, 0, 0};

    if (!state) return result;

    uint16_t total_len = (uint16_t)a.length + (uint16_t)b.length;
    if (total_len > 255) {
        /* String too long error - would be LS error in original */
        return result;
    }

    if (total_len == 0) return result;

    uint16_t ptr = string_alloc(state, (uint8_t)total_len);
    if (ptr == 0) return result;

    /* Copy first string */
    if (a.length > 0) {
        const char *data_a = string_get_data(state, a);
        if (data_a) {
            memcpy(state->memory + ptr, data_a, a.length);
        }
    }

    /* Copy second string */
    if (b.length > 0) {
        const char *data_b = string_get_data(state, b);
        if (data_b) {
            memcpy(state->memory + ptr + a.length, data_b, b.length);
        }
    }

    result.length = (uint8_t)total_len;
    result.ptr = ptr;
    return result;
}

/*
 * Compare two strings.
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b.
 */
int string_compare(basic_state_t *state, string_desc_t a, string_desc_t b) {
    /* Both empty */
    if (a.length == 0 && b.length == 0) return 0;

    /* One empty */
    if (a.length == 0) return -1;
    if (b.length == 0) return 1;

    const char *data_a = string_get_data(state, a);
    const char *data_b = string_get_data(state, b);

    if (!data_a && !data_b) return 0;
    if (!data_a) return -1;
    if (!data_b) return 1;

    /* Compare character by character */
    uint8_t min_len = (a.length < b.length) ? a.length : b.length;

    for (uint8_t i = 0; i < min_len; i++) {
        if ((unsigned char)data_a[i] < (unsigned char)data_b[i]) return -1;
        if ((unsigned char)data_a[i] > (unsigned char)data_b[i]) return 1;
    }

    /* Common prefix matches - shorter string is "less" */
    if (a.length < b.length) return -1;
    if (a.length > b.length) return 1;
    return 0;
}

/*
 * LEFT$ function - get leftmost n characters.
 */
string_desc_t string_left(basic_state_t *state, string_desc_t str, uint8_t n) {
    if (n == 0 || str.length == 0) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    if (n >= str.length) {
        return string_copy(state, str);
    }

    const char *data = string_get_data(state, str);
    if (!data) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    return string_create_len(state, data, n);
}

/*
 * RIGHT$ function - get rightmost n characters.
 */
string_desc_t string_right(basic_state_t *state, string_desc_t str, uint8_t n) {
    if (n == 0 || str.length == 0) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    if (n >= str.length) {
        return string_copy(state, str);
    }

    const char *data = string_get_data(state, str);
    if (!data) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    return string_create_len(state, data + (str.length - n), n);
}

/*
 * MID$ function - get substring starting at position start (1-based), length n.
 * If n is 0 or omitted, returns rest of string from start.
 */
string_desc_t string_mid(basic_state_t *state, string_desc_t str, uint8_t start, uint8_t n) {
    if (start == 0 || str.length == 0) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    /* Convert to 0-based index */
    uint8_t idx = start - 1;

    if (idx >= str.length) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    const char *data = string_get_data(state, str);
    if (!data) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    /* Calculate available length from start position */
    uint8_t available = str.length - idx;

    /* If n is 0, use rest of string */
    if (n == 0) n = available;

    /* Clamp to available */
    if (n > available) n = available;

    return string_create_len(state, data + idx, n);
}

/*
 * LEN function - get string length.
 */
uint8_t string_len(string_desc_t str) {
    return str.length;
}

/*
 * ASC function - get ASCII value of first character.
 * Returns 0 for empty string (original gives FC error).
 */
uint8_t string_asc(basic_state_t *state, string_desc_t str) {
    if (str.length == 0) return 0;

    const char *data = string_get_data(state, str);
    if (!data) return 0;

    return (uint8_t)data[0];
}

/*
 * CHR$ function - create single-character string.
 */
string_desc_t string_chr(basic_state_t *state, uint8_t ch) {
    char buf[1];
    buf[0] = (char)ch;
    return string_create_len(state, buf, 1);
}

/*
 * VAL function - convert string to number.
 * Returns MBF value.
 */
mbf_t string_val(basic_state_t *state, string_desc_t str) {
    if (str.length == 0) return MBF_ZERO;

    const char *data = string_get_data(state, str);
    if (!data) return MBF_ZERO;

    /* Create null-terminated copy for parsing */
    char buf[256];
    uint8_t len = str.length;
    memcpy(buf, data, len);
    buf[len] = '\0';

    mbf_t result;
    mbf_from_string(buf, &result);
    return result;
}

/*
 * STR$ function - convert number to string.
 */
string_desc_t string_str(basic_state_t *state, mbf_t value) {
    char buf[32];
    size_t len = mbf_to_string(value, buf, sizeof(buf));

    if (len == 0) {
        string_desc_t empty = {0, 0, 0};
        return empty;
    }

    /* Add leading space for positive numbers (matching original) */
    if (!mbf_is_negative(value)) {
        char buf2[33];
        buf2[0] = ' ';
        memcpy(buf2 + 1, buf, len);
        return string_create_len(state, buf2, (uint8_t)(len + 1));
    }

    return string_create_len(state, buf, (uint8_t)len);
}

/*
 * Garbage collection for string space.
 *
 * The original 8K BASIC uses a mark-and-compact algorithm:
 * 1. Find the highest string pointer among all variables/arrays
 * 2. Copy that string to the top of string space
 * 3. Update the descriptor
 * 4. Repeat until all strings are compacted
 *
 * This is O(n^2) but works well for the small memory sizes of the era.
 */
void string_garbage_collect(basic_state_t *state) {
    if (!state) return;

    /* Reset string space to empty */
    uint16_t new_string_start = state->string_end;

    /* Scan all simple string variables */
    uint8_t *ptr = state->memory + state->var_start;
    uint8_t *end = state->memory + state->array_start;

    while (ptr + 6 <= end) {
        /* Check if this is a string variable (bit 7 set in second name byte) */
        if (ptr[1] & 0x80) {
            /* Get descriptor from bytes 2-5 */
            uint8_t len = ptr[2];
            uint16_t old_ptr = (uint16_t)(ptr[4] | (ptr[5] << 8));

            if (len > 0 && old_ptr != 0 && old_ptr < state->memory_size) {
                /* Allocate new space */
                if (new_string_start - len >= state->array_start) {
                    new_string_start -= len;

                    /* Copy string data */
                    memcpy(state->memory + new_string_start,
                           state->memory + old_ptr, len);

                    /* Update descriptor pointer */
                    ptr[4] = (uint8_t)(new_string_start & 0xFF);
                    ptr[5] = (uint8_t)(new_string_start >> 8);
                }
            }
        }
        ptr += 6;
    }

    /* TODO: Also scan string arrays when array_find is implemented */

    state->string_start = new_string_start;
}

/*
 * Get free string space.
 */
uint16_t string_free(basic_state_t *state) {
    if (!state) return 0;

    if (state->string_start <= state->array_start) return 0;

    return state->string_start - state->array_start;
}

/*
 * Clear string space.
 * Called by CLEAR statement.
 */
void string_clear(basic_state_t *state) {
    if (!state) return;
    string_init(state);
}
