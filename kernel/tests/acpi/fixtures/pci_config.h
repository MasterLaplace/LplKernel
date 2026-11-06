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
 * @file pci_config.h
 * @brief The made-up tables of kernel/tests/acpi/pci_config.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/pci_config/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_PCI_CONFIG_H
#define KERNEL_TESTS_ACPI_FIXTURES_PCI_CONFIG_H

#include <stdint.h>

/**
 * @brief Store (Add (One, Add (2, \PCI0.DEV_.VID_)), \V2__): a read of the vendor field nested two operators deep.
 */
static const uint8_t pci_adds_vendor_aml[] = {
    0x70u, 0x72u, 0x01u, 0x72u, 0x0Au, 0x02u, 0x5Cu, 0x2Fu, 0x03u, 0x50u, 0x43u, 0x49u, 0x30u, 0x44u,
    0x45u, 0x56u, 0x5Fu, 0x56u, 0x49u, 0x44u, 0x5Fu, 0x00u, 0x00u, 0x5Cu, 0x56u, 0x32u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Return (_BBN): the body of a _BBN that calls itself.
 */
static const uint8_t pci_calls_itself_aml[] = {
    0xA4u, 0x5Fu, 0x42u, 0x42u, 0x4Eu,
};

/**
 * @brief Name (_CID, Package (1) {"PNP0A08"}): a compatible identifier of a PCI Express root bridge.
 */
static const uint8_t pci_cid_pnp0a08_aml[] = {
    0x08u, 0x5Fu, 0x43u, 0x49u, 0x44u, 0x12u, 0x0Bu, 0x01u, 0x0Du,
    0x50u, 0x4Eu, 0x50u, 0x30u, 0x41u, 0x30u, 0x38u, 0x00u,
};

/**
 * @brief Name (_HID, "PNP0A08"): a PCI Express root bridge.
 */
static const uint8_t pci_express_root_hid_aml[] = {
    0x08u, 0x5Fu, 0x48u, 0x49u, 0x44u, 0x0Du, 0x50u, 0x4Eu, 0x50u, 0x30u, 0x41u, 0x30u, 0x38u, 0x00u,
};

/**
 * @brief Name (_HID, "ACPI0016"): a CXL host bridge.
 */
static const uint8_t pci_hid_acpi0016_aml[] = {
    0x08u, 0x5Fu, 0x48u, 0x49u, 0x44u, 0x0Du, 0x41u, 0x43u, 0x50u, 0x49u, 0x30u, 0x30u, 0x31u, 0x36u, 0x00u,
};

/**
 * @brief Load (\TBLR, Local0): the body of a _BBN that loads a table.
 */
static const uint8_t pci_loads_table_aml[] = {
    0x5Bu, 0x20u, 0x5Cu, 0x54u, 0x42u, 0x4Cu, 0x52u, 0x60u,
};

/**
 * @brief Load (\TBLR, Local0) then Return (Zero): the body of a _BBN that loads a table.
 */
static const uint8_t pci_loads_table_and_returns_aml[] = {
    0x5Bu, 0x20u, 0x5Cu, 0x54u, 0x42u, 0x4Cu, 0x52u, 0x60u, 0xA4u, 0x00u,
};

/**
 * @brief Store (\PCI0.DEV_.VID_, \V1__): a read of the vendor field of a PCI_Config region.
 */
static const uint8_t pci_reads_vendor_aml[] = {
    0x70u, 0x5Cu, 0x2Fu, 0x03u, 0x50u, 0x43u, 0x49u, 0x30u, 0x44u, 0x45u, 0x56u,
    0x5Fu, 0x56u, 0x49u, 0x44u, 0x5Fu, 0x5Cu, 0x56u, 0x31u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Return (0x00030000): the body of an _ADR, device 3 function 0.
 */
static const uint8_t pci_return_address_aml[] = {
    0xA4u, 0x0Cu, 0x00u, 0x00u, 0x03u, 0x00u,
};

/**
 * @brief Return (BUSN): the body of an attach method that returns the bus number a Name holds.
 */
static const uint8_t pci_return_bus_aml[] = {
    0xA4u, 0x42u, 0x55u, 0x53u, 0x4Eu,
};

/**
 * @brief Return (Divide (One, Zero)): the body of an attach method that fails with divide_by_zero.
 */
static const uint8_t pci_return_divide_by_zero_aml[] = {
    0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u,
};

/**
 * @brief Return (\NOPE), a name no table declares: the body of a method that fails.
 */
static const uint8_t pci_return_missing_name_aml[] = {
    0xA4u, 0x5Cu, 0x4Eu, 0x4Fu, 0x50u, 0x45u,
};

/**
 * @brief Store (\FALT, \V2__): the statement of a loaded table that calls a method that fails.
 */
static const uint8_t pci_stores_failing_call_aml[] = {
    0x70u, 0x5Cu, 0x46u, 0x41u, 0x4Cu, 0x54u, 0x5Cu, 0x56u, 0x32u, 0x5Fu, 0x5Fu,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_PCI_CONFIG_H */
