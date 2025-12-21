/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file interpreter.c
 * @brief Main BASIC Interpreter - Execution Engine
 *
 * This is the heart of the BASIC interpreter. It implements:
 * - State initialization and management
 * - Direct command execution (typing commands at the OK prompt)
 * - Program execution (RUN command)
 * - Statement dispatch and execution
 * - Error handling and reporting
 * - Interrupt handling (Ctrl-C)
 *
 * ## Execution Flow
 *
 * ```
 *   User Input
 *       |
 *       v
 *   basic_execute_line()
 *       |
 *       +-- Has line number? --> program_insert_line() --> Store in program
 *       |
 *       +-- No line number --> execute_statement() --> Direct execution
 *                                   |
 *                                   v
 *                           Statement dispatch (switch)
 *                                   |
 *         +--------+--------+-------+-------+--------+
 *         |        |        |       |       |        |
 *         v        v        v       v       v        v
 *       PRINT    LET     GOTO    FOR    INPUT     etc...
 *         |        |        |       |       |
 *         v        v        v       v       v
 *     io.c    variables.c  flow.c  flow.c  io.c
 * ```
 *
 * ## Program Execution Loop
 *
 * When RUN is executed:
 * ```
 *   basic_run_program()
 *       |
 *       v
 *   while (running) {
 *       1. Check for Ctrl-C interrupt
 *       2. Find current line from text_ptr
 *       3. Extract statement (up to ':' or end of line)
 *       4. execute_statement()
 *       5. Advance to next statement or line
 *   }
 * ```
 *
 * ## Statement Dispatch
 *
 * The execute_statement() function is a large switch on the first token:
 * - TOK_REM: Skip (comment)
 * - TOK_PRINT: Output expressions
 * - TOK_LET: Variable assignment (implicit LET also handled)
 * - TOK_GOTO/GOSUB: Branch to line number
 * - TOK_FOR/NEXT: Loop control
 * - TOK_IF: Conditional execution
 * - TOK_INPUT/READ: Get data
 * - etc.
 *
 * ## Key State Variables
 *
 * - `text_ptr`: Current position in program being executed
 * - `current_line`: Line number being executed (0xFFFF = direct mode)
 * - `running`: True while program is executing
 * - `can_continue`: True if CONT can resume execution
 *
 * ## Error Handling
 *
 * Errors are returned as basic_error_t values. The main loop checks
 * for errors after each statement and:
 * 1. Prints the error message with line number
 * 2. Stops execution
 * 3. Returns to the OK prompt
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include "basic/tokens.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>


/* In 8K BASIC, only first 2 chars of variable names are significant,
 * but we must consume all remaining alphanumeric characters */
#define SKIP_EXTRA_VAR_CHARS() \
    while (pos < len && isalnum(tokenized[pos])) pos++


/*============================================================================
 * GLOBAL ERROR STATE
 *
 * The error system uses a global variable to track the most recent error.
 * This is checked after operations that can fail (math, I/O, etc.).
 *============================================================================*/

/**
 * @brief Global error context
 *
 * Holds information about the last error that occurred:
 * - code: The error code (ERR_SN, ERR_OM, etc.)
 * - line_number: Where it happened (0xFFFF = direct mode)
 * - position: Character position (reserved for future use)
 */
error_context_t g_last_error = {0};


/*============================================================================
 * CTRL-C INTERRUPT HANDLING
 *
 * Users can press Ctrl-C to interrupt a running program. This uses the
 * POSIX signal mechanism to set a flag that's checked in the main loop.
 *
 * The interrupt is cooperative - the program checks g_interrupt_flag
 * between statements and exits cleanly if set.
 *============================================================================*/

/** Flag set by signal handler when Ctrl-C is pressed */
static volatile sig_atomic_t g_interrupt_flag = 0;

/** Pointer to interpreter state for interrupt handling */
static basic_state_t *g_interrupt_state = NULL;

/**
 * @brief Signal handler for SIGINT (Ctrl-C)
 *
 * Simply sets the interrupt flag. The main execution loop checks
 * this flag and handles the break gracefully.
 *
 * @param sig Signal number (unused, always SIGINT)
 */
static void interrupt_handler(int sig) {
    (void)sig;  /* Unused parameter */
    g_interrupt_flag = 1;
}

/**
 * @brief Set up Ctrl-C interrupt handling for program execution
 *
 * Called before basic_run_program() to enable clean interruption.
 * Saves the interpreter state so the handler knows what to interrupt.
 *
 * @param state The interpreter state to associate with interrupts
 */
