/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file processor_sleep.h
 * @brief Sleeping between buffers instead of spinning.
 *
 * A satellite is silent most of its life. `HLT` alone wakes late; the monitor/wait pair
 * lets the processor sleep and be woken by a write to a watched address — which is
 * precisely what a device pushing an audio buffer does. This is the difference between
 * a node that idles at a few watts and one that idles at twenty.
 *
 * Everything here is accounted, and that is not decoration. "This profile costs
 * nothing when idle" is a claim about energy, and a claim about energy that is not
 * measured is a wish. The counters below are what let the satellite profile print a
 * duty cycle instead of a promise.
 *
 * The pair also has a discipline that a wrapper cannot enforce and a caller must
 * not skip: between arming the monitor and sleeping, re-check the condition. If the
 * device wrote in that window the monitor is already spent, and the sleep would be
 * waiting for a wake-up that has already happened. @ref processor_sleep_until_write
 * takes the condition as an argument for exactly this reason.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef KERNEL_POWER_PROCESSOR_SLEEP_H
#define KERNEL_POWER_PROCESSOR_SLEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MWAIT's hint index for C1, the shallowest: clock stopped, power maintained. Passing
 * this is what the kernel did before any of the enumeration below existed, and it is
 * still the default, because a deeper state costs wake latency that the caller — not
 * this module — is the one able to afford or not.
 */
#define PROCESSOR_SLEEP_HINT_C1 0u

/**
 * CPUID.05H:EDX carries eight nibbles, C0* through C7*, so the deepest hint it can
 * enumerate targets C7*. Note MWAIT reserves the hint value 0b1111 for C0, which is why
 * the useful range stops well short of the field's width.
 */
#define PROCESSOR_SLEEP_HINT_MAX 6u

/**
 * @enum ProcessorSleepMode_t
 * @brief How the processor was actually put to sleep.
 */
typedef enum {
    PROCESSOR_SLEEP_NONE = 0, /**< It was not: the condition was already true. */
    PROCESSOR_SLEEP_HALT,     /**< `HLT`, woken by an interrupt. */
    PROCESSOR_SLEEP_MONITOR,  /**< `MONITOR`/`MWAIT`, woken by a write or an interrupt. */
} ProcessorSleepMode_t;

/**
 * @brief Probes what the processor offers and zeroes the accounting.
 *
 * MONITOR/MWAIT is advertised by CPUID leaf 1, ECX bit 3. Probed rather than
 * assumed: it is absent on early processors and, more relevantly here, absent from
 * QEMU's default model — so a kernel that executed MWAIT unconditionally would take
 * an invalid-opcode fault on the machine it is developed on.
 *
 * @note A processor that advertises the pair but reports a zero line size in CPUID leaf 5
 *       is treated as lacking it: it is not one to trust with a sleep that only a write
 *       can end, and the line size is not something to guess at.
 */
void kernel_processor_sleep_initialize(void);

/**
 * @brief Does this processor implement the monitor/wait pair?
 * @return true when CPUID advertises it.
 */
bool kernel_processor_sleep_has_monitor(void);

/**
 * @brief Smallest cache line the monitor can watch, in bytes.
 * @return The size, or 0 when the pair is unavailable.
 */
uint32_t kernel_processor_sleep_monitor_line_bytes(void);

/**
 * @brief Which MWAIT hints this CPUID word enumerates, as a bit per hint.
 *
 * @details Pure, and separate from the live CPUID read so a test can hand it words no
 *          emulator produces. The offset it encodes is the one worth getting right:
 *          CPUID.05H:EDX reports C1* in bits 7:4 and C2* in bits 11:8, while MWAIT's
 *          hint 0 targets C1 and 1 targets C2 — so hint @c h is enumerated exactly when
 *          nibble @c h+1 is non-zero. A bit mask rather than a count because real
 *          processors leave GAPS, offering C1 and C6 and nothing between; a count would
 *          claim the missing ones exist.
 *
 * @param leaf5_edx The value CPUID leaf 5 returned in EDX.
 * @return Bit @c h set when hint @c h is enumerated. Zero means none is.
 */
uint32_t kernel_processor_sleep_enumerated_hints(uint32_t leaf5_edx);

/**
 * @brief Which MWAIT hints this processor enumerates.
 * @return The mask, or zero when the pair is unavailable.
 */
uint32_t kernel_processor_sleep_available_hints(void);

