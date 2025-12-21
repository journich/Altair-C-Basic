/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file tokens.h
 * @brief BASIC Token Definitions and Tokenization
 *
 * This module defines the token system used by Altair 8K BASIC for compact
 * program storage. Keywords are converted to single-byte tokens (0x81-0xC6)
 * for memory efficiency - critical on machines with only 4-64KB of RAM.
 *
 * ## Tokenization Overview
 *
 * When a BASIC line is entered:
 * 1. Keywords are converted to single bytes (tokens)
 * 2. String literals are preserved as-is (inside quotes)
 * 3. Numbers are preserved as ASCII (not converted to binary)
 * 4. Spaces are preserved for readability
 *
 * Example:
 * ```
 * Input:  10 PRINT "HELLO"; CHR$(65)
 * Stored: [0A 00] [97] [20] [22 48 45 4C 4C 4F 22] [3B] [20] [C3] [24 28 36 35 29]
 *         line#   PRINT sp  "HELLO"              ;    sp   CHR$ $(65)
 * ```
 *
 * ## Token Value Ranges
 *
 * | Range       | Category      | Description                     |
 * |-------------|---------------|---------------------------------|
 * | 0x00-0x80   | ASCII         | Not tokens - literal characters |
 * | 0x81-0x9D   | Statements    | END, FOR, NEXT, ..., NEW        |
 * | 0x9E-0xA4   | Keywords      | TAB, TO, FN, SPC, THEN, NOT, STEP |
 * | 0xA5-0xAE   | Operators     | +, -, *, /, ^, AND, OR, >, =, < |
 * | 0xAF-0xC6   | Functions     | SGN, INT, ..., MID$             |
 *
 * ## Why Tokenize?
 *
 * Memory was extremely limited in 1976:
 * - Altair 8800 base: 256 bytes RAM
 * - Typical system: 4KB-16KB
 * - Maximum: 64KB (full address space)
 *
 * Tokenization saves ~4-5 bytes per keyword:
 * - "PRINT" (5 chars) becomes 0x97 (1 byte) - saves 4 bytes
 * - "GOSUB" (5 chars) becomes 0x8D (1 byte) - saves 4 bytes
 *
 * A typical program might have hundreds of keywords, saving KBs.
 *
 * ## Detokenization (LIST command)
 *
 * When LIST displays the program, tokens are expanded back to keywords:
 * ```
 * Stored: [97] [20] [22 48 45 4C 4C 4F 22]
 * Output: PRINT "HELLO"
 * ```
 *
 * ## Original Source Reference
 *
 * Token values and keyword table are from 8kbas_src.mac lines 154-224.
 * The order of keywords in the table determines their token values.
 */

#ifndef BASIC8K_TOKENS_H
#define BASIC8K_TOKENS_H

#include <stdint.h>
#include <stddef.h>


/*============================================================================
 * TOKEN DEFINITIONS
 *
 * All tokens are in the range 0x81-0xC6 (129-198 decimal).
 * Values below 0x81 are literal ASCII characters.
 *============================================================================*/

/**
 * @brief BASIC tokens enumeration
 *
 * Token values assigned to BASIC keywords. These match the original
 * Altair 8K BASIC 4.0 exactly for compatibility.
 *
 * Tokens are grouped by function:
 * - Statements (0x81-0x9D): Commands that perform actions
 * - Keywords (0x9E-0xA4): Modifiers used within statements
 * - Operators (0xA5-0xAE): Mathematical and logical operators
 * - Functions (0xAF-0xC6): Built-in functions that return values
 */
