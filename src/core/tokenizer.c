/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file tokenizer.c
 * @brief BASIC Keyword Tokenization
 *
 * Converts BASIC keywords to single-byte tokens for compact storage.
 * This exactly matches the original Altair 8K BASIC tokenization.
 *
 * ## Why Tokenize?
 *
 * In 1976, memory was extremely limited. A typical Altair 8800 might have
 * only 4-16KB of RAM. Tokenization saves space:
 * - "PRINT" (5 chars) -> 0x97 (1 byte) - saves 4 bytes per occurrence
 * - "GOSUB" (5 chars) -> 0x8D (1 byte) - saves 4 bytes per occurrence
 *
 * A typical BASIC program might have hundreds of keywords, so this
 * adds up to significant savings.
 *
 * ## Tokenization Rules
 *
 * 1. **Keywords** become single-byte tokens (0x81-0xC6)
 *    - Case-insensitive: "print", "PRINT", "Print" all become 0x97
 *    - Can be embedded in identifiers: FORI=1TO10 parses as FOR I = 1 TO 10
 *
 * 2. **Strings in quotes** are preserved exactly
 *    - "HELLO" stays as "HELLO" (with quote characters)
 *
 * 3. **Numbers** are preserved as ASCII
 *    - Numbers are not converted to binary during tokenization
 *    - Conversion happens during expression evaluation
 *
 * 4. **After REM**: rest of line is preserved literally
 *    - REM is a comment, everything after it is ignored
 *    - Colons do NOT end a REM statement
 *
 * 5. **After DATA**: content is preserved until colon
 *    - DATA values are not tokenized
 *    - Colon ends DATA and resumes tokenization
 *
 * 6. **Operators** are tokenized for consistency
 *    - +, -, *, /, ^, >, <, = all have token values
 *
 * 7. **Special tokens**: TAB( and SPC( include the parenthesis
 *    - This is for compatibility with original BASIC
 *
 * ## Detokenization
 *
 * The LIST command uses detokenize_line() to convert tokens back to
 * readable keywords.
 */

#include "basic/tokens.h"
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>


/*============================================================================
 * KEYWORD TABLE
 *
 * This table maps token values to keyword strings. The order MUST match
 * the token values defined in tokens.h (0x81 = END, 0x82 = FOR, etc.).
 *============================================================================*/

/**
 * @brief Keyword table mapping token values to strings
 *
 * Index 0 = token 0x81 (END)
 * Index 1 = token 0x82 (FOR)
 * ... and so on.
 *
 * Used by:
 * - tokenize_line(): Match keywords in input
 * - detokenize_line(): Convert tokens back to keywords for LIST
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

/** Number of keywords in the table */
const size_t KEYWORD_COUNT = (sizeof(KEYWORD_TABLE) / sizeof(KEYWORD_TABLE[0])) - 1;


/*============================================================================
 * TOKEN/KEYWORD CONVERSION
 *============================================================================*/

/**
 * @brief Get keyword string for a token (for LIST command)
 *
 * @param token Token value (0x81-0xC6)
 * @return Keyword string, or NULL if invalid token
 */
const char *token_to_keyword(uint8_t token) {
    if (token < TOK_FIRST || token > TOK_LAST) {
        return NULL;
    }
    return KEYWORD_TABLE[token - TOK_FIRST];
}

/**
 * @brief Check if a character could start a keyword
 *
 * All BASIC keywords start with letters A-Z. This provides a quick
 * filter before scanning the keyword table.
 *
 * @param c Character to check
 * @return Non-zero if c is a letter, 0 otherwise
 */
int is_keyword_start(char c) {
    return isalpha((unsigned char)c);
}


/*============================================================================
 * KEYWORD MATCHING
 *============================================================================*/

/**
 * @brief Case-insensitive keyword prefix match
 *
 * Compares input text against a keyword, case-insensitively.
 *
 * IMPORTANT: This does NOT check that the keyword is followed by a
 * non-alphanumeric character. Original BASIC allowed "FORI=1" to be
 * parsed as "FOR I=1". This matches that behavior.
 *
 * @param input Input text to match
 * @param keyword Keyword to match against
 * @return Length of keyword if matched, 0 if no match
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
    /* Original Microsoft BASIC allows keywords anywhere, even embedded in identifiers.
     * For example: FORI=1TO10 parses as FOR I = 1 TO 10
     * So we do NOT check if the keyword is followed by alphanumeric. */
    return len;
}


/*============================================================================
 * TOKENIZATION
 *============================================================================*/

/**
 * @brief Tokenize a BASIC line
 *
 * Converts keywords to single-byte tokens for compact storage.
 *
 * Algorithm:
 * 1. Skip leading whitespace
 * 2. Copy line number if present
 * 3. For each character:
 *    - If in string literal, copy verbatim
 *    - If after REM, copy rest of line verbatim
 *    - If after DATA, copy until colon
 *    - Try to match a keyword; if found, emit token
 *    - For operators (+, -, etc.), emit operator token
 *    - Otherwise copy character as-is
 *
 * @param input Null-terminated ASCII input line
 * @param output Buffer for tokenized output
 * @param output_size Size of output buffer
 * @return Number of bytes written to output, or 0 on error
 *
 * Example:
 * @code
 *     char input[] = "10 PRINT \"HELLO\"";
 *     uint8_t output[256];
 *     size_t len = tokenize_line(input, output, sizeof(output));
 *     // output: "10" + 0x97 + "\"HELLO\""
 * @endcode
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


/*============================================================================
 * DETOKENIZATION
 *============================================================================*/

/**
 * @brief Detokenize a line for display (LIST command)
 *
 * Converts token bytes back to keyword strings for human-readable output.
 *
 * @param input Tokenized line data
 * @param input_len Length of tokenized data
 * @param output Buffer for detokenized output
 * @param output_size Size of output buffer
 * @return Number of characters written, or 0 on error
 *
 * Example:
 * @code
 *     uint8_t tokenized[] = {0x97, '"', 'H', 'I', '"'};  // PRINT "HI"
 *     char output[256];
 *     size_t len = detokenize_line(tokenized, 5, output, sizeof(output));
 *     // output = "PRINT\"HI\""
 * @endcode
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

/**
 * @brief Find the longest keyword match at the current position
 *
 * Scans the keyword table for the longest match. This handles cases
 * like GOTO vs GO where we want to prefer the longer match.
 *
 * @param input Pointer to start of potential keyword
 * @return Token value (0x81-0xC6) if match found, 0 if no match
 *
 * Example:
 * @code
 *     uint8_t tok = find_keyword_token("GOTO");   // Returns TOK_GOTO
 *     uint8_t tok2 = find_keyword_token("GO");    // Returns 0 (no GO keyword)
 *     uint8_t tok3 = find_keyword_token("XYZ");   // Returns 0 (no match)
 * @endcode
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
