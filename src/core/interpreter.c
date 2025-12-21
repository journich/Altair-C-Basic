/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * interpreter.c - Main BASIC interpreter
 *
 * Implements the main command loop, statement execution, and program control.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include "basic/tokens.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

/* Error state */
error_context_t g_last_error = {0};

/* Ctrl-C interrupt handling */
static volatile sig_atomic_t g_interrupt_flag = 0;
static basic_state_t *g_interrupt_state = NULL;

static void interrupt_handler(int sig) {
    (void)sig;
    g_interrupt_flag = 1;
}

void basic_setup_interrupt(basic_state_t *state) {
    g_interrupt_state = state;
    g_interrupt_flag = 0;
    signal(SIGINT, interrupt_handler);
}

void basic_clear_interrupt(void) {
    g_interrupt_flag = 0;
    signal(SIGINT, SIG_DFL);
}

/*
 * Error code strings - exact match to original BASIC.
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
    /* Note: max addressable memory is 65535 (uint16_t max) */
    uint16_t max_addr = (mem_size > 65535) ? 65535 : (uint16_t)mem_size;
    state->program_start = 0;
    state->program_end = 0;
    state->var_start = 0;
    state->array_start = 0;
    state->string_end = max_addr;
    state->string_start = state->string_end;
    state->var_count_ = 0;

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

    /* Clear user functions */
    memset(state->user_funcs, 0, sizeof(state->user_funcs));

    /* Reinitialize RND */
    rnd_init(&state->rnd);

    /* Reset DATA pointer */
    state->data_line = 0;
    state->data_ptr = 0;
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
    if (line != 0xFFFF && line != 0) {
        fprintf(state->output, " IN %u", line);
    }
    fprintf(state->output, "\n");
}

/*
 * Parse a line number from the start of a string.
 * Returns the line number, or 0 if none found.
 * Updates *pos to point past the line number.
 */
static uint16_t parse_line_number(const char *line, size_t *pos) {
    size_t i = *pos;

    /* Skip leading whitespace */
    while (line[i] == ' ') i++;

    /* Check for digits */
    if (!isdigit((unsigned char)line[i])) {
        return 0;
    }

    /* Parse number */
    uint32_t num = 0;
    while (isdigit((unsigned char)line[i])) {
        num = num * 10 + (uint32_t)(line[i] - '0');
        i++;
        if (num > 65529) {
            /* Line number too large */
            return 0;
        }
    }

    *pos = i;
    return (uint16_t)num;
}

/*
 * Execute a direct statement (one without a line number).
 * Returns error code.
 */
static basic_error_t execute_statement(basic_state_t *state,
                                       const uint8_t *tokenized, size_t len);

/*
 * Execute a single line of BASIC (direct mode).
 * If the line has a line number, it's stored in the program.
 * Otherwise, it's executed immediately.
 */
bool basic_execute_line(basic_state_t *state, const char *line) {
    if (!state || !line) return false;

    /* Skip leading whitespace */
    size_t pos = 0;
    while (line[pos] == ' ') pos++;

    /* Check for empty line */
    if (line[pos] == '\0' || line[pos] == '\n' || line[pos] == '\r') {
        return true;
    }

    /* Check for line number */
    uint16_t line_num = parse_line_number(line, &pos);

    /* Tokenize the line */
    uint8_t tokenized[256];
    size_t tok_len = tokenize_line(line + pos, tokenized, sizeof(tokenized));

    if (line_num > 0) {
        /* Store in program */
        if (tok_len == 0 || (tok_len == 1 && tokenized[0] == '\0')) {
            /* Empty line - delete it */
            program_insert_line(state, line_num, NULL, 0);
        } else {
            if (!program_insert_line(state, line_num, tokenized, tok_len)) {
                basic_print_error(state, ERR_OM, 0xFFFF);
                return false;
            }
        }
        return true;
    }

    /* Direct execution */
    state->current_line = 0xFFFF;  /* Direct mode */

    basic_error_t err = execute_statement(state, tokenized, tok_len);
    if (err != ERR_NONE) {
        basic_print_error(state, err, 0xFFFF);
        return false;
    }

    return true;
}

/*
 * Execute a statement from tokenized text.
 */
