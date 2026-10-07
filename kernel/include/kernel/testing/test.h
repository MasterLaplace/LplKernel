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
 * @file test.h
 * @brief One declaration per kernel test, found by the linker and reported in KTAP.
 *
 * A test file declares its suite once, then each test once, next to nothing else:
 *
 * @code
 * KERNEL_TEST_SUITE(pool_allocator, KERNEL_TEST_STAGE_INITIALIZATION);
 *
 * KERNEL_TEST(double_free_is_refused)
 * {
 *     void *object = kernel_pool_alloc();
 *     kernel_test_check(test, kernel_pool_free(object), "the first free is accepted");
 *     kernel_test_check(test, !kernel_pool_free(object), "the second free of the same object is refused");
 * }
 * @endcode
 *
 * - Registration: the declaration puts a pointer in the `.kernel_tests` section; no list names a
 *   test. Every `.c` of a folder of `kernel/tests/` or `kernel/arch/<arch>/tests/` is built into
 *   debug images only, which always poison memory.
 * - Order: stage, then file, then line, the same on every build path.
 * - Report: KTAP on COM1, one subtest per suite, and each claim that did not hold.
 * - A test that checks nothing fails. kernel_test_skip() says why a test cannot run here.
 * - KERNEL_TEST_MANUAL() tests halt the machine and run only when named in full:
 *   `lpl.test=exceptions.breakpoint`. `lpl.test=heap.*` narrows any run to the other tests.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-07
 **************************************************************************/

#ifndef KERNEL_TESTING_TEST_H
#define KERNEL_TESTING_TEST_H

#if !defined(LPL_KERNEL_ENABLE_SMOKE_TESTS) || !defined(LPL_KERNEL_DEBUG_POISON)
#    error "kernel tests belong to a debug build, with LPL_KERNEL_ENABLE_SMOKE_TESTS and LPL_KERNEL_DEBUG_POISON"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/drivers/serial.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum KernelTestStage
 * @brief The two moments of the boot at which kernel tests run.
 */
typedef enum KernelTestStage {
    KERNEL_TEST_STAGE_INITIALIZATION = 0, /**< Inside kernel_initialize, once the timers and the other CPUs are
                                               up and before the bus is enumerated. */
    KERNEL_TEST_STAGE_BOOTED = 1,         /**< From kernel_main, once every constructor has run and the
                                               read-only sections are protected. */
} KernelTestStage_t;

/**
 * @brief The run of one test: what the test body receives, and what its checks report to.
 */
typedef struct KernelTest KernelTest_t;

/**
 * @struct KernelTestSuite
 * @brief A group of tests: one per file, named once with KERNEL_TEST_SUITE().
 */
typedef struct KernelTestSuite {
    const char *name;        /**< Name of the suite, as KTAP reports it. */
    KernelTestStage_t stage; /**< When every test of the suite runs. */
} KernelTestSuite_t;

/**
 * @struct KernelTestCase
 * @brief One test, as KERNEL_TEST() declares it.
 */
typedef struct KernelTestCase {
    const KernelTestSuite_t *suite;   /**< Suite of the file the test is declared in. */
    const char *name;                 /**< Name of the test within its suite. */
    const char *file;                 /**< File the test is declared in, which orders the suites. */
    uint32_t line;                    /**< Line the test is declared on, which orders the tests of a suite. */
    void (*body)(KernelTest_t *test); /**< The test itself. */
    const char *manual_reason;        /**< Why the test only runs when selected by name, or NULL. */
} KernelTestCase_t;

/**
 * @brief Names the suite of the current file and the stage its tests run at.
 *
 * @param suite_name Identifier of the suite.
 * @param suite_stage A KernelTestStage_t.
 */
#define KERNEL_TEST_SUITE(suite_name, suite_stage)                                                                     \
    static const KernelTestSuite_t kernel_test_suite = {#suite_name, suite_stage}

/**
 * @brief Declares a test of the current file's suite and registers it with the runner.
 *
 * @param test_name Identifier of the test, unique within its file.
 * @param reason    Why the test only runs when selected by name, or NULL.
 */
#define KERNEL_TEST_DECLARE(test_name, reason)                                                                         \
    static void kernel_test_body_##test_name(KernelTest_t *test);                                                      \
    static const KernelTestCase_t kernel_test_case_##test_name = {                                                     \
        &kernel_test_suite, #test_name, __FILE__, __LINE__, kernel_test_body_##test_name, reason};                     \
    static const KernelTestCase_t *const kernel_test_entry_##test_name                                                 \
        __attribute__((used, section(".kernel_tests"))) = &kernel_test_case_##test_name;                               \
    static void kernel_test_body_##test_name(KernelTest_t *test)

/**
 * @brief Declares a test that runs on every debug boot.
 *
 * @param test_name Identifier of the test, unique within its file.
 */
#define KERNEL_TEST(test_name) KERNEL_TEST_DECLARE(test_name, NULL)

/**
 * @brief Declares a test that runs only when the boot command line selects it by name.
 *
 * @param test_name Identifier of the test, unique within its file.
 * @param reason    Why it does not run on every boot, reported with the skip.
 */
#define KERNEL_TEST_MANUAL(test_name, reason) KERNEL_TEST_DECLARE(test_name, reason)

/**
 * @brief Checks one claim of the running test.
 *
 * @details A false claim fails the test and prints it as a KTAP diagnostic. The test goes on
 *          running, so every claim that does not hold is reported, not only the first.
 *
 * @param test      The running test.
 * @param condition Whether the claim holds.
 * @param claim     What the condition proves, written as the sentence it is.
 * @return @p condition, so a test can stop where a later claim would make no sense.
 */
extern bool kernel_test_check(KernelTest_t *test, bool condition, const char *claim);

/**
 * @brief Reports that the running test cannot run here, and why.
 *
 * @details The caller returns right after. Checks made before the skip still count: one that
 *          failed fails the test.
 *
 * @param test   The running test.
 * @param reason Why it cannot run on this machine or with this profile.
 */
extern void kernel_test_skip(KernelTest_t *test, const char *reason);

/**
 * @brief Prints a value the test measured, in decimal, as a KTAP diagnostic.
 *
 * @param test  The running test.
 * @param key   Name of the value, without a space or an equals sign.
 * @param value The value.
 */
extern void kernel_test_measure(KernelTest_t *test, const char *key, uint32_t value);

/**
 * @brief Prints a value the test measured, in hexadecimal, as a KTAP diagnostic.
 *
 * @param test  The running test.
 * @param key   Name of the value, without a space or an equals sign.
 * @param value The value.
 */
extern void kernel_test_measure_hexadecimal(KernelTest_t *test, const char *key, uint32_t value);

/**
 * @brief Prints a remark that does not judge the test, such as a timing over its usual bound.
 *
 * @param test The running test.
 * @param text The remark.
 */
extern void kernel_test_note(KernelTest_t *test, const char *text);

/**
 * @brief Runs every test of one stage and reports it in KTAP.
 *
 * @details The first stage to run prints the KTAP header and the plan, which counts the suites
 *          of both stages. The booted stage ends with the totals.
 *
 * @param stage  The stage that has been reached.
 * @param serial Port the report is written to.
 */
extern void kernel_test_run_stage(KernelTestStage_t stage, Serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_TESTING_TEST_H */
