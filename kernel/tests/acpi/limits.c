#include "fixtures/limits.h"
#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_limits, KERNEL_TEST_STAGE_INITIALIZATION);

#define LIMITS_AML_CAPACITY     16384u /**< Bytes of AML the deepest made-up table holds. */
#define LIMITS_MOST_INNER_NAMES 513u   /**< Names the inner method of limits_run_names holds at most. */

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
 * @brief Loads, in a namespace of @p max_nodes nodes, a table that declares RES_ as 5, the @p before_length bytes of
 *        table-level terms at @p before, a method M009 whose body is the @p length bytes at the end of the AML
 *        buffer, and, when @p calls holds, stores M009 to RES_.
 */
static bool limits_run_method(KernelTest_t *test, uint32_t length, uint32_t max_nodes, const uint8_t *before,
                              uint32_t before_length, bool calls)
{
    uint32_t start = LIMITS_AML_CAPACITY - length;

    limits_aml[--start] = 0x00u;
    start -= 4u;
    memcpy(limits_aml + start, "M009", 4u);
    start = limits_package_length(start, LIMITS_AML_CAPACITY - start);
    limits_aml[--start] = 0x14u;
    start -= before_length;
    memcpy(limits_aml + start, before, before_length);
    start -= (uint32_t) sizeof(limits_result_five_aml);
    memcpy(limits_aml + start, limits_result_five_aml, sizeof(limits_result_five_aml));
    memmove(limits_aml, limits_aml + start, LIMITS_AML_CAPACITY - start);
    memcpy(limits_aml + (LIMITS_AML_CAPACITY - start), limits_store_m009_aml,
           calls ? sizeof(limits_store_m009_aml) : 0u);
    advanced_configuration_and_power_interface_simulated_host_open(&limits_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &limits_load, &limits_host.host, max_nodes, MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
    {
        advanced_configuration_and_power_interface_machine_language_close(&limits_load);
        return false;
    }
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &limits_load, made_up_definition_block(limits_table, "DSDT", 2u, limits_aml,
                                               LIMITS_AML_CAPACITY - start +
                                                   (calls ? (uint32_t) sizeof(limits_store_m009_aml) : 0u)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&limits_load);
    return true;
}

/**
 * @brief Runs M009 { Return (Add (Add (... Add (One, One) ..., One), One)) }, @p adds Add deep, which gives
 *        @p adds + 1 and holds @p adds + 2 operators pending at once with the table's Store; no term comes before.
 *        M009 is only declared when @p calls is false.
 */
static bool limits_run_adds(KernelTest_t *test, uint32_t adds, bool calls)
{
    const uint32_t length = 3u * adds + 2u;
    uint8_t *body = limits_aml + LIMITS_AML_CAPACITY - length;

    body[0] = 0xA4u;
    memset(body + 1u, 0x72, adds);
    body[1u + adds] = 0x01u;
    for (uint32_t add = 0u; add < adds; ++add)
    {
        body[2u + adds + 2u * add] = 0x01u;
        body[3u + adds + 2u * add] = 0x00u;
    }
    return limits_run_method(test, length, MADE_UP_MAX_NODES, body, 0u, calls);
}

/**
 * @brief Appends, from @p at on, Name (Txxx, Zero) for each of the @p count numbers from @p first on, Txxx the number
 *        in hexadecimal.
 *
 * @return Where the Names end.
 */
static uint8_t *limits_write_names(uint8_t *at, uint32_t first, uint32_t count)
{
    static const char digits[] = "0123456789ABCDEF";

    for (uint32_t index = first; index < first + count; ++index)
    {
        const uint8_t declaration[] = {0x08u,
                                       'T',
                                       (uint8_t) digits[(index >> 8u) & 0xFu],
                                       (uint8_t) digits[(index >> 4u) & 0xFu],
                                       (uint8_t) digits[index & 0xFu],
                                       0x00u};

        memcpy(at, declaration, sizeof(declaration));
        at += sizeof(declaration);
    }
    return at;
}

/**
 * @brief Runs M009 { Name (T000, Zero) ... Return (M00A) }, which names 512, where M00A { Name (T200, Zero) ...
 *        Return (One) } names @p inner: the evaluation holds 512 + @p inner temporary nodes at once, where the
 *        decoding pass, which decodes one body at a time, meets 512, in a namespace with room for them all.
 */
static bool limits_run_names(KernelTest_t *test, uint32_t inner)
{
    static uint8_t other[1u + MADE_UP_AML_PACKAGE_LENGTH_BYTES + 5u + 6u * LIMITS_MOST_INNER_NAMES + 2u];
    static const uint8_t inner_head[] = {'M', '0', '0', 'A', 0x00u};
    uint8_t encoded[MADE_UP_AML_PACKAGE_LENGTH_BYTES];
    const uint32_t inner_content =
        (uint32_t) sizeof(inner_head) + 6u * inner + (uint32_t) sizeof(limits_return_one_aml);
    const uint32_t encoded_bytes = made_up_aml_encode_package_length(inner_content, true, encoded);
    const uint32_t length = 6u * 512u + (uint32_t) sizeof(limits_return_m00a_aml);
    uint8_t *at = other;

    if (!kernel_test_check(test, inner <= LIMITS_MOST_INNER_NAMES, "the inner method fits its buffer"))
        return false;
    *at++ = 0x14u;
    memcpy(at, encoded, encoded_bytes);
    at += encoded_bytes;
    memcpy(at, inner_head, sizeof(inner_head));
    at = limits_write_names(at + sizeof(inner_head), 512u, inner);
    memcpy(at, limits_return_one_aml, sizeof(limits_return_one_aml));
    at += sizeof(limits_return_one_aml);
    memcpy(limits_write_names(limits_aml + LIMITS_AML_CAPACITY - length, 0u, 512u), limits_return_m00a_aml,
           sizeof(limits_return_m00a_aml));
    return limits_run_method(test, length, 2u * MADE_UP_MAX_NODES, other, (uint32_t) (at - other), true);
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
    kernel_test_measure(test, "blocks_peak", made_up_limit_peak(&limits_load, "blocks_peak"));
    kernel_test_check(test,
                      made_up_limit_peak(&limits_load, "blocks_peak") ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS,
                      "the blocks' peak is 512");
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
    kernel_test_measure(test, "loop_iterations_peak", made_up_limit_peak(&limits_load, "loop_iterations_peak"));
    kernel_test_check(test,
                      made_up_limit_peak(&limits_load, "loop_iterations_peak") ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS,
                      "the back-edges' peak is the 2^20 the evaluation took, not the one refused");
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

/**
 * @brief An evaluation holds 256 pending operators (§6.1): the table's Store, M009's Return and 254 Add hold them all
 *        and give 255; one Add more fails with limit_pending_operators, counted with the faults.
 */
KERNEL_TEST(nested_operators_fill_the_operator_stack_and_one_more_fails)
{
    if (!limits_run_adds(test, 254u, true))
        return;
    kernel_test_measure(test, "pending_operators_peak", made_up_limit_peak(&limits_load, "pending_operators_peak"));
    kernel_test_check(test,
                      limits_integer("RES_") == 255u && limits_load.evaluation.failures == 0u &&
                          made_up_limit_peak(&limits_load, "pending_operators_peak") ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS,
                      "254 Add deep give 255, the 256 operators held at once");
    limits_close(test);
    if (!limits_run_adds(test, 255u, true))
        return;
    kernel_test_check(
        test,
        limits_integer("RES_") == 5u &&
            limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PENDING_OPERATORS) ==
                1u &&
            limits_load.faults
                    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PENDING_OPERATORS] != 0u,
        "255 Add deep fail with limit_pending_operators, counted with the faults");
    limits_close(test);
}

