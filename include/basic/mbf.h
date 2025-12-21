/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file mbf.h
 * @brief Microsoft Binary Format (MBF) Floating Point
 *
 * This module implements the exact floating-point format used by Altair 8K
 * BASIC 4.0. This is NOT IEEE 754 - it's a custom Microsoft format with
 * different exponent bias and byte layout.
 *
 * ## Why Not IEEE 754?
 *
 * For 100% compatibility with the original BASIC, we must use MBF because:
 * 1. The RND function produces specific bit patterns that users relied on
 * 2. PRINT formatting depends on MBF precision characteristics
 * 3. Games like "Super Star Trek" have RND sequences baked into solutions
 * 4. Any deviation would break programs expecting exact numeric behavior
 *
 * ## MBF Format (4 bytes, little-endian)
 *
 * ```
 *   Byte 0   Byte 1   Byte 2   Byte 3
 *  [Mant_lo][Mant_mid][S|Mant_hi][Exponent]
 *
 *  Where:
 *    Mant_lo   = Bits 0-7 of mantissa (least significant)
 *    Mant_mid  = Bits 8-15 of mantissa
 *    S         = Sign bit (bit 7 of byte 2, 1 = negative)
 *    Mant_hi   = Bits 16-22 of mantissa (bits 0-6 of byte 2)
 *    Exponent  = Biased exponent (bias = 129)
 * ```
 *
 * ## Special Values
 *
 * - **Zero**: Exponent byte = 0 (mantissa ignored)
 * - **Normalized form**: Leading bit of mantissa is implicit 1
 *
 * ## Numeric Range
 *
 * - **Minimum positive**: ~2.9E-39 (exponent = 1)
 * - **Maximum positive**: ~1.7E+38 (exponent = 255)
 * - **Precision**: ~7 decimal digits (24-bit mantissa)
 *
 * ## Value Calculation
 *
 * For non-zero values:
 * ```
 * value = (-1)^sign * (1.mantissa) * 2^(exponent - 129)
 * ```
 *
 * The "1." before mantissa is the implicit leading bit.
 *
 * ## Example Representations
 *
 * | Decimal | Exponent | Mantissa (24-bit) | Sign | Raw Bytes |
 * |---------|----------|-------------------|------|-----------|
 * | 0       | 0x00     | (ignored)         | -    | 00 00 00 00 |
 * | 1       | 0x81     | 0x000000          | +    | 00 00 00 81 |
 * | -1      | 0x81     | 0x000000          | -    | 00 00 80 81 |
 * | 2       | 0x82     | 0x000000          | +    | 00 00 00 82 |
 * | 0.5     | 0x80     | 0x000000          | +    | 00 00 00 80 |
 * | 10      | 0x84     | 0x200000          | +    | 00 00 20 84 |
 *
 * ## Comparison with IEEE 754 Single Precision
 *
 * | Feature        | MBF             | IEEE 754        |
 * |----------------|-----------------|-----------------|
 * | Size           | 32 bits         | 32 bits         |
 * | Mantissa bits  | 24 (23 stored)  | 24 (23 stored)  |
 * | Exponent bits  | 8               | 8               |
 * | Exponent bias  | 129             | 127             |
 * | Sign location  | Byte 2, bit 7   | Byte 3, bit 7   |
 * | Byte order     | Little-endian   | Little-endian   |
 * | Zero           | exp=0           | exp=0, mant=0   |
 * | Denormals      | No              | Yes             |
 * | Inf/NaN        | No              | Yes             |
 *
 * ## Original Source Reference
 *
 * The MBF arithmetic routines are in 8kbas_src.mac:
 * - Lines 3243-3365: Floating-point addition and normalization
 * - Lines 3518-3600: Floating-point multiplication
 * - Lines 3601-3700: Floating-point division
 */

#ifndef BASIC8K_MBF_H
#define BASIC8K_MBF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


/*============================================================================
 * CONSTANTS
 *============================================================================*/

/**
 * @brief MBF exponent bias
 *
 * MBF uses 129 as the exponent bias (IEEE uses 127).
 * True exponent = stored_exponent - 129
 *
 * Examples:
 * - Exponent byte 0x81 (129) = 2^(129-129) = 2^0 = 1.0 (for mantissa 1.0)
 * - Exponent byte 0x82 (130) = 2^(130-129) = 2^1 = 2.0
 * - Exponent byte 0x80 (128) = 2^(128-129) = 2^-1 = 0.5
 */
