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
 * @file field_cases.h
 * @brief Bit-exact field cases: one unit of a SystemMemory field read, written and read back at a
 *        given offset, length, access width and update rule, against an independent bit model.
 *
 * The host runs every case, ring 0 a stratified sample, through the simulated host and through the
 * machine's memory (docs/acpi/specification.md, §8.2).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-25
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_FIELD_CASES_H
#define KERNEL_TESTS_ACPI_FIELD_CASES_H

#include "made_up_table.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>

#define FIELD_CASES_BYTES 24u /**< Bytes of memory the cases reach, and some past them that must not move. */

/**
 * @brief One case: where the unit lies, how wide its datums are, and what a partial write keeps.
 */
typedef struct FieldCase {
    uint8_t access;     /**< AccessType: AnyAcc, ByteAcc, WordAcc, DWordAcc or QWordAcc. */
    uint8_t update;     /**< UpdateRule: Preserve, WriteAsOnes or WriteAsZeros. */
    uint8_t bit_offset; /**< Its first bit, 0 to 63. */
    uint8_t bit_length; /**< Its length in bits, 1 to 64. */
} FieldCase_t;

/**
 * @brief The memory under the cases' region, reached apart from the field engine.
 */
typedef struct FieldCasesMemory {
    void *context;                                                      /**< What the calls are given back. */
    void (*fill)(void *context, const uint8_t *bytes, uint32_t length); /**< Writes the first bytes of the region. */
    uint8_t (*byte)(void *context, uint32_t offset);                    /**< Reads a byte of the region. */
} FieldCasesMemory_t;

/**
 * @brief Loads a table that declares a SystemMemory region of FIELD_CASES_BYTES at
 *        @p physical_address and one unit over it, whose layout each case then gives.
 *
 * @param test             The test.
 * @param load             Filled; closed by the caller.
 * @param host             The host the region is reached through.
 * @param table            The buffer the table is written in, MADE_UP_TABLE_HEADER + 64 bytes at least.
 * @param physical_address Where the region lies.
 * @param out_unit         Receives the unit's node.
 * @return false when the load could not be opened or the unit was not built.
 */
extern bool field_cases_open(KernelTest_t *test, AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                             const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint8_t *table,
                             uint64_t physical_address, uint32_t *out_unit);

/**
 * @brief Runs one case: fills the memory with a fixed pattern, reads the unit, writes it, checks the
 *        memory, reads it back, each against the bit model, and folds what it saw into @p fold.
 *
 * @param load   The load field_cases_open filled.
 * @param unit   The unit's node.
 * @param memory The memory under the region.
 * @param field  The case.
 * @param fold   An FNV-1a fold, updated.
 * @return Whether every result agreed with the model.
 */
extern bool field_cases_run(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t unit,
                            const FieldCasesMemory_t *memory, const FieldCase_t *field, uint32_t *fold);

/**
 * @brief The stratified sample ring 0 runs, 432 cases: ByteAcc to QWordAcc, each update rule, six
 *        offsets and six lengths at the edges of a byte, of a datum and of 64 bits.
 *
 * @param index The case, from 0.
 * @param out_case Receives it.
 * @return false past the last case.
 */
extern bool field_cases_sample(uint32_t index, FieldCase_t *out_case);

#endif /* KERNEL_TESTS_ACPI_FIELD_CASES_H */
