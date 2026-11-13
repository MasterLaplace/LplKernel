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
 * @file energy.h
 * @brief The boot's record of the package energy counter it found.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-13
 **************************************************************************/

#ifndef KERNEL_DIAG_ENERGY_H
#define KERNEL_DIAG_ENERGY_H

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes the `[LPLTLM] energy` record of the package energy counter the last detection found
 *        (kernel/power/energy_counter.h).
 *
 * @details Keys:
 *          - `backend`: `intel_core`, `intel_atom`, `amd` or `none`;
 *          - `state`: `present`, or why not: `not_listed`, `unit_faulted`, `no_unit`, `counter_faulted`, `frozen`,
 *            `untimed`, `not_detected`;
 *          - `esu` and `pu`: the energy and power unit exponents its unit register gives, `none` where it did not read;
 *          - `tdp_mw`, `pl1_mw` and `pl1_enabled`: Intel's thermal design power, power limit 1 and whether that limit
 * is enabled, `none` where unknown;
 *          - `refused_reads`: the MSR reads the #GP handler turned into a refusal since the boot.
 *
 * @param serial Output port.
 */
void kernel_energy_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DIAG_ENERGY_H */