/**
 * @brief An evaluation holds 1,024 temporary nodes (§6.1): a method that declares a method and 512 names, then calls
 *        the one it declared, which names 511, runs; one whose inner method names 512 fails with
 *        limit_temporary_nodes, counted with the faults.
 */
KERNEL_TEST(a_method_names_up_to_1024_temporary_nodes)
{
    if (!limits_run_names(test, 512u))
        return;
    kernel_test_measure(test, "temporary_nodes_peak", made_up_limit_peak(&limits_load, "temporary_nodes_peak"));
    kernel_test_check(test,
                      limits_integer("RES_") == 1u && limits_load.evaluation.failures == 0u &&
                          made_up_limit_peak(&limits_load, "temporary_nodes_peak") ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES,
                      "1,024 names are made, the most an evaluation holds");
    limits_close(test);
    if (!limits_run_names(test, 513u))
        return;
    kernel_test_check(
        test,
        limits_integer("RES_") == 5u &&
            limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TEMPORARY_NODES) ==
                1u &&
            limits_load
                    .faults[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TEMPORARY_NODES] !=
                0u,
        "1,025 fail with limit_temporary_nodes, counted with the faults");
    limits_close(test);
}

/**
 * @brief A VarPackage holds 4,096 elements (§6.1): one of 4,096 is built, one of 4,097 fails with
 *        limit_package_elements, counted with the faults.
 */
