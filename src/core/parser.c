/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file parser.c
 * @brief Expression Parser and Evaluator for 8K BASIC
 *
 * This module implements a recursive descent parser that directly evaluates
 * BASIC expressions. Unlike a traditional parser that builds an AST, this
 * parser evaluates expressions as it parses them - the same approach used
 * by the original 8K BASIC interpreter.
 *
 * ## Parser Architecture
 *
 * The parser uses a hierarchy of functions, each handling a specific
 * precedence level. Lower precedence operators are handled first:
 *
 * ```
 *   eval_expression()
 *         |
 *         v
 *   parse_expression()
 *         |
 *         v
 *   parse_or_expr()        <- OR (lowest precedence)
 *         |
 *         v
 *   parse_and_expr()       <- AND
 *         |
 *         v
 *   parse_not_expr()       <- NOT (unary)
 *         |
 *         v
 *   parse_relational()     <- =, <>, <, >, <=, >=
 *         |
 *         v
 *   parse_additive()       <- +, -
 *         |
 *         v
 *   parse_multiplicative() <- *, /
 *         |
 *         v
 *   parse_power()          <- ^ (exponentiation)
 *         |
 *         v
 *   parse_unary()          <- unary -, +
 *         |
 *         v
 *   parse_primary()        <- numbers, variables, functions, (expr)
 * ```
 *
 * ## Operator Precedence (lowest to highest)
 *
 * | Level | Operators        | Associativity |
 * |-------|------------------|---------------|
 * | 1     | OR               | Left          |
 * | 2     | AND              | Left          |
 * | 3     | NOT              | Unary (right) |
 * | 4     | =, <>, <, >, <=, >=| Left        |
 * | 5     | +, -             | Left          |
 * | 6     | *, /             | Left          |
 * | 7     | ^                | Left*         |
 * | 8     | unary -, +       | Unary (right) |
 * | 9     | primary          | -             |
 *
 * *Note: Standard math has ^ as right-associative, but we do left for simplicity.
 *
 * ## String Expression Handling
 *
 * String expressions are handled separately from numeric:
 * - parse_string_term(): Single string (literal, variable, function)
 * - parse_string_arg(): String with concatenation (+)
 * - parse_string_function(): LEFT$, RIGHT$, MID$, CHR$, STR$
 *
 * String comparisons are detected in parse_relational() and handled specially.
 *
 * ## Error Handling
 *
 * Errors are recorded in the parse_state_t.error field. Once set, parsing
 * continues but results are undefined. The caller checks for errors.
 *
 * ## Public Entry Points
 *
 * - eval_expression(): Evaluate a numeric expression
 * - eval_string_expression(): Evaluate a string expression (returns char*)
 * - eval_string_desc(): Evaluate a string expression (returns string_desc_t)
 */

#include "basic/basic.h"
#include "basic/tokens.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>


/*============================================================================
 * PARSER STATE
 *
 * The parse_state_t structure tracks the current position in the input
 * and provides access to the interpreter state for variable lookup.
 *============================================================================*/

/**
 * @brief Parser state for expression evaluation
 *
 * This structure is passed through all parsing functions. It contains:
 * - Input text and current position
 * - Reference to interpreter state (for variable access)
 * - Error tracking
 */
typedef struct {
    const uint8_t *text;    /**< Tokenized input text */
    size_t pos;             /**< Current parse position (byte offset) */
    size_t len;             /**< Total length of input */
    basic_state_t *basic;   /**< Interpreter state for variable lookup */
    basic_error_t error;    /**< Error code if parsing failed */
} parse_state_t;


/*============================================================================
 * FORWARD DECLARATIONS
 *
 * The parsing functions form a hierarchy based on operator precedence.
 * Each level calls the next higher precedence level.
 *============================================================================*/

/* Forward declarations */
static mbf_t parse_expression(parse_state_t *ps);
static mbf_t parse_or_expr(parse_state_t *ps);
static mbf_t parse_and_expr(parse_state_t *ps);
static mbf_t parse_not_expr(parse_state_t *ps);
static mbf_t parse_relational(parse_state_t *ps);
static mbf_t parse_additive(parse_state_t *ps);
static mbf_t parse_multiplicative(parse_state_t *ps);
static mbf_t parse_power(parse_state_t *ps);
static mbf_t parse_unary(parse_state_t *ps);
static mbf_t parse_primary(parse_state_t *ps);
static mbf_t parse_number(parse_state_t *ps);
static mbf_t parse_function(parse_state_t *ps, uint8_t token);
static string_desc_t parse_string_term(parse_state_t *ps);
static string_desc_t parse_string_arg(parse_state_t *ps);
static string_desc_t parse_string_function(parse_state_t *ps);


