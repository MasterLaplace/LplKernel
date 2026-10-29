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

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINT_TABLES_OPTION                                                 \
    "lpl.acpi.tables=print" /**< The boot option that has the boot print every ACPI table it read. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_BEGIN                                                \
    "[acpidump] begin" /**< The line before the tables printed, which the host harness looks for. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_END "[acpidump] end" /**< The line after them. */

/**
 * @brief Writes the `[LPLTLM] acpi` record: the root table walked, and every table it led to.
 *
 * @param serial The port the record is written to.
 */
extern void write_acpi_tables_info(Serial_t *serial);
extern void write_acpi_madt_info(Serial_t *serial);

/**
 * @brief Writes the `[LPLTLM] acpi.madt` record: the CPU slots, I/O APICs and interrupt source
 *        overrides the MADT keeps, and every entry counted without a place.
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
 * @brief Writes the `[LPLTLM] acpi.timer` record: whether the boot found an ACPI power management timer
 *        that Stall waits on, its port, its rate against the kernel tick when it was measured, the reads
 *        and the time the checks took, and the waits made on it and their reads.
 *
 * @param serial The port the record is written to.
 */
extern void write_advanced_configuration_and_power_interface_power_management_timer_record(Serial_t *serial);

/**
 * @brief Writes the `[LPLTLM] acpi.aml` record: what the boot's load of the definition blocks
 *        decoded, declared and built, what waits for the interpreter, its faults, what its namespace
 *        and stacks took, and the namespace fold; then the `[LPLTLM] acpi.init` record of the
 *        initialisation; then the `[LPLTLM] acpi.load` record: the tables Load and LoadTable added, refused
 *        and found missing, and the bytes Load copied; then the `[LPLTLM] acpi.regions` record: its field
 *        accesses and what reaching their regions took; then the `[LPLTLM] acpi.values` record: the
 *        objects its heap allocated, freed and held at most, the releases that found nothing to
 *        release, and the conversions it counted.
 *
 * @param serial The port the record is written to.
 */
extern void write_acpi_machine_language_record(Serial_t *serial);

/**
 * @brief Writes one `[LPLTLM] acpi.cst` record per CPU slot (§3.10): its slot, local APIC ID and ACPI
 *        Processor UID, whether a _CST was evaluated for it, its state count and the faulty states it left
 *        out, then each state it published as `stateN=type:latency:power:space:width:offset:access:address`
 *        in hexadecimal, each field as wide as its type.
 *
 * @param serial The port the records are written to.
 */
extern void write_advanced_configuration_and_power_interface_processor_idle_record(Serial_t *serial);

/**
 * @brief Writes every table the boot's directory holds, its bytes as acpidump prints them, so that
 *        the host harness loads the bytes this boot read (`test-acpi --firmware-log`).
 *
 * @details Each table is a line `<signature> @ 0x<physical address>`, its bytes 16 per line, each line
 *          led by its offset, then an empty line; ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_BEGIN
 *          comes before them and ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_END after.
 *          acpidump's ASCII column is left out.
 *
 * @param serial The port the tables are written to.
 */
extern void write_advanced_configuration_and_power_interface_tables(Serial_t *serial);
extern void write_acpi_ioapics_info(Serial_t *serial);
extern void write_acpi_isos_info(Serial_t *serial);
extern void write_acpi_isa_routing_info(Serial_t *serial);

#endif /* KERNEL_CPU_ACPI_HELPER_H */
