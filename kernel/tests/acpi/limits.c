#include "fixtures/limits.h"
#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_limits, KERNEL_TEST_STAGE_INITIALIZATION);

#define LIMITS_AML_CAPACITY 16384u /**< Bytes of AML the deepest made-up table holds. */

static uint8_t limits_aml[LIMITS_AML_CAPACITY];
static uint8_t limits_table[MADE_UP_TABLE_HEADER + LIMITS_AML_CAPACITY];
static uint8_t limits_second_table[MADE_UP_TABLE_HEADER + 64u];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t limits_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t limits_load;

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t limits_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&limits_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief How many runs failed with @p status.
 */
static uint32_t limits_failed(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status)
{
    return limits_load.evaluation.statuses[status];
}

/**
 * @brief Checks that the failed runs left the evaluation's stacks empty, then closes the load.
 */
static void limits_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &limits_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          limits_load.temporary_count == 0u,
                      "a failed run leaves no frame, value, operator, block or temporary node behind");
    kernel_test_check(test, limits_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    advanced_configuration_and_power_interface_machine_language_close(&limits_load);
}

/**
 * @brief Writes, before @p end, the PkgLength of @p content bytes that follow it.
 *
 * @return Where the PkgLength starts.
 */
static uint32_t limits_package_length(uint32_t end, uint32_t content)
{
    uint8_t encoded[MADE_UP_AML_PACKAGE_LENGTH_BYTES];
    const uint32_t bytes = made_up_aml_encode_package_length(content, true, encoded);

    memcpy(limits_aml + end - bytes, encoded, bytes);
    return end - bytes;
}

/**
 * @brief Writes a ladder of @p depth If and Else at the end of the AML buffer, from the inside out:
 *        level k returns k when Arg0 is k, the innermost Else returns Ones. Its bytes are written at run time, from
 *        raw ones: the depth is a parameter.
 *
 * @return Where the ladder starts.
 */
static uint32_t limits_ladder(uint32_t depth)
{
    uint32_t start = LIMITS_AML_CAPACITY - 2u;

    limits_aml[start] = 0xA4u;
    limits_aml[start + 1u] = 0xFFu;
    for (uint32_t level = depth; level > 0u; --level)
    {
        const uint8_t body[] = {
            0x93u,
            0x68u,
            0x0Bu,
            (uint8_t) (level - 1u),
            (uint8_t) ((level - 1u) >> 8u),
            0xA4u,
            0x0Bu,
            (uint8_t) (level - 1u),
            (uint8_t) ((level - 1u) >> 8u),
        };

        start = limits_package_length(start, LIMITS_AML_CAPACITY - start);
        limits_aml[--start] = 0xA1u;
        start -= (uint32_t) sizeof(body);
        memcpy(limits_aml + start, body, sizeof(body));
        start = limits_package_length(start, (uint32_t) sizeof(body));
        limits_aml[--start] = 0xA0u;
    }
    return start;
}

/**
 * @brief Loads a table that declares RES_ as 5, a method M008 of one argument whose body is a ladder
 *        of @p depth, and stores M008 (@p depth - 1) to RES_, which reaches its deepest level. The Store is raw
 *        bytes: its constant is @p depth - 1.
 */
static bool limits_run_ladder(KernelTest_t *test, uint32_t depth)
{
    const uint8_t store[] = {0x70u, 'M', '0', '0', '8', 0x0Bu, (uint8_t) (depth - 1u), (uint8_t) ((depth - 1u) >> 8u),
                             'R',   'E', 'S', '_'};
    uint32_t start = limits_ladder(depth);

    limits_aml[--start] = 0x01u;
    start -= 4u;
    memcpy(limits_aml + start, "M008", 4u);
    start = limits_package_length(start, LIMITS_AML_CAPACITY - start);
    limits_aml[--start] = 0x14u;
    start -= (uint32_t) sizeof(limits_result_five_aml);
    memcpy(limits_aml + start, limits_result_five_aml, sizeof(limits_result_five_aml));
    memmove(limits_aml, limits_aml + start, LIMITS_AML_CAPACITY - start);
    memcpy(limits_aml + (LIMITS_AML_CAPACITY - start), store, sizeof(store));
    return made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_aml,
                              LIMITS_AML_CAPACITY - start + (uint32_t) sizeof(store));
}