/*============================================================================
 * TOKENIZED INPUT HELPERS
 *
 * These functions provide low-level access to the tokenized input stream.
 * They handle the mechanics of reading and advancing through tokens.
 *============================================================================*/

/**
 * @brief Peek at current token without consuming it
 *
 * @param ps Parser state
 * @return Current token byte, or 0 if at end
 */
static uint8_t peek(parse_state_t *ps) {
    if (ps->pos >= ps->len) return 0;
    return ps->text[ps->pos];
}

/* Consume and return current token */
static uint8_t consume(parse_state_t *ps) {
    if (ps->pos >= ps->len) return 0;
    return ps->text[ps->pos++];
}

/* Skip whitespace (shouldn't be any after tokenization, but just in case) */
static void skip_space(parse_state_t *ps) {
    while (ps->pos < ps->len && ps->text[ps->pos] == ' ') {
        ps->pos++;
    }
}

/* Check if current position is at end or statement separator */
__attribute__((unused))
static bool at_end(parse_state_t *ps) {
    skip_space(ps);
    uint8_t c = peek(ps);
    return c == 0 || c == ':' || c == '\n' || c == '\r';
}

/* Expect and consume a specific character */
static bool expect(parse_state_t *ps, uint8_t expected) {
    skip_space(ps);
    if (peek(ps) == expected) {
        consume(ps);
        return true;
    }
    return false;
}


/*============================================================================
 * STRING EXPRESSION PARSING
 *
 * String expressions are handled separately from numeric expressions.
 * String functions (LEFT$, RIGHT$, MID$, CHR$, STR$) and string
 * concatenation (+) are handled here.
 *============================================================================*/

/**
 * @brief Parse a single string term
 *
 * Parses one of:
 * - String literal: "HELLO"
 * - String variable: A$
 * - String array element: A$(1)
 * - String function: LEFT$, RIGHT$, MID$, CHR$, STR$
 *
 * Does NOT handle concatenation - use parse_string_arg() for that.
 *
 * @param ps Parser state
 * @return String descriptor, or {0} on error
 */
static string_desc_t parse_string_term(parse_state_t *ps) {
    string_desc_t result = {0};
    skip_space(ps);

    uint8_t c = peek(ps);

    if (c == '"') {
        /* String literal */
        consume(ps);  /* Skip opening quote */
        size_t start = ps->pos;
        while (ps->pos < ps->len && ps->text[ps->pos] != '"' && ps->text[ps->pos] != '\0') {
            ps->pos++;
        }
        size_t len = ps->pos - start;
        if (peek(ps) == '"') consume(ps);  /* Skip closing quote */

        if (ps->basic && len > 0) {
            result = string_create_len(ps->basic, (const char *)(ps->text + start), (uint8_t)len);
        }
    } else if (isalpha(c)) {
        /* String variable or array */
        char var_name[4] = {0};
        size_t name_len = 0;
        var_name[name_len++] = (char)consume(ps);
        if (ps->pos < ps->len && isalnum(peek(ps))) {
            var_name[name_len++] = (char)consume(ps);
        }
        if (peek(ps) == '$') {
            consume(ps);  /* Skip $ */
            var_name[name_len] = '$';

            /* Check for array subscript */
            skip_space(ps);
            if (peek(ps) == '(') {
                /* String array access */
                consume(ps);  /* Skip ( */
                mbf_t idx1_val = parse_expression(ps);
                if (ps->error != ERR_NONE) return result;

                bool overflow;
                int16_t idx1 = mbf_to_int16(idx1_val, &overflow);
                if (overflow) {
                    ps->error = ERR_BS;
                    return result;
                }

                int16_t idx2 = -1;  /* -1 means 1D array */
                skip_space(ps);
                if (peek(ps) == ',') {
                    consume(ps);  /* Skip , */
                    mbf_t idx2_val = parse_expression(ps);
                    if (ps->error != ERR_NONE) return result;
                    idx2 = mbf_to_int16(idx2_val, &overflow);
                    if (overflow) {
                        ps->error = ERR_BS;
                        return result;
                    }
                }

                skip_space(ps);
                if (peek(ps) != ')') {
                    ps->error = ERR_SN;
                    return result;
                }
                consume(ps);  /* Skip ) */

                if (ps->basic) {
                    result = array_get_string(ps->basic, var_name, idx1, idx2);
                }
            } else {
                /* Simple string variable */
                if (ps->basic) {
                    result = var_get_string(ps->basic, var_name);
                }
            }
        } else {
            ps->error = ERR_TM;  /* Type mismatch - expected string */
        }
    } else if (TOK_IS_STRING_FUNC(c)) {
        /* String function */
        result = parse_string_function(ps);
    } else {
        ps->error = ERR_TM;  /* Type mismatch */
    }

    return result;
}

