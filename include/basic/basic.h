/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file basic.h
 * @brief Altair 8K BASIC 4.0 Interpreter - Main Header
 *
 * This is the primary interface for the Altair 8K BASIC interpreter, a 100%
 * compatible C17 implementation of Microsoft's original 8080 assembly version.
 * The implementation produces identical output to the 1976 original when given
 * the same input.
 *
 * ARCHITECTURE OVERVIEW
 * =====================
 * The interpreter consists of several major subsystems:
 *
 * 1. TOKENIZER (tokenizer.c)
 *    Converts ASCII BASIC source into compact tokenized form. Keywords become
 *    single bytes (0x81-0xC6), reducing memory usage and speeding parsing.
 *
 * 2. PARSER/EVALUATOR (parser.c, evaluator.c)
 *    Recursive-descent expression parser with proper operator precedence.
 *    Handles numeric expressions, string expressions, and function calls.
 *
 * 3. INTERPRETER (interpreter.c)
 *    The main execution engine. Executes tokenized BASIC statements, manages
 *    control flow, and coordinates all other subsystems.
 *
 * 4. MEMORY MANAGEMENT (variables.c, arrays.c, strings.c, program.c)
 *    Manages the flat 64KB memory space exactly like the original:
 *    - Program storage with linked-list line structure
 *    - Simple variable table (6 bytes per variable)
 *    - Array storage with dimension headers
 *    - String space (heap growing downward from top of memory)
 *
 * 5. MATH (mbf.c, mbf_arith.c, mbf_trig.c, rnd.c)
 *    Microsoft Binary Format (MBF) floating-point, NOT IEEE 754.
 *    Custom implementation matches original precision and rounding.
 *
 * MEMORY LAYOUT (matches original exactly)
 * ========================================
 *
 *  +------------------------+ <- 0x0000 (memory[0])
 *  |   (Reserved/Unused)    |
 *  +------------------------+ <- program_start
 *  |                        |
 *  |   Program Storage      |  Lines stored as: [link_lo][link_hi][line_lo][line_hi][tokens...][0x00]
 *  |   (linked list)        |
 *  |                        |
 *  +------------------------+ <- program_end / var_start
 *  |                        |
 *  |   Simple Variables     |  6 bytes each: [name1][name2][value: 4 bytes]
 *  |                        |
 *  +------------------------+ <- array_start
 *  |                        |
 *  |   Arrays               |  Header + elements, grows upward
 *  |                        |
 *  +------------------------+
 *  |                        |
 *  |   (Free Space)         |  FRE() returns size of this gap
 *  |                        |
 *  +------------------------+ <- string_end (grows downward)
 *  |                        |
 *  |   String Space         |  Strings allocated from top, garbage collected
 *  |                        |
 *  +------------------------+ <- string_start (top of memory - 1)
 *  |   (Reserved)           |
 *  +------------------------+ <- memory_size
 *
 * COMPATIBILITY NOTES
 * ===================
 * - Numbers use 4-byte MBF format, not IEEE 754 floats
 * - RND uses the exact 24-bit LCG algorithm from Microsoft BASIC (A=16598013, C=12820163)
 * - Hardware I/O (INP, OUT, WAIT, USR) stubs with warnings for portability
 * - PEEK/POKE work against the interpreter's memory space
 * - All error messages match original text exactly
 *
 * Original BASIC: Copyright (c) 1976 Microsoft (Bill Gates, Paul Allen, Monte Davidoff)
 */

#ifndef BASIC8K_BASIC_H
#define BASIC8K_BASIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "mbf.h"
#include "tokens.h"
#include "errors.h"

/* ============================================================================
 * VERSION AND CONFIGURATION CONSTANTS
 * ============================================================================ */

/** Major version number - matches original BASIC 4.x */
#define BASIC8K_VERSION_MAJOR   4
/** Minor version number */
#define BASIC8K_VERSION_MINOR   0
/** Version string displayed at startup */
#define BASIC8K_VERSION_STRING  "4.0"

/**
 * Minimum usable memory in bytes.
 * Below this, there's not enough room for program + variables.
 */
#define BASIC8K_MIN_MEMORY      4096

/**
 * Maximum addressable memory (64KB).
 * The original 8080 was limited to 16-bit addresses.
 */
#define BASIC8K_MAX_MEMORY      65536

/** Default memory size if not specified */
#define BASIC8K_DEFAULT_MEMORY  65536

/** Default terminal width for PRINT formatting and TAB() */
#define BASIC8K_DEFAULT_WIDTH   72
/** Minimum terminal width */
#define BASIC8K_MIN_WIDTH       16
/** Maximum terminal width (one byte) */
#define BASIC8K_MAX_WIDTH       255


/* ============================================================================
 * CORE DATA TYPES
 * ============================================================================ */