typedef enum {
    /*------------------------------------------------------------------------
     * STATEMENT TOKENS (0x81-0x9D)
     *
     * These tokens can begin a statement. Some may also appear
     * within statements (like IF can contain GOTO).
     *------------------------------------------------------------------------*/

    /**
     * @brief END - Terminate program execution
     *
     * Stops the program. Unlike STOP, cannot be CONTinued.
     * Usage: `END`
     */
    TOK_END     = 0x81,

    /**
     * @brief FOR - Begin a FOR loop
     *
     * Usage: `FOR var = start TO end [STEP increment]`
     *
     * The loop variable, limits, and step are pushed onto the FOR stack.
     * Matching NEXT pops and tests the loop.
     */
    TOK_FOR     = 0x82,

    /**
     * @brief NEXT - End of FOR loop, test for continuation
     *
     * Usage: `NEXT [var [, var ...]]`
     *
     * Increments loop variable by STEP, tests against limit.
     * If not done, jumps back to statement after FOR.
     * Multiple variables close multiple loops: NEXT Y,X
     */
    TOK_NEXT    = 0x83,

    /**
     * @brief DATA - Inline data for READ statements
     *
     * Usage: `DATA value, value, ...`
     *
     * Values can be numbers or strings (strings may omit quotes
     * if they don't contain commas or leading/trailing spaces).
     */
    TOK_DATA    = 0x84,

    /**
     * @brief INPUT - Read user input into variables
     *
     * Usage: `INPUT ["prompt";] var [, var ...]`
     *
     * Displays prompt (or "?" by default), reads line, parses
     * comma-separated values into variables.
     */
    TOK_INPUT   = 0x85,

    /**
     * @brief DIM - Dimension an array
     *
     * Usage: `DIM var(size) [, var(size) ...]`
     *
     * Arrays are 0-based. Size specifies maximum index.
     * Default size (without DIM) is 10.
     */
    TOK_DIM     = 0x86,

    /**
     * @brief READ - Read data from DATA statements
     *
     * Usage: `READ var [, var ...]`
     *
     * Reads from DATA statements in program order.
     * Maintains internal data pointer.
     */
    TOK_READ    = 0x87,

    /**
     * @brief LET - Variable assignment (optional keyword)
     *
     * Usage: `[LET] var = expression`
     *
     * The LET keyword is optional in most BASICs.
     * `LET X = 5` and `X = 5` are equivalent.
     */
    TOK_LET     = 0x88,

    /**
     * @brief GOTO - Unconditional branch
     *
     * Usage: `GOTO line_number`
     *
     * Transfers execution to specified line.
     * Error UL (Undefined Line) if line doesn't exist.
     */
    TOK_GOTO    = 0x89,

    /**
     * @brief RUN - Execute program
     *
     * Usage: `RUN [line_number]`
     *
     * Clears variables, resets DATA pointer, begins execution.
     * Optional line number starts from that line.
     */
    TOK_RUN     = 0x8A,

    /**
     * @brief IF - Conditional execution
     *
     * Usage: `IF expression THEN statement(s) [ELSE statement(s)]`
     * Or:    `IF expression GOTO line_number`
     *
     * Expression is evaluated. If non-zero (true), THEN clause executes.
     * If zero (false), ELSE clause (if present) or next line.
     */
    TOK_IF      = 0x8B,

    /**
     * @brief RESTORE - Reset DATA pointer
     *
     * Usage: `RESTORE [line_number]`
     *
     * Resets the DATA read pointer to the beginning of DATA,
     * or to a specific line number if provided.
     */
    TOK_RESTORE = 0x8C,

    /**
     * @brief GOSUB - Call subroutine
     *
     * Usage: `GOSUB line_number`
     *
     * Pushes return address onto GOSUB stack, transfers to line.
     * RETURN pops and returns.
     */
    TOK_GOSUB   = 0x8D,

    /**
     * @brief RETURN - Return from subroutine
     *
     * Usage: `RETURN`
     *
     * Pops return address from GOSUB stack, continues there.
     * Error RG (RETURN without GOSUB) if stack empty.
     */
    TOK_RETURN  = 0x8E,

    /**
     * @brief REM - Comment (remark)
     *
     * Usage: `REM any text here`
     *
     * Everything after REM to end of line is ignored.
     * Note: Colon (:) does NOT end a REM - entire rest of line is comment.
     */
    TOK_REM     = 0x8F,

    /**
     * @brief STOP - Stop program execution
     *
     * Usage: `STOP`
     *
     * Like END but can be CONTinued.
     * Prints "BREAK IN line" message.
     */
    TOK_STOP    = 0x90,

    /**
     * @brief OUT - Output to I/O port (hardware-specific)
     *
     * Usage: `OUT port, value`
     *
     * On original Altair, writes to 8080 I/O port.
     * In this implementation: no-op with warning.
     */
    TOK_OUT     = 0x91,

    /**
     * @brief ON - Computed GOTO/GOSUB
     *
     * Usage: `ON expression GOTO line1, line2, ...`
     * Or:    `ON expression GOSUB line1, line2, ...`
     *
     * Expression (1-based) selects which line number to use.
     * If expression < 1 or > count, continues to next line.
     */
    TOK_ON      = 0x92,

    /**
     * @brief NULL - Set null character count
     *
     * Usage: `NULL n`
     *
     * Sends n null characters after each CR/LF.
     * Used for slow serial terminals needing time after line end.
     */
    TOK_NULL    = 0x93,

    /**
     * @brief WAIT - Wait for port condition (hardware-specific)
     *
     * Usage: `WAIT port, and_mask [, xor_mask]`
     *
     * On original Altair: reads port, applies masks, loops until non-zero.
     * In this implementation: no-op with warning.
     */
    TOK_WAIT    = 0x94,

    /**
     * @brief DEF - Define user function
     *
     * Usage: `DEF FNx(var) = expression`
     *
     * Defines a single-line function. Name must be FN followed by
     * one letter (FNA-FNZ). Only valid in a program line.
     */
    TOK_DEF     = 0x95,

    /**
     * @brief POKE - Write byte to memory
     *
     * Usage: `POKE address, value`
     *
     * Writes value (0-255) to memory address.
     * In this implementation: writes to interpreter's virtual memory.
     */
    TOK_POKE    = 0x96,

    /**
     * @brief PRINT - Output to terminal
     *
     * Usage: `PRINT [expression] [;|,] [expression] ...`
     *
     * Semicolon: no space between items
     * Comma: tab to next 14-column zone
     * No separator at end: newline
     * Trailing ; or ,: no newline
     */
    TOK_PRINT   = 0x97,

    /**
     * @brief CONT - Continue after STOP
     *
     * Usage: `CONT`
     *
     * Continues execution after STOP, END, or Ctrl-C.
     * Error CN (Can't Continue) if program was modified.
     */
    TOK_CONT    = 0x98,

    /**
     * @brief LIST - List program
     *
     * Usage: `LIST [start] [-end]`
     *
     * Lists program lines. Detokenizes for display.
     * Examples: LIST, LIST 100, LIST 100-200, LIST -50
     */
    TOK_LIST    = 0x99,

    /**
     * @brief CLEAR - Clear variables and strings
     *
     * Usage: `CLEAR [string_space_size]`
     *
     * Clears all variables, resets string space.
     * Optional parameter sets string space size.
     */
    TOK_CLEAR   = 0x9A,

    /**
     * @brief CLOAD - Load program from cassette/file
     *
     * Usage: `CLOAD ["filename"]`
     *
     * Original: loads from cassette tape.
     * This implementation: loads from .bas text file.
     */
    TOK_CLOAD   = 0x9B,

    /**
     * @brief CSAVE - Save program to cassette/file
     *
     * Usage: `CSAVE ["filename"]`
     *
     * Original: saves to cassette tape.
     * This implementation: saves to .bas text file.
     */
    TOK_CSAVE   = 0x9C,

    /**
     * @brief NEW - Clear program and variables
     *
     * Usage: `NEW`
     *
     * Erases the program in memory and all variables.
     * Cannot be undone.
     */
    TOK_NEW     = 0x9D,


    /*------------------------------------------------------------------------
     * KEYWORD TOKENS (0x9E-0xA4)
     *
     * These appear within statements, not at the beginning.
     *------------------------------------------------------------------------*/

    /**
     * @brief TAB( - Tab to column in PRINT
     *
     * Usage: `PRINT TAB(column); expression`
     *
     * Moves cursor to specified column (1-based).
     * If already past, moves to next line.
     */
    TOK_TAB     = 0x9E,

    /**
     * @brief TO - Specifies FOR loop limit
     *
     * Usage: `FOR var = start TO limit`
     */
    TOK_TO      = 0x9F,

    /**
     * @brief FN - Call user-defined function
     *
     * Usage: `FNx(expression)`
     *
     * Calls function defined by DEF FNx.
     * Error UF if function not defined.
     */
    TOK_FN      = 0xA0,

    /**
     * @brief SPC( - Print spaces in PRINT
     *
     * Usage: `PRINT SPC(count); expression`
     *
     * Outputs specified number of spaces.
     */
    TOK_SPC     = 0xA1,

    /**
     * @brief THEN - Introduces IF clause
     *
     * Usage: `IF condition THEN action`
     *
     * Can be followed by statements or a line number.
     */
    TOK_THEN    = 0xA2,

    /**
     * @brief NOT - Logical NOT operator
     *
     * Usage: `IF NOT condition THEN ...`
     *
     * Returns -1 (true) if operand is 0, else 0 (false).
     */
    TOK_NOT     = 0xA3,

    /**
     * @brief STEP - Specifies FOR loop increment
     *
     * Usage: `FOR var = start TO limit STEP increment`
     *
     * If omitted, STEP defaults to 1.
     */
    TOK_STEP    = 0xA4,


    /*------------------------------------------------------------------------
     * OPERATOR TOKENS (0xA5-0xAE)
     *
     * These are tokenized for consistency, though they're also single
     * ASCII characters. Having them as tokens allows the parser to
     * treat all operators uniformly.
     *------------------------------------------------------------------------*/

    /**
     * @brief + Addition operator
     */
    TOK_PLUS    = 0xA5,

    /**
     * @brief - Subtraction operator (also unary minus)
     */
    TOK_MINUS   = 0xA6,

    /**
     * @brief * Multiplication operator
     */
    TOK_MUL     = 0xA7,

    /**
     * @brief / Division operator
     */
    TOK_DIV     = 0xA8,

    /**
     * @brief ^ Exponentiation operator
     *
     * Usage: `X ^ Y` means X raised to power Y
     *
     * Highest precedence among binary operators.
     */
    TOK_POW     = 0xA9,

    /**
     * @brief AND - Bitwise/logical AND
     *
     * Usage: `A AND B`
     *
     * Performs bitwise AND on 16-bit integer representations.
     * For boolean: -1 AND -1 = -1, -1 AND 0 = 0
     */
    TOK_AND     = 0xAA,

    /**
     * @brief OR - Bitwise/logical OR
     *
     * Usage: `A OR B`
     *
     * Performs bitwise OR on 16-bit integer representations.
     */
    TOK_OR      = 0xAB,

    /**
     * @brief > Greater than comparison
     *
     * Returns -1 (true) or 0 (false).
     */
    TOK_GT      = 0xAC,

    /**
     * @brief = Equals (comparison or assignment)
     *
     * In expressions: comparison, returns -1 or 0.
     * In LET: assignment.
     */
    TOK_EQ      = 0xAD,

    /**
     * @brief < Less than comparison
     *
     * Returns -1 (true) or 0 (false).
     * Combine: <= (<=), >= (>=), <> (not equal)
     */
    TOK_LT      = 0xAE,


    /*------------------------------------------------------------------------
     * FUNCTION TOKENS (0xAF-0xC6)
     *
     * These are built-in functions that take arguments and return values.
     * Numeric functions return MBF numbers.
     * String functions (with $) return strings.
     *------------------------------------------------------------------------*/

    /**
     * @brief SGN - Sign of number
     *
     * Usage: `SGN(x)`
     * Returns: -1 if x<0, 0 if x=0, 1 if x>0
     */
    TOK_SGN     = 0xAF,

    /**
     * @brief INT - Integer part (floor)
     *
     * Usage: `INT(x)`
     * Returns: largest integer <= x
     *
     * Note: INT(-3.5) = -4, not -3!
     */
    TOK_INT     = 0xB0,

    /**
     * @brief ABS - Absolute value
     *
     * Usage: `ABS(x)`
     * Returns: |x|
     */
    TOK_ABS     = 0xB1,

    /**
     * @brief USR - Call machine language subroutine
     *
     * Usage: `USR(address)`
     *
     * Original: calls machine code at address.
     * This implementation: returns 0 with warning.
     */
    TOK_USR     = 0xB2,

    /**
     * @brief FRE - Free memory
     *
     * Usage: `FRE(x)` or `FRE(x$)`
     *
     * Numeric arg: returns free memory bytes.
     * String arg: forces garbage collection, returns string space.
     */
    TOK_FRE     = 0xB3,

    /**
     * @brief INP - Input from I/O port
     *
     * Usage: `INP(port)`
     *
     * Original: reads byte from 8080 I/O port.
     * This implementation: returns 0 with warning.
     */
    TOK_INP     = 0xB4,

    /**
     * @brief POS - Cursor position
     *
     * Usage: `POS(x)`
     *
     * Returns current horizontal cursor position (0-based).
     * Argument is ignored (dummy).
     */
    TOK_POS     = 0xB5,

    /**
     * @brief SQR - Square root
     *
     * Usage: `SQR(x)`
     * Returns: √x
     *
     * Error FC if x < 0.
     */
    TOK_SQR     = 0xB6,

    /**
     * @brief RND - Random number
     *
     * Usage: `RND(x)`
     *
     * x > 0: next random number 0 < result < 1
     * x = 0: repeat last random number
     * x < 0: seed with x, return first number
     *
     * Uses two-stage table-based algorithm for compatibility.
     */
    TOK_RND     = 0xB7,

    /**
     * @brief LOG - Natural logarithm
     *
     * Usage: `LOG(x)`
     * Returns: ln(x)
     *
     * Error FC if x <= 0.
     */
    TOK_LOG     = 0xB8,

    /**
     * @brief EXP - Exponential (e^x)
     *
     * Usage: `EXP(x)`
     * Returns: e^x
     *
     * Error OV if result overflows.
     */
    TOK_EXP     = 0xB9,

    /**
     * @brief COS - Cosine (radians)
     *
     * Usage: `COS(x)`
     * Returns: cos(x)
     */
    TOK_COS     = 0xBA,

    /**
     * @brief SIN - Sine (radians)
     *
     * Usage: `SIN(x)`
     * Returns: sin(x)
     */
    TOK_SIN     = 0xBB,

    /**
     * @brief TAN - Tangent (radians)
     *
     * Usage: `TAN(x)`
     * Returns: tan(x)
     */
    TOK_TAN     = 0xBC,

    /**
     * @brief ATN - Arctangent
     *
     * Usage: `ATN(x)`
     * Returns: arctan(x) in radians (-π/2 to π/2)
     */
    TOK_ATN     = 0xBD,

    /**
     * @brief PEEK - Read memory byte
     *
     * Usage: `PEEK(address)`
     * Returns: byte value at address (0-255)
     *
     * Reads from interpreter's virtual memory.
     */
    TOK_PEEK    = 0xBE,

    /**
     * @brief LEN - String length
     *
     * Usage: `LEN(x$)`
     * Returns: number of characters in string (0-255)
     */
    TOK_LEN     = 0xBF,

    /**
     * @brief STR$ - Number to string
     *
     * Usage: `STR$(x)`
     * Returns: string representation of x
     *
     * Includes leading space for positive numbers.
     */
    TOK_STR     = 0xC0,

    /**
     * @brief VAL - String to number
     *
     * Usage: `VAL(x$)`
     * Returns: numeric value of string
     *
     * Leading spaces ignored. Returns 0 if not a number.
     */
    TOK_VAL     = 0xC1,

    /**
     * @brief ASC - ASCII code of first character
     *
     * Usage: `ASC(x$)`
     * Returns: ASCII value of first character (0-127)
     *
     * Error FC if string is empty.
     */
    TOK_ASC     = 0xC2,

    /**
     * @brief CHR$ - Character from ASCII code
     *
     * Usage: `CHR$(x)`
     * Returns: single-character string
     *
     * Error FC if x not in 0-255.
     */
    TOK_CHR     = 0xC3,

    /**
     * @brief LEFT$ - Left substring
     *
     * Usage: `LEFT$(x$, n)`
     * Returns: leftmost n characters of x$
     *
     * If n > LEN(x$), returns entire string.
     * Error FC if n < 0.
     */
    TOK_LEFT    = 0xC4,

    /**
     * @brief RIGHT$ - Right substring
     *
     * Usage: `RIGHT$(x$, n)`
     * Returns: rightmost n characters of x$
     *
     * If n > LEN(x$), returns entire string.
     */
    TOK_RIGHT   = 0xC5,

    /**
     * @brief MID$ - Middle substring
     *
     * Usage: `MID$(x$, start [, length])`
     * Returns: substring starting at position start
     *
     * start is 1-based. If length omitted, returns rest of string.
     */
    TOK_MID     = 0xC6,
} basic_token_t;


