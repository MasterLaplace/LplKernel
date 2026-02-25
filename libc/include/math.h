/**
 * @file math.h
 * @brief Mathematical functions for kernel space
 *
 * This provides basic math functions implemented without relying on
 * external libraries. Uses Taylor series and other approximations.
 */

#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Mathematical constants */
#define M_PI      3.14159265358979323846
#define M_PI_2    1.57079632679489661923 /* pi/2 */
#define M_PI_4    0.78539816339744830962 /* pi/4 */
#define M_E       2.71828182845904523536
#define M_LOG2E   1.44269504088896340736
#define M_LOG10E  0.43429448190325182765
#define M_LN2     0.69314718055994530942
#define M_LN10    2.30258509299404568402
#define M_SQRT2   1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440

/* Floating point infinity and NaN */
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))

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
 */
float sqrtf(float x);

/**
 * @brief Sine function (radians)
 */
float sinf(float x);

/**
 * @brief Cosine function (radians)
 */
float cosf(float x);

/**
 * @brief Tangent function (radians)
 */
float tanf(float x);

/**
 * @brief Exponential function (e^x)
 */
float expf(float x);

/**
 * @brief Natural logarithm
 */
float logf(float x);

/**
 * @brief Power function (x^y)
 */
float powf(float x, float y);

/**
 * @brief Hyperbolic sine
 */
float sinhf(float x);

/**
 * @brief Hyperbolic cosine
 */
float coshf(float x);

/**
 * @brief Hyperbolic tangent
 */
float tanhf(float x);

/**
 * @brief Arc tangent of y/x (returns angle in correct quadrant)
 */
float atan2f(float y, float x);

/* Double precision versions (implemented as float wrappers for now) */
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

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H */