/**
 * @brief An evaluation holds 64 frames, the table's own included: a recursion 63 calls deep returns,
 *        one call deeper fails with limit_frames (§6.1).
 */
KERNEL_TEST(a_recursion_past_64_frames_is_refused)
{
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_recursion_aml,
                            sizeof(limits_recursion_aml)))
        return;
    kernel_test_measure(test, "frames_peak", limits_load.evaluation.frames_peak);
    kernel_test_check(test, limits_integer("RES0") == 0u, "M007 (62) returns through 64 frames");
    kernel_test_check(
        test,
        limits_integer("RES1") == 5u &&
            limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FRAMES) == 1u,
        "M007 (63) would need 65 and fails with limit_frames");
    limits_close(test);
}

/**
 * @brief An ElseIf ladder 86 levels deep, which real firmware ships, runs; one of 510 levels fills the
 *        512 blocks of §6.1 exactly, and one of 511 or 600 levels fails with limit_blocks.
 */
KERNEL_TEST(an_else_if_ladder_runs_up_to_the_block_limit)
{
    if (!limits_run_ladder(test, 86u))
        return;
    kernel_test_check(test, limits_integer("RES_") == 85u && limits_load.evaluation.failures == 0u,
                      "the 86-deep ladder answers 85 from its deepest level");
    limits_close(test);
    if (!limits_run_ladder(test, 510u))
        return;
    kernel_test_check(
        test, limits_integer("RES_") == 509u && limits_load.evaluation.failures == 0u,
        "510 levels fill the 512 blocks exactly: the table's, the method's, 509 Else bodies, the last If");
    limits_close(test);
    if (!limits_run_ladder(test, 511u))
        return;
    kernel_test_check(
        test,
        limits_integer("RES_") == 5u &&
            limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS) == 1u,
        "511 levels need a 513th block and fail with limit_blocks");
    limits_close(test);
    if (!limits_run_ladder(test, 600u))
        return;
    kernel_test_check(
        test,
        limits_integer("RES_") == 5u &&
            limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS) == 1u,
        "the 600-deep ladder fails with limit_blocks");
    limits_close(test);
}

/**
 * @brief A table's code takes at most 2^20 While back-edges, the methods it calls included (§6.1): a
 *        method that loops forever fails with limit_loop_iterations, then a table-level While whose
 *        body only continues, and a table-level While that never ends, each fail at their first
 *        back-edge, since the table's budget is spent; the load goes on, and the next table, an SSDT,
 *        starts a budget of its own, in which a While of 32 back-edges completes.
 */
KERNEL_TEST(an_endless_loop_is_stopped)
{
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_endless_loops_aml,
                            sizeof(limits_endless_loops_aml)))
        return;
    kernel_test_check(
        test,
        limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_LOOP_ITERATIONS) == 3u &&
            limits_load.evaluation.iterations ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS + 3u,
        "the method's loop is stopped past 2^20 back-edges, the two table-level loops at their first");
    kernel_test_check(test, limits_integer("AFTR") == 1u, "the Name after them is declared");
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &limits_load, made_up_definition_block(limits_second_table, "SSDT", 2u, limits_endless_loops_second_aml,
                                               sizeof(limits_endless_loops_second_aml)));
    kernel_test_check(test, limits_integer("CNT2") == 0x20u,
                      "the SSDT's While (LLess (CNT2, 0x20)) completes in a budget of its own");
    limits_close(test);
}

/**
 * @brief A Device the decoding pass meets in a branch the declaration pass did not take, 511 If deep,
 *        fails with limit_blocks before its TermList opens, and leaves no mark behind: a fault in the
 *        body of the If (One) after it is a decode error of a declared construct, not an undeclared one.
 *
 * The If (One) after the 511 If (Zero) is raw bytes: iasl does not emit the byte 0x02, which opens no term, in a body.
 */
