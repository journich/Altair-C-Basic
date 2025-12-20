/*
 * parser.c - Expression Parser for 8K BASIC
 *
 * Implements a recursive descent parser that directly evaluates expressions.
 * This matches the original 8K BASIC approach where parsing and evaluation
 * are interleaved.
 *
 * Operator Precedence (lowest to highest):
 * 1. OR
 * 2. AND
 * 3. NOT (unary)
 * 4. Relational: =, <>, <, >, <=, >=
 * 5. Addition/Subtraction: +, -
 * 6. Multiplication/Division: *, /
 * 7. Exponentiation: ^
 * 8. Unary: -, +
 * 9. Primary: numbers, variables, functions, (expr)
 */

#include "basic/basic.h"
#include "basic/tokens.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Parser state - tracks current position in tokenized line */
typedef struct {
    const uint8_t *text;    /* Tokenized text */
    size_t pos;             /* Current position */
    size_t len;             /* Total length */
    basic_state_t *basic;   /* Interpreter state */
    basic_error_t error;    /* Parse error */
} parse_state_t;

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

/* Peek at current token without consuming */
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

/*
 * Parse a complete expression.
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
 * Parse relational expression: additive ((=|<>|<|>|<=|>=) additive)?
 */
static mbf_t parse_relational(parse_state_t *ps) {
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

    /* Variable - for now just return 0 */
    /* TODO: implement variable lookup */
    if (isalpha(c)) {
        /* Skip variable name */
        while (ps->pos < ps->len && (isalnum(peek(ps)) || peek(ps) == '$')) {
            consume(ps);
        }
        /* Check for array subscript */
        if (peek(ps) == '(') {
            /* Skip array subscript for now */
            int depth = 1;
            consume(ps);
            while (depth > 0 && ps->pos < ps->len) {
                c = consume(ps);
                if (c == '(') depth++;
                else if (c == ')') depth--;
            }
        }
        return MBF_ZERO;  /* TODO: look up variable value */
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
                return mbf_from_int16((int16_t)basic_free_memory(ps->basic));
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

        /* String functions - return 0 for numeric context */
        case TOK_LEN:
        case TOK_ASC:
        case TOK_VAL:
            /* TODO: implement string functions */
            return MBF_ZERO;

        default:
            ps->error = ERR_SN;
            return MBF_ZERO;
    }
}

/*
 * Public interface: Evaluate an expression from tokenized text.
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

/*
 * Evaluate a string expression (for PRINT, etc.)
 * Returns pointer to string in string space, or NULL on error.
 */
const char *eval_string_expression(basic_state_t *state, const uint8_t *text,
                                   size_t len, size_t *consumed,
                                   basic_error_t *error) {
    /* TODO: implement string expression evaluation */
    (void)state;
    (void)text;
    (void)len;
    if (consumed) *consumed = 0;
    if (error) *error = ERR_NONE;
    return NULL;
}
