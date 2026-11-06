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
 * @file initialization.h
 * @brief The made-up tables of kernel/tests/acpi/initialization.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/initialization/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_INITIALIZATION_H
#define KERNEL_TESTS_ACPI_FIXTURES_INITIALIZATION_H

#include <stdint.h>

/**
 * @brief Name (_HID, EisaId ("PNP0A08")): a PCI Express root bridge.
 */
static const uint8_t initialization_bridge_identifier_aml[] = {
    0x08u, 0x5Fu, 0x48u, 0x49u, 0x44u, 0x0Cu, 0x41u, 0xD0u, 0x0Au, 0x08u,
};

/**
 * @brief _REG (2, One): the body of CALR, which calls the _REG of its scope.
 */
static const uint8_t initialization_calls_registration_aml[] = {
    0x5Fu, 0x52u, 0x45u, 0x47u, 0x0Au, 0x02u, 0x01u,
};

/**
 * @brief The body of a _STA that counts its calls in \STAC and returns 0x0F.
 */
static const uint8_t initialization_counted_status_aml[] = {
    0x75u, 0x5Cu, 0x53u, 0x54u, 0x41u, 0x43u, 0xA4u, 0x0Au, 0x0Fu,
};

/**
 * @brief The body of MKRL: a PCI_Config region DYNL in the method's own scope.
 */
static const uint8_t initialization_creates_own_region_aml[] = {
    0x5Bu, 0x80u, 0x44u, 0x59u, 0x4Eu, 0x4Cu, 0x02u, 0x00u, 0x0Au, 0x10u,
};

/**
 * @brief The body of MKMM: a SystemMemory region ^DYNM in the scope above the method's.
 */
static const uint8_t initialization_creates_parent_memory_region_aml[] = {
    0x5Bu, 0x80u, 0x5Eu, 0x44u, 0x59u, 0x4Eu, 0x4Du, 0x00u, 0x0Bu, 0x00u, 0x30u, 0x0Au, 0x10u,
};

/**
 * @brief The body of MKRG: a PCI_Config region ^DYNR in the scope above the method's, then Store (\TRAC, \SNAP).
 */
static const uint8_t initialization_creates_parent_region_aml[] = {
    0x5Bu, 0x80u, 0x5Eu, 0x44u, 0x59u, 0x4Eu, 0x52u, 0x02u, 0x00u, 0x0Au, 0x10u,
    0x70u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u, 0x5Cu, 0x53u, 0x4Eu, 0x41u, 0x50u,
};

/**
 * @brief The body of the other MKRG: a PCI_Config region ^DYNR in the scope above the method's, then
 *        Store (One, \SNAP).
 */
static const uint8_t initialization_creates_parent_region_then_marks_aml[] = {
    0x5Bu, 0x80u, 0x5Eu, 0x44u, 0x59u, 0x4Eu, 0x52u, 0x02u, 0x00u,
    0x0Au, 0x10u, 0x70u, 0x01u, 0x5Cu, 0x53u, 0x4Eu, 0x41u, 0x50u,
};

/**
 * @brief The body of MKRG: a PCI_Config region ^DYNR in the scope above the method's.
 */
static const uint8_t initialization_creates_region_above_aml[] = {
    0x5Bu, 0x80u, 0x5Eu, 0x44u, 0x59u, 0x4Eu, 0x52u, 0x02u, 0x00u, 0x0Au, 0x10u,
};

/**
 * @brief The creator's statements: Acquire (\MTXA, 0xFFFF), a PCI_Config region ^DYNR in the scope above, then
 *        Release (\MTXA).
 */
static const uint8_t initialization_creates_region_under_mutex_aml[] = {
    0x5Bu, 0x23u, 0x5Cu, 0x4Du, 0x54u, 0x58u, 0x41u, 0xFFu, 0xFFu, 0x5Bu, 0x80u, 0x5Eu, 0x44u, 0x59u,
    0x4Eu, 0x52u, 0x02u, 0x00u, 0x0Au, 0x10u, 0x5Bu, 0x27u, 0x5Cu, 0x4Du, 0x54u, 0x58u, 0x41u,
};

/**
 * @brief Name (_STA, Buffer (0) {}): a status that reads as 0.
 */
static const uint8_t initialization_empty_status_aml[] = {
    0x08u, 0x5Fu, 0x53u, 0x54u, 0x41u, 0x11u, 0x02u, 0x00u,
};

/**
 * @brief CreateDWordField (Arg3, 0, CDW1): the first statement of the _OSC a recorder writes.
 */
static const uint8_t initialization_first_dword_field_aml[] = {
    0x8Au, 0x6Bu, 0x00u, 0x43u, 0x44u, 0x57u, 0x31u,
};

/**
 * @brief Alias (\INIM, _INI): an _INI that is an Alias of a method elsewhere.
 */
