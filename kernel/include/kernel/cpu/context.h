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
 * @file context.h
 * @brief A ring-0 execution context, its stack and its extended state, and the switch from one to another.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-08
 **************************************************************************/

#ifndef KERNEL_CPU_CONTEXT_H
#define KERNEL_CPU_CONTEXT_H

#include <kernel/cpu/extended_state.h>

/**
 * @name Where context_switch.S finds a context's fields
 * @{
 */
#define CPU_CONTEXT_OFFSET_STACK_POINTER  0 /**< CpuContext_t's stack_pointer. */
#define CPU_CONTEXT_OFFSET_EXTENDED_STATE 4 /**< Its extended_state. */
#define CPU_CONTEXT_OFFSET_SAVE           8 /**< Its save. */
/** @} */

#ifndef __ASSEMBLER__

#    ifdef __cplusplus
extern "C" {
#    endif

/**
 * @brief A ring-0 execution context: where its stack stood when it was switched away from, and its extended state.
 */
typedef struct {
    uint32_t stack_pointer;   /**< The stack pointer the next switch to it resumes from. */
    void *extended_state;     /**< Its extended state area, EXTENDED_STATE_AREA_ALIGNMENT-aligned. */
    ExtendedStateSave_t save; /**< How that area is saved and restored. */
} CpuContext_t;

/**
 * @brief What a prepared context runs first; it never returns.
 *
 * @param argument What cpu_context_prepare was given.
 */
typedef void (*CpuContextEntry_t)(void *argument);

/**
 * @brief The first code a prepared context runs, from the frame cpu_context_prepare writes: it calls the entry with
 *        its argument, and halts the processor if the entry returns. Never called.
 */
void cpu_context_start(void);

/**
 * @brief Makes the code that runs the call @p context: its next cpu_context_switch away saves it there.
 *
 * @details @p extended_state must not be the area the processor last restored from: XSAVEOPT and XSAVES skip a
 *          component unmodified since that restore, and would keep the initial state this call writes (Intel SDM
 *          vol. 1, §13.6).
 *
 * @param context        The context.
 * @param extended_state An area of extended_state_area_size(@p save) bytes, EXTENDED_STATE_AREA_ALIGNMENT-aligned.
 * @param save           How the area is saved and restored, one the processor offers.
 */
void cpu_context_adopt(CpuContext_t *context, void *extended_state, ExtendedStateSave_t save);

/**
 * @brief Prepares @p context so that the first switch to it runs @p entry with @p argument, on the stack that ends at
 *        @p stack_top, from the initial extended state.
 *
 * @details @p entry is called with the stack 16-byte aligned, as the System V i386 ABI asks. An entry that returns
 *          leaves its processor halted with interrupts disabled.
 *
 * @param context        The context.
 * @param stack_top      One past the stack's highest byte.
 * @param extended_state An area of extended_state_area_size(@p save) bytes, EXTENDED_STATE_AREA_ALIGNMENT-aligned.
 * @param save           How the area is saved and restored, one the processor offers.
 * @param entry          What the context runs.
 * @param argument       What @p entry receives.
 */
void cpu_context_prepare(CpuContext_t *context, void *stack_top, void *extended_state, ExtendedStateSave_t save,
                         CpuContextEntry_t entry, void *argument);

/**
 * @brief Saves the running code into @p from and resumes @p to where it was switched away from, or at its entry.
 *
 * @details It saves EBP, EBX, ESI, EDI and the stack pointer, the registers the System V i386 ABI asks a callee to
 *          keep, then the extended state with @p from's save, and restores @p to's with its own. The flags are not
 *          saved: the caller disables interrupts, and @p to resumes with them disabled. Switching to @p from itself
 *          does nothing.
 *
 * @param from Where the running code is saved.
 * @param to   What runs next, on the same processor.
 */
void cpu_context_switch(CpuContext_t *from, const CpuContext_t *to);

#    ifdef __cplusplus
}
#    endif

#endif /* __ASSEMBLER__ */

#endif /* KERNEL_CPU_CONTEXT_H */
