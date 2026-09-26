/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file stdfix.h
 * @brief Q16.16 fixed-point type of the kernel C library.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-02-25
 **************************************************************************/

#ifndef _STDFIX_H
#define _STDFIX_H

#include <stdint.h>

typedef int32_t q16_16_t;
typedef q16_16_t fixed_t;

#define FIX_FRAC_BITS 16
#define FIX_SCALE     (1 << FIX_FRAC_BITS)

/**
 * @brief Converts an integer to a fixed-point number in q16.16 format.
 *
 * @param i  The integer to convert.
 * @return q16_16_t  The converted fixed-point number in q16.16 format.
 */
static inline q16_16_t int_to_q(int32_t i) { return (q16_16_t) (i << FIX_FRAC_BITS); }

/**
 * @brief Converts a fixed-point number in q16.16 format to an integer, rounding towards zero.
 *
 * @param q  The fixed-point number in q16.16 format to convert.
 * @return int32_t  The converted integer.
 */
static inline int32_t q_to_int(q16_16_t q) { return (int32_t) (q >> FIX_FRAC_BITS); }

/**
 * @brief Multiplies two fixed-point numbers in q16.16 format, returning the result in the same format.
 *
 * @details The multiplication is performed using 64-bit intermediate results to prevent overflow,
 * and the final result is rounded to the nearest fixed-point value to avoid precision loss.
 *
 * @param x  The first operand in q16.16 format.
 * @param y  The second operand in q16.16 format.
 * @return q16_16_t  The product in q16.16 format.
 */
static inline q16_16_t q_mul(q16_16_t x, q16_16_t y)
{
    int64_t tmp = (int64_t) x * (int64_t) y;
    tmp += (1 << (FIX_FRAC_BITS - 1));
    return (q16_16_t) (tmp >> FIX_FRAC_BITS);
}

/**
 * @brief Division of two fixed-point numbers with handling of division by zero.
 *
 * @warning This function does not handle division by zero.
 * The caller must ensure that `y` is not zero before calling this function.
 *
 * @param x  The dividend in q16.16 format.
 * @param y  The divisor in q16.16 format.
 * @return q16_16_t  The quotient in q16.16 format.
 */
static inline q16_16_t q_div(q16_16_t x, q16_16_t y) { return (q16_16_t) (((int64_t) x << FIX_FRAC_BITS) / y); }

#endif /* _STDFIX_H */
