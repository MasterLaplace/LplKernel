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
 * @file regions.h
 * @brief The made-up tables of kernel/tests/acpi/regions.c, compiled from the ASL sources of
 *        kernel/tests/acpi/fixtures/regions/ (docs/acpi/specification.md, §8.2).
 *
 * Written by tools/acpi/regenerate-fixtures.sh --write, which checks it against its sources when run
 * without an option: a table changes in its source, never here.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-05
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIXTURES_REGIONS_H
#define KERNEL_TESTS_ACPI_FIXTURES_REGIONS_H

#include <stdint.h>

/**
 * @brief MEXT (): the call of the method whose field list keeps extended access and its lock.
 */
static const uint8_t regions_calls_extended_aml[] = {
    0x4Du,
    0x45u,
    0x58u,
    0x54u,
};

/**
 * @brief MNOR (), MCON (), MPFX () and MPFC (): the calls of the methods whose field lists the test refuses
 *        or passes over.
 */
static const uint8_t regions_calls_field_lists_aml[] = {
    0x4Du, 0x4Eu, 0x4Fu, 0x52u, 0x4Du, 0x43u, 0x4Fu, 0x4Eu, 0x4Du, 0x50u, 0x46u, 0x58u, 0x4Du, 0x50u, 0x46u, 0x43u,
};

/**
 * @brief MOVF (): the call of the method whose field list passes 2^32 bits.
 */
static const uint8_t regions_calls_overflow_aml[] = {
    0x4Du,
    0x4Fu,
    0x56u,
    0x46u,
};

/**
 * @brief Field (RAMR, ByteAcc, NoLock, Preserve) { AccessAs (DWordAcc), DW__, 32 }: a method's field list that widens
 *        its units.
 */
static const uint8_t regions_dword_access_field_aml[] = {
    0x5Bu, 0x81u, 0x0Eu, 0x52u, 0x41u, 0x4Du, 0x52u, 0x01u, 0x01u, 0x03u, 0x00u, 0x44u, 0x57u, 0x5Fu, 0x5Fu, 0x20u,
};

/**
 * @brief Field (RAMR, ByteAcc, NoLock, Preserve) { AccessAs (DWordAcc, AttribBytes (4)), DX__, 32 }: a method's unit
 *        of extended access.
 */
static const uint8_t regions_extended_access_field_aml[] = {
    0x5Bu, 0x81u, 0x0Fu, 0x52u, 0x41u, 0x4Du, 0x52u, 0x01u, 0x03u,
    0x03u, 0x0Bu, 0x04u, 0x44u, 0x58u, 0x5Fu, 0x5Fu, 0x20u,
};

/**
 * @brief Field (RAMR, ByteAcc, Lock, WriteAsOnes) { Offset (4), LW__, 4 }: a method's unit that takes the global lock.
 */
static const uint8_t regions_locked_field_aml[] = {
    0x5Bu, 0x81u, 0x0Du, 0x52u, 0x41u, 0x4Du, 0x52u, 0x31u, 0x00u, 0x20u, 0x4Cu, 0x57u, 0x5Fu, 0x5Fu, 0x04u,
};

/**
 * @brief Store (P01_, Local0) then Store (P00_, Local0): page 1 again, then page 0, which 64 later pages evicted.
 */
static const uint8_t regions_reads_pages_again_aml[] = {
    0x70u, 0x50u, 0x30u, 0x31u, 0x5Fu, 0x60u, 0x70u, 0x50u, 0x30u, 0x30u, 0x5Fu, 0x60u,
};

/**
 * @brief OperationRegion (MR__, SystemMemory, Arg0, 2): the region MREG creates at the offset its argument gives.
 */
static const uint8_t regions_region_at_argument_aml[] = {
    0x5Bu, 0x80u, 0x4Du, 0x52u, 0x5Fu, 0x5Fu, 0x00u, 0x68u, 0x0Au, 0x02u,
};

/**
 * @brief OperationRegion (NAMR, SystemMemory, Add (BASE, Zero), One): a region whose offset an expression gives.
 */
static const uint8_t regions_region_at_base_aml[] = {
    0x5Bu, 0x80u, 0x4Eu, 0x41u, 0x4Du, 0x52u, 0x00u, 0x72u, 0x42u, 0x41u, 0x53u, 0x45u, 0x00u, 0x00u, 0x01u,
};

/**
 * @brief The body of MTYP: OperationRegion (MT__, SystemMemory, 0x10000, 4), then Return (ObjectType (MT__)).
 */
