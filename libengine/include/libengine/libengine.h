/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** libengine — C-callable facade for the freestanding LplPlugin engine module.
**
** libengine.a is the LplPlugin engine compiled -ffreestanding into the kernel
** image (the C++ sibling of libk.a). This header is the ONLY surface the pure-C
** kernel includes; it must stay free of C++ and of any lpl/ engine type.
*/
#ifndef _LIBENGINE_H
#define _LIBENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** P0 determinism smoke. Computes a handful of Fixed32 (Q16.16) and CORDIC
** results whose raw bit patterns are fixed by the math sources. The kernel
** prints these on serial; the xmake/host oracle computes the SAME values, so a
** byte-for-byte match proves the freestanding engine build is bit-identical to
** the Linux build (the HARD determinism contract, P0 exit gate).
*/
typedef struct {
    int32_t cordic_sin_quarter_pi_raw; /* sin(pi/4) in Q16.16 */
    int32_t cordic_cos_quarter_pi_raw; /* cos(pi/4) in Q16.16 */
    int32_t cordic_atan2_one_one_raw;  /* atan2(1, 1) in Q16.16 (~pi/4) */
    int32_t fixed_mul_three_half_raw;  /* (3.0 * 0.5) in Q16.16 = 1.5    */
    int32_t fixed_div_one_three_raw;   /* (1.0 / 3.0) in Q16.16          */
} libengine_p0_smoke_result_t;

extern void libengine_p0_smoke(libengine_p0_smoke_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _LIBENGINE_H */
