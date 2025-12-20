/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * flow.c - Control Flow Statements
 *
 * Implements GOTO, GOSUB, FOR/NEXT, IF/THEN, and related statements.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include <string.h>

/*
 * Find a line by number in the program.
 * Returns pointer to line start (link field), or NULL if not found.
 */
static uint8_t *find_line(basic_state_t *state, uint16_t line_num) {
    if (!state) return NULL;

    uint8_t *ptr = state->memory + state->program_start;
    uint8_t *end = state->memory + state->program_end;

    while (ptr < end) {
        /* Line format: link[2], line_num[2], tokenized_text..., 0 */
        uint16_t link = (uint16_t)(ptr[0] | (ptr[1] << 8));
        uint16_t num = (uint16_t)(ptr[2] | (ptr[3] << 8));

        if (num == line_num) {
            return ptr;
        }

        if (link == 0) break;  /* End of program */
        ptr = state->memory + link;
    }

    return NULL;
}

/*
 * Execute GOTO statement.
 * Transfers execution to the specified line number.
 */
basic_error_t stmt_goto(basic_state_t *state, uint16_t line_num) {
    if (!state) return ERR_FC;

    uint8_t *target = find_line(state, line_num);
    if (!target) {
        return ERR_UL;  /* Undefined line */
    }

    /* Set execution position to start of target line */
    state->current_line = line_num;
    state->text_ptr = (uint16_t)(target - state->memory) + 4;  /* Skip link and line number */

    return ERR_NONE;
}

/*
 * Execute GOSUB statement.
 * Pushes return address and transfers to target line.
 */
basic_error_t stmt_gosub(basic_state_t *state, uint16_t line_num,
                         uint16_t return_line, uint16_t return_ptr) {
    if (!state) return ERR_FC;

    /* Check stack space */
    if (state->gosub_sp >= 16) {
        return ERR_OM;  /* Out of memory (stack overflow) */
    }

    /* Push return address */
    state->gosub_stack[state->gosub_sp].line_number = return_line;
    state->gosub_stack[state->gosub_sp].text_ptr = return_ptr;
    state->gosub_sp++;

    /* Transfer to target */
    return stmt_goto(state, line_num);
}

/*
 * Execute RETURN statement.
 * Pops return address and continues from there.
 */
basic_error_t stmt_return(basic_state_t *state) {
    if (!state) return ERR_FC;

    /* Check if there's something to return to */
    if (state->gosub_sp == 0) {
        return ERR_RG;  /* RETURN without GOSUB */
    }

    /* Pop return address */
    state->gosub_sp--;
    state->current_line = state->gosub_stack[state->gosub_sp].line_number;
    state->text_ptr = state->gosub_stack[state->gosub_sp].text_ptr;

    return ERR_NONE;
}

/*
 * Execute FOR statement.
 * Initializes loop variable and pushes loop parameters.
 */
basic_error_t stmt_for(basic_state_t *state, const char *var_name,
                       mbf_t initial, mbf_t limit, mbf_t step,
                       uint16_t next_line, uint16_t next_ptr) {
    if (!state || !var_name) return ERR_FC;

    /* Check stack space */
    if (state->for_sp >= 16) {
        return ERR_OM;  /* Out of memory (stack overflow) */
    }

    /* Set initial value */
    if (!var_set_numeric(state, var_name, initial)) {
        return ERR_OM;
    }

    /* Find or create the variable to get its pointer */
    uint8_t *var = var_get_or_create(state, var_name);
    if (!var) {
        return ERR_OM;
    }

    /* Check if this variable already has a FOR loop */
    /* If so, reuse that stack entry (nested FOR with same variable) */
    for (int i = state->for_sp - 1; i >= 0; i--) {
        if (state->for_stack[i].var == var) {
            /* Reuse this entry */
            state->for_stack[i].line_number = next_line;
            state->for_stack[i].text_ptr = next_ptr;
            state->for_stack[i].limit = limit;
            state->for_stack[i].step = step;
            return ERR_NONE;
        }
    }

    /* Push new FOR entry */
    for_entry_t *entry = &state->for_stack[state->for_sp];
    entry->line_number = next_line;
    entry->text_ptr = next_ptr;
    entry->var = var;
    entry->limit = limit;
    entry->step = step;
    state->for_sp++;

    return ERR_NONE;
}

/*
 * Execute NEXT statement.
 * Increments loop variable and checks termination condition.
 * Returns true if loop should continue, false if done.
 */
