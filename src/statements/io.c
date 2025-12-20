/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * io.c - I/O Statements
 *
 * Implements PRINT, INPUT, DATA/READ/RESTORE, and related statements.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include <string.h>
#include <ctype.h>

/*
 * Output a character to the terminal.
 * Handles column tracking and null padding.
 */
void io_putchar(basic_state_t *state, char ch) {
    if (!state || state->output_suppressed) return;

    fputc(ch, state->output);

    if (ch == '\r' || ch == '\n') {
        state->terminal_x = 0;
        /* Send null padding if needed */
        for (int i = 0; i < state->null_count; i++) {
            fputc('\0', state->output);
        }
    } else if (ch == '\t') {
        /* TAB advances to next tab stop (every 8 columns in original) */
        state->terminal_x = (state->terminal_x + 8) & ~7;
    } else {
        state->terminal_x++;
        if (state->terminal_x >= state->terminal_width) {
            /* Auto line wrap */
            fputc('\r', state->output);
            fputc('\n', state->output);
            state->terminal_x = 0;
        }
    }
}

/*
 * Output a string to the terminal.
 */
void io_print_string(basic_state_t *state, const char *str, size_t len) {
    if (!state || !str) return;

    for (size_t i = 0; i < len; i++) {
        io_putchar(state, str[i]);
    }
}

/*
 * Output a null-terminated string to the terminal.
 */
void io_print_cstring(basic_state_t *state, const char *str) {
    if (!state || !str) return;

    while (*str) {
        io_putchar(state, *str++);
    }
}

/*
 * Output a newline (CR LF for original compatibility).
 */
void io_newline(basic_state_t *state) {
    if (!state) return;
    io_putchar(state, '\r');
    io_putchar(state, '\n');
}

/*
 * Print a numeric value in BASIC format.
 * Positive numbers have leading space, negative have leading minus.
 * Trailing space is added.
 */
void io_print_number(basic_state_t *state, mbf_t value) {
    if (!state) return;

    char buf[32];
    size_t len = mbf_to_string(value, buf, sizeof(buf));

    /* Leading space for positive numbers */
    if (!mbf_is_negative(value)) {
        io_putchar(state, ' ');
    }

    io_print_string(state, buf, len);

    /* Trailing space */
    io_putchar(state, ' ');
}

/*
 * Print TAB function - move to specified column.
 */
void io_tab(basic_state_t *state, int column) {
    if (!state) return;

    /* Column is 0-based internally but 1-based in BASIC */
    column--;

    if (column < 0) column = 0;
    if (column >= state->terminal_width) column = state->terminal_width - 1;

    /* If we're past the target, go to next line first */
    if (state->terminal_x > column) {
        io_newline(state);
    }

    /* Space to target column */
    while (state->terminal_x < column) {
        io_putchar(state, ' ');
    }
}

/*
 * Print SPC function - output specified number of spaces.
 */
void io_spc(basic_state_t *state, int count) {
    if (!state) return;

    for (int i = 0; i < count; i++) {
        io_putchar(state, ' ');
    }
}

/*
 * Read a line of input from the terminal.
 * Returns the line in buf (null-terminated), length in *len.
 * Handles backspace and basic line editing.
 */
bool io_input_line(basic_state_t *state, char *buf, size_t bufsize, size_t *len) {
    if (!state || !buf || bufsize == 0) return false;

    size_t pos = 0;
    int ch;

    while ((ch = fgetc(state->input)) != EOF) {
        if (ch == '\n' || ch == '\r') {
            break;
        } else if (ch == '\b' || ch == 127) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                /* Echo backspace sequence */
                io_putchar(state, '\b');
                io_putchar(state, ' ');
                io_putchar(state, '\b');
            }
        } else if (ch == 3) {
            /* Ctrl-C - cancel input */
            return false;
        } else if (pos < bufsize - 1) {
            buf[pos++] = (char)ch;
            io_putchar(state, (char)ch);
        }
    }

    buf[pos] = '\0';
    if (len) *len = pos;

    io_newline(state);
    return true;
}

/*
 * Parse a numeric value from input string.
 * Returns number of characters consumed.
 */
size_t io_parse_number(const char *str, mbf_t *value) {
    if (!str || !value) return 0;

    /* Skip leading whitespace */
    const char *p = str;
    while (*p == ' ') p++;

    return mbf_from_string(p, value);
}

/*
 * DATA/READ state management.
 */

/*
 * Initialize DATA pointer (called at program start).
 */
void io_data_init(basic_state_t *state) {
    if (!state) return;
    state->data_line = 0;
    state->data_ptr = 0;
}

/*
 * RESTORE statement - reset DATA pointer to beginning.
 */
basic_error_t stmt_restore(basic_state_t *state) {
    if (!state) return ERR_FC;
    io_data_init(state);
    return ERR_NONE;
}