/*
 * Parse a string argument (for LEN, ASC, VAL, etc.)
 * Handles string literals, string variables, string functions, and concatenation.
 */
static string_desc_t parse_string_arg(parse_state_t *ps) {
    string_desc_t result = parse_string_term(ps);
    if (ps->error != ERR_NONE) return result;

    /* Handle string concatenation with + */
    skip_space(ps);
    while (peek(ps) == '+' || peek(ps) == TOK_PLUS) {
        consume(ps);  /* Skip + */
        skip_space(ps);

        string_desc_t right = parse_string_term(ps);
        if (ps->error != ERR_NONE) return result;

        /* Concatenate */
        if (ps->basic) {
            result = string_concat(ps->basic, result, right);
        }
        skip_space(ps);
    }

    return result;
}

/*
 * Parse a string function call (LEFT$, RIGHT$, MID$, CHR$, STR$)
 */
static string_desc_t parse_string_function(parse_state_t *ps) {
    string_desc_t result = {0};
    uint8_t token = consume(ps);

    if (!expect(ps, '(')) {
        ps->error = ERR_SN;
        return result;
    }

    switch (token) {
        case TOK_LEFT:
        case TOK_RIGHT: {
            /* LEFT$(str, n) or RIGHT$(str, n) */
            string_desc_t str = parse_string_arg(ps);
            if (ps->error != ERR_NONE) return result;

            if (!expect(ps, ',')) {
                ps->error = ERR_SN;
                return result;
            }

            mbf_t n = parse_expression(ps);
            bool overflow;
            int16_t count = mbf_to_int16(n, &overflow);
            if (count < 0) count = 0;
            if (count > 255) count = 255;

            if (!expect(ps, ')')) {
                ps->error = ERR_SN;
                return result;
            }

            if (ps->basic) {
                if (token == TOK_LEFT) {
                    result = string_left(ps->basic, str, (uint8_t)count);
                } else {
                    result = string_right(ps->basic, str, (uint8_t)count);
                }
            }
            break;
        }

        case TOK_MID: {
            /* MID$(str, start, n) or MID$(str, start) */
            string_desc_t str = parse_string_arg(ps);
            if (ps->error != ERR_NONE) return result;

            if (!expect(ps, ',')) {
                ps->error = ERR_SN;
                return result;
            }

            mbf_t start_mbf = parse_expression(ps);
            bool overflow;
            int16_t start = mbf_to_int16(start_mbf, &overflow);
            if (start < 1) start = 1;

            uint8_t count = 255;  /* Default: rest of string */
            if (expect(ps, ',')) {
                mbf_t n = parse_expression(ps);
                int16_t c = mbf_to_int16(n, &overflow);
                if (c < 0) c = 0;
                if (c > 255) c = 255;
                count = (uint8_t)c;
            }

            if (!expect(ps, ')')) {
                ps->error = ERR_SN;
                return result;
            }

            if (ps->basic) {
                result = string_mid(ps->basic, str, (uint8_t)start, count);
            }
            break;
        }

        case TOK_CHR: {
            /* CHR$(n) */
            mbf_t code = parse_expression(ps);
            bool overflow;
            int16_t c = mbf_to_int16(code, &overflow);

            if (!expect(ps, ')')) {
                ps->error = ERR_SN;
                return result;
            }

            if (ps->basic && c >= 0 && c <= 255) {
                result = string_chr(ps->basic, (uint8_t)c);
            }
            break;
        }

        case TOK_STR: {
            /* STR$(n) */
            mbf_t value = parse_expression(ps);

            if (!expect(ps, ')')) {
                ps->error = ERR_SN;
                return result;
            }

            if (ps->basic) {
                result = string_str(ps->basic, value);
            }
            break;
        }

        default:
            ps->error = ERR_SN;
            break;
    }

    return result;
}


/*============================================================================
 * NUMERIC EXPRESSION PARSING
 *
 * These functions implement the recursive descent parser for numeric
 * expressions. Each function handles one precedence level.
 *============================================================================*/

/**
 * @brief Parse a complete numeric expression
 *
 * Entry point for numeric expression parsing. Delegates to
 * parse_or_expr() which is the lowest precedence level.
 *
 * @param ps Parser state
 * @return Evaluated result as MBF number
 */
static mbf_t parse_expression(parse_state_t *ps) {
    skip_space(ps);
    return parse_or_expr(ps);
}

/*
 * Parse OR expression: and_expr (OR and_expr)*
 */
