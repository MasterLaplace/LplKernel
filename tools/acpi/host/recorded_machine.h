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
 * @file recorded_machine.h
 * @brief The firmware of a machine recorded by acpidump, served by a simulated host.
 *
 * A dump holds every table the machine's firmware handed to Linux, the SSDTs it loads at run time
 * included, but neither its root pointer nor its root table, and every table address is 0. What a
 * FADT names keeps its real address: the DSDT is served there. Every other table is served at an
 * address of the harness's choosing, and a root pointer and an XSDT that list them all are
 * written. A live machine's XSDT leaves out the SSDTs loaded at run time; telling them apart takes
 * the interpreter's Load, so until then the directory holds them too.
 *
 * The tables a debug kernel printed on its serial port, in the same format, are read and served the
 * same way, and what a recorded machine serves can be written as an image, the Multiboot module of a
 * debug kernel.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef TOOLS_ACPI_HOST_RECORDED_MACHINE_H
#define TOOLS_ACPI_HOST_RECORDED_MACHINE_H

#include "simulated_host.h"

#include <stdbool.h>
#include <stdint.h>

#define RECORDED_MACHINE_MAX_TABLES 96u /**< Tables one dump may hold. */

/**
 * @brief One table of a dump, as acpidump printed it.
 */
typedef struct RecordedTable {
    char signature[4]; /**< Its four letters, from its first bytes. */
    uint8_t *bytes;    /**< Its bytes, as many as the dump holds: a table cut short is never padded. */
    uint32_t length;   /**< How many of them the table has. */
    uint32_t capacity; /**< How many @p bytes can hold before it grows. */
} RecordedTable_t;

/**
 * @brief A recorded machine: its tables, the root pointer and the XSDT written for them, and the
 *        simulated host that serves them.
 */
typedef struct RecordedMachine {
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t simulated; /**< What the kernel reads through. */
    uint32_t table_count;                                            /**< Tables the dump holds. */
    RecordedTable_t tables[RECORDED_MACHINE_MAX_TABLES];             /**< In the dump's order. */
    uint8_t *root;                                                   /**< The root pointer, then the XSDT. */
} RecordedMachine_t;

/**
 * @brief Reads an acpidump text file and serves its tables. Its memory and the configuration space of
 *        its PCI functions read as the same fill, as acpiexec's regions do (§8.4).
 *
 * @param machine Filled; released with recorded_machine_close, even when this fails.
 * @param path    The dump.
 * @param error   Receives what went wrong, when it fails.
 * @return false when the file cannot be read, holds no table, holds more than
 *         RECORDED_MACHINE_MAX_TABLES, or a table cannot be served.
 */
extern bool recorded_machine_open(RecordedMachine_t *machine, const char *path, const char **error);

/**
 * @brief Reads the tables a kernel printed on its serial port and serves them, as recorded_machine_open
 *        serves a dump's.
 *
 * @details A debug kernel booted with ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINT_TABLES_OPTION
 *          prints every table its directory holds, in acpidump's format, between
 *          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_BEGIN and
 *          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_END; every other line of the log is
 *          left alone.
 *
 * @param machine Filled; released with recorded_machine_close, even when this fails.
 * @param path    The serial log.
 * @param error   Receives what went wrong, when it fails.
 * @return false when the log cannot be read, holds no printed tables, ends before their end, holds a
 *         table of fewer or more bytes than its header says, or they cannot be served.
 */
extern bool recorded_machine_open_printed(RecordedMachine_t *machine, const char *path, const char **error);

/**
 * @brief Writes what @p machine serves as an image, which a debug kernel booted with it as a
 *        Multiboot module serves in turn (simulated_host.h).
 *
 * @details The image is read back as ring 0 reads it, and compared with what @p machine serves,
 *          before the file is written.
 *
 * @param machine An opened machine.
 * @param path    The image file, created or replaced.
 * @param error   Receives what went wrong, when it fails.
 * @return false when the image read back differs, or the file cannot be written.
 */
extern bool recorded_machine_write_image(const RecordedMachine_t *machine, const char *path, const char **error);

/**
 * @brief Frees what recorded_machine_open or recorded_machine_open_printed took.
 *
 * @param machine The machine, unusable afterwards.
 */
extern void recorded_machine_close(RecordedMachine_t *machine);

#endif /* TOOLS_ACPI_HOST_RECORDED_MACHINE_H */
