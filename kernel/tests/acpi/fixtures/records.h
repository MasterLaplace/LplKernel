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
 * @file records.h
 * @brief The made-up tables of kernel/tests/acpi/records.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/records/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-06
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_RECORDS_H
#define KERNEL_TESTS_ACPI_FIXTURES_RECORDS_H

#include <stdint.h>

/**
 * @brief The bytes a load decodes: INT_ an integer the declaration pass builds, BUF_ a Buffer it keeps in its table,
 *        its initialiser stepped over, DEV_ a Device whose body the interpreter hands back to the declaration pass, and
 *        LOOP a method whose While loops three times, so that its statements run from records.
 */
static const uint8_t records_decoded_bytes_aml[] = {
    0x08u, 0x49u, 0x4Eu, 0x54u, 0x5Fu, 0x0Au, 0x05u, 0x08u, 0x42u, 0x55u, 0x46u, 0x5Fu, 0x11u,
    0x05u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x5Bu, 0x82u, 0x0Bu, 0x44u, 0x45u, 0x56u, 0x5Fu, 0x08u,
    0x5Fu, 0x41u, 0x44u, 0x52u, 0x00u, 0x14u, 0x13u, 0x4Cu, 0x4Fu, 0x4Fu, 0x50u, 0x00u, 0x70u,
    0x00u, 0x60u, 0xA2u, 0x07u, 0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief One object of each kind a result holds: INT_ an Integer, STR_ a String, BUF_ a Buffer, PKG_ a Package of an
 *        Integer, a String, a nested Package and a name, DEV_ a Device, which holds no data, and MTH_ a method that
 *        returns INT_ plus one.
 */
static const uint8_t records_every_kind_aml[] = {
    0x08u, 0x49u, 0x4Eu, 0x54u, 0x5Fu, 0x0Cu, 0xEFu, 0xCDu, 0xABu, 0x89u, 0x08u, 0x53u, 0x54u, 0x52u,
    0x5Fu, 0x0Du, 0x61u, 0x62u, 0x00u, 0x08u, 0x42u, 0x55u, 0x46u, 0x5Fu, 0x11u, 0x06u, 0x0Au, 0x03u,
    0x01u, 0x02u, 0x03u, 0x08u, 0x50u, 0x4Bu, 0x47u, 0x5Fu, 0x12u, 0x10u, 0x04u, 0x0Au, 0x05u, 0x0Du,
    0x63u, 0x00u, 0x12u, 0x04u, 0x01u, 0x0Au, 0x06u, 0x49u, 0x4Eu, 0x54u, 0x5Fu, 0x5Bu, 0x82u, 0x0Bu,
    0x44u, 0x45u, 0x56u, 0x5Fu, 0x08u, 0x5Fu, 0x41u, 0x44u, 0x52u, 0x00u, 0x14u, 0x0Eu, 0x4Du, 0x54u,
    0x48u, 0x5Fu, 0x00u, 0xA4u, 0x72u, 0x49u, 0x4Eu, 0x54u, 0x5Fu, 0x01u, 0x00u,
};

/**
 * @brief PKG_ a Package of a Buffer of four zeros, whose bytes, without the element's type, fold as those of a
 *        Package of the Integer 4.
 */
static const uint8_t records_package_of_a_buffer_aml[] = {
    0x08u, 0x50u, 0x4Bu, 0x47u, 0x5Fu, 0x12u, 0x0Au, 0x01u, 0x11u, 0x07u, 0x0Au, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief PKG_ a Package of the Integer 4, whose bytes, without the element's type, fold as those of a Package of a
 *        Buffer of four zeros.
 */
static const uint8_t records_package_of_an_integer_aml[] = {
    0x08u, 0x50u, 0x4Bu, 0x47u, 0x5Fu, 0x12u, 0x04u, 0x01u, 0x0Au, 0x04u,
};

/**
 * @brief PKG_ a Package of two elements given one initialiser, the Integer 4: its second element is Uninitialized.
 */
static const uint8_t records_package_padded_aml[] = {
    0x08u, 0x50u, 0x4Bu, 0x47u, 0x5Fu, 0x12u, 0x04u, 0x02u, 0x0Au, 0x04u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_RECORDS_H */
