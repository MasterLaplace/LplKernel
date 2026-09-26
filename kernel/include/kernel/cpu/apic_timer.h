/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
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
 * This calibration keeps LAPIC timer masked and only produces diagnostics for
 * next-stage APIC timer ownership migration.
 *
 * @return Non-zero when calibration data is available, zero otherwise.
 */
extern uint8_t advanced_pic_timer_backend_calibrate_with_pit(void);

/**
 * @brief Return measured LAPIC timer frequency estimate in Hertz.
 */
extern uint32_t advanced_pic_timer_backend_get_calibrated_timer_frequency_hz(void);

/**
 * @brief Enable LAPIC periodic timer mode using the remapped timer vector.
 *
 * This is an experimental ownership handoff path and should be used only when
 * APIC probe + late init + calibration succeeded.
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
 * @brief Arms the local timer to fire once, after a delay.
 *
 * The half of the timer a tickless kernel needs: periodic mode wakes on a cadence,
 * this wakes at a deadline. Programming a deadline instead of a period is what lets
 * an idle core sleep for seconds rather than a millisecond at a time.
 *
 * @param microseconds Delay before the interrupt.
 * @return 1 when armed, 0 when the local APIC is unavailable or uncalibrated.
 */
extern uint8_t advanced_pic_timer_backend_arm_one_shot(uint32_t microseconds);

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
