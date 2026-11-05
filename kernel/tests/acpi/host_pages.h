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
 * @file host_pages.h
 * @brief The contract every host keeps for the address space it reserves: reserve_pages, commit_pages and
 *        release_reservation, checked on the simulated host and on the machine's (docs/acpi/specification.md, §3.3).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_HOST_PAGES_H
#define KERNEL_TESTS_ACPI_HOST_PAGES_H

#include <kernel/acpi/host.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>

#define HOST_PAGES_RESERVED 4u /**< Pages the contract reserves. */

/**
 * @brief Checks a host's reservations: four pages reserved; two committed from the start, then
 *        the fourth, each page written and read back at its first and last byte; the third committed after them, so
 *        that a commit never moves the pages committed before; the reservation released, all four counted as
 *        committed; then a commit at the released address refused.
 *
 * @param test The test that checks it.
 * @param host The host.
 * @return Whether every step held.
 */
extern bool host_pages_keep_their_contract(KernelTest_t *test,
                                           const AdvancedConfigurationAndPowerInterfaceHost_t *host);

#endif /* KERNEL_TESTS_ACPI_HOST_PAGES_H */
