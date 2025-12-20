# tests/CMakeLists.txt - Test configuration

# We use a minimal test framework embedded in the project
# No external dependencies required

# Simple test harness
add_library(test_harness INTERFACE)
target_include_directories(test_harness INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Test helper header
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/test_harness.h
"/*
 * test_harness.h - Minimal test framework
 */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _tests_run = 0;
static int _tests_failed = 0;
static const char *_current_test = NULL;

#define TEST(name) static void name(void)

#define RUN_TEST(name) do { \\
    _current_test = #name; \\
    _tests_run++; \\
    name(); \\
    printf(\".\"); \\
    fflush(stdout); \\
} while(0)

#define ASSERT(cond) do { \\
    if (!(cond)) { \\
        printf(\"\\nFAIL: %s\\n\", _current_test); \\
        printf(\"  Assertion failed: %s\\n\", #cond); \\
        printf(\"  At %s:%d\\n\", __FILE__, __LINE__); \\
        _tests_failed++; \\
        return; \\
    } \\
} while(0)

#define ASSERT_EQ(a, b) do { \\
    if ((a) != (b)) { \\
        printf(\"\\nFAIL: %s\\n\", _current_test); \\
        printf(\"  Expected: %s == %s\\n\", #a, #b); \\
        printf(\"  At %s:%d\\n\", __FILE__, __LINE__); \\
        _tests_failed++; \\
        return; \\
    } \\
} while(0)

#define ASSERT_EQ_INT(a, b) do { \\
    long long _a = (long long)(a); \\
    long long _b = (long long)(b); \\
    if (_a != _b) { \\
        printf(\"\\nFAIL: %s\\n\", _current_test); \\
        printf(\"  Expected: %s == %s\\n\", #a, #b); \\
        printf(\"  Got: %lld != %lld\\n\", _a, _b); \\
        printf(\"  At %s:%d\\n\", __FILE__, __LINE__); \\
        _tests_failed++; \\
        return; \\
    } \\
} while(0)

#define ASSERT_EQ_HEX(a, b) do { \\
    unsigned long long _a = (unsigned long long)(a); \\
    unsigned long long _b = (unsigned long long)(b); \\
    if (_a != _b) { \\
        printf(\"\\nFAIL: %s\\n\", _current_test); \\
        printf(\"  Expected: %s == %s\\n\", #a, #b); \\
        printf(\"  Got: 0x%llX != 0x%llX\\n\", _a, _b); \\
        printf(\"  At %s:%d\\n\", __FILE__, __LINE__); \\
        _tests_failed++; \\
        return; \\
    } \\
} while(0)

#define ASSERT_STR_EQ(a, b) do { \\
    if (strcmp((a), (b)) != 0) { \\
        printf(\"\\nFAIL: %s\\n\", _current_test); \\
        printf(\"  Expected: \\\"%s\\\"\\n\", (b)); \\
        printf(\"  Got:      \\\"%s\\\"\\n\", (a)); \\
        printf(\"  At %s:%d\\n\", __FILE__, __LINE__); \\
        _tests_failed++; \\
        return; \\
    } \\
} while(0)

#define ASSERT_EQ_STR(a, b) ASSERT_STR_EQ(a, b)

#define ASSERT_MBF_EQ(a, b) do { \\
    mbf_t _a = (a); \\
    mbf_t _b = (b); \\
    if (_a.raw != _b.raw) { \\
        printf(\"\\nFAIL: %s\\n\", _current_test); \\
        printf(\"  Expected MBF: 0x%08X\\n\", _b.raw); \\
        printf(\"  Got MBF:      0x%08X\\n\", _a.raw); \\
        printf(\"  At %s:%d\\n\", __FILE__, __LINE__); \\
        _tests_failed++; \\
        return; \\
    } \\
} while(0)

#define TEST_MAIN() \\
int main(void) { \\
    printf(\"Running tests...\\n\"); \\
    run_tests(); \\
    printf(\"\\n\\n%d tests, %d failures\\n\", _tests_run, _tests_failed); \\
    return _tests_failed > 0 ? 1 : 0; \\
}

#endif /* TEST_HARNESS_H */
")

# Unit tests
add_executable(test_mbf unit/test_mbf.c)
target_link_libraries(test_mbf PRIVATE basic8k_core test_harness)
add_test(NAME MBF_Tests COMMAND test_mbf)

add_executable(test_rnd unit/test_rnd.c)
target_link_libraries(test_rnd PRIVATE basic8k_core test_harness)
add_test(NAME RND_Tests COMMAND test_rnd)

add_executable(test_tokenizer unit/test_tokenizer.c)
target_link_libraries(test_tokenizer PRIVATE basic8k_core test_harness)
add_test(NAME Tokenizer_Tests COMMAND test_tokenizer)

add_executable(test_parser unit/test_parser.c)
target_link_libraries(test_parser PRIVATE basic8k_core test_harness)
add_test(NAME Parser_Tests COMMAND test_parser)

add_executable(test_memory unit/test_memory.c)
target_link_libraries(test_memory PRIVATE basic8k_core test_harness)
add_test(NAME Memory_Tests COMMAND test_memory)

# Integration tests will be added later
# add_executable(test_programs integration/test_programs.c)
# target_link_libraries(test_programs PRIVATE basic8k_core test_harness)
# add_test(NAME Program_Tests COMMAND test_programs)
