/*
 * mbf_arith.c - MBF Arithmetic Operations
 *
 * Implementation of add, subtract, multiply, divide that matches
 * the original 8K BASIC exactly (8kbas_src.mac lines 3243-3600).
 */

#include "basic/mbf.h"
#include <stdbool.h>


/*
 * Addition: a + b
 *
 * Algorithm from 8kbas_src.mac L1212 (FAdd):
 * 1. If either operand is zero, return the other
 * 2. Align mantissas by exponent difference
 * 3. Add or subtract based on signs
 * 4. Normalize result
 */
mbf_t mbf_add(mbf_t a, mbf_t b) {
    /* Handle zero cases */
    if (mbf_is_zero(a)) return b;
    if (mbf_is_zero(b)) return a;

    bool neg_a = mbf_is_negative(a);
    bool neg_b = mbf_is_negative(b);
    uint8_t exp_a = a.bytes.exponent;
    uint8_t exp_b = b.bytes.exponent;

    /* Get mantissas with implicit bit */
    uint32_t mant_a = mbf_get_mantissa24(a);
    uint32_t mant_b = mbf_get_mantissa24(b);

    /* Make a the larger exponent */
    if (exp_b > exp_a) {
        /* Swap */
        uint8_t tmp_exp = exp_a; exp_a = exp_b; exp_b = tmp_exp;
        uint32_t tmp_mant = mant_a; mant_a = mant_b; mant_b = tmp_mant;
        bool tmp_neg = neg_a; neg_a = neg_b; neg_b = tmp_neg;
    }

    /* Align mantissas - shift b right by exponent difference */
    int exp_diff = exp_a - exp_b;
    if (exp_diff > 24) {
        /* b is too small to matter */
        return mbf_make(neg_a, exp_a, mant_a);
    }

    /* Use 32-bit math for the addition with room for carry */
    uint32_t aligned_b = mant_b >> exp_diff;

    uint32_t result_mant;
    bool result_neg;
    uint8_t result_exp = exp_a;

    if (neg_a == neg_b) {
        /* Same sign: add magnitudes */
        result_mant = mant_a + aligned_b;
        result_neg = neg_a;

        /* Handle carry (overflow of 24-bit mantissa) */
        if (result_mant & 0x1000000) {
            result_mant >>= 1;
            result_exp++;
            if (result_exp == 0) {
                /* Overflow */
                mbf_set_error(MBF_OVERFLOW);
                return mbf_make(result_neg, 0xFF, 0xFFFFFF);
            }
        }
    } else {
        /* Different signs: subtract smaller from larger */
        if (mant_a >= aligned_b) {
            result_mant = mant_a - aligned_b;
            result_neg = neg_a;
        } else {
            result_mant = aligned_b - mant_a;
            result_neg = neg_b;
        }

        /* Result might be zero */
        if (result_mant == 0) {
            return MBF_ZERO;
        }

        /* Normalize: shift left until MSB is 1 */
        while ((result_mant & 0x800000) == 0) {
            result_mant <<= 1;
            result_exp--;
            if (result_exp == 0) {
                /* Underflow to zero */
                return MBF_ZERO;
            }
        }
    }

    return mbf_make(result_neg, result_exp, result_mant);
}

/*
 * Subtraction: a - b
 */
mbf_t mbf_sub(mbf_t a, mbf_t b) {
    return mbf_add(a, mbf_neg(b));
}

/*
 * Multiplication: a * b
 *
 * Algorithm from 8kbas_src.mac L135B (FMul):
 * 1. If either operand is zero, return zero
 * 2. Add exponents (subtract bias)
 * 3. Multiply mantissas (48-bit result, take top 24)
 * 4. Result sign is XOR of operand signs
 * 5. Normalize result
 */
