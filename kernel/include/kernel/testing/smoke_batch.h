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
 * @file smoke_batch.h
 * @brief Sequencing of the kernel smoke test batteries.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-31
 **************************************************************************/

#ifndef KERNEL_TESTING_SMOKE_BATCH_H
#define KERNEL_TESTING_SMOKE_BATCH_H

#include <kernel/drivers/serial.h>

/**
 * @brief Runs all enabled runtime initialization smoke tests.
 *
 * This function consolidates the execution of individual subsystem
 * diagnostics and validations after the kernel is fully loaded.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void smoke_batch_run_initialization_tests(Serial_t *com1);

/**
 * @brief Runs all enabled post-boot and exception smoke tests.
 *
 * This function invokes testing for exceptions (#PF, #GP, etc.) and
 * other interactive demonstrations.
 *
 * @note Section protection runs here and not with the initialization battery: the pages
 *       only become read-only in kernel_main, once every global constructor has run. It
 *       runs before the controlled exception regressions, which are deliberate faults and
 *       would muddy a probe that is counting one.
 * @note The wake-up accounting battery runs before the power floor and before the
 *       deliberate faults: it resets the live counters on its way out, so what the floor
 *       reports afterwards is the boot's own sleeps rather than this test's synthetic ones.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void smoke_batch_run_post_boot_tests(Serial_t *com1);

#endif /* KERNEL_TESTING_SMOKE_BATCH_H */