#define MBF_BIAS 129

/**
 * @brief Maximum exponent value
 *
 * Exponent 255 is the maximum, giving 2^(255-129) = 2^126 ≈ 8.5E37.
 * With mantissa ~2, maximum value is about 1.7E38.
 */
#define MBF_MAX_EXP 255


/*============================================================================
 * MBF TYPE DEFINITION
 *============================================================================*/

/**
 * @brief Microsoft Binary Format floating-point number
 *
 * Uses a union for flexible access:
 * - `bytes` - access individual components
 * - `raw` - 32-bit value for quick copy/compare
 * - `byte_array` - indexed access for algorithms
 *
 * Memory layout (when stored in memory, little-endian):
 * ```
 * Address:   +0         +1          +2           +3
 * Content: [mant_lo] [mant_mid] [sign|mant_hi] [exponent]
 * ```
 *
 * Example usage:
 * @code
 *     mbf_t value;
 *     value.bytes.exponent = 0x81;    // Set exponent for 1.0
 *     value.bytes.mantissa_lo = 0;
 *     value.bytes.mantissa_mid = 0;
 *     value.bytes.mantissa_hi = 0;    // Positive, mantissa 1.0 (implicit)
 *
 *     // Or copy quickly:
 *     mbf_t copy = value;              // Uses raw member
 *
 *     // Or access by index:
 *     for (int i = 0; i < 4; i++) {
 *         printf("%02X ", value.byte_array[i]);
 *     }
 * @endcode
 */
typedef union {
    /**
     * @brief Named access to MBF components
     *
     * Note the non-intuitive order: least significant bytes first,
     * then sign+mantissa_hi, then exponent last.
     */
    struct {
        uint8_t mantissa_lo;    /**< Bits 0-7 of mantissa (LSB) */
        uint8_t mantissa_mid;   /**< Bits 8-15 of mantissa */
        uint8_t mantissa_hi;    /**< Bits 16-22 of mantissa + sign in bit 7 */
        uint8_t exponent;       /**< Biased exponent (add 129 for storage) */
    } bytes;

    /**
     * @brief Raw 32-bit value
     *
     * Useful for:
     * - Quick zero comparison: `if (val.raw == 0)`
     * - Fast copy: `dest = src` (copies via raw)
     * - Bitwise operations in algorithms
     */
    uint32_t raw;

    /**
     * @brief Indexed byte access
     *
     * byte_array[0] = mantissa_lo
     * byte_array[1] = mantissa_mid
     * byte_array[2] = mantissa_hi (with sign bit 7)
     * byte_array[3] = exponent
     */
    uint8_t byte_array[4];
} mbf_t;


/*============================================================================
 * COMPILE-TIME CONSTANTS
 *============================================================================*/

/**
 * @brief MBF representation of zero
 *
 * Zero is represented by exponent = 0. Mantissa bits are ignored.
 * This is the only way to represent zero in MBF.
 */
#define MBF_ZERO    ((mbf_t){.raw = 0x00000000})

/**
 * @brief MBF representation of 1.0
 *
 * Value: (-1)^0 * 1.0 * 2^(0x81 - 129) = 1 * 1 * 2^0 = 1.0
 *
 * Note: The raw value 0x81000000 puts 0x81 in the exponent byte
 * (byte 3 in little-endian) and zeros in mantissa/sign.
 */
#define MBF_ONE     ((mbf_t){.raw = 0x81000000})


/*============================================================================
 * BASIC ARITHMETIC OPERATIONS
 *
 * These functions must produce byte-for-byte identical results to the
 * original 8080 assembly code. The algorithms are carefully matched.
 *============================================================================*/

/**
 * @brief Add two MBF numbers
 *
 * Algorithm:
 * 1. If either is zero, return the other
 * 2. Align mantissas by shifting the smaller exponent's mantissa right
 * 3. Add mantissas (handling signs)
 * 4. Normalize result
 *
 * @param a First operand
 * @param b Second operand
 * @return Sum of a and b
 *
 * Sets MBF_OVERFLOW error if result exceeds range.
 */
mbf_t mbf_add(mbf_t a, mbf_t b);

/**
 * @brief Subtract two MBF numbers
 *
 * Implemented as: a - b = a + (-b)
 *
 * @param a First operand (minuend)
 * @param b Second operand (subtrahend)
 * @return Difference a - b
 */