/**
 * String variable descriptor.
 *
 * Strings in BASIC are not stored inline with variables. Instead, variables
 * hold a descriptor pointing to the string data in the string heap. This allows
 * efficient string operations without copying data for temporary results.
 *
 * Layout matches original 8080 format for compatibility:
 * - Byte 0: Length (0-255 characters)
 * - Byte 1: Reserved (alignment)
 * - Bytes 2-3: 16-bit pointer into string space
 */
typedef struct {
    uint8_t length;         /**< String length (0-255) */
    uint8_t _reserved;      /**< Padding for alignment */
    uint16_t ptr;           /**< Offset into memory[] where string data starts */
} string_desc_t;

/**
 * Union representing either a numeric or string value.
 *
 * In the original BASIC, variables are 6 bytes: 2 for name, 4 for value.
 * Numeric values use all 4 bytes for MBF format. String values use the
 * string_desc_t format (length + pointer).
 */
typedef union {
    mbf_t numeric;          /**< Numeric value in Microsoft Binary Format */
    string_desc_t string;   /**< String descriptor (length + pointer) */
} basic_value_t;

/**
 * Simple variable storage entry.
 *
 * Variables are stored in a contiguous table in memory. Each entry is exactly
 * 6 bytes to match the original format. The name uses 1-2 characters; if only
 * 1 char, the second is NUL. String variables have '$' as the last character.
 *
 * Examples:
 *   "A\0"   - Numeric variable A
 *   "X1"    - Numeric variable X1
 *   "N$"    - String variable N$
 *   "A$"    - String variable A$ (different from numeric A)
 */
typedef struct {
    char name[2];           /**< Variable name (1-2 chars, '$'=string) */
    basic_value_t value;    /**< Value (4 bytes) */
} basic_variable_t;


/* ============================================================================
 * CONTROL FLOW STRUCTURES
 * ============================================================================ */

/**
 * FOR loop stack entry.
 *
 * When a FOR statement executes, it pushes an entry describing the loop.
 * NEXT pops this entry to get loop parameters. The stack supports 16 levels
 * of nested FOR loops (matching the original).
 *
 * The 'var' pointer points directly to the loop variable in the variable table,
 * avoiding repeated name lookups during iteration.
 */
typedef struct {
    uint16_t line_number;   /**< Line number containing the NEXT statement */
    uint16_t text_ptr;      /**< Byte offset where loop body starts */
    uint8_t *var;           /**< Direct pointer to loop variable value */
    mbf_t limit;            /**< TO value - loop continues while var <= limit (or >= for STEP<0) */
    mbf_t step;             /**< STEP value (default 1) */
} for_entry_t;

/**
 * GOSUB return stack entry.
 *
 * GOSUB pushes the return address; RETURN pops it. Supports 16 levels of
 * nested subroutine calls.
 */
typedef struct {
    uint16_t line_number;   /**< Line to return to after RETURN */
    uint16_t text_ptr;      /**< Exact position within line to resume */
} gosub_entry_t;


/* ============================================================================
 * RANDOM NUMBER GENERATOR STATE
 * ============================================================================ */

/**
 * RND function state.
 *
 * Altair 8K BASIC 4.0 uses a two-stage table-based pseudo-random generator:
 * 1. Multiply by MULTIPLIER_TABLE[counter3 & 7]
 * 2. Add ADDEND_TABLE[counter2 & 3] (index 0 is never used during generation)
 * 3. XOR with 0x4F, swap bytes, set exponent to 0x80
 * 4. Normalize (mixing in the old exponent)
 *
 * Three counters control the sequence:
 * - counter1: Wraps at 0xAB (171), triggers extra byte scrambling
 * - counter2: Cycles 1, 2, 3, 1, 2, 3... for addend selection
 * - counter3: (RST5_result + counter3) & 7 for multiplier selection
 *
 * RND(0) returns previous value, RND(negative) reseeds, RND(positive) advances.
 */
typedef struct {
    uint8_t counter1;       /**< Main counter, wraps at 0xAB (171 decimal) */
    uint8_t counter2;       /**< Addend table index, mod 4 (but 0 not used) */
    uint8_t counter3;       /**< Multiplier table index, mod 8 */
    mbf_t last_value;       /**< Previous result (for RND(0)) and current seed */
} rnd_state_t;


/* ============================================================================
 * INTERPRETER CONFIGURATION AND STATE
 * ============================================================================ */

/**
 * Configuration for initializing a new interpreter.
 *
 * Pass this to basic_init() to customize the interpreter. All fields have
 * sensible defaults if zero-initialized.
 */