/*============================================================================
 * TOKEN RANGE MACROS
 *
 * These macros help identify token types for parsing and dispatch.
 *============================================================================*/

/** First token value */
#define TOK_FIRST       0x81

/** Last token value */
#define TOK_LAST        0xC6

/**
 * @brief Check if byte is a token
 *
 * @param c Byte to test
 * @return Non-zero if c is a valid token (0x81-0xC6)
 */
#define TOK_IS_TOKEN(c) ((c) >= TOK_FIRST && (c) <= TOK_LAST)

/** Alias for TOK_IS_TOKEN - all tokens are keywords */
#define TOK_IS_KEYWORD(c) TOK_IS_TOKEN(c)

/**
 * @brief Check if token is a statement
 *
 * Statement tokens can start a line or appear after colons.
 *
 * @param c Token to test
 * @return Non-zero if c is a statement token (END through NEW)
 */
#define TOK_IS_STATEMENT(c) ((c) >= TOK_END && (c) <= TOK_NEW)

/**
 * @brief Check if token is a function
 *
 * Function tokens take arguments and return values.
 *
 * @param c Token to test
 * @return Non-zero if c is a function token (SGN through MID$)
 */
#define TOK_IS_FUNCTION(c) ((c) >= TOK_SGN && (c) <= TOK_MID)

