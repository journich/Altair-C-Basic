/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * program.c - Program Storage
 *
 * Manages BASIC program storage in memory.
 * Lines are stored as linked list: link[2], line_num[2], tokenized_text..., 0
 */

#include "basic/basic.h"
#include "basic/tokens.h"
#include <string.h>

/*
 * Find a program line by number.
 * Returns pointer to line start (link field), or NULL if not found.
 * If prev is not NULL, stores pointer to previous line (or NULL if first).
 */
static uint8_t *find_line(basic_state_t *state, uint16_t line_num, uint8_t **prev) {
    if (!state) return NULL;

    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *end = state->memory + state->program_end;
    uint8_t *prev_ptr = NULL;

    while (ptr < end) {
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num == line_num) {
            if (prev) *prev = prev_ptr;
            return ptr;
        }

        if (num > line_num || link == 0) {
            /* Line not found, but we found where it would go */
            if (prev) *prev = prev_ptr;
            return NULL;
        }

        prev_ptr = ptr;
        ptr = state->memory + link;
    }

    if (prev) *prev = prev_ptr;
    return NULL;
}

/*
 * Get line length (including header but not link to next).
 */
static uint16_t line_length(const uint8_t *line) {
    const uint8_t *ptr = line + 4;  /* Skip link and line number */
    while (*ptr) ptr++;
    return (uint16_t)(ptr - line + 1);  /* +1 for null terminator */
}

/*
 * Insert or replace a program line.
 * If tokenized_len is 0, deletes the line.
 * Returns true on success, false on out of memory.
 */
