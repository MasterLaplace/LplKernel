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
 * @file common.h
 * @brief The made-up tables more than one suite of kernel/tests/acpi/ uses, compiled from the ASL
 *        sources of kernel/tests/acpi/fixtures/common/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-06
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_COMMON_H
#define KERNEL_TESTS_ACPI_FIXTURES_COMMON_H

#include <stdint.h>

/**
 * @brief Divide (One, Zero): the body of a method that fails with divide_by_zero.
 */
static const uint8_t common_divide_by_zero_aml[] = {
    0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief Noop: the body of a method that does nothing, a whole table, or one Noop of a run.
 */
static const uint8_t common_noop_aml[] = {
    0xA3u,
};

/**
 * @brief Return (Divide (One, Zero)): the body of a method that fails with divide_by_zero, returning its quotient.
 */
static const uint8_t common_return_divide_by_zero_aml[] = {
    0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_COMMON_H */
