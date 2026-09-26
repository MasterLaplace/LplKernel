#include <math.h>

/**
 * @brief Reduce angle to [-pi, pi] range
 */
static float reduce_angle(float x)
{
    while (x > M_PI)
        x -= 2.0f * M_PI;
    while (x < -M_PI)
        x += 2.0f * M_PI;
    return x;
}

/**
 * @brief First guess at 1/sqrt(x), from the bits of @p x.
 *
 * @details The fast inverse square root trick: halving the float's bit pattern and
 *          subtracting it from the magic number 0x5f3759df approximates the exponent and
 *          mantissa of the inverse root.
 */
static float inverse_sqrt_initial_guess(float x)
{
    union {
        float f;
        unsigned int i;
    } conv = {.f = x};

    conv.i = 0x5f3759df - (conv.i >> 1);
    return conv.f;
}

/**
 * @brief One Newton-Raphson iteration refining @p y towards 1/sqrt(x).
 */
static float refine_inverse_sqrt(float x, float y) { return y * (1.5f - (0.5f * x * y * y)); }

/**
 * @brief sin(x) by its Taylor series, x - x³/3! + x⁵/5! - x⁷/7! + ..., to seven terms.
 *
 * @details Seven terms are enough for good precision once @p x is in [-pi, pi].
 */
static float sine_taylor_series(float x)
{
    float x2 = x * x;
    float result = x;
    float term = x;

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

/**
 * @brief e^r by its Taylor series, 1 + r + r²/2! + r³/3! + ..., to twelve terms.
 *
 * @details Accurate for a small @p r, which is what the range reduction in expf leaves.
 */
static float exp_taylor_series(float r)
{
    float result = 1.0f;
    float term = 1.0f;

    for (int i = 1; i <= 12; i++)
    {
        term *= r / i;
        result += term;
    }
    return result;
}

/**
 * @brief Multiplies @p value by 2^k by adding @p k to its exponent field.
 */
static float scale_by_power_of_two(float value, int k)
{
    union {
        float f;
        unsigned int i;
    } conv;
    conv.f = value;
    conv.i += (unsigned int) k << 23;
    return conv.f;
}

/**
 * @brief Splits @p x into m * 2^e, with the mantissa m in [1, 2).
 *
 * @details The mantissa is read by forcing the float's exponent field to zero.
 *
 * @param x            A positive float.
 * @param out_mantissa Receives m.
 * @return e.
 */
static int split_exponent_and_mantissa(float x, float *out_mantissa)
{
    union {
        float f;
        unsigned int i;
    } conv = {.f = x};

    int e = ((conv.i >> 23) & 0xFF) - 127;
    conv.i = (conv.i & 0x007FFFFF) | 0x3F800000;
    *out_mantissa = conv.f;
    return e;
}

/**
 * @brief log(1 + y) by its Taylor series, y - y²/2 + y³/3 - y⁴/4 + ..., to ten terms.
 *
 * @details Accurate for y close to 0, which is what taking the mantissa out of a float
 *          leaves: log(m) = log(1 + y) with y = m - 1.
 */
static float log1p_taylor_series(float y)
{
    float result = y;
    float term = y;

    for (int i = 2; i <= 10; i++)
    {
        term *= -y;
        result += term / i;
    }
    return result;
}

/**
 * @brief atan(z) by its Taylor series, z - z³/3 + z⁵/5 - ..., to eight terms.
 *
 * @details Converges for |z| <= 1 only; atan2f folds the other half of the range onto it
 *          with atan(z) = pi/2 - atan(1/z).
 */
static float atan_taylor_series(float z)
{
    float z2 = z * z;
    float atan_val = z;
    float term = z;
    for (int i = 1; i <= 7; i++)
    {
        term *= -z2;
        atan_val += term / (2 * i + 1);
    }
    return atan_val;
}

/**
 * @brief Moves an arc tangent of y/x into the quadrant @p x and @p y actually sit in.
 */
static float adjust_for_quadrant(float atan_val, float y, float x)
{
    if (x >= 0.0f)
        return atan_val;
    return (y >= 0.0f) ? atan_val + M_PI : atan_val - M_PI;
}

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

float sqrtf(float x)
{
    if (x < 0)
        return NAN;
    if (x == 0)
        return 0;

    float inverse = inverse_sqrt_initial_guess(x);
    inverse = refine_inverse_sqrt(x, inverse);
    inverse = refine_inverse_sqrt(x, inverse);
    inverse = refine_inverse_sqrt(x, inverse);

    return x * inverse;
}

float sinf(float x) { return sine_taylor_series(reduce_angle(x)); }

float cosf(float x) { return sinf(x + M_PI_2); }

float tanf(float x)
{
    float c = cosf(x);
    if (fabsf(c) < 1e-10f)
        return (x > 0) ? INFINITY : -INFINITY;
    return sinf(x) / c;
}

float expf(float x)
{
    if (x > 88.0f)
        return INFINITY;
    if (x < -88.0f)
        return 0.0f;

    int k = (int) (x / M_LN2);
    float r = x - k * M_LN2;

    float result = exp_taylor_series(r);
    if (k != 0)
        result = scale_by_power_of_two(result, k);
    return result;
}

float logf(float x)
{
    if (x <= 0)
        return -INFINITY;
    if (x == 1.0f)
        return 0.0f;

    float m = 0.0f;
    int e = split_exponent_and_mantissa(x, &m);
    return log1p_taylor_series(m - 1.0f) + e * M_LN2;
}

float powf(float x, float y)
{
    if (x == 0.0f)
        return 0.0f;
    if (y == 0.0f)
        return 1.0f;

    return expf(y * logf(fabsf(x)));
}

float sinhf(float x)
{
    float ex = expf(x);
    return (ex - 1.0f / ex) * 0.5f;
}

float coshf(float x)
{
    float ex = expf(x);
    return (ex + 1.0f / ex) * 0.5f;
}

float tanhf(float x)
{
    if (x > 20.0f)
        return 1.0f;
    if (x < -20.0f)
        return -1.0f;

    float e2x = expf(2.0f * x);
    return (e2x - 1.0f) / (e2x + 1.0f);
}

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

    float z = y / x;
    float atan_val;
    if (fabsf(z) <= 1.0f)
        atan_val = atan_taylor_series(z);
    else
        atan_val = (y > 0 ? M_PI_2 : -M_PI_2) - atan_taylor_series(1.0f / z);

    return adjust_for_quadrant(atan_val, y, x);
}

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