typedef struct {
    uint32_t memory_size;   /**< Memory size (default: 65536) */
    uint8_t terminal_width; /**< Terminal width for TAB/PRINT (default: 72) */
    bool want_trig;         /**< Enable SIN/COS/TAN/ATN? (original prompted for this) */
    FILE *input;            /**< Input stream (default: stdin) */
    FILE *output;           /**< Output stream (default: stdout) */
} basic_config_t;

/**
 * Main interpreter state structure.
 *
 * This holds ALL state for a running BASIC interpreter instance. Multiple
 * independent instances can run simultaneously. The design follows the
 * original's use of memory regions and stacks.
 *
 * IMPORTANT: Fields prefixed with an underscore are internal implementation
 * details. Access through the provided API functions.
 */
typedef struct {
    /* -------------------------------------------------------------------------
     * Memory Management
     * ------------------------------------------------------------------------- */

    /** Main memory buffer - all program, variables, arrays, strings stored here */
    uint8_t *memory;

    /** Total size of memory buffer */
    uint32_t memory_size;

    /* Memory region boundaries (byte offsets into memory[]) */
    uint16_t program_start;     /**< First byte of program area */
    uint16_t program_end;       /**< End of program, start of variables */
    uint16_t var_start;         /**< Start of simple variable table */
    uint16_t array_start;       /**< Start of array storage */
    uint16_t string_start;      /**< Bottom of string space (top of memory) */
    uint16_t string_end;        /**< Current end of used string space (grows down) */

    /** Number of simple variables currently allocated */
    uint16_t var_count_;

    /* -------------------------------------------------------------------------
     * Execution State
     * ------------------------------------------------------------------------- */

    /** Current line number being executed (0xFFFF = direct mode) */
    uint16_t current_line;

    /** Current byte position within program memory */
    uint16_t text_ptr;

    /** Current DATA statement line for READ */
    uint16_t data_line;

    /** Current position within DATA statement */
    uint16_t data_ptr;

    /** Floating-point accumulator (like original's FACCUM register) */
    mbf_t fac;

    /** Type of value in accumulator (0=numeric, 0xFF=string) */
    uint8_t value_type;

    /** Random number generator state */
    rnd_state_t rnd;

    /* -------------------------------------------------------------------------
     * Control Flow Stacks
     * ------------------------------------------------------------------------- */

    /** FOR/NEXT loop stack (max 16 nested loops) */
    for_entry_t for_stack[16];
    /** FOR stack pointer (0 = empty) */
    int for_sp;

    /** GOSUB/RETURN stack (max 16 nested calls) */
    gosub_entry_t gosub_stack[16];
    /** GOSUB stack pointer (0 = empty) */
    int gosub_sp;

    /* -------------------------------------------------------------------------
     * User-Defined Functions (DEF FN)
     * ------------------------------------------------------------------------- */

    /**
     * User function definitions (FNA through FNZ).
     * The original supported 26 user functions. Each stores the location
     * of its DEF FN statement for evaluation when called.
     */
    struct {
        char name;          /**< Function letter (A-Z), 0 if not defined */
        uint16_t line;      /**< Line number of DEF FN statement */
        uint16_t ptr;       /**< Byte offset to parameter list */
    } user_funcs[26];

    /* -------------------------------------------------------------------------
     * Terminal/Output State
     * ------------------------------------------------------------------------- */

    uint8_t terminal_x;         /**< Current column (0-based) for TAB() */
    uint8_t terminal_width;     /**< Line width for wrapping */
    uint8_t null_count;         /**< NULL statement padding count */
    bool output_suppressed;     /**< Ctrl-O toggle suppresses output */
    bool want_trig;             /**< Trig functions enabled */

    /** Input stream (usually stdin) */
    FILE *input;
    /** Output stream (usually stdout) */
    FILE *output;

    /* -------------------------------------------------------------------------
     * Execution Flags
     * ------------------------------------------------------------------------- */

    bool running;           /**< true while program is executing */
    bool can_continue;      /**< true if CONT command is allowed */
    uint16_t cont_line;     /**< Line to continue from after STOP/Ctrl-C */
    uint16_t cont_ptr;      /**< Position within line to continue from */

    /* Hardware stub warning flags - warn only once per session */
    bool warned_inp;        /**< Already warned about INP() stub */
    bool warned_out;        /**< Already warned about OUT stub */
    bool warned_wait;       /**< Already warned about WAIT stub */
    bool warned_usr;        /**< Already warned about USR() stub */

} basic_state_t;


/* ============================================================================
 * CORE API - INTERPRETER LIFECYCLE
 * ============================================================================ */