KERNEL_TEST(a_variable_package_holds_4096_elements)
{
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_variable_packages_aml,
                            sizeof(limits_variable_packages_aml)))
        return;
    kernel_test_measure(test, "package_elements_peak", made_up_limit_peak(&limits_load, "package_elements_peak"));
    kernel_test_check(test,
                      limits_integer("FULL") == 4096u && limits_integer("OVER") == 5u &&
                          made_up_limit_peak(&limits_load, "package_elements_peak") == 4096u,
                      "4,096 elements are built, 4,097 are not");
    kernel_test_check(
        test,
        limits_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PACKAGE_ELEMENTS) ==
                1u &&
            limits_load.faults
                    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PACKAGE_ELEMENTS] == 1u,
        "the refusal is counted with the faults");
    limits_close(test);
}

/**
 * @brief A data object nests 32 packages (§6.1): DP32, kept in its table (O20), is built 32 deep by the general data
 *        decoder, which a load whose general_load is set uses and which checks the kept data once bodies are decoded,
 *        then by the builder of kept data (C4) at its first read in a load that skips bodies, as a release kernel's
 *        does; DP33 waits for the interpreter, counted in values_deferred, and builds nothing deeper.
 */
KERNEL_TEST(a_data_object_nests_32_packages)
{
    for (uint32_t mode = 0u; mode < 2u; ++mode)
    {
        advanced_configuration_and_power_interface_simulated_host_open(&limits_host);
        if (!kernel_test_check(test,
                               advanced_configuration_and_power_interface_machine_language_open(
                                   &limits_load, &limits_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
                               "the namespace opens"))
        {
            advanced_configuration_and_power_interface_machine_language_close(&limits_load);
            return;
        }
        limits_load.general_load = mode == 0u;
        limits_load.skips_bodies = mode == 1u;
        advanced_configuration_and_power_interface_machine_language_declare_table(
            &limits_load, made_up_definition_block(limits_table, "DSDT", 2u, limits_nested_packages_aml,
                                                   sizeof(limits_nested_packages_aml)));
        advanced_configuration_and_power_interface_machine_language_decode_tables(&limits_load);

        const uint32_t deferred = limits_load.values_deferred;
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *deepest =
            made_up_root_node(&limits_load, "DP32");

        kernel_test_measure(test, "nested_packages_peak", made_up_limit_peak(&limits_load, "nested_packages_peak"));
        kernel_test_check(test,
                          deepest->value.kind ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE &&
                              made_up_limit_peak(&limits_load, "nested_packages_peak") ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES,
                          "DP32 is built 32 deep, the most a data object nests");
        kernel_test_check(test,
                          made_up_root_node(&limits_load, "DP33")->value.kind !=
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE &&
                              limits_load.values_deferred == deferred + 1u,
                          "DP33 waits for the interpreter");
        limits_close(test);
    }
}

/**
 * @brief The decoding pass holds the temporary nodes of the body it decodes against the same cap (§6.1): a method that
 *        names 1,025, never called, is a decode error, limit_temporary_nodes counted with the faults once, at the peak
 *        of 1,024 it held.
 */
KERNEL_TEST(the_decoding_pass_holds_1024_temporary_nodes)
{
    uint8_t *body = limits_aml + LIMITS_AML_CAPACITY - 6u * 1025u - (uint32_t) sizeof(limits_return_one_aml);

    memcpy(limits_write_names(body, 0u, 1025u), limits_return_one_aml, sizeof(limits_return_one_aml));
    if (!limits_run_method(test, 6u * 1025u + (uint32_t) sizeof(limits_return_one_aml), 2u * MADE_UP_MAX_NODES, body,
                           0u, false))
        return;
    kernel_test_check(
        test,
        limits_integer("RES_") == 5u && limits_load.decode_errors == 1u &&
            limits_load
                    .faults[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TEMPORARY_NODES] ==
                1u &&
            made_up_limit_peak(&limits_load, "temporary_nodes_peak") ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES,
        "the body's 1,025th name is refused, counted, after 1,024 held");
    limits_close(test);
}

/**
 * @brief The arena's peak is the most it held, not what it holds (§6.1): MBIG's Buffer of 32 KiB takes pages of its
 *        own, given back when MBIG returns.
 */
KERNEL_TEST(the_arena_s_peak_keeps_the_pages_given_back)
{
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_arena_buffer_aml,
                            sizeof(limits_arena_buffer_aml)))
        return;
    kernel_test_check(test,
                      limits_integer("RES_") == 0x8000u && made_up_limit_peak(&limits_load, "arena_bytes_peak") >=
                                                               limits_load.space.arena_bytes + 0x8000u,
                      "the arena held the Buffer's 32 KiB more than it holds once MBIG returned");
    limits_close(test);
}