static basic_error_t execute_statement(basic_state_t *state,
                                       const uint8_t *tokenized, size_t len) {
    if (!state || !tokenized || len == 0) return ERR_NONE;

    size_t pos = 0;

    /* Skip leading whitespace */
    while (pos < len && tokenized[pos] == ' ') pos++;

    if (pos >= len || tokenized[pos] == '\0') {
        return ERR_NONE;
    }

    uint8_t cmd = tokenized[pos];

    /* Handle statements */
    switch (cmd) {
        case TOK_REM:
            /* Comment - skip rest of line */
            return ERR_NONE;

        case TOK_PRINT:
        case '?': {
            /* PRINT statement */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            bool need_newline = true;

            while (pos < len && tokenized[pos] != '\0' && tokenized[pos] != ':') {
                uint8_t ch = tokenized[pos];

                if (ch == '"') {
                    /* String expression starting with literal - use eval_string_desc */
                    basic_error_t err;
                    size_t consumed;
                    string_desc_t desc = eval_string_desc(state, tokenized + pos,
                                                          len - pos, &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;

                    /* Print the string */
                    if (desc.length > 0 && desc.ptr > 0) {
                        const char *str = (const char *)(state->memory + desc.ptr);
                        for (uint8_t i = 0; i < desc.length; i++) {
                            io_putchar(state, str[i]);
                        }
                    }
                    need_newline = true;
                } else if (ch == ';') {
                    /* Semicolon - no space */
                    pos++;
                    need_newline = false;
                } else if (ch == ',') {
                    /* Comma - tab to next zone */
                    int col = state->terminal_x;
                    int next_zone = ((col / 14) + 1) * 14;
                    while (state->terminal_x < next_zone) {
                        io_putchar(state, ' ');
                    }
                    pos++;
                    need_newline = false;
                } else if (ch == TOK_TAB) {
                    /* TAB function - token includes the '(' */
                    pos++;
                    basic_error_t err;
                    size_t consumed;
                    mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                                &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;
                    if (pos < len && tokenized[pos] == ')') pos++;

                    bool overflow;
                    int col = mbf_to_int16(val, &overflow);
                    if (!overflow && col >= 1) {
                        io_tab(state, col);
                    }
                    need_newline = false;  /* TAB doesn't imply newline */
                } else if (ch == TOK_SPC) {
                    /* SPC function - token includes the '(' */
                    pos++;
                    basic_error_t err;
                    size_t consumed;
                    mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                                &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;
                    if (pos < len && tokenized[pos] == ')') pos++;

                    bool overflow;
                    int count = mbf_to_int16(val, &overflow);
                    if (!overflow && count >= 0) {
                        io_spc(state, count);
                    }
                    need_newline = false;  /* SPC doesn't imply newline */
                } else if (ch == ' ') {
                    pos++;
                } else if (isalpha(ch)) {
                    /* Check for string variable or function */
                    size_t save_pos = pos;
                    char var_name[4] = {0};  /* Room for 2 chars + $ + null */
                    size_t name_len = 0;
                    var_name[name_len++] = (char)tokenized[pos++];
                    if (pos < len && isalnum(tokenized[pos])) {
                        var_name[name_len++] = (char)tokenized[pos++];
                    }

                    if (pos < len && tokenized[pos] == '$') {
                        /* String expression - use eval_string_desc to handle concatenation */
                        pos = save_pos;  /* Restore to start of expression */
                        basic_error_t err;
                        size_t consumed;
                        string_desc_t desc = eval_string_desc(state, tokenized + pos,
                                                              len - pos, &consumed, &err);
                        if (err != ERR_NONE) return err;
                        pos += consumed;

                        /* Print the string */
                        if (desc.length > 0 && desc.ptr > 0) {
                            const char *str = (const char *)(state->memory + desc.ptr);
                            for (uint8_t i = 0; i < desc.length; i++) {
                                io_putchar(state, str[i]);
                            }
                        }
                        need_newline = true;
                    } else {
                        /* Numeric variable or expression - restore and evaluate */
                        pos = save_pos;
                        basic_error_t err;
                        size_t consumed;
                        mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                                    &consumed, &err);
                        if (err != ERR_NONE) return err;
                        pos += consumed;

                        io_print_number(state, val);
                        need_newline = true;
                    }
                } else if (TOK_IS_STRING_FUNC(ch)) {
                    /* String function */
                    basic_error_t err;
                    size_t consumed;
                    string_desc_t desc = eval_string_desc(state, tokenized + pos,
                                                          len - pos, &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;

                    /* Print the string */
                    if (desc.length > 0 && desc.ptr > 0) {
                        const char *str = (const char *)(state->memory + desc.ptr);
                        for (uint8_t i = 0; i < desc.length; i++) {
                            io_putchar(state, str[i]);
                        }
                    }
                    need_newline = true;
                } else {
                    /* Numeric expression */
                    basic_error_t err;
                    size_t consumed;
                    mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                                &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;

                    io_print_number(state, val);
                    need_newline = true;
                }
            }

            if (need_newline) {
                io_newline(state);
            }
            return ERR_NONE;
        }

        case TOK_LIST: {
            /* LIST statement */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            uint16_t start = 0, end = 0;

            if (pos < len && isdigit(tokenized[pos])) {
                /* Parse start line */
                while (pos < len && isdigit(tokenized[pos])) {
                    start = start * 10 + (uint16_t)(tokenized[pos] - '0');
                    pos++;
                }
            }

            if (pos < len && tokenized[pos] == '-') {
                pos++;
                if (pos < len && isdigit(tokenized[pos])) {
                    while (pos < len && isdigit(tokenized[pos])) {
                        end = end * 10 + (uint16_t)(tokenized[pos] - '0');
                        pos++;
                    }
                } else {
                    end = 0xFFFF;
                }
            } else if (start > 0) {
                end = start;
            }

            basic_list_program(state, start, end);
            return ERR_NONE;
        }

        case TOK_RUN: {
            /* RUN statement */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            uint16_t start_line = 0;
            if (pos < len && isdigit(tokenized[pos])) {
                while (pos < len && isdigit(tokenized[pos])) {
                    start_line = start_line * 10 + (uint16_t)(tokenized[pos] - '0');
                    pos++;
                }
            }

            basic_error_t err = stmt_run(state, start_line);
            if (err != ERR_NONE) return err;

            /* Execute the program */
            basic_run_program(state);
            return ERR_NONE;
        }

        case TOK_NEW:
            stmt_new(state);
            return ERR_NONE;

        case TOK_CLOAD: {
            /* CLOAD "filename" - Load program from file */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            /* Optional filename in quotes */
            char filename[256] = "";
            if (pos < len && tokenized[pos] == '"') {
                pos++;  /* Skip opening quote */
                size_t fname_len = 0;
                while (pos < len && tokenized[pos] != '"' && fname_len < sizeof(filename) - 1) {
                    filename[fname_len++] = (char)tokenized[pos++];
                }
                filename[fname_len] = '\0';
                if (pos < len && tokenized[pos] == '"') pos++;  /* Skip closing quote */
            }

            if (filename[0] == '\0') {
                return ERR_FC;  /* Function call error - no filename */
            }

            /* Open and read the file */
            FILE *fp = fopen(filename, "r");
            if (!fp) {
                io_print_cstring(state, "?FILE NOT FOUND\r\n");
                return ERR_NONE;
            }

            /* Clear existing program */
            stmt_new(state);

            /* Read each line and store it */
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                /* Remove trailing newline/CR */
                size_t linelen = strlen(line);
                while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) {
                    line[--linelen] = '\0';
                }

                if (linelen > 0) {
                    if (!basic_execute_line(state, line)) {
                        fclose(fp);
                        return ERR_SN;
                    }
                }
            }

            fclose(fp);
            io_print_cstring(state, "OK\r\n");
            return ERR_NONE;
        }

        case TOK_CSAVE: {
            /* CSAVE "filename" - Save program to file */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            /* Filename in quotes */
            char filename[256] = "";
            if (pos < len && tokenized[pos] == '"') {
                pos++;  /* Skip opening quote */
                size_t fname_len = 0;
                while (pos < len && tokenized[pos] != '"' && fname_len < sizeof(filename) - 1) {
                    filename[fname_len++] = (char)tokenized[pos++];
                }
                filename[fname_len] = '\0';
                if (pos < len && tokenized[pos] == '"') pos++;  /* Skip closing quote */
            }

            if (filename[0] == '\0') {
                return ERR_FC;  /* Function call error - no filename */
            }

            /* Open file for writing */
            FILE *fp = fopen(filename, "w");
            if (!fp) {
                io_print_cstring(state, "?FILE ERROR\r\n");
                return ERR_NONE;
            }

            /* Iterate through program and write lines */
            uint8_t *ptr = state->memory + state->program_start;
            uint8_t *end = state->memory + state->program_end;

            while (ptr < end) {
                uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
                uint16_t line_num = (uint16_t)(ptr[2] | (ptr[3] << 8));

                if (link == 0) break;

                /* Detokenize the line */
                uint8_t *line_start = ptr + 4;
                uint8_t *line_end = state->memory + link;
                size_t line_len = (size_t)(line_end - line_start - 1);  /* -1 for null */

                char detok_buf[512];
                size_t detok_len = detokenize_line(line_start, line_len, detok_buf, sizeof(detok_buf));

                fprintf(fp, "%d %.*s\n", line_num, (int)detok_len, detok_buf);

                ptr = state->memory + link;
            }

            fclose(fp);
            io_print_cstring(state, "OK\r\n");
            return ERR_NONE;
        }

        case TOK_CLEAR: {
            pos++;
            int string_space = 0;
            if (pos < len && isdigit(tokenized[pos])) {
                while (pos < len && isdigit(tokenized[pos])) {
                    string_space = string_space * 10 + (tokenized[pos] - '0');
                    pos++;
                }
            }
            return stmt_clear(state, string_space);
        }

        case TOK_CONT: {
            basic_error_t err = stmt_cont(state);
            if (err != ERR_NONE) return err;
            /* Resume program execution */
            basic_run_program(state);
            return ERR_NONE;
        }

        case TOK_END:
            return stmt_end(state);

        case TOK_STOP:
            return stmt_stop(state, state->current_line, state->text_ptr);

        case TOK_RESTORE: {
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            if (pos < len && isdigit(tokenized[pos])) {
                uint16_t line_num = 0;
                while (pos < len && isdigit(tokenized[pos])) {
                    line_num = line_num * 10 + (uint16_t)(tokenized[pos] - '0');
                    pos++;
                }
                return stmt_restore_line(state, line_num);
            }
            return stmt_restore(state);
        }

        case TOK_GOTO: {
            pos++;
            basic_error_t err;
            size_t consumed;
            mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                        &consumed, &err);
            if (err != ERR_NONE) return err;

            bool overflow;
            int16_t line_num = mbf_to_int16(val, &overflow);
            if (overflow || line_num < 0) return ERR_UL;

            return stmt_goto(state, (uint16_t)line_num);
        }

        case TOK_GOSUB: {
            pos++;
            basic_error_t err;
            size_t consumed;
            mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                        &consumed, &err);
            if (err != ERR_NONE) return err;

            bool overflow;
            int16_t line_num = mbf_to_int16(val, &overflow);
            if (overflow || line_num < 0) return ERR_UL;

            /* Calculate return position (after this statement) */
            uint16_t return_ptr;
            const uint8_t *stmt_ptr = state->memory + state->text_ptr;
            while (*stmt_ptr != '\0' && *stmt_ptr != ':') {
                stmt_ptr++;
            }

            if (*stmt_ptr == ':') {
                /* More statements on this line - return to next statement */
                return_ptr = (uint16_t)(stmt_ptr + 1 - state->memory);
            } else {
                /* End of line - find next line */
                uint8_t *line_ptr = state->memory + state->program_start;
                uint8_t *end_ptr = state->memory + state->program_end;
                return_ptr = state->text_ptr;  /* Default if not found */

                while (line_ptr < end_ptr) {
                    uint16_t link = (uint16_t)(line_ptr[0] | (line_ptr[1] << 8));
                    uint8_t *line_text = line_ptr + 4;
                    uint8_t *line_end = (link > 0) ? state->memory + link : end_ptr;

                    if (state->text_ptr >= (uint16_t)(line_text - state->memory) &&
                        state->text_ptr < (uint16_t)(line_end - state->memory)) {
                        if (link > 0) {
                            return_ptr = link + 4;
                        } else {
                            return_ptr = state->program_end;
                        }
                        break;
                    }

                    if (link == 0) break;
                    line_ptr = state->memory + link;
                }
            }

            return stmt_gosub(state, (uint16_t)line_num,
                              state->current_line, return_ptr);
        }

        case TOK_RETURN:
            return stmt_return(state);

        case TOK_FOR: {
            /* FOR var = init TO limit [STEP step] */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            /* Get variable name */
            char var_name[3] = {0};
            if (pos < len && isalpha(tokenized[pos])) {
                var_name[0] = (char)tokenized[pos++];
                if (pos < len && isalnum(tokenized[pos])) {
                    var_name[1] = (char)tokenized[pos++];
                }
            } else {
                return ERR_SN;
            }

            /* Expect = */
            while (pos < len && tokenized[pos] == ' ') pos++;
            if (pos >= len || tokenized[pos] != TOK_EQ) {
                return ERR_SN;
            }
            pos++;

            /* Parse initial value */
            basic_error_t err;
            size_t consumed;
            mbf_t initial = eval_expression(state, tokenized + pos, len - pos,
                                            &consumed, &err);
            if (err != ERR_NONE) return err;
            pos += consumed;

            /* Expect TO */
            while (pos < len && tokenized[pos] == ' ') pos++;
            if (pos >= len || tokenized[pos] != TOK_TO) {
                return ERR_SN;
            }
            pos++;

            /* Parse limit */
            mbf_t limit = eval_expression(state, tokenized + pos, len - pos,
                                          &consumed, &err);
            if (err != ERR_NONE) return err;
            pos += consumed;

            /* Check for STEP */
            mbf_t step = MBF_ONE;
            while (pos < len && tokenized[pos] == ' ') pos++;
            if (pos < len && tokenized[pos] == TOK_STEP) {
                pos++;
                step = eval_expression(state, tokenized + pos, len - pos,
                                       &consumed, &err);
                if (err != ERR_NONE) return err;
                pos += consumed;
            }

            /* The loop body starts after this statement */
            /* We need to find the position after the current statement */
            uint16_t next_line = state->current_line;
            uint16_t next_ptr = state->text_ptr;

            /* Check if there are more statements on this line */
            const uint8_t *stmt_ptr = state->memory + state->text_ptr;
            while (*stmt_ptr != '\0' && *stmt_ptr != ':') {
                stmt_ptr++;
            }

            if (*stmt_ptr == ':') {
                /* More statements on this line - return to next statement */
                next_ptr = (uint16_t)(stmt_ptr + 1 - state->memory);
            } else {
                /* End of line - find next line */
                /* Go back to find line start (link bytes) */
                uint8_t *line_ptr = state->memory + state->program_start;
                uint8_t *end_ptr = state->memory + state->program_end;

                while (line_ptr < end_ptr) {
                    uint16_t link = (uint16_t)(line_ptr[0] | (line_ptr[1] << 8));
                    uint16_t line_num = (uint16_t)(line_ptr[2] | (line_ptr[3] << 8));
                    uint8_t *line_text = line_ptr + 4;
                    uint8_t *line_end = (link > 0) ? state->memory + link : end_ptr;

                    if (state->text_ptr >= (uint16_t)(line_text - state->memory) &&
                        state->text_ptr < (uint16_t)(line_end - state->memory)) {
                        /* Found current line - get next line's text start */
                        if (link > 0) {
                            next_line = (uint16_t)(state->memory[link + 2] |
                                                   (state->memory[link + 3] << 8));
                            next_ptr = link + 4;
                        } else {
                            /* No next line - shouldn't happen in valid FOR loop */
                            next_ptr = state->text_ptr;
                        }
                        break;
                    }
                    (void)line_num;

                    if (link == 0) break;
                    line_ptr = state->memory + link;
                }
            }

            return stmt_for(state, var_name, initial, limit, step, next_line, next_ptr);
        }

        case TOK_NEXT: {
            /* NEXT [var [, var ...]] - handles comma-separated variables */
            pos++;

            do {
                while (pos < len && tokenized[pos] == ' ') pos++;

                char var_name[3] = {0};
                if (pos < len && isalpha(tokenized[pos])) {
                    var_name[0] = (char)tokenized[pos++];
                    if (pos < len && isalnum(tokenized[pos])) {
                        var_name[1] = (char)tokenized[pos++];
                    }
                }

                bool continue_loop;
                basic_error_t err = stmt_next(state, var_name, &continue_loop);
                if (err != ERR_NONE) return err;

                /* If this loop continues, stmt_next set text_ptr back to FOR */
                if (continue_loop) {
                    return ERR_NONE;
                }

                /* Loop finished, check for comma and another variable */
                while (pos < len && tokenized[pos] == ' ') pos++;
            } while (pos < len && tokenized[pos] == ',' && ++pos);

            /* All loops finished, continue to next statement */
            return ERR_NONE;
        }

        case TOK_IF: {
            /* IF expr THEN statements / line_num */
            pos++;

            /* Evaluate condition */
            basic_error_t err;
            size_t consumed;
            mbf_t condition = eval_expression(state, tokenized + pos, len - pos,
                                              &consumed, &err);
            if (err != ERR_NONE) return err;
            pos += consumed;

            /* Expect THEN */
            while (pos < len && tokenized[pos] == ' ') pos++;
            if (pos >= len || tokenized[pos] != TOK_THEN) {
                return ERR_SN;
            }
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            /* If condition is false, skip rest of line (not just THEN clause) */
            if (!stmt_if_eval(condition)) {
                /* Find end of line (null terminator) and set text_ptr there */
                /* This causes the main loop to advance to the next line */
                uint8_t *ptr = state->memory + state->text_ptr;
                while (*ptr != '\0') ptr++;
                state->text_ptr = (uint16_t)(ptr - state->memory);
                return ERR_NONE;
            }

            /* Condition is true - check if THEN is followed by line number */
            if (pos < len && isdigit(tokenized[pos])) {
                /* GOTO the line number */
                uint16_t line_num = 0;
                while (pos < len && isdigit(tokenized[pos])) {
                    line_num = line_num * 10 + (uint16_t)(tokenized[pos] - '0');
                    pos++;
                }
                return stmt_goto(state, line_num);
            }

            /* Execute ALL statements in the THEN clause (colon-separated) */
            while (pos < len && tokenized[pos] != '\0') {
                /* Find end of this statement (colon or end of line) */
                size_t stmt_len = 0;
                bool in_string = false;
                size_t scan = pos;
                while (scan < len && tokenized[scan] != '\0') {
                    if (tokenized[scan] == '"') {
                        in_string = !in_string;
                    } else if (tokenized[scan] == ':' && !in_string) {
                        break;
                    }
                    scan++;
                    stmt_len++;
                }

                /* Save text_ptr to detect flow control changes */
                uint16_t saved_ptr = state->text_ptr;

                /* Execute this statement */
                err = execute_statement(state, tokenized + pos, stmt_len);
                if (err != ERR_NONE) return err;

                /* If a flow control statement modified text_ptr, stop */
                /* (GOTO, GOSUB, NEXT continuing loop, etc.) */
                if (state->text_ptr != saved_ptr) {
                    return ERR_NONE;
                }

                /* Move past this statement */
                pos += stmt_len;

                /* Skip the colon if present */
                if (pos < len && tokenized[pos] == ':') {
                    pos++;
                }

                /* Skip whitespace before next statement */
                while (pos < len && tokenized[pos] == ' ') pos++;
            }
            return ERR_NONE;
        }

        case TOK_INPUT: {
            /* INPUT ["prompt";] var[,var...] */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            /* Check for prompt string */
            const char *prompt = "? ";
            if (pos < len && tokenized[pos] == '"') {
                pos++;
                while (pos < len && tokenized[pos] != '"') {
                    io_putchar(state, (char)tokenized[pos]);
                    pos++;
                }
                if (pos < len && tokenized[pos] == '"') pos++;
                if (pos < len && tokenized[pos] == ';') {
                    pos++;
                    prompt = "? ";
                } else if (pos < len && tokenized[pos] == ',') {
                    pos++;
                    prompt = "";  /* No question mark */
                }
            }

            /* Print prompt */
            io_print_cstring(state, prompt);

            /* Read input line */
            char input_buf[256];
            size_t input_len;
            if (!io_input_line(state, input_buf, sizeof(input_buf), &input_len)) {
                return ERR_NONE;  /* Ctrl-C pressed */
            }

            /* Parse multiple variables, each getting a value from input */
            const char *input_ptr = input_buf;

            while (pos < len && tokenized[pos] != ':' && tokenized[pos] != '\0') {
                while (pos < len && tokenized[pos] == ' ') pos++;

                if (!isalpha(tokenized[pos])) break;

                /* Parse variable name */
                char var_name[4] = {0};
                size_t name_len = 0;
                var_name[name_len++] = (char)tokenized[pos++];
                if (pos < len && isalnum(tokenized[pos])) {
                    var_name[name_len++] = (char)tokenized[pos++];
                }

                /* Check for string variable */
                if (pos < len && tokenized[pos] == '$') {
                    pos++;  /* Skip $ */
                    var_name[name_len] = '$';

                    /* Find end of this input value (comma or end) */
                    const char *end = input_ptr;
                    while (*end && *end != ',') end++;
                    size_t val_len = (size_t)(end - input_ptr);
                    if (val_len > 255) val_len = 255;

                    /* Create string from this portion of input */
                    string_desc_t desc = string_create_len(state, input_ptr, (uint8_t)val_len);
                    basic_error_t err = stmt_let_string(state, var_name, desc);
                    if (err != ERR_NONE) return err;

                    /* Advance input pointer past this value and comma */
                    input_ptr = end;
                    if (*input_ptr == ',') input_ptr++;
                } else {
                    /* Parse numeric value from input */
                    mbf_t value;
                    size_t consumed = io_parse_number(input_ptr, &value);
                    if (consumed == 0) {
                        value = MBF_ZERO;
                    }

                    basic_error_t err = stmt_let_numeric(state, var_name, value);
                    if (err != ERR_NONE) return err;

                    /* Advance input pointer past this value and comma */
                    input_ptr += consumed;
                    while (*input_ptr == ' ') input_ptr++;
                    if (*input_ptr == ',') input_ptr++;
                }

                /* Skip comma in token stream for next variable */
                while (pos < len && tokenized[pos] == ' ') pos++;
                if (pos < len && tokenized[pos] == ',') {
                    pos++;
                }
            }
            return ERR_NONE;
        }

        case TOK_READ: {
            /* READ var[,var...] */
            pos++;

            while (pos < len) {
                while (pos < len && tokenized[pos] == ' ') pos++;

                if (pos >= len || tokenized[pos] == ':' || tokenized[pos] == '\0') {
                    break;
                }

                if (isalpha(tokenized[pos])) {
                    char var_name[4] = {0};  /* Room for 2 chars + $ + null */
                    size_t name_len = 0;
                    var_name[name_len++] = (char)tokenized[pos++];
                    if (pos < len && isalnum(tokenized[pos])) {
                        var_name[name_len++] = (char)tokenized[pos++];
                    }

                    /* Check for string variable ($ comes before array subscript) */
                    bool is_string = false;
                    if (pos < len && tokenized[pos] == '$') {
                        is_string = true;
                        var_name[name_len] = '$';
                        pos++;
                    }

                    /* Check for array subscript */
                    int16_t idx1 = 0, idx2 = -1;
                    bool is_array = false;
                    if (pos < len && tokenized[pos] == '(') {
                        is_array = true;
                        pos++;  /* Skip ( */

                        basic_error_t err;
                        size_t consumed;
                        mbf_t idx1_val = eval_expression(state, tokenized + pos, len - pos,
                                                         &consumed, &err);
                        if (err != ERR_NONE) return err;
                        pos += consumed;

                        bool overflow;
                        idx1 = mbf_to_int16(idx1_val, &overflow);
                        if (overflow) return ERR_BS;

                        /* Check for second dimension */
                        if (pos < len && tokenized[pos] == ',') {
                            pos++;
                            mbf_t idx2_val = eval_expression(state, tokenized + pos, len - pos,
                                                             &consumed, &err);
                            if (err != ERR_NONE) return err;
                            pos += consumed;
                            idx2 = mbf_to_int16(idx2_val, &overflow);
                            if (overflow) return ERR_BS;
                        }

                        if (pos >= len || tokenized[pos] != ')') {
                            return ERR_SN;
                        }
                        pos++;  /* Skip ) */
                    }

                    /* Read value from DATA */
                    basic_error_t err;
                    if (is_string) {
                        string_desc_t value;
                        err = io_read_string(state, &value);
                        if (err != ERR_NONE) return err;
                        if (is_array) {
                            if (!array_set_string(state, var_name, idx1, idx2, value)) {
                                return ERR_BS;
                            }
                            err = ERR_NONE;
                        } else {
                            err = stmt_let_string(state, var_name, value);
                        }
                    } else {
                        mbf_t value;
                        err = io_read_numeric(state, &value);
                        if (err != ERR_NONE) return err;
                        if (is_array) {
                            if (!array_set_numeric(state, var_name, idx1, idx2, value)) {
                                return ERR_BS;
                            }
                            err = ERR_NONE;
                        } else {
                            err = stmt_let_numeric(state, var_name, value);
                        }
                    }
                    if (err != ERR_NONE) return err;
                }

                /* Skip comma between variables */
                while (pos < len && tokenized[pos] == ' ') pos++;
                if (pos < len && tokenized[pos] == ',') {
                    pos++;
                } else {
                    break;
                }
            }
            return ERR_NONE;
        }

        case TOK_DATA:
            /* DATA is skipped during execution - only used by READ */
            return ERR_NONE;

        case TOK_DIM: {
            /* DIM var(size)[,var(size)...] */
            pos++;

            while (pos < len) {
                while (pos < len && tokenized[pos] == ' ') pos++;

                if (pos >= len || tokenized[pos] == ':' || tokenized[pos] == '\0') {
                    break;
                }

                if (isalpha(tokenized[pos])) {
                    char var_name[4] = {0};  /* Room for 2 chars + $ + null */
                    size_t name_len = 0;
                    var_name[name_len++] = (char)tokenized[pos++];
                    if (pos < len && isalnum(tokenized[pos])) {
                        var_name[name_len++] = (char)tokenized[pos++];
                    }
                    /* Check for string array */
                    if (pos < len && tokenized[pos] == '$') {
                        var_name[name_len] = '$';
                        pos++;
                    }

                    /* Expect ( */
                    while (pos < len && tokenized[pos] == ' ') pos++;
                    if (pos >= len || tokenized[pos] != '(') {
                        return ERR_SN;
                    }
                    pos++;

                    /* Parse first dimension */
                    basic_error_t err;
                    size_t consumed;
                    mbf_t dim1_val = eval_expression(state, tokenized + pos, len - pos,
                                                     &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;

                    bool overflow;
                    int16_t dim1 = mbf_to_int16(dim1_val, &overflow);
                    if (overflow || dim1 < 0) return ERR_BS;

                    int16_t dim2 = 0;
                    while (pos < len && tokenized[pos] == ' ') pos++;
                    if (pos < len && tokenized[pos] == ',') {
                        pos++;
                        mbf_t dim2_val = eval_expression(state, tokenized + pos, len - pos,
                                                         &consumed, &err);
                        if (err != ERR_NONE) return err;
                        pos += consumed;
                        dim2 = mbf_to_int16(dim2_val, &overflow);
                        if (overflow || dim2 < 0) return ERR_BS;
                    }

                    /* Expect ) */
                    while (pos < len && tokenized[pos] == ' ') pos++;
                    if (pos >= len || tokenized[pos] != ')') {
                        return ERR_SN;
                    }
                    pos++;

                    /* Dimension the array */
                    /* Check if array already exists */
                    if (array_find(state, var_name)) {
                        return ERR_DD;  /* Double dimension */
                    }
                    uint8_t *arr = array_create(state, var_name, dim1,
                                                dim2 > 0 ? dim2 : -1);
                    if (!arr) return ERR_OM;  /* Out of memory */
                }

                /* Skip comma between dimensions */
                while (pos < len && tokenized[pos] == ' ') pos++;
                if (pos < len && tokenized[pos] == ',') {
                    pos++;
                } else {
                    break;
                }
            }
            return ERR_NONE;
        }

        case TOK_ON: {
            /* ON expr GOTO/GOSUB line[,line...] */
            pos++;

            /* Evaluate selector expression */
            basic_error_t err;
            size_t consumed;
            mbf_t selector = eval_expression(state, tokenized + pos, len - pos,
                                             &consumed, &err);
            if (err != ERR_NONE) return err;
            pos += consumed;

            bool overflow;
            int16_t value = mbf_to_int16(selector, &overflow);
            if (overflow) return ERR_FC;

            /* Check for GOTO or GOSUB */
            while (pos < len && tokenized[pos] == ' ') pos++;
            bool is_gosub = false;
            if (pos < len && tokenized[pos] == TOK_GOTO) {
                pos++;
            } else if (pos < len && tokenized[pos] == TOK_GOSUB) {
                pos++;
                is_gosub = true;
            } else {
                return ERR_SN;
            }

            /* Parse line numbers */
            uint16_t lines[16];
            int num_lines = 0;

            while (pos < len && num_lines < 16) {
                while (pos < len && tokenized[pos] == ' ') pos++;

                if (pos >= len || !isdigit(tokenized[pos])) break;

                uint16_t line_num = 0;
                while (pos < len && isdigit(tokenized[pos])) {
                    line_num = line_num * 10 + (uint16_t)(tokenized[pos] - '0');
                    pos++;
                }
                lines[num_lines++] = line_num;

                while (pos < len && tokenized[pos] == ' ') pos++;
                if (pos < len && tokenized[pos] == ',') {
                    pos++;
                } else {
                    break;
                }
            }

            if (is_gosub) {
                return stmt_on_gosub(state, value, lines, num_lines,
                                     state->current_line, state->text_ptr);
            } else {
                return stmt_on_goto(state, value, lines, num_lines);
            }
        }

        case TOK_DEF: {
            /* DEF FNx(y) = expr - define user function */
            pos++;
            while (pos < len && tokenized[pos] == ' ') pos++;

            /* Expect FN - either as token or as literal 'F' 'N' */
            if (pos >= len) return ERR_SN;

            char fn_name = 0;
            if (tokenized[pos] == TOK_FN) {
                /* FN was tokenized */
                pos++;
                if (pos >= len || !isalpha(tokenized[pos])) {
                    return ERR_SN;
                }
                fn_name = (char)toupper(tokenized[pos]);
                pos++;
            } else if (toupper(tokenized[pos]) == 'F' &&
                       pos + 1 < len && toupper(tokenized[pos + 1]) == 'N' &&
                       pos + 2 < len && isalpha(tokenized[pos + 2])) {
                /* FN was not tokenized - it's literal "FN" followed by function name */
                pos += 2;  /* Skip 'F' and 'N' */
                fn_name = (char)toupper(tokenized[pos]);
                pos++;
            } else {
                return ERR_SN;
            }

            /* Store pointer to the function definition (includes parameter and expr) */
            int fn_idx = fn_name - 'A';
            if (fn_idx < 0 || fn_idx >= 26) return ERR_SN;

            state->user_funcs[fn_idx].name = fn_name;
            state->user_funcs[fn_idx].line = state->current_line;
            /* Store pointer to opening paren of parameter */
            state->user_funcs[fn_idx].ptr = (uint16_t)(state->text_ptr + pos);

            /* Skip to end of line - don't evaluate the definition */
            return ERR_NONE;
        }

        case TOK_POKE: {
            pos++;
            basic_error_t err;
            size_t consumed;

            mbf_t addr_val = eval_expression(state, tokenized + pos, len - pos,
                                             &consumed, &err);
            if (err != ERR_NONE) return err;
            pos += consumed;

            if (pos >= len || tokenized[pos] != ',') return ERR_SN;
            pos++;

            mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                        &consumed, &err);
            if (err != ERR_NONE) return err;

            bool overflow1, overflow2;
            int16_t addr = mbf_to_int16(addr_val, &overflow1);
            int16_t value = mbf_to_int16(val, &overflow2);

            if (overflow1 || overflow2 || addr < 0 || value < 0 || value > 255) {
                return ERR_FC;
            }

            return stmt_poke(state, (uint16_t)addr, (uint8_t)value);
        }

        case TOK_NULL: {
            pos++;
            basic_error_t err;
            size_t consumed;

            mbf_t count_val = eval_expression(state, tokenized + pos, len - pos,
                                              &consumed, &err);
            if (err != ERR_NONE) return err;

            bool overflow;
            int16_t count = mbf_to_int16(count_val, &overflow);
            if (overflow) return ERR_FC;

            return stmt_null(state, count);
        }

        default:
            /* Check for variable assignment (LET is optional) */
            if (isalpha(cmd) || cmd == TOK_LET) {
                if (cmd == TOK_LET) pos++;

                /* Skip spaces */
                while (pos < len && tokenized[pos] == ' ') pos++;

                /* Get variable name */
                char var_name[4] = {0};  /* Room for 2 chars + $ + null */
                size_t name_len = 0;
                if (pos < len && isalpha(tokenized[pos])) {
                    var_name[name_len++] = (char)tokenized[pos++];
                    if (pos < len && isalnum(tokenized[pos])) {
                        var_name[name_len++] = (char)tokenized[pos++];
                    }
                } else {
                    return ERR_SN;
                }

                /* Check for string variable/array first ($ comes before () in BASIC) */
                int16_t idx1 = 0, idx2 = -1;
                bool is_array = false;
                bool is_string = false;

                if (pos < len && tokenized[pos] == '$') {
                    is_string = true;
                    var_name[name_len] = '$';  /* Append $ to var_name */
                    pos++;
                }

                /* Now check for array subscript */
                if (pos < len && tokenized[pos] == '(') {
                    /* Array element */
                    is_array = true;
                    pos++;  /* Skip ( */

                    /* Parse first index */
                    basic_error_t err;
                    size_t consumed;
                    mbf_t idx1_val = eval_expression(state, tokenized + pos, len - pos,
                                                     &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;

                    bool overflow;
                    idx1 = mbf_to_int16(idx1_val, &overflow);
                    if (overflow) return ERR_BS;

                    /* Check for second dimension */
                    if (pos < len && tokenized[pos] == ',') {
                        pos++;
                        mbf_t idx2_val = eval_expression(state, tokenized + pos, len - pos,
                                                         &consumed, &err);
                        if (err != ERR_NONE) return err;
                        pos += consumed;
                        idx2 = mbf_to_int16(idx2_val, &overflow);
                        if (overflow) return ERR_BS;
                    }

                    /* Expect ) */
                    if (pos >= len || tokenized[pos] != ')') {
                        return ERR_SN;
                    }
                    pos++;
                }

                /* Skip to = */
                while (pos < len && tokenized[pos] == ' ') pos++;

                if (pos >= len || tokenized[pos] != TOK_EQ) {
                    return ERR_SN;
                }
                pos++;

                /* Skip spaces after = */
                while (pos < len && tokenized[pos] == ' ') pos++;

                if (is_string) {
                    /* Parse string expression (handles literals, variables, functions, concatenation) */
                    basic_error_t err;
                    size_t consumed;
                    string_desc_t desc = eval_string_desc(state, tokenized + pos,
                                                          len - pos, &consumed, &err);
                    if (err != ERR_NONE) return err;
                    pos += consumed;

                    if (is_array) {
                        /* String array element assignment */
                        if (!array_set_string(state, var_name, idx1, idx2, desc)) {
                            return ERR_BS;  /* Bad subscript */
                        }
                        return ERR_NONE;
                    }
                    return stmt_let_string(state, var_name, desc);
                } else {
                    /* Evaluate numeric expression */
                    basic_error_t err;
                    size_t consumed;
                    mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                                &consumed, &err);
                    if (err != ERR_NONE) return err;

                    if (is_array) {
                        if (!array_set_numeric(state, var_name, idx1, idx2, val)) {
                            return ERR_BS;  /* Bad subscript */
                        }
                        return ERR_NONE;
                    } else {
                        return stmt_let_numeric(state, var_name, val);
                    }
                }
            }

            return ERR_SN;
    }
}