/**
 * Create and initialize a new BASIC interpreter instance.
 *
 * Allocates memory and initializes all state. The config parameter allows
 * customization; pass NULL for defaults (64KB memory, stdin/stdout I/O).
 *
 * @param config  Configuration options, or NULL for defaults
 * @return        New interpreter state, or NULL on allocation failure
 *
 * @note Caller must call basic_free() when done to release memory.
 *
 * Example:
 *   basic_config_t cfg = { .memory_size = 32768 };
 *   basic_state_t *basic = basic_init(&cfg);
 *   if (!basic) { fprintf(stderr, "Out of memory\n"); exit(1); }
 */
basic_state_t *basic_init(const basic_config_t *config);

/**
 * Free all resources associated with an interpreter.
 *
 * Releases the memory buffer and all internal state. The state pointer
 * becomes invalid after this call.
 *
 * @param state  Interpreter to free (may be NULL)
 */
void basic_free(basic_state_t *state);

/**
 * Reset interpreter to initial state.
 *
 * Equivalent to the NEW command: clears program, variables, and stacks.
 * Memory allocation is preserved.
 *
 * @param state  Interpreter to reset
 */
void basic_reset(basic_state_t *state);


/* ============================================================================
 * EXECUTION API
 * ============================================================================ */

/**
 * Run the interactive interpreter loop.
 *
 * Displays the startup banner ("MEMORY SIZE?" etc.) and enters command mode.
 * This function returns only when the user types BYE or input ends.
 *
 * @param state  Initialized interpreter state
 */
void basic_run_interactive(basic_state_t *state);

/**
 * Execute a single line of BASIC.
 *
 * Used for direct mode commands. The line is tokenized and executed
 * immediately. If it's a numbered line, it's stored in the program.
 *
 * @param state  Interpreter state
 * @param line   ASCII line to execute (may include line number)
 * @return       true on success, false on error (error already printed)
 */
bool basic_execute_line(basic_state_t *state, const char *line);

/**
 * Start executing the stored program.
 *
 * Equivalent to the RUN command. Clears variables and begins execution
 * from the first line (or specified line if RUN with line number).
 *
 * @param state  Interpreter state with program loaded
 */
void basic_run_program(basic_state_t *state);


/* ============================================================================
 * FILE I/O
 * ============================================================================ */

/**
 * Load a BASIC program from a file.
 *
 * Implements CLOAD. The file should contain ASCII BASIC with line numbers.
 * Clears any existing program before loading.
 *
 * @param state     Interpreter state
 * @param filename  Path to .bas file
 * @return          true on success, false on error
 */
bool basic_load_file(basic_state_t *state, const char *filename);

/**
 * Save the current program to a file.
 *
 * Implements CSAVE. Writes ASCII BASIC with line numbers.
 *
 * @param state     Interpreter state
 * @param filename  Path to save to
 * @return          true on success, false on error
 */
bool basic_save_file(basic_state_t *state, const char *filename);


/* ============================================================================
 * INTERRUPT HANDLING
 * ============================================================================ */

/**
 * Install Ctrl-C interrupt handler.
 *
 * The handler will set a flag that causes program execution to stop
 * gracefully at the next statement boundary.
 *
 * @param state  Interpreter that should handle the interrupt
 */
void basic_setup_interrupt(basic_state_t *state);

/**
 * Remove the Ctrl-C interrupt handler.
 *
 * Call this before freeing the interpreter state.
 */
void basic_clear_interrupt(void);


/* ============================================================================
 * PROGRAM LISTING
 * ============================================================================ */

/**
 * List program lines.
 *
 * Implements the LIST command. Outputs to the interpreter's output stream.
 *
 * @param state  Interpreter state
 * @param start  First line to list (0 = first line)
 * @param end    Last line to list (0xFFFF = last line)
 */
void basic_list_program(basic_state_t *state, uint16_t start, uint16_t end);


/* ============================================================================
 * OUTPUT HELPERS
 * ============================================================================ */

/** Print the startup banner ("ALTAIR BASIC REV. 4.0" etc.) */
void basic_print_banner(basic_state_t *state);

/** Print the "OK" ready prompt */
void basic_print_ok(basic_state_t *state);

/**
 * Print an error message.
 *
 * Formats the error like the original: "?XX ERROR IN line"
 *
 * @param state  Interpreter state
 * @param err    Error code
 * @param line   Line number (0 for direct mode)
 */
void basic_print_error(basic_state_t *state, basic_error_t err, uint16_t line);


/* ============================================================================
 * EXPRESSION EVALUATION
 *
 * These functions parse and evaluate BASIC expressions from tokenized text.
 * They handle operator precedence, parentheses, function calls, and
 * variable/array references.
 * ============================================================================ */

/**
 * Evaluate a numeric expression.
 *
 * Parses and evaluates a numeric expression, returning the result in MBF format.
 * Handles operators (+, -, *, /, ^, AND, OR, NOT, comparisons), function calls,
 * and variable references.
 *
 * @param state     Interpreter state (for variable lookup, function calls)
 * @param text      Tokenized expression bytes
 * @param len       Length of text buffer
 * @param consumed  OUT: Number of bytes consumed from text
 * @param error     OUT: Error code (ERR_NONE on success)
 * @return          Evaluated value in MBF format
 */
