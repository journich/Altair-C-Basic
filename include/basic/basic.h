/*
 * basic.h - Altair 8K BASIC 4.0 Interpreter
 *
 * A 100% compatible C17 implementation of Microsoft's Altair 8K BASIC 4.0.
 * This implementation produces byte-for-byte identical output to the original
 * 8080 assembly version when given the same input.
 *
 * Copyright (c) 2025
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

/*
 * Version information
 */
#define BASIC8K_VERSION_MAJOR   4
#define BASIC8K_VERSION_MINOR   0
#define BASIC8K_VERSION_STRING  "4.0"

/*
 * Memory configuration
 */
#define BASIC8K_MIN_MEMORY      4096    /* Minimum usable memory */
#define BASIC8K_MAX_MEMORY      65536   /* Maximum addressable memory */
#define BASIC8K_DEFAULT_MEMORY  65536   /* Default memory size */

/*
 * Terminal configuration
 */
#define BASIC8K_DEFAULT_WIDTH   72      /* Default terminal width */
#define BASIC8K_MIN_WIDTH       16      /* Minimum terminal width */
#define BASIC8K_MAX_WIDTH       255     /* Maximum terminal width */

/*
 * String variable descriptor
 */
typedef struct {
    uint8_t length;         /* String length (0-255) */
    uint8_t _reserved;      /* Alignment padding */
    uint16_t ptr;           /* Pointer to string data in string space */
} string_desc_t;

/*
 * Variable value (union of numeric and string)
 */
typedef union {
    mbf_t numeric;          /* Numeric value (MBF format) */
    string_desc_t string;   /* String descriptor */
} basic_value_t;

/*
 * Variable entry (6 bytes, matches original format)
 */
typedef struct {
    char name[2];           /* Variable name (1-2 chars, $=string) */
    basic_value_t value;    /* Value */
} basic_variable_t;

/*
 * FOR loop stack entry
 */
typedef struct {
    uint16_t line_number;   /* Line to return to */
    uint16_t text_ptr;      /* Position in line */
    basic_variable_t *var;  /* Loop variable */
    mbf_t limit;            /* TO value */
    mbf_t step;             /* STEP value */
} for_entry_t;

/*
 * GOSUB stack entry
 */
typedef struct {
    uint16_t line_number;   /* Line to return to */
    uint16_t text_ptr;      /* Position in line */
} gosub_entry_t;

/*
 * Random number generator state
 * Matches the two-stage algorithm from the original.
 */
typedef struct {
    uint8_t counter1;       /* Main counter (wraps at 0xAB) */
    uint8_t counter2;       /* Addend table index (mod 4) */
    uint8_t counter3;       /* Multiplier table index (mod 8) */
    mbf_t last_value;       /* Last generated value / seed */
} rnd_state_t;

/*
 * Interpreter configuration
 */
typedef struct {
    uint32_t memory_size;   /* Total memory size */
    uint8_t terminal_width; /* Terminal width (for PRINT TAB) */
    bool want_trig;         /* Include SIN/COS/TAN/ATN? */
    FILE *input;            /* Input stream (default: stdin) */
    FILE *output;           /* Output stream (default: stdout) */
} basic_config_t;

/*
 * Main interpreter state
 */
