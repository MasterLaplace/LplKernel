/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
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
 * @file math.h
 * @brief Mathematical functions for kernel space
 *
 * This provides basic math functions implemented without relying on
 * external libraries. Uses Taylor series and other approximations.
 *
 * These are software implementations suitable for kernel space, where the standard
 * C library is not available.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-02-25
 **************************************************************************/

#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Mathematical constants
 * @{
 */
#define M_PI      3.14159265358979323846
#define M_PI_2    1.57079632679489661923 /**< pi/2 */
#define M_PI_4    0.78539816339744830962 /**< pi/4 */
#define M_E       2.71828182845904523536
#define M_LOG2E   1.44269504088896340736
#define M_LOG10E  0.43429448190325182765
#define M_LN2     0.69314718055994530942
#define M_LN10    2.30258509299404568402
#define M_SQRT2   1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440
/** @} */

/**
 * @name Floating point infinity and NaN
 * @{
 */
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
/** @} */

/**
 * @brief Absolute value of a float
 */
float fabsf(float x);

/**
 * @brief Floor function - largest integer <= x
 */
float floorf(float x);

/**
 * @brief Ceiling function - smallest integer >= x
 */
float ceilf(float x);

/**
 * @brief Floating point modulo
 */
float fmodf(float x, float y);

/**
 * @brief Square root
 *
 * @details x times its inverse square root, the latter guessed from the float's bits and
 *          refined by three Newton-Raphson iterations.
 */
float sqrtf(float x);

/**
 * @brief Sine function (radians)
 */
float sinf(float x);

/**
 * @brief Cosine function (radians)
 *
 * @details cos(x) = sin(x + pi/2).
 */
float cosf(float x);

/**
 * @brief Tangent function (radians)
 */
float tanf(float x);

/**
 * @brief Exponential function (e^x)
 *
 * @details Range-reduced as e^x = e^(k*ln2 + r) = 2^k * e^r with r small, so the Taylor
 *          series only ever sees a small argument. Saturates beyond |x| = 88, where a float
 *          overflows or underflows.
 */
float expf(float x);

/**
 * @brief Natural logarithm
 *
 * @details log(m * 2^e) = log(m) + e*log(2), with the mantissa m in [1, 2) so its logarithm
 *          is a Taylor series around 1.
 */
float logf(float x);

/**
 * @brief Power function (x^y)
 *
 * @details x^y = e^(y * ln|x|).
 */
float powf(float x, float y);

/**
 * @brief Hyperbolic sine
 *
 * @details sinh(x) = (e^x - e^(-x)) / 2.
 */
float sinhf(float x);

/**
 * @brief Hyperbolic cosine
 *
 * @details cosh(x) = (e^x + e^(-x)) / 2.
 */
float coshf(float x);

/**
 * @brief Hyperbolic tangent
 *
 * @details tanh(x) = (e^2x - 1) / (e^2x + 1), saturated to ±1 beyond |x| = 20 so e^2x
 *          cannot overflow.
 */
float tanhf(float x);

/**
 * @brief Arc tangent of y/x (returns angle in correct quadrant)
 */
float atan2f(float y, float x);

/**
 * @name Double precision versions (implemented as float wrappers for now)
 * @{
 */
double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double exp(double x);
double log(double x);
double pow(double x, double y);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double atan2(double y, double x);
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H */
