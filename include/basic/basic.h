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
    uint8_t *var;           /* Loop variable (pointer into memory) */
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
    uint16_t array_start;           /* End of arrays / next allocation point */
    uint16_t string_start;          /* Bottom of string space */
    uint16_t string_end;            /* Top of string space (grows down) */

    /* Variable tracking */
    uint16_t var_count_;            /* Number of simple variables */

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

/*
 * Variable functions.
 */
bool var_is_string(const char *name);
uint8_t *var_find(basic_state_t *state, const char *name);
uint8_t *var_create(basic_state_t *state, const char *name);
uint8_t *var_get_or_create(basic_state_t *state, const char *name);
mbf_t var_get_numeric(basic_state_t *state, const char *name);
bool var_set_numeric(basic_state_t *state, const char *name, mbf_t value);
string_desc_t var_get_string(basic_state_t *state, const char *name);
bool var_set_string(basic_state_t *state, const char *name, string_desc_t desc);
void var_clear_all(basic_state_t *state);
int var_count(basic_state_t *state);

/*
 * Array functions.
 */
uint8_t *array_find(basic_state_t *state, const char *name);
uint8_t *array_create(basic_state_t *state, const char *name, int dim1, int dim2);
uint8_t *array_get_element(basic_state_t *state, const char *name,
                           int index1, int index2);
mbf_t array_get_numeric(basic_state_t *state, const char *name,
                        int index1, int index2);
bool array_set_numeric(basic_state_t *state, const char *name,
                       int index1, int index2, mbf_t value);
string_desc_t array_get_string(basic_state_t *state, const char *name,
                               int index1, int index2);
bool array_set_string(basic_state_t *state, const char *name,
                      int index1, int index2, string_desc_t desc);
void array_clear_all(basic_state_t *state);

/*
 * String space functions.
 */
void string_init(basic_state_t *state);
uint16_t string_alloc(basic_state_t *state, uint8_t length);
string_desc_t string_create(basic_state_t *state, const char *str);
string_desc_t string_create_len(basic_state_t *state, const char *data, uint8_t length);
const char *string_get_data(basic_state_t *state, string_desc_t desc);
string_desc_t string_copy(basic_state_t *state, string_desc_t src);
string_desc_t string_concat(basic_state_t *state, string_desc_t a, string_desc_t b);
int string_compare(basic_state_t *state, string_desc_t a, string_desc_t b);
string_desc_t string_left(basic_state_t *state, string_desc_t str, uint8_t n);
string_desc_t string_right(basic_state_t *state, string_desc_t str, uint8_t n);
string_desc_t string_mid(basic_state_t *state, string_desc_t str, uint8_t start, uint8_t n);
uint8_t string_len(string_desc_t str);
uint8_t string_asc(basic_state_t *state, string_desc_t str);
string_desc_t string_chr(basic_state_t *state, uint8_t ch);
mbf_t string_val(basic_state_t *state, string_desc_t str);
string_desc_t string_str(basic_state_t *state, mbf_t value);
void string_garbage_collect(basic_state_t *state);
uint16_t string_free(basic_state_t *state);
void string_clear(basic_state_t *state);

/*
 * Control flow statements (flow.c).
 */
basic_error_t stmt_goto(basic_state_t *state, uint16_t line_num);
basic_error_t stmt_gosub(basic_state_t *state, uint16_t line_num,
                         uint16_t return_line, uint16_t return_ptr);
basic_error_t stmt_return(basic_state_t *state);
basic_error_t stmt_for(basic_state_t *state, const char *var_name,
                       mbf_t initial, mbf_t limit, mbf_t step,
                       uint16_t next_line, uint16_t next_ptr);
basic_error_t stmt_next(basic_state_t *state, const char *var_name, bool *continue_loop);
bool stmt_if_eval(mbf_t condition);
basic_error_t stmt_end(basic_state_t *state);
basic_error_t stmt_stop(basic_state_t *state, uint16_t line, uint16_t ptr);
basic_error_t stmt_cont(basic_state_t *state);
basic_error_t stmt_on_goto(basic_state_t *state, int value,
                           uint16_t *lines, int num_lines);
basic_error_t stmt_on_gosub(basic_state_t *state, int value,
                            uint16_t *lines, int num_lines,
                            uint16_t return_line, uint16_t return_ptr);
basic_error_t stmt_pop(basic_state_t *state);
void stmt_clear_stacks(basic_state_t *state);

/*
 * I/O statements (io.c).
 */
