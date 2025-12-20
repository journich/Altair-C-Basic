/*
 * test_tokenizer.c - Unit tests for BASIC tokenizer
 *
 * Tests the tokenization and detokenization of BASIC lines.
 */

#include "test_harness.h"
#include "basic/tokens.h"
#include <string.h>
#include <stdbool.h>

/* Test simple keyword tokenization */
TEST(test_tokenize_print) {
    uint8_t output[256];
    size_t len = tokenize_line("PRINT", output, sizeof(output));

    ASSERT(len > 0);
    ASSERT_EQ_HEX(output[0], TOK_PRINT);
    ASSERT_EQ(output[1], 0);  /* Null terminated */
}

/* Test case insensitivity */
TEST(test_tokenize_case_insensitive) {
    uint8_t output1[256], output2[256], output3[256];

    tokenize_line("PRINT", output1, sizeof(output1));
    tokenize_line("print", output2, sizeof(output2));
    tokenize_line("Print", output3, sizeof(output3));

    ASSERT_EQ_HEX(output1[0], TOK_PRINT);
    ASSERT_EQ_HEX(output2[0], TOK_PRINT);
    ASSERT_EQ_HEX(output3[0], TOK_PRINT);
}

/* Test line with line number */
TEST(test_tokenize_with_line_number) {
    uint8_t output[256];
    size_t len = tokenize_line("10 PRINT", output, sizeof(output));

    ASSERT(len > 0);
    /* Line number is stored as ASCII digits */
    ASSERT_EQ(output[0], '1');
    ASSERT_EQ(output[1], '0');
    ASSERT_EQ_HEX(output[2], TOK_PRINT);
}

/* Test PRINT with string literal */
TEST(test_tokenize_print_string) {
    uint8_t output[256];
    size_t len = tokenize_line("PRINT \"HELLO\"", output, sizeof(output));

    ASSERT(len > 0);
    ASSERT_EQ_HEX(output[0], TOK_PRINT);
    /* String should be preserved as-is */
    ASSERT_EQ(output[1], '"');
    ASSERT_EQ(output[2], 'H');
    ASSERT_EQ(output[3], 'E');
    ASSERT_EQ(output[4], 'L');
    ASSERT_EQ(output[5], 'L');
    ASSERT_EQ(output[6], 'O');
    ASSERT_EQ(output[7], '"');
}

/* Test FOR statement */
TEST(test_tokenize_for) {
    uint8_t output[256];
    size_t len = tokenize_line("FOR I=1 TO 10", output, sizeof(output));

    ASSERT(len > 0);
    /* FOR I = 1 TO 10 */
    int pos = 0;
    ASSERT_EQ_HEX(output[pos++], TOK_FOR);
    ASSERT_EQ(output[pos++], 'I');
    ASSERT_EQ_HEX(output[pos++], TOK_EQ);
    ASSERT_EQ(output[pos++], '1');
    ASSERT_EQ_HEX(output[pos++], TOK_TO);
    ASSERT_EQ(output[pos++], '1');
    ASSERT_EQ(output[pos++], '0');
}

/* Test REM - rest of line should be literal */
TEST(test_tokenize_rem) {
    uint8_t output[256];
    size_t len = tokenize_line("REM THIS IS A COMMENT PRINT FOR", output, sizeof(output));

    ASSERT(len > 0);
    ASSERT_EQ_HEX(output[0], TOK_REM);
    /* After REM, everything is literal - PRINT and FOR should NOT be tokenized */
    /* Check that PRINT doesn't appear as token 0x97 */
    bool found_print_token = false;
    for (size_t i = 1; i < len; i++) {
        if (output[i] == TOK_PRINT) {
            found_print_token = true;
        }
    }
    ASSERT(!found_print_token);
}

/* Test multiple statements on one line */
TEST(test_tokenize_multi_statement) {
    uint8_t output[256];
    size_t len = tokenize_line("10 PRINT:GOTO 10", output, sizeof(output));

    ASSERT(len > 0);
    /* 1 0 PRINT : GOTO 1 0 */
    int pos = 0;
    ASSERT_EQ(output[pos++], '1');
    ASSERT_EQ(output[pos++], '0');
    ASSERT_EQ_HEX(output[pos++], TOK_PRINT);
    ASSERT_EQ(output[pos++], ':');
    ASSERT_EQ_HEX(output[pos++], TOK_GOTO);
    ASSERT_EQ(output[pos++], '1');
    ASSERT_EQ(output[pos++], '0');
}

