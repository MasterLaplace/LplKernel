#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_evaluation, KERNEL_TEST_STAGE_INITIALIZATION);

#define EVALUATION_TABLE_CAPACITY 512u /**< Bytes of the buffer a made-up DSDT is written in. */

static uint8_t evaluation_table[EVALUATION_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t evaluation_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t evaluation_load;

/**
 * @brief Loads @p aml as a DSDT of @p revision, running its table-level code.
 */
static bool evaluation_run(KernelTest_t *test, uint8_t revision, const uint8_t *aml, uint32_t length)
{
    return made_up_load_table(test, &evaluation_load, &evaluation_host, evaluation_table, revision, aml, length);
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t evaluation_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&evaluation_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief How many runs failed with @p status.
 */
static uint32_t evaluation_failed(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status)
{
    return evaluation_load.evaluation.statuses[status];
}

/**
 * @brief Checks that every run left the evaluation's stacks empty, then closes the load.
 */
static void evaluation_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &evaluation_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          evaluation_load.temporary_count == 0u,
                      "every run leaves no frame, value, operator, block or temporary node behind");
    advanced_configuration_and_power_interface_machine_language_close(&evaluation_load);
}

/**
 * @brief A method that ends without Return gives Integer 0 to a caller that uses the result, counted
 *        (§5.2); a call whose result is dropped counts nothing.
 */
KERNEL_TEST(a_method_without_return_gives_integer_zero)
{
    static const uint8_t aml[] = {
        0x08u, 'R', 'E', 'S', '_', 0x0Au, 0x05u, 0x14u, 0x07u, 'M', '0', '0', '0', 0x00u,
        0xA3u, 'M', '0', '0', '0', 0x70u, 'M',   '0',   '0',   '0', 'R', 'E', 'S', '_',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_measure(test, "implicit_returns", evaluation_load.evaluation.implicit_returns);
    kernel_test_check(test, evaluation_integer("RES_") == 0u, "Store (M000 (), RES_) stores 0");
    kernel_test_check(test,
                      evaluation_load.evaluation.implicit_returns == 1u && evaluation_load.evaluation.runs == 2u &&
                          evaluation_load.evaluation.failures == 0u,
                      "two runs, one implicit return counted: the statement M000 () uses no result");
    evaluation_close(test);
}

/**
 * @brief An uninitialized local read as an operand gives Integer 0, counted (§5.2).
 */
KERNEL_TEST(an_uninitialized_local_reads_as_integer_zero)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   'E',   'S',   '_', 0x0Au, 0x05u, 0x14u, 0x08u, 'M', '0', '0', '1',
        0x00u, 0xA4u, 0x60u, 0x70u, 'M', '0',   '0',   '1',   'R',   'E', 'S', '_',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, evaluation_integer("RES_") == 0u, "Return (Local0) gives 0");
    kernel_test_check(test, evaluation_load.evaluation.uninitialized_operands == 1u, "the read is counted");
    evaluation_close(test);
}

/**
 * @brief Under a revision-1 DSDT every integer is 32 bits: Ones is 0xFFFFFFFF, Add (Ones, One) wraps
 *        to 0, and True is 0xFFFFFFFF; the same table under revision 2, the control, comes out 64 bits
 *        wide.
 */
KERNEL_TEST(integers_are_32_bits_under_a_revision_1_definition_block)
{
    static const uint8_t aml[] = {
        0x08u, 'R', 'E',   'S',   '0',   0x00u, 0x08u, 'R',   'E',   'S',   '1',   0x00u, 0x08u, 'R',   'E',
        'S',   '2', 0x00u, 0x70u, 0x72u, 0xFFu, 0x01u, 0x00u, 'R',   'E',   'S',   '0',   0x70u, 0xFFu, 'R',
        'E',   'S', '1',   0x70u, 0x93u, 0xFFu, 0x0Cu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 'R',   'E',   'S',   '2',
    };

    if (!evaluation_run(test, 1u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, evaluation_integer("RES0") == 0u, "Add (Ones, One) is 0");
    kernel_test_check(test, evaluation_integer("RES1") == 0xFFFFFFFFu, "Ones is 0xFFFFFFFF");
    kernel_test_check(test, evaluation_integer("RES2") == 0xFFFFFFFFu, "LEqual (Ones, 0xFFFFFFFF) is True");
    evaluation_close(test);
    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, evaluation_integer("RES1") == ~0ull && evaluation_integer("RES2") == 0u,
                      "under revision 2, Ones is 64 bits wide and differs from 0xFFFFFFFF");
    evaluation_close(test);
}

/**
 * @brief A method that only computes integers allocates nothing (§7.2, O3): a loop of ten, an
 *        addition and a product give 210, and the object heap holds only `\_OS_`.
 */
