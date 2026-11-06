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
 * @file pit.h
 * @brief 8253/8254 programmable interval timer.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-06
 **************************************************************************/

#ifndef KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H
#define KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H

#include <kernel/lib/asmutils.h>

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configure PIT channel 0 in rate generator mode.
 *
 * @param target_frequency_hz Requested IRQ0 frequency in Hertz.
 */
extern void programmable_interval_timer_initialize(uint32_t target_frequency_hz);

/**
 * @brief Return currently programmed PIT channel 0 frequency in Hertz.
 */
extern uint32_t programmable_interval_timer_get_frequency_hz(void);

/**
 * @brief Starts PIT channel 2 counting down from 65,535, in mode 0, its gate opened and its output kept
 *        from the speaker, as Linux does to check a clock against it, then reads its count once.
 *
 * @details The 8254 loads a count written to it at the next edge of its clock, 838 ns later, and a count
 *          latched before then is the one it held before. The read lets that edge pass, as Linux's
 *          quick_pit_calibrate does, so that a read made right after the start counts from the new count.
 *
 * @return The rate it counts at, 1,193,182 Hz; its count lasts 54.9 ms.
 */
extern uint32_t programmable_interval_timer_start_channel_2(void);

/**
 * @brief Reads what PIT channel 2 counted since programmable_interval_timer_start_channel_2, from its
 *        latched count, with no interrupt involved.
 *
 * @param out_counted What it counted, valid when the count has not run out.
 * @return Whether the count has not run out: its output, read after the count, is still low.
 */
extern bool programmable_interval_timer_read_channel_2(uint32_t *out_counted);

#endif /* KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H */
