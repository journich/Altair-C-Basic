/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file numeric.c
 * @brief Numeric BASIC Functions
 *
 * Implements all built-in numeric functions that return numbers:
 *
 * ## Mathematical Functions
 * - SGN(x) - Sign of number: -1, 0, or 1
 * - INT(x) - Floor (truncate toward negative infinity)
 * - ABS(x) - Absolute value
 * - SQR(x) - Square root (x >= 0)
 *
 * ## Transcendental Functions
 * - SIN(x) - Sine (radians)
 * - COS(x) - Cosine (radians)
 * - TAN(x) - Tangent (radians)
 * - ATN(x) - Arctangent (returns radians)
 * - LOG(x) - Natural logarithm (x > 0)
 * - EXP(x) - e^x (exponential)
 *
 * ## Random Numbers
 * - RND(x) - Random number (see rnd.c for algorithm)
 *
 * ## System Functions
 * - PEEK(addr) - Read byte from memory address
 * - FRE(0) - Return free memory bytes
 * - POS(0) - Return current cursor column (1-based)
 * - INP(port) - Read from I/O port (stub)
 * - USR(addr) - Call machine code (stub)
 *
 * ## IEEE-to-MBF Conversion
 *
 * This file also contains `mbf_to_double()` and `mbf_from_double()` which
 * convert between Microsoft Binary Format and IEEE 754 double precision.
 * These are used for transcendental functions which currently use the
 * C math library rather than the original polynomial approximations.
 *
 * ## Implementation Notes
 *
 * The transcendental functions (SIN, COS, TAN, ATN, LOG, EXP) currently
 * convert MBF to IEEE double, call the C math library, and convert back.
 * This produces results that are close but not byte-for-byte identical
 * to the original 8K BASIC which used Chebyshev polynomial approximations.
 *
 * For exact compatibility, these could be replaced with the original
 * polynomial implementations from 8kbas_src.mac.
 */

#include "basic/basic.h"
#include "basic/errors.h"
#include <math.h>

/*
 * SGN function - returns sign of number.
 * Returns: -1 (negative), 0 (zero), or 1 (positive)
 */
mbf_t fn_sgn(mbf_t value) {
    int sign = mbf_sign(value);
    return mbf_from_int16((int16_t)sign);
}

/*
 * INT function - floor (truncate toward negative infinity).
 */
mbf_t fn_int(mbf_t value) {
    return mbf_int(value);
}

/*
 * ABS function - absolute value.
 */
mbf_t fn_abs(mbf_t value) {
    return mbf_abs(value);
}

/*
 * SQR function - square root.
 * Uses Newton-Raphson iteration.
 */
mbf_t fn_sqr(mbf_t value) {
    if (mbf_is_zero(value)) {
        return MBF_ZERO;
    }

    if (mbf_is_negative(value)) {
        mbf_set_error(MBF_DOMAIN);
        return MBF_ZERO;
    }

    /* Convert to IEEE, compute sqrt, convert back */
    /* This is a working implementation - exact matching to original
     * would require implementing the original Newton-Raphson algorithm */
    double x = mbf_to_double(value);
    double result = sqrt(x);
    return mbf_from_double(result);
}

/*
 * EXP function - e raised to a power.
 */
mbf_t fn_exp(mbf_t value) {
    double x = mbf_to_double(value);

    /* Check for overflow */
    if (x > 88.0) {
        mbf_set_error(MBF_OVERFLOW);
        return MBF_ZERO;
    }

    double result = exp(x);
    return mbf_from_double(result);
}

/*
 * LOG function - natural logarithm.
 */
mbf_t fn_log(mbf_t value) {
    if (mbf_is_zero(value) || mbf_is_negative(value)) {
        mbf_set_error(MBF_DOMAIN);
        return MBF_ZERO;
    }

    double x = mbf_to_double(value);
    double result = log(x);
    return mbf_from_double(result);
}

/*
 * SIN function - sine (radians).
 */
mbf_t fn_sin(mbf_t value) {
    double x = mbf_to_double(value);
    double result = sin(x);
    return mbf_from_double(result);
}

/*
 * COS function - cosine (radians).
 */
