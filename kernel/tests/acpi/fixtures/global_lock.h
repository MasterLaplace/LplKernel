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
 * @file global_lock.h
 * @brief The made-up tables of kernel/tests/acpi/global_lock.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/global_lock/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_GLOBAL_LOCK_H
#define KERNEL_TESTS_ACPI_FIXTURES_GLOBAL_LOCK_H

#include <stdint.h>

/**
 * @brief Methods that acquire `\_GL`: TAKE acquires then releases it and returns what Acquire gave,
 *        HOLD acquires it and keeps it, NEST acquires it twice and releases it twice.
 */
static const uint8_t global_lock_acquire_aml[] = {
    0x14u, 0x1Au, 0x54u, 0x41u, 0x4Bu, 0x45u, 0x00u, 0x70u, 0x5Bu, 0x23u, 0x5Cu, 0x5Fu, 0x47u, 0x4Cu,
    0x5Fu, 0xFFu, 0xFFu, 0x60u, 0x5Bu, 0x27u, 0x5Cu, 0x5Fu, 0x47u, 0x4Cu, 0x5Fu, 0xA4u, 0x60u, 0x14u,
    0x10u, 0x48u, 0x4Fu, 0x4Cu, 0x44u, 0x00u, 0xA4u, 0x5Bu, 0x23u, 0x5Cu, 0x5Fu, 0x47u, 0x4Cu, 0x5Fu,
    0xFFu, 0xFFu, 0x14u, 0x26u, 0x4Eu, 0x45u, 0x53u, 0x54u, 0x00u, 0x5Bu, 0x23u, 0x5Cu, 0x5Fu, 0x47u,
    0x4Cu, 0x5Fu, 0xFFu, 0xFFu, 0x5Bu, 0x23u, 0x5Cu, 0x5Fu, 0x47u, 0x4Cu, 0x5Fu, 0xFFu, 0xFFu, 0x5Bu,
    0x27u, 0x5Cu, 0x5Fu, 0x47u, 0x4Cu, 0x5Fu, 0x5Bu, 0x27u, 0x5Cu, 0x5Fu, 0x47u, 0x4Cu, 0x5Fu,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_GLOBAL_LOCK_H */