mbf_t eval_expression(basic_state_t *state, const uint8_t *text, size_t len,
                      size_t *consumed, basic_error_t *error);

/**
 * Evaluate a string expression.
 *
 * Returns pointer to the string data in string space. Valid until next
 * string operation or garbage collection.
 *
 * @param state     Interpreter state
 * @param text      Tokenized expression
 * @param len       Buffer length
 * @param consumed  OUT: Bytes consumed
 * @param error     OUT: Error code
 * @return          Pointer to string data, or NULL on error
 */
const char *eval_string_expression(basic_state_t *state, const uint8_t *text,
                                   size_t len, size_t *consumed,
                                   basic_error_t *error);

/**
 * Evaluate a string expression, returning a descriptor.
 *
 * Like eval_string_expression but returns the full descriptor with length.
 */
string_desc_t eval_string_desc(basic_state_t *state, const uint8_t *text,
                               size_t len, size_t *consumed,
                               basic_error_t *error);


/* ============================================================================
 * VARIABLE MANAGEMENT (memory/variables.c)
 *
 * Variables are stored in a contiguous table. Each entry is 6 bytes:
 * 2 bytes for name, 4 bytes for value. String variables have '$' as
 * the second character of their name.
 * ============================================================================ */

/** Check if a variable name is a string variable (ends with $) */
bool var_is_string(const char *name);

/** Find a variable by name. Returns pointer to value bytes, or NULL if not found. */
uint8_t *var_find(basic_state_t *state, const char *name);

/** Create a new variable. Returns pointer to value bytes, or NULL if out of memory. */
uint8_t *var_create(basic_state_t *state, const char *name);

/** Find or create a variable. Returns pointer to value bytes. */
uint8_t *var_get_or_create(basic_state_t *state, const char *name);

/** Get value of a numeric variable. Returns 0 if not found. */
mbf_t var_get_numeric(basic_state_t *state, const char *name);

/** Set value of a numeric variable. Creates if needed. */
bool var_set_numeric(basic_state_t *state, const char *name, mbf_t value);

/** Get value of a string variable. Returns empty descriptor if not found. */
string_desc_t var_get_string(basic_state_t *state, const char *name);

/** Set value of a string variable. Creates if needed. */
bool var_set_string(basic_state_t *state, const char *name, string_desc_t desc);

/** Clear all variables (for NEW/RUN). */
void var_clear_all(basic_state_t *state);

/** Return count of allocated variables. */
int var_count(basic_state_t *state);


/* ============================================================================
 * ARRAY MANAGEMENT (memory/arrays.c)
 *
 * Arrays are stored after simple variables. Each array has a header with
 * name, dimensions, and element count, followed by the elements.
 * Default dimension is 10 if not DIM'd. Arrays can be 1D or 2D.
 * ============================================================================ */

/** Find an array by name. Returns pointer to header, or NULL. */
uint8_t *array_find(basic_state_t *state, const char *name);

/** Create a new array with specified dimensions. */
uint8_t *array_create(basic_state_t *state, const char *name, int dim1, int dim2);

/** Get pointer to an array element. Returns NULL if out of bounds. */
uint8_t *array_get_element(basic_state_t *state, const char *name,
                           int index1, int index2);

/** Get numeric value from array element. */
mbf_t array_get_numeric(basic_state_t *state, const char *name,
                        int index1, int index2);

/** Set numeric value in array element. */
bool array_set_numeric(basic_state_t *state, const char *name,
                       int index1, int index2, mbf_t value);

/** Get string descriptor from array element. */
string_desc_t array_get_string(basic_state_t *state, const char *name,
                               int index1, int index2);

/** Set string value in array element. */
bool array_set_string(basic_state_t *state, const char *name,
                      int index1, int index2, string_desc_t desc);

/** Clear all arrays. */
void array_clear_all(basic_state_t *state);


/* ============================================================================
 * STRING SPACE MANAGEMENT (memory/strings.c)
 *
 * Strings are stored in a heap that grows downward from the top of memory.
 * When space runs low, garbage collection compacts live strings.
 * ============================================================================ */

/** Initialize string space. */
void string_init(basic_state_t *state);

/** Allocate space for a string of given length. Returns offset, or 0 on failure. */
uint16_t string_alloc(basic_state_t *state, uint8_t length);

/** Create a string from a null-terminated C string. */
string_desc_t string_create(basic_state_t *state, const char *str);

/** Create a string from data with explicit length (may contain NUL). */
string_desc_t string_create_len(basic_state_t *state, const char *data, uint8_t length);