/**
 * @brief Check if token is a string function
 *
 * String functions return string values (STR$, CHR$, LEFT$, RIGHT$, MID$).
 *
 * @param c Token to test
 * @return Non-zero if c is a string function
 */
#define TOK_IS_STRING_FUNC(c) ((c) == TOK_STR || (c) == TOK_CHR || \
                               (c) == TOK_LEFT || (c) == TOK_RIGHT || \
                               (c) == TOK_MID)


/*============================================================================
 * KEYWORD TABLE
 *
 * Maps token values to keyword strings for LIST command.
 *============================================================================*/

/**
 * @brief Keyword table
 *
 * Array of keyword strings indexed by (token - 0x81).
 * Used for LIST command detokenization.
 *
 * Example:
 * @code
 *     uint8_t token = TOK_PRINT;  // 0x97
 *     const char *keyword = KEYWORD_TABLE[token - 0x81];  // "PRINT"
 * @endcode
 */
extern const char *const KEYWORD_TABLE[];

/**
 * @brief Number of keywords
 *
 * Count of entries in KEYWORD_TABLE.
 */
extern const size_t KEYWORD_COUNT;


/*============================================================================
 * TOKEN/KEYWORD CONVERSION FUNCTIONS
 *============================================================================*/

/**
 * @brief Get keyword string for a token
 *
 * Converts a token byte to its keyword string for display.
 * Used by the LIST command.
 *
 * @param token Token value (0x81-0xC6)
 * @return Keyword string, or NULL if invalid token
 *
 * Example:
 * @code
 *     const char *kw = token_to_keyword(0x97);  // Returns "PRINT"
 * @endcode
 */
