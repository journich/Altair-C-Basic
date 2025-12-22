/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Tim Buchalka
 * Based on Altair 8K BASIC 4.0, Copyright (c) 1976 Microsoft
 */

/**
 * @file mbf.c
 * @brief Microsoft Binary Format - Core Operations
 *
 * This module implements the core MBF floating-point operations:
 * - Error handling (thread-safe)
 * - Mantissa access and manipulation
 * - Normalization
 * - Sign operations (negation, absolute value, sign detection)
 * - Comparison
 * - Integer/MBF conversion
 * - String/MBF conversion
 * - INT function (floor)
 *
 * ## MBF Format Reminder
 *
 * MBF is a 4-byte floating-point format:
 * ```
 *   Byte 0: Mantissa bits 0-7 (LSB)
 *   Byte 1: Mantissa bits 8-15
 *   Byte 2: Mantissa bits 16-22 + sign in bit 7
 *   Byte 3: Biased exponent (bias = 129)
 *
 *   Value = (-1)^sign * (1.mantissa) * 2^(exponent - 129)
 * ```
 *
 * The leading 1 bit in the mantissa is implicit (not stored).
 *
 * ## Original Source Reference
 *
 * These functions match the behavior of routines in 8kbas_src.mac:
 * - Normalization: lines 3339-3365
 * - Sign test: RST 5 (FTestSign)
 * - INT function: lines 4560-4600
 *
 * ## Thread Safety
 *
 * Error state is thread-local, so each thread has its own error flag.
 */

#include "basic/mbf.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>


/*============================================================================
 * ERROR HANDLING
 *
 * MBF operations set error flags instead of using exceptions.
 * Error state is thread-local for thread safety.
 *============================================================================*/

/** Thread-local error state for current MBF operation */
static _Thread_local mbf_error_t mbf_last_error = MBF_OK;

/**
 * @brief Get the last MBF error
 * @return Error code from most recent operation
 */
mbf_error_t mbf_get_error(void) {
    return mbf_last_error;
}

/**
 * @brief Clear the MBF error state
 *
 * Call before a sequence of operations to detect errors.
 */
void mbf_clear_error(void) {
    mbf_last_error = MBF_OK;
}

/**
 * @brief Set the MBF error state
 *
 * Called internally by MBF operations when an error occurs.
 *
 * @param err Error code to set
 */
void mbf_set_error(mbf_error_t err) {
    mbf_last_error = err;
}


/*============================================================================
 * MANTISSA ACCESS
 *
 * The mantissa is stored in 3 bytes with an implicit leading 1 bit.
 * These functions provide convenient access to the full 24-bit mantissa.
 *============================================================================*/

/**
 * @brief Get the 24-bit mantissa with implicit leading 1 bit
 *
 * Reconstructs the full mantissa from the stored bytes and adds
 * the implicit leading 1 bit (bit 23).
 *
 * For zero values (exponent = 0), returns 0.
 *
 * @param a MBF value
 * @return 24-bit mantissa with bit 23 set (0x800000 to 0xFFFFFF)
 */
uint32_t mbf_get_mantissa24(mbf_t a) {
    if (a.bytes.exponent == 0) {
        return 0;  /* Zero */
    }
    /* Mantissa with implicit 1 bit set and sign bit cleared */
    uint32_t m = ((uint32_t)(a.bytes.mantissa_hi | 0x80) << 16) |
                 ((uint32_t)a.bytes.mantissa_mid << 8) |
                 (uint32_t)a.bytes.mantissa_lo;
    /* Clear the sign bit position (bit 23 becomes implicit 1) */
    return m | 0x800000;
}

/*
 * Create an MBF number from components.
 * mantissa24 should have bit 23 set (the implicit 1).
 */
mbf_t mbf_make(bool negative, uint8_t exponent, uint32_t mantissa24) {
    mbf_t result;

    if (exponent == 0) {
        /* Zero */
        result.raw = 0;
        return result;
    }

    result.bytes.exponent = exponent;
    result.bytes.mantissa_lo = (uint8_t)(mantissa24 & 0xFF);
    result.bytes.mantissa_mid = (uint8_t)((mantissa24 >> 8) & 0xFF);
    /* High byte: bits 16-22 of mantissa, bit 7 = sign */
    result.bytes.mantissa_hi = (uint8_t)((mantissa24 >> 16) & 0x7F);
    if (negative) {
        result.bytes.mantissa_hi |= 0x80;
    }

    return result;
}