static const uint8_t initialization_initialization_alias_aml[] = {
    0x06u, 0x5Cu, 0x49u, 0x4Eu, 0x49u, 0x4Du, 0x5Fu, 0x49u, 0x4Eu, 0x49u,
};

/**
 * @brief Mutex (MTXA, 1), whose SyncLevel keeps a _REG of level 10 from running.
 */
static const uint8_t initialization_mutex_aml[] = {
    0x5Bu, 0x01u, 0x4Du, 0x54u, 0x58u, 0x41u, 0x01u,
};

/**
 * @brief The body of an _INI: Notify (\NOTE, 0x80).
 */
static const uint8_t initialization_notifies_aml[] = {
    0x86u, 0x5Cu, 0x4Eu, 0x4Fu, 0x54u, 0x45u, 0x0Au, 0x80u,
};

/**
 * @brief Name (_STA, Package (0) {}): a status that is no Integer.
 */
static const uint8_t initialization_package_status_aml[] = {
    0x08u, 0x5Fu, 0x53u, 0x54u, 0x41u, 0x12u, 0x02u, 0x00u,
};

/**
 * @brief The body of READ: Local0 takes FLD0, a loop turns once, and READ returns Local0.
 */
static const uint8_t initialization_reads_field_aml[] = {
    0x70u, 0x46u, 0x4Cu, 0x44u, 0x30u, 0x60u, 0x70u, 0x00u, 0x61u,
    0xA2u, 0x06u, 0x95u, 0x61u, 0x01u, 0x75u, 0x61u, 0xA4u, 0x60u,
};

/**
 * @brief The body of a _REG (2) that traces Arg0 then Arg1, then creates a PCI_Config region ^DYNZ in the scope
 *        above its own.
 */
static const uint8_t initialization_registration_creates_region_aml[] = {
    0x70u, 0x72u, 0x77u, 0x72u, 0x77u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u, 0x0Au, 0x10u,
    0x00u, 0x68u, 0x00u, 0x0Au, 0x10u, 0x00u, 0x69u, 0x00u, 0x5Cu, 0x54u, 0x52u, 0x41u,
    0x43u, 0x5Bu, 0x80u, 0x5Eu, 0x44u, 0x59u, 0x4Eu, 0x5Au, 0x02u, 0x00u, 0x0Au, 0x10u,
};

/**
 * @brief The body of _REG (2): \TRAC takes Arg0 then Arg1 as its two last hexadecimal digits.
 */
static const uint8_t initialization_registration_traces_aml[] = {
    0x70u, 0x72u, 0x77u, 0x72u, 0x77u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u, 0x0Au, 0x10u, 0x00u,
    0x68u, 0x00u, 0x0Au, 0x10u, 0x00u, 0x69u, 0x00u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u,
};

/**
 * @brief Return (Arg3): an _OSC that gives back the Buffer it was given.
 */
static const uint8_t initialization_return_arg3_aml[] = {
    0xA4u,
    0x6Bu,
};

/**
 * @brief Return (One): an _OSC that returns an Integer.
 */
static const uint8_t initialization_return_one_aml[] = {
    0xA4u,
    0x01u,
};

/**
 * @brief Return (Buffer (4) {}): an _OSC that returns a Buffer of one DWORD.
 */
static const uint8_t initialization_return_short_buffer_aml[] = {
    0xA4u, 0x11u, 0x03u, 0x0Au, 0x04u,
};

/**
 * @brief CreateDWordField (Arg3, 4, CDW2): the second statement of the _OSC a recorder writes.
 */
static const uint8_t initialization_second_dword_field_aml[] = {
    0x8Au, 0x6Bu, 0x0Au, 0x04u, 0x43u, 0x44u, 0x57u, 0x32u,
};

/**
 * @brief The body of _PIC (1): \PICM takes Arg0.
 */
static const uint8_t initialization_stores_interrupt_model_aml[] = {
    0x70u, 0x68u, 0x5Cu, 0x50u, 0x49u, 0x43u, 0x4Du,
};

/**
 * @brief The body of a _REG (1): \TRAC takes 0xF as its last hexadecimal digit.
 */
static const uint8_t initialization_trace_f_aml[] = {
    0x70u, 0x72u, 0x77u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u, 0x0Au, 0x10u,
    0x00u, 0x0Au, 0x0Fu, 0x00u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u,
};

/**
 * @brief The body of the _REG of SyncLevel 10: \TRAC takes 1.
 */
static const uint8_t initialization_trace_one_aml[] = {
    0x70u, 0x01u, 0x5Cu, 0x54u, 0x52u, 0x41u, 0x43u,
};

/**
 * @brief Name (_UID, 2).
 */
static const uint8_t initialization_unique_identifier_aml[] = {
    0x08u, 0x5Fu, 0x55u, 0x49u, 0x44u, 0x0Au, 0x02u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_INITIALIZATION_H */
