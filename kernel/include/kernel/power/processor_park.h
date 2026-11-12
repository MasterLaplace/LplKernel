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
 * read return at once. A refusal holds while the verdict that let the depth in does.
 *
 * The guard judges each port read by what the processor can see when the read returns, interrupts still masked: how
 * long it took, whether an interrupt is requested in the local APIC, and, after a C3, whether a bus master requested
 * the bus. A read that slept until an interrupt finds that interrupt requested; one that a bus master ended, which
 * ACPI requires of C3 once BM_RLD is set, finds BM_STS set; one that the hardware ended for another reason, which ACPI
 * allows, slept first; one that does not stop the processor returns at once with nothing requested.
 *
 * A C3 is entered behind the bus-master check, as the bootstrap processor's sleeps are: where a bus master was active
 * since the last check, the park enters the deepest depth below it it may, without the check.
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
 * A port read that returns sooner than this with no interrupt requested did not stop the processor; a read that slept
 * lasts until an interrupt. A read of a port nothing decodes takes 5.7 to 6.5 us on average under KVM, the longest of
 * 256 reads 15 to 82 us, and 0.1 us under TCG (`read_ns_mean` and `read_ns_longest` of processor_sleep.the_timed_read_
 * reads_its_port_between_two_fenced_counter_reads, five boots): a VM exit, now and then a host that schedules late,
 * which only delays a refusal.
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
 * @brief What a parked processor saw when one of its port reads returned, interrupts masked.
 */
typedef struct ProcessorParkRead {
    uint64_t cycles;      /**< The read's length, in TSC cycles. */
    bool requested;       /**< Whether an interrupt was requested in the local APIC. */
    bool still_requested; /**< Whether one was still once interrupts were enabled again; read only when requested. */
    bool bus_mastered;    /**< Whether BM_STS was set after a read of a depth with the bus-master check. */
} ProcessorParkRead_t;

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
    volatile uint32_t bus_mastered; /**< Of them, those after which BM_STS was set. */
    volatile uint32_t demotions;    /**< Turns the bus-master check demoted from a C3. */
} ProcessorParkRecord_t;

/**
 * @brief Judges one read of a port depth's port, and says whether it refuses the depth.
 *
 * @details A read is quick when it returned sooner than @p quick_cycles with no interrupt requested and no bus master
 *          seen; stuck when its interrupt was still requested once interrupts were enabled.
 *
 * @param judge        The streaks of this depth, updated.
 * @param read         What the processor saw when the read returned.
 * @param quick_cycles The length under which a read with nothing requested did not stop the processor.
 * @return true when this read makes KERNEL_PROCESSOR_PARK_QUICK_LIMIT quick reads, or KERNEL_PROCESSOR_PARK_STUCK_LIMIT
 *         stuck ones, in a row; both streaks then start over.
 */
extern bool kernel_processor_park_judge(ProcessorParkJudge_t *judge, const ProcessorParkRead_t *read,
                                        uint64_t quick_cycles);

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
 * @brief The depths of @p ladder a parked processor may enter: every available one that is not a port depth, and each
 *        port depth inherited and not refused while the clock is known.
 *
 * @param ladder      Its ladder.
 * @param inherited   Its port depths the bootstrap processor validated.
 * @param refused     Its port depths its guard refused.
 * @param clock_known Whether the guard can time a read; without it, no port depth.
 * @return A mask of depths, C1's always set.
 */
extern uint32_t kernel_processor_park_allowed_depths(const ProcessorSleepLadder_t *ladder, uint32_t inherited,
                                                     uint32_t refused, bool clock_known);

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
 *          needs too, then sends an interrupt on a vector of its own, allocated by the first call, to each other
 *          processor online, one by one: one that never started, whose local APIC may be in any state, is sent none,
 *          as Linux sends no broadcast until every present processor started once (v6.12 arch/x86/kernel/apic/ipi.c,
 *          `apic_smt_update`). A processor that is not parked takes it and goes on.
 */
extern void kernel_processor_park_reconsider(void);

/**
 * @brief The vector @ref kernel_processor_park_reconsider sends.
 * @return The vector, or 0 before its first call, or when no vector was free.
 */
extern uint8_t kernel_processor_park_reconsider_vector(void);

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