/** Get pointer to string data. Valid until next string operation. */
const char *string_get_data(basic_state_t *state, string_desc_t desc);

/** Create a copy of a string. */
string_desc_t string_copy(basic_state_t *state, string_desc_t src);

/** Concatenate two strings. */
string_desc_t string_concat(basic_state_t *state, string_desc_t a, string_desc_t b);

/** Compare two strings. Returns <0, 0, >0 like strcmp. */
int string_compare(basic_state_t *state, string_desc_t a, string_desc_t b);

/** LEFT$: Get leftmost n characters. */
string_desc_t string_left(basic_state_t *state, string_desc_t str, uint8_t n);

/** RIGHT$: Get rightmost n characters. */
string_desc_t string_right(basic_state_t *state, string_desc_t str, uint8_t n);

/** MID$: Get substring starting at position (1-based). */
string_desc_t string_mid(basic_state_t *state, string_desc_t str, uint8_t start, uint8_t n);

/** LEN: Get string length. */
uint8_t string_len(string_desc_t str);

/** ASC: Get ASCII code of first character. */
uint8_t string_asc(basic_state_t *state, string_desc_t str);

/** CHR$: Create single-character string from ASCII code. */
string_desc_t string_chr(basic_state_t *state, uint8_t ch);

/** VAL: Convert string to number. */
mbf_t string_val(basic_state_t *state, string_desc_t str);

/** STR$: Convert number to string. */
string_desc_t string_str(basic_state_t *state, mbf_t value);

/** Run garbage collection to compact string space. */
void string_garbage_collect(basic_state_t *state);

/** Get free bytes in string space. */
uint16_t string_free(basic_state_t *state);

/** Clear all strings (for NEW/RUN). */
void string_clear(basic_state_t *state);


/* ============================================================================
 * PROGRAM STORAGE (memory/program.c)
 *
 * Programs are stored as a linked list of lines. Each line has format:
 *   [link_lo][link_hi] - Offset to next line (0 = end)
 *   [line_lo][line_hi] - Line number
 *   [tokens...]        - Tokenized BASIC code
 *   [0x00]             - Line terminator
 * ============================================================================ */

/**
 * Insert or replace a program line.
 *
 * If the line already exists, it's replaced. Lines are kept in sorted order.
 * If tokenized is empty, the line is deleted.
 *
 * @param state         Interpreter state
 * @param line_num      Line number (1-65535)
 * @param tokenized     Tokenized line content
 * @param tokenized_len Length of tokenized content
 * @return              true on success, false on out of memory
 */
bool program_insert_line(basic_state_t *state, uint16_t line_num,
                         const uint8_t *tokenized, size_t tokenized_len);

/**
 * Get a program line by number.
 *
 * @param state     Interpreter state
 * @param line_num  Line to find
 * @param line_len  OUT: Length of tokenized content
 * @return          Pointer to tokenized content, or NULL if not found
 */
const uint8_t *program_get_line(basic_state_t *state, uint16_t line_num, size_t *line_len);

/** Get the first line number in the program, or 0 if empty. */
uint16_t program_first_line(basic_state_t *state);

/** Get the next line number after the given one, or 0 if none. */
uint16_t program_next_line(basic_state_t *state, uint16_t line_num);

/** Clear the entire program. */
void program_clear(basic_state_t *state);


/* ============================================================================
 * CONTROL FLOW STATEMENTS (statements/flow.c)
 * ============================================================================ */

/** GOTO: Jump to specified line. */
basic_error_t stmt_goto(basic_state_t *state, uint16_t line_num);

/** GOSUB: Call subroutine at line, saving return address. */
basic_error_t stmt_gosub(basic_state_t *state, uint16_t line_num,
                         uint16_t return_line, uint16_t return_ptr);

/** RETURN: Return from GOSUB. */
basic_error_t stmt_return(basic_state_t *state);

/** FOR: Initialize loop with variable, limit, and step. */
basic_error_t stmt_for(basic_state_t *state, const char *var_name,
                       mbf_t initial, mbf_t limit, mbf_t step,
                       uint16_t next_line, uint16_t next_ptr);

/** NEXT: Advance loop variable, continue or exit loop. */
basic_error_t stmt_next(basic_state_t *state, const char *var_name, bool *continue_loop);

/** Evaluate IF condition (non-zero = true). */
bool stmt_if_eval(mbf_t condition);

/** END: Stop program execution. */
basic_error_t stmt_end(basic_state_t *state);

/** STOP: Stop with ability to CONT. */
basic_error_t stmt_stop(basic_state_t *state, uint16_t line, uint16_t ptr);

/** CONT: Continue after STOP or Ctrl-C. */
basic_error_t stmt_cont(basic_state_t *state);

