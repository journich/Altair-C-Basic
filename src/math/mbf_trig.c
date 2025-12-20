/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/*
 * mbf_trig.c - Transcendental functions (SIN, COS, TAN, ATN, LOG, EXP, SQR)
 *
 * These implementations use standard math library functions converted
 * through the MBF<->double conversion. For byte-for-byte compatibility
 * with the original, these should be replaced with the exact polynomial
 * approximations from the 8080 assembly.
 */

#include "basic/mbf.h"
#include <math.h>

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
