/**
 * @file math.c
 * @brief Mathematical functions implementation
 *
 * Implements math functions using Taylor series and other approximations.
 * These are software implementations suitable for kernel space where
 * we can't rely on the standard C library.
 */

#include <math.h>

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/**
 * @brief Reduce angle to [-pi, pi] range
 */
static float reduce_angle(float x)
{
    /* Reduce to [0, 2*pi] first */
    while (x > M_PI)
        x -= 2.0f * M_PI;
    while (x < -M_PI)
        x += 2.0f * M_PI;
    return x;
}

/* ============================================================================
 * Basic functions
 * ============================================================================ */

float fabsf(float x) { return (x < 0) ? -x : x; }

float floorf(float x)
{
    int i = (int) x;
    return (float) (x < i ? i - 1 : i);
}

float ceilf(float x)
{
    int i = (int) x;
    return (float) (x > i ? i + 1 : i);
}

float fmodf(float x, float y)
{
    if (y == 0.0f)
        return NAN;
    return x - (int) (x / y) * y;
}

/* ============================================================================
 * Square root - Newton-Raphson method
 * ============================================================================ */

float sqrtf(float x)
{
    if (x < 0)
        return NAN;
    if (x == 0)
        return 0;

    /* Initial guess using bit manipulation (fast inverse square root inspired) */
    union {
        float f;
        unsigned int i;
    } conv = {.f = x};

    conv.i = 0x5f3759df - (conv.i >> 1); /* Magic number for inverse sqrt */
    float y = conv.f;

    /* Newton-Raphson iterations for 1/sqrt(x) */
    y = y * (1.5f - (0.5f * x * y * y));
    y = y * (1.5f - (0.5f * x * y * y));
    y = y * (1.5f - (0.5f * x * y * y));

    return x * y; /* sqrt(x) = x * (1/sqrt(x)) */
}

/* ============================================================================
 * Trigonometric functions - Taylor series
 * ============================================================================ */

float sinf(float x)
{
    /* Reduce to [-pi, pi] */
    x = reduce_angle(x);

    /* Taylor series: sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ... */
    float x2 = x * x;
    float result = x;
    float term = x;

    /* 7 terms for good precision */
    term *= -x2 / (2.0f * 3.0f);
    result += term;
    term *= -x2 / (4.0f * 5.0f);
    result += term;
    term *= -x2 / (6.0f * 7.0f);
    result += term;
    term *= -x2 / (8.0f * 9.0f);
    result += term;
    term *= -x2 / (10.0f * 11.0f);
    result += term;
    term *= -x2 / (12.0f * 13.0f);
    result += term;

    return result;
}

float cosf(float x)
{
    /* cos(x) = sin(x + pi/2) */
    return sinf(x + M_PI_2);
}

float tanf(float x)
{
    float c = cosf(x);
    if (fabsf(c) < 1e-10f)
        return (x > 0) ? INFINITY : -INFINITY;
    return sinf(x) / c;
}

/* ============================================================================
 * Exponential and logarithm - Taylor series
 * ============================================================================ */

float expf(float x)
{
    /* Handle edge cases */
    if (x > 88.0f)
        return INFINITY;
    if (x < -88.0f)
        return 0.0f;

    /* Reduce x to smaller range: e^x = 2^k * e^r where r is small */
    /* e^x = e^(k*ln2 + r) = 2^k * e^r */
    int k = (int) (x / M_LN2);
    float r = x - k * M_LN2;

    /* Taylor series for e^r: 1 + r + r²/2! + r³/3! + ... */
    float result = 1.0f;
    float term = 1.0f;

    for (int i = 1; i <= 12; i++)
    {
        term *= r / i;
        result += term;
    }

    /* Multiply by 2^k using bit manipulation */
    if (k != 0)
    {
        union {
            float f;
            unsigned int i;
        } conv;
        conv.f = result;
        conv.i += (unsigned int) k << 23; /* Add k to exponent */
        result = conv.f;
    }

    return result;
}