KERNEL_TEST(a_construct_refused_at_the_block_limit_leaves_no_mark_behind)
{
    static const uint8_t after[] = {0xA0u, 0x03u, 0x01u, 0x02u};
    uint32_t start = LIMITS_AML_CAPACITY - (uint32_t) sizeof(limits_refused_device_aml);

    memcpy(limits_aml + start, limits_refused_device_aml, sizeof(limits_refused_device_aml));
    for (uint32_t level = 0u; level < 511u; ++level)
    {
        limits_aml[--start] = 0x00u;
        start = limits_package_length(start, LIMITS_AML_CAPACITY - start);
        limits_aml[--start] = 0xA0u;
    }
    memmove(limits_aml, limits_aml + start, LIMITS_AML_CAPACITY - start);
    memcpy(limits_aml + (LIMITS_AML_CAPACITY - start), after, sizeof(after));
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_aml,
                            LIMITS_AML_CAPACITY - start + (uint32_t) sizeof(after)))
        return;
    kernel_test_check(test, limits_load.decode_errors == 2u, "the block limit and the byte 0x02 are decode errors");
    kernel_test_check(test, limits_load.undeclared_faults == 0u,
                      "neither is counted as a fault of an undeclared construct");
    limits_close(test);
}

/**
 * @brief Loads a table whose code nests @p depth If (One), the @p length bytes at @p inner at the deepest
 *        level. The If (One) are written at run time, from raw bytes: their depth is a parameter.
 */
static bool limits_run_nested(KernelTest_t *test, uint32_t depth, const uint8_t *inner, uint32_t length)
{
    uint32_t start = LIMITS_AML_CAPACITY - length;

    memcpy(limits_aml + start, inner, length);
    for (uint32_t level = 0u; level < depth; ++level)
    {
        limits_aml[--start] = 0x01u;
        start = limits_package_length(start, LIMITS_AML_CAPACITY - start);
        limits_aml[--start] = 0xA0u;
    }
    memmove(limits_aml, limits_aml + start, LIMITS_AML_CAPACITY - start);
    return made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_aml,
                              LIMITS_AML_CAPACITY - start);
}

/**
 * @brief Table-level code nests 511 If (One), whose bodies and the table's fill the 512 blocks of §6.1: the
 *        Name at the deepest level is declared. One If more is refused with limit_blocks, a fault of the
 *        load, and its body skipped: it is not a predicate that failed, whose body would be declared.
 */
KERNEL_TEST(table_level_code_nests_up_to_the_block_limit)
{
    if (!limits_run_nested(test, 511u, limits_deep_name_aml, sizeof(limits_deep_name_aml)))
        return;
    kernel_test_check(test, limits_integer("DEEP") == 1u && limits_load.load_errors == 0u,
                      "511 If (One) fill the 512 blocks: DEEP is declared");
    limits_close(test);
    if (!limits_run_nested(test, 512u, limits_deep_name_aml, sizeof(limits_deep_name_aml)))
        return;
    kernel_test_check(test,
                      made_up_root_node(&limits_load, "DEEP") == NULL && limits_load.load_errors == 1u &&
                          limits_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS,
                      "the 512th If is refused with limit_blocks, and DEEP is not declared");
    kernel_test_check(test, limits_load.evaluation.failures == 0u && limits_load.declared_not_run == 0u,
                      "nothing fails to run, and nothing is declared without running");
    limits_close(test);
}

/**
 * @brief A Device at the bottom of 511 If (One), whose bodies and the table's fill the 512 blocks of §6.1,
 *        is declared, but its TermList is refused with limit_blocks, a fault of the load, and skipped: the
 *        Name inside it is not declared, the Device is not declared twice, and the Name after it is.
 */
