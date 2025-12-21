/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file program.c
 * @brief BASIC Program Storage
 *
 * Manages BASIC program storage in memory as a linked list of tokenized lines.
 * This module handles line insertion, deletion, lookup, and listing.
 *
 * ## Program Line Format
 *
 * Each program line is stored as a linked-list node:
 * ```
 *   +--------+--------+---------+---------+------...------+------+
 *   |Link_lo |Link_hi |LineNo_lo|LineNo_hi| Tokenized Text| 0x00 |
 *   +--------+--------+---------+---------+------...------+------+
 *   Byte 0    Byte 1   Byte 2    Byte 3    Bytes 4-N       N+1
 *
 *   Link:    Offset to next line (0 = end of program), little-endian
 *   LineNo:  BASIC line number (1-65535), little-endian
 *   Text:    Tokenized program text (keywords are single bytes 0x81-0xC6)
 *   0x00:    Null terminator marking end of line
 * ```
 *
 * ## Memory Layout
 *
 * ```
 *   program_start                            program_end
 *       |                                         |
 *       v                                         v
 *   +-------+-------+-------+-------+-------------+
 *   |Line 10|Line 20|Line 30|Line 40| (empty)     |
 *   +-------+-------+-------+-------+-------------+
 *       |       ^       ^       ^
 *       +-------+       |       |
 *               +-------+       |
 *                       +-------+
 *                               (link=0)
 *
 *   Lines are stored in ascending order by line number.
 *   Each line's link field points to the next line.
 *   The last line has link=0 to mark end of program.
 * ```
 *
 * ## Operations
 *
 * - **Insert**: Shifts all following data up, updates all affected links
 * - **Delete**: Shifts all following data down, updates all affected links
 * - **Replace**: Delete old + Insert new (to handle size changes)
 * - **Lookup**: Linear search through linked list
 * - **LIST**: Traverse list, detokenizing keywords for display
 *
 * ## Tokenization
 *
 * Program text is stored in tokenized form to save memory:
 * - Keywords (PRINT, GOTO, etc.) become single bytes 0x81-0xC6
 * - Strings are stored as-is with their quotes
 * - Numbers are stored as ASCII digits (not converted to MBF)
 * - Variable names are stored as uppercase ASCII
 *
 * Example: "10 PRINT X" becomes:
 * ```
 *   [link][0x0A,0x00][0x92][ ][X][0x00]
 *          line 10    PRINT     X  null
 * ```
 *
 * ## Link Maintenance
 *
 * When inserting or deleting lines, all link fields pointing to or past
 * the affected area must be updated. This is O(n) but necessary to
 * maintain the linked list structure.
 */

#include "basic/basic.h"
#include "basic/tokens.h"
#include <string.h>

/*
 * Insert or replace a program line.
 * If tokenized_len is 0, deletes the line.
 * Returns true on success, false on out of memory.
 */
bool program_insert_line(basic_state_t *state, uint16_t line_num,
                         const uint8_t *tokenized, size_t tokenized_len) {
    if (!state) return false;

    /* Calculate new line size: 2 (link) + 2 (line num) + text + 1 (null) */
    uint16_t new_line_size = (tokenized_len > 0) ? (uint16_t)(4 + tokenized_len + 1) : 0;

    /* Find existing line and its predecessor */
    uint8_t *prev_line = NULL;
    uint8_t *curr_line = NULL;
    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *prog_end = state->memory + state->program_end;

    while (ptr < prog_end) {
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num == line_num) {
            curr_line = ptr;
            break;
        }
        if (num > line_num) {
            break;  /* Insert position found */
        }

        prev_line = ptr;
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        if (link == 0) {
            ptr = prog_end;  /* End of program */
            break;
        }
        ptr = state->memory + link;
    }

    /* Calculate old line size if it exists */
    uint16_t old_line_size = 0;
    if (curr_line) {
        uint8_t *line_text = curr_line + 4;
        while (*line_text) line_text++;
        old_line_size = (uint16_t)(line_text - curr_line + 1);
    }

    /* Check memory availability */
    int32_t size_delta = (int32_t)new_line_size - (int32_t)old_line_size;
    if (size_delta > 0) {
        uint16_t free_space = state->string_start - state->array_start;
        if ((uint16_t)size_delta > free_space) {
            return false;  /* Out of memory */
        }
    }

    if (curr_line) {
        /* Line exists - delete it first */
        uint8_t *line_end = curr_line + old_line_size;
        uint16_t old_link = (uint16_t)(curr_line[0] | (curr_line[1] << 8));

        /* Shift everything after it down */
        memmove(curr_line, line_end, (size_t)(prog_end - line_end));
        state->program_end -= old_line_size;
        prog_end = state->memory + state->program_end;

        /* Update previous line's link to skip over deleted line */
        if (prev_line) {
            uint16_t new_link = old_link;
            if (new_link != 0) {
                new_link -= old_line_size;  /* Adjust for removed bytes */
            }
            prev_line[0] = (uint8_t)(new_link & 0xFF);
            prev_line[1] = (uint8_t)(new_link >> 8);
        }

        /* Update all links that point past the deleted line */
        ptr = state->memory + state->program_start;
        while (ptr < prog_end) {
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            if (link > (uint16_t)(curr_line - state->memory)) {
                link -= old_line_size;
                ptr[0] = (uint8_t)(link & 0xFF);
                ptr[1] = (uint8_t)(link >> 8);
            }
            if (link == 0) break;
            ptr = state->memory + link;
        }

        /* Reset for re-insertion */
        curr_line = NULL;
        prev_line = NULL;

        /* Re-find insertion point */
        ptr = state->memory + state->program_start;
        while (ptr < prog_end) {
            uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));
            if (num > line_num) {
                break;
            }
            prev_line = ptr;
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            if (link == 0) {
                ptr = prog_end;
                break;
            }
            ptr = state->memory + link;
        }
    }

    /* Insert new line if we have content */
    if (new_line_size > 0) {
        uint8_t *insert_pos = ptr;  /* Insert before this position */

        /* Shift everything from insert_pos to make room */
        size_t tail_size = (size_t)(prog_end - insert_pos);
        memmove(insert_pos + new_line_size, insert_pos, tail_size);
        state->program_end += new_line_size;
        prog_end = state->memory + state->program_end;

        /* Update all links that point at or past insert position */
        uint8_t *scan = state->memory + state->program_start;
        while (scan < prog_end) {
            /* Skip the new line we're about to write */
            if (scan >= insert_pos && scan < insert_pos + new_line_size) {
                scan = insert_pos + new_line_size;
                if (scan >= prog_end) break;
            }
            uint16_t link = (uint16_t)(scan[0] | (scan[1] << 8));
            if (link != 0 && link >= (uint16_t)(insert_pos - state->memory)) {
                link += new_line_size;
                scan[0] = (uint8_t)(link & 0xFF);
                scan[1] = (uint8_t)(link >> 8);
            }
            if (link == 0) break;
            scan = state->memory + link;
        }

        /* Calculate link to next line */
        uint16_t next_offset = (uint16_t)(insert_pos + new_line_size - state->memory);
        if (next_offset >= state->program_end) {
            next_offset = 0;  /* End of program */
        }

        /* Write the new line */
        insert_pos[0] = (uint8_t)(next_offset & 0xFF);
        insert_pos[1] = (uint8_t)(next_offset >> 8);
        insert_pos[2] = (uint8_t)(line_num & 0xFF);
        insert_pos[3] = (uint8_t)(line_num >> 8);
        memcpy(insert_pos + 4, tokenized, tokenized_len);
        insert_pos[4 + tokenized_len] = 0;

        /* Update previous line's link to point to new line */
        if (prev_line) {
            uint16_t new_link = (uint16_t)(insert_pos - state->memory);
            prev_line[0] = (uint8_t)(new_link & 0xFF);
            prev_line[1] = (uint8_t)(new_link >> 8);
        }
    }

    /* Update var_start and array_start */
    state->var_start = state->program_end;
    state->array_start = state->var_start;

    /* Can't continue after modifying program */
    state->can_continue = false;

    return true;
}

