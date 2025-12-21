/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file mbf_trig.c
 * @brief Transcendental Functions (SIN, COS, TAN, ATN, LOG, EXP, SQR)
 *
 * This module implements the mathematical functions available in BASIC:
 * - SQR(x) - Square root
 * - SIN(x) - Sine (radians)
 * - COS(x) - Cosine (radians)
 * - TAN(x) - Tangent (radians)
 * - ATN(x) - Arctangent (returns radians)
 * - LOG(x) - Natural logarithm
 * - EXP(x) - e^x (exponential)
 *
 * ## Implementation Strategy
 *
 * Currently, these use the C math library via MBF<->double conversion.
 * This is simpler but may not produce byte-for-byte identical results
 * to the original BASIC.
 *
 * For exact compatibility, these should be replaced with the original
 * polynomial approximations from 8kbas_src.mac:
 * - SIN/COS: Chebyshev polynomial approximation
 * - ATN: Polynomial approximation
 * - LOG: Series expansion
 * - EXP: Series expansion
 *
 * ## Error Handling
 *
 * - SQR of negative: Returns 0, caller should generate FC error
 * - LOG of zero/negative: Returns 0, caller should generate FC error
 * - EXP overflow: Returns 0, caller should generate OV error
 */

#include "basic/mbf.h"
#include <math.h>


/*============================================================================
 * SQUARE ROOT
 *============================================================================*/

/* Square root using Newton-Raphson iteration in MBF format */
mbf_t mbf_sqr(mbf_t a) {
    /* Negative numbers are illegal for SQR */
    if (mbf_is_negative(a)) {
        return MBF_ZERO;  /* Should trigger FC error in caller */
    }

    /* SQR(0) = 0 */
    if (mbf_is_zero(a)) {
        return MBF_ZERO;
    }

    /* Convert to double, compute sqrt, convert back */
    double val = mbf_to_double(a);
    double result = sqrt(val);
    return mbf_from_double(result);
}

mbf_t mbf_sin(mbf_t a) {
    double val = mbf_to_double(a);
    double result = sin(val);
    return mbf_from_double(result);
}

mbf_t mbf_cos(mbf_t a) {
    double val = mbf_to_double(a);
    double result = cos(val);
    return mbf_from_double(result);
}

mbf_t mbf_tan(mbf_t a) {
    double val = mbf_to_double(a);
    double result = tan(val);
    return mbf_from_double(result);
}

mbf_t mbf_atn(mbf_t a) {
    double val = mbf_to_double(a);
    double result = atan(val);
    return mbf_from_double(result);
}

mbf_t mbf_log(mbf_t a) {
    /* LOG of negative or zero is illegal */
    if (mbf_is_zero(a) || mbf_is_negative(a)) {
        return MBF_ZERO;  /* Should trigger FC error in caller */
    }

    double val = mbf_to_double(a);
    double result = log(val);
    return mbf_from_double(result);
}

mbf_t mbf_exp(mbf_t a) {
    double val = mbf_to_double(a);
    double result = exp(val);

    /* Check for overflow */
    if (isinf(result) || result > 1e38) {
        return MBF_ZERO;  /* Should trigger OV error */
    }

    return mbf_from_double(result);
}
