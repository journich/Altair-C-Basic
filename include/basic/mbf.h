/*
 * mbf.h - Microsoft Binary Format (MBF) Floating Point
 *
 * This implements the exact floating-point format used by Altair 8K BASIC 4.0.
 * NOT IEEE 754 - this is a custom format with different exponent bias and layout.
 *
 * Format (4 bytes, little-endian in memory):
 *   Byte 0: Mantissa low (bits 0-7)
 *   Byte 1: Mantissa mid (bits 8-15)
 *   Byte 2: Mantissa high (bits 16-22) + Sign (bit 7)
 *   Byte 3: Exponent (bias = 129)
 *
 * The mantissa has an implicit leading 1 bit (normalized form).
 * Zero is represented by exponent = 0 (mantissa ignored).
 *
 * Value = (-1)^sign * 1.mantissa * 2^(exponent - 129)
 */

#ifndef BASIC8K_MBF_H
#define BASIC8K_MBF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* MBF exponent bias (different from IEEE 754's 127) */
#define MBF_BIAS 129

/* Maximum exponent value (255 - bias = 126) */
#define MBF_MAX_EXP 255

/*
 * Microsoft Binary Format floating-point number.
 * Uses a union for easy byte access and whole-value operations.
 */
typedef union {
    struct {
        uint8_t mantissa_lo;    /* Bits 0-7 of mantissa */
        uint8_t mantissa_mid;   /* Bits 8-15 of mantissa */
        uint8_t mantissa_hi;    /* Bits 16-22 of mantissa, bit 7 = sign */
        uint8_t exponent;       /* Biased exponent (bias = 129) */
    } bytes;
    uint32_t raw;               /* For whole-value comparisons/copying */
    uint8_t byte_array[4];      /* For indexed access */
} mbf_t;

/* Compile-time constants */
#define MBF_ZERO    ((mbf_t){.raw = 0x00000000})
#define MBF_ONE     ((mbf_t){.raw = 0x81000000})  /* 1.0 = exp=129, mantissa=0 */

/*
 * MBF Arithmetic Operations
 * These must produce byte-for-byte identical results to the original 8080 code.
 */

/* Basic arithmetic */
mbf_t mbf_add(mbf_t a, mbf_t b);
mbf_t mbf_sub(mbf_t a, mbf_t b);
mbf_t mbf_mul(mbf_t a, mbf_t b);
mbf_t mbf_div(mbf_t a, mbf_t b);

/* Unary operations */
mbf_t mbf_neg(mbf_t a);
mbf_t mbf_abs(mbf_t a);

/* Comparison - returns -1, 0, or 1 */
int mbf_cmp(mbf_t a, mbf_t b);

/* Sign test - returns -1 (negative), 0 (zero), or 1 (positive) */
int mbf_sign(mbf_t a);

/* Check if zero */
static inline bool mbf_is_zero(mbf_t a) {
    return a.bytes.exponent == 0;
}

/* Check if negative (non-zero) */
static inline bool mbf_is_negative(mbf_t a) {
    return !mbf_is_zero(a) && (a.bytes.mantissa_hi & 0x80);
}

/*
 * Integer/MBF Conversion
 */
mbf_t mbf_from_int16(int16_t n);
mbf_t mbf_from_uint16(uint16_t n);
mbf_t mbf_from_int32(int32_t n);

/* Convert to integer (truncates toward zero) */
int16_t mbf_to_int16(mbf_t a, bool *overflow);
int32_t mbf_to_int32(mbf_t a, bool *overflow);

/*
 * String Conversion
 * These match the exact formatting of the original BASIC.
 */

/* Parse number from string, returns bytes consumed (0 on error) */
size_t mbf_from_string(const char *str, mbf_t *result);

/* Format number to string, returns length written */
size_t mbf_to_string(mbf_t a, char *buf, size_t buflen);

/*
 * Mathematical Functions
 * These use the exact polynomial approximations from the original 8K BASIC.
 */
mbf_t mbf_int(mbf_t a);     /* INT - floor function */
mbf_t mbf_sqr(mbf_t a);     /* SQR - square root */
mbf_t mbf_sin(mbf_t a);     /* SIN - sine (radians) */
mbf_t mbf_cos(mbf_t a);     /* COS - cosine (radians) */
mbf_t mbf_tan(mbf_t a);     /* TAN - tangent (radians) */
mbf_t mbf_atn(mbf_t a);     /* ATN - arctangent */
mbf_t mbf_log(mbf_t a);     /* LOG - natural logarithm */
mbf_t mbf_exp(mbf_t a);     /* EXP - e^x */

/*
 * IEEE Double Conversion (for transcendentals via C library)
 * Note: These are used for SIN/COS/TAN/ATN/LOG/EXP/SQR where exact
 * bit-for-bit matching isn't required and IEEE is acceptable.
 */
double mbf_to_double(mbf_t a);
mbf_t mbf_from_double(double x);

/*
 * Internal helper functions (exposed for testing)
 */

/* Normalize a number (shift mantissa until MSB = 1, adjust exponent) */
mbf_t mbf_normalize(mbf_t a);

/* Extract the 24-bit mantissa with implicit bit */
uint32_t mbf_get_mantissa24(mbf_t a);

/* Create MBF from components */
mbf_t mbf_make(bool negative, uint8_t exponent, uint32_t mantissa24);

/*
 * Error handling
 * These get set by operations that overflow/underflow.
 * The interpreter checks these after math operations.
 */
typedef enum {
    MBF_OK = 0,
    MBF_OVERFLOW,
    MBF_UNDERFLOW,
    MBF_DIV_ZERO,
    MBF_DOMAIN         /* Domain error (e.g., SQR of negative, LOG of zero) */
} mbf_error_t;

/* Get/clear/set last error */
mbf_error_t mbf_get_error(void);
void mbf_clear_error(void);
void mbf_set_error(mbf_error_t err);

#endif /* BASIC8K_MBF_H */