basic_error_t stmt_next(basic_state_t *state, const char *var_name, bool *continue_loop) {
    if (!state || !continue_loop) return ERR_FC;

    *continue_loop = false;

    /* Find the FOR entry */
    int idx = state->for_sp - 1;

    if (var_name && var_name[0]) {
        /* Find FOR with specific variable */
        uint8_t *var = var_find(state, var_name);
        if (!var) {
            return ERR_NF;  /* NEXT without FOR */
        }

        while (idx >= 0 && state->for_stack[idx].var != var) {
            idx--;
        }
        if (idx < 0) {
            return ERR_NF;  /* NEXT without FOR */
        }

        /* Pop any inner loops */
        state->for_sp = idx + 1;
    } else {
        /* NEXT without variable - use most recent FOR */
        if (idx < 0) {
            return ERR_NF;  /* NEXT without FOR */
        }
    }

    for_entry_t *entry = &state->for_stack[idx];

    /* Get current value */
    uint8_t *var_ptr = (uint8_t *)entry->var;
    mbf_t current;
    memcpy(&current.raw, var_ptr + 2, 4);

    /* Add step */
    mbf_t new_value = mbf_add(current, entry->step);
    memcpy(var_ptr + 2, &new_value.raw, 4);

    /* Check termination */
    int step_sign = mbf_sign(entry->step);
    int cmp = mbf_cmp(new_value, entry->limit);

    if (step_sign >= 0) {
        /* Positive step: continue while value <= limit */
        *continue_loop = (cmp <= 0);
    } else {
        /* Negative step: continue while value >= limit */
        *continue_loop = (cmp >= 0);
    }

    if (*continue_loop) {
        /* Loop continues - go back to after FOR */
        state->current_line = entry->line_number;
        state->text_ptr = entry->text_ptr;
    } else {
        /* Loop done - pop entry */
        state->for_sp = idx;
    }

    return ERR_NONE;
}

/*
 * Execute IF statement evaluation.
 * Returns true if condition is true (non-zero), false otherwise.
 */
bool stmt_if_eval(mbf_t condition) {
    return !mbf_is_zero(condition);
}

/*
 * Execute END statement.
 * Stops program execution.
 */
basic_error_t stmt_end(basic_state_t *state) {
    if (!state) return ERR_FC;

    state->running = false;
    state->can_continue = false;

    return ERR_NONE;
}

/*
 * Execute STOP statement.
 * Stops program execution but allows CONT.
 */
basic_error_t stmt_stop(basic_state_t *state, uint16_t line, uint16_t ptr) {
    if (!state) return ERR_FC;

    state->running = false;
    state->can_continue = true;
    state->cont_line = line;
    state->cont_ptr = ptr;

    return ERR_NONE;
}

/*
 * Execute CONT statement.
 * Continues execution after STOP or Ctrl-C.
 */
basic_error_t stmt_cont(basic_state_t *state) {
    if (!state) return ERR_FC;

    if (!state->can_continue) {
        return ERR_CN;  /* Can't continue */
    }

    state->running = true;
    state->current_line = state->cont_line;
    state->text_ptr = state->cont_ptr;

    return ERR_NONE;
}

/*
 * Execute ON...GOTO statement.
 * Branches to one of several lines based on expression value.
 */
basic_error_t stmt_on_goto(basic_state_t *state, int value,
                           uint16_t *lines, int num_lines) {
    if (!state || !lines || num_lines <= 0) return ERR_FC;

    /* Value must be in range 1..num_lines */
    if (value < 1 || value > num_lines) {
        /* Out of range - just continue to next statement */
        return ERR_NONE;
    }

    return stmt_goto(state, lines[value - 1]);
}

/*
 * Execute ON...GOSUB statement.
 * Subroutine call to one of several lines based on expression value.
 */
basic_error_t stmt_on_gosub(basic_state_t *state, int value,
                            uint16_t *lines, int num_lines,
                            uint16_t return_line, uint16_t return_ptr) {
    if (!state || !lines || num_lines <= 0) return ERR_FC;

    /* Value must be in range 1..num_lines */
    if (value < 1 || value > num_lines) {
        /* Out of range - just continue to next statement */
        return ERR_NONE;
    }

    return stmt_gosub(state, lines[value - 1], return_line, return_ptr);
}

/*
 * Execute POP statement.
 * Removes GOSUB return address from stack without returning.
 */
basic_error_t stmt_pop(basic_state_t *state) {
    if (!state) return ERR_FC;

    if (state->gosub_sp == 0) {
        return ERR_RG;  /* No GOSUB to pop */
    }

    state->gosub_sp--;
    return ERR_NONE;
}

/*
 * Clear control stacks (called by RUN, CLEAR, etc.)
 */
void stmt_clear_stacks(basic_state_t *state) {
    if (!state) return;
    state->for_sp = 0;
    state->gosub_sp = 0;
}