void basic_setup_interrupt(basic_state_t *state) {
    g_interrupt_state = state;
    g_interrupt_flag = 0;
    signal(SIGINT, interrupt_handler);
}

/**
 * @brief Disable interrupt handling and restore default behavior
 *
 * Called after program execution completes to restore normal
 * Ctrl-C behavior (terminate process).
 */
void basic_clear_interrupt(void) {
    g_interrupt_flag = 0;
    signal(SIGINT, SIG_DFL);
}


/*============================================================================
 * ERROR CODE TABLE
 *
 * This table maps error codes to their 2-letter abbreviations.
 * These abbreviations match the original Altair BASIC exactly.
 *============================================================================*/

/**
 * @brief Error code strings
 *
 * Two-letter error codes as displayed to the user.
 * Index matches the basic_error_t enum values.
 *
 * When an error occurs, BASIC prints:
 *   ?XX ERROR IN line
 *
 * Where XX is one of these codes.
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

/**
 * @brief Get the 2-letter error code string for an error
 *
 * Looks up the error code in the ERROR_CODES table.
 *
 * @param err Error code to look up
 * @return Pointer to 2-character string (e.g., "SN", "OM")
 *         Returns "??" for unknown error codes.
 */
const char *error_code_string(basic_error_t err) {
    if (err >= ERR_COUNT) return "??";
    return ERROR_CODES[err];
}


/*============================================================================
 * ERROR MANAGEMENT FUNCTIONS
 *============================================================================*/

/**
 * @brief Raise an error (direct mode)
 *
 * Sets the global error state. Line number is set to 0xFFFF
 * to indicate direct mode (no program line).
 *
 * @param code The error code to raise
 */
void basic_error(basic_error_t code) {
    g_last_error.code = code;
    g_last_error.line_number = 0xFFFF;
    g_last_error.position = 0;
}

/**
 * @brief Raise an error at a specific line
 *
 * Sets the global error state with a specific line number.
 * Used when an error occurs during program execution.
 *
 * @param code The error code to raise
 * @param line The line number where the error occurred
 */
void basic_error_at_line(basic_error_t code, uint16_t line) {
    g_last_error.code = code;
    g_last_error.line_number = line;
    g_last_error.position = 0;
}

/**
 * @brief Clear the last error
 *
 * Resets the error state to ERR_NONE. Called before operations
 * that might fail, so errors can be detected.
 */
void basic_clear_error(void) {
    g_last_error.code = ERR_NONE;
}


/*============================================================================
 * INTERPRETER INITIALIZATION
 *
 * These functions manage the interpreter's lifecycle:
 * - basic_init: Create and configure a new interpreter
 * - basic_free: Destroy interpreter and free memory
 * - basic_reset: Clear program/variables but keep interpreter
 *============================================================================*/

/**
 * @brief Initialize a new BASIC interpreter
 *
 * Allocates and configures all interpreter resources:
 * - Memory buffer (default 64KB, configurable)
 * - I/O streams (default stdin/stdout)
 * - Terminal settings
 * - RND state initialization
 *
 * Memory Layout After Init:
 * ```
 *   +------------------+
 *   | (empty program)  | <- program_start = 0
 *   +------------------+
 *   | (empty vars)     | <- program_end, var_start
 *   +------------------+
 *   | (empty arrays)   | <- array_start
 *   +------------------+
 *   |                  |
 *   |  (free space)    |
 *   |                  |
 *   +------------------+
 *   | (empty strings)  | <- string_start
 *   +------------------+
 *                        <- string_end = top of memory
 * ```
 *
 * @param config Configuration options (NULL for defaults)
 * @return New interpreter state, or NULL on allocation failure
 *
 * Example:
 * @code
 *     basic_config_t config = {
 *         .memory_size = 32768,  // 32KB
 *         .terminal_width = 80
 *     };
 *     basic_state_t *state = basic_init(&config);
 *     if (!state) {
 *         fprintf(stderr, "Failed to initialize BASIC\n");
 *         exit(1);
 *     }
 * @endcode
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

/**
 * @brief Free interpreter state and all resources
 *
 * Releases all memory allocated by basic_init().
 * After calling this, the state pointer is invalid.
 *
 * @param state Interpreter state to free (NULL is safe)
 */
void basic_free(basic_state_t *state) {
    if (state) {
        free(state->memory);
        free(state);
    }
}

/**
 * @brief Reset interpreter to initial state
 *
 * Clears the program, variables, arrays, and strings while
 * keeping the interpreter instance. Used by NEW command.
 *
 * After reset:
 * - Program memory is empty
 * - All variables are cleared
 * - FOR/GOSUB stacks are empty
 * - DATA pointer is reset to beginning
 * - User functions are cleared
 * - RND is re-initialized
 *
 * Does NOT change:
 * - Memory size
 * - I/O streams
 * - Terminal settings
 *
 * @param state Interpreter state to reset
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


/*============================================================================
 * MEMORY MANAGEMENT
 *============================================================================*/

