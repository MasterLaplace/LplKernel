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
 * @file mode.h
 * @brief The made-up tables of kernel/tests/acpi/mode.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/mode/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-06
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_MODE_H
#define KERNEL_TESTS_ACPI_FIXTURES_MODE_H

#include <stdint.h>

/**
 * @brief Table-level code that reads SCI_EN, bit 0 of PM1a_CNT at port 0x404, through a SystemIO field and stores
 *        it in SCIS, 2 before it runs.
 */
static const uint8_t mode_table_code_reads_sci_enable_aml[] = {
    0x5Bu, 0x80u, 0x50u, 0x4Du, 0x43u, 0x54u, 0x01u, 0x0Bu, 0x04u, 0x04u, 0x0Au, 0x02u, 0x5Bu, 0x81u,
    0x0Bu, 0x50u, 0x4Du, 0x43u, 0x54u, 0x02u, 0x53u, 0x43u, 0x49u, 0x45u, 0x01u, 0x08u, 0x53u, 0x43u,
    0x49u, 0x53u, 0x0Au, 0x02u, 0x70u, 0x53u, 0x43u, 0x49u, 0x45u, 0x53u, 0x43u, 0x49u, 0x53u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_MODE_H */
