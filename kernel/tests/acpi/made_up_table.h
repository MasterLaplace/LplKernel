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
 * @brief A made-up definition block written in a buffer, for the tests that load AML.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_MADE_UP_TABLE_H
#define KERNEL_TESTS_ACPI_MADE_UP_TABLE_H

#include <kernel/acpi/tables.h>

#include <stdint.h>

#define MADE_UP_TABLE_HEADER 36u /**< Bytes of a table header. */

/**
 * @brief Writes a definition block in @p table: a header of @p signature, revision 2, OEM "LPLTST" and
 *        table "MADEUPML", then @p length bytes of AML, its checksum made to hold.
 *
 * @param table     The buffer, at least MADE_UP_TABLE_HEADER + @p length bytes.
 * @param signature Four characters.
 * @param aml       The AML.
 * @param length    Bytes of AML.
 * @return The table's header.
 */
extern const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
made_up_definition_block(uint8_t *table, const char *signature, const uint8_t *aml, uint32_t length);

#endif /* KERNEL_TESTS_ACPI_MADE_UP_TABLE_H */