/*
 * RESTORE to specific line number.
 */
basic_error_t stmt_restore_line(basic_state_t *state, uint16_t line_num) {
    if (!state) return ERR_FC;

    /* Find the line */
    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *end = state->memory + state->program_end;

    while (ptr < end) {
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num >= line_num) {
            state->data_line = num;
            state->data_ptr = (uint16_t)(ptr - state->memory) + 4;
            return ERR_NONE;
        }

        if (link == 0) break;
        ptr = state->memory + link;
    }

    return ERR_UL;  /* Undefined line */
}

/*
 * Find next DATA item.
 * Scans forward in program looking for DATA statements.
 * Returns pointer to the data item, or NULL if out of data.
 */
static const uint8_t *find_next_data(basic_state_t *state) {
    if (!state) return NULL;

    uint8_t *ptr;
    uint8_t *end = state->memory + state->program_end;

    if (state->data_ptr == 0) {
        /* Start from beginning of first line's TEXT (skip 4-byte header) */
        ptr = state->memory + state->program_start + 4;
    } else {
        ptr = state->memory + state->data_ptr;
    }

    while (ptr < end) {
        /* If we're at a DATA token, return pointer to next item */
        if (*ptr == TOK_DATA) {
            ptr++;  /* Skip DATA token */
            state->data_ptr = (uint16_t)(ptr - state->memory);
            return ptr;
        }

        /* If we're at a comma in a DATA statement, move to next item */
        if (*ptr == ',') {
            ptr++;
            state->data_ptr = (uint16_t)(ptr - state->memory);
            return ptr;
        }

        /* If we're at end of line (0), move to next line */
        if (*ptr == 0) {
            ptr++;  /* Skip null */

            /* Check if there's more program */
            if (ptr >= end) return NULL;

            /* Read link to next line */
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            if (link == 0) return NULL;

            /* Update data line number */
            state->data_line = (uint16_t)(ptr[2] | (ptr[3] << 8));

            /* Move to start of line text */
            ptr += 4;
            continue;
        }

        /* Skip other content */
        ptr++;
    }

    return NULL;
}

/*
 * READ numeric value from DATA.
 */
basic_error_t io_read_numeric(basic_state_t *state, mbf_t *value) {
    if (!state || !value) return ERR_FC;

    const uint8_t *data = find_next_data(state);
    if (!data) {
        return ERR_OD;  /* Out of data */
    }

    /* Parse the number */
    size_t consumed = mbf_from_string((const char *)data, value);
    if (consumed == 0) {
        *value = MBF_ZERO;
    }

    /* Advance past this item */
    const uint8_t *p = data;
    while (*p && *p != ',' && *p != ':') p++;
    state->data_ptr = (uint16_t)(p - state->memory);

    return ERR_NONE;
}

/*
 * READ string value from DATA.
 */
basic_error_t io_read_string(basic_state_t *state, string_desc_t *value) {
    if (!state || !value) return ERR_FC;

    const uint8_t *data = find_next_data(state);
    if (!data) {
        return ERR_OD;  /* Out of data */
    }

    /* Find end of string (comma, colon, or null) */
    const uint8_t *start = data;
    const uint8_t *end = data;

    /* Check for quoted string */
    if (*start == '"') {
        start++;
        end = start;
        while (*end && *end != '"') end++;
    } else {
        /* Unquoted - ends at comma or colon */
        while (*end && *end != ',' && *end != ':') end++;
    }

    size_t len = (size_t)(end - start);
    if (len > 255) len = 255;

    *value = string_create_len(state, (const char *)start, (uint8_t)len);

    /* Advance past this item */
    const uint8_t *p = end;
    if (*p == '"') p++;  /* Skip closing quote */
    while (*p == ' ') p++;  /* Skip trailing spaces */
    state->data_ptr = (uint16_t)(p - state->memory);

    return ERR_NONE;
}

/*
 * NULL statement - set null padding count.
 */
basic_error_t stmt_null(basic_state_t *state, int count) {
    if (!state) return ERR_FC;

    if (count < 0 || count > 255) {
        return ERR_FC;
    }

    state->null_count = (uint8_t)count;
    return ERR_NONE;
}

/*
 * WIDTH statement - set terminal width.
 */
basic_error_t stmt_width(basic_state_t *state, int width) {
    if (!state) return ERR_FC;

    if (width < BASIC8K_MIN_WIDTH || width > BASIC8K_MAX_WIDTH) {
        return ERR_FC;
    }

    state->terminal_width = (uint8_t)width;
    return ERR_NONE;
}

/*
 * POS function - return current column position (1-based).
 */
int io_pos(basic_state_t *state) {
    if (!state) return 0;
    return state->terminal_x + 1;  /* BASIC uses 1-based */
}