/**
 * @brief Get free memory available for program/variables
 *
 * Returns the bytes between the end of arrays (growing up) and
 * the start of strings (growing down). This is the FRE() function.
 *
 * Memory Layout:
 * ```
 *   [Program][Variables][Arrays]  <-FREE->  [Strings]
 *                              ^            ^
 *                         array_start   string_start
 *
 *   free_memory = string_start - array_start
 * ```
 *
 * @param state Interpreter state
 * @return Bytes of free memory, or 0 if state is NULL or out of memory
 */
uint16_t basic_free_memory(basic_state_t *state) {
    if (!state) return 0;
    /* Free memory is between end of arrays and start of strings */
    if (state->string_start <= state->array_start) return 0;
    return state->string_start - state->array_start;
}


/*============================================================================
 * OUTPUT FUNCTIONS
 *
 * These functions handle output to the terminal, including the
 * startup banner, OK prompt, and error messages.
 *============================================================================*/

/**
 * @brief Print the BASIC startup banner
 *
 * Displays the Microsoft BASIC banner with proper attribution:
 * ```
 * MICROSOFT BASIC REV. 4.0 - ALTAIR VERSION
 * [8K VERSION]
 * COPYRIGHT 1976 BY MICROSOFT
 * C VERSION COPYRIGHT 2025 BY TIM BUCHALKA
 *
 * XXXXX BYTES FREE
 * ```
 *
 * @param state Interpreter state
 */
void basic_print_banner(basic_state_t *state) {
    if (!state || !state->output) return;
    fprintf(state->output, "\nMICROSOFT BASIC REV. 4.0 - ALTAIR VERSION\n");
    fprintf(state->output, "[8K VERSION]\n");
    fprintf(state->output, "COPYRIGHT 1976 BY MICROSOFT\n");
    fprintf(state->output, "C VERSION COPYRIGHT 2025 BY TIM BUCHALKA\n\n");
    fprintf(state->output, "%u BYTES FREE\n\n", basic_free_memory(state));
}

/**
 * @brief Print the OK prompt
 *
 * Displayed after successful command execution in direct mode.
 *
 * @param state Interpreter state
 */
void basic_print_ok(basic_state_t *state) {
    if (!state || !state->output) return;
    fprintf(state->output, "OK\n");
}

/**
 * @brief Print an error message
 *
 * Formats and displays an error in the standard BASIC format:
 * ```
 * ?XX ERROR IN line
 * ```
 *
 * If line is 0xFFFF (direct mode), the "IN line" part is omitted.
 *
 * @param state Interpreter state
 * @param err Error code to display
 * @param line Line number where error occurred (0xFFFF for direct mode)
 */
void basic_print_error(basic_state_t *state, basic_error_t err, uint16_t line) {
    if (!state || !state->output) return;

    /* Original BASIC prints error on a new line */
    fprintf(state->output, "\n?%s ERROR", error_code_string(err));
    if (line != 0xFFFF && line != 0) {
        fprintf(state->output, " IN %u", line);
    }
    fprintf(state->output, "\n");
}


/*============================================================================
 * LINE PARSING
 *
 * When the user types a line, we first check if it starts with a
 * line number. If so, it's stored in the program. Otherwise, it's
 * executed immediately.
 *============================================================================*/

/**
 * @brief Parse a line number from the start of a string
 *
 * Skips leading whitespace, then parses decimal digits.
 * Line numbers must be 0-65529.
 *
 * @param line Input line to parse
 * @param[in,out] pos Current position; updated to point past the number
 * @return Line number parsed (1-65529), or 0 if no valid number found
 *
 * Example:
 * @code
 *     size_t pos = 0;
 *     uint16_t num = parse_line_number("  100 PRINT", &pos);
 *     // num = 100, pos = 5 (pointing at space before PRINT)
 * @endcode
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


/*============================================================================
 * STATEMENT EXECUTION
 *
 * The execute_statement() function is the main dispatcher for all
 * BASIC statements. It looks at the first token and branches to
 * the appropriate handler.
 *============================================================================*/

/**
 * @brief Execute a single tokenized statement
 *
 * This is the core statement dispatcher. It handles all BASIC statements
 * by examining the first token and calling the appropriate handler.
 *
 * @param state Interpreter state
 * @param tokenized Tokenized statement text
 * @param len Length of tokenized text
 * @return ERR_NONE on success, error code on failure
 *
 * Forward declaration - implementation is after basic_execute_line.
 */
