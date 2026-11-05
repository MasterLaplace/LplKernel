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
 * @file processor_idle.h
 * @brief The made-up tables of kernel/tests/acpi/processor_idle.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/processor_idle/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_PROCESSOR_IDLE_H
#define KERNEL_TESTS_ACPI_FIXTURES_PROCESSOR_IDLE_H

#include <stdint.h>

/**
 * @brief The body of a _STA that counts its calls in \DSTA and returns 0, absent.
 */
static const uint8_t idle_absent_dsta_aml[] = {
    0x75u, 0x5Cu, 0x44u, 0x53u, 0x54u, 0x41u, 0xA4u, 0x00u,
};

/**
 * @brief The body of a _STA that counts its calls in \P1ST and returns 0, absent.
 */
static const uint8_t idle_absent_p1st_aml[] = {
    0x75u, 0x5Cu, 0x50u, 0x31u, 0x53u, 0x54u, 0xA4u, 0x00u,
};

/**
 * @brief The body of a _STA that counts its calls in \D0RD and returns 0, absent.
 */
static const uint8_t idle_counted_absent_aml[] = {
    0x75u, 0x5Cu, 0x44u, 0x30u, 0x52u, 0x44u, 0xA4u, 0x00u,
};

/**
 * @brief Divide (One, Zero), which fails with divide_by_zero.
 */
static const uint8_t idle_divide_by_zero_aml[] = {
    0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief Method (MTH1) {}: a method a package element names.
 */
static const uint8_t idle_empty_method_aml[] = {
    0x14u, 0x06u, 0x4Du, 0x54u, 0x48u, 0x31u, 0x00u,
};

/**
 * @brief The body of Intel's _CST: If (And (PDCB, 0x200)) { Store (MWC1, Index (C1TM, Zero)) }, Store (C6LT,
 *        Index (C6TM, 2)), Store (C7LT, Index (C7TM, 2)), then Return (Package () { 3, C1TM, C6TM, C7TM }): names,
 *        which are read where they point.
 */
static const uint8_t idle_intel_states_method_aml[] = {
    0xA0u, 0x16u, 0x7Bu, 0x50u, 0x44u, 0x43u, 0x42u, 0x0Bu, 0x00u, 0x02u, 0x00u, 0x70u, 0x4Du, 0x57u,
    0x43u, 0x31u, 0x88u, 0x43u, 0x31u, 0x54u, 0x4Du, 0x00u, 0x00u, 0x70u, 0x43u, 0x36u, 0x4Cu, 0x54u,
    0x88u, 0x43u, 0x36u, 0x54u, 0x4Du, 0x0Au, 0x02u, 0x00u, 0x70u, 0x43u, 0x37u, 0x4Cu, 0x54u, 0x88u,
    0x43u, 0x37u, 0x54u, 0x4Du, 0x0Au, 0x02u, 0x00u, 0xA4u, 0x12u, 0x10u, 0x04u, 0x0Au, 0x03u, 0x43u,
    0x31u, 0x54u, 0x4Du, 0x43u, 0x36u, 0x54u, 0x4Du, 0x43u, 0x37u, 0x54u, 0x4Du,
};

/**
 * @brief Noop: the body of a method that does nothing.
 */
static const uint8_t idle_noop_aml[] = {
    0xA3u,
};

/**
 * @brief Notify (\CPU0, 0x81) and Notify (\CPU1, 0x81): the C-states of CPU0 and CPU1 changed.
 */
static const uint8_t idle_notifies_cpu0_and_cpu1_c_states_aml[] = {
    0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x30u, 0x0Au, 0x81u, 0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x31u, 0x0Au, 0x81u,
};

/**
 * @brief Notify (\CPU1, 0x81): the C-states of CPU1 changed.
 */
static const uint8_t idle_notifies_cpu1_c_states_aml[] = {
    0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x31u, 0x0Au, 0x81u,
};

/**
 * @brief Notify (\CPU2, 0x81): the C-states of CPU2 changed.
 */
static const uint8_t idle_notifies_cpu2_c_states_aml[] = {
    0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x32u, 0x0Au, 0x81u,
};

/**
 * @brief Notify (\CPU2, 0x80): a notification of CPU2 that is not 0x81.
 */
static const uint8_t idle_notifies_cpu2_status_aml[] = {
    0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x32u, 0x0Au, 0x80u,
};

/**
 * @brief Name (PKG0, Package (4) {7, NAM1, MTH1}): elements that are an Integer, names and an Uninitialized one.
 */
static const uint8_t idle_package_of_names_aml[] = {
    0x08u, 0x50u, 0x4Bu, 0x47u, 0x30u, 0x12u, 0x0Cu, 0x04u, 0x0Au,
    0x07u, 0x4Eu, 0x41u, 0x4Du, 0x31u, 0x4Du, 0x54u, 0x48u, 0x31u,
};

/**
 * @brief The body of a _STA that counts its calls in \CSTA and returns 0x0F, present.
 */
static const uint8_t idle_present_csta_aml[] = {
    0x75u, 0x5Cu, 0x43u, 0x53u, 0x54u, 0x41u, 0xA4u, 0x0Au, 0x0Fu,
};

/**
 * @brief The body of a _STA that counts its calls in \PSTA and returns 0x0F, present.
 */
static const uint8_t idle_present_psta_aml[] = {
    0x75u, 0x5Cu, 0x50u, 0x53u, 0x54u, 0x41u, 0xA4u, 0x0Au, 0x0Fu,
};

/**
 * @brief Return (Divide (One, Zero)): the body of a _STA that fails.
 */
static const uint8_t idle_return_divide_by_zero_aml[] = {
    0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief The start of a _CST: Increment (\RUNS), Notify (\CPU0, 0x81), then Divide (One, Zero) on its second run.
 */
static const uint8_t idle_runs_then_fails_twice_aml[] = {
    0x75u, 0x5Cu, 0x52u, 0x55u, 0x4Eu, 0x53u, 0x86u, 0x5Cu, 0x43u, 0x50u, 0x55u, 0x30u, 0x0Au, 0x81u, 0xA0u,
    0x0Eu, 0x93u, 0x5Cu, 0x52u, 0x55u, 0x4Eu, 0x53u, 0x0Au, 0x02u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief Store (One, \FLAG): the body of the _INI that makes the _STA above present.
 */
static const uint8_t idle_sets_flag_aml[] = {
    0x70u, 0x01u, 0x5Cu, 0x46u, 0x4Cu, 0x41u, 0x47u,
};

/**
 * @brief The body of a _STA that counts its calls in \BSTA and returns 0x0F once \FLAG is set, 0 before.
 */
static const uint8_t idle_status_by_flag_aml[] = {
    0x75u, 0x5Cu, 0x42u, 0x53u, 0x54u, 0x41u, 0xA0u, 0x09u, 0x5Cu,
    0x46u, 0x4Cu, 0x41u, 0x47u, 0xA4u, 0x0Au, 0x0Fu, 0xA4u, 0x00u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_PROCESSOR_IDLE_H */
