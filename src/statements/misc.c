/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * misc.c - Miscellaneous Statements
 *
 * Implements LET, DIM, DEF, POKE, CLEAR, NEW, RUN, and related statements.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include <string.h>

/*
 * Execute LET statement (assignment).
 * Note: LET is optional in 8K BASIC syntax.
 */
basic_error_t stmt_let_numeric(basic_state_t *state, const char *var_name, mbf_t value) {
    if (!state || !var_name) return ERR_FC;

    if (!var_set_numeric(state, var_name, value)) {
        return ERR_OM;
    }

    return ERR_NONE;
}

/*
 * LET for string variables.
 */
basic_error_t stmt_let_string(basic_state_t *state, const char *var_name, string_desc_t value) {
    if (!state || !var_name) return ERR_FC;

    if (!var_set_string(state, var_name, value)) {
        return ERR_OM;
    }

    return ERR_NONE;
}

/*
 * LET for array element (numeric).
 */
basic_error_t stmt_let_array_numeric(basic_state_t *state, const char *arr_name,
                                      int idx1, int idx2, mbf_t value) {
    if (!state || !arr_name) return ERR_FC;

    if (!array_set_numeric(state, arr_name, idx1, idx2, value)) {
        return ERR_BS;  /* Bad subscript or out of memory */
    }

    return ERR_NONE;
}

/*
 * LET for array element (string).
 */
basic_error_t stmt_let_array_string(basic_state_t *state, const char *arr_name,
                                     int idx1, int idx2, string_desc_t value) {
    if (!state || !arr_name) return ERR_FC;

    if (!array_set_string(state, arr_name, idx1, idx2, value)) {
        return ERR_BS;  /* Bad subscript or out of memory */
    }

    return ERR_NONE;
}

/*
 * Execute DIM statement.
 * Creates arrays with specified dimensions.
 */
basic_error_t stmt_dim(basic_state_t *state, const char *arr_name, int dim1, int dim2) {
    if (!state || !arr_name) return ERR_FC;

    /* Check if array already exists */
    if (array_find(state, arr_name)) {
        return ERR_DD;  /* Double dimension */
    }

    /* Create the array */
    if (!array_create(state, arr_name, dim1, dim2)) {
        return ERR_OM;  /* Out of memory */
    }

    return ERR_NONE;
}

/*
 * Execute DEF FN statement.
 * Defines a user function.
 */
basic_error_t stmt_def_fn(basic_state_t *state, char fn_name,
                          uint16_t line, uint16_t ptr) {
    if (!state) return ERR_FC;

    /* Function name must be A-Z */
    fn_name = (char)(fn_name & 0x1F);  /* Extract letter index */
    if (fn_name < 0 || fn_name > 25) {
        return ERR_SN;
    }

    /* Store the definition location */
    state->user_funcs[(int)fn_name].name = fn_name + 'A';
    state->user_funcs[(int)fn_name].line = line;
    state->user_funcs[(int)fn_name].ptr = ptr;

    return ERR_NONE;
}

/*
 * Look up a user function.
 * Returns the definition location, or error if undefined.
 */
basic_error_t stmt_fn_lookup(basic_state_t *state, char fn_name,
                             uint16_t *line, uint16_t *ptr) {
    if (!state || !line || !ptr) return ERR_FC;

    fn_name = (char)(fn_name & 0x1F);
    if (fn_name < 0 || fn_name > 25) {
        return ERR_SN;
    }

    if (state->user_funcs[(int)fn_name].line == 0) {
        return ERR_UF;  /* Undefined function */
    }

    *line = state->user_funcs[(int)fn_name].line;
    *ptr = state->user_funcs[(int)fn_name].ptr;
    return ERR_NONE;
}

/*
 * Execute POKE statement.
 * Writes a byte to memory address.
 */
basic_error_t stmt_poke(basic_state_t *state, uint16_t address, uint8_t value) {
    if (!state) return ERR_FC;

    /* Address must be within our memory space */
    if (address >= state->memory_size) {
        return ERR_FC;
    }

    state->memory[address] = value;
    return ERR_NONE;
}

/*
 * PEEK function.
 * Reads a byte from memory address.
 */
uint8_t stmt_peek(basic_state_t *state, uint16_t address) {
    if (!state || address >= state->memory_size) {
        return 0;
    }

    return state->memory[address];
}

/*
 * Execute CLEAR statement.
 * Clears all variables and optionally sets string space size.
 */
basic_error_t stmt_clear(basic_state_t *state, int string_space) {
    if (!state) return ERR_FC;

    /* Reset variable area */
    var_clear_all(state);

    /* Reset string space */
    string_clear(state);

    /* Clear control stacks */
    state->for_sp = 0;
    state->gosub_sp = 0;

    /* Clear user functions */
    memset(state->user_funcs, 0, sizeof(state->user_funcs));

    /* Reset DATA pointer */
    state->data_line = 0;
    state->data_ptr = 0;

    /* Can't continue after CLEAR */
    state->can_continue = false;

    /* If string_space specified, adjust memory */
    if (string_space > 0) {
        uint16_t new_start = state->string_end - (uint16_t)string_space;
        if (new_start <= state->program_end) {
            return ERR_OM;
        }
        state->string_start = state->string_end;  /* Reset to top */
    }

    return ERR_NONE;
}

