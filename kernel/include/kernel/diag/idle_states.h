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
 * @file idle_states.h
 * @brief The C-states the boot processor may enter, as records: which MWAIT hints it enumerates and its local timer
 *        allows, and what firmware's _CST says each costs.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-11
 **************************************************************************/

#ifndef KERNEL_DIAG_IDLE_STATES_H
#define KERNEL_DIAG_IDLE_STATES_H

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes the `[LPLTLM] cstates` record of the boot processor, then one `[LPLTLM] cstate` record per MWAIT hint
 *        the _CST of its CPU slot targets, in increasing order.
 *
 * @details Keys of `cstates`, each mask a bit per hint, bit h for hint h, which targets C(h+1):
 *          - `slot`: the processor's CPU slot in the MADT, `none` when the local APIC does not run or no slot has its
 *            ID;
 *          - `monitor`: 1 when the processor implements MONITOR and MWAIT, so that an `enumerated` of 0 tells a pair
 *            that is absent from a leaf 5 that lists no hint, as AMD processors have left it (Linux commit
 *            5209654a46ee);
 *          - `enumerated`: the hints CPUID leaf 5 lists where the pair is implemented, 0 otherwise;
 *          - `usable`: those a sleep may take, every one where the local timer always runs and C1's alone where it may
 *            stop;
 *          - `published`: the hints the slot's _CST states target.
 *
 *          Keys of `cstate`:
 *          - `hint`;
 *          - `latency_us`: the worst-case latency to enter and leave the state, the longest its states publish;
 *          - `residency_us`: how long a sleep must last for the state to save energy;
 *          - `residency_source`: where that residency comes from: `rule`, derived from the latency as Linux's
 *            intel_idle derives it from _CST.
 *
 *          Called once the local timer is set up, since `usable` reads whether it always runs.
 *
 * @param serial Output port.
 */
void kernel_idle_states_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DIAG_IDLE_STATES_H */
