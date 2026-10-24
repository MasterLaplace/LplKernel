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
 * A dump holds every table the machine's firmware handed to Linux, but neither its root pointer
 * nor its root table, and every table address is 0. What the FADT names keeps its real address:
 * the DSDT is served there. Every other table is served at an address of the harness's choosing,
 * and a root pointer and an XSDT that list them all are written, so that the kernel walks the
 * recorded machine as it walks a live one.
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
    uint8_t *bytes;    /**< Its bytes, at least as many as its header's length. */
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
 * @brief Reads an acpidump text file and serves its tables.
 *
 * @param machine Filled; released with recorded_machine_close, even when this fails.
 * @param path    The dump.
 * @param error   Receives what went wrong, when it fails.
 * @return false when the file cannot be read, holds no table, holds more than
 *         RECORDED_MACHINE_MAX_TABLES, or a table cannot be served.
 */
extern bool recorded_machine_open(RecordedMachine_t *machine, const char *path, const char **error);

/**
 * @brief Frees what recorded_machine_open took.
 *
 * @param machine The machine, unusable afterwards.
 */
extern void recorded_machine_close(RecordedMachine_t *machine);

#endif /* TOOLS_ACPI_HOST_RECORDED_MACHINE_H */
