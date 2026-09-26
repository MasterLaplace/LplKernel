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
 * @file smoke_libengine.h
 * @brief Cross-target smoke battery of the engine, assistant and knowledge gates.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-07-01
 **************************************************************************/

#ifndef KERNEL_TESTING_SMOKE_LIBENGINE_H
#define KERNEL_TESTING_SMOKE_LIBENGINE_H

#include <kernel/drivers/serial.h>

/**
 * @brief Runs the cross-target smoke battery: every engine, assistant and knowledge gate.
 *
 * Extracted from kernel_main so the boot path stays readable and so the whole
 * battery can be compiled out of a release/production image (it is only invoked
 * when LPL_KERNEL_ENABLE_SMOKE_TESTS is defined). Each gate folds deterministic
 * results and prints them over serial for byte-for-byte comparison against the
 * host oracle (the HARD determinism contract); each has its own report function,
 * named after the gate, in smoke_libengine.c.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void smoke_libengine_run_all(Serial_t *com1);

#endif /* KERNEL_TESTING_SMOKE_LIBENGINE_H */