const char *token_to_keyword(uint8_t token);

/**
 * @brief Check if character could start a keyword
 *
 * Quick filter to avoid checking keyword table for every character.
 * All keywords start with letters A-Z.
 *
 * @param c Character to test
 * @return Non-zero if c is a letter (could start keyword)
 */
int is_keyword_start(char c);


/*============================================================================
 * TOKENIZATION FUNCTIONS
 *============================================================================*/

/**
 * @brief Tokenize a BASIC line
 *
 * Converts keywords to token bytes. Preserves:
 * - String literals (inside quotes)
 * - Numbers (kept as ASCII)
 * - Variable names
 * - Operators
 * - Spaces
 *
 * Algorithm:
 * 1. Copy characters until keyword match is found
 * 2. Replace keyword with single token byte
 * 3. Inside quotes: copy verbatim until closing quote
 * 4. After REM: copy rest of line verbatim
 * 5. After DATA: copy until colon or end
 *
 * @param input Source line (null-terminated)
 * @param output Buffer for tokenized line
 * @param output_size Size of output buffer
 * @return Number of bytes written, or 0 on error
 *
 * Example:
 * @code
 *     char input[] = "PRINT \"HELLO\"";
 *     uint8_t output[256];
 *     size_t len = tokenize_line(input, output, sizeof(output));
 *     // output[0] = 0x97 (PRINT), output[1] = ' ', output[2] = '"', ...
 * @endcode
 */