KERNEL_TEST(a_device_at_the_block_limit_is_declared_without_its_body)
{
    if (!limits_run_nested(test, 511u, limits_deep_device_aml, sizeof(limits_deep_device_aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&limits_load, "DEVX");

    kernel_test_check(test, node != NULL && node->first_child == 0u && limits_integer("AFTD") == 1u,
                      "DEVX is declared without INNR, and AFTD after it");
    kernel_test_check(test,
                      limits_load.load_errors == 1u &&
                          limits_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS &&
                          limits_load.duplicates == 0u && limits_load.evaluation.failures == 0u,
                      "the refused TermList is the one fault, of the load, and nothing is a duplicate");
    limits_close(test);
}

/**
 * @brief A taken If whose body the block limit refuses steps over the Else after it, even when an earlier turn
 *        of the While around it found that If false and ran its Else: at the bottom of 510 If (One), the While's
 *        body takes the 512th block, and neither the If's body nor the Else's finds one (§3.6, §6.2).
 */
KERNEL_TEST(a_taken_if_refused_at_the_block_limit_steps_over_its_else_after_a_turn_that_ran_it)
{
    if (!limits_run_nested(test, 510u, limits_deep_loop_aml, sizeof(limits_deep_loop_aml)))
        return;
    kernel_test_check(test,
                      limits_integer("CNT2") == 2u && limits_integer("ELSC") == 0u &&
                          made_up_root_node(&limits_load, "INIF") == NULL,
                      "the While takes two turns, and neither body runs");
    kernel_test_check(test,
                      limits_load.load_errors == 2u &&
                          limits_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS,
                      "the Else's body on the first turn and the If's on the second are the two faults: the Else "
                      "after the taken If is stepped over");
    limits_close(test);
}

/**
 * @brief A table-level run refuses to start when the evaluation holds 64 frames, or 256 pending
 *        operators, with limit_frames or limit_pending_operators, counted.
 */
KERNEL_TEST(a_table_level_run_refuses_full_stacks)
{
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_result_zero_aml,
                            sizeof(limits_result_zero_aml)))
        return;
    limits_load.evaluation.frame_count =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES;
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_run_table_term(
                          &limits_load, 0u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                          MADE_UP_TABLE_HEADER, MADE_UP_TABLE_HEADER + sizeof(limits_result_zero_aml)) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FRAMES,
                      "64 frames held: limit_frames");
    limits_load.evaluation.frame_count = 0u;
    limits_load.evaluation.operator_count =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS;
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_run_table_term(
                          &limits_load, 0u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                          MADE_UP_TABLE_HEADER, MADE_UP_TABLE_HEADER + sizeof(limits_result_zero_aml)) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PENDING_OPERATORS,
                      "256 operators held: limit_pending_operators");
    limits_load.evaluation.operator_count = 0u;
    kernel_test_check(
        test,
        limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FRAMES) == 1u &&
            limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PENDING_OPERATORS) ==
                1u,
        "both refusals are counted");
    limits_close(test);
}

/**
 * @brief A table declared while the evaluation holds its 64 frames, as one a Load at the bottom of a deep
 *        recursion adds, has no room for its frame: its code is declared without running, as a branch
 *        whose predicate failed (§3.6), every name of it counted so, and the refusal counted.
 */
KERNEL_TEST(a_table_with_no_room_for_its_frame_is_declared_without_running)
{
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_result_five_aml,
                            sizeof(limits_result_five_aml)))
        return;
    limits_load.evaluation.frame_count =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &limits_load, made_up_definition_block(limits_second_table, "SSDT", 2u, limits_frameless_loaded_aml,
                                               sizeof(limits_frameless_loaded_aml)));
    limits_load.evaluation.frame_count = 0u;
    kernel_test_check(test, limits_integer("LDED") == 1u && limits_integer("INIF") == 1u,
                      "LDED, and INIF in the If (One) after it, are declared");
    kernel_test_check(test, limits_integer("RES_") == 5u && limits_load.declared_not_run == 2u,
                      "the Store (Zero, RES_) does not run, and both names count as declared without running");
    kernel_test_check(
        test, limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FRAMES) == 1u,
        "the refusal of the frame is counted");
    limits_close(test);
}
