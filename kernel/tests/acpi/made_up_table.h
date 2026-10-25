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
 * @file made_up_table.h
 * @brief A made-up definition block written in a buffer, and loaded, for the tests that run AML.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_MADE_UP_TABLE_H
#define KERNEL_TESTS_ACPI_MADE_UP_TABLE_H

#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>

#define MADE_UP_TABLE_HEADER    36u             /**< Bytes of a table header. */
#define MADE_UP_MAX_NODES       1024u           /**< The node cap of a made-up load. */
#define MADE_UP_MAX_ARENA_BYTES (1024u * 1024u) /**< The arena cap of a made-up load. */

/**
 * @brief Writes a definition block in @p table: a header of @p signature and @p revision, OEM "LPLTST"
 *        and table "MADEUPML", then @p length bytes of AML, its checksum made to hold.
 *
 * @param table     The buffer, at least MADE_UP_TABLE_HEADER + @p length bytes.
 * @param signature Four characters.
 * @param revision  The header's revision: below 2, a DSDT makes every integer 32 bits.
 * @param aml       The AML.
 * @param length    Bytes of AML.
 * @return The table's header.
 */
extern const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
made_up_definition_block(uint8_t *table, const char *signature, uint8_t revision, const uint8_t *aml, uint32_t length);

/**
 * @brief Loads @p aml as the only definition block, a DSDT of @p revision written in @p table: opens
 *        @p load on a fresh @p host, declares the table, running its table-level code, and decodes it.
 *
 * @param test     The test, which the namespace opening is checked on.
 * @param load     Filled; closed with advanced_configuration_and_power_interface_machine_language_close.
 * @param host     The simulated host, opened here.
 * @param table    The buffer the table is written in.
 * @param revision The DSDT's revision.
 * @param aml      The AML.
 * @param length   Bytes of AML.
 * @return false when the namespace could not be opened.
 */
extern bool made_up_load_table(KernelTest_t *test, AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                               AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *host, uint8_t *table,
                               uint8_t revision, const uint8_t *aml, uint32_t length);

/**
 * @brief The node at the root named @p name.
 *
 * @param load The load.
 * @param name Four characters.
 * @return The node, or NULL when the root has no such child.
 */
extern const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
made_up_root_node(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const char *name);

#endif /* KERNEL_TESTS_ACPI_MADE_UP_TABLE_H */