/*
 * Normalize a number - shift mantissa left until MSB is 1, adjust exponent.
 * This matches the 'normal' routine in 8kbas_src.mac (lines 3339-3365).
 */
mbf_t mbf_normalize(mbf_t a) {
    if (a.bytes.exponent == 0) {
        return MBF_ZERO;  /* Already zero */
    }

    bool negative = (a.bytes.mantissa_hi & 0x80) != 0;
    uint32_t mantissa = mbf_get_mantissa24(a);
    uint8_t exponent = a.bytes.exponent;

    if (mantissa == 0) {
        return MBF_ZERO;
    }

    /* Shift left until bit 23 is set */
    while ((mantissa & 0x800000) == 0) {
        mantissa <<= 1;
        exponent--;
        if (exponent == 0) {
            /* Underflow to zero */
            mbf_last_error = MBF_UNDERFLOW;
            return MBF_ZERO;
        }
    }

    return mbf_make(negative, exponent, mantissa);
}


/*============================================================================
 * SIGN OPERATIONS
 *============================================================================*/

/**
 * @brief Negate an MBF number
 *
 * Flips the sign bit. Zero remains zero (sign of zero is undefined).
 *
 * @param a Value to negate
 * @return -a
 */
mbf_t mbf_neg(mbf_t a) {
    if (mbf_is_zero(a)) {
        return a;  /* -0 = 0 */
    }
    a.bytes.mantissa_hi ^= 0x80;  /* Flip sign bit */
    return a;
}

/*
 * Absolute value.
 */
mbf_t mbf_abs(mbf_t a) {
    a.bytes.mantissa_hi &= 0x7F;  /* Clear sign bit */
    return a;
}

/*
 * Return sign: -1 (negative), 0 (zero), or 1 (positive).
 * This matches the RST 5 (FTestSign) in the original.
 */
int mbf_sign(mbf_t a) {
    if (a.bytes.exponent == 0) {
        return 0;  /* Zero */
    }
    return (a.bytes.mantissa_hi & 0x80) ? -1 : 1;
}


/*============================================================================
 * COMPARISON
 *============================================================================*/

/**
 * @brief Compare two MBF numbers
 *
 * Three-way comparison like strcmp().
 *
 * Algorithm:
 * 1. Compare signs (negative < zero < positive)
 * 2. If same sign, compare exponents
 * 3. If same exponent, compare mantissas
 *
 * @param a First value
 * @param b Second value
 * @return -1 if a < b, 0 if a == b, 1 if a > b
 */
int mbf_cmp(mbf_t a, mbf_t b) {
    int sign_a = mbf_sign(a);
    int sign_b = mbf_sign(b);

    /* Handle sign differences */
    if (sign_a != sign_b) {
        return (sign_a > sign_b) ? 1 : -1;
    }

    /* Both zero */
    if (sign_a == 0) {
        return 0;
    }

    /* Same sign - compare magnitudes */
    /* First compare exponents */
    if (a.bytes.exponent != b.bytes.exponent) {
        int exp_cmp = (a.bytes.exponent > b.bytes.exponent) ? 1 : -1;
        return (sign_a > 0) ? exp_cmp : -exp_cmp;
    }

    /* Same exponent - compare mantissas (ignoring sign bit) */
    uint32_t mant_a = mbf_get_mantissa24(a);
    uint32_t mant_b = mbf_get_mantissa24(b);

    if (mant_a == mant_b) {
        return 0;
    }

    int mant_cmp = (mant_a > mant_b) ? 1 : -1;
    return (sign_a > 0) ? mant_cmp : -mant_cmp;
}


/*============================================================================
 * INTEGER CONVERSION
 *
 * These functions convert between MBF and integer types.
 * Note that MBF has 24 bits of mantissa, so integers larger than
 * 16,777,216 (2^24) may lose precision.
 *============================================================================*/

