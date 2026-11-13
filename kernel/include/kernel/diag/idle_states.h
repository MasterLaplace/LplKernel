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
 *        allows, what firmware's _CST says each costs, and the depths its sleeps may take.
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
 *        the _CST of its CPU slot targets, in increasing order, then the `[LPLTLM] sleep.depths` record of the ladder
 *        its facts build (kernel_processor_sleep_build_ladder) and one `[LPLTLM] sleep.depth` record per depth.
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
 *          Keys of `sleep.depths`:
 *          - `available`: a bit per depth, depth 0 being C1;
 *          - `ports`: whether the slot's port states are the depths past C1, `taken`, or why not: `monitor_wait`
 *            (MWAIT reaches past C1, and its hints are the depths), `no_slot`, `timer_may_stop`, `counter_may_stop` or
 *            `not_handed_over`;
 *          - `ports_refused`: the port states of type 2 or 3 the slot lists that are no depth, when they are taken.
 *
 *          Keys of `sleep.depth`:
 *          - `depth`;
 *          - `entry`: `halt`, `mwait` or `port`;
 *          - `target`: the MWAIT hint, the port read, or 0 for HLT;
 *          - `latency_us`: the latency _CST publishes for it, `none` where it publishes none.
 *
 *          Then a `park` record per online application processor, what its park did so far
 *          (kernel/power/processor_park.h):
 *          - `slot`; `depth`, `entry` and `target` of its last sleep, as in `sleep.depth`;
 *          - `inherited`: its port depths a validated verdict of this processor's probe names;
 *          - `refused`: those its guard refused;
 *          - `turns`: the sleeps its park entered; `port_reads`, and of them `quick` (returned quick, no interrupt
 *            requested) and `stuck` (an interrupt still requested once interrupts were enabled).
 *
 *          Called once the local timer is set up, since `usable` and the ladder read whether it always runs.
 *
 * @param serial Output port.
 */
void kernel_idle_states_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DIAG_IDLE_STATES_H */