void io_putchar(basic_state_t *state, char ch);
void io_print_string(basic_state_t *state, const char *str, size_t len);
void io_print_cstring(basic_state_t *state, const char *str);
void io_newline(basic_state_t *state);
void io_print_number(basic_state_t *state, mbf_t value);
void io_tab(basic_state_t *state, int column);
void io_spc(basic_state_t *state, int count);
bool io_input_line(basic_state_t *state, char *buf, size_t bufsize, size_t *len);
size_t io_parse_number(const char *str, mbf_t *value);
void io_data_init(basic_state_t *state);
basic_error_t stmt_restore(basic_state_t *state);
basic_error_t stmt_restore_line(basic_state_t *state, uint16_t line_num);
basic_error_t io_read_numeric(basic_state_t *state, mbf_t *value);
basic_error_t io_read_string(basic_state_t *state, string_desc_t *value);
basic_error_t stmt_null(basic_state_t *state, int count);
basic_error_t stmt_width(basic_state_t *state, int width);
int io_pos(basic_state_t *state);

/*
 * Miscellaneous statements (misc.c).
 */
basic_error_t stmt_let_numeric(basic_state_t *state, const char *var_name, mbf_t value);
basic_error_t stmt_let_string(basic_state_t *state, const char *var_name, string_desc_t value);
basic_error_t stmt_let_array_numeric(basic_state_t *state, const char *arr_name,
                                      int idx1, int idx2, mbf_t value);
basic_error_t stmt_let_array_string(basic_state_t *state, const char *arr_name,
                                     int idx1, int idx2, string_desc_t value);
basic_error_t stmt_dim(basic_state_t *state, const char *arr_name, int dim1, int dim2);
basic_error_t stmt_def_fn(basic_state_t *state, char fn_name,
                          uint16_t line, uint16_t ptr);
basic_error_t stmt_fn_lookup(basic_state_t *state, char fn_name,
                             uint16_t *line, uint16_t *ptr);
basic_error_t stmt_poke(basic_state_t *state, uint16_t address, uint8_t value);
uint8_t stmt_peek(basic_state_t *state, uint16_t address);
basic_error_t stmt_clear(basic_state_t *state, int string_space);
basic_error_t stmt_new(basic_state_t *state);
basic_error_t stmt_run(basic_state_t *state, uint16_t start_line);
basic_error_t stmt_rem(void);
basic_error_t stmt_swap_numeric(basic_state_t *state,
                                const char *var1, const char *var2);
basic_error_t stmt_swap_string(basic_state_t *state,
                               const char *var1, const char *var2);
uint8_t stmt_inp(basic_state_t *state, uint8_t port);
basic_error_t stmt_out(basic_state_t *state, uint8_t port, uint8_t value);
basic_error_t stmt_wait(basic_state_t *state, uint8_t port, uint8_t mask, uint8_t xor_val);
mbf_t stmt_usr(basic_state_t *state, mbf_t arg);
int32_t stmt_fre(basic_state_t *state);
basic_error_t stmt_randomize(basic_state_t *state, mbf_t seed);

/*
 * Numeric functions (functions/numeric.c).
 */
mbf_t fn_sgn(mbf_t value);
mbf_t fn_int(mbf_t value);
mbf_t fn_abs(mbf_t value);
mbf_t fn_sqr(mbf_t value);
mbf_t fn_exp(mbf_t value);
mbf_t fn_log(mbf_t value);
mbf_t fn_sin(mbf_t value);
mbf_t fn_cos(mbf_t value);
mbf_t fn_tan(mbf_t value);
mbf_t fn_atn(mbf_t value);
mbf_t fn_rnd(basic_state_t *state, mbf_t arg);
mbf_t fn_peek(basic_state_t *state, mbf_t address);
mbf_t fn_fre(basic_state_t *state, mbf_t dummy);
mbf_t fn_pos(basic_state_t *state, mbf_t dummy);
mbf_t fn_usr(basic_state_t *state, mbf_t arg);
mbf_t fn_inp(basic_state_t *state, mbf_t port);

/*
 * String functions (functions/string.c).
 */
mbf_t fn_len(string_desc_t str);
string_desc_t fn_left(basic_state_t *state, string_desc_t str, mbf_t n);
string_desc_t fn_right(basic_state_t *state, string_desc_t str, mbf_t n);
string_desc_t fn_mid(basic_state_t *state, string_desc_t str, mbf_t start, mbf_t n);
mbf_t fn_asc(basic_state_t *state, string_desc_t str);
string_desc_t fn_chr(basic_state_t *state, mbf_t code);
string_desc_t fn_str(basic_state_t *state, mbf_t value);
mbf_t fn_val(basic_state_t *state, string_desc_t str);
mbf_t fn_instr(basic_state_t *state, int start,
               string_desc_t main_str, string_desc_t search_str);
string_desc_t fn_space(basic_state_t *state, mbf_t n);
string_desc_t fn_string(basic_state_t *state, mbf_t n, uint8_t ch);
string_desc_t fn_hex(basic_state_t *state, mbf_t value);
string_desc_t fn_oct(basic_state_t *state, mbf_t value);

#endif /* BASIC8K_BASIC_H */