/**
 * @brief Convert 16-bit signed integer to MBF
 *
 * @param n Integer value (-32768 to 32767)
 * @return MBF representation of n
 */
mbf_t mbf_from_int16(int16_t n) {
    if (n == 0) {
        return MBF_ZERO;
    }

    bool negative = (n < 0);
    uint32_t value = negative ? (uint32_t)(-(int32_t)n) : (uint32_t)n;

    /* Shift to get mantissa in position (16 bits -> need 8 more shifts to reach bit 23) */
    uint32_t mantissa = value << 8;
    uint8_t exponent = MBF_BIAS + 15;  /* 2^15 position */

    /* Normalize */
    while ((mantissa & 0x800000) == 0 && exponent > 0) {
        mantissa <<= 1;
        exponent--;
    }

    return mbf_make(negative, exponent, mantissa);
}

/*
 * Convert 16-bit unsigned integer to MBF.
 */
mbf_t mbf_from_uint16(uint16_t n) {
    if (n == 0) {
        return MBF_ZERO;
    }

    uint32_t mantissa = (uint32_t)n << 8;
    uint8_t exponent = MBF_BIAS + 15;

    while ((mantissa & 0x800000) == 0 && exponent > 0) {
        mantissa <<= 1;
        exponent--;
    }

    return mbf_make(false, exponent, mantissa);
}

/*
 * Convert 32-bit signed integer to MBF.
 */
mbf_t mbf_from_int32(int32_t n) {
    if (n == 0) {
        return MBF_ZERO;
    }

    bool negative = (n < 0);
    uint32_t value = negative ? (uint32_t)(-n) : (uint32_t)n;

    /* Find the highest bit */
    uint8_t exponent = MBF_BIAS + 31;
    while ((value & 0x80000000) == 0 && exponent > MBF_BIAS) {
        value <<= 1;
        exponent--;
    }

    /* Extract top 24 bits for mantissa */
    uint32_t mantissa = (value >> 8) | 0x800000;

    return mbf_make(negative, exponent, mantissa);
}

/*
 * Convert MBF to 16-bit signed integer.
 * Truncates toward zero (like original INT function).
 */
int16_t mbf_to_int16(mbf_t a, bool *overflow) {
    if (mbf_is_zero(a)) {
        if (overflow) *overflow = false;
        return 0;
    }

    bool negative = mbf_is_negative(a);
    uint8_t exponent = a.bytes.exponent;

    /* Check for overflow (exponent too large for 16-bit) */
    /* Max value is 32767 = 2^14.xxx, so exponent must be <= 129+14 = 143 */
    if (exponent > MBF_BIAS + 14) {
        if (overflow) *overflow = true;
        return negative ? -32768 : 32767;
    }

    if (overflow) *overflow = false;

    /* exponent < 129 means value < 1 */
    if (exponent < MBF_BIAS) {
        return 0;
    }

    uint32_t mantissa = mbf_get_mantissa24(a);
    int shift = 23 - (exponent - MBF_BIAS);

    int32_t value;
    if (shift >= 0) {
        value = (int32_t)(mantissa >> shift);
    } else {
        value = (int32_t)(mantissa << (-shift));
    }

    return negative ? (int16_t)(-value) : (int16_t)value;
}

/*
 * Convert MBF to 32-bit signed integer.
 */
int32_t mbf_to_int32(mbf_t a, bool *overflow) {
    if (mbf_is_zero(a)) {
        if (overflow) *overflow = false;
        return 0;
    }

    bool negative = mbf_is_negative(a);
    uint8_t exponent = a.bytes.exponent;

    /* Check for overflow */
    if (exponent > MBF_BIAS + 30) {
        if (overflow) *overflow = true;
        return negative ? (int32_t)(-2147483647 - 1) : 2147483647;
    }

    if (overflow) *overflow = false;

    if (exponent < MBF_BIAS) {
        return 0;
    }

    uint32_t mantissa = mbf_get_mantissa24(a);
    int shift = 23 - (exponent - MBF_BIAS);

    int32_t value;
    if (shift >= 0) {
        value = (int32_t)(mantissa >> shift);
    } else if (shift > -8) {
        value = (int32_t)(mantissa << (-shift));
    } else {
        /* Would overflow */
        if (overflow) *overflow = true;
        return negative ? (int32_t)(-2147483647 - 1) : 2147483647;
    }

    return negative ? -value : value;
}

