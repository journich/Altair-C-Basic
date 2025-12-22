/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file mbf_arith.c
 * @brief MBF Arithmetic Operations - Add, Subtract, Multiply, Divide
 *
 * This module implements the four basic arithmetic operations for
 * MBF floating-point numbers. These algorithms are carefully matched
 * to the original 8K BASIC implementation.
 *
 * ## Original Source Reference
 *
 * The algorithms are from 8kbas_src.mac:
 * - Addition: lines 3243-3365 (FAdd, with normalization)
 * - Subtraction: (implemented as add + negate)
 * - Multiplication: lines 3518-3600 (FMul)
 * - Division: lines 3601-3700 (FDiv)
 *
 * ## Algorithm Notes
 *
 * ### Addition
 * 1. Handle zero cases (a+0 = a, 0+b = b)
 * 2. Align mantissas by shifting the smaller value right
 * 3. Add or subtract mantissas based on signs
 * 4. Normalize the result
 *
 * ### Multiplication
 * 1. Handle zero cases (a*0 = 0, 0*b = 0)
 * 2. Add exponents, subtract bias once
 * 3. Multiply 24-bit mantissas -> 48-bit product
 * 4. Take top 24 bits of product
 * 5. XOR signs for result sign
 * 6. Normalize
 *
 * ### Division
 * 1. Check for division by zero
 * 2. Handle zero dividend
 * 3. Subtract exponents, add bias once
 * 4. Divide mantissas
 * 5. XOR signs for result sign
 * 6. Normalize
 *
 * ## Precision
 *
 * All operations maintain 24-bit mantissa precision. Intermediate
 * calculations may use 32-bit or 64-bit integers for accuracy.
 */

#include "basic/mbf.h"
#include <stdbool.h>


/*============================================================================
 * ADDITION
 *============================================================================*/

/**
 * @brief Add two MBF numbers
 *
 * Implements floating-point addition with proper alignment and normalization.
 *
 * @param a First operand
 * @param b Second operand
 * @return Sum a + b
 *
 * Errors:
 * - MBF_OVERFLOW: Result exceeds MBF range
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


/*============================================================================
 * SUBTRACTION
 *============================================================================*/

/**
 * @brief Subtract two MBF numbers
 *
 * Implemented as: a - b = a + (-b)
 *
 * @param a Minuend
 * @param b Subtrahend
 * @return Difference a - b
 */
mbf_t mbf_sub(mbf_t a, mbf_t b) {
    return mbf_add(a, mbf_neg(b));
}


/*============================================================================
 * MULTIPLICATION
 *============================================================================*/

/**
 * @brief Multiply two MBF numbers using exact 8080 algorithm
 *
 * This implements the EXACT shift-and-add algorithm from 8kbas_src.mac
 * lines 3518-3594 (L135B/fmult2/fmult4).
 *
 * The 8080 uses RAR (rotate right through carry) which creates a 33-bit
 * shift register: carry + C + H + L + B. The algorithm:
 *
 * For each FAC mantissa byte (lo, mid, hi):
 *   If byte is 0: shift product right by 8 (optimization)
 *   Else for each of 8 bits:
 *     1. RAR the FAC byte - bit 0 goes to carry
 *     2. If carry: add multiplicand to C,H,L (24-bit add with carry to C)
 *     3. RAR through C,H,L,B - shifts right with carry propagation
 *
 * The key is that RAR rotates THROUGH the carry flag, so the carry from
 * the addition becomes the new MSB after shifting.
 *
 * @param a First operand (FAC - the value being multiplied)
 * @param b Second operand (multiplicand from memory)
 * @return Product a * b
 */