size_t tokenize_line(const char *input, uint8_t *output, size_t output_size);

/**
 * @brief Detokenize a line for display
 *
 * Converts token bytes back to keyword strings.
 * Used by LIST command.
 *
 * @param input Tokenized line
 * @param input_len Length of tokenized line
 * @param output Buffer for detokenized line
 * @param output_size Size of output buffer
 * @return Number of characters written, or 0 on error
 *
 * Example:
 * @code
 *     uint8_t tokenized[] = {0x97, ' ', '"', 'H', 'I', '"'};
 *     char output[256];
 *     size_t len = detokenize_line(tokenized, 6, output, sizeof(output));
 *     // output = "PRINT \"HI\""
 * @endcode
 */
size_t detokenize_line(const uint8_t *input, size_t input_len,
                       char *output, size_t output_size);

/**
 * @brief Find token for keyword at current position
 *
 * Attempts to match a keyword at the current input position.
 * Match is case-insensitive.
 *
 * @param input Pointer to potential keyword start
 * @return Token value (0x81-0xC6) if match found, 0 otherwise
 *
 * Example:
 * @code
 *     uint8_t tok = find_keyword_token("PRINT X");  // Returns 0x97
 *     uint8_t tok2 = find_keyword_token("PRINTER"); // Returns 0x97 (PRINT matches)
 *     uint8_t tok3 = find_keyword_token("XYZ");     // Returns 0 (no match)
 * @endcode
 *
 * Note: Keywords are matched greedily but non-alphabetic character
 * must follow (or end of string) to prevent "PRINTING" matching "PRINT".
 */
uint8_t find_keyword_token(const char *input);

#endif /* BASIC8K_TOKENS_H */
