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
 * @file exception.h
 * @brief CPU exception handlers and their panic reports.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-06
 **************************************************************************/

#ifndef KERNEL_CPU_EXCEPTION_H
#define KERNEL_CPU_EXCEPTION_H

#include <kernel/cpu/isr.h>
#include <kernel/lib/asmutils.h>

/**
 * @brief Register dedicated handlers for critical CPU exceptions.
 *
 * Current bring-up installs explicit handlers for:
 *   - #DB (vector 1)
 *   - #BP (vector 3)
 *   - #UD (vector 6)
 *   - #DF (vector 8)
 *   - #GP (vector 13)
 *   - #PF (vector 14)
 */
extern void interrupt_exception_initialize(void);

#endif /* KERNEL_CPU_EXCEPTION_H */
