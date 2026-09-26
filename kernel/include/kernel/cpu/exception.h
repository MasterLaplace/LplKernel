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
