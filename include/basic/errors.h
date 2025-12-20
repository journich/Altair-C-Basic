/*
 * errors.h - BASIC Error Codes
 *
 * Error codes from Altair 8K BASIC 4.0 (8kbas_src.mac lines 276-313)
 * Each error has a 2-letter code that gets printed: "XX ERROR [IN line]"
 */

#ifndef BASIC8K_ERRORS_H
#define BASIC8K_ERRORS_H

#include <stdint.h>

/*
 * Error codes match the original error table order.
 * The 2-letter code is derived from the error name.
 */
typedef enum {
    ERR_NONE = 0,       /* No error */

    /* Runtime errors */
    ERR_NF = 1,         /* NF - NEXT without FOR */
    ERR_SN = 2,         /* SN - Syntax error */
    ERR_RG = 3,         /* RG - RETURN without GOSUB */
    ERR_OD = 4,         /* OD - Out of DATA */
    ERR_FC = 5,         /* FC - Function Call error (illegal argument) */
    ERR_OV = 6,         /* OV - Overflow */
    ERR_OM = 7,         /* OM - Out of Memory */
    ERR_UL = 8,         /* UL - Undefined Line number */
    ERR_BS = 9,         /* BS - Bad Subscript */
    ERR_DD = 10,        /* DD - Double Dimension (redimensioned array) */
    ERR_DZ = 11,        /* /0 - Division by Zero */
    ERR_ID = 12,        /* ID - Illegal Direct (statement not allowed in direct mode) */
    ERR_TM = 13,        /* TM - Type Mismatch */
    ERR_OS = 14,        /* OS - Out of String space */
    ERR_LS = 15,        /* LS - String too Long */
    ERR_ST = 16,        /* ST - String formula Too complex */
    ERR_CN = 17,        /* CN - Can't coNtinue */
    ERR_UF = 18,        /* UF - Undefined user Function */
    ERR_MO = 19,        /* MO - Missing Operand */

    ERR_COUNT           /* Number of error codes */
} basic_error_t;

/*
 * Error code strings (2 letters each)
 * Index matches basic_error_t values.
 */
extern const char ERROR_CODES[][3];

/*
 * Get the 2-letter error code string for an error.
 * Returns "??" for unknown errors.
 */
const char *error_code_string(basic_error_t err);

/*
 * Error context structure - stores information about where an error occurred.
 */
typedef struct {
    basic_error_t code;         /* Error code */
    uint16_t line_number;       /* Line number (0xFFFF = direct mode) */
    uint16_t position;          /* Character position in line */
} error_context_t;

/*
 * Global error state (set by interpreter when error occurs)
 */
extern error_context_t g_last_error;

/*
 * Raise an error - stores error info and longjmps to error handler.
 * In the interpreter, this will print the error and return to command mode.
 */
void basic_error(basic_error_t code);

/*
 * Raise an error at a specific line.
 */
void basic_error_at_line(basic_error_t code, uint16_t line);

/*
 * Clear the last error.
 */
void basic_clear_error(void);

/*
 * Check if there's a pending error.
 */
static inline int basic_has_error(void) {
    return g_last_error.code != ERR_NONE;
}

#endif /* BASIC8K_ERRORS_H */