/* Test operators */
TEST(test_tokenize_operators) {
    uint8_t output[256];
    size_t len = tokenize_line("A+B-C*D/E^F", output, sizeof(output));

    ASSERT(len > 0);
    ASSERT_EQ(output[0], 'A');
    ASSERT_EQ_HEX(output[1], TOK_PLUS);
    ASSERT_EQ(output[2], 'B');
    ASSERT_EQ_HEX(output[3], TOK_MINUS);
    ASSERT_EQ(output[4], 'C');
    ASSERT_EQ_HEX(output[5], TOK_MUL);
    ASSERT_EQ(output[6], 'D');
    ASSERT_EQ_HEX(output[7], TOK_DIV);
    ASSERT_EQ(output[8], 'E');
    ASSERT_EQ_HEX(output[9], TOK_POW);
    ASSERT_EQ(output[10], 'F');
}

/* Test comparison operators */
TEST(test_tokenize_comparisons) {
    uint8_t output[256];
    size_t len = tokenize_line("A>B=C<D", output, sizeof(output));

    ASSERT(len > 0);
    ASSERT_EQ(output[0], 'A');
    ASSERT_EQ_HEX(output[1], TOK_GT);
    ASSERT_EQ(output[2], 'B');
    ASSERT_EQ_HEX(output[3], TOK_EQ);
    ASSERT_EQ(output[4], 'C');
    ASSERT_EQ_HEX(output[5], TOK_LT);
    ASSERT_EQ(output[6], 'D');
}

/* Test logical operators */
TEST(test_tokenize_logical) {
    uint8_t output[256];
    size_t len = tokenize_line("A AND B OR NOT C", output, sizeof(output));

    ASSERT(len > 0);
    int pos = 0;
    ASSERT_EQ(output[pos++], 'A');
    ASSERT_EQ_HEX(output[pos++], TOK_AND);
    ASSERT_EQ(output[pos++], 'B');
    ASSERT_EQ_HEX(output[pos++], TOK_OR);
    ASSERT_EQ_HEX(output[pos++], TOK_NOT);
    ASSERT_EQ(output[pos++], 'C');
}

/* Test functions */
TEST(test_tokenize_functions) {
    uint8_t output[256];
    size_t len = tokenize_line("X=SIN(Y)+COS(Z)", output, sizeof(output));

    ASSERT(len > 0);
    int pos = 0;
    ASSERT_EQ(output[pos++], 'X');
    ASSERT_EQ_HEX(output[pos++], TOK_EQ);
    ASSERT_EQ_HEX(output[pos++], TOK_SIN);
    ASSERT_EQ(output[pos++], '(');
    ASSERT_EQ(output[pos++], 'Y');
    ASSERT_EQ(output[pos++], ')');
    ASSERT_EQ_HEX(output[pos++], TOK_PLUS);
    ASSERT_EQ_HEX(output[pos++], TOK_COS);
    ASSERT_EQ(output[pos++], '(');
    ASSERT_EQ(output[pos++], 'Z');
    ASSERT_EQ(output[pos++], ')');
}

/* Test TAB( and SPC( which include the parenthesis */
TEST(test_tokenize_tab_spc) {
    uint8_t output[256];
    size_t len = tokenize_line("PRINT TAB(10);SPC(5)", output, sizeof(output));

    ASSERT(len > 0);
    int pos = 0;
    ASSERT_EQ_HEX(output[pos++], TOK_PRINT);
    ASSERT_EQ_HEX(output[pos++], TOK_TAB);  /* TAB( includes the ( */
    ASSERT_EQ(output[pos++], '1');
    ASSERT_EQ(output[pos++], '0');
    ASSERT_EQ(output[pos++], ')');
    ASSERT_EQ(output[pos++], ';');
    ASSERT_EQ_HEX(output[pos++], TOK_SPC);  /* SPC( includes the ( */
    ASSERT_EQ(output[pos++], '5');
    ASSERT_EQ(output[pos++], ')');
}

/* Test string functions with $ */
TEST(test_tokenize_string_functions) {
    uint8_t output[256];
    size_t len = tokenize_line("A$=LEFT$(B$,5)", output, sizeof(output));

    ASSERT(len > 0);
    int pos = 0;
    ASSERT_EQ(output[pos++], 'A');
    ASSERT_EQ(output[pos++], '$');
    ASSERT_EQ_HEX(output[pos++], TOK_EQ);
    ASSERT_EQ_HEX(output[pos++], TOK_LEFT);
    ASSERT_EQ(output[pos++], '(');
    ASSERT_EQ(output[pos++], 'B');
    ASSERT_EQ(output[pos++], '$');
    ASSERT_EQ(output[pos++], ',');
    ASSERT_EQ(output[pos++], '5');
    ASSERT_EQ(output[pos++], ')');
}