bool program_insert_line(basic_state_t *state, uint16_t line_num,
                         const uint8_t *tokenized, size_t tokenized_len) {
    if (!state) return false;

    uint8_t *prev = NULL;
    uint8_t *existing = find_line(state, line_num, &prev);
    uint16_t old_len = existing ? line_length(existing) : 0;
    uint16_t new_len = (tokenized_len > 0) ? (uint16_t)(4 + tokenized_len + 1) : 0;
    int16_t delta = (int16_t)new_len - (int16_t)old_len;

    /* Check if we have space */
    if (delta > 0) {
        uint16_t free_space = state->string_start - state->array_start;
        if ((uint16_t)delta > free_space) {
            return false;  /* Out of memory */
        }
    }

    if (existing) {
        /* Line exists - we need to delete or replace it */
        uint8_t *line_end = existing + old_len;
        uint8_t *prog_end = state->memory + state->program_end;

        if (new_len == 0) {
            /* Delete line - shift everything after it down */
            memmove(existing, line_end, (size_t)(prog_end - line_end));
            state->program_end -= old_len;

            /* Update previous line's link */
            if (prev) {
                uint16_t next_link = (uint16_t)(line_end[0] | (line_end[1] << 8));
                if (next_link != 0) {
                    next_link -= old_len;
                }
                prev[0] = (uint8_t)(next_link & 0xFF);
                prev[1] = (uint8_t)(next_link >> 8);
            }

            /* Update all links that point past the deleted line */
            uint8_t *ptr = state->memory + state->program_start;
            while (ptr < state->memory + state->program_end) {
                uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
                if (link > (uint16_t)(existing - state->memory)) {
                    link -= old_len;
                    ptr[0] = (uint8_t)(link & 0xFF);
                    ptr[1] = (uint8_t)(link >> 8);
                }
                if (link == 0) break;
                ptr = state->memory + link;
            }
        } else {
            /* Replace line - resize in place */
            if (delta != 0) {
                memmove(existing + new_len, line_end, (size_t)(prog_end - line_end));
                state->program_end = (uint16_t)((int16_t)state->program_end + delta);

                /* Update all links that point past this line */
                uint8_t *ptr = state->memory + state->program_start;
                while (ptr < state->memory + state->program_end) {
                    uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
                    if (ptr != existing && link > (uint16_t)(existing - state->memory)) {
                        link = (uint16_t)((int16_t)link + delta);
                        ptr[0] = (uint8_t)(link & 0xFF);
                        ptr[1] = (uint8_t)(link >> 8);
                    }
                    if (link == 0) break;
                    ptr = state->memory + link;
                }
            }

            /* Update link for this line */
            uint16_t old_link = (uint16_t)(existing[0] | (existing[1] << 8));
            if (old_link != 0) {
                old_link = (uint16_t)((int16_t)old_link + delta);
            }
            existing[0] = (uint8_t)(old_link & 0xFF);
            existing[1] = (uint8_t)(old_link >> 8);

            /* Line number stays the same */
            existing[2] = (uint8_t)(line_num & 0xFF);
            existing[3] = (uint8_t)(line_num >> 8);

            /* Copy new content */
            memcpy(existing + 4, tokenized, tokenized_len);
            existing[4 + tokenized_len] = 0;
        }
    } else if (new_len > 0) {
        /* Line doesn't exist - insert it */
        /* Find insert position */
        uint8_t *insert_pos;
        uint8_t *ptr = state->memory + state->program_start;
        uint8_t *end = state->memory + state->program_end;

        insert_pos = ptr;
        while (ptr < end) {
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

            if (num > line_num) {
                insert_pos = ptr;
                break;
            }

            if (link == 0) {
                insert_pos = end;
                break;
            }

            insert_pos = state->memory + link;
            ptr = state->memory + link;
        }

        /* Make room for new line */
        size_t tail_size = (size_t)(end - insert_pos);
        memmove(insert_pos + new_len, insert_pos, tail_size);
        state->program_end += new_len;

        /* Update all links that point at or past insert position */
        ptr = state->memory + state->program_start;
        while (ptr < state->memory + state->program_end) {
            if (ptr >= insert_pos && ptr < insert_pos + new_len) {
                /* This is our new line, skip it for now */
                ptr = insert_pos + new_len;
                if (ptr >= state->memory + state->program_end) break;
            }
            uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
            if (link >= (uint16_t)(insert_pos - state->memory) && link != 0) {
                link += new_len;
                ptr[0] = (uint8_t)(link & 0xFF);
                ptr[1] = (uint8_t)(link >> 8);
            }
            if (link == 0) break;
            ptr = state->memory + link;
        }

        /* Write the new line */
        /* Calculate link to next line */
        uint16_t next_line_offset = (uint16_t)(insert_pos + new_len - state->memory);
        if (next_line_offset >= state->program_end) {
            next_line_offset = 0;  /* End of program */
        }

        insert_pos[0] = (uint8_t)(next_line_offset & 0xFF);
        insert_pos[1] = (uint8_t)(next_line_offset >> 8);
        insert_pos[2] = (uint8_t)(line_num & 0xFF);
        insert_pos[3] = (uint8_t)(line_num >> 8);
        memcpy(insert_pos + 4, tokenized, tokenized_len);
        insert_pos[4 + tokenized_len] = 0;

        /* Update previous line's link if needed */
        if (insert_pos > state->memory + state->program_start) {
            ptr = state->memory + state->program_start;
            while (ptr < insert_pos) {
                uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
                uint8_t *next = state->memory + link;
                if (next == insert_pos + new_len || link == 0 ||
                    (ptr[2] | (ptr[3] << 8)) < line_num) {
                    /* This line should point to our new line */
                    uint16_t new_link = (uint16_t)(insert_pos - state->memory);
                    ptr[0] = (uint8_t)(new_link & 0xFF);
                    ptr[1] = (uint8_t)(new_link >> 8);
                }
                if (link == 0) break;
                ptr = next;
            }
        }
    }

    /* Update var_start and array_start if needed */
    state->var_start = state->program_end;
    state->array_start = state->var_start;

    /* Can't continue after modifying program */
    state->can_continue = false;

    return true;
}

/*
 * Get a program line by number.
 * Returns pointer to tokenized content (after line number), or NULL if not found.
 * Sets *line_len to length of tokenized content (not including null).
 */
const uint8_t *program_get_line(basic_state_t *state, uint16_t line_num, size_t *line_len) {
    if (!state) return NULL;

    uint8_t *line = find_line(state, line_num, NULL);
    if (!line) return NULL;

    const uint8_t *text = line + 4;
    if (line_len) {
        const uint8_t *p = text;
        while (*p) p++;
        *line_len = (size_t)(p - text);
    }

    return text;
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

    uint8_t *line = find_line(state, line_num, NULL);
    if (!line) return 0;

    uint16_t link = (uint16_t)(line[0] | (line[1] << 8));
    if (link == 0 || link >= state->program_end) {
        return 0;
    }

    return (uint16_t)(state->memory[link + 2] | (state->memory[link + 3] << 8));
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
                    /* Print keyword */
                    const char *kw = token_to_keyword(ch);
                    if (kw) {
                        fprintf(state->output, "%s", kw);
                    }
                } else if (ch == '"') {
                    /* Print string literal */
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
