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
 * @file acpi_helper.h
 * @brief Serial reports of the ACPI MADT, I/O APICs and interrupt overrides.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_CPU_ACPI_HELPER_H
#define KERNEL_CPU_ACPI_HELPER_H

#include <kernel/drivers/serial.h>

/**
 * @brief Writes the `[LPLTLM] acpi` record: the root table walked, and every table it led to.
 *
 * @param serial The port the record is written to.
 */
extern void write_acpi_tables_info(Serial_t *serial);
extern void write_acpi_madt_info(Serial_t *serial);

/**
 * @brief Writes the `[LPLTLM] acpi.madt` record: the MADT's CPU slots, and the entries counted
 *        without one.
 *
 * @param serial The port the record is written to.
 */
extern void write_acpi_madt_record(Serial_t *serial);

/**
 * @brief Writes the `[LPLTLM] acpi.mode` record: how the boot's switch to ACPI mode ended, the enable
 *        registers it cleared, its reads of SCI_EN and the milliseconds it waited, and the FADT
 *        fields it used.
 *
 * @param serial The port the record is written to.
 */
extern void write_acpi_mode_record(Serial_t *serial);

/**
 * @brief Writes the `[LPLTLM] acpi.aml` record: what the boot's load of the definition blocks
 *        decoded, declared and built, what waits for the interpreter, its faults, what its namespace
 *        and stacks took, and the namespace fold; then the `[LPLTLM] acpi.regions` record: its field
 *        accesses and what reaching their regions took; then the `[LPLTLM] acpi.values` record: the
 *        objects its heap allocated, freed and held at most, the releases that found nothing to
 *        release, and the conversions it counted.
 *
 * @param serial The port the record is written to.
 */
extern void write_acpi_machine_language_record(Serial_t *serial);
extern void write_acpi_ioapics_info(Serial_t *serial);
extern void write_acpi_isos_info(Serial_t *serial);
extern void write_acpi_isa_routing_info(Serial_t *serial);

#endif /* KERNEL_CPU_ACPI_HELPER_H */
