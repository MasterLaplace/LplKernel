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
 * @brief Local APIC timer backend: calibration, periodic mode, and one-shot deadlines in TSC-deadline or count mode.
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
 * @brief Whether the one-shot timer arms deadlines on the timestamp counter: CPUID.01H:ECX[24], read by
 *        @ref advanced_pic_timer_backend_initialize.
 *
 * @details TSC-deadline mode (Intel SDM vol. 3A, §13.5.4.1, revision 093) interrupts when the timestamp counter
 *          reaches the deadline written to IA32_TSC_DEADLINE: the deadline is the plan's own cycle, with no count
 *          scaled by a calibrated ratio, and arming it is one write. Without it the one-shot counts down.
 *
 * @return 1 in TSC-deadline mode, 0 in count mode.
 */
extern uint8_t advanced_pic_timer_backend_arms_timestamp_counter_deadlines(void);

/**
 * @brief Whether the timestamp counter runs at one rate in every P-, C- and T-state: CPUID.80000007H:EDX[8]
 *        (Intel SDM vol. 3B, §20.17.1, revision 093), read by @ref advanced_pic_timer_backend_initialize.
 *
 * @note A hypervisor hides the bit from a guest it may migrate (QEMU's `-cpu host` does, `-cpu host,+invtsc` does
 *       not) while the counter it gives still runs at one rate: the bit says the rate is promised, not that it is
 *       steady, so nothing is refused without it.
 *
 * @return 1 when CPUID promises it, 0 otherwise or when the extended leaf is absent.
 */
extern uint8_t advanced_pic_timer_backend_has_invariant_timestamp_counter(void);

/**
 * @brief Arms the local timer to interrupt once, when the timestamp counter reaches @p deadline.
 *
 * The half of the timer a tickless kernel needs: periodic mode wakes on a cadence, this wakes at a deadline.
 * Programming a deadline instead of a period is what lets an idle core sleep for seconds rather than a
 * millisecond at a time.
 *
 * @details In TSC-deadline mode the deadline is written to IA32_TSC_DEADLINE, and the interrupt comes when the
 *          counter equals or exceeds it, never before. In count mode the count is the cycles left scaled by the two
 *          counts of the calibration, rounded down; at least one count, at most the timer's range. A deadline
 *          already passed fires as soon as the hardware can, in both modes. The timer's mode is written only when
 *          it changes, so that a sleep after the first of a session costs one register write.
 *
 * @note The mode written is the bootstrap processor's: the backend keeps one timer's state, as for the
 *       calibration and periodic mode.
 *
 * @param deadline Cycle of the timestamp counter.
 * @return 1 when armed, 0 when the local APIC is unavailable, or in count mode uncalibrated.
 */
extern uint8_t advanced_pic_timer_backend_arm_at_cycle(uint64_t deadline);

/**
 * @brief Disarms the timer @ref advanced_pic_timer_backend_arm_at_cycle armed, if it has not fired yet.
 *
 * @details In TSC-deadline mode a deadline the counter has not reached by @p now has not fired, as the SDM tells a
 *          stale interrupt apart (§13.5.4.1), and one it has reached cleared itself; in count mode a current count
 *          other than zero is still counting.
 *
 * @param now The cycle the core woke at.
 * @return 1 when the timer had not fired, so that the wake-up came from elsewhere; 0 when it had, or was not armed.
 */
extern uint8_t advanced_pic_timer_backend_disarm(uint64_t now);

/**
 * @brief Stops the local timer, whichever mode it is in, and masks its vector.
 */
extern void advanced_pic_timer_backend_disable(void);

#endif /* KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_TIMER_H */
