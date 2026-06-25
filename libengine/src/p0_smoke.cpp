/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** P0 determinism smoke — computes fixed-point/CORDIC results whose raw bit
** patterns are emitted on the kernel serial port and compared, byte-for-byte,
** against the Linux/xmake oracle. This is the freestanding-build entry point
** that proves the engine math is bit-identical across the two targets.
*/
#include "libengine/libengine.h"

#include <lpl/math/Cordic.hpp>
#include <lpl/math/FixedPoint.hpp>

extern "C" void libengine_p0_smoke(libengine_p0_smoke_result_t *out)
{
    using lpl::math::Cordic;
    using lpl::math::Fixed32;

    if (out == nullptr)
        return;

    // angle = pi / 4
    const Fixed32 quarterPi = Fixed32::pi() / Fixed32::fromInt(4);

    Fixed32 s, c;
    Cordic::sincos(quarterPi, s, c);

    const Fixed32 atan2OneOne = Cordic::atan2(Fixed32::fromInt(1), Fixed32::fromInt(1));

    const Fixed32 mul = Fixed32::fromInt(3) * Fixed32::half();           // 3.0 * 0.5 = 1.5
    const Fixed32 div = Fixed32::fromInt(1) / Fixed32::fromInt(3);       // 1.0 / 3.0

    out->cordic_sin_quarter_pi_raw = s.raw();
    out->cordic_cos_quarter_pi_raw = c.raw();
    out->cordic_atan2_one_one_raw = atan2OneOne.raw();
    out->fixed_mul_three_half_raw = mul.raw();
    out->fixed_div_one_three_raw = div.raw();
}