/*
 * INT function - floor (truncate toward negative infinity).
 * This matches the original behavior exactly.
 */
mbf_t mbf_int(mbf_t a) {
    if (mbf_is_zero(a)) {
        return MBF_ZERO;
    }

    uint8_t exponent = a.bytes.exponent;

    /* If exponent < 129, value is between -1 and 1 */
    if (exponent < MBF_BIAS) {
        if (mbf_is_negative(a)) {
            /* Negative fraction -> -1 */
            return mbf_from_int16(-1);
        }
        return MBF_ZERO;
    }

    /* If exponent >= 129 + 24, all bits are integer part */
    if (exponent >= MBF_BIAS + 24) {
        return a;
    }

    /* Mask off fractional bits */
    bool negative = mbf_is_negative(a);
    uint32_t mantissa = mbf_get_mantissa24(a);
    int frac_bits = 23 - (exponent - MBF_BIAS);

    if (frac_bits > 0) {
        uint32_t mask = ~((1u << frac_bits) - 1);
        uint32_t frac = mantissa & ~mask;
        mantissa &= mask;

        /* For negative numbers, if there was a fractional part, round down (away from zero) */
        if (negative && frac != 0) {
            /* Need to increase magnitude (make more negative) by adding 1 */
            mantissa += (1u << frac_bits);
            /* Handle carry/overflow - need to renormalize */
            if (mantissa & 0x1000000) {
                mantissa >>= 1;
                exponent++;
            }
        }
    }

    return mbf_make(negative, exponent, mantissa);
}

/*
 * Parse a number from string.
 * Returns number of characters consumed, or 0 on error.
 * TODO: Match exact original parsing behavior.
 */
size_t mbf_from_string(const char *str, mbf_t *result) {
    if (!str || !result) return 0;

    /* Skip leading whitespace */
    const char *p = str;
    while (*p == ' ') p++;

    /* Check for sign */
    bool negative = false;
    if (*p == '-') {
        negative = true;
        p++;
    } else if (*p == '+') {
        p++;
    }

    /* Parse integer part */
    int64_t int_part = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        int_part = int_part * 10 + (*p - '0');
        p++;
        digits++;
    }

    /* Parse fractional part */
    int64_t frac_part = 0;
    int frac_digits = 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            frac_part = frac_part * 10 + (*p - '0');
            p++;
            frac_digits++;
        }
    }

    /* Parse exponent */
    int exponent = 0;
    if (*p == 'E' || *p == 'e') {
        p++;
        int exp_sign = 1;
        if (*p == '-') {
            exp_sign = -1;
            p++;
        } else if (*p == '+') {
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            exponent = exponent * 10 + (*p - '0');
            p++;
        }
        exponent *= exp_sign;
    }

    if (digits == 0 && frac_digits == 0) {
        return 0;  /* No valid number */
    }

    /* Build the number */
    /* For integers that fit in int32, use the faster path */
    if (frac_digits == 0 && exponent == 0 && int_part <= 2147483647LL) {
        *result = mbf_from_int32((int32_t)int_part);
        if (negative) {
            *result = mbf_neg(*result);
        }
        return (size_t)(p - str);
    }

    /* Convert using double as intermediate for decimal numbers */
    double val = (double)int_part;

    /* Add fractional part */
    if (frac_digits > 0) {
        double frac_scale = 1.0;
        for (int i = 0; i < frac_digits; i++) {
            frac_scale *= 10.0;
        }
        val += (double)frac_part / frac_scale;
    }

    /* Apply exponent */
    if (exponent != 0) {
        double exp_scale = 1.0;
        int abs_exp = exponent < 0 ? -exponent : exponent;
        for (int i = 0; i < abs_exp; i++) {
            exp_scale *= 10.0;
        }
        if (exponent > 0) {
            val *= exp_scale;
        } else {
            val /= exp_scale;
        }
    }

    if (negative) {
        val = -val;
    }

    *result = mbf_from_double(val);
    return (size_t)(p - str);
}