mbf_t mbf_mul(mbf_t a, mbf_t b) {
    /* Handle zero cases */
    if (mbf_is_zero(a) || mbf_is_zero(b)) {
        return MBF_ZERO;
    }

    bool neg_a = mbf_is_negative(a);
    bool neg_b = mbf_is_negative(b);
    bool result_neg = (neg_a != neg_b);

    /*
     * Calculate result exponent: exp_a + exp_b - 128
     *
     * Note: The bias for MULTIPLICATION is 128, not 129!
     * This matches the 8080's muldiv code which uses ADI 80H (add 128).
     *
     * Mathematical justification:
     * - Both mantissas have implicit 1 at bit 23, so they're in [2^23, 2^24)
     * - Product is up to 48 bits, with MSB at bit 46-47
     * - We take top 24 bits, effectively dividing by 2^24
     * - This requires subtracting 24 from the exponent sum
     * - But wait, the mantissa interpretation uses 2^(exp - 129 - 23)
     * - So: result_exp - 152 = exp_a + exp_b - 258 - 46 + 24
     *       result_exp = exp_a + exp_b - 128
     */
    int result_exp = (int)a.bytes.exponent + (int)b.bytes.exponent - 128;

    /* Check for overflow/underflow */
    if (result_exp > 255) {
        mbf_set_error(MBF_OVERFLOW);
        return mbf_make(result_neg, 0xFF, 0xFFFFFF);
    }
    if (result_exp < 1) {
        mbf_set_error(MBF_UNDERFLOW);
        return MBF_ZERO;
    }

    /*
     * Get mantissas with implicit bit.
     * 'a' is the FAC (multiplier) - we process its bits one by one.
     * 'b' is the multiplicand - we add it when FAC bits are 1.
     */
    uint32_t fac_mant = mbf_get_mantissa24(a);
    uint32_t mult_mant = mbf_get_mantissa24(b);

    /*
     * 8080 register simulation:
     *   C = product_hi (8 bits)
     *   H = product_mid (8 bits)
     *   L = product_lo (8 bits)
     *   B = rounding byte (8 bits)
     *   carry = carry flag (1 bit)
     *
     * Multiplicand is in DE with high byte stored separately:
     *   mult_lo = mult_mant & 0xFF
     *   mult_mid = (mult_mant >> 8) & 0xFF
     *   mult_hi = (mult_mant >> 16) & 0xFF
     */
    uint8_t C = 0, H = 0, L = 0, B = 0;
    uint8_t carry = 0;

    uint8_t mult_lo = mult_mant & 0xFF;
    uint8_t mult_mid = (mult_mant >> 8) & 0xFF;
    uint8_t mult_hi = (mult_mant >> 16) & 0xFF;

    /* FAC mantissa bytes (processed lo, mid, hi) */
    uint8_t fac_bytes[3] = {
        (uint8_t)(fac_mant & 0xFF),
        (uint8_t)((fac_mant >> 8) & 0xFF),
        (uint8_t)((fac_mant >> 16) & 0xFF)
    };

    for (int byte_idx = 0; byte_idx < 3; byte_idx++) {
        uint8_t fac_byte = fac_bytes[byte_idx];

        if (fac_byte == 0) {
            /*
             * fmult3: Shift product right by 8 (optimization)
             * mov b,e; mov e,d; mov d,c; mov c,a (where a=0)
             */
            B = L;
            L = H;
            H = C;
            C = 0;
        } else {
            /*
             * fmult4 loop: Process 8 bits of FAC byte
             *
             * IMPORTANT: The 8080's 'ora a' instruction (which tests if
             * the byte is zero) also CLEARS the carry flag! We must do
             * the same before entering the bit loop.
             */
            carry = 0;  /* ora a clears carry */
            for (int bit = 0; bit < 8; bit++) {
                /*
                 * RAR: Rotate A right through carry
                 * New carry = bit 0 of A
                 * Bit 7 of A = old carry
                 */
                uint8_t new_carry = fac_byte & 1;
                fac_byte = (uint8_t)((fac_byte >> 1) | (carry << 7));
                carry = new_carry;

                if (carry) {
                    /*
                     * Add multiplicand to C,H,L:
                     * dad d: HL = HL + DE (add mult_lo/mid to H/L)
                     * aci 0: C = C + 0 + carry
                     */
                    uint16_t hl = (uint16_t)(((uint16_t)H << 8) | L);
                    uint16_t de = (uint16_t)(((uint16_t)mult_mid << 8) | mult_lo);
                    uint32_t sum = hl + de;
                    L = sum & 0xFF;
                    H = (sum >> 8) & 0xFF;
                    carry = (sum >> 16) & 1;

                    /* aci 00H: C = C + carry + mult_hi */
                    uint16_t c_sum = (uint16_t)C + (uint16_t)mult_hi + carry;
                    C = c_sum & 0xFF;
                    carry = (c_sum >> 8) & 1;
                }

                /*
                 * RAR through C, H, L, B:
                 * Each RAR rotates right through carry
                 */
                uint8_t new_c_carry = C & 1;
                C = (uint8_t)((C >> 1) | (carry << 7));
                carry = new_c_carry;

                uint8_t new_h_carry = H & 1;
                H = (uint8_t)((H >> 1) | (carry << 7));
                carry = new_h_carry;

                uint8_t new_l_carry = L & 1;
                L = (uint8_t)((L >> 1) | (carry << 7));
                carry = new_l_carry;

                uint8_t new_b_carry = B & 1;
                B = (uint8_t)((B >> 1) | (carry << 7));
                carry = new_b_carry;
            }
        }
    }

    /*
     * After multiplication:
     *   C,H,L = 24-bit mantissa result
     *   B = rounding byte (extended precision)
     *
     * The 8080's 'normal' function includes B in the shift loop:
     *   - L = B (rounding byte), H = original L (low product byte)
     *   - Shift C,D,H,L together, so B's bits flow into the product
     *   - After shifting, round based on the new extended precision bits
     */

    /*
     * 8080-style normalization with extended precision byte.
     * We shift the full 32 bits (C << 24 | H << 16 | L << 8 | B).
     * After each shift, B's bits flow into L, L's into H, etc.
     */
    while ((C & 0x80) == 0 && (C != 0 || H != 0 || L != 0) && result_exp > 0) {
        /* Shift C,H,L,B left together */
        uint8_t shift_carry = (B >> 7) & 1;
        B = (uint8_t)(B << 1);

        uint8_t next_carry = (L >> 7) & 1;
        L = (uint8_t)((L << 1) | shift_carry);
        shift_carry = next_carry;

        next_carry = (H >> 7) & 1;
        H = (uint8_t)((H << 1) | shift_carry);
        shift_carry = next_carry;

        C = (uint8_t)((C << 1) | shift_carry);

        result_exp--;
    }

    /*
     * Rounding: The 8080 tests bit 7 of B (the extended precision byte)
     * after the shift loop completes. Since we simulated the exact shift
     * behavior (including B), we test bit 7 of the current B value.
     */
    if (B & 0x80) {
        /* rounda: increment L, propagate carry through H and C */
        L++;
        if (L == 0) {
            H++;
            if (H == 0) {
                C++;
                if (C == 0) {
                    C = 0x80;
                    result_exp++;
                }
            }
        }
    }

    uint32_t result_mant = ((uint32_t)C << 16) | ((uint32_t)H << 8) | L;

    if (result_exp < 1 || result_mant == 0) {
        return MBF_ZERO;
    }
    if (result_exp > 255) {
        mbf_set_error(MBF_OVERFLOW);
        return mbf_make(result_neg, 0xFF, 0xFFFFFF);
    }

    return mbf_make(result_neg, (uint8_t)result_exp, result_mant);
}


/*============================================================================
 * DIVISION
 *============================================================================*/

/**
 * @brief Divide two MBF numbers
 *
 * Implements floating-point division.
 *
 * @param a Dividend
 * @param b Divisor
 * @return Quotient a / b
 *
 * Errors:
 * - MBF_DIV_ZERO: Division by zero
 * - MBF_OVERFLOW: Result exceeds MBF range
 * - MBF_UNDERFLOW: Result is too small
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