static basic_error_t execute_statement(basic_state_t *state,
                                       const uint8_t *tokenized, size_t len);


/*============================================================================
 * DIRECT MODE EXECUTION
 *
 * When the user types a line at the OK prompt, basic_execute_line()
 * processes it. If it has a line number, it's stored in the program.
 * Otherwise, it's executed immediately.
 *============================================================================*/

/**
 * @brief Execute a single line of BASIC input
 *
 * This is the main entry point for user input. Handles both:
 * - Program lines (with line number): stored for later execution
 * - Direct commands (no line number): executed immediately
 *
 * Flow:
 * 1. Skip leading whitespace
 * 2. Check for line number at start
 * 3. Tokenize the rest of the line
 * 4. If line number: store in program (or delete if line is empty)
 * 5. If no line number: execute immediately
 *
 * @param state Interpreter state
 * @param line Raw input line from user
 * @return true on success, false on error
 *
 * Example (program line):
 * @code
 *     basic_execute_line(state, "100 PRINT \"HELLO\"");
 *     // Stores tokenized line at line number 100
 * @endcode
 *
 * Example (direct command):
 * @code
 *     basic_execute_line(state, "PRINT 2+2");
 *     // Immediately prints: 4
 * @endcode
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

/**
 * @brief Execute a tokenized statement
 *
 * This is the core statement dispatcher. It examines the first token
 * of the statement and branches to the appropriate handler.
 *
 * Statement Categories:
 *
 * **Flow Control (flow.c):**
 * - GOTO, GOSUB, RETURN, FOR, NEXT, IF, ON, END, STOP, CONT
 *
 * **I/O Operations (io.c):**
 * - PRINT, INPUT, READ, DATA, RESTORE
 *
 * **Variables and Memory:**
 * - LET (implicit or explicit), DIM, POKE, DEF
 *
 * **Program Management:**
 * - LIST, RUN, NEW, CLEAR, CLOAD, CSAVE
 *
 * **Miscellaneous:**
 * - REM (comment, skipped), NULL
 *
 * @param state Interpreter state
 * @param tokenized Tokenized statement bytes
 * @param len Length of tokenized data
 * @return ERR_NONE on success, error code on failure
 *
 * Note: This function may modify state->text_ptr for flow control
 * statements (GOTO, GOSUB, NEXT, etc.). The caller checks if text_ptr
 * changed to determine whether to advance normally or let the
 * statement control flow.
 */