/**
 * @brief The list of the limits fills each of its entries, each with the key its peak is printed under.
 */
KERNEL_TEST(every_limit_of_the_list_has_a_peak)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t
        limits[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS];
    bool named = true;

    memset(limits, 0, sizeof(limits));
    if (!made_up_load_table(test, &limits_load, &limits_host, limits_table, 2u, limits_result_five_aml,
                            sizeof(limits_result_five_aml)))
        return;
    advanced_configuration_and_power_interface_machine_language_limits(&limits_load, limits);
    for (uint32_t index = 0u; index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS; ++index)
        named = named && limits[index].peak_name != NULL;
    kernel_test_check(test, named, "each of the 21 entries names its peak");
    limits_close(test);
}

/**
 * @brief The decoding pass holds the operators and the blocks of the body it decodes against the same caps (§6.1):
 *        a body 254 Add deep, then a ladder 200 If deep, decoded and never run, leave peaks the evaluations never
 *        reached.
 */
KERNEL_TEST(the_decoding_pass_s_stacks_count_in_the_peaks)
{
    if (!limits_run_adds(test, 254u, false))
        return;
    kernel_test_check(test,
                      limits_integer("RES_") == 5u && limits_load.evaluation.operators_peak < 8u &&
                          made_up_limit_peak(&limits_load, "pending_operators_peak") > 254u,
                      "the 254 Add the decoding pass holds are the operators' peak");
    limits_close(test);

    const uint32_t start = limits_ladder(200u);

    if (!limits_run_method(test, LIMITS_AML_CAPACITY - start, MADE_UP_MAX_NODES, limits_aml, 0u, false))
        return;
    kernel_test_check(test,
                      limits_integer("RES_") == 5u && limits_load.evaluation.blocks_peak < 8u &&
                          made_up_limit_peak(&limits_load, "blocks_peak") > 200u,
                      "the 200 If the decoding pass holds are the blocks' peak");
    limits_close(test);
}