mbf_t fn_cos(mbf_t value) {
    double x = mbf_to_double(value);
    double result = cos(x);
    return mbf_from_double(result);
}

/*
 * TAN function - tangent (radians).
 */
mbf_t fn_tan(mbf_t value) {
    double x = mbf_to_double(value);
    double result = tan(x);
    return mbf_from_double(result);
}

/*
 * ATN function - arctangent (returns radians).
 */
mbf_t fn_atn(mbf_t value) {
    double x = mbf_to_double(value);
    double result = atan(x);
    return mbf_from_double(result);
}

/*
 * RND function wrapper.
 * Note: The actual RND implementation is in rnd.c
 */
mbf_t fn_rnd(basic_state_t *state, mbf_t arg) {
    return rnd_next(&state->rnd, arg);
}

/*
 * PEEK function - read byte from memory.
 */
mbf_t fn_peek(basic_state_t *state, mbf_t address) {
    bool overflow;
    int32_t addr = mbf_to_int32(address, &overflow);

    if (overflow || addr < 0 || (uint32_t)addr >= state->memory_size) {
        return MBF_ZERO;
    }

    uint8_t value = state->memory[addr];
    return mbf_from_int16((int16_t)value);
}

/*
 * FRE function - return free memory.
 */
mbf_t fn_fre(basic_state_t *state, __attribute__((unused)) mbf_t dummy) {
    int32_t free_mem = stmt_fre(state);
    return mbf_from_int32(free_mem);
}

/*
 * POS function - return cursor column (1-based).
 */
mbf_t fn_pos(basic_state_t *state, __attribute__((unused)) mbf_t dummy) {
    int pos = io_pos(state);
    return mbf_from_int16((int16_t)pos);
}

/*
 * USR function - call machine language routine.
 * Not implemented - returns 0 with warning.
 */
mbf_t fn_usr(basic_state_t *state, mbf_t arg) {
    return stmt_usr(state, arg);
}

/*
 * INP function - read from I/O port.
 * Not implemented - returns 0 with warning.
 */
mbf_t fn_inp(basic_state_t *state, mbf_t port) {
    bool overflow;
    int16_t p = mbf_to_int16(port, &overflow);
    uint8_t value = stmt_inp(state, (uint8_t)p);
    return mbf_from_int16((int16_t)value);
}

/*
 * Helper to convert MBF to IEEE double.
 */
double mbf_to_double(mbf_t a) {
    if (mbf_is_zero(a)) return 0.0;

    bool negative = mbf_is_negative(a);
    uint32_t mantissa = mbf_get_mantissa24(a);
    int exponent = a.bytes.exponent - MBF_BIAS;

    /* Mantissa has implicit 1 at bit 23, so it represents 1.xxxxx... */
    /* Convert to double: value = mantissa * 2^(exponent - 23) */
    double result = (double)mantissa * pow(2.0, (double)(exponent - 23));

    return negative ? -result : result;
}

/*
 * Helper to convert IEEE double to MBF.
 */
mbf_t mbf_from_double(double x) {
    if (x == 0.0) return MBF_ZERO;

    bool negative = (x < 0);
    if (negative) x = -x;

    /* Get exponent and mantissa */
    int exp;
    double mant = frexp(x, &exp);  /* x = mant * 2^exp, 0.5 <= mant < 1 */

    /* Adjust to MBF format: mantissa with bit 23 set */
    mant *= 2.0;   /* Now 1.0 <= mant < 2.0 */
    exp--;

    /* Convert to 24-bit integer mantissa with rounding */
    uint32_t mantissa = (uint32_t)(mant * (double)(1 << 23) + 0.5);
    if (mantissa >= (1u << 24)) {
        mantissa >>= 1;
        exp++;
    }

    /* Check for overflow/underflow */
    int mbf_exp = exp + MBF_BIAS;
    if (mbf_exp > 255) {
        mbf_set_error(MBF_OVERFLOW);
        return MBF_ZERO;
    }
    if (mbf_exp <= 0) {
        /* Underflow to zero */
        return MBF_ZERO;
    }

    return mbf_make(negative, (uint8_t)mbf_exp, mantissa);
}