/*
 * Format a number to string.
 * Returns number of characters written.
 * TODO: Match exact original formatting (6 significant digits, scientific notation).
 */
size_t mbf_to_string(mbf_t a, char *buf, size_t buflen) {
    if (!buf || buflen < 2) return 0;

    if (mbf_is_zero(a)) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    /* Convert to double for formatting */
    double val = mbf_to_double(a);
    double absval = val < 0 ? -val : val;

    /* Check if this is an integer value that fits in normal display */
    /* Original BASIC uses scientific notation for 7+ digit numbers */
    bool overflow;
    int32_t int_val = mbf_to_int32(a, &overflow);
    if (!overflow && absval < 1e6) {
        double check = (double)int_val;
        /* If converting back gives the same value, use integer format */
        if (check == val) {
            int len = snprintf(buf, buflen, "%ld", (long)int_val);
            return (size_t)(len > 0 ? len : 0);
        }
    }

    /* Floating-point formatting */
    /* BASIC typically uses up to 6 significant digits */
    int len;

    if (absval >= 1e6 || absval < 0.01) {
        /* Scientific notation for very large or small numbers */
        len = snprintf(buf, buflen, "%.5E", val);
    } else if (absval >= 1.0) {
        /* Regular decimal notation */
        /* Determine number of decimal places needed */
        if (absval >= 100000.0) {
            len = snprintf(buf, buflen, "%.0f", val);
        } else if (absval >= 10000.0) {
            len = snprintf(buf, buflen, "%.1f", val);
        } else if (absval >= 1000.0) {
            len = snprintf(buf, buflen, "%.2f", val);
        } else if (absval >= 100.0) {
            len = snprintf(buf, buflen, "%.3f", val);
        } else if (absval >= 10.0) {
            len = snprintf(buf, buflen, "%.4f", val);
        } else {
            len = snprintf(buf, buflen, "%.5f", val);
        }
    } else {
        /* Small numbers (0.01 to 1) */
        len = snprintf(buf, buflen, "%.6f", val);
    }

    /* Post-process the formatted string to match original BASIC */
    if (len > 0) {
        char *e_ptr = strchr(buf, 'E');

        if (e_ptr != NULL) {
            /* Scientific notation: convert "1.00000E+06" to "1E+06" */
            char *dot = strchr(buf, '.');
            if (dot && dot < e_ptr) {
                /* Remove trailing zeros before E and possibly the decimal point */
                char *p = e_ptr - 1;
                while (p > dot && *p == '0') {
                    p--;
                }
                if (p == dot) {
                    /* All zeros after decimal, remove the dot too */
                    memmove(dot, e_ptr, strlen(e_ptr) + 1);
                    len = (int)strlen(buf);
                } else if (p < e_ptr - 1) {
                    /* Some trailing zeros to remove */
                    memmove(p + 1, e_ptr, strlen(e_ptr) + 1);
                    len = (int)strlen(buf);
                }
            }
            /* Keep E+0X / E-0X format - original BASIC uses two-digit exponents */
            /* No simplification needed */
        } else {
            /* Regular decimal: remove trailing zeros */
            char *p = buf + len - 1;
            while (p > buf && *p == '0') {
                p--;
                len--;
            }
            if (*p == '.') {
                p--;
                len--;
            }
            *(p + 1) = '\0';
        }

        /* Remove leading zero before decimal point for 0.xxx -> .xxx */
        /* Check for "0." or "-0." at start */
        if (buf[0] == '0' && buf[1] == '.') {
            memmove(buf, buf + 1, strlen(buf));  /* Remove leading 0 */
            len--;
        } else if (buf[0] == '-' && buf[1] == '0' && buf[2] == '.') {
            memmove(buf + 1, buf + 2, strlen(buf + 1));  /* Remove leading 0 after - */
            len--;
        }
    }

    return (size_t)(len > 0 ? len : 0);
}
