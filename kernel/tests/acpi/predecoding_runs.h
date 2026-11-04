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
 * @file predecoding_runs.h
 * @brief A made-up method run with the records of pre-decoded loops off and on, and what it left, folded,
 *        for the tests that compare the two runs.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-03
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_PREDECODING_RUNS_H
#define KERNEL_TESTS_ACPI_PREDECODING_RUNS_H

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>

#define PREDECODING_NO_ARGUMENT ~0ull /**< What a method called without an argument is given. */

/**
 * @brief What one evaluation of a made-up method left: its fold, and what the records ran and handed
 *        back.
 */
typedef struct PredecodingRun {
    uint32_t fold;        /**< The status, the result and everything the load counted, as FNV-1a. */
    uint32_t records_run; /**< Statements and predicates run from records. */
    uint32_t declined;    /**< Statements the records handed back, over every reason. */
    uint32_t flushes;     /**< Times the cache was emptied. */
    uint32_t built;       /**< Records built. */
} PredecodingRun_t;

/**
 * @brief The load the runs use, open between predecoding_open and predecoding_close.
 */
extern AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t predecoding_load;

/**
 * @brief Opens the load on a simulated host, with no table, records on when @p predecodes holds.
 *
 * @param test       The test, which checks that the namespace opens.
 * @param predecodes Whether loops that loop run from records.
 * @return false when the namespace did not open.
 */
extern bool predecoding_open_empty(KernelTest_t *test, bool predecodes);

/**
 * @brief Opens the load on a simulated host, records on or off as the boot line says, and declares @p aml as a
 *        DSDT of revision 2.
 *
 * @param test   The test, which checks that the namespace opens.
 * @param aml    The DSDT's AML.
 * @param length Its length.
 * @return false when the namespace did not open.
 */
extern bool predecoding_open_booted(KernelTest_t *test, const uint8_t *aml, uint32_t length);

/**
 * @brief Declares @p aml in the open load as a table of @p signature and @p revision, written in @p table,
 *        then decodes the bodies of its methods.
 *
 * @param table     A buffer of MADE_UP_TABLE_HEADER bytes more than @p length, kept while the load is open.
 * @param signature The table's signature, as "DSDT" or "SSDT".
 * @param revision  Its revision.
 * @param aml       Its AML.
 * @param length    Its length.
 */
extern void predecoding_declare(uint8_t *table, const char *signature, uint8_t revision, const uint8_t *aml,
                                uint32_t length);

/**
 * @brief Opens the load on a simulated host, records on when @p predecodes holds, and declares @p aml as a
 *        DSDT of @p revision.
 *
 * @param test       The test, which checks that the namespace opens.
 * @param revision   The DSDT's revision.
 * @param aml        The DSDT's AML.
 * @param length     Its length.
 * @param predecodes Whether loops that loop run from records.
 * @return false when the namespace did not open.
 */
extern bool predecoding_open(KernelTest_t *test, uint8_t revision, const uint8_t *aml, uint32_t length,
                             bool predecodes);

/**
 * @brief Closes the load and its simulated host.
 */
extern void predecoding_close(void);

/**
 * @brief Evaluates @p path in the open load, with one Integer argument unless @p argument is
 *        PREDECODING_NO_ARGUMENT, and folds what it left: its status and result, everything the
 *        evaluations counted, the namespace's lookups, probes and nodes, the heap's objects, the field
 *        accesses and the namespace itself; then what the records ran and handed back.
 *
 * @param path     An absolute path.
 * @param argument The argument, or PREDECODING_NO_ARGUMENT.
 * @param out_run  Receives the fold and the counts.
 */
extern void predecoding_evaluate(const char *path, uint64_t argument, PredecodingRun_t *out_run);

/**
 * @brief Loads @p aml as a DSDT of @p revision, evaluates @p path and folds what it left, records on when
 *        @p predecodes holds.
 *
 * @return false when the namespace did not open.
 */
extern bool predecoding_run(KernelTest_t *test, const uint8_t *aml, uint32_t length, uint8_t revision, const char *path,
                            uint64_t argument, bool predecodes, PredecodingRun_t *out_run);

#endif /* KERNEL_TESTS_ACPI_PREDECODING_RUNS_H */