KERNEL_TEST(an_integer_only_method_allocates_nothing)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   'E',   'S',   '_',   0x00u, 0x14u, 0x1Au, 'M',   '0',   '0',   '2',   0x01u, 0x70u, 0x72u,
        0x68u, 0x0Au, 0x10u, 0x00u, 0x60u, 0xA2u, 0x07u, 0x95u, 0x61u, 0x0Au, 0x0Au, 0x75u, 0x61u, 0xA4u, 0x77u,
        0x60u, 0x61u, 0x00u, 0x70u, 'M',   '0',   '0',   '2',   0x0Au, 0x05u, 'R',   'E',   'S',   '_',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_measure(test, "allocations", evaluation_load.heap.objects);
    kernel_test_check(test, evaluation_integer("RES_") == 210u, "(5 + 16) * 10 is 210");
    kernel_test_check(test, evaluation_load.heap.objects == 1u, "the heap holds \\_OS_ and nothing else");
    evaluation_close(test);
}

/**
 * @brief A While runs until a Break, a Continue skips the rest of its body, and an If takes its Else
 *        when its predicate is false: 1 + 2 + 4 + 5 is 12, and M004 answers 1, then 2.
 */
KERNEL_TEST(control_flow_runs_loops_branches_and_their_exits)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   'E',   'S',   '0',   0x00u, 0x08u, 'R',   'E',   'S',   '1',   0x00u, 0x08u, 'R',   'E',
        'S',   '2',   0x00u, 0x14u, 0x1Eu, 'M',   '0',   '0',   '3',   0x01u, 0xA2u, 0x15u, 0x01u, 0x75u, 0x60u,
        0xA0u, 0x06u, 0x93u, 0x60u, 0x0Au, 0x03u, 0x9Fu, 0xA0u, 0x05u, 0x94u, 0x60u, 0x68u, 0xA5u, 0x72u, 0x61u,
        0x60u, 0x61u, 0xA4u, 0x61u, 0x14u, 0x13u, 'M',   '0',   '0',   '4',   0x01u, 0xA0u, 0x07u, 0x93u, 0x68u,
        0x0Au, 0x05u, 0xA4u, 0x01u, 0xA1u, 0x04u, 0xA4u, 0x0Au, 0x02u, 0x70u, 'M',   '0',   '0',   '3',   0x0Au,
        0x05u, 'R',   'E',   'S',   '0',   0x70u, 'M',   '0',   '0',   '4',   0x0Au, 0x05u, 'R',   'E',   'S',
        '1',   0x70u, 'M',   '0',   '0',   '4',   0x0Au, 0x06u, 'R',   'E',   'S',   '2',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, evaluation_integer("RES0") == 12u, "the loop skips 3 and stops past 5: 12");
    kernel_test_check(test, evaluation_integer("RES1") == 1u && evaluation_integer("RES2") == 2u,
                      "M004 takes its If for 5, its Else for 6");
    kernel_test_check(test, evaluation_load.evaluation.failures == 0u, "nothing fails");
    evaluation_close(test);
}

/**
 * @brief A Name a method declares lives as long as the method (§5.1): two calls both see it, it is
 *        gone after them, and a loop that declares it twice fails with name_exists.
 */
KERNEL_TEST(a_name_a_method_declares_lives_as_long_as_the_method)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   'E',   'S',   '0',   0x00u, 0x08u, 'R',   'E',   'S', '1',   0x00u, 0x14u, 0x12u,
        'M',   '0',   '0',   '5',   0x00u, 0x08u, 'T',   'M',   'P',   '_', 0x0Au, 0x07u, 0xA4u, 'T',
        'M',   'P',   '_',   0x70u, 'M',   '0',   '0',   '5',   'R',   'E', 'S',   '0',   0x70u, 'M',
        '0',   '0',   '5',   'R',   'E',   'S',   '1',   0x14u, 0x0Fu, 'M', '0',   '0',   '6',   0x00u,
        0xA2u, 0x08u, 0x01u, 0x08u, 'L',   'O',   'O',   'P',   0x01u, 'M', '0',   '0',   '6',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *method =
        made_up_root_node(&evaluation_load, "M005");

    kernel_test_check(test, evaluation_integer("RES0") == 7u && evaluation_integer("RES1") == 7u,
                      "both calls return the 7 TMP_ holds");
    kernel_test_check(test, method != NULL && method->first_child == 0u, "M005 has no child once its calls are over");
    kernel_test_check(
        test, evaluation_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_EXISTS) == 1u,
        "LOOP declared twice in M006 fails with name_exists");
    evaluation_close(test);
}

/**
 * @brief Table-level code runs as the load meets it: an If declares only the branch it takes, and
 *        the table's locals last from one statement to the next.
 */
KERNEL_TEST(table_level_code_declares_only_the_branch_it_takes)
{
    static const uint8_t aml[] = {
        0xA0u, 0x08u, 0x00u, 0x08u, 'A',   'A',   'A',   'A',   0x01u, 0xA1u, 0x08u, 0x08u, 'B',   'B',
        'B',   'B',   0x0Au, 0x02u, 0xA0u, 0x09u, 0x01u, 0x08u, 'C',   'C',   'C',   'C',   0x0Au, 0x03u,
        0xA1u, 0x08u, 0x08u, 'D',   'D',   'D',   'D',   0x0Au, 0x04u, 0x08u, 'R',   'E',   'S',   '_',
        0x00u, 0x70u, 0x0Au, 0x2Au, 0x60u, 0x70u, 0x60u, 'R',   'E',   'S',   '_',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_root_node(&evaluation_load, "AAAA") == NULL && evaluation_integer("BBBB") == 2u,
                      "If (Zero) declares its Else, not its body");
    kernel_test_check(test, evaluation_integer("CCCC") == 3u && made_up_root_node(&evaluation_load, "DDDD") == NULL,
                      "If (One) declares its body, not its Else");
    kernel_test_check(test, evaluation_integer("RES_") == 42u, "Local0 keeps 42 from one statement to the next");
    kernel_test_check(test, evaluation_load.evaluation.failures == 0u, "nothing fails");
    evaluation_close(test);
}