mbf_t mbf_sub(mbf_t a, mbf_t b);

/**
 * @brief Multiply two MBF numbers
 *
 * Algorithm:
 * 1. If either is zero, return zero
 * 2. Add exponents (subtract bias once)
 * 3. Multiply mantissas (24-bit x 24-bit -> 48-bit)
 * 4. Take upper 24 bits of product
 * 5. XOR signs
 * 6. Normalize result
 *
 * @param a First operand
 * @param b Second operand
 * @return Product a * b
 *
 * Sets MBF_OVERFLOW error if result exceeds range.
 */
mbf_t mbf_mul(mbf_t a, mbf_t b);

/**
 * @brief Divide two MBF numbers
 *
 * Algorithm:
 * 1. If divisor is zero, set MBF_DIV_ZERO error
 * 2. If dividend is zero, return zero
 * 3. Subtract exponents (add bias once)
 * 4. Divide mantissas
 * 5. XOR signs
 * 6. Normalize result
 *
 * @param a Dividend
 * @param b Divisor
 * @return Quotient a / b
 *
 * Sets MBF_DIV_ZERO error if b is zero.
 * Sets MBF_OVERFLOW error if result exceeds range.
 */
mbf_t mbf_div(mbf_t a, mbf_t b);


/*============================================================================
 * UNARY OPERATIONS
 *============================================================================*/

/**
 * @brief Negate an MBF number
 *
 * Flips the sign bit (bit 7 of mantissa_hi byte).
 * Zero remains zero (sign of zero is undefined).
 *
 * @param a Value to negate
 * @return -a
 */
mbf_t mbf_neg(mbf_t a);

/**
 * @brief Absolute value of MBF number
 *
 * Clears the sign bit (bit 7 of mantissa_hi byte).
 *
 * @param a Input value
 * @return |a| (always non-negative)
 */
mbf_t mbf_abs(mbf_t a);


/*============================================================================
 * COMPARISON OPERATIONS
 *============================================================================*/

/**
 * @brief Compare two MBF numbers
 *
 * Returns a three-way comparison result like strcmp().
 *
 * @param a First value
 * @param b Second value
 * @return -1 if a < b, 0 if a == b, +1 if a > b
 *
 * Comparison algorithm:
 * 1. Handle zero cases (exponent = 0)
 * 2. Compare signs (negative < positive)
 * 3. Compare exponents (for same sign)
 * 4. Compare mantissas (for same exponent)
 */
int mbf_cmp(mbf_t a, mbf_t b);

/**
 * @brief Get sign of MBF number (SGN function)
 *
 * @param a Value to test
 * @return -1 if negative, 0 if zero, +1 if positive
 *
 * This matches the BASIC SGN() function exactly.
 */
int mbf_sign(mbf_t a);

/**
 * @brief Check if MBF number is zero
 *
 * In MBF, zero is represented solely by exponent = 0.
 * Mantissa bits are ignored.
 *
 * @param a Value to test
 * @return true if a is zero, false otherwise
 */
static inline bool mbf_is_zero(mbf_t a) {
    return a.bytes.exponent == 0;
}

/**
 * @brief Check if MBF number is negative
 *
 * Tests the sign bit (bit 7 of mantissa_hi byte).
 * Zero is never negative (even with sign bit set).
 *
 * @param a Value to test
 * @return true if a is negative (and non-zero)
 */
static inline bool mbf_is_negative(mbf_t a) {
    return !mbf_is_zero(a) && (a.bytes.mantissa_hi & 0x80);
}


/*============================================================================
 * INTEGER/MBF CONVERSION
 *============================================================================*/

/**
 * @brief Convert signed 16-bit integer to MBF
 *
 * @param n Integer value (-32768 to 32767)
 * @return MBF representation of n
 *
 * Algorithm:
 * 1. Handle zero case
 * 2. Record sign, take absolute value
 * 3. Find position of most significant bit
 * 4. Set exponent based on bit position + bias
 * 5. Shift mantissa to normalized form
 */
mbf_t mbf_from_int16(int16_t n);

/**
 * @brief Convert unsigned 16-bit integer to MBF
 *
 * @param n Integer value (0 to 65535)
 * @return MBF representation of n
 */
mbf_t mbf_from_uint16(uint16_t n);

