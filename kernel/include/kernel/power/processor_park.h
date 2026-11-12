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
 * @file processor_park.h
 * @brief Where an application processor with nothing to run waits: the deepest depth its own ladder gives.
 *
 * A package sleeps deep only when every one of its threads is in its deepest state, so an application processor left
 * in HLT holds the whole package in C1, however deep the bootstrap processor sleeps. A parked processor builds its own
 * ladder from its own CPUID and CPU slot and enters its deepest depth: an MWAIT hint as it is; a port depth once the
 * bootstrap processor's probe found that port's read stops a processor, and only while its own guard has not seen the
 * read return at once.
 *
 * The guard judges each port read by what the processor can see when the read returns, interrupts still masked: how
 * long it took, and whether an interrupt is requested in the local APIC. A read that slept until an interrupt finds
 * that interrupt requested; one that the hardware ended for another reason, which ACPI allows, slept first; one that
 * does not stop the processor returns at once with nothing requested.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-12
 **************************************************************************/

#ifndef KERNEL_POWER_PROCESSOR_PARK_H
#define KERNEL_POWER_PROCESSOR_PARK_H

#include <kernel/power/processor_sleep.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A port read that returns sooner than this with no interrupt requested did not stop the processor: 36 times the
 * 7.1 us a read of a port nothing decodes takes under KVM, and a read that slept lasts until an interrupt.
 */
#define KERNEL_PROCESSOR_PARK_QUICK_MICROSECONDS 256u

#define KERNEL_PROCESSOR_PARK_QUICK_LIMIT 8u /**< Quick reads in a row that refuse a port depth. */

/**
 * Reads in a row ended by an interrupt still requested once interrupts are enabled again that refuse a port depth: an
 * interrupt that is never taken makes every read look ended by it, and the read would spin.
 */
#define KERNEL_PROCESSOR_PARK_STUCK_LIMIT 64u

/**
 * @brief The guard's streaks over the reads of one port depth.
 */
typedef struct ProcessorParkJudge {
    uint32_t quick; /**< Reads in a row that returned quick, no interrupt requested. */
    uint32_t stuck; /**< Reads in a row whose interrupt was still requested once interrupts were enabled. */
} ProcessorParkJudge_t;

/**
 * @brief What one parked processor did, each field written by that processor alone and a 32-bit word, never torn.
 */
typedef struct ProcessorParkRecord {
    volatile uint32_t turns;      /**< Sleeps its park entered; 0 for a processor that never parked. */
    volatile uint32_t depth;      /**< The depth of its last sleep. */
    volatile uint32_t entry;      /**< That depth's ProcessorSleepEntry_t. */
    volatile uint32_t target;     /**< That depth's port, or its MWAIT hint. */
    volatile uint32_t inherited;  /**< Its port depths a validated verdict of the bootstrap processor names. */
    volatile uint32_t refused;    /**< Its port depths its guard refused. */
    volatile uint32_t port_reads; /**< The port reads it made. */
    volatile uint32_t quick;      /**< Of them, those that returned quick with no interrupt requested. */
    volatile uint32_t stuck; /**< Of them, those whose interrupt was still requested once interrupts were enabled. */
} ProcessorParkRecord_t;

/**
 * @brief Judges one read of a port depth's port, and says whether it refuses the depth.
 *
 * @param judge           The streaks of this depth, updated.
 * @param requested       Whether an interrupt was requested when the read returned, interrupts masked.
 * @param still_requested Whether one was still requested once interrupts were enabled again; read only when
 *                        @p requested.
 * @param cycles          The read's length, in TSC cycles.
 * @param quick_cycles    The length under which a read with no interrupt requested did not stop the processor.
 * @return true when this read makes KERNEL_PROCESSOR_PARK_QUICK_LIMIT quick reads, or KERNEL_PROCESSOR_PARK_STUCK_LIMIT
 *         stuck ones, in a row.
 */
extern bool kernel_processor_park_judge(ProcessorParkJudge_t *judge, bool requested, bool still_requested,
                                        uint64_t cycles, uint64_t quick_cycles);

/**
 * @brief The port depths of @p ladder whose port a validated verdict of @p verdicts names and no refused one does.
 *
 * @param ladder   The parked processor's ladder.
 * @param verdicts The bootstrap processor's verdicts (@ref kernel_processor_sleep_port_verdicts).
 * @param count    How many words @p verdicts holds.
 * @return A mask of depths.
 */
extern uint32_t kernel_processor_park_inherited_depths(const ProcessorSleepLadder_t *ladder, const uint32_t *verdicts,
                                                       uint32_t count);

/**
 * @brief The depth a parked processor sleeps at: the deepest available one of @p ladder it may enter.
 *
 * @param ladder      Its ladder.
 * @param inherited   Its port depths the bootstrap processor validated.
 * @param refused     Its port depths its guard refused.
 * @param clock_known Whether the guard can time a read; without it, no port depth.
 * @return A depth of @p ladder that is not a port depth, or one inherited and not refused while the clock is known;
 *         PROCESSOR_SLEEP_DEPTH_C1 when there is no other.
 */
extern uint32_t kernel_processor_park_target(const ProcessorSleepLadder_t *ladder, uint32_t inherited, uint32_t refused,
                                             bool clock_known);

/**
 * @brief Parks the calling application processor for good: builds its ladder, then sleeps at its target, taking each
 *        interrupt that wakes it, and looks at its target again after each one.
 *
 * @details Called with interrupts masked, from the processor's start-up, on its bootstrap stack. Records what it does
 *          in its slot's ProcessorParkRecord_t.
 */
extern void kernel_processor_park(void) __attribute__((noreturn));

/**
 * @brief Tells every parked processor to look at its target again, after a verdict of the bootstrap processor's probe
 *        changed.
 *
 * @details Called on the bootstrap processor, by the probe once it judged a port depth (kernel/power/tickless.h).
 *          Publishes the cycles of KERNEL_PROCESSOR_PARK_QUICK_MICROSECONDS at the calibrated rate, which a probe
 *          needs too, then sends every other processor an interrupt on a vector of its own, allocated by the first
 *          call; a processor that is not parked takes it and goes on.
 */
extern void kernel_processor_park_reconsider(void);

/**
 * @brief The vector @ref kernel_processor_park_reconsider sends.
 * @return The vector, or 0 before its first call, or when no vector was free.
 */
extern uint8_t kernel_processor_park_reconsider_vector(void);

/**
 * @brief The cycles under which the guard calls a read quick.
 * @return Those cycles, or 0 while the TSC rate is unknown, in which case no processor parks at a port depth.
 */
extern uint32_t kernel_processor_park_quick_cycles(void);

/**
 * @brief Copies what the processor of @p slot did in its park.
 *
 * @param slot       The processor's logical slot.
 * @param out_record Receives the copy.
 * @return false for a slot past CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC, @p out_record untouched.
 */
extern bool kernel_processor_park_record(uint32_t slot, ProcessorParkRecord_t *out_record);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_PROCESSOR_PARK_H */