static const uint8_t regions_region_object_type_aml[] = {
    0x5Bu, 0x80u, 0x4Du, 0x54u, 0x5Fu, 0x5Fu, 0x00u, 0x0Cu, 0x00u, 0x00u,
    0x01u, 0x00u, 0x0Au, 0x04u, 0xA4u, 0x8Eu, 0x4Du, 0x54u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Return (BK1_): the end of MBNK, which reads a unit of its BankField.
 */
static const uint8_t regions_return_banked_aml[] = {
    0xA4u, 0x42u, 0x4Bu, 0x31u, 0x5Fu,
};

/**
 * @brief Return (DW__): the end of MACC.
 */
static const uint8_t regions_return_dword_unit_aml[] = {
    0xA4u, 0x44u, 0x57u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Return (U2__): the end of MIDX, which reads a unit of its IndexField.
 */
static const uint8_t regions_return_indexed_aml[] = {
    0xA4u, 0x55u, 0x32u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Return (Add (MF0_, MF1_)): the end of MREG, which reads both units of its field.
 */
static const uint8_t regions_return_sum_aml[] = {
    0xA4u, 0x72u, 0x4Du, 0x46u, 0x30u, 0x5Fu, 0x4Du, 0x46u, 0x31u, 0x5Fu, 0x00u,
};

/**
 * @brief If (ECF_) { OperationRegion (SKPR, SystemMemory, Add (BASE, Zero), One) and its field SKF_ }: the branch
 *        the load declares without taking.
 */
static const uint8_t regions_skipped_branch_aml[] = {
    0xA0u, 0x21u, 0x45u, 0x43u, 0x46u, 0x5Fu, 0x5Bu, 0x80u, 0x53u, 0x4Bu, 0x50u, 0x52u,
    0x00u, 0x72u, 0x42u, 0x41u, 0x53u, 0x45u, 0x00u, 0x00u, 0x01u, 0x5Bu, 0x81u, 0x0Bu,
    0x53u, 0x4Bu, 0x50u, 0x52u, 0x01u, 0x53u, 0x4Bu, 0x46u, 0x5Fu, 0x08u,
};

/**
 * @brief Store (MACC, RD__): the table's call of MACC.
 */
static const uint8_t regions_store_access_call_aml[] = {
    0x70u, 0x4Du, 0x41u, 0x43u, 0x43u, 0x52u, 0x44u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Store (CU__, RC__) and Store (CB__, RB__): the reads of the units after each Connection.
 */
static const uint8_t regions_store_connected_units_aml[] = {
    0x70u, 0x43u, 0x55u, 0x5Fu, 0x5Fu, 0x52u, 0x43u, 0x5Fu, 0x5Fu,
    0x70u, 0x43u, 0x42u, 0x5Fu, 0x5Fu, 0x52u, 0x42u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Store (DX__, RX__) and Store (Zero, LW__).
 */
static const uint8_t regions_store_extended_and_locked_aml[] = {
    0x70u, 0x44u, 0x58u, 0x5Fu, 0x5Fu, 0x52u, 0x58u, 0x5Fu, 0x5Fu, 0x70u, 0x00u, 0x4Cu, 0x57u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Store (MIDX, RI__) and Store (MBNK, RB__): the table's calls of the two methods.
 */
static const uint8_t regions_store_index_and_bank_calls_aml[] = {
    0x70u, 0x4Du, 0x49u, 0x44u, 0x58u, 0x52u, 0x49u, 0x5Fu, 0x5Fu,
    0x70u, 0x4Du, 0x42u, 0x4Eu, 0x4Bu, 0x52u, 0x42u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Store (MREG (MADR), R1__) and Store (MREG (MADR), R2__): two calls of MREG at the same offset, the one MADR
 *        names.
 */
static const uint8_t regions_store_two_calls_aml[] = {
    0x70u, 0x4Du, 0x52u, 0x45u, 0x47u, 0x4Du, 0x41u, 0x44u, 0x52u, 0x52u, 0x31u, 0x5Fu, 0x5Fu,
    0x70u, 0x4Du, 0x52u, 0x45u, 0x47u, 0x4Du, 0x41u, 0x44u, 0x52u, 0x52u, 0x32u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Store (MTYP, RT__): the table's call of MTYP.
 */
static const uint8_t regions_store_type_call_aml[] = {
    0x70u, 0x4Du, 0x54u, 0x59u, 0x50u, 0x52u, 0x54u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Store (Zero, ZW__).
 */
static const uint8_t regions_store_zero_to_unit_aml[] = {
    0x70u, 0x00u, 0x5Au, 0x57u, 0x5Fu, 0x5Fu,
};

/**
 * @brief Field (RAMR, ByteAcc, NoLock, WriteAsZeros) { Offset (5), ZW__, 4 }: a method's unit whose update rule writes
 *        zeros.
 */
static const uint8_t regions_write_as_zeros_field_aml[] = {
    0x5Bu, 0x81u, 0x0Du, 0x52u, 0x41u, 0x4Du, 0x52u, 0x41u, 0x00u, 0x28u, 0x5Au, 0x57u, 0x5Fu, 0x5Fu, 0x04u,
};

#endif /* KERNEL_TESTS_ACPI_FIXTURES_REGIONS_H */
