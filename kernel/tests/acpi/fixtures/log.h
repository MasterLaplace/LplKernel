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
 * @file log.h
 * @brief The made-up tables of kernel/tests/acpi/log.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/log/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-06
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_LOG_H
#define KERNEL_TESTS_ACPI_FIXTURES_LOG_H

#include <stdint.h>

/**
 * @brief An endless table-level loop, then AFTR, One.
 */
static const uint8_t log_endless_table_level_loop_aml[] = {
    0xA2u, 0x03u, 0x01u, 0xA3u, 0x08u, 0x41u, 0x46u, 0x54u, 0x52u, 0x01u,
};

/**
 * @brief In DEV0, MTH0 stores Revision, which §5.2 leaves unimplemented, and MTH1 divides One by Zero.
 */
static const uint8_t log_faults_in_methods_aml[] = {
    0x5Bu, 0x82u, 0x1Eu, 0x44u, 0x45u, 0x56u, 0x30u, 0x14u, 0x0Au, 0x4Du, 0x54u, 0x48u, 0x30u, 0x00u, 0x70u, 0x5Bu,
    0x30u, 0x60u, 0x14u, 0x0Du, 0x4Du, 0x54u, 0x48u, 0x31u, 0x00u, 0x70u, 0x78u, 0x01u, 0x00u, 0x61u, 0x62u, 0x60u,
};

/**
 * @brief M009 loops for ever, and table-level code calls it, then loops twice, the first loop by Continue, before
 *        AFTR, One.
 */
static const uint8_t log_loops_the_budget_stops_aml[] = {
    0x14u, 0x0Au, 0x4Du, 0x30u, 0x30u, 0x39u, 0x00u, 0xA2u, 0x03u, 0x01u, 0xA3u, 0x4Du, 0x30u, 0x30u, 0x39u,
    0xA2u, 0x03u, 0x01u, 0x9Fu, 0xA2u, 0x03u, 0x01u, 0xA3u, 0x08u, 0x41u, 0x46u, 0x54u, 0x52u, 0x01u,
};

/**
 * @brief Declares CLEN, One: a table whose load meets no fault.
 */
static const uint8_t log_name_clen_aml[] = {
    0x08u, 0x43u, 0x4Cu, 0x45u, 0x4Eu, 0x01u,
};

/**
 * @brief Declares DEEP, One: the Name past the ladder that runs a table's code out of blocks.
 */
static const uint8_t log_name_deep_aml[] = {
    0x08u, 0x44u, 0x45u, 0x45u, 0x50u, 0x01u,
};

/**
 * @brief Declares DNAM, One.
 */
static const uint8_t log_name_dnam_aml[] = {
    0x08u, 0x44u, 0x4Eu, 0x41u, 0x4Du, 0x01u,
};

/**
 * @brief Declares LDED, 0x2A: the table Load reads.
 */
static const uint8_t log_name_lded_aml[] = {
    0x08u, 0x4Cu, 0x44u, 0x45u, 0x44u, 0x0Au, 0x2Au,
};

/**
 * @brief Declares LDED, One: the table Load would read.
 */
static const uint8_t log_name_lded_one_aml[] = {
    0x08u, 0x4Cu, 0x44u, 0x45u, 0x44u, 0x01u,
};

/**
 * @brief Declares SNAM, One.
 */
static const uint8_t log_name_snam_aml[] = {
    0x08u, 0x53u, 0x4Eu, 0x41u, 0x4Du, 0x01u,
};

/**
 * @brief Stores Revision, which §5.2 leaves unimplemented, to Local0 in table-level code.
 */
static const uint8_t log_revision_at_table_level_aml[] = {
    0x70u,
    0x5Bu,
    0x30u,
    0x60u,
};

/**
 * @brief Stores 0x2A to Debug once.
 */
static const uint8_t log_store_to_debug_once_aml[] = {
    0x70u, 0x0Au, 0x2Au, 0x5Bu, 0x31u,
};

/**
 * @brief Stores to Debug an Integer, a String whose space the line writes as _, and a Buffer of two bytes.
 */
static const uint8_t log_stores_to_debug_aml[] = {
    0x70u, 0x0Au, 0x2Au, 0x5Bu, 0x31u, 0x70u, 0x0Du, 0x48u, 0x69u, 0x20u, 0x74u, 0x68u, 0x65u, 0x72u,
    0x65u, 0x00u, 0x5Bu, 0x31u, 0x70u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u, 0xFEu, 0x5Bu, 0x31u,
};

/**
 * @brief Stores to Debug a Buffer longer than a line shows, a Package, a reference to PKG0 and an empty String.
 */
static const uint8_t log_stores_to_debug_of_each_kind_aml[] = {
    0x08u, 0x50u, 0x4Bu, 0x47u, 0x30u, 0x12u, 0x03u, 0x01u, 0x01u, 0x70u, 0x11u, 0x03u, 0x0Au,
    0x14u, 0x5Bu, 0x31u, 0x70u, 0x12u, 0x05u, 0x02u, 0x01u, 0x0Au, 0x02u, 0x5Bu, 0x31u, 0x70u,
    0x71u, 0x50u, 0x4Bu, 0x47u, 0x30u, 0x5Bu, 0x31u, 0x70u, 0x0Du, 0x00u, 0x5Bu, 0x31u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_LOG_H */