/** ON...GOTO: Computed GOTO. */
basic_error_t stmt_on_goto(basic_state_t *state, int value,
                           uint16_t *lines, int num_lines);

/** ON...GOSUB: Computed GOSUB. */
basic_error_t stmt_on_gosub(basic_state_t *state, int value,
                            uint16_t *lines, int num_lines,
                            uint16_t return_line, uint16_t return_ptr);

/** POP: Discard top of GOSUB stack. */
basic_error_t stmt_pop(basic_state_t *state);

/** Clear both FOR and GOSUB stacks. */
void stmt_clear_stacks(basic_state_t *state);


/* ============================================================================
 * I/O STATEMENTS (statements/io.c)
 * ============================================================================ */

/** Output a single character, handling terminal width. */
void io_putchar(basic_state_t *state, char ch);

/** Output a string with specified length. */
void io_print_string(basic_state_t *state, const char *str, size_t len);

/** Output a null-terminated C string. */
void io_print_cstring(basic_state_t *state, const char *str);

/** Output a newline. */
void io_newline(basic_state_t *state);

/** Print a numeric value with proper formatting. */
void io_print_number(basic_state_t *state, mbf_t value);

/** TAB to specified column (1-based). */
void io_tab(basic_state_t *state, int column);

/** SPC: Output specified number of spaces. */
void io_spc(basic_state_t *state, int count);

/** Read a line of input. Returns true if successful. */
bool io_input_line(basic_state_t *state, char *buf, size_t bufsize, size_t *len);

/** Parse a number from input string. Returns bytes consumed. */
size_t io_parse_number(const char *str, mbf_t *value);

/** Initialize DATA pointer to start of program. */
void io_data_init(basic_state_t *state);

/** RESTORE: Reset DATA pointer to beginning. */
basic_error_t stmt_restore(basic_state_t *state);

/** RESTORE to specific line. */
basic_error_t stmt_restore_line(basic_state_t *state, uint16_t line_num);

/** READ next numeric value from DATA. */
basic_error_t io_read_numeric(basic_state_t *state, mbf_t *value);

/** READ next string value from DATA. */
basic_error_t io_read_string(basic_state_t *state, string_desc_t *value);

/** NULL: Set padding null count for paper tape. */
basic_error_t stmt_null(basic_state_t *state, int count);

/** WIDTH: Set terminal width. */
basic_error_t stmt_width(basic_state_t *state, int width);

/** POS: Get current cursor column (1-based). */
int io_pos(basic_state_t *state);


/* ============================================================================
 * MISCELLANEOUS STATEMENTS (statements/misc.c)
 * ============================================================================ */

/** LET for simple numeric variable. */
basic_error_t stmt_let_numeric(basic_state_t *state, const char *var_name, mbf_t value);

/** LET for simple string variable. */
basic_error_t stmt_let_string(basic_state_t *state, const char *var_name, string_desc_t value);

/** LET for numeric array element. */
basic_error_t stmt_let_array_numeric(basic_state_t *state, const char *arr_name,
                                      int idx1, int idx2, mbf_t value);

/** LET for string array element. */
basic_error_t stmt_let_array_string(basic_state_t *state, const char *arr_name,
                                     int idx1, int idx2, string_desc_t value);

/** DIM: Dimension an array. */
basic_error_t stmt_dim(basic_state_t *state, const char *arr_name, int dim1, int dim2);

/** DEF FN: Define a user function. */
basic_error_t stmt_def_fn(basic_state_t *state, char fn_name,
                          uint16_t line, uint16_t ptr);

/** Look up a user function definition. */
basic_error_t stmt_fn_lookup(basic_state_t *state, char fn_name,
                             uint16_t *line, uint16_t *ptr);

/** POKE: Write byte to memory address. */
basic_error_t stmt_poke(basic_state_t *state, uint16_t address, uint8_t value);

/** PEEK: Read byte from memory address. */
uint8_t stmt_peek(basic_state_t *state, uint16_t address);

/** CLEAR: Clear variables and optionally set string space. */
basic_error_t stmt_clear(basic_state_t *state, int string_space);

/** NEW: Clear program and all data. */
basic_error_t stmt_new(basic_state_t *state);

/** RUN from specific line. */
basic_error_t stmt_run(basic_state_t *state, uint16_t start_line);

/** REM: No operation (comment). */
basic_error_t stmt_rem(void);

/** SWAP two numeric variables. */
basic_error_t stmt_swap_numeric(basic_state_t *state,
                                const char *var1, const char *var2);

/** SWAP two string variables. */
basic_error_t stmt_swap_string(basic_state_t *state,
                               const char *var1, const char *var2);

/** INP: Input from hardware port (stub). */
uint8_t stmt_inp(basic_state_t *state, uint8_t port);

