/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * tokenizer.c - BASIC keyword tokenization
 *
 * Converts BASIC keywords to single-byte tokens for compact storage.
 * This exactly matches the original 8K BASIC tokenization.
 *
 * Tokenization rules:
 * - Keywords are case-insensitive and become single byte tokens (0x81-0xC6)
 * - Strings in quotes are preserved as-is
 * - Numbers are preserved as-is (stored as ASCII)
 * - After REM or DATA: rest of line preserved literally
 * - Operators (+, -, *, /, ^, >, <, =) are tokenized
 * - Special: TAB( and SPC( include the opening parenthesis
 */

#include "basic/tokens.h"
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/*
 * Keyword table - keywords in token order (0x81 = END, etc.)
 * Order MUST match the token values in tokens.h.
 */
const char *const KEYWORD_TABLE[] = {
    /* Statements 0x81-0x9D */
    "END", "FOR", "NEXT", "DATA", "INPUT", "DIM", "READ", "LET",
    "GOTO", "RUN", "IF", "RESTORE", "GOSUB", "RETURN", "REM", "STOP",
    "OUT", "ON", "NULL", "WAIT", "DEF", "POKE", "PRINT", "CONT",
    "LIST", "CLEAR", "CLOAD", "CSAVE", "NEW",
    /* Keywords 0x9E-0xA4 */
    "TAB(", "TO", "FN", "SPC(", "THEN", "NOT", "STEP",
    /* Operators 0xA5-0xAE */
    "+", "-", "*", "/", "^", "AND", "OR", ">", "=", "<",
    /* Functions 0xAF-0xC6 */
    "SGN", "INT", "ABS", "USR", "FRE", "INP", "POS", "SQR", "RND",
    "LOG", "EXP", "COS", "SIN", "TAN", "ATN", "PEEK",
    "LEN", "STR$", "VAL", "ASC", "CHR$", "LEFT$", "RIGHT$", "MID$",
    NULL
};

const size_t KEYWORD_COUNT = (sizeof(KEYWORD_TABLE) / sizeof(KEYWORD_TABLE[0])) - 1;

/*
 * Get keyword string for a token (for LIST command).
 */
const char *token_to_keyword(uint8_t token) {
    if (token < TOK_FIRST || token > TOK_LAST) {
        return NULL;
    }
    return KEYWORD_TABLE[token - TOK_FIRST];
}

/*
 * Check if a character could start a keyword.
 */
int is_keyword_start(char c) {
    return isalpha((unsigned char)c);
}

/*
 * Case-insensitive comparison of keyword prefix.
 * Returns the keyword length if matched, 0 otherwise.
 */
static size_t match_keyword(const char *input, const char *keyword) {
    size_t len = 0;
    while (keyword[len]) {
        /* Case-insensitive comparison for letters */
        char c1 = (char)toupper((unsigned char)input[len]);
        char c2 = (char)toupper((unsigned char)keyword[len]);
        if (c1 != c2) {
            return 0;
        }
        len++;
    }
    /* For keywords that end with a letter, ensure the match isn't part of a longer identifier.
     * Keywords like TAB( and SPC( end with non-letter, so don't apply this check. */
    if (len > 0 && isalpha((unsigned char)keyword[len - 1]) && isalnum((unsigned char)input[len])) {
        return 0;  /* e.g., "PRINTING" should not match "PRINT" */
    }
    return len;
}

/*
 * Tokenize a BASIC line.
 *
 * Input: null-terminated ASCII string (may include line number)
 * Output: tokenized form written to output buffer
 * Returns: number of bytes written to output, or 0 on error
 *
 * Line format (both input and output):
 * - If line starts with a digit, it's a numbered line
 * - Otherwise it's a direct command
 */
size_t tokenize_line(const char *input, uint8_t *output, size_t output_size) {
    size_t in_pos = 0;
    size_t out_pos = 0;
    bool in_string = false;       /* Inside a quoted string? */
    bool after_rem = false;       /* After REM - copy rest literally */
    bool after_data = false;      /* After DATA - mostly literal */

    if (!input || !output || output_size == 0) {
        return 0;
    }

    /* Skip leading whitespace */
    while (input[in_pos] == ' ') {
        in_pos++;
    }

    /* Copy line number if present */
    while (isdigit((unsigned char)input[in_pos])) {
        if (out_pos >= output_size - 1) return 0;  /* Buffer overflow */
        output[out_pos++] = (uint8_t)input[in_pos++];
    }

    /* Skip space after line number */
    while (input[in_pos] == ' ') {
        in_pos++;
    }

    /* Process rest of line */
    while (input[in_pos] && input[in_pos] != '\n' && input[in_pos] != '\r') {
        if (out_pos >= output_size - 1) return 0;  /* Buffer overflow */

        char c = input[in_pos];

        /* After REM, copy everything literally */
        if (after_rem) {
            output[out_pos++] = (uint8_t)c;
            in_pos++;
            continue;
        }

        /* Handle quoted strings */
        if (c == '"') {
            in_string = !in_string;
            output[out_pos++] = (uint8_t)c;
            in_pos++;
            continue;
        }

        if (in_string) {
            output[out_pos++] = (uint8_t)c;
            in_pos++;
            continue;
        }

        /* After DATA, copy until colon (statement separator) */
        if (after_data) {
            if (c == ':') {
                after_data = false;
            }
            output[out_pos++] = (uint8_t)c;
            in_pos++;
            continue;
        }

        /* Try to match a keyword */
        if (is_keyword_start(c)) {
            bool found = false;
            for (size_t i = 0; i < KEYWORD_COUNT; i++) {
                size_t kw_len = match_keyword(&input[in_pos], KEYWORD_TABLE[i]);
                if (kw_len > 0) {
                    uint8_t token = (uint8_t)(TOK_FIRST + i);
                    output[out_pos++] = token;
                    in_pos += kw_len;
                    found = true;

                    /* Check for special cases */
                    if (token == TOK_REM) {
                        after_rem = true;
                    } else if (token == TOK_DATA) {
                        after_data = true;
                    }
                    break;
                }
            }
            if (found) continue;
        }

        /* Check single-character operators that match tokens */
        /* +, -, *, /, ^, >, =, < are in the token table */
        uint8_t token = 0;
        switch (c) {
            case '+': token = TOK_PLUS; break;
            case '-': token = TOK_MINUS; break;
            case '*': token = TOK_MUL; break;
            case '/': token = TOK_DIV; break;
            case '^': token = TOK_POW; break;
            case '>': token = TOK_GT; break;
            case '=': token = TOK_EQ; break;
            case '<': token = TOK_LT; break;
            default: break;
        }

        if (token) {
            output[out_pos++] = token;
            in_pos++;
            continue;
        }

        /* Skip spaces outside of strings (original BASIC compresses spaces) */
        if (c == ' ') {
            while (input[in_pos] == ' ') {
                in_pos++;
            }
            continue;
        }

        /* Copy character as-is (numbers, punctuation, etc.) */
        output[out_pos++] = (uint8_t)c;
        in_pos++;
    }

    /* Null-terminate */
    output[out_pos] = 0;
    return out_pos;
}

/*
 * Detokenize a line for output (LIST command).
 *
 * Converts token bytes back to keywords.
 * Returns: number of bytes written to output, or 0 on error.
 */
size_t detokenize_line(const uint8_t *input, size_t input_len,
                       char *output, size_t output_size) {
    size_t in_pos = 0;
    size_t out_pos = 0;
    bool in_string = false;

    if (!input || !output || output_size == 0) {
        return 0;
    }

    while (in_pos < input_len && input[in_pos] != 0) {
        uint8_t c = input[in_pos++];

        /* Handle string quotes */
        if (c == '"') {
            in_string = !in_string;
            if (out_pos >= output_size - 1) return 0;
            output[out_pos++] = (char)c;
            continue;
        }

        /* Inside string - copy literally */
        if (in_string) {
            if (out_pos >= output_size - 1) return 0;
            output[out_pos++] = (char)c;
            continue;
        }

        /* Check if it's a token */
        if (TOK_IS_TOKEN(c)) {
            const char *keyword = token_to_keyword(c);
            if (keyword) {
                size_t kw_len = strlen(keyword);
                if (out_pos + kw_len >= output_size) return 0;
                memcpy(&output[out_pos], keyword, kw_len);
                out_pos += kw_len;
            }
            continue;
        }

        /* Regular character - copy as-is */
        if (out_pos >= output_size - 1) return 0;
        output[out_pos++] = (char)c;
    }

    /* Null-terminate */
    output[out_pos] = 0;
    return out_pos;
}

/*
 * Find the longest keyword match at the current position.
 * This is used when we need to prefer longer matches (e.g., GOTO vs GO).
 *
 * Returns the token value, or 0 if no match.
 */
uint8_t find_keyword_token(const char *input) {
    size_t best_len = 0;
    uint8_t best_token = 0;

    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        size_t kw_len = match_keyword(input, KEYWORD_TABLE[i]);
        if (kw_len > best_len) {
            best_len = kw_len;
            best_token = (uint8_t)(TOK_FIRST + i);
        }
    }

    return best_token;
}
