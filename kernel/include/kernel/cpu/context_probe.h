/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file context_probe.h
 * @brief Routines that hold known values in the x87, SSE and AVX registers across a context switch, for the tests.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-08
 **************************************************************************/

#ifndef KERNEL_CPU_CONTEXT_PROBE_H
#define KERNEL_CPU_CONTEXT_PROBE_H

#include <kernel/cpu/context.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTEXT_PROBE_VECTOR_BYTES 256u /**< YMM0-YMM7, 32 bytes each; XMM0-XMM7 take the first 16 of each. */

/**
 * @brief The registers a probe loads or stores: no C code may hold them across a call.
 */
typedef struct {
    uint16_t control_word;                       /**< The x87 control word. */
    uint16_t reserved;                           /**< Keeps the MXCSR on a 4-byte boundary. */
    uint32_t simd_control_status;                /**< The MXCSR. */
    uint8_t vectors[CONTEXT_PROBE_VECTOR_BYTES]; /**< YMM0-YMM7, or XMM0-XMM7 at the start of each 32 bytes. */
} ContextProbeState_t;

/**
 * @brief Loads @p held into the registers, switches from @p from to @p to, and once switched back stores the registers
 *        into @p after.
 *
 * @param held  What the registers hold before the switch.
 * @param after Receives what they hold once the context runs again.
 * @param from  The running context.
 * @param to    The context it switches to.
 * @param wide  Non-zero to hold YMM0-YMM7, which needs AVX; zero for XMM0-XMM7.
 */
void context_probe_hold_across_switch(const ContextProbeState_t *held, ContextProbeState_t *after, CpuContext_t *from,
                                      const CpuContext_t *to, uint32_t wide);

/**
 * @brief Loads @p state into the registers, then switches from @p from to @p to.
 *
 * @param state What the registers hold when the switch saves them.
 * @param from  The running context.
 * @param to    The context it switches to.
 * @param wide  Non-zero to load YMM0-YMM7, which needs AVX; zero for XMM0-XMM7.
 */
void context_probe_load_and_switch(const ContextProbeState_t *state, CpuContext_t *from, const CpuContext_t *to,
                                   uint32_t wide);

/**
 * @brief Stores the registers into @p state.
 *
 * @param state Receives what they hold.
 * @param wide  Non-zero to store YMM0-YMM7, which needs AVX; zero for XMM0-XMM7.
 */
void context_probe_read(ContextProbeState_t *state, uint32_t wide);

/**
 * @brief Loads @p state into the registers.
 *
 * @param state What the registers hold after the call.
 * @param wide  Non-zero to load YMM0-YMM7, which needs AVX; zero for XMM0-XMM7.
 */
void context_probe_load(const ContextProbeState_t *state, uint32_t wide);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CPU_CONTEXT_PROBE_H */