/**
 * @brief A field read fails with field_access_deferred until fields land (#541), counted; an If over
 *        such a read declares both its branches without running them, so that the namespace loses
 *        nothing the interpreter cannot run yet.
 */
KERNEL_TEST(a_field_read_waits_for_fields_and_keeps_both_branches)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'R',   'E',   'G',   '0',   0x00u, 0x00u, 0x0Au, 0x10u, 0x5Bu, 0x81u, 0x0Bu, 'R',   'E',
        'G',   '0',   0x01u, 'F',   'L',   'D',   '0',   0x08u, 0xA0u, 0x0Bu, 'F',   'L',   'D',   '0',   0x08u,
        'E',   'E',   'E',   'E',   0x01u, 0xA1u, 0x07u, 0x08u, 'F',   'F',   'F',   'F',   0x01u, 0x08u, 'R',
        'E',   'S',   '_',   0x0Au, 0x05u, 0x70u, 'F',   'L',   'D',   '0',   'R',   'E',   'S',   '_',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, evaluation_integer("EEEE") == 1u && evaluation_integer("FFFF") == 1u,
                      "both branches of If (FLD0) are declared");
    kernel_test_check(test, evaluation_integer("RES_") == 5u, "Store (FLD0, RES_) stores nothing");
    kernel_test_check(
        test,
        evaluation_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_FIELD_ACCESS_DEFERRED) ==
            2u,
        "both reads are counted as field_access_deferred");
    evaluation_close(test);
}

/**
 * @brief An opcode the needs-path closure does not reach fails with unimplemented_operator, counted
 *        per opcode (§4.5).
 */
KERNEL_TEST(an_opcode_outside_the_closure_is_unimplemented)
{
    static const uint8_t aml[] = {
        0x08u, 'R', 'E', 'S', '_', 0x0Au, 0x05u, 0x70u, 0x81u, 0x01u, 0x00u, 'R', 'E', 'S', '_',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(
        test,
        evaluation_load.evaluation.unimplemented
                    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_FIND_SET_LEFT_BIT] == 1u &&
            evaluation_failed(
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_UNIMPLEMENTED_OPERATOR) == 1u,
        "FindSetLeftBit is counted as unimplemented");
    kernel_test_check(test, evaluation_integer("RES_") == 5u, "RES_ keeps its value");
    evaluation_close(test);
}

/**
 * @brief A shift by the integer width gives 0, and a division or a modulo by zero fails with
 *        divide_by_zero (§5.2).
 */
KERNEL_TEST(shifts_past_the_width_and_divisions_by_zero)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   'E',   'S',   '0',   0x0Au, 0x05u, 0x08u, 'R',   'E',   'S',   '1',   0x0Au,
        0x05u, 0x08u, 'R',   'E',   'S',   '2',   0x0Au, 0x05u, 0x70u, 0x79u, 0x01u, 0x0Au, 0x40u,
        0x00u, 'R',   'E',   'S',   '0',   0x70u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u, 'R',   'E',
        'S',   '1',   0x70u, 0x85u, 0x01u, 0x00u, 0x00u, 'R',   'E',   'S',   '2',
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, evaluation_integer("RES0") == 0u, "ShiftLeft (One, 64) is 0");
    kernel_test_check(test, evaluation_integer("RES1") == 5u && evaluation_integer("RES2") == 5u,
                      "Divide and Mod by zero store nothing");
    kernel_test_check(
        test,
        evaluation_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_DIVIDE_BY_ZERO) == 2u,
        "both fail with divide_by_zero");
    evaluation_close(test);
}

/**
 * @brief The decoding pass still decodes a method the load did not declare, in a branch table-level
 *        code did not take, but its faults are counted apart: its names would resolve against objects
 *        that do not exist. The same body in a declared method is a decode error.
 */
KERNEL_TEST(a_method_the_load_did_not_declare_counts_its_faults_apart)
{
    static const uint8_t aml[] = {
        0xA0u, 0x0Au, 0x00u, 0x14u, 0x07u, 'D', 'E', 'A',   'D',   0x00u,
        0x70u, 0x14u, 0x07u, 'L',   'I',   'V', 'E', 0x00u, 0x70u,
    };

    if (!evaluation_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_root_node(&evaluation_load, "DEAD") == NULL, "DEAD is not declared");
    kernel_test_check(test, evaluation_load.undeclared_faults == 1u && evaluation_load.decode_errors == 1u,
                      "DEAD's cut Store is counted apart, LIVE's is a decode error");
    evaluation_close(test);
}
