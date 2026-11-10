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
 * @file apic_timer.h
 * @brief Local APIC timer backend: calibration, periodic and one-shot modes.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-08
 **************************************************************************/

#ifndef KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_TIMER_H
#define KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_TIMER_H

#include <kernel/cpu/apic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pic.h>
#include <kernel/lib/asmutils.h>

/**
 * @brief Initialize advanced PIC timer backend (probe-only in current stage).
 *
 * @details The local APIC may come in xAPIC or in x2APIC mode: by default, the firmware hands it over
 *          in x2APIC mode when a processor's x2APIC ID is 0xFF or more (Intel SDM vol. 3, §13.12.7).
 *
 * @return Non-zero when advanced PIC timer backend is active, zero when unavailable.
 */
extern uint8_t advanced_pic_timer_backend_initialize(uint32_t target_frequency_hz);

/**
 * @brief Perform late APIC runtime preparation after paging/PMM init.
 *
 * This stage maps LAPIC MMIO, validates register accessibility, and keeps
 * APIC timer LVT masked (no scheduler ownership takeover yet).
 *
 * @return Non-zero when late preparation succeeded, zero otherwise.
 */
extern uint8_t advanced_pic_timer_backend_late_initialize(void);

/**
 * @brief Return advanced PIC backend name string.
 */
extern const char *advanced_pic_timer_backend_name(void);

/**
 * @brief Return detected local APIC physical MMIO base address.
 *
 * Returns 0 when probe failed or local APIC MMIO mode is unavailable.
 */
extern uint32_t advanced_pic_timer_backend_get_local_apic_physical_base(void);

/**
 * @brief Return mapped LAPIC MMIO virtual base used by APIC backend.
 *
 * Returns 0 when LAPIC MMIO is not mapped yet.
 */
extern uint32_t advanced_pic_timer_backend_get_local_apic_virtual_base(void);

/**
 * @brief Return non-zero when LAPIC MMIO page is mapped for runtime access.
 */
extern uint8_t advanced_pic_timer_backend_is_local_apic_mmio_mapped(void);

/**
 * @brief Return LAPIC version register value observed during late initialization.
 */
extern uint32_t advanced_pic_timer_backend_get_local_apic_version_register(void);

/**
 * @brief Calibrate LAPIC timer rate against PIT tick cadence.
 *
 * The timer stays masked while it counts eight PIT ticks; the frequency it measures is the one
 * periodic and one-shot modes are programmed from. The timestamp counter is read at both ends of the
 * same ticks, so its rate costs no wait of its own, and the ratio of the two counts, which one-shot
 * mode is armed from, holds whatever a late tick did to the window.
 *
 * @return Non-zero when calibration data is available, zero otherwise.
 */
extern uint8_t advanced_pic_timer_backend_calibrate_with_pit(void);

/**
 * @brief Return measured LAPIC timer frequency estimate in Hertz.
 */
extern uint32_t advanced_pic_timer_backend_get_calibrated_timer_frequency_hz(void);

/**
 * @brief The timestamp counter's rate, measured over the ticks the local timer was calibrated on.
 * @return Cycles per second; 0 until a calibration succeeded.
 */
extern uint64_t advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz(void);

/**
 * @brief Enable LAPIC periodic timer mode using the remapped timer vector.
 *
 * Every profile takes this path once the probe, the late initialization and the calibration
 * succeeded; the PIT's IRQ0 is masked and the local timer owns the tick from then on.
 *
 * @param target_frequency_hz desired scheduler tick frequency.
 * @return Non-zero when APIC periodic mode is active, zero otherwise.
 */
extern uint8_t advanced_pic_timer_backend_enable_periodic_mode(uint32_t target_frequency_hz);

/**
 * @brief Return non-zero when APIC periodic timer mode is active.
 */
extern uint8_t advanced_pic_timer_backend_is_periodic_mode_enabled(void);

/**
 * @brief Send Local APIC EOI for an APIC-delivered timer interrupt.
 */
extern void advanced_pic_timer_backend_signal_end_of_interrupt(void);

/**
 * @brief Return whether current CPU is bootstrap processor according to APIC base MSR.
 */
extern uint8_t advanced_pic_timer_backend_is_bootstrap_processor(void);

/**
 * @brief Arms the local timer to fire once, @p cycles of the timestamp counter from now.
 *
 * The half of the timer a tickless kernel needs: periodic mode wakes on a cadence,
 * this wakes at a deadline. Programming a deadline instead of a period is what lets
 * an idle core sleep for seconds rather than a millisecond at a time.
 *
 * @details The count is the cycles scaled by the two counts of the calibration, rounded down so that
 *          the timer never fires after the cycle asked for; at least one count, at most the timer's range.
 *
 * @param cycles Delay before the interrupt, in cycles of the timestamp counter.
 * @return 1 when armed, 0 when the local APIC is unavailable or uncalibrated.
 */
extern uint8_t advanced_pic_timer_backend_arm_one_shot_in_cycles(uint64_t cycles);

/**
 * @brief Stops the local timer and masks its vector.
 */
extern void advanced_pic_timer_backend_disable(void);

/**
 * @brief Ticks remaining before the local timer fires.
 * @return The current count, or 0 when the timer is unavailable.
 */
extern uint32_t advanced_pic_timer_backend_read_current_count(void);

#endif /* KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_TIMER_H */
