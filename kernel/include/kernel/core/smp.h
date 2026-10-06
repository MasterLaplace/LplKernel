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
 * @file smp.h
 * @brief Symmetric multiprocessing start-up of the discovered application processors.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-31
 **************************************************************************/

#ifndef KERNEL_CORE_SMP_H
#define KERNEL_CORE_SMP_H

#include <kernel/drivers/serial.h>

/**
 * @brief Coordinates the SMP (Symmetric Multiprocessing) startup sequence for all discovered application processors
 * (APs).
 *
 * This function handles bootstrapping, INIT/SIPI sequence dispatch, trampoline installation,
 * and telemetry/diagnostics for bringing secondary cores online safely.
 *
 * @param com1 Pointer to the primary serial interface for diagnostic output.
 */
extern void kernel_symmetric_multiprocessing_try_start_discovered_aps(Serial_t *com1);

/**
 * @brief Emits what the multiprocessor bring-up achieved, as one telemetry record.
 *
 * @details `cpus` is what the MADT enables, `online` what the topology counts once the boot is
 *          done, and the shootdown counts say whether an AP ever had to answer. It passes when
 *          every enabled CPU is online, every AP attempted was delivered and no shootdown timed
 *          out. A boot asked for two CPUs that reports one is caught by whoever asked, by
 *          comparing `cpus` with the count it requested.
 *
 * @param serial Serial port the record is written to.
 */
extern void kernel_symmetric_multiprocessing_report(Serial_t *serial);

#endif /* KERNEL_CORE_SMP_H */
