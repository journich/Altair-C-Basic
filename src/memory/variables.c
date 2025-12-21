/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * variables.c - Variable Storage
 *
 * Implements variable lookup and storage matching the original 8K BASIC.
 *
 * Variable format (6 bytes each):
 *   Bytes 0-1: Variable name (1-2 chars, $ suffix for strings stored in byte 1 bit 7)
 *   Bytes 2-5: Value (MBF for numeric, string descriptor for strings)
 *
 * Variables are stored sequentially after the program area.
 * Simple variables come first, then arrays.
 */

#include "basic/basic.h"
#include <string.h>
#include <ctype.h>

/* Size of a variable entry */
#define VAR_SIZE 6

/*
 * Encode a variable name into 2 bytes.
 * First char in byte 0, second char (or 0) in byte 1.
 * String variables have bit 7 set in byte 1.
 */
static void encode_var_name(const char *name, uint8_t *encoded) {
    encoded[0] = (uint8_t)toupper((unsigned char)name[0]);
    encoded[1] = 0;

    size_t len = strlen(name);
    bool is_string = (len > 0 && name[len - 1] == '$');

    if (is_string) {
        len--;  /* Exclude the $ from name processing */
    }

    if (len >= 2 && isalnum((unsigned char)name[1])) {
        encoded[1] = (uint8_t)toupper((unsigned char)name[1]);
    }

    if (is_string) {
        encoded[1] |= 0x80;  /* Set string flag */
    }
}

/*
 * Compare encoded variable name with a variable entry.
 */
static bool name_matches(const uint8_t *encoded, const uint8_t *entry) {
    return encoded[0] == entry[0] && encoded[1] == entry[1];
}

/*
 * Check if a variable name represents a string variable.
 */
bool var_is_string(const char *name) {
    size_t len = strlen(name);
    return len > 0 && name[len - 1] == '$';
}

/*
 * Find a variable by name.
 * Returns pointer to the variable entry, or NULL if not found.
 */
uint8_t *var_find(basic_state_t *state, const char *name) {
    if (!state || !name || !name[0]) return NULL;

    uint8_t encoded[2];
    encode_var_name(name, encoded);

    /* Scan through variable area only (not arrays) */
    /* Variables occupy var_count_ * VAR_SIZE bytes starting at var_start */
    uint8_t *ptr = state->memory + state->var_start;
    uint8_t *end = ptr + state->var_count_ * VAR_SIZE;

    while (ptr < end) {
        if (name_matches(encoded, ptr)) {
            return ptr;
        }
        ptr += VAR_SIZE;
    }

    return NULL;
}

/*
 * Create a new variable.
 * Returns pointer to the variable entry, or NULL if out of memory.
 */
uint8_t *var_create(basic_state_t *state, const char *name) {
    if (!state || !name || !name[0]) return NULL;

    /* Check if there's room */
    if (state->array_start + VAR_SIZE > state->string_start) {
        return NULL;  /* Out of memory */
    }

    /* Variables are inserted at var_start + var_count_ * 6 */
    /* If arrays exist, we need to make room by moving them up */
    uint16_t var_end = state->var_start + state->var_count_ * VAR_SIZE;

    /* Arrays are stored from var_end to array_start */
    /* Check if arrays exist (array_start > var_end means there are arrays) */
    if (state->array_start > var_end) {
        /* There are arrays - move them up by VAR_SIZE bytes */
        size_t array_bytes = state->array_start - var_end;
        memmove(state->memory + var_end + VAR_SIZE,
                state->memory + var_end,
                array_bytes);
    }

    /* Add variable at end of variable area */
    uint8_t *ptr = state->memory + var_end;

    /* Encode name */
    encode_var_name(name, ptr);

    /* Initialize value to zero */
    memset(ptr + 2, 0, 4);

    /* Update pointers */
    state->var_count_++;
    state->array_start += VAR_SIZE;

    return ptr;
}

/*
 * Find or create a variable.
 * Returns pointer to the variable entry, or NULL if out of memory.
 */
uint8_t *var_get_or_create(basic_state_t *state, const char *name) {
    uint8_t *var = var_find(state, name);
    if (var) return var;
    return var_create(state, name);
}

/*
 * Get numeric value of a variable.
 */
mbf_t var_get_numeric(basic_state_t *state, const char *name) {
    uint8_t *var = var_find(state, name);
    if (!var) return MBF_ZERO;

    /* Check it's not a string variable */
    if (var[1] & 0x80) return MBF_ZERO;

    /* Extract MBF value from bytes 2-5 */
    mbf_t result;
    memcpy(&result.raw, var + 2, 4);
    return result;
}

/*
 * Set numeric value of a variable.
 * Creates the variable if it doesn't exist.
 */
bool var_set_numeric(basic_state_t *state, const char *name, mbf_t value) {
    if (var_is_string(name)) return false;

    uint8_t *var = var_get_or_create(state, name);
    if (!var) return false;

    /* Store MBF value in bytes 2-5 */
    memcpy(var + 2, &value.raw, 4);
    return true;
}

/*
 * Get string descriptor from a string variable.
 */
string_desc_t var_get_string(basic_state_t *state, const char *name) {
    string_desc_t empty = {0, 0, 0};

    uint8_t *var = var_find(state, name);
    if (!var) return empty;

    /* Check it's a string variable */
    if (!(var[1] & 0x80)) return empty;

    /* Extract string descriptor from bytes 2-5 */
    string_desc_t result;
    result.length = var[2];
    result._reserved = var[3];
    result.ptr = (uint16_t)(var[4] | (var[5] << 8));
    return result;
}

/*
 * Set string descriptor for a string variable.
 */
bool var_set_string(basic_state_t *state, const char *name, string_desc_t desc) {
    if (!var_is_string(name)) return false;

    uint8_t *var = var_get_or_create(state, name);
    if (!var) return false;

    /* Store string descriptor in bytes 2-5 */
    var[2] = desc.length;
    var[3] = desc._reserved;
    var[4] = (uint8_t)(desc.ptr & 0xFF);
    var[5] = (uint8_t)(desc.ptr >> 8);
    return true;
}

/*
 * Clear all variables (but not arrays).
 * Called by CLEAR statement.
 */
void var_clear_all(basic_state_t *state) {
    if (!state) return;

    /* Reset variable area - arrays stay but are also cleared */
    state->var_start = state->program_end;
    state->array_start = state->var_start;
    state->var_count_ = 0;
}

/*
 * Get the number of defined variables.
 */
int var_count(basic_state_t *state) {
    if (!state) return 0;
    return (int)state->var_count_;
}