/* Test detokenization - round trip */
TEST(test_detokenize_print) {
    uint8_t tokenized[256];
    char detokenized[256];

    size_t tok_len = tokenize_line("PRINT", tokenized, sizeof(tokenized));
    ASSERT(tok_len > 0);

    size_t detok_len = detokenize_line(tokenized, tok_len, detokenized, sizeof(detokenized));
    ASSERT(detok_len > 0);
    ASSERT_EQ_STR(detokenized, "PRINT");
}

/* Test detokenization of complex line */
TEST(test_detokenize_complex) {
    uint8_t tokenized[256];
    char detokenized[256];

    /* Tokenize and detokenize */
    size_t tok_len = tokenize_line("10 FOR I=1 TO 10", tokenized, sizeof(tokenized));
    ASSERT(tok_len > 0);

    size_t detok_len = detokenize_line(tokenized, tok_len, detokenized, sizeof(detokenized));
    ASSERT(detok_len > 0);

    /* The detokenized version should be equivalent */
    /* Note: spacing might differ, so we just check key parts */
    ASSERT(strstr(detokenized, "10") != NULL);
    ASSERT(strstr(detokenized, "FOR") != NULL);
    ASSERT(strstr(detokenized, "TO") != NULL);
}

/* Test that keywords in identifiers aren't tokenized */
TEST(test_no_tokenize_in_identifier) {
    uint8_t output[256];
    size_t len = tokenize_line("PRINTING=1", output, sizeof(output));

    ASSERT(len > 0);
    /* PRINTING should NOT be tokenized as PRINT + ING */
    /* It should be stored as letters */
    ASSERT_EQ(output[0], 'P');
    ASSERT_EQ(output[1], 'R');
    ASSERT_EQ(output[2], 'I');
    ASSERT_EQ(output[3], 'N');
    ASSERT_EQ(output[4], 'T');
    ASSERT_EQ(output[5], 'I');
    ASSERT_EQ(output[6], 'N');
    ASSERT_EQ(output[7], 'G');
}

/* Test token_to_keyword */
TEST(test_token_to_keyword) {
    ASSERT_EQ_STR(token_to_keyword(TOK_PRINT), "PRINT");
    ASSERT_EQ_STR(token_to_keyword(TOK_GOTO), "GOTO");
    ASSERT_EQ_STR(token_to_keyword(TOK_FOR), "FOR");
    ASSERT_EQ_STR(token_to_keyword(TOK_SIN), "SIN");
    ASSERT_EQ_STR(token_to_keyword(TOK_LEFT), "LEFT$");
    ASSERT(token_to_keyword(0x00) == NULL);  /* Invalid token */
    ASSERT(token_to_keyword(0xFF) == NULL);  /* Invalid token */
}

/* Test find_keyword_token */
TEST(test_find_keyword_token) {
    ASSERT_EQ_HEX(find_keyword_token("PRINT"), TOK_PRINT);
    ASSERT_EQ_HEX(find_keyword_token("print"), TOK_PRINT);
    ASSERT_EQ_HEX(find_keyword_token("GOTO"), TOK_GOTO);
    ASSERT_EQ_HEX(find_keyword_token("SIN"), TOK_SIN);
    ASSERT_EQ_HEX(find_keyword_token("LEFT$"), TOK_LEFT);
    ASSERT_EQ(find_keyword_token("NOTAKEYWORD"), 0);
}

/* Run all tests */
void run_tests(void) {
    RUN_TEST(test_tokenize_print);
    RUN_TEST(test_tokenize_case_insensitive);
    RUN_TEST(test_tokenize_with_line_number);
    RUN_TEST(test_tokenize_print_string);
    RUN_TEST(test_tokenize_for);
    RUN_TEST(test_tokenize_rem);
    RUN_TEST(test_tokenize_multi_statement);
    RUN_TEST(test_tokenize_operators);
    RUN_TEST(test_tokenize_comparisons);
    RUN_TEST(test_tokenize_logical);
    RUN_TEST(test_tokenize_functions);
    RUN_TEST(test_tokenize_tab_spc);
    RUN_TEST(test_tokenize_string_functions);
    RUN_TEST(test_detokenize_print);
    RUN_TEST(test_detokenize_complex);
    RUN_TEST(test_no_tokenize_in_identifier);
    RUN_TEST(test_token_to_keyword);
    RUN_TEST(test_find_keyword_token);
}

TEST_MAIN()
