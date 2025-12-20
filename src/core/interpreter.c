/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * interpreter.c - Main BASIC interpreter
 *
 * TODO: Implement the main interpreter loop and command handling.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include <stdlib.h>
#include <string.h>

/* Error state */
error_context_t g_last_error = {0};

/*
 * Error code strings.
 */
const char ERROR_CODES[][3] = {
    "??",  /* ERR_NONE */
    "NF",  /* ERR_NF - NEXT without FOR */
    "SN",  /* ERR_SN - Syntax error */
    "RG",  /* ERR_RG - RETURN without GOSUB */
    "OD",  /* ERR_OD - Out of DATA */
    "FC",  /* ERR_FC - Function Call error */
    "OV",  /* ERR_OV - Overflow */
    "OM",  /* ERR_OM - Out of Memory */
    "UL",  /* ERR_UL - Undefined Line */
    "BS",  /* ERR_BS - Bad Subscript */
    "DD",  /* ERR_DD - Double Dimension */
    "/0",  /* ERR_DZ - Division by Zero */
    "ID",  /* ERR_ID - Illegal Direct */
    "TM",  /* ERR_TM - Type Mismatch */
    "OS",  /* ERR_OS - Out of String space */
    "LS",  /* ERR_LS - String too Long */
    "ST",  /* ERR_ST - String formula Too complex */
    "CN",  /* ERR_CN - Can't coNtinue */
    "UF",  /* ERR_UF - Undefined user Function */
    "MO",  /* ERR_MO - Missing Operand */
};

/*
 * Get error code string.
 */
const char *error_code_string(basic_error_t err) {
    if (err >= ERR_COUNT) return "??";
    return ERROR_CODES[err];
}

/*
 * Raise an error.
 */
void basic_error(basic_error_t code) {
    g_last_error.code = code;
    g_last_error.line_number = 0xFFFF;
    g_last_error.position = 0;
}

/*
 * Raise an error at a specific line.
 */
void basic_error_at_line(basic_error_t code, uint16_t line) {
    g_last_error.code = code;
    g_last_error.line_number = line;
    g_last_error.position = 0;
}

/*
 * Clear the last error.
 */
void basic_clear_error(void) {
    g_last_error.code = ERR_NONE;
}

/*
 * Initialize interpreter state.
 */
basic_state_t *basic_init(const basic_config_t *config) {
    basic_state_t *state = calloc(1, sizeof(basic_state_t));
    if (!state) return NULL;

    /* Allocate memory */
    uint32_t mem_size = config ? config->memory_size : BASIC8K_DEFAULT_MEMORY;
    if (mem_size < BASIC8K_MIN_MEMORY) mem_size = BASIC8K_MIN_MEMORY;
    if (mem_size > BASIC8K_MAX_MEMORY) mem_size = BASIC8K_MAX_MEMORY;

    state->memory = calloc(1, mem_size);
    if (!state->memory) {
        free(state);
        return NULL;
    }
    state->memory_size = mem_size;

    /* Set up I/O */
    state->input = config && config->input ? config->input : stdin;
    state->output = config && config->output ? config->output : stdout;

    /* Terminal settings */
    state->terminal_width = config ? config->terminal_width : BASIC8K_DEFAULT_WIDTH;
    state->want_trig = config ? config->want_trig : true;

    /* Initialize RND */
    rnd_init(&state->rnd);

    /* Set up memory regions */
    state->program_start = 0;
    state->program_end = 0;
    state->var_start = 0;
    state->array_start = 0;
    state->string_end = (uint16_t)mem_size;
    state->string_start = state->string_end;

    return state;
}

/*
 * Free interpreter state.
 */
void basic_free(basic_state_t *state) {
    if (state) {
        free(state->memory);
        free(state);
    }
}

/*
 * Reset interpreter.
 */
void basic_reset(basic_state_t *state) {
    if (!state) return;

    /* Clear program and variables */
    state->program_end = state->program_start;
    state->var_start = state->program_end;
    state->array_start = state->var_start;
    state->string_start = state->string_end;
    state->var_count_ = 0;

    /* Reset stacks */
    state->for_sp = 0;
    state->gosub_sp = 0;

    /* Reset execution state */
    state->running = false;
    state->can_continue = false;

    /* Reinitialize RND */
    rnd_init(&state->rnd);
}

/*
 * Get free memory.
 */
uint16_t basic_free_memory(basic_state_t *state) {
    if (!state) return 0;
    /* Free memory is between end of arrays and start of strings */
    if (state->string_start <= state->array_start) return 0;
    return state->string_start - state->array_start;
}

/*
 * Print startup banner.
 */
void basic_print_banner(basic_state_t *state) {
    if (!state || !state->output) return;
    fprintf(state->output, "\nALTAIR BASIC REV. 4.0\n");
    fprintf(state->output, "[8K VERSION]\n");
    fprintf(state->output, "COPYRIGHT 1976 BY MITS INC.\n\n");
    fprintf(state->output, "%u BYTES FREE\n\n", basic_free_memory(state));
}

/*
 * Print OK prompt.
 */
void basic_print_ok(basic_state_t *state) {
    if (!state || !state->output) return;
    fprintf(state->output, "OK\n");
}

/*
 * Print error message.
 */
void basic_print_error(basic_state_t *state, basic_error_t err, uint16_t line) {
    if (!state || !state->output) return;

    fprintf(state->output, "?%s ERROR", error_code_string(err));
    if (line != 0xFFFF) {
        fprintf(state->output, " IN %u", line);
    }
    fprintf(state->output, "\n");
}

/*
 * Run interactive interpreter.
 * TODO: Implement full interactive loop.
 */
void basic_run_interactive(basic_state_t *state) {
    if (!state) return;
    basic_print_banner(state);
    basic_print_ok(state);
    /* TODO: implement command loop */
}

/*
 * Execute a line.
 * TODO: Implement statement execution.
 */
bool basic_execute_line(basic_state_t *state, const char *line) {
    (void)state;
    (void)line;
    return true;
}

/*
 * Load program from file.
 * TODO: Implement file loading.
 */
bool basic_load_file(basic_state_t *state, const char *filename) {
    (void)state;
    (void)filename;
    return false;
}

/*
 * Save program to file.
 * TODO: Implement file saving.
 */
bool basic_save_file(basic_state_t *state, const char *filename) {
    (void)state;
    (void)filename;
    return false;
}

/*
 * Run program.
 * TODO: Implement program execution.
 */
void basic_run_program(basic_state_t *state) {
    (void)state;
}

/*
 * List program.
 * TODO: Implement program listing.
 */
void basic_list_program(basic_state_t *state, uint16_t start, uint16_t end) {
    (void)state;
    (void)start;
    (void)end;
}
