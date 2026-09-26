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
 * @file rtc.h
 * @brief CMOS real-time clock and its periodic interrupt.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-06
 **************************************************************************/

#ifndef KERNEL_DRIVERS_REALTIME_CLOCK_H
#define KERNEL_DRIVERS_REALTIME_CLOCK_H

#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} RealtimeClockTime_t;

/**
 * @brief Initialize RTC support (handler registration + known register state).
 */
extern void realtime_clock_initialize(void);

/**
 * @brief Enable/disable periodic IRQ8 generation.
 */
extern void realtime_clock_set_periodic_interrupt_enabled(uint8_t enabled);

/**
 * @brief Return non-zero when periodic IRQ8 generation is enabled.
 */
extern uint8_t realtime_clock_is_periodic_interrupt_enabled(void);

/**
 * @brief Return number of handled periodic RTC IRQ8 events.
 */
extern uint32_t realtime_clock_get_periodic_interrupt_count(void);

/**
 * @brief Read a coherent RTC wall-clock snapshot from CMOS.
 */
extern void realtime_clock_read_time(RealtimeClockTime_t *time_snapshot);

#endif /* KERNEL_DRIVERS_REALTIME_CLOCK_H */
