/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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

#endif /* KERNEL_CPU_PROGRAMMABLE_INTERVAL_TIMER_H */