/**
 * @brief Can an unmasked interrupt break an MWAIT on this processor?
 *
 * @details CPUID.05H:ECX bit 1. Setting MWAIT's extension bit without it raises #GP, so
 *          this is a guard and not a capability report.
 *
 * @return true when the extension may be used.
 */
bool kernel_processor_sleep_has_interrupt_break(void);

/**
 * @brief Asks for a sleep depth, clamped to what the processor actually enumerates.
 *
 * @details Asking is not getting: a request the processor does not enumerate is lowered
 *          to the deepest enumerated hint below it and counted, rather than passed
 *          through to raise a fault or to land somewhere unintended. What a hint costs
 *          in wake latency is NOT in CPUID — it lives in ACPI `_CST`, which needs an AML
 *          interpreter this kernel does not have — so choosing a depth stays a decision
 *          of the caller that knows its own deadline, never a derivation.
 *
 * @param hint Desired hint index; @ref PROCESSOR_SLEEP_HINT_C1 is the floor.
 * @return true when the request was granted exactly as asked.
 */
bool kernel_processor_sleep_request_hint(uint32_t hint);

/**
 * @brief The hint MWAIT is currently given.
 * @return The hint index.
 */
uint32_t kernel_processor_sleep_active_hint(void);

/**
 * @brief Times a requested depth was lowered to what the processor enumerates.
 * @return The count.
 */
uint32_t kernel_processor_sleep_clamped_count(void);

/**
 * @brief Sleeps until @p watched changes, or an interrupt arrives.
 *
 * The value is re-read after arming the monitor and before sleeping, which closes
 * the race the pair is famous for: if the device wrote between the caller's own check
 * and the MONITOR, the monitor is already spent and MWAIT would sleep waiting for
 * something that has already happened. Falls back to `HLT` where the pair is absent —
 * correct, just less precise: a halted core still wakes on the timer interrupt, it
 * simply cannot be woken by a device's DMA alone.
 *
 * @param watched  Address to watch; typically a driver's write index.
 * @param expected Value that means "nothing new yet".
 * @return How the processor was put to sleep, if at all.
 */
ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected);

/**
 * @brief Sleeps until the next interrupt, at the depth last granted.
 *
 * For an idle loop with nothing to watch. Where MONITOR/MWAIT exists the monitor is
 * armed on a line nothing writes, so only an interrupt ends the wait, as with HLT, but
 * at the hint @ref kernel_processor_sleep_request_hint granted; without the pair it
 * halts in C1 and is counted as a halt. MWAIT may also exit for implementation-defined
 * reasons with no interrupt at all, and such a wake is counted as unattributed rather
 * than credited to a source that did not cause it.
 */
void processor_sleep_until_interrupt(void);

/**
 * @brief Times the processor was put to sleep.
 * @return The count, both modes together.
 */
uint32_t kernel_processor_sleep_count(void);

/**
 * @brief Times a sleep was skipped because the condition was already true.
 *
 * The number that says whether the watch is set on the right address. A node that
 * never actually sleeps is a node whose condition is always already true — which
 * looks identical, from the outside, to one that sleeps perfectly.
 *
 * @return The count.
 */
uint32_t kernel_processor_sleep_skipped_count(void);

/**
 * @brief Times the fallback was taken because the pair is unavailable.
 * @return The count.
 */
uint32_t kernel_processor_sleep_halt_count(void);

/**
 * @brief Cycles spent asleep.
 * @return The total, from the timestamp counter.
 */
uint64_t kernel_processor_sleep_asleep_cycles(void);

/**
 * @brief Cycles spent awake between sleeps.
 *
 * Measured from the end of one sleep to the start of the next, so it counts the work
 * and not the accounting. Before the first sleep there is nothing to measure and this
 * stays zero.
 *
 * @return The total.
 */
uint64_t kernel_processor_sleep_awake_cycles(void);

/**
 * @brief Share of accounted time the processor was awake, in thousandths.
 *
 * The satellite profile's headline number. Nothing accounted yet reads as fully
 * awake rather than fully asleep: a counter that claimed perfect efficiency before
 * measuring anything is the one answer a power figure must never give by default.
 *
 * @return Awake over awake plus asleep, 0 to 1000.
 */
uint32_t kernel_processor_sleep_duty_cycle_permille(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_PROCESSOR_SLEEP_H */