/*
 * Get a program line by number.
 * Returns pointer to tokenized content (after line number), or NULL if not found.
 */
const uint8_t *program_get_line(basic_state_t *state, uint16_t line_num, size_t *line_len) {
    if (!state) return NULL;

    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *end = state->memory + state->program_end;

    while (ptr < end) {
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num == line_num) {
            const uint8_t *text = ptr + 4;
            if (line_len) {
                const uint8_t *p = text;
                while (*p) p++;
                *line_len = (size_t)(p - text);
            }
            return text;
        }

        if (link == 0) break;
        ptr = state->memory + link;
    }

    return NULL;
}

/*
 * Get the first line in the program.
 * Returns line number, or 0 if program is empty.
 */
uint16_t program_first_line(basic_state_t *state) {
    if (!state || state->program_end == state->program_start) {
        return 0;
    }
    uint8_t *ptr = state->memory + state->program_start;
    return (uint16_t)(ptr[2] | (ptr[3] << 8));
}

/*
 * Get the next line after line_num.
 * Returns line number, or 0 if no more lines.
 */
uint16_t program_next_line(basic_state_t *state, uint16_t line_num) {
    if (!state) return 0;

    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *end = state->memory + state->program_end;

    while (ptr < end) {
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num == line_num) {
            if (link == 0 || link >= state->program_end) {
                return 0;
            }
            return (uint16_t)(state->memory[link + 2] | (state->memory[link + 3] << 8));
        }

        if (link == 0) break;
        ptr = state->memory + link;
    }

    return 0;
}

/*
 * List program lines.
 */
void basic_list_program(basic_state_t *state, uint16_t start, uint16_t end) {
    if (!state || !state->output) return;

    if (end == 0) end = 0xFFFF;

    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *prog_end = state->memory + state->program_end;

    while (ptr < prog_end) {
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num >= start && num <= end) {
            /* Print line number */
            fprintf(state->output, "%u ", num);

            /* Detokenize and print line content */
            const uint8_t *text = ptr + 4;
            while (*text) {
                uint8_t ch = *text++;

                if (TOK_IS_KEYWORD(ch)) {
                    const char *kw = token_to_keyword(ch);
                    if (kw) {
                        fprintf(state->output, "%s", kw);
                    }
                } else if (ch == '"') {
                    fputc('"', state->output);
                    while (*text && *text != '"') {
                        fputc(*text++, state->output);
                    }
                    if (*text == '"') {
                        fputc('"', state->output);
                        text++;
                    }
                } else {
                    fputc(ch, state->output);
                }
            }
            fputc('\n', state->output);
        }

        if (num > end || link == 0) break;
        ptr = state->memory + link;
    }
}

/*
 * Clear the program.
 */
void program_clear(basic_state_t *state) {
    if (!state) return;
    state->program_end = state->program_start;
    state->var_start = state->program_end;
    state->array_start = state->var_start;
    state->var_count_ = 0;
    state->can_continue = false;
}