/**
 * @brief Convert signed 32-bit integer to MBF
 *
 * Note: MBF has only 24 bits of mantissa, so integers
 * larger than 16,777,216 (2^24) will lose precision.
 *
 * @param n Integer value
 * @return MBF representation of n (may lose precision)
 */
mbf_t mbf_from_int32(int32_t n);

/**
 * @brief Convert MBF to signed 16-bit integer
 *
 * Truncates toward zero (like BASIC's INT function for positive,
 * but not quite - this truncates, INT floors).
 *
 * @param a MBF value to convert
 * @param[out] overflow Set to true if value doesn't fit in int16_t
 * @return Integer value, or 0 if overflow
 */
int16_t mbf_to_int16(mbf_t a, bool *overflow);

/**
 * @brief Convert MBF to signed 32-bit integer
 *
 * Truncates toward zero.
 *
 * @param a MBF value to convert
 * @param[out] overflow Set to true if value doesn't fit in int32_t
 * @return Integer value, or 0 if overflow
 */
int32_t mbf_to_int32(mbf_t a, bool *overflow);


/*============================================================================
 * STRING CONVERSION
 *
 * These must match the exact formatting rules of original BASIC.
 *============================================================================*/

/**
 * @brief Parse number from string to MBF
 *
 * Parses numeric constants in BASIC format:
 * - Integers: 123, -456
 * - Decimals: 3.14159, .5, 10.
 * - Scientific: 1E10, 3.14E-5, -2.5E+3
 *
 * @param str Input string (need not be null-terminated after number)
 * @param[out] result Parsed MBF value
 * @return Number of characters consumed, or 0 on error
 *
 * Example:
 * @code
 *     mbf_t value;
 *     size_t len = mbf_from_string("3.14159ABC", &value);
 *     // len = 7, value = 3.14159 (approximately)
 * @endcode
 */
size_t mbf_from_string(const char *str, mbf_t *result);

/**
 * @brief Format MBF number to string
 *
 * Formats according to original BASIC rules:
 * - Leading space for positive, "-" for negative
 * - No trailing zeros after decimal point
 * - Scientific notation for very large/small numbers
 * - Maximum ~9 characters for typical numbers
 *
 * @param a Value to format
 * @param buf Output buffer
 * @param buflen Size of output buffer
 * @return Number of characters written (not counting null terminator)
 *
 * Example outputs:
 * - 1 -> " 1" (with leading space)
 * - -1 -> "-1"
 * - 3.14159 -> " 3.14159"
 * - 0.0000001 -> " 1E-07"
 */
size_t mbf_to_string(mbf_t a, char *buf, size_t buflen);


/*============================================================================
 * MATHEMATICAL FUNCTIONS
 *
 * These implement BASIC's built-in numeric functions. Where possible,
 * they use the same polynomial approximations as the original 8K BASIC.
 *============================================================================*/

/**
 * @brief INT function - floor of MBF number
 *
 * Returns the largest integer not greater than the input.
 * This is floor(), not truncation.
 *
 * Examples:
 * - INT(3.7) = 3
 * - INT(-3.7) = -4 (not -3!)
 *
 * @param a Input value
 * @return Floor of a
 */
mbf_t mbf_int(mbf_t a);

/**
 * @brief SQR function - square root
 *
 * @param a Input value (must be non-negative)
 * @return Square root of a
 *
 * Sets MBF_DOMAIN error if a is negative.
 */
mbf_t mbf_sqr(mbf_t a);

/**
 * @brief SIN function - sine (radians)
 *
 * Uses polynomial approximation matching original BASIC.
 *
 * @param a Angle in radians
 * @return Sine of a
 */
mbf_t mbf_sin(mbf_t a);

/**
 * @brief COS function - cosine (radians)
 *
 * @param a Angle in radians
 * @return Cosine of a
 */
mbf_t mbf_cos(mbf_t a);

/**
 * @brief TAN function - tangent (radians)
 *
 * @param a Angle in radians
 * @return Tangent of a
 *
 * Note: Near pi/2, results may overflow.
 */
mbf_t mbf_tan(mbf_t a);

/**
 * @brief ATN function - arctangent
 *
 * @param a Input value
 * @return Arctangent in radians (-pi/2 to pi/2)
 */
mbf_t mbf_atn(mbf_t a);

