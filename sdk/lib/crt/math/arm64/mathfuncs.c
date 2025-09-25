/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 Math Functions Implementation
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <precomp.h>
#include <math.h>
#include <float.h>

/* For kernel mode (libcntpr), errno is not available */
#ifndef _LIBCNT_
#include <errno.h>
#endif

/* For kernel mode, _HUGE is not available, use INFINITY instead */
#ifdef _LIBCNT_
#undef HUGE_VAL
#define HUGE_VAL INFINITY
#endif

/* Forward declarations for ARM64 */
int __cdecl _isnan(double x);
int __cdecl _finite(double x);

/* fabsf - removed duplicate, use the one from fabsf.c */

/* ldexp - multiply x by 2^exp */
double ldexp(double x, int exp)
{
    if (_isnan(x))
    {
#ifndef _LIBCNT_
        errno = EDOM;
#endif
        return x;
    }

    if (!_finite(x) || x == 0.0)
        return x;

    /* Use bit manipulation for efficiency on ARM64 */
    union {
        double d;
        unsigned long long u;
    } val;

    val.d = x;
    int current_exp = ((val.u >> 52) & 0x7FF);

    if (current_exp == 0) {
        /* Denormalized number */
        return x * pow(2.0, exp);
    }

    current_exp += exp;

    if (current_exp <= 0) {
        /* Result is denormalized or underflow */
#ifndef _LIBCNT_
        errno = ERANGE;
#endif
        return 0.0;
    }

    if (current_exp >= 0x7FF) {
        /* Overflow */
#ifndef _LIBCNT_
        errno = ERANGE;
#endif
        return x < 0 ? -HUGE_VAL : HUGE_VAL;
    }

    val.u = (val.u & ~(0x7FFULL << 52)) | ((unsigned long long)current_exp << 52);
    return val.d;
}

/* exp - exponential function */
double exp(double x)
{
    /* Simple implementation using Taylor series for now */
    /* TODO: Replace with optimized ARM64 NEON implementation */

    if (_isnan(x))
        return x;

    if (x > 709.782712893384) {
#ifndef _LIBCNT_
        errno = ERANGE;
#endif
        return HUGE_VAL;
    }

    if (x < -745.133219101941) {
        return 0.0;
    }

    /* Use the standard library exp for now - will be optimized later */
    /* This is a temporary implementation to get the build working */
    double result = 1.0;
    double term = 1.0;
    int i;

    /* Taylor series: exp(x) = 1 + x + x^2/2! + x^3/3! + ... */
    for (i = 1; i < 50; i++) {
        term *= x / i;
        result += term;
        if (term < 1e-15 && term > -1e-15)
            break;
    }

    return result;
}

/* log - natural logarithm */
double log(double x)
{
    if (_isnan(x))
        return x;

    if (x < 0) {
#ifndef _LIBCNT_
        errno = EDOM;
#endif
        return NAN;
    }

    if (x == 0) {
#ifndef _LIBCNT_
        errno = ERANGE;
#endif
        return -HUGE_VAL;
    }

    /* Simple implementation using series expansion */
    /* TODO: Replace with optimized ARM64 implementation */

    /* Normalize x to range [1, 2) */
    int exp_part = 0;
    double mantissa = x;

    while (mantissa >= 2.0) {
        mantissa /= 2.0;
        exp_part++;
    }

    while (mantissa < 1.0) {
        mantissa *= 2.0;
        exp_part--;
    }

    /* Use series expansion for log(1 + y) where y = mantissa - 1 */
    double y = mantissa - 1.0;
    double result = y;
    double term = y;
    int i;

    for (i = 2; i < 100; i++) {
        term *= -y;
        result += term / i;
        if (term < 1e-15 && term > -1e-15)
            break;
    }

    /* Add the exponent part: log(x) = log(mantissa) + exp_part * log(2) */
    result += exp_part * 0.693147180559945309417;

    return result;
}

/* tan - tangent function */
double tan(double x)
{
    /* Use sin/cos for now */
    return sin(x) / cos(x);
}

/* atan - arc tangent function */
double atan(double x)
{
    if (_isnan(x))
        return x;

    /* Simple implementation using series expansion */
    /* TODO: Replace with optimized ARM64 implementation */

    int invert = 0;
    double sign = 1.0;

    if (x < 0) {
        x = -x;
        sign = -1.0;
    }

    if (x > 1.0) {
        x = 1.0 / x;
        invert = 1;
    }

    /* Use series expansion for small x */
    double x2 = x * x;
    double result = x;
    double term = x;
    int i;

    for (i = 1; i < 50; i++) {
        term *= -x2;
        result += term / (2 * i + 1);
        if (term < 1e-15 && term > -1e-15)
            break;
    }

    if (invert)
        result = 1.5707963267948966192313 - result; /* pi/2 - result */

    return sign * result;
}

/* ceil - ceiling function (rounds up) */
double ceil(double x)
{
    if (_isnan(x) || !_finite(x))
        return x;

    /* Use bit manipulation for ARM64 efficiency */
    union {
        double d;
        unsigned long long u;
    } val;

    val.d = x;

    /* Extract exponent */
    int exp = ((val.u >> 52) & 0x7FF) - 1023;

    /* If x is already an integer or too large to have fraction */
    if (exp >= 52)
        return x;

    /* If x is less than 1 */
    if (exp < 0) {
        if (x <= 0.0)
            return x < 0.0 ? -0.0 : 0.0;
        return 1.0;
    }

    /* Clear fractional bits */
    unsigned long long mask = ~((1ULL << (52 - exp)) - 1);
    unsigned long long frac_mask = ~mask & 0xFFFFFFFFFFFFFULL;

    /* Check if there are fractional bits */
    int has_fraction = (val.u & frac_mask) != 0;

    /* Clear fractional part */
    val.u &= mask;

    /* If positive and has fraction, round up */
    if (x > 0.0 && has_fraction) {
        val.d += 1.0;
    }

    return val.d;
}