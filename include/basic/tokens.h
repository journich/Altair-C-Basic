/*
 * tokens.h - BASIC Token Definitions
 *
 * Token values from Altair 8K BASIC 4.0 (8kbas_src.mac lines 154-224)
 * Keywords are tokenized to single bytes 0x81-0xC6 for compact storage.
 */

#ifndef BASIC8K_TOKENS_H
#define BASIC8K_TOKENS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Statement tokens (0x81-0x9D)
 * These appear at the start of a statement.
 */
typedef enum {
    TOK_END     = 0x81,     /* END - terminate program */
    TOK_FOR     = 0x82,     /* FOR - start loop */
    TOK_NEXT    = 0x83,     /* NEXT - end loop */
    TOK_DATA    = 0x84,     /* DATA - inline data */
    TOK_INPUT   = 0x85,     /* INPUT - get user input */
    TOK_DIM     = 0x86,     /* DIM - dimension array */
    TOK_READ    = 0x87,     /* READ - read from DATA */
    TOK_LET     = 0x88,     /* LET - assignment (optional) */
    TOK_GOTO    = 0x89,     /* GOTO - unconditional jump */
    TOK_RUN     = 0x8A,     /* RUN - execute program */
    TOK_IF      = 0x8B,     /* IF - conditional */
    TOK_RESTORE = 0x8C,     /* RESTORE - reset DATA pointer */
    TOK_GOSUB   = 0x8D,     /* GOSUB - call subroutine */
    TOK_RETURN  = 0x8E,     /* RETURN - return from subroutine */
    TOK_REM     = 0x8F,     /* REM - comment */
    TOK_STOP    = 0x90,     /* STOP - halt execution */
    TOK_OUT     = 0x91,     /* OUT - output to port */
    TOK_ON      = 0x92,     /* ON - computed GOTO/GOSUB */
    TOK_NULL    = 0x93,     /* NULL - set null count */
    TOK_WAIT    = 0x94,     /* WAIT - wait for port condition */
    TOK_DEF     = 0x95,     /* DEF - define function */
    TOK_POKE    = 0x96,     /* POKE - write memory */
    TOK_PRINT   = 0x97,     /* PRINT - output */
    TOK_CONT    = 0x98,     /* CONT - continue after STOP */
    TOK_LIST    = 0x99,     /* LIST - list program */
    TOK_CLEAR   = 0x9A,     /* CLEAR - clear variables */
    TOK_CLOAD   = 0x9B,     /* CLOAD - cassette load */
    TOK_CSAVE   = 0x9C,     /* CSAVE - cassette save */
    TOK_NEW     = 0x9D,     /* NEW - clear program */

    /*
     * Keyword tokens used within statements (0x9E-0xA4)
     */
    TOK_TAB     = 0x9E,     /* TAB( - tab function in PRINT */
    TOK_TO      = 0x9F,     /* TO - FOR...TO */
    TOK_FN      = 0xA0,     /* FN - user function call */
    TOK_SPC     = 0xA1,     /* SPC( - space function in PRINT */
    TOK_THEN    = 0xA2,     /* THEN - IF...THEN */
    TOK_NOT     = 0xA3,     /* NOT - logical not */
    TOK_STEP    = 0xA4,     /* STEP - FOR...STEP */

    /*
     * Operator tokens (0xA5-0xAE)
     */
    TOK_PLUS    = 0xA5,     /* + */
    TOK_MINUS   = 0xA6,     /* - */
    TOK_MUL     = 0xA7,     /* * */
    TOK_DIV     = 0xA8,     /* / */
    TOK_POW     = 0xA9,     /* ^ (exponentiation) */
    TOK_AND     = 0xAA,     /* AND */
    TOK_OR      = 0xAB,     /* OR */
    TOK_GT      = 0xAC,     /* > */
    TOK_EQ      = 0xAD,     /* = */
    TOK_LT      = 0xAE,     /* < */

    /*
     * Function tokens (0xAF-0xC6)
     */
    TOK_SGN     = 0xAF,     /* SGN - sign */
    TOK_INT     = 0xB0,     /* INT - integer part */
    TOK_ABS     = 0xB1,     /* ABS - absolute value */
    TOK_USR     = 0xB2,     /* USR - call machine code */
    TOK_FRE     = 0xB3,     /* FRE - free memory */
    TOK_INP     = 0xB4,     /* INP - input from port */
    TOK_POS     = 0xB5,     /* POS - cursor position */
    TOK_SQR     = 0xB6,     /* SQR - square root */
    TOK_RND     = 0xB7,     /* RND - random number */
    TOK_LOG     = 0xB8,     /* LOG - natural logarithm */
    TOK_EXP     = 0xB9,     /* EXP - exponential */
    TOK_COS     = 0xBA,     /* COS - cosine */
    TOK_SIN     = 0xBB,     /* SIN - sine */
    TOK_TAN     = 0xBC,     /* TAN - tangent */
    TOK_ATN     = 0xBD,     /* ATN - arctangent */
    TOK_PEEK    = 0xBE,     /* PEEK - read memory */
    TOK_LEN     = 0xBF,     /* LEN - string length */
    TOK_STR     = 0xC0,     /* STR$ - number to string */
    TOK_VAL     = 0xC1,     /* VAL - string to number */
    TOK_ASC     = 0xC2,     /* ASC - ASCII code */
    TOK_CHR     = 0xC3,     /* CHR$ - character from code */
    TOK_LEFT    = 0xC4,     /* LEFT$ - left substring */
    TOK_RIGHT   = 0xC5,     /* RIGHT$ - right substring */
    TOK_MID     = 0xC6,     /* MID$ - middle substring */
} basic_token_t;

/* Token range checks */
#define TOK_FIRST       0x81
#define TOK_LAST        0xC6
#define TOK_IS_TOKEN(c) ((c) >= TOK_FIRST && (c) <= TOK_LAST)

/* Statement tokens (can start a statement) */
#define TOK_IS_STATEMENT(c) ((c) >= TOK_END && (c) <= TOK_NEW)

/* Function tokens (return a value) */
#define TOK_IS_FUNCTION(c) ((c) >= TOK_SGN && (c) <= TOK_MID)

/* String function tokens (return a string) */
#define TOK_IS_STRING_FUNC(c) ((c) == TOK_STR || (c) == TOK_CHR || \
                               (c) == TOK_LEFT || (c) == TOK_RIGHT || \
                               (c) == TOK_MID)

/*
 * Keyword table for tokenization.
 * Keywords are stored with first character having bit 7 set.
 * The order matches token values starting at 0x81.
 */
extern const char *const KEYWORD_TABLE[];
extern const size_t KEYWORD_COUNT;

/* Get keyword string for a token (for LIST command) */
const char *token_to_keyword(uint8_t token);

/* Check if a character starts a keyword (for tokenizer) */
int is_keyword_start(char c);

/*
 * Tokenize a BASIC line.
 * Converts keywords to single-byte tokens.
 * Returns: number of bytes written to output, or 0 on error.
 */
size_t tokenize_line(const char *input, uint8_t *output, size_t output_size);

/*
 * Detokenize a line for output (LIST command).
 * Converts token bytes back to keywords.
 * Returns: number of bytes written to output, or 0 on error.
 */
size_t detokenize_line(const uint8_t *input, size_t input_len,
                       char *output, size_t output_size);

/*
 * Find the token for the keyword at the current position.
 * Returns the token value (0x81-0xC6), or 0 if no match.
 */
uint8_t find_keyword_token(const char *input);

#endif /* BASIC8K_TOKENS_H */
