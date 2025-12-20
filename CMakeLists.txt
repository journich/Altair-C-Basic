cmake_minimum_required(VERSION 3.16)
project(basic8k VERSION 4.0.0 LANGUAGES C)

# C17 standard, strictly conforming
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compiler warnings - strict for quality
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wdouble-promotion -Wformat=2
        -Wnull-dereference -Wunused
        -Wshadow -Wcast-align
        -Wstrict-prototypes
    )
    # Treat warnings as errors in Debug mode
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-Werror)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# Build options
option(BUILD_TESTS "Build test suite" ON)
option(ENABLE_TRACE "Enable execution tracing for debugging" OFF)

if(ENABLE_TRACE)
    add_compile_definitions(BASIC8K_TRACE=1)
endif()

# Core library - all the interpreter logic
add_library(basic8k_core STATIC
    src/math/mbf.c
    src/math/mbf_arith.c
    src/math/mbf_trig.c
    src/math/rnd.c
    src/core/tokenizer.c
    src/core/parser.c
    src/core/evaluator.c
    src/core/interpreter.c
    src/memory/program.c
    src/memory/variables.c
    src/memory/arrays.c
    src/memory/strings.c
    src/statements/flow.c
    src/statements/io.c
    src/statements/misc.c
    src/functions/numeric.c
    src/functions/string.c
    src/io/terminal.c
)

target_include_directories(basic8k_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Main executable
add_executable(basic8k src/main.c)
target_link_libraries(basic8k PRIVATE basic8k_core)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(basic8k_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS basic8k RUNTIME DESTINATION bin)
install(TARGETS basic8k_core ARCHIVE DESTINATION lib)
install(DIRECTORY include/basic DESTINATION include)