/** OUT: Output to hardware port (stub). */
basic_error_t stmt_out(basic_state_t *state, uint8_t port, uint8_t value);

/** WAIT: Wait for hardware port condition (stub). */
basic_error_t stmt_wait(basic_state_t *state, uint8_t port, uint8_t mask, uint8_t xor_val);

/** USR: Call machine language routine (stub). */
mbf_t stmt_usr(basic_state_t *state, mbf_t arg);

/** FRE: Get free memory. */
int32_t stmt_fre(basic_state_t *state);

/** RANDOMIZE: Seed RNG. */
basic_error_t stmt_randomize(basic_state_t *state, mbf_t seed);


/* ============================================================================
 * RANDOM NUMBER GENERATOR (math/rnd.c)
 * ============================================================================ */

/** Get amount of free memory (for FRE function). */
uint16_t basic_free_memory(basic_state_t *state);

/** RND function - matches original algorithm exactly. */
mbf_t basic_rnd(basic_state_t *state, mbf_t arg);

/** Initialize RND state with default seed. */
void rnd_init(rnd_state_t *state);

/** Reseed the RNG to initial state. */
void rnd_reseed(rnd_state_t *state);

/** Seed the RNG from an MBF value (for RANDOMIZE statement). */
void rnd_seed_from_mbf(rnd_state_t *state, mbf_t seed);

/** Generate next random value. */
mbf_t rnd_next(rnd_state_t *state, mbf_t arg);


/* ============================================================================
 * NUMERIC FUNCTIONS (functions/numeric.c)
 * ============================================================================ */

mbf_t fn_sgn(mbf_t value);          /**< SGN: Sign (-1, 0, or 1) */
mbf_t fn_int(mbf_t value);          /**< INT: Floor to integer */
mbf_t fn_abs(mbf_t value);          /**< ABS: Absolute value */
mbf_t fn_sqr(mbf_t value);          /**< SQR: Square root */
mbf_t fn_exp(mbf_t value);          /**< EXP: e^x */
mbf_t fn_log(mbf_t value);          /**< LOG: Natural logarithm */
mbf_t fn_sin(mbf_t value);          /**< SIN: Sine (radians) */
mbf_t fn_cos(mbf_t value);          /**< COS: Cosine (radians) */
mbf_t fn_tan(mbf_t value);          /**< TAN: Tangent (radians) */
mbf_t fn_atn(mbf_t value);          /**< ATN: Arctangent (radians) */
mbf_t fn_rnd(basic_state_t *state, mbf_t arg);  /**< RND: Random number */
mbf_t fn_peek(basic_state_t *state, mbf_t address);  /**< PEEK: Read memory */
mbf_t fn_fre(basic_state_t *state, mbf_t dummy);     /**< FRE: Free memory */
mbf_t fn_pos(basic_state_t *state, mbf_t dummy);     /**< POS: Cursor column */
mbf_t fn_usr(basic_state_t *state, mbf_t arg);       /**< USR: Machine code (stub) */
mbf_t fn_inp(basic_state_t *state, mbf_t port);      /**< INP: Hardware input (stub) */


/* ============================================================================
 * STRING FUNCTIONS (functions/string.c)
 * ============================================================================ */

mbf_t fn_len(string_desc_t str);    /**< LEN: String length */

string_desc_t fn_left(basic_state_t *state, string_desc_t str, mbf_t n);   /**< LEFT$ */
string_desc_t fn_right(basic_state_t *state, string_desc_t str, mbf_t n);  /**< RIGHT$ */
string_desc_t fn_mid(basic_state_t *state, string_desc_t str, mbf_t start, mbf_t n);  /**< MID$ */

mbf_t fn_asc(basic_state_t *state, string_desc_t str);  /**< ASC: First char code */
string_desc_t fn_chr(basic_state_t *state, mbf_t code); /**< CHR$: Char from code */
string_desc_t fn_str(basic_state_t *state, mbf_t value); /**< STR$: Number to string */
mbf_t fn_val(basic_state_t *state, string_desc_t str);  /**< VAL: String to number */

/**
 * INSTR: Find substring position.
 * @param start  Starting position (1-based)
 * @return       Position found (1-based), or 0 if not found
 */
mbf_t fn_instr(basic_state_t *state, int start,
               string_desc_t main_str, string_desc_t search_str);

string_desc_t fn_space(basic_state_t *state, mbf_t n);  /**< SPACE$: n spaces */
string_desc_t fn_string(basic_state_t *state, mbf_t n, uint8_t ch);  /**< STRING$: n copies */
string_desc_t fn_hex(basic_state_t *state, mbf_t value);  /**< HEX$: Hex string */
string_desc_t fn_oct(basic_state_t *state, mbf_t value);  /**< OCT$: Octal string */

#endif /* BASIC8K_BASIC_H */
