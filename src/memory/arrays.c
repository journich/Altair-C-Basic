/*
 * arrays.c - Array Handling
 *
 * Implements array storage and access matching the original 8K BASIC.
 *
 * Array format in memory:
 *   Bytes 0-1: Array name (same encoding as variables)
 *   Byte 2: Number of dimensions (1 or 2)
 *   Bytes 3-4: Size of dimension 1 (little-endian)
 *   Bytes 5-6: Size of dimension 2 (if 2D, else not present)
 *   Following: Array data (4 bytes per element for numeric, varies for string)
 *
 * Arrays are stored after simple variables and grow upward.
 * Default dimension is 10 (0-10, so 11 elements) if not DIMmed.
 */

#include "basic/basic.h"
#include <string.h>
#include <ctype.h>

/* Default array dimension (0 to 10 = 11 elements) */
#define DEFAULT_DIM 10

/* Size of array header for 1D array */
#define ARRAY_HEADER_1D 5

/* Size of array header for 2D array */
#define ARRAY_HEADER_2D 7

/*
 * Encode array name (same as variable name encoding).
 */
static void encode_array_name(const char *name, uint8_t *encoded) {
    encoded[0] = (uint8_t)toupper((unsigned char)name[0]);
    encoded[1] = 0;

    size_t len = strlen(name);
    bool is_string = (len > 0 && name[len - 1] == '$');

    if (is_string) {
        len--;
    }

    if (len >= 2 && isalnum((unsigned char)name[1])) {
        encoded[1] = (uint8_t)toupper((unsigned char)name[1]);
    }

    if (is_string) {
        encoded[1] |= 0x80;
    }
}

/*
 * Get the size of an array element (4 for numeric, 4 for string descriptor).
 */
static size_t element_size(bool is_string) {
    (void)is_string;
    return 4;  /* Both numeric (MBF) and string descriptor are 4 bytes */
}

/*
 * Calculate size of an array given its header.
 */
static size_t array_size_from_header(const uint8_t *header, bool is_string) {
    int dims = header[2];
    int dim1 = header[3] | (header[4] << 8);

    if (dims == 1) {
        return ARRAY_HEADER_1D + (size_t)(dim1 + 1) * element_size(is_string);
    } else {
        int dim2 = header[5] | (header[6] << 8);
        return ARRAY_HEADER_2D + (size_t)(dim1 + 1) * (size_t)(dim2 + 1) * element_size(is_string);
    }
}


/*
 * Find an array by name.
 * Returns pointer to the array header, or NULL if not found.
 */
uint8_t *array_find(basic_state_t *state, const char *name) {
    if (!state || !name || !name[0]) return NULL;

    uint8_t encoded[2];
    encode_array_name(name, encoded);

    bool is_string = (encoded[1] & 0x80) != 0;

    /* Calculate where arrays begin: after all simple variables */
    /* Variables are 6 bytes each from var_start */
    /* array_start tracks where the NEXT item (var or array) will be placed */

    /* We need to scan from var_start through all 6-byte variable entries */
    /* then scan array headers. The challenge is distinguishing vars from arrays. */

    /* Since we don't have a separate arrays_base, we'll scan all memory
     * looking for array headers. Arrays have a specific format:
     * - name[2], dims (1 or 2), dim1[2], [dim2[2]] */

    /* Simple approach: Scan from var_start, skip 6-byte var entries using var_count,
     * then look for arrays. var_count() gives us the number of variables. */
    int num_vars = var_count(state);
    uint16_t arrays_base = state->var_start + (uint16_t)(num_vars * 6);

    /* Now scan from arrays_base to array_start (current allocation pointer) */
    uint8_t *ptr = state->memory + arrays_base;
    uint8_t *end = state->memory + state->array_start;

    while (ptr < end) {
        /* Check if this is our array */
        if (ptr[0] == encoded[0] && ptr[1] == encoded[1]) {
            return ptr;
        }

        /* Skip to next array */
        size_t arr_size = array_size_from_header(ptr, (ptr[1] & 0x80) != 0);
        ptr += arr_size;
    }

    (void)is_string;
    return NULL;
}

/*
 * Calculate total array size in bytes.
 */
static size_t array_total_size(int dims, int dim1, int dim2, bool is_string) {
    size_t header = (dims == 1) ? ARRAY_HEADER_1D : ARRAY_HEADER_2D;
    size_t elements = (size_t)(dim1 + 1);  /* 0 to dim1 inclusive */
    if (dims == 2) {
        elements *= (size_t)(dim2 + 1);
    }
    return header + elements * element_size(is_string);
}

/*
 * Create a new array with DIM.
 * dim1 and dim2 are the maximum indices (not sizes).
 * For 1D arrays, dim2 should be -1.
 * Returns pointer to array header, or NULL on error.
 */
