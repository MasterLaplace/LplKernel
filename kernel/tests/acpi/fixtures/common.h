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
 * @brief Device (ZDEV) {}: an empty device a table declares.
 */
static const uint8_t common_device_zdev_aml[] = {
    0x5Bu, 0x82u, 0x05u, 0x5Au, 0x44u, 0x45u, 0x56u,
};

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
 * @brief Notify (\CPU1, 0x81): the C-states of CPU1 changed.
 */
static const uint8_t common_notifies_cpu1_c_states_aml[] = {
    0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x31u, 0x0Au, 0x81u,
};

/**
 * @brief RES0, an Integer 0.
 */
static const uint8_t common_result_zero_aml[] = {
    0x08u, 0x52u, 0x45u, 0x53u, 0x30u, 0x00u,
};

/**
 * @brief Return (Divide (One, Zero)): the body of a method that fails with divide_by_zero, returning its quotient.
 */
static const uint8_t common_return_divide_by_zero_aml[] = {
    0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief Return (\NOPE), a name no table declares: the body of a method that fails.
 */
static const uint8_t common_return_missing_name_aml[] = {
    0xA4u, 0x5Cu, 0x4Eu, 0x4Fu, 0x50u, 0x45u,
};

/**
 * @brief Return (One).
 */
static const uint8_t common_return_one_aml[] = {
    0xA4u,
    0x01u,
};

/**
 * @brief Store (One, \FLAG): the body of an _INI that makes present a _STA that reads \FLAG.
 */
static const uint8_t common_sets_flag_aml[] = {
    0x70u, 0x01u, 0x5Cu, 0x46u, 0x4Cu, 0x41u, 0x47u,
};

/**
 * @brief The body of a _STA that counts its calls in \BSTA and returns 0x0F once \FLAG is set, 0 before.
 */
static const uint8_t common_status_by_flag_aml[] = {
    0x75u, 0x5Cu, 0x42u, 0x53u, 0x54u, 0x41u, 0xA0u, 0x09u, 0x5Cu,
    0x46u, 0x4Cu, 0x41u, 0x47u, 0xA4u, 0x0Au, 0x0Fu, 0xA4u, 0x00u,
};

/**
 * @brief Store (Add (Multiply (Add (Multiply (\TRAC, 0x10), Arg0), 0x10), Arg1), \TRAC): the body of a _REG (2), \TRAC
 *        taking Arg0 then Arg1 as its two last hexadecimal digits.
 */
static const uint8_t common_traces_two_arguments_aml[] = {
    0x70u, 0x72u, 0x77u, 0x72u, 0x77u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u, 0x0Au, 0x10u, 0x00u,
    0x68u, 0x00u, 0x0Au, 0x10u, 0x00u, 0x69u, 0x00u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u,
};

/**
 * @brief The loop of window W4: LOOP sets Local0 to 0, then runs
 *        `While (LLess (Local0, 0x03E8)) { Increment (Local0) }` and returns Local0.
 */
static const uint8_t common_w4_loop_aml[] = {
    0x14u, 0x14u, 0x4Cu, 0x4Fu, 0x4Fu, 0x50u, 0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
    0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_COMMON_H */
