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
 * @file machine.h
 * @brief The machine a boot ran on, as one record: the key runs and regressions are grouped by.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-08
 **************************************************************************/

#ifndef KERNEL_DIAG_MACHINE_H
#define KERNEL_DIAG_MACHINE_H

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes the `[LPLTLM] machine` record: the hypervisor, the processor and the state each
 *        probe ended in.
 *
 * @details Every value names a class of machine, never one machine: two boots of the same class
 *          write the same record. Keys:
 *          - `hypervisor`: the signature of CPUID leaf 0x40000000 (`TCGTCGTCGTCG`, `KVMKVMKVM`),
 *            `unnamed` when the hypervisor bit is set without one, `none` without the bit;
 *          - `vendor`, `family`, `model`, `stepping`: CPUID leaves 0 and 1;
 *          - `cpus`: the processors online when the record is written;
 *          - `acpi`, `ioapic`, `topology`: the state each of those probes ended in;
 *          - `apic`: `x2apic`, `xapic` or `absent`;
 *          - `audio_controller`: 1 when the bus holds a high-definition audio controller.
 *          A character other than a letter, a digit, '.', '-' or '_' in a value becomes '-', so
 *          the telemetry module never has to sanitise one.
 *
 * @param serial Output port.
 */
void kernel_machine_report(Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DIAG_MACHINE_H */