/*
 * Run the current program.
 */
void basic_run_program(basic_state_t *state) {
    if (!state) return;

    state->running = true;
    basic_setup_interrupt(state);

    while (state->running) {
        /* Check for Ctrl-C interrupt */
        if (g_interrupt_flag) {
            g_interrupt_flag = 0;
            fprintf(state->output, "\nBREAK");
            if (state->current_line > 0) {
                fprintf(state->output, " IN %u", state->current_line);
            }
            fprintf(state->output, "\n");
            state->running = false;
            state->can_continue = true;
            state->cont_line = state->current_line;
            state->cont_ptr = state->text_ptr;
            break;
        }

        /* Check if we're at a valid position */
        if (state->text_ptr >= state->program_end) {
            state->running = false;
            break;
        }

        /* Get current line info */
        uint8_t *line_start = NULL;
        uint8_t *ptr = state->memory + state->program_start;
        uint8_t *end = state->memory + state->program_end;

        /* Find the line containing text_ptr */
        while (ptr < end) {
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            uint16_t line_num = (uint16_t)(ptr[2] | (ptr[3] << 8));
            uint8_t *line_text = ptr + 4;
            uint8_t *line_end = (link > 0) ? state->memory + link : end;

            if (state->text_ptr >= (uint16_t)(line_text - state->memory) &&
                state->text_ptr < (uint16_t)(line_end - state->memory)) {
                state->current_line = line_num;
                line_start = ptr;
                break;
            }

            if (link == 0) break;
            ptr = state->memory + link;
        }

        if (!line_start) {
            state->running = false;
            break;
        }

        /* Get the statement to execute */
        uint8_t *text = state->memory + state->text_ptr;
        size_t text_len = 0;

        /* Skip leading spaces to find the statement token */
        size_t skip = 0;
        while (text[skip] == ' ') skip++;

        /* For REM statements, the entire rest of line is the statement */
        /* (colons in REM comments should not be treated as separators) */
        if (text[skip] == TOK_REM) {
            while (text[text_len] != '\0') text_len++;
        } else {
            /* Find end of statement (: or null), but skip over strings */
            bool in_string = false;
            while (text[text_len] != '\0') {
                if (text[text_len] == '"') {
                    in_string = !in_string;
                } else if (text[text_len] == ':' && !in_string) {
                    break;
                }
                text_len++;
            }
        }

        /* Save current position to detect flow control */
        uint16_t saved_text_ptr = state->text_ptr;

        /* Execute the statement */
        basic_error_t err = execute_statement(state, text, text_len);

        if (err != ERR_NONE) {
            basic_print_error(state, err, state->current_line);
            state->running = false;
            state->can_continue = false;
            break;
        }

        /* Check if statement changed text_ptr (GOTO, GOSUB, NEXT, etc.) */
        if (state->text_ptr != saved_text_ptr) {
            /* Statement modified text_ptr - don't override it */
            /* Check if execution was stopped */
            if (!state->running) break;
            continue;
        }

        /* Move to next statement */
        if (text[text_len] == ':') {
            /* More statements on this line */
            state->text_ptr += (uint16_t)(text_len + 1);
        } else {
            /* Move to next line */
            uint16_t link = (uint16_t)(line_start[0] | (line_start[1] << 8));
            if (link == 0) {
                state->running = false;
            } else {
                state->text_ptr = link + 4;  /* Skip link and line number */
            }
        }

        /* Check if execution was stopped by STOP (after advancing text_ptr) */
        /* This ensures CONT will resume from the next statement */
        if (!state->running) {
            if (state->can_continue) {
                /* Save continuation point at next statement */
                state->cont_ptr = state->text_ptr;
            }
            break;
        }
    }

    basic_clear_interrupt();
}