mbf_t mbf_mul(mbf_t a, mbf_t b) {
    /* Handle zero cases */
    if (mbf_is_zero(a) || mbf_is_zero(b)) {
        return MBF_ZERO;
    }

    bool neg_a = mbf_is_negative(a);
    bool neg_b = mbf_is_negative(b);
    bool result_neg = (neg_a != neg_b);

    /* Calculate result exponent: exp_a + exp_b - bias */
    int result_exp = (int)a.bytes.exponent + (int)b.bytes.exponent - MBF_BIAS;

    /* Check for overflow/underflow */
    if (result_exp > 255) {
        mbf_set_error(MBF_OVERFLOW);
        return mbf_make(result_neg, 0xFF, 0xFFFFFF);
    }
    if (result_exp < 1) {
        mbf_set_error(MBF_UNDERFLOW);
        return MBF_ZERO;
    }

    /* Get mantissas */
    uint32_t mant_a = mbf_get_mantissa24(a);
    uint32_t mant_b = mbf_get_mantissa24(b);

    /*
     * Multiply 24-bit mantissas to get 48-bit result.
     * We use 64-bit arithmetic for the multiplication.
     */
    uint64_t product = (uint64_t)mant_a * (uint64_t)mant_b;

    /*
     * The product of two 24-bit mantissas (each with implicit 1 bit = 1.xxx)
     * gives a result between 1.0 and 4.0 (scaled by 2^46).
     *
     * If bit 47 is set (product >= 2.0 * 2^46), we take bits 47:24 and ADD 1 to exponent.
     * If only bit 46 is set (1.0 <= product < 2.0 * 2^46), we take bits 46:23.
     */
    uint32_t result_mant;
    if (product & 0x800000000000ULL) {
        /* Bit 47 set: product >= 2.0, shift right 24 and add 1 to exponent */
        result_mant = (uint32_t)(product >> 24);
        result_exp++;
        if (result_exp > 255) {
            mbf_set_error(MBF_OVERFLOW);
            return mbf_make(result_neg, 0xFF, 0xFFFFFF);
        }
    } else {
        /* Only bit 46 set: product < 2.0, shift right 23 */
        result_mant = (uint32_t)(product >> 23);
    }

    /* Ensure bit 23 is set (normalized) */
    while ((result_mant & 0x800000) == 0 && result_exp > 0) {
        result_mant <<= 1;
        result_exp--;
    }

    if (result_exp < 1) {
        return MBF_ZERO;
    }

    return mbf_make(result_neg, (uint8_t)result_exp, result_mant);
}

/*
 * Division: a / b
 *
 * Algorithm from 8kbas_src.mac L13A9 (FDiv):
 * 1. If divisor is zero, error
 * 2. If dividend is zero, return zero
 * 3. Subtract exponents (add bias)
 * 4. Divide mantissas
 * 5. Result sign is XOR of operand signs
 * 6. Normalize result
 */
mbf_t mbf_div(mbf_t a, mbf_t b) {
    /* Check for division by zero */
    if (mbf_is_zero(b)) {
        mbf_set_error(MBF_DIV_ZERO);
        return MBF_ZERO;  /* Return 0, error is signaled */
    }

    /* Zero dividend returns zero */
    if (mbf_is_zero(a)) {
        return MBF_ZERO;
    }

    bool neg_a = mbf_is_negative(a);
    bool neg_b = mbf_is_negative(b);
    bool result_neg = (neg_a != neg_b);

    /* Calculate result exponent: exp_a - exp_b + bias */
    int result_exp = (int)a.bytes.exponent - (int)b.bytes.exponent + MBF_BIAS;

    /* Get mantissas */
    uint32_t mant_a = mbf_get_mantissa24(a);
    uint32_t mant_b = mbf_get_mantissa24(b);

    /*
     * Divide using 64-bit arithmetic for precision.
     * Shift dividend left by 24 bits before dividing to get 24 bits of quotient.
     */
    uint64_t dividend = (uint64_t)mant_a << 24;
    uint32_t quotient = (uint32_t)(dividend / mant_b);

    /*
     * The quotient might need normalization.
     * quotient = (mant_a / mant_b) * 2^24
     *
     * If mant_a >= mant_b (ratio >= 1.0): quotient >= 2^24, bit 24 is set.
     *   Shift right to normalize, exponent stays the same.
     *
     * If mant_a < mant_b (ratio < 1.0): quotient < 2^24, bit 24 is clear.
     *   Quotient has bit 23 set (since ratio > 0.5 for normalized inputs).
     *   Decrement exponent to compensate for ratio < 1.0.
     */
    if (quotient & 0x1000000) {
        /* Bit 24 set: shift right to normalize, keep exponent */
        quotient >>= 1;
    } else {
        /* Bit 24 clear: mantissa ratio < 1.0, decrement exponent */
        result_exp--;
    }

    /* Normalize (safety net for edge cases) */
    while ((quotient & 0x800000) == 0 && result_exp > 0) {
        quotient <<= 1;
        result_exp--;
    }

    /* Check for overflow/underflow */
    if (result_exp > 255) {
        mbf_set_error(MBF_OVERFLOW);
        return mbf_make(result_neg, 0xFF, 0xFFFFFF);
    }
    if (result_exp < 1) {
        mbf_set_error(MBF_UNDERFLOW);
        return MBF_ZERO;
    }

    return mbf_make(result_neg, (uint8_t)result_exp, quotient);
}