float logf(float x)
{
    if (x <= 0)
        return -INFINITY;
    if (x == 1.0f)
        return 0.0f;

    /* Extract exponent and mantissa: x = m * 2^e where 1 <= m < 2 */
    union {
        float f;
        unsigned int i;
    } conv = {.f = x};

    int e = ((conv.i >> 23) & 0xFF) - 127;
    conv.i = (conv.i & 0x007FFFFF) | 0x3F800000; /* Set exponent to 0 (m in [1,2)) */
    float m = conv.f;

    /* log(x) = log(m * 2^e) = log(m) + e*log(2) */
    /* For m close to 1, use: log(m) = log(1+y) where y = m-1 */
    /* Taylor series: log(1+y) = y - y²/2 + y³/3 - y⁴/4 + ... */
    float y = m - 1.0f;
    float result = y;
    float term = y;

    for (int i = 2; i <= 10; i++)
    {
        term *= -y;
        result += term / i;
    }

    return result + e * M_LN2;
}

float powf(float x, float y)
{
    if (x == 0.0f)
        return 0.0f;
    if (y == 0.0f)
        return 1.0f;

    /* x^y = e^(y * ln(x)) */
    return expf(y * logf(fabsf(x)));
}

/* ============================================================================
 * Hyperbolic functions
 * ============================================================================ */

float sinhf(float x)
{
    /* sinh(x) = (e^x - e^(-x)) / 2 */
    float ex = expf(x);
    return (ex - 1.0f / ex) * 0.5f;
}

float coshf(float x)
{
    /* cosh(x) = (e^x + e^(-x)) / 2 */
    float ex = expf(x);
    return (ex + 1.0f / ex) * 0.5f;
}

float tanhf(float x)
{
    /* tanh(x) = sinh(x) / cosh(x) = (e^2x - 1) / (e^2x + 1) */
    if (x > 20.0f)
        return 1.0f; /* Avoid overflow */
    if (x < -20.0f)
        return -1.0f;

    float e2x = expf(2.0f * x);
    return (e2x - 1.0f) / (e2x + 1.0f);
}

/* ============================================================================
 * Arc tangent
 * ============================================================================ */

float atan2f(float y, float x)
{
    if (x == 0.0f)
    {
        if (y > 0.0f)
            return M_PI_2;
        if (y < 0.0f)
            return -M_PI_2;
        return 0.0f;
    }

    float atan_val;
    float z = y / x;

    /* Use identity: atan(z) for |z| <= 1 */
    if (fabsf(z) <= 1.0f)
    {
        /* Taylor series: atan(z) = z - z³/3 + z⁵/5 - ... */
        float z2 = z * z;
        atan_val = z;
        float term = z;
        for (int i = 1; i <= 7; i++)
        {
            term *= -z2;
            atan_val += term / (2 * i + 1);
        }
    }
    else
    {
        /* Use identity: atan(z) = pi/2 - atan(1/z) for |z| > 1 */
        z = 1.0f / z;
        float z2 = z * z;
        atan_val = z;
        float term = z;
        for (int i = 1; i <= 7; i++)
        {
            term *= -z2;
            atan_val += term / (2 * i + 1);
        }
        atan_val = (y > 0 ? M_PI_2 : -M_PI_2) - atan_val;
    }

    /* Adjust for quadrant */
    if (x < 0.0f)
    {
        if (y >= 0.0f)
            atan_val += M_PI;
        else
            atan_val -= M_PI;
    }

    return atan_val;
}

/* ============================================================================
 * Double precision wrappers (just use float for now)
 * ============================================================================ */

double fabs(double x) { return (double) fabsf((float) x); }
double floor(double x) { return (double) floorf((float) x); }
double ceil(double x) { return (double) ceilf((float) x); }
double fmod(double x, double y) { return (double) fmodf((float) x, (float) y); }
double sqrt(double x) { return (double) sqrtf((float) x); }
double sin(double x) { return (double) sinf((float) x); }
double cos(double x) { return (double) cosf((float) x); }
double tan(double x) { return (double) tanf((float) x); }
double exp(double x) { return (double) expf((float) x); }
double log(double x) { return (double) logf((float) x); }
double pow(double x, double y) { return (double) powf((float) x, (float) y); }
double sinh(double x) { return (double) sinhf((float) x); }
double cosh(double x) { return (double) coshf((float) x); }
double tanh(double x) { return (double) tanhf((float) x); }
double atan2(double y, double x) { return (double) atan2f((float) y, (float) x); }