static mbf_t parse_or_expr(parse_state_t *ps) {
    mbf_t left = parse_and_expr(ps);

    while (peek(ps) == TOK_OR) {
        consume(ps);
        mbf_t right = parse_and_expr(ps);

        /* OR: convert to integers, bitwise OR */
        int16_t a = mbf_to_int16(left, NULL);
        int16_t b = mbf_to_int16(right, NULL);
        left = mbf_from_int16(a | b);
    }

    return left;
}

/*
 * Parse AND expression: not_expr (AND not_expr)*
 */
static mbf_t parse_and_expr(parse_state_t *ps) {
    mbf_t left = parse_not_expr(ps);

    while (peek(ps) == TOK_AND) {
        consume(ps);
        mbf_t right = parse_not_expr(ps);

        /* AND: convert to integers, bitwise AND */
        int16_t a = mbf_to_int16(left, NULL);
        int16_t b = mbf_to_int16(right, NULL);
        left = mbf_from_int16(a & b);
    }

    return left;
}

/*
 * Parse NOT expression: NOT* relational
 */
static mbf_t parse_not_expr(parse_state_t *ps) {
    if (peek(ps) == TOK_NOT) {
        consume(ps);
        mbf_t val = parse_not_expr(ps);

        /* NOT: convert to integer, bitwise complement */
        int16_t a = mbf_to_int16(val, NULL);
        return mbf_from_int16(~a);
    }

    return parse_relational(ps);
}

/*
 * Check if current position looks like a string expression.
 * Returns true for: string variable (A$), string literal ("..."), string function (LEFT$, etc.)
 */
static bool is_string_expr_start(parse_state_t *ps) {
    size_t save_pos = ps->pos;
    skip_space(ps);
    uint8_t c = peek(ps);

    bool is_string = false;

    if (c == '"') {
        is_string = true;
    } else if (TOK_IS_STRING_FUNC(c)) {
        is_string = true;
    } else if (isalpha(c)) {
        /* Look ahead for $ after variable name */
        consume(ps);
        if (ps->pos < ps->len && isalnum(peek(ps))) {
            consume(ps);
        }
        if (peek(ps) == '$') {
            is_string = true;
        }
    }

    ps->pos = save_pos;
    return is_string;
}

/*
 * Compare two strings.
 * Returns: -1 if s1 < s2, 0 if s1 == s2, 1 if s1 > s2
 */