/*
 * Run interactive interpreter.
 */
void basic_run_interactive(basic_state_t *state) {
    if (!state) return;

    basic_print_banner(state);
    basic_print_ok(state);

    char line[256];

    while (1) {
        /* Read a line */
        if (!fgets(line, sizeof(line), state->input)) {
            break;
        }

        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Execute the line */
        basic_execute_line(state, line);

        /* Print OK if we're not running */
        if (!state->running) {
            basic_print_ok(state);
        }
    }
}

/*
 * Load program from file.
 */
bool basic_load_file(basic_state_t *state, const char *filename) {
    if (!state || !filename) return false;

    FILE *f = fopen(filename, "r");
    if (!f) return false;

    /* Clear existing program */
    stmt_new(state);

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Execute line (stores if it has line number) */
        if (!basic_execute_line(state, line)) {
            fclose(f);
            return false;
        }
    }

    fclose(f);
    return true;
}

/*
 * Save program to file.
 */
bool basic_save_file(basic_state_t *state, const char *filename) {
    if (!state || !filename) return false;

    FILE *f = fopen(filename, "w");
    if (!f) return false;

    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *end = state->memory + state->program_end;

    while (ptr < end) {
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        /* Print line number */
        fprintf(f, "%u ", num);

        /* Detokenize and print */
        const uint8_t *text = ptr + 4;
        while (*text) {
            uint8_t ch = *text++;

            if (TOK_IS_KEYWORD(ch)) {
                const char *kw = token_to_keyword(ch);
                if (kw) fprintf(f, "%s", kw);
            } else if (ch == '"') {
                fputc('"', f);
                while (*text && *text != '"') {
                    fputc(*text++, f);
                }
                if (*text == '"') {
                    fputc('"', f);
                    text++;
                }
            } else {
                fputc(ch, f);
            }
        }
        fputc('\n', f);

        if (link == 0) break;
        ptr = state->memory + link;
    }

    fclose(f);
    return true;
}

/* basic_rnd is implemented in rnd.c */
