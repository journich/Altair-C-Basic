/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file main.c
 * @brief Altair 8K BASIC 4.0 Entry Point
 *
 * This is a clean-room C17 implementation of Microsoft's historic
 * Altair 8K BASIC 4.0 interpreter, originally written in 8080 assembly
 * for the MITS Altair 8800 computer in 1975-1976.
 *
 * ## Usage
 *
 * ```
 *   basic8k                    # Start interactive interpreter
 *   basic8k program.bas        # Load and run a BASIC program
 *   basic8k -m 32768 game.bas  # Run with 32KB memory
 *   basic8k -w 80 program.bas  # Set 80-column terminal width
 *   basic8k -n program.bas     # Load without running (for debugging)
 * ```
 *
 * ## Command Line Options
 *
 * - `-m SIZE` : Set memory size in bytes (default: 65536)
 * - `-w WIDTH` : Set terminal width in columns (default: 72)
 * - `-n` : Load file but don't run (just enter interactive mode)
 * - `-h` : Show help
 *
 * ## Startup Sequence
 *
 * 1. Parse command line arguments
 * 2. Initialize interpreter state with configuration
 * 3. If a file is specified:
 *    - Load the BASIC program from file
 *    - Print banner and run program (unless -n specified)
 *    - Enter interactive mode when program ends
 * 4. If no file specified:
 *    - Enter interactive mode directly
 *
 * ## Interactive Mode
 *
 * In interactive mode (immediate mode), the interpreter:
 * - Displays "OK" prompt
 * - Accepts commands or program lines
 * - Lines starting with a number are added to the program
 * - Lines without a number are executed immediately
 * - Special commands: RUN, LIST, NEW, LOAD, SAVE, etc.
 *
 * ## Memory Model
 *
 * The interpreter allocates a contiguous block of memory (default 64KB)
 * that simulates the Altair's address space. The program, variables,
 * arrays, and strings all live within this space, exactly as they
 * would have on the original hardware.
 */

#include "basic/basic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program) {
    fprintf(stderr, "Altair 8K BASIC 4.0 (C17 Implementation)\n");
    fprintf(stderr, "Usage: %s [options] [file.bas]\n", program);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -m SIZE    Set memory size in bytes (default: 65536)\n");
    fprintf(stderr, "  -w WIDTH   Set terminal width (default: 72)\n");
    fprintf(stderr, "  -h         Show this help\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s                    Start interactive interpreter\n", program);
    fprintf(stderr, "  %s program.bas        Load and run program\n", program);
    fprintf(stderr, "  %s -m 32768 game.bas  Run with 32KB memory\n", program);
}

int main(int argc, char *argv[]) {
    basic_config_t config = {
        .memory_size = BASIC8K_DEFAULT_MEMORY,
        .terminal_width = BASIC8K_DEFAULT_WIDTH,
        .want_trig = true,
        .input = stdin,
        .output = stdout
    };

    const char *load_file = NULL;
    bool run_after_load = true;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'm':
                    if (i + 1 < argc) {
                        config.memory_size = (uint32_t)atoi(argv[++i]);
                    }
                    break;
                case 'w':
                    if (i + 1 < argc) {
                        config.terminal_width = (uint8_t)atoi(argv[++i]);
                    }
                    break;
                case 'n':
                    run_after_load = false;
                    break;
                case 'h':
                    print_usage(argv[0]);
                    return 0;
                default:
                    fprintf(stderr, "Unknown option: %s\n", argv[i]);
                    print_usage(argv[0]);
                    return 1;
            }
        } else {
            load_file = argv[i];
        }
    }

    /* Initialize interpreter */
    basic_state_t *state = basic_init(&config);
    if (!state) {
        fprintf(stderr, "Error: Failed to initialize interpreter\n");
        return 1;
    }

    if (load_file) {
        /* Load program from file */
        if (!basic_load_file(state, load_file)) {
            fprintf(stderr, "Error: Failed to load '%s'\n", load_file);
            basic_free(state);
            return 1;
        }

        if (run_after_load) {
            /* Print banner and run */
            basic_print_banner(state);
            basic_error_t err = stmt_run(state, 0);
            if (err == ERR_NONE) {
                basic_run_program(state);
            } else {
                basic_print_error(state, err, 0xFFFF);
            }
            basic_print_ok(state);
            /* Exit after running file - don't enter interactive mode */
            basic_free(state);
            return 0;
        }

        /* Only enter interactive mode if -n flag was used (not running) */
        basic_run_interactive(state);
    } else {
        /* Start interactive interpreter */
        basic_run_interactive(state);
    }

    basic_free(state);
    return 0;
}
