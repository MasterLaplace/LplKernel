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
 * @file extended_state.h
 * @brief The x87, SSE and AVX state every processor starts from, and the extended state components XSAVE covers.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-08
 **************************************************************************/

#ifndef KERNEL_CPU_EXTENDED_STATE_H
#define KERNEL_CPU_EXTENDED_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Puts the processor that runs the call in the known x87, SSE and AVX state.
 *
 * @details It sets CR0.NE and CR0.MP and clears CR0.EM and CR0.TS, so x87 errors raise #MF rather than go through
 *          FERR# (Intel SDM vol. 3, 325384-093US, §2.5). It sets CR4.OSFXSR and CR4.OSXMMEXCPT, and CR4.OSXSAVE when
 *          CPUID.1:ECX.XSAVE says the processor has XSAVE; XCR0 then enables, of the components CPUID leaf 0xD offers,
 *          x87, SSE, AVX and the three AVX-512 ones, each only with those XSETBV requires it with (SDM vol. 1, §13.3).
 *          FNINIT and LDMXCSR then give the x87 control word 0x037F and the MXCSR 0x1F80. The bootstrap processor
 *          calls it before any constructor, each application processor first thing in its C entry: an INIT leaves the
 *          x87 and SSE state as it was (SDM vol. 3, Table 12-1).
 */
void extended_state_initialize_on_this_processor(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CPU_EXTENDED_STATE_H */