static int string_cmp(basic_state_t *state, string_desc_t s1, string_desc_t s2) {
    const char *p1 = (s1.ptr > 0 && state) ? (const char *)(state->memory + s1.ptr) : "";
    const char *p2 = (s2.ptr > 0 && state) ? (const char *)(state->memory + s2.ptr) : "";
    size_t len1 = s1.length;
    size_t len2 = s2.length;
    size_t min_len = len1 < len2 ? len1 : len2;

    for (size_t i = 0; i < min_len; i++) {
        if ((unsigned char)p1[i] < (unsigned char)p2[i]) return -1;
        if ((unsigned char)p1[i] > (unsigned char)p2[i]) return 1;
    }

    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

/*
 * Parse relational expression: additive ((=|<>|<|>|<=|>=) additive)?
 * Also handles string comparisons.
 */
static mbf_t parse_relational(parse_state_t *ps) {
    /* Check if this is a string comparison */
    if (is_string_expr_start(ps)) {
        string_desc_t left = parse_string_arg(ps);
        if (ps->error != ERR_NONE) return MBF_ZERO;

        skip_space(ps);
        uint8_t op = peek(ps);

        /* Check for relational operators */
        if (op == TOK_EQ || op == TOK_LT || op == TOK_GT || op == '=' || op == '<' || op == '>') {
            consume(ps);

            /* Check for compound operators: <>, <=, >= */
            uint8_t op2 = peek(ps);
            int cmp_type = 0;  /* 0=eq, 1=lt, 2=gt, 3=le, 4=ge, 5=ne */

            if (op == TOK_EQ || op == '=') {
                cmp_type = 0;  /* = */
            } else if (op == TOK_LT || op == '<') {
                if (op2 == TOK_GT || op2 == '>') {
                    consume(ps);
                    cmp_type = 5;  /* <> (not equal) */
                } else if (op2 == TOK_EQ || op2 == '=') {
                    consume(ps);
                    cmp_type = 3;  /* <= */
                } else {
                    cmp_type = 1;  /* < */
                }
            } else if (op == TOK_GT || op == '>') {
                if (op2 == TOK_EQ || op2 == '=') {
                    consume(ps);
                    cmp_type = 4;  /* >= */
                } else {
                    cmp_type = 2;  /* > */
                }
            }

            string_desc_t right = parse_string_arg(ps);
            if (ps->error != ERR_NONE) return MBF_ZERO;

            int cmp = string_cmp(ps->basic, left, right);

            int result = 0;
            switch (cmp_type) {
                case 0: result = (cmp == 0) ? -1 : 0; break;  /* = */
                case 1: result = (cmp < 0) ? -1 : 0; break;   /* < */
                case 2: result = (cmp > 0) ? -1 : 0; break;   /* > */
                case 3: result = (cmp <= 0) ? -1 : 0; break;  /* <= */
                case 4: result = (cmp >= 0) ? -1 : 0; break;  /* >= */
                case 5: result = (cmp != 0) ? -1 : 0; break;  /* <> */
            }

            return mbf_from_int16((int16_t)result);
        }

        /* String expression without comparison - error in numeric context */
        ps->error = ERR_TM;
        return MBF_ZERO;
    }

    /* Numeric comparison */
    mbf_t left = parse_additive(ps);

    skip_space(ps);
    uint8_t op = peek(ps);

    /* Check for relational operators */
    if (op == TOK_EQ || op == TOK_LT || op == TOK_GT) {
        consume(ps);

        /* Check for compound operators: <>, <=, >= */
        uint8_t op2 = peek(ps);
        int cmp_type = 0;  /* 0=eq, 1=lt, 2=gt, 3=le, 4=ge, 5=ne */

        if (op == TOK_EQ) {
            cmp_type = 0;  /* = */
        } else if (op == TOK_LT) {
            if (op2 == TOK_GT) {
                consume(ps);
                cmp_type = 5;  /* <> (not equal) */
            } else if (op2 == TOK_EQ) {
                consume(ps);
                cmp_type = 3;  /* <= */
            } else {
                cmp_type = 1;  /* < */
            }
        } else if (op == TOK_GT) {
            if (op2 == TOK_EQ) {
                consume(ps);
                cmp_type = 4;  /* >= */
            } else {
                cmp_type = 2;  /* > */
            }
        }

        mbf_t right = parse_additive(ps);
        int cmp = mbf_cmp(left, right);

        int result = 0;
        switch (cmp_type) {
            case 0: result = (cmp == 0) ? -1 : 0; break;  /* = */
            case 1: result = (cmp < 0) ? -1 : 0; break;   /* < */
            case 2: result = (cmp > 0) ? -1 : 0; break;   /* > */
            case 3: result = (cmp <= 0) ? -1 : 0; break;  /* <= */
            case 4: result = (cmp >= 0) ? -1 : 0; break;  /* >= */
            case 5: result = (cmp != 0) ? -1 : 0; break;  /* <> */
        }

        return mbf_from_int16((int16_t)result);
    }

    return left;
}

/*
 * Parse additive expression: multiplicative ((+|-) multiplicative)*
 */
static mbf_t parse_additive(parse_state_t *ps) {
    mbf_t left = parse_multiplicative(ps);

    for (;;) {
        skip_space(ps);
        uint8_t op = peek(ps);

        if (op == TOK_PLUS) {
            consume(ps);
            mbf_t right = parse_multiplicative(ps);
            left = mbf_add(left, right);
        } else if (op == TOK_MINUS) {
            consume(ps);
            mbf_t right = parse_multiplicative(ps);
            left = mbf_sub(left, right);
        } else {
            break;
        }
    }

    return left;
}

/*
 * Parse multiplicative expression: power ((*|/) power)*
 */
static mbf_t parse_multiplicative(parse_state_t *ps) {
    mbf_t left = parse_power(ps);

    for (;;) {
        skip_space(ps);
        uint8_t op = peek(ps);

        if (op == TOK_MUL) {
            consume(ps);
            mbf_t right = parse_power(ps);
            left = mbf_mul(left, right);
        } else if (op == TOK_DIV) {
            consume(ps);
            mbf_t right = parse_power(ps);
            left = mbf_div(left, right);
        } else {
            break;
        }
    }

    return left;
}

/*
 * Parse power expression: unary (^ unary)*
 * Note: ^ is right-associative, but we implement left-to-right for simplicity
 */
static mbf_t parse_power(parse_state_t *ps) {
    mbf_t left = parse_unary(ps);

    while (peek(ps) == TOK_POW) {
        consume(ps);
        mbf_t right = parse_unary(ps);

        /* Exponentiation: a^b = exp(b * log(a)) */
        /* TODO: Use proper MBF implementation */
        /* For now, use a simple approximation */
        if (mbf_is_zero(left)) {
            left = MBF_ZERO;
        } else {
            bool overflow;
            int32_t exp_int = mbf_to_int32(right, &overflow);
            if (!overflow && exp_int >= 0 && exp_int <= 10) {
                /* Small positive integer exponent - use repeated multiplication */
                mbf_t result = mbf_from_int16(1);
                for (int32_t i = 0; i < exp_int; i++) {
                    result = mbf_mul(result, left);
                }
                left = result;
            } else {
                /* TODO: implement general exponentiation */
                ps->error = ERR_OV;
                left = MBF_ZERO;
            }
        }
    }

    return left;
}

/*
 * Parse unary expression: (+|-)? primary
 */
static mbf_t parse_unary(parse_state_t *ps) {
    skip_space(ps);
    uint8_t op = peek(ps);

    if (op == TOK_PLUS) {
        consume(ps);
        return parse_unary(ps);
    } else if (op == TOK_MINUS) {
        consume(ps);
        mbf_t val = parse_unary(ps);
        return mbf_neg(val);
    }

    return parse_primary(ps);
}

/*
 * Parse primary expression: number | variable | function | (expr)
 */
static mbf_t parse_primary(parse_state_t *ps) {
    skip_space(ps);
    uint8_t c = peek(ps);

    /* Parenthesized expression */
    if (c == '(') {
        consume(ps);
        mbf_t val = parse_expression(ps);
        if (!expect(ps, ')')) {
            ps->error = ERR_SN;
        }
        return val;
    }

    /* Number literal */
    if (isdigit(c) || c == '.') {
        return parse_number(ps);
    }

    /* Function call */
    if (TOK_IS_FUNCTION(c)) {
        consume(ps);
        return parse_function(ps, c);
    }

    /* User-defined function FN (either as token or literal "FN") */
    bool is_fn = (c == TOK_FN) ||
                 (toupper(c) == 'F' &&
                  ps->pos + 1 < ps->len &&
                  toupper(ps->text[ps->pos + 1]) == 'N' &&
                  ps->pos + 2 < ps->len &&
                  isalpha(ps->text[ps->pos + 2]));
    if (is_fn) {
        char fn_name;
        if (c == TOK_FN) {
            consume(ps);  /* Consume FN token */
            /* Get function name (A-Z) */
            if (ps->pos >= ps->len || !isalpha(peek(ps))) {
                ps->error = ERR_SN;
                return MBF_ZERO;
            }
            fn_name = (char)toupper(consume(ps));
        } else {
            /* Literal "FN" */
            consume(ps);  /* Consume 'F' */
            consume(ps);  /* Consume 'N' */
            fn_name = (char)toupper(consume(ps));  /* Get function name */
        }
        int fn_idx = fn_name - 'A';

        if (!ps->basic || fn_idx < 0 || fn_idx >= 26 ||
            ps->basic->user_funcs[fn_idx].name == 0) {
            ps->error = ERR_UF;  /* Undefined function */
            return MBF_ZERO;
        }

        /* Expect opening paren for argument */
        if (!expect(ps, '(')) {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }

        /* Evaluate argument */
        mbf_t arg_value = parse_expression(ps);

        if (!expect(ps, ')')) {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }

        /* Get function definition pointer */
        uint16_t def_ptr = ps->basic->user_funcs[fn_idx].ptr;
        const uint8_t *def_text = ps->basic->memory + def_ptr;

        /* Parse parameter from definition: (X) = expr */
        size_t dp = 0;
        if (def_text[dp] != '(') {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }
        dp++;

        /* Get parameter name */
        char param_name[3] = {0};
        if (!isalpha(def_text[dp])) {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }
        param_name[0] = (char)def_text[dp++];
        if (isalnum(def_text[dp])) {
            param_name[1] = (char)def_text[dp++];
        }

        if (def_text[dp] != ')') {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }
        dp++;

        /* Skip = (may be literal '=' or TOK_EQ token) */
        while (def_text[dp] == ' ') dp++;
        if (def_text[dp] != '=' && def_text[dp] != TOK_EQ) {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }
        dp++;

        /* Save current value of parameter */
        mbf_t saved_value = var_get_numeric(ps->basic, param_name);

        /* Set parameter to argument value */
        var_set_numeric(ps->basic, param_name, arg_value);

        /* Find end of definition (end of line or colon) */
        size_t def_len = 0;
        while (def_text[dp + def_len] != '\0' && def_text[dp + def_len] != ':') {
            def_len++;
        }

        /* Evaluate expression */
        basic_error_t err;
        size_t consumed;
        mbf_t result = eval_expression(ps->basic, def_text + dp, def_len, &consumed, &err);

        /* Restore parameter */
        var_set_numeric(ps->basic, param_name, saved_value);

        if (err != ERR_NONE) {
            ps->error = err;
            return MBF_ZERO;
        }

        return result;
    }

    /* Variable lookup */
    if (isalpha(c)) {
        char var_name[3] = {0};
        var_name[0] = (char)consume(ps);

        /* Second character of variable name */
        if (ps->pos < ps->len && isalnum(peek(ps))) {
            var_name[1] = (char)consume(ps);
        }

        /* Check for string variable (ends with $) - not handled for numeric expressions */
        if (peek(ps) == '$') {
            consume(ps);
            /* String variable in numeric context - return 0 */
            return MBF_ZERO;
        }

        /* Check for array subscript */
        if (peek(ps) == '(') {
            consume(ps);  /* Consume ( */

            /* Parse first index */
            mbf_t idx1_val = parse_expression(ps);
            bool overflow;
            int idx1 = mbf_to_int16(idx1_val, &overflow);

            int idx2 = -1;  /* -1 indicates 1D array */
            if (peek(ps) == ',') {
                consume(ps);
                mbf_t idx2_val = parse_expression(ps);
                idx2 = mbf_to_int16(idx2_val, &overflow);
            }

            if (!expect(ps, ')')) {
                ps->error = ERR_SN;
                return MBF_ZERO;
            }

            /* Look up array element */
            if (ps->basic) {
                return array_get_numeric(ps->basic, var_name, idx1, idx2);
            }
            return MBF_ZERO;
        }

        /* Simple variable lookup */
        if (ps->basic) {
            return var_get_numeric(ps->basic, var_name);
        }
        return MBF_ZERO;
    }

    /* Unknown - return 0 and set error */
    ps->error = ERR_SN;
    return MBF_ZERO;
}

/*
 * Parse a number literal.
 */
static mbf_t parse_number(parse_state_t *ps) {
    char buf[64];
    size_t len = 0;

    /* Collect digits and decimal point */
    while (len < sizeof(buf) - 1) {
        uint8_t c = peek(ps);
        if (isdigit(c) || c == '.' || c == 'E' || c == 'e' ||
            ((c == '+' || c == '-') && len > 0 &&
             (buf[len-1] == 'E' || buf[len-1] == 'e'))) {
            buf[len++] = (char)consume(ps);
        } else {
            break;
        }
    }
    buf[len] = '\0';

    /* Parse the number */
    mbf_t result;
    if (mbf_from_string(buf, &result) == 0) {
        /* Fallback: use strtod and convert */
        double val = strtod(buf, NULL);
        /* Simple conversion - TODO: proper MBF conversion */
        if (val == 0.0) {
            return MBF_ZERO;
        }
        int32_t intval = (int32_t)val;
        if (val == (double)intval && intval >= -32768 && intval <= 32767) {
            return mbf_from_int16((int16_t)intval);
        }
        /* TODO: proper float conversion */
        return mbf_from_int32(intval);
    }

    return result;
}

/*
 * Parse a function call.
 */
static mbf_t parse_function(parse_state_t *ps, uint8_t token) {
    mbf_t arg = MBF_ZERO;

    /* Handle string functions specially - they take string arguments */
    if (token == TOK_LEN || token == TOK_ASC || token == TOK_VAL) {
        if (!expect(ps, '(')) {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }

        string_desc_t str = parse_string_arg(ps);

        if (!expect(ps, ')')) {
            ps->error = ERR_SN;
            return MBF_ZERO;
        }

        switch (token) {
            case TOK_LEN:
                return mbf_from_int16(str.length);

            case TOK_ASC:
                if (str.length > 0 && str.ptr > 0 && ps->basic) {
                    return mbf_from_int16(ps->basic->memory[str.ptr]);
                }
                ps->error = ERR_FC;  /* Illegal function call */
                return MBF_ZERO;

            case TOK_VAL: {
                if (str.length == 0 || str.ptr == 0 || !ps->basic) {
                    return MBF_ZERO;
                }
                /* Copy string to buffer and parse as number */
                char buf[256];
                size_t len = str.length < 255 ? str.length : 255;
                memcpy(buf, ps->basic->memory + str.ptr, len);
                buf[len] = '\0';
                mbf_t result;
                if (mbf_from_string(buf, &result) > 0) {
                    return result;
                }
                return MBF_ZERO;
            }
            default:
                return MBF_ZERO;
        }
    }

    /* Most functions require parentheses */
    if (!expect(ps, '(')) {
        ps->error = ERR_SN;
        return MBF_ZERO;
    }

    arg = parse_expression(ps);

    if (!expect(ps, ')')) {
        ps->error = ERR_SN;
        return MBF_ZERO;
    }

    /* Dispatch based on function token */
    switch (token) {
        case TOK_ABS:
            return mbf_abs(arg);

        case TOK_SGN:
            return mbf_from_int16((int16_t)mbf_sign(arg));

        case TOK_INT:
            return mbf_int(arg);

        case TOK_SQR:
            return mbf_sqr(arg);

        case TOK_RND:
            if (ps->basic) {
                return basic_rnd(ps->basic, arg);
            }
            return MBF_ZERO;

        case TOK_SIN:
            return mbf_sin(arg);

        case TOK_COS:
            return mbf_cos(arg);

        case TOK_TAN:
            return mbf_tan(arg);

        case TOK_ATN:
            return mbf_atn(arg);

        case TOK_LOG:
            return mbf_log(arg);

        case TOK_EXP:
            return mbf_exp(arg);

        case TOK_PEEK: {
            bool overflow;
            int16_t addr = mbf_to_int16(arg, &overflow);
            if (ps->basic && !overflow && addr >= 0) {
                uint16_t uaddr = (uint16_t)addr;
                if (uaddr < ps->basic->memory_size) {
                    return mbf_from_int16(ps->basic->memory[uaddr]);
                }
            }
            return MBF_ZERO;
        }

        case TOK_FRE:
            if (ps->basic) {
                return mbf_from_int32((int32_t)basic_free_memory(ps->basic));
            }
            return MBF_ZERO;

        case TOK_POS:
            if (ps->basic) {
                return mbf_from_int16(ps->basic->terminal_x);
            }
            return MBF_ZERO;

        case TOK_USR:
            /* USR(addr) - call machine code, not supported */
            if (ps->basic && !ps->basic->warned_usr) {
                fprintf(ps->basic->output, "?USR NOT SUPPORTED\n");
                ps->basic->warned_usr = true;
            }
            return MBF_ZERO;

        case TOK_INP:
            /* INP(port) - input from port, not supported */
            if (ps->basic && !ps->basic->warned_inp) {
                fprintf(ps->basic->output, "?INP NOT SUPPORTED\n");
                ps->basic->warned_inp = true;
            }
            return MBF_ZERO;

        default:
            ps->error = ERR_SN;
            return MBF_ZERO;
    }
}


/*============================================================================
 * PUBLIC INTERFACE
 *
 * These functions are the public entry points used by the interpreter
 * to evaluate expressions from tokenized BASIC code.
 *============================================================================*/

/**
 * @brief Evaluate a numeric expression from tokenized text
 *
 * This is the main entry point for expression evaluation. It creates
 * a parser state and evaluates the expression at the current position.
 *
 * @param state Interpreter state (for variable lookup)
 * @param text Tokenized input text
 * @param len Length of input text
 * @param[out] consumed Number of bytes consumed (NULL to ignore)
 * @param[out] error Error code if evaluation failed (NULL to ignore)
 * @return Evaluated result as MBF number
 *
 * Example:
 * @code
 *     size_t consumed;
 *     basic_error_t err;
 *     mbf_t result = eval_expression(state, tokenized + pos, len - pos,
 *                                    &consumed, &err);
 *     if (err != ERR_NONE) {
 *         // Handle error
 *     }
 *     pos += consumed;
 * @endcode
 */
mbf_t eval_expression(basic_state_t *state, const uint8_t *text, size_t len,
                      size_t *consumed, basic_error_t *error) {
    parse_state_t ps = {
        .text = text,
        .pos = 0,
        .len = len,
        .basic = state,
        .error = ERR_NONE
    };

    mbf_t result = parse_expression(&ps);

    if (consumed) *consumed = ps.pos;
    if (error) *error = ps.error;

    return result;
}

/**
 * @brief Evaluate a string expression and return pointer
 *
 * Evaluates a string expression and returns a pointer to the string
 * data in the interpreter's memory space.
 *
 * @param state Interpreter state
 * @param text Tokenized input text
 * @param len Length of input text
 * @param[out] consumed Number of bytes consumed
 * @param[out] error Error code if evaluation failed
 * @return Pointer to string data, or NULL on error/empty string
 *
 * @note The returned pointer is only valid until the next string
 *       operation that might cause garbage collection.
 */
const char *eval_string_expression(basic_state_t *state, const uint8_t *text,
                                   size_t len, size_t *consumed,
                                   basic_error_t *error) {
    parse_state_t ps = {
        .text = text,
        .pos = 0,
        .len = len,
        .basic = state,
        .error = ERR_NONE
    };

    string_desc_t result = parse_string_arg(&ps);

    if (consumed) *consumed = ps.pos;
    if (error) *error = ps.error;

    if (ps.error != ERR_NONE || result.length == 0) {
        return NULL;
    }

    return (const char *)(state->memory + result.ptr);
}

/*
 * Evaluate a string expression and return descriptor.
 */
string_desc_t eval_string_desc(basic_state_t *state, const uint8_t *text,
                               size_t len, size_t *consumed,
                               basic_error_t *error) {
    parse_state_t ps = {
        .text = text,
        .pos = 0,
        .len = len,
        .basic = state,
        .error = ERR_NONE
    };

    string_desc_t result = parse_string_arg(&ps);

    if (consumed) *consumed = ps.pos;
    if (error) *error = ps.error;

    return result;
}
