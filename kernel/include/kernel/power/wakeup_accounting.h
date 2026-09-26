/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
 * @file wakeup_accounting.h
 * @brief Who woke the processor — because you cannot shorten a wake-up you cannot name.
 *
 * A wake-up is NOT an interrupt. Interrupts fire constantly while the core is busy and
 * none of them woke anything; what makes one a wake-up is that the core was asleep when
 * it arrived. So the sleep path ARMS, and the first interrupt to reach the dispatcher
 * while armed is the waker. Counting interrupts instead is the failure that looks most
 * like success: thousands of events, a plausible distribution, and a number that means
 * nothing.
 *
 * Every armed sleep ends exactly one of three ways — a vector named it, a monitored
 * write ended it, or nothing accounted for it — which is what makes these counters
 * checkable rather than merely plausible. That check is
 * @ref kernel_wakeup_accounting_conserves, and the equation lives in its body rather
 * than here: spelled out in prose it would be a second copy, free to drift from the one
 * that runs.
 *
 * The one inaccuracy, stated rather than hidden: an interrupt arriving between the arm
 * and the halt is credited as the waker. It is not — it fired while the core was still
 * running — but it is the closest fact available without disabling interrupts across the
 * pair, and it leaves the law intact because arming stays one-to-one with entering a
 * sleep. Powertop lives with the same approximation.
 *
 * The state is global rather than per-processor. An interrupt taken by another core
 * while this one is armed would claim the wake-up — wrong about WHICH core was woken,
 * still right about the law. It does not arise today: the power floor sleeps on the
 * bootstrap processor only.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-09-26
 **************************************************************************/

#ifndef KERNEL_POWER_WAKEUP_ACCOUNTING_H
#define KERNEL_POWER_WAKEUP_ACCOUNTING_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One counter per interrupt vector: the vector IS the name of the source. */
#define KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT 256u

/** Reported when no sleep has been attributed to any vector yet. */
#define KERNEL_WAKEUP_ACCOUNTING_NO_VECTOR 0xFFFFu

/**
 * @brief Drops every counter back to zero.
 */
void kernel_wakeup_accounting_reset(void);

/**
 * @brief Declares that the core is about to sleep, so the next interrupt is a wake-up.
 *
 * @details Call immediately before the halt or the monitor wait. Arming while a sleep is
 *          already outstanding is refused and counted rather than applied.
 */
void kernel_wakeup_accounting_arm(void);

/**
 * @brief Credits this vector with the wake-up, if one is outstanding.
 *
 * @details Call first in the interrupt dispatcher, before the handler runs, since a
 *          handler may sleep again and arm a fresh wake-up. Credits nothing when nothing
 *          is armed, which costs one load and one branch on the overwhelmingly common
 *          path: an interrupt taken while the core was busy.
 *
 * @param vector The interrupt vector being dispatched.
 */
void kernel_wakeup_accounting_attribute(uint8_t vector);

/**
 * @brief Closes an outstanding sleep that a monitored write ended.
 *
 * @details Only the MWAIT path may claim this: with break-on-interrupt set, a wait that
 *          returns while still armed was ended by a write, since an interrupt would have
 *          been attributed on its way through the dispatcher.
 */
void kernel_wakeup_accounting_close_monitor_write(void);

/**
 * @brief Closes an outstanding sleep that nothing named.
 *
 * @details A halt can only resume on an interrupt, so reaching here means one arrived by
 *          a path that bypasses the dispatcher — a non-maskable interrupt, or a defect.
 */
void kernel_wakeup_accounting_close_unattributed(void);

/**
 * @brief Whether a sleep is currently outstanding.
 *
 * @return True between an arm and its close.
 */
bool kernel_wakeup_accounting_is_armed(void);

/**
 * @brief Wake-ups credited to one vector.
 *
 * @param vector The interrupt vector.
 * @return The count.
 */
uint32_t kernel_wakeup_accounting_get_vector_count(uint8_t vector);

/**
 * @brief Sleeps entered, which is the number of times arming took effect.
 *
 * @return The count, and the denominator of the conservation law.
 */
uint32_t kernel_wakeup_accounting_get_sleep_count(void);

/**
 * @brief Wake-ups credited to some vector, summed over the table.
 *
 * @return The count.
 */
uint32_t kernel_wakeup_accounting_get_attributed_count(void);

/**
 * @brief Sleeps ended by a write to a monitored line rather than by an interrupt.
 *
 * @return The count. Zero on a processor without MONITOR/MWAIT, which includes QEMU.
 */
uint32_t kernel_wakeup_accounting_get_monitor_wake_count(void);

/**
 * @brief Sleeps that ended without anything accounting for them.
 *
 * @return The count. Expected to stay zero.
 */
uint32_t kernel_wakeup_accounting_get_unattributed_count(void);

/**
 * @brief Times arming was requested while a sleep was already outstanding.
 *
 * @return The count. Expected to stay zero; non-zero means a sleep path closed itself
 *         improperly.
 */
uint32_t kernel_wakeup_accounting_get_double_arm_count(void);

/**
 * @brief The vector that woke the core most often, ties going to the lowest.
 *
 * @return The vector, or @ref KERNEL_WAKEUP_ACCOUNTING_NO_VECTOR when none was credited.
 */
uint16_t kernel_wakeup_accounting_get_busiest_vector(void);

/**
 * @brief How many distinct vectors ever woke the processor.
 *
 * @return The count.
 */
uint32_t kernel_wakeup_accounting_get_source_count(void);

/**
 * @brief Whether the three outcomes sum to the sleeps entered.
 *
 * @details Safe to ask mid-sleep: an outstanding sleep has no outcome yet and is left
 *          out of the total, rather than counted as a shortfall that closing it would
 *          repair on its own. Callers must also require a non-zero sleep count before
 *          treating this as evidence, because on an empty run it holds as 0 == 0.
 *
 * @return True when the books balance.
 */
bool kernel_wakeup_accounting_conserves(void);

/**
 * @brief Emits the counters as one telemetry record.
 *
 * @details Counting runs in every build and costs a branch on an interrupt already
 *          taken; emitting costs about a millisecond per character on COM1, so it is a
 *          verb somebody calls rather than something that happens. The `wakeup` console
 *          command is that verb, and a governor that adapts to the wake profile reads
 *          the counters directly, with nothing printed at all.
 *
 * @param serial Output port.
 */
void kernel_wakeup_accounting_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_WAKEUP_ACCOUNTING_H */
