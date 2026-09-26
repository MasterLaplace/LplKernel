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
 * @file clock.h
 * @brief System clock: the periodic tick, its frequency and its backend.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-08
 **************************************************************************/

#ifndef KERNEL_CPU_CLOCK_H
#define KERNEL_CPU_CLOCK_H

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>

#include <kernel/drivers/rtc.h>

#include <stdint.h>

typedef enum {
    CLOCK_PROFILE_CLIENT_RT = 0,
    CLOCK_PROFILE_SERVER_THROUGHPUT = 1,
} ClockProfile_t;

/**
 * @brief Initialize timer subsystem using active build profile policy.
 */
extern void clock_initialize(void);

/**
 * @brief Return selected runtime clock profile.
 */
extern ClockProfile_t clock_get_profile(void);

/**
 * @brief Return active timer backend name.
 */
extern const char *clock_get_backend_name(void);

/**
 * @brief Return active profile name.
 */
extern const char *clock_get_profile_name(void);

/**
 * @brief Return current scheduler tick frequency in Hertz.
 */
extern uint32_t clock_get_tick_hz(void);

/**
 * @brief Return number of tick interrupts since clock initialization.
 */
extern uint32_t clock_get_tick_count(void);

/**
 * @brief Return detected spurious IRQ7 count.
 */
extern uint32_t clock_get_spurious_irq7_count(void);

/**
 * @brief Return detected spurious IRQ15 count.
 */
extern uint32_t clock_get_spurious_irq15_count(void);

/**
 * @brief Return periodic RTC IRQ count (0 when disabled).
 */
extern uint32_t clock_get_rtc_periodic_interrupt_count(void);

/**
 * @brief Return non-zero when periodic RTC IRQ mode is enabled.
 */
extern uint8_t clock_is_rtc_periodic_enabled(void);

/**
 * @brief Read RTC wall-clock snapshot via polling (no periodic IRQ required).
 */
extern void clock_read_walltime(RealtimeClockTime_t *time_snapshot);

#endif /* KERNEL_CPU_CLOCK_H */