uint8_t *array_create(basic_state_t *state, const char *name, int dim1, int dim2) {
    if (!state || !name || !name[0]) return NULL;
    if (dim1 < 0 || dim1 > 32767) return NULL;
    if (dim2 > 32767) return NULL;

    /* Check if array already exists */
    if (array_find(state, name)) {
        return NULL;  /* DD error - double dimension */
    }

    bool is_string = (name[strlen(name) - 1] == '$');
    int dims = (dim2 >= 0) ? 2 : 1;

    size_t total_size = array_total_size(dims, dim1, dim2 >= 0 ? dim2 : 0, is_string);

    /* Check if there's room */
    if (state->array_start + total_size > state->string_start) {
        return NULL;  /* OM error - out of memory */
    }

    uint8_t *ptr = state->memory + state->array_start;

    /* Write header */
    encode_array_name(name, ptr);
    ptr[2] = (uint8_t)dims;
    ptr[3] = (uint8_t)(dim1 & 0xFF);
    ptr[4] = (uint8_t)((dim1 >> 8) & 0xFF);

    if (dims == 2) {
        ptr[5] = (uint8_t)(dim2 & 0xFF);
        ptr[6] = (uint8_t)((dim2 >> 8) & 0xFF);
    }

    /* Initialize data to zero */
    size_t header = (dims == 1) ? ARRAY_HEADER_1D : ARRAY_HEADER_2D;
    size_t data_size = total_size - header;
    memset(ptr + header, 0, data_size);

    /* Update pointer */
    state->array_start += (uint16_t)total_size;

    return ptr;
}

/*
 * Get element from array.
 * Returns pointer to the element data (4 bytes), or NULL on error.
 */
uint8_t *array_get_element(basic_state_t *state, const char *name,
                           int index1, int index2) {
    uint8_t *arr = array_find(state, name);
    if (!arr) {
        /* Auto-create with default dimension */
        arr = array_create(state, name, DEFAULT_DIM, index2 >= 0 ? DEFAULT_DIM : -1);
        if (!arr) return NULL;
    }

    int dims = arr[2];
    int dim1 = arr[3] | (arr[4] << 8);

    /* Check bounds */
    if (index1 < 0 || index1 > dim1) return NULL;

    size_t offset;
    if (dims == 1) {
        if (index2 >= 0) return NULL;  /* Too many subscripts */
        offset = ARRAY_HEADER_1D + (size_t)index1 * 4;
    } else {
        int dim2 = arr[5] | (arr[6] << 8);
        if (index2 < 0 || index2 > dim2) return NULL;
        offset = ARRAY_HEADER_2D + ((size_t)index1 * (size_t)(dim2 + 1) + (size_t)index2) * 4;
    }

    return arr + offset;
}

/*
 * Get numeric value from array element.
 */
mbf_t array_get_numeric(basic_state_t *state, const char *name,
                        int index1, int index2) {
    uint8_t *elem = array_get_element(state, name, index1, index2);
    if (!elem) return MBF_ZERO;

    mbf_t result;
    memcpy(&result.raw, elem, 4);
    return result;
}

/*
 * Set numeric value in array element.
 */
bool array_set_numeric(basic_state_t *state, const char *name,
                       int index1, int index2, mbf_t value) {
    uint8_t *elem = array_get_element(state, name, index1, index2);
    if (!elem) return false;

    memcpy(elem, &value.raw, 4);
    return true;
}

/*
 * Get string descriptor from array element.
 */
string_desc_t array_get_string(basic_state_t *state, const char *name,
                               int index1, int index2) {
    string_desc_t empty = {0, 0, 0};
    uint8_t *elem = array_get_element(state, name, index1, index2);
    if (!elem) return empty;

    string_desc_t result;
    result.length = elem[0];
    result._reserved = elem[1];
    result.ptr = (uint16_t)(elem[2] | (elem[3] << 8));
    return result;
}

/*
 * Set string descriptor in array element.
 */
bool array_set_string(basic_state_t *state, const char *name,
                      int index1, int index2, string_desc_t desc) {
    uint8_t *elem = array_get_element(state, name, index1, index2);
    if (!elem) return false;

    elem[0] = desc.length;
    elem[1] = desc._reserved;
    elem[2] = (uint8_t)(desc.ptr & 0xFF);
    elem[3] = (uint8_t)(desc.ptr >> 8);
    return true;
}

/*
 * Clear all arrays.
 * Called by CLEAR statement.
 */
void array_clear_all(basic_state_t *state) {
    if (!state) return;
    /* Arrays are between array_start and string_start */
    /* Just reset array_start to var_start */
    /* Note: var_clear_all already does this */
}
