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
 * @file microbenchmarks.h
 * @brief The made-up tables of kernel/tests/acpi/microbenchmarks.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/microbenchmarks/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_MICROBENCHMARKS_H
#define KERNEL_TESTS_ACPI_FIXTURES_MICROBENCHMARKS_H

#include <stdint.h>

/**
 * @brief OperationRegion (FREG, SystemIO, 0x1000, 0x1000), which the field units of a declaration load lie in.
 */
static const uint8_t microbenchmark_field_region_aml[] = {
    0x5Bu, 0x80u, 0x46u, 0x52u, 0x45u, 0x47u, 0x01u, 0x0Bu, 0x00u, 0x10u, 0x0Bu, 0x00u, 0x10u,
};

/**
 * @brief Name (LAST, Zero), after the Externals of a declaration load.
 */
static const uint8_t microbenchmark_last_name_aml[] = {
    0x08u, 0x4Cu, 0x41u, 0x53u, 0x54u, 0x00u,
};

/**
 * @brief Device (LNKA), which the packages and the Externals of a declaration load name.
 */
static const uint8_t microbenchmark_link_device_aml[] = {
    0x5Bu, 0x82u, 0x05u, 0x4Cu, 0x4Eu, 0x4Bu, 0x41u,
};

/**
 * @brief The loop of window W4: LOOP sets Local0 to 0, then runs
 *        `While (LLess (Local0, 0x03E8)) { Increment (Local0) }` and returns Local0.
 */
static const uint8_t microbenchmark_loop_aml[] = {
    0x14u, 0x14u, 0x4Cu, 0x4Fu, 0x4Fu, 0x50u, 0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
    0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief The polling loop of window W4, shaped like the loops real firmware runs at boot but with no Stall
 *        nor Sleep, so that it counts the interpreter alone: STS, 8 bits of a DWordAcc field of a
 *        SystemMemory region at 0x10000, then POLL: `Local0 = 0; Local2 = 1000; While (Local0 < 1000)
 *        { Local1 = (STS & 0x10); If (Local1 == 0x10) { Break }; Local2--; Local0++ }; Return (Local0 +
 *        Local2)`, which gives 1000 when every turn runs its Decrement and its Increment.
 */
static const uint8_t microbenchmark_polling_aml[] = {
    0x5Bu, 0x80u, 0x50u, 0x52u, 0x45u, 0x47u, 0x00u, 0x0Cu, 0x00u, 0x00u, 0x01u, 0x00u, 0x0Au, 0x04u, 0x5Bu,
    0x81u, 0x0Bu, 0x50u, 0x52u, 0x45u, 0x47u, 0x03u, 0x53u, 0x54u, 0x53u, 0x5Fu, 0x08u, 0x14u, 0x2Du, 0x50u,
    0x4Fu, 0x4Cu, 0x4Cu, 0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x0Bu, 0xE8u, 0x03u, 0x62u, 0xA2u, 0x19u, 0x95u,
    0x60u, 0x0Bu, 0xE8u, 0x03u, 0x7Bu, 0x53u, 0x54u, 0x53u, 0x5Fu, 0x0Au, 0x10u, 0x61u, 0xA0u, 0x06u, 0x93u,
    0x61u, 0x0Au, 0x10u, 0xA5u, 0x76u, 0x62u, 0x75u, 0x60u, 0xA4u, 0x72u, 0x60u, 0x62u, 0x00u,
};

/**
 * @brief Return (Arg0): the body of each method of a declaration load, after its flags.
 */
static const uint8_t microbenchmark_return_arg0_aml[] = {
    0xA4u,
    0x68u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_MICROBENCHMARKS_H */
