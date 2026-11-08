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

/**
 * @name How a context's extended state area is written and read back
 * @{
 */
#define EXTENDED_STATE_SAVE_FXSAVE                                                                                     \
    0 /**< FXSAVE and FXRSTOR: x87 and SSE in 512 bytes, on a processor without XSAVE.                                 \
       */
#define EXTENDED_STATE_SAVE_XSAVE                                                                                      \
    1 /**< XSAVE and XRSTOR, in the standard format: every enabled component, written.                                 \
       */
#define EXTENDED_STATE_SAVE_XSAVEOPT                                                                                   \
    2 /**< XSAVEOPT and XRSTOR: a component in its initial state, or unmodified since the area was restored, is not    \
           written (Intel SDM vol. 1, 253665-093US, §13.9). */
#define EXTENDED_STATE_SAVE_XSAVES                                                                                     \
    3 /**< XSAVES and XRSTORS, in the compacted format, with both optimisations of XSAVEOPT (SDM vol. 1, §13.11). */
/** @} */

#define EXTENDED_STATE_AREA_ALIGNMENT 64 /**< The alignment XSAVE and XRSTOR need, which covers FXSAVE's 16. */

#ifndef __ASSEMBLER__

#    include <stdbool.h>
#    include <stdint.h>

#    ifdef __cplusplus
extern "C" {
#    endif

typedef uint32_t ExtendedStateSave_t; /**< One of the EXTENDED_STATE_SAVE_ values. */

/**
 * @brief Puts the processor that runs the call in the known x87, SSE and AVX state.
 *
 * @details It sets CR0.NE and CR0.MP and clears CR0.EM and CR0.TS, so x87 errors raise #MF rather than go through
 *          FERR# (Intel SDM vol. 3, 325384-093US, §2.5). It sets CR4.OSFXSR and CR4.OSXMMEXCPT, and CR4.OSXSAVE when
 *          CPUID.1:ECX.XSAVE says the processor has XSAVE; XCR0 then enables, of the components CPUID leaf 0xD offers,
 *          x87, SSE, AVX and the three AVX-512 ones, each only with those XSETBV requires it with
 *          (extended_state_components_to_enable).
 *          FNINIT and LDMXCSR then give the x87 control word 0x037F and the MXCSR 0x1F80. The bootstrap processor
 *          calls it before any constructor, each application processor first thing in its C entry: an INIT leaves the
 *          x87 and SSE state as it was (SDM vol. 3, Table 12-1).
 */
void extended_state_initialize_on_this_processor(void);

/**
 * @brief The components XCR0 enables among the @p offered ones that CPUID leaf 0xD reports.
 *
 * @details It keeps x87, SSE, AVX and the three AVX-512 components, opmask, ZMM_Hi256 and Hi16_ZMM, each only with the
 *          ones XSETBV requires it with: bit 0 always, AVX only with SSE, and the AVX-512 three all together, with SSE
 *          and AVX (Intel SDM vol. 1, 253665-093US, §13.3). MPX, PKRU, AMX and every supervisor component stay off.
 *
 * @param offered CPUID.(EAX=0DH,ECX=0):EAX, the components the processor supports.
 * @return The value XCR0 takes.
 */
uint32_t extended_state_components_to_enable(uint32_t offered);

/**
 * @brief The strongest save the processor offers: XSAVES, then XSAVEOPT, then XSAVE, then FXSAVE.
 *
 * @details CPUID leaf 0xD, sub-leaf 1, says which of XSAVEOPT and XSAVES the processor has; each processor's
 *          extended_state_initialize_on_this_processor records it, and clears IA32_XSS on itself. The order follows the
 *          SDM, not a clock: XSAVES writes the compacted format and has the init and the modified optimisations,
 *          XSAVEOPT has both optimisations, XSAVE neither (Intel SDM vol. 1, 253665-093US, §13.7, §13.9 and §13.11).
 *          XSAVEC is left out: XSAVES has its compaction and adds the modified optimisation.
 *
 * @return One of the EXTENDED_STATE_SAVE_ values.
 */
ExtendedStateSave_t extended_state_preferred_save(void);

/**
 * @brief Whether the processor offers @p save.
 *
 * @param save One of the EXTENDED_STATE_SAVE_ values.
 * @return true when it does. FXSAVE is offered only while XCR0 enables nothing past SSE: it would lose the rest.
 */
bool extended_state_offers_save(ExtendedStateSave_t save);

/**
 * @brief The bytes an area saved with @p save takes, for the components XCR0 enables.
 *
 * @details 512 for FXSAVE; CPUID leaf 0xD gives the others, sub-leaf 0 for the standard format and sub-leaf 1 for the
 *          compacted one of XSAVES.
 *
 * @param save One of the EXTENDED_STATE_SAVE_ values the processor offers.
 * @return The size, a multiple of 64.
 */
uint32_t extended_state_area_size(ExtendedStateSave_t save);

/**
 * @brief Writes into @p area the initial state a context starts from: every component in its initial configuration,
 *        the x87 control word 0x037F and the MXCSR 0x1F80.
 *
 * @details The header's XSTATE_BV is 0, so XRSTOR and XRSTORS initialise every component; the standard XRSTOR still
 *          loads the MXCSR from offset 24, and FXRSTOR reads the whole legacy region, both of which hold the initial
 *          values (Intel SDM vol. 1, 253665-093US, §13.8.1 and §13.8.2). XCOMP_BV names the compacted format and the
 *          enabled components for XRSTORS.
 *
 * @param area An area of extended_state_area_size(@p save) bytes, EXTENDED_STATE_AREA_ALIGNMENT-aligned.
 * @param save How the area will be saved and restored.
 */
void extended_state_prepare_area(void *area, ExtendedStateSave_t save);

#    ifdef __cplusplus
}
#    endif

#endif /* __ASSEMBLER__ */

#endif /* KERNEL_CPU_EXTENDED_STATE_H */