/**
 * @brief LOG function - natural logarithm
 *
 * @param a Input value (must be positive)
 * @return Natural logarithm of a
 *
 * Sets MBF_DOMAIN error if a <= 0.
 */
mbf_t mbf_log(mbf_t a);

/**
 * @brief EXP function - e^x
 *
 * @param a Exponent
 * @return e raised to the power a
 *
 * Sets MBF_OVERFLOW if result is too large.
 */
mbf_t mbf_exp(mbf_t a);


/*============================================================================
 * IEEE DOUBLE CONVERSION
 *
 * For transcendental functions, we may use the C library via IEEE doubles.
 * These functions convert between MBF and IEEE format.
 *============================================================================*/

/**
 * @brief Convert MBF to IEEE double
 *
 * Used internally for transcendental functions where exact bit-for-bit
 * matching with original BASIC is not required.
 *
 * @param a MBF value
 * @return IEEE double approximation
 *
 * Note: This is exact for values within MBF range since IEEE double
 * has more precision than MBF.
 */
double mbf_to_double(mbf_t a);

/**
 * @brief Convert IEEE double to MBF
 *
 * Rounds to MBF precision.
 *
 * @param x IEEE double value
 * @return MBF approximation
 *
 * Sets MBF_OVERFLOW if x is outside MBF range.
 */
mbf_t mbf_from_double(double x);


/*============================================================================
 * INTERNAL HELPER FUNCTIONS
 *
 * Exposed for testing and for use by mbf_arith.c, mbf_trig.c, etc.
 *============================================================================*/

/**
 * @brief Normalize an MBF number
 *
 * Shifts the mantissa left until the most significant bit is 1,
 * adjusting the exponent accordingly. This is called after arithmetic
 * operations to maintain the implicit leading 1 bit.
 *
 * @param a Unnormalized MBF value
 * @return Normalized MBF value
 *
 * If the value underflows to zero during normalization, returns MBF_ZERO.
 */
mbf_t mbf_normalize(mbf_t a);

/**
 * @brief Extract 24-bit mantissa with implicit bit
 *
 * Returns the full 24-bit mantissa including the implicit leading 1.
 * The returned value has bit 23 set (the implicit bit) for non-zero inputs.
 *
 * @param a MBF value
 * @return 24-bit mantissa (0x800000 to 0xFFFFFF for normalized values)
 *
 * Returns 0 for zero inputs.
 */
uint32_t mbf_get_mantissa24(mbf_t a);

/**
 * @brief Create MBF from components
 *
 * Constructs an MBF value from sign, exponent, and 24-bit mantissa.
 * The mantissa should include the implicit bit.
 *
 * @param negative True for negative values
 * @param exponent Biased exponent (0 = zero, 1-255 = normal)
 * @param mantissa24 24-bit mantissa with implicit bit
 * @return Constructed MBF value
 */
mbf_t mbf_make(bool negative, uint8_t exponent, uint32_t mantissa24);


/*============================================================================
 * ERROR HANDLING
 *
 * MBF operations set these error flags instead of using exceptions.
 * The interpreter checks for errors after math operations.
 *============================================================================*/

/**
 * @brief MBF arithmetic error codes
 *
 * These are internal to the MBF module. The interpreter translates
 * them to BASIC error codes (e.g., MBF_OVERFLOW -> ERR_OV).
 */
typedef enum {
    MBF_OK = 0,        /**< No error - operation successful */
    MBF_OVERFLOW,      /**< Result exceeds MBF range (~1.7E38) */
    MBF_UNDERFLOW,     /**< Result too small (flushed to zero) */
    MBF_DIV_ZERO,      /**< Division by zero attempted */
    MBF_DOMAIN         /**< Domain error (SQR of negative, LOG of zero) */
} mbf_error_t;

/**
 * @brief Get last MBF error
 *
 * Returns the error from the most recent MBF operation.
 * Errors persist until cleared.
 *
 * @return Error code from last operation
 */
mbf_error_t mbf_get_error(void);

/**
 * @brief Clear MBF error state
 *
 * Resets the error to MBF_OK. Should be called before a sequence
 * of operations where you want to check for errors.
 */
void mbf_clear_error(void);

/**
 * @brief Set MBF error state
 *
 * Used internally by MBF functions to record errors.
 *
 * @param err Error code to set
 */
void mbf_set_error(mbf_error_t err);

#endif /* BASIC8K_MBF_H */
