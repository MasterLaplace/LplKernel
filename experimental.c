#include <stdio.h>
#include <stdint.h>

#define IS_NEGATIVE(value) ((value) & 0x80000000)

typedef union {
    float f;
    uint32_t i;
} float_cast_t;

/**
 * @brief
 * $|x| = -x$
 * @param value
 * @return float
 */
float my_fabsf(float value)
{
    float_cast_t converter = {.f = value};

    converter.i &= 0x7FFFFFFF;
    return converter.f;
}

/**
 * @brief
 * $\lfloor x \rfloor$
 * @param value
 * @return float
 */
float my_floorf(float value)
{
    float_cast_t converter = {.f = value};

    int exposant = ((converter.i >> 23) & 0xFF) - 127;

    if (exposant < 0)
        return IS_NEGATIVE(converter.i) ? -1.0f : 0.0f;
    else if (exposant >= 23)
        return value;

    int nb_bits_to_delete = 23 - exposant;
    unsigned mask = 0xFFFFFFFF << nb_bits_to_delete;

    if (IS_NEGATIVE(converter.i) && (converter.i & ~mask) != 0)
    {
        converter.i &= mask;
        return converter.f - 1.0f;
    }

    converter.i &= mask;
    return converter.f;
}

/**
 * @brief
 * $\lceil x \rceil$
 * @param value
 * @return float
 */
float my_ceilf(float value)
{
    float_cast_t converter = {.f = value};

    if ((converter.i & 0x7FFFFFFF) == 0)
        return value;

    int exposant = ((converter.i >> 23) & 0xFF) - 127;

    if (exposant < 0)
        return IS_NEGATIVE(converter.i) ? -0.0f : 1.0f;
    else if (exposant >= 23)
        return value;

    int nb_bits_to_delete = 23 - exposant;
    unsigned mask = 0xFFFFFFFF << nb_bits_to_delete;

    if (!IS_NEGATIVE(converter.i) && (converter.i & ~mask) != 0)
    {
        converter.i &= mask;
        return converter.f + 1.0f;
    }

    converter.i &= mask;
    return converter.f;
}

float my_modf(float value, float modul)
{
    return value - (trunc(value / modul) * modul);
}

int main(void)
{
    printf("abs : %f | floor :  %f | ceil : %f\n", my_fabsf(-5.f), my_floorf(-5.5f), my_ceilf(-5.5f));
    // abs : 5.000000 | floor :  -6.000000 | ceil : -5.000000
    return 0;
}