typedef struct {
    /* Memory */
    uint8_t *memory;                /* Memory buffer */
    uint32_t memory_size;           /* Size of memory buffer */

    /* Memory region pointers (indices into memory) */
    uint16_t program_start;         /* Start of program area */
    uint16_t program_end;           /* End of program / start of variables */
    uint16_t var_start;             /* Start of simple variables */
    uint16_t array_start;           /* Start of arrays */
    uint16_t string_start;          /* Bottom of string space */
    uint16_t string_end;            /* Top of string space (grows down) */

    /* Execution state */
    uint16_t current_line;          /* Current line number (0xFFFF = direct) */
    uint16_t text_ptr;              /* Current position in program */
    uint16_t data_line;             /* Current DATA line */
    uint16_t data_ptr;              /* Current position in DATA */

    /* Floating-point accumulator (like original FACCUM) */
    mbf_t fac;
    uint8_t value_type;             /* 0 = numeric, 0xFF = string */

    /* Random number generator */
    rnd_state_t rnd;

    /* Control stacks */
    for_entry_t for_stack[16];      /* FOR loop stack */
    int for_sp;                     /* FOR stack pointer */
    gosub_entry_t gosub_stack[16];  /* GOSUB stack */
    int gosub_sp;                   /* GOSUB stack pointer */

    /* User-defined functions */
    struct {
        char name;                  /* Function name (FNA-FNZ) */
        uint16_t line;              /* Definition line */
        uint16_t ptr;               /* Definition position */
    } user_funcs[26];

    /* Terminal state */
    uint8_t terminal_x;             /* Current column (0-based) */
    uint8_t terminal_width;         /* Line width */
    uint8_t null_count;             /* NULL statement value */
    bool output_suppressed;         /* Ctrl-O toggle */
    bool want_trig;                 /* Trig functions enabled */

    /* I/O */
    FILE *input;
    FILE *output;

    /* Flags */
    bool running;                   /* Program is running */
    bool can_continue;              /* CONT is possible */
    uint16_t cont_line;             /* Line to continue from */
    uint16_t cont_ptr;              /* Position to continue from */

    /* Hardware warning flags (only warn once) */
    bool warned_inp;
    bool warned_out;
    bool warned_wait;
    bool warned_usr;

} basic_state_t;

/*
 * Initialize a new interpreter state.
 * Returns NULL on failure (out of memory).
 */
basic_state_t *basic_init(const basic_config_t *config);

/*
 * Free interpreter state and all associated memory.
 */
void basic_free(basic_state_t *state);

/*
 * Reset interpreter to initial state (like typing NEW).
 */
void basic_reset(basic_state_t *state);

/*
 * Run the interactive interpreter loop.
 * This shows the initialization prompts and enters command mode.
 */
void basic_run_interactive(basic_state_t *state);

/*
 * Execute a single line of BASIC (direct mode).
 * Returns true if successful, false on error.
 */
bool basic_execute_line(basic_state_t *state, const char *line);

/*
 * Load a program from a file.
 * Returns true if successful, false on error.
 */
bool basic_load_file(basic_state_t *state, const char *filename);

/*
 * Save program to a file.
 * Returns true if successful, false on error.
 */
bool basic_save_file(basic_state_t *state, const char *filename);

/*
 * Run the current program (like typing RUN).
 */
void basic_run_program(basic_state_t *state);

/*
 * List program lines (like typing LIST).
 */
void basic_list_program(basic_state_t *state, uint16_t start, uint16_t end);

/*
 * Get free memory (for FRE function).
 */
uint16_t basic_free_memory(basic_state_t *state);

/*
 * RND function - returns next random number.
 * Matches original algorithm exactly for compatibility.
 */
mbf_t basic_rnd(basic_state_t *state, mbf_t arg);

/*
 * RND internal functions.
 */
void rnd_init(rnd_state_t *state);
void rnd_reseed(rnd_state_t *state);
mbf_t rnd_next(rnd_state_t *state, mbf_t arg);

/*
 * Print the startup banner.
 */
void basic_print_banner(basic_state_t *state);

/*
 * Print "OK" prompt.
 */
void basic_print_ok(basic_state_t *state);

/*
 * Print an error message.
 */
void basic_print_error(basic_state_t *state, basic_error_t err, uint16_t line);

/*
 * Expression evaluation.
 * Evaluates a numeric expression from tokenized text.
 * Returns the result as an MBF value.
 */
mbf_t eval_expression(basic_state_t *state, const uint8_t *text, size_t len,
                      size_t *consumed, basic_error_t *error);

/*
 * String expression evaluation.
 * Returns pointer to string in string space, or NULL on error.
 */
const char *eval_string_expression(basic_state_t *state, const uint8_t *text,
                                   size_t len, size_t *consumed,
                                   basic_error_t *error);

#endif /* BASIC8K_BASIC_H */