/*
 * Execute NEW statement.
 * Clears program and all data.
 */
basic_error_t stmt_new(basic_state_t *state) {
    if (!state) return ERR_FC;

    basic_reset(state);
    return ERR_NONE;
}

/*
 * Execute RUN statement.
 * Starts program execution from beginning or specified line.
 */
basic_error_t stmt_run(basic_state_t *state, uint16_t start_line) {
    if (!state) return ERR_FC;

    /* Clear variables and stacks */
    stmt_clear(state, 0);

    /* Set execution position */
    if (start_line == 0) {
        /* Start from beginning */
        state->current_line = 0;
        state->text_ptr = state->program_start + 4;  /* Skip first link and line number */
    } else {
        /* Start from specified line */
        uint8_t *line = NULL;
        uint8_t *ptr = state->memory + state->program_start;
        uint8_t *end = state->memory + state->program_end;

        while (ptr < end) {
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

            if (num >= start_line) {
                line = ptr;
                break;
            }

            if (link == 0) break;
            ptr = state->memory + link;
        }

        if (!line) {
            return ERR_UL;  /* Undefined line */
        }

        state->current_line = (uint16_t)(line[2] | (line[3] << 8));
        state->text_ptr = (uint16_t)(line - state->memory) + 4;
    }

    state->running = true;
    return ERR_NONE;
}

/*
 * Execute REM statement.
 * Does nothing - just a comment.
 */
basic_error_t stmt_rem(void) {
    return ERR_NONE;
}

/*
 * Execute SWAP statement.
 * Swaps values of two variables.
 */
basic_error_t stmt_swap_numeric(basic_state_t *state,
                                const char *var1, const char *var2) {
    if (!state || !var1 || !var2) return ERR_FC;

    mbf_t val1 = var_get_numeric(state, var1);
    mbf_t val2 = var_get_numeric(state, var2);

    if (!var_set_numeric(state, var1, val2)) return ERR_OM;
    if (!var_set_numeric(state, var2, val1)) return ERR_OM;

    return ERR_NONE;
}

/*
 * Execute SWAP statement for strings.
 */
basic_error_t stmt_swap_string(basic_state_t *state,
                               const char *var1, const char *var2) {
    if (!state || !var1 || !var2) return ERR_FC;

    string_desc_t val1 = var_get_string(state, var1);
    string_desc_t val2 = var_get_string(state, var2);

    if (!var_set_string(state, var1, val2)) return ERR_OM;
    if (!var_set_string(state, var2, val1)) return ERR_OM;

    return ERR_NONE;
}

/*
 * INP function - read from I/O port.
 * Not implemented in this version - returns 0 with warning.
 */
uint8_t stmt_inp(basic_state_t *state, uint8_t port) {
    (void)port;

    if (state && !state->warned_inp) {
        fprintf(stderr, "Warning: INP not supported in this version\n");
        state->warned_inp = true;
    }

    return 0;
}

/*
 * OUT statement - write to I/O port.
 * Not implemented in this version - prints warning.
 */
basic_error_t stmt_out(basic_state_t *state, uint8_t port, uint8_t value) {
    (void)port;
    (void)value;

    if (state && !state->warned_out) {
        fprintf(stderr, "Warning: OUT not supported in this version\n");
        state->warned_out = true;
    }

    return ERR_NONE;
}

/*
 * WAIT statement - wait for I/O port condition.
 * Not implemented in this version - returns immediately with warning.
 */
basic_error_t stmt_wait(basic_state_t *state, uint8_t port, uint8_t mask, uint8_t xor_val) {
    (void)port;
    (void)mask;
    (void)xor_val;

    if (state && !state->warned_wait) {
        fprintf(stderr, "Warning: WAIT not supported in this version\n");
        state->warned_wait = true;
    }

    return ERR_NONE;
}

/*
 * USR function - call machine language routine.
 * Not implemented in this version - returns 0 with warning.
 */
mbf_t stmt_usr(basic_state_t *state, mbf_t arg) {
    (void)arg;

    if (state && !state->warned_usr) {
        fprintf(stderr, "Warning: USR not supported in this version\n");
        state->warned_usr = true;
    }

    return MBF_ZERO;
}

/*
 * FRE function - return free memory.
 */
int32_t stmt_fre(basic_state_t *state) {
    if (!state) return 0;

    return (int32_t)(state->string_start - state->array_start);
}

/*
 * Execute RANDOMIZE statement.
 * Reseeds the random number generator.
 */
basic_error_t stmt_randomize(basic_state_t *state, mbf_t seed) {
    if (!state) return ERR_FC;

    if (mbf_is_zero(seed)) {
        /* Seed from time/counter */
        rnd_reseed(&state->rnd);
    } else {
        /* Use provided seed */
        state->rnd.last_value = seed;
    }

    return ERR_NONE;
}
