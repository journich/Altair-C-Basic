/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file evaluator.c
 * @brief Expression Evaluator (Stub)
 *
 * This is a placeholder file. Expression evaluation is currently integrated
 * into the parser (parser.c) and interpreter (interpreter.c) rather than
 * being a separate module.
 *
 * The parsing functions in parser.c directly evaluate expressions as they
 * parse them, following the recursive-descent approach. Function calls are
 * dispatched to numeric.c and string.c, while operators are handled inline.
 *
 * If a separate evaluator were needed (e.g., for an AST-based approach),
 * it would be implemented here.
 */
#include "basic/basic.h"