static basic_error_t execute_statement(basic_state_t *state,
                                       const uint8_t *tokenized, size_t len) {
    if (!state || !tokenized || len == 0) return ERR_NONE;

    size_t pos = 0;

    /* Skip leading whitespace */
    while (pos < len && tokenized[pos] == ' ') pos++;

    /* Empty statement is not an error */
    if (pos >= len || tokenized[pos] == '\0') {
        return ERR_NONE;
    }

    /* Get the command token (first non-space character) */
    uint8_t cmd = tokenized[pos];

    /*
     * Main statement dispatch switch.
     *
     * Each case handles a specific statement type. Complex statements
     * delegate to functions in other modules (flow.c, io.c, etc.).
     */
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
                        /* Check if this is a string comparison (result is numeric) */
                        /* Look ahead past $ and any array subscript to see if comparison follows */
                        size_t look_pos = pos + 1;  /* Skip $ */
                        /* Skip any remaining variable name chars in look_pos */
                        while (look_pos < len && isalnum(tokenized[look_pos])) look_pos++;
                        if (look_pos < len && tokenized[look_pos] == '(') {
                            /* Skip array subscript */
                            int paren_depth = 1;
                            look_pos++;
                            while (look_pos < len && paren_depth > 0) {
                                if (tokenized[look_pos] == '(') paren_depth++;
                                else if (tokenized[look_pos] == ')') paren_depth--;
                                look_pos++;
                            }
                        }
                        /* Skip spaces */
                        while (look_pos < len && tokenized[look_pos] == ' ') look_pos++;
                        /* Check for comparison operator */
                        bool is_comparison = false;
                        if (look_pos < len) {
                            uint8_t next_ch = tokenized[look_pos];
                            if (next_ch == TOK_LT || next_ch == TOK_GT || next_ch == TOK_EQ ||
                                next_ch == '<' || next_ch == '>' || next_ch == '=') {
                                is_comparison = true;
                            }
                        }

                        if (is_comparison) {
                            /* String comparison - result is numeric, use eval_expression */
                            pos = save_pos;
                            basic_error_t err;
                            size_t consumed;
                            mbf_t val = eval_expression(state, tokenized + pos, len - pos,
                                                        &consumed, &err);
                            if (err != ERR_NONE) return err;
                            pos += consumed;
                            io_print_number(state, val);
                        } else {
                            /* Pure string expression - use eval_string_desc */
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
                SKIP_EXTRA_VAR_CHARS();  /* Skip remaining chars in long var names */
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
                    SKIP_EXTRA_VAR_CHARS();  /* Skip remaining chars in long var names */
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
                SKIP_EXTRA_VAR_CHARS();  /* Skip remaining chars in long var names */

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
                    SKIP_EXTRA_VAR_CHARS();  /* Skip remaining chars in long var names */

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
                    SKIP_EXTRA_VAR_CHARS();  /* Skip remaining chars in long var names */
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
                /* Calculate return position (after this statement) */
                /* Same logic as regular GOSUB */
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

                return stmt_on_gosub(state, value, lines, num_lines,
                                     state->current_line, return_ptr);
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
                    SKIP_EXTRA_VAR_CHARS();  /* Skip remaining chars in long var names */
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


/*============================================================================
 * PROGRAM EXECUTION
 *
 * basic_run_program() is the main execution loop. It processes
 * statements one at a time until the program ends or an error occurs.
 *============================================================================*/

/**
 * @brief Execute the current BASIC program
 *
 * This is the main program execution loop. It runs from the current
 * text_ptr position until:
 * - The program ends (END statement or runs off the end)
 * - An error occurs
 * - The user presses Ctrl-C
 * - A STOP statement is executed
 *
 * Execution Loop:
 * ```
 * while (running) {
 *     1. Check for Ctrl-C interrupt
 *     2. Find the line containing text_ptr
 *     3. Extract current statement (up to ':' or end of line)
 *     4. execute_statement()
 *     5. If text_ptr unchanged, advance to next statement
 *     6. If text_ptr changed (GOTO, etc.), loop back without advancing
 * }
 * ```
 *
 * Statement Separation:
 * - Multiple statements per line are separated by ':'
 * - After executing a statement, we check for ':'
 * - If found, continue on same line; otherwise, move to next line
 *
 * Flow Control:
 * - GOTO/GOSUB/NEXT may modify text_ptr directly
 * - We detect this by comparing text_ptr before and after execution
 * - If changed, we don't advance - the statement handled it
 *
 * Interrupt Handling:
 * - Ctrl-C sets g_interrupt_flag
 * - We check this at the start of each iteration
 * - If set, print "BREAK IN line" and stop with can_continue=true
 *
 * @param state Interpreter state with text_ptr set to starting position
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


/*============================================================================
 * INTERACTIVE MODE
 *
 * basic_run_interactive() provides the classic BASIC experience:
 * display banner, print OK prompt, and process commands until EOF.
 *============================================================================*/

/**
 * @brief Run interactive BASIC interpreter
 *
 * This is the main entry point for interactive use. It:
 * 1. Prints the startup banner
 * 2. Prints "OK"
 * 3. Reads a line of input
 * 4. Executes it (store or run depending on line number)
 * 5. Prints "OK" again (if not running a program)
 * 6. Repeats until EOF
 *
 * @param state Interpreter state
 *
 * Example session:
 * ```
 * ALTAIR BASIC REV. 4.0
 * [8K VERSION]
 * COPYRIGHT 1976 BY MITS INC.
 *
 * 65279 BYTES FREE
 *
 * OK
 * 10 PRINT "HELLO"
 * OK
 * RUN
 * HELLO
 * OK
 * ```
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


/*============================================================================
 * FILE OPERATIONS
 *
 * These functions load and save BASIC programs to/from text files.
 * They are the implementation behind CLOAD and CSAVE commands.
 *============================================================================*/

/**
 * @brief Load a BASIC program from a text file
 *
 * Reads a .bas file and loads it into the interpreter.
 * Each line in the file should be a numbered BASIC line.
 *
 * The file format is plain text:
 * ```
 * 10 PRINT "HELLO"
 * 20 GOTO 10
 * ```
 *
 * @param state Interpreter state
 * @param filename Path to the .bas file
 * @return true on success, false on error (file not found, syntax error)
 *
 * Note: Clears any existing program before loading.
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

/**
 * @brief Save the current BASIC program to a text file
 *
 * Writes the program to a .bas file in plain text format.
 * The output is suitable for loading with basic_load_file().
 *
 * Lines are detokenized (keywords expanded) before writing.
 *
 * @param state Interpreter state
 * @param filename Path to save to
 * @return true on success, false on error (can't create file)
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
