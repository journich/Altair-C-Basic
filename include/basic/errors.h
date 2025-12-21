/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file errors.h
 * @brief BASIC Error Codes and Error Handling
 *
 * This module defines the error system for the Altair 8K BASIC interpreter.
 * The error codes and their 2-letter abbreviations are taken directly from
 * the original 8080 assembly source (8kbas_src.mac lines 276-313).
 *
 * ## Error Format
 *
 * When an error occurs, BASIC displays it in this format:
 * ```
 * ?XX ERROR IN line_number
 * ```
 * For example: `?SN ERROR IN 100` means "Syntax error in line 100"
 *
 * Direct mode errors omit the line number:
 * ```
 * ?SN ERROR
 * ```
 *
 * ## Error Categories
 *
 * Errors fall into several categories:
 *
 * **Syntax Errors (caught at parse/run time):**
 * - SN (Syntax) - malformed statement, invalid character
 * - MO (Missing Operand) - incomplete expression
 *
 * **Runtime Errors (caught during execution):**
 * - NF (NEXT without FOR) - unmatched NEXT statement
 * - RG (RETURN without GOSUB) - unmatched RETURN
 * - OD (Out of DATA) - READ beyond available DATA
 * - FC (Function Call) - illegal argument to function
 * - OV (Overflow) - numeric result too large
 * - UL (Undefined Line) - GOTO/GOSUB to non-existent line
 * - BS (Bad Subscript) - array index out of range
 * - DD (Double Dimension) - array already dimensioned
 * - /0 (Division by Zero) - divide by zero
 * - ID (Illegal Direct) - statement not allowed in direct mode
 * - TM (Type Mismatch) - string where number expected or vice versa
 * - UF (Undefined Function) - FN call to undefined DEF FN
 *
 * **Memory Errors:**
 * - OM (Out of Memory) - no room for program/variables/stack
 * - OS (Out of String space) - string heap exhausted
 *
 * **String Errors:**
 * - LS (String too Long) - string exceeds 255 characters
 * - ST (String formula Too complex) - nested string expressions
 *
 * **Continuation Error:**
 * - CN (Can't Continue) - CONT not possible after edit/NEW
 *
 * ## Error Handling Flow
 *
 * ```
 *   [Error Detected]
 *          |
 *          v
 *   basic_error(code) or basic_error_at_line(code, line)
 *          |
 *          v
 *   [Store in g_last_error]
 *          |
 *          v
 *   [longjmp to error handler in interpreter]
 *          |
 *          v
 *   [Print error message]
 *          |
 *          v
 *   [Return to READY prompt]
 * ```
 *
 * ## Original Source Reference
 *
 * The error table in 8kbas_src.mac defines error messages as:
 * ```
 * ERRTAB: DB      'NF'    ; NEXT without FOR
 *         DB      'SN'    ; Syntax error
 *         DB      'RG'    ; RETURN without GOSUB
 *         ... etc ...
 * ```
 *
 * Each error code is exactly 2 characters. The order in the table
 * determines the numeric error code (1 = NF, 2 = SN, etc.).
 */

#ifndef BASIC8K_ERRORS_H
#define BASIC8K_ERRORS_H

#include <stdint.h>

/*============================================================================
 * ERROR CODE ENUMERATION
 *
 * These values match the order in the original BASIC error table.
 * The numeric values are used internally; users see 2-letter codes.
 *============================================================================*/

/**
 * @brief BASIC error codes
 *
 * Error codes from the original Altair 8K BASIC 4.0.
 * The enum values match the index in the original error table.
 *
 * Example usage:
 * @code
 *     if (divisor == 0) {
 *         basic_error(ERR_DZ);  // Division by zero
 *     }
 * @endcode
 */
typedef enum {
    /**
     * @brief No error - operation successful
     *
     * This is the default state. Functions returning basic_error_t
     * return ERR_NONE on success.
     */
    ERR_NONE = 0,

    /*------------------------------------------------------------------------
     * Runtime Control Flow Errors
     *------------------------------------------------------------------------*/

    /**
     * @brief NF - NEXT without FOR
     *
     * A NEXT statement was encountered but no matching FOR loop is active.
     * This happens when:
     * - NEXT appears without any preceding FOR
     * - NEXT variable doesn't match any active FOR variable
     * - FOR loop was exited via GOTO, but NEXT still executed
     *
     * Example that causes NF error:
     * @code
     *     10 PRINT "HELLO"
     *     20 NEXT I        ' No FOR I loop exists
     * @endcode
     */
    ERR_NF = 1,

    /**
     * @brief SN - Syntax Error
     *
     * The statement could not be parsed. This is the catch-all error
     * for malformed input. Common causes:
     * - Misspelled keywords
     * - Missing required elements (parentheses, operators)
     * - Invalid characters in identifiers
     * - Unrecognized statement structure
     *
     * Example that causes SN error:
     * @code
     *     10 PIRNT "HELLO"   ' Misspelled PRINT
     *     20 LET X = 5 +     ' Incomplete expression
     * @endcode
     */
    ERR_SN = 2,

    /**
     * @brief RG - RETURN without GOSUB
     *
     * A RETURN statement was encountered but no GOSUB is active.
     * The GOSUB stack is empty.
     *
     * Example that causes RG error:
     * @code
     *     10 PRINT "START"
     *     20 RETURN          ' No GOSUB called us
     * @endcode
     */
    ERR_RG = 3,

    /**
     * @brief OD - Out of DATA
     *
     * A READ statement tried to read data, but all DATA elements
     * have been consumed. Use RESTORE to reset the data pointer.
     *
     * Example that causes OD error:
     * @code
     *     10 DATA 1, 2, 3
     *     20 READ A, B, C, D   ' Only 3 values, need 4
     * @endcode
     *
     * Fix by adding more DATA or using RESTORE to re-read.
     */
    ERR_OD = 4,

    /**
     * @brief FC - Illegal Function Call
     *
     * A function received an argument outside its valid domain.
     * Examples:
     * - SQR(-1) - square root of negative number
     * - LEFT$("HELLO", -1) - negative length
     * - CHR$(256) - value outside 0-255 range
     * - array subscript of wrong dimension count
     *
     * Example that causes FC error:
     * @code
     *     10 X = SQR(-5)      ' Can't take square root of negative
     * @endcode
     */
    ERR_FC = 5,

    /*------------------------------------------------------------------------
     * Numeric Errors
     *------------------------------------------------------------------------*/

    /**
     * @brief OV - Overflow
     *
     * A numeric result exceeded the MBF floating-point range.
     * MBF can represent approximately ±1.7E38.
     *
     * Example that causes OV error:
     * @code
     *     10 X = 1E30 * 1E30   ' Result exceeds 1.7E38
     * @endcode
     */
    ERR_OV = 6,

    /*------------------------------------------------------------------------
     * Memory Errors
     *------------------------------------------------------------------------*/

    /**
     * @brief OM - Out of Memory
     *
     * Insufficient memory for the requested operation. Causes include:
     * - Program too large
     * - Too many variables
     * - FOR/GOSUB stack overflow (too many nested loops/calls)
     * - DIM array too large
     *
     * Memory is laid out as:
     * @code
     *     [Program] [Variables] [Arrays] ... [Stack] [Strings]
     *     Low mem                             High mem
     * @endcode
     *
     * The middle area is shared. When stack meets arrays, OM occurs.
     */
    ERR_OM = 7,

    /**
     * @brief UL - Undefined Line Number
     *
     * GOTO, GOSUB, or other branch tried to jump to a line that
     * doesn't exist in the program.
     *
     * Example that causes UL error:
     * @code
     *     10 GOTO 500         ' Line 500 doesn't exist
     * @endcode
     */
    ERR_UL = 8,

    /**
     * @brief BS - Bad Subscript
     *
     * Array subscript out of bounds. Arrays are 0-based in storage
     * but 1-based in BASIC syntax. Default dimension is 10.
     *
     * Example that causes BS error:
     * @code
     *     10 DIM A(5)
     *     20 A(10) = 100      ' Only 0-5 are valid
     * @endcode
     */
    ERR_BS = 9,

    /**
     * @brief DD - Redimensioned Array (Double Dimension)
     *
     * Attempt to DIM an array that already exists.
     * Arrays can only be dimensioned once per RUN.
     *
     * Example that causes DD error:
     * @code
     *     10 DIM A(10)
     *     20 DIM A(20)        ' Already dimensioned at line 10
     * @endcode
     */
    ERR_DD = 10,

    /**
     * @brief /0 - Division by Zero
     *
     * Attempt to divide by zero. This includes both explicit
     * division and modulo operations.
     *
     * Example that causes /0 error:
     * @code
     *     10 X = 5 / 0
     * @endcode
     */
    ERR_DZ = 11,

    /**
     * @brief ID - Illegal Direct
     *
     * Statement cannot be executed in direct mode (without a line number).
     * These statements require a program context:
     * - DEF (must be in program to be executed by RUN)
     * - INPUT (in some versions)
     *
     * Example that causes ID error:
     * @code
     *     DEF FNA(X) = X * 2   ' Must be in a program line
     * @endcode
     */
    ERR_ID = 12,

    /*------------------------------------------------------------------------
     * Type Errors
     *------------------------------------------------------------------------*/

    /**
     * @brief TM - Type Mismatch
     *
     * String value where number expected, or vice versa.
     * BASIC has two data types: numeric and string.
     * String variables end with $.
     *
     * Example that causes TM error:
     * @code
     *     10 A$ = 5           ' A$ is string, 5 is numeric
     *     20 X = "HELLO"      ' X is numeric, "HELLO" is string
     * @endcode
     */
    ERR_TM = 13,

    /*------------------------------------------------------------------------
     * String Errors
     *------------------------------------------------------------------------*/

    /**
     * @brief OS - Out of String Space
     *
     * The string heap is exhausted. String space is allocated at
     * the top of memory and grows downward. Unlike program memory,
     * strings use a garbage-collected heap.
     *
     * Fix: Use CLEAR to reset string space, or reduce string usage.
     */
    ERR_OS = 14,

    /**
     * @brief LS - String Too Long
     *
     * String operation would result in a string longer than 255
     * characters. This is a fundamental BASIC limit (1-byte length).
     *
     * Example that causes LS error:
     * @code
     *     10 A$ = STRING$(200, "X")
     *     20 B$ = A$ + A$     ' Would be 400 chars
     * @endcode
     */
    ERR_LS = 15,

    /**
     * @brief ST - String Formula Too Complex
     *
     * Nested string expressions exceed the string temp stack.
     * The interpreter has a limited number of temporary string
     * descriptors for intermediate results.
     *
     * Workaround: Break complex expressions into simpler steps.
     */
    ERR_ST = 16,

    /**
     * @brief CN - Can't Continue
     *
     * CONT command issued, but the program cannot be continued.
     * This happens after:
     * - Editing or adding program lines
     * - Using NEW or CLEAR
     * - Never running a program (CONT after startup)
     *
     * CONT only works after STOP, END, or Ctrl-C during execution
     * when no program modifications have been made.
     */
    ERR_CN = 17,

    /**
     * @brief UF - Undefined User Function
     *
     * FN call to a function that was never defined with DEF FN.
     *
     * Example that causes UF error:
     * @code
     *     10 PRINT FNA(5)     ' No DEF FNA exists
     * @endcode
     */
    ERR_UF = 18,

    /**
     * @brief MO - Missing Operand
     *
     * An expression is incomplete - an operator is missing its operand.
     *
     * Example that causes MO error:
     * @code
     *     10 X = 5 +          ' Missing operand after +
     *     20 PRINT -          ' Missing operand after unary -
     * @endcode
     */
    ERR_MO = 19,

    /**
     * @brief Sentinel value - number of error codes
     *
     * Used for bounds checking and array sizing.
     */
    ERR_COUNT
} basic_error_t;


/*============================================================================
 * ERROR CODE STRINGS
 *
 * The 2-letter codes that are printed to the user.
 *============================================================================*/

/**
 * @brief Error code strings (2 letters each)
 *
 * This array maps error code enum values to their 2-letter string
 * representations. The index matches basic_error_t values.
 *
 * Defined in errors.c:
 * @code
 *     const char ERROR_CODES[][3] = {
 *         "OK", "NF", "SN", "RG", "OD", "FC", "OV", "OM",
 *         "UL", "BS", "DD", "/0", "ID", "TM", "OS", "LS",
 *         "ST", "CN", "UF", "MO"
 *     };
 * @endcode
 */
extern const char ERROR_CODES[][3];


/*============================================================================
 * ERROR QUERY FUNCTIONS
 *============================================================================*/

/**
 * @brief Get the 2-letter error code string for an error
 *
 * Converts a basic_error_t enum value to its printable 2-letter code.
 *
 * @param err The error code to convert
 * @return Pointer to a 2-character null-terminated string.
 *         Returns "??" for unknown/invalid error codes.
 *
 * Example:
 * @code
 *     printf("?%s ERROR\n", error_code_string(ERR_SN));
 *     // Output: ?SN ERROR
 * @endcode
 */
const char *error_code_string(basic_error_t err);


/*============================================================================
 * ERROR CONTEXT
 *
 * Stores information about where an error occurred for diagnostics.
 *============================================================================*/

/**
 * @brief Error context - stores where an error occurred
 *
 * When an error is raised, this structure captures:
 * - The error code
 * - The line number (or 0xFFFF for direct mode)
 * - The position within the line (for future use)
 *
 * This information is used when printing "?XX ERROR IN line".
 */
typedef struct {
    /** The error code that occurred */
    basic_error_t code;

    /**
     * Line number where error occurred.
     * 0xFFFF (65535) indicates direct mode (no line number).
     */
    uint16_t line_number;

    /**
     * Character position within the line (0-based).
     * Reserved for future enhanced error reporting.
     */
    uint16_t position;
} error_context_t;

/**
 * @brief Global error state
 *
 * Set by the interpreter when an error occurs. Contains the most
 * recent error information. Check with basic_has_error().
 *
 * Thread safety: Not thread-safe. Each interpreter instance should
 * have its own error context if running multiple interpreters.
 */
extern error_context_t g_last_error;


/*============================================================================
 * ERROR RAISING FUNCTIONS
 *
 * These functions set the error state and (typically) longjmp to the
 * error handler in the main interpreter loop.
 *============================================================================*/

/**
 * @brief Raise an error
 *
 * Records the error code and uses longjmp to return to the error
 * handler in the interpreter's main loop. The line number is taken
 * from the currently executing line.
 *
 * This function does not return (it longjmps).
 *
 * @param code The error code to raise
 *
 * Example:
 * @code
 *     if (index > array_size) {
 *         basic_error(ERR_BS);  // Bad subscript
 *         // Never reaches here
 *     }
 * @endcode
 */
void basic_error(basic_error_t code);

/**
 * @brief Raise an error at a specific line
 *
 * Like basic_error(), but allows specifying the line number explicitly.
 * Useful when the error is detected while processing one line but
 * should be reported for a different line.
 *
 * @param code The error code to raise
 * @param line The line number to report (0xFFFF for direct mode)
 */
void basic_error_at_line(basic_error_t code, uint16_t line);

/**
 * @brief Clear the last error
 *
 * Resets g_last_error to ERR_NONE. Called when starting execution
 * or after an error has been handled.
 */
void basic_clear_error(void);


/*============================================================================
 * ERROR CHECKING FUNCTIONS
 *============================================================================*/

/**
 * @brief Check if there's a pending error
 *
 * Quick check to see if an error has been recorded but not yet handled.
 *
 * @return Non-zero if an error is pending, 0 otherwise
 *
 * Example:
 * @code
 *     mbf_t result = mbf_div(a, b);
 *     if (basic_has_error()) {
 *         return;  // Division by zero occurred
 *     }
 * @endcode
 */
static inline int basic_has_error(void) {
    return g_last_error.code != ERR_NONE;
}

#endif /* BASIC8K_ERRORS_H */
