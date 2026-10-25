#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_values, KERNEL_TEST_STAGE_INITIALIZATION);

#define VALUES_TABLE_CAPACITY 640u                  /**< Bytes of the buffer a made-up DSDT is written in. */
#define VALUES_ONES           0xFFFFFFFFFFFFFFFFull /**< Ones, and True, under a revision-2 table. */

static uint8_t values_table[VALUES_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t values_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t values_load;

/**
 * @brief Loads @p aml as a DSDT of @p revision, running its table-level code.
 */
static bool values_run(KernelTest_t *test, uint8_t revision, const uint8_t *aml, uint32_t length)
{
    return made_up_load_table(test, &values_load, &values_host, values_table, revision, aml, length);
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t values_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&values_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief The object a value names, when it is of @p kind.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *
values_object_of(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint8_t kind)
{
    if (value == NULL || value->kind != kind)
        return NULL;
    return advanced_configuration_and_power_interface_machine_language_object(&values_load.heap, value->handle);
}

/**
 * @brief The object the Name at the root called @p name holds, when it is of @p kind.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *values_object(const char *name,
                                                                                                uint8_t kind)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&values_load, name);

    return (node != NULL) ? values_object_of(&node->value, kind) : NULL;
}

/**
 * @brief Whether a value is a String or a Buffer, as @p kind says, of exactly @p length bytes equal to @p bytes.
 */
static bool values_value_holds(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint8_t kind,
                               const void *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object = values_object_of(value, kind);

    return object != NULL && object->length == length && memcmp(object + 1, bytes, length) == 0;
}

/**
 * @brief Whether the Name at the root called @p name holds a String of the characters of @p text.
 */
static bool values_string_is(const char *name, const char *text)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&values_load, name);

    return node != NULL &&
           values_value_holds(&node->value, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                              text, (uint32_t) strlen(text));
}

/**
 * @brief Whether the Name at the root called @p name holds a Buffer of @p length bytes equal to @p bytes.
 */
static bool values_buffer_is(const char *name, const uint8_t *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&values_load, name);

    return node != NULL &&
           values_value_holds(&node->value, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                              bytes, length);
}

/**
 * @brief Element @p index of the Package @p package, or NULL past its end or when it is no Package.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *
values_element(const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package, uint32_t index)
{
    if (package == NULL || package->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE ||
        index >= package->length)
        return NULL;
    return (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1) + index;
}

/**
 * @brief Whether a value is the Integer @p integer.
 */
static bool values_value_is_integer(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value,
                                    uint64_t integer)
{
    return value != NULL && value->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
           value->payload == integer;
}

/**
 * @brief Objects the heap holds alive.
 */
static uint32_t values_live_objects(void) { return values_load.heap.objects - values_load.heap.objects_freed; }

/**
 * @brief How many runs failed with @p status.
 */
static uint32_t values_failed(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status)
{
    return values_load.evaluation.statuses[status];
}

/**
 * @brief Checks that every run left the evaluation's stacks empty, then closes the load.
 */
static void values_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &values_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          values_load.temporary_count == 0u,
                      "every run leaves no frame, value, operator, block or temporary node behind");
    advanced_configuration_and_power_interface_machine_language_close(&values_load);
    kernel_test_check(test, values_host.outstanding_pages == 0u, "every page goes back to the host");
}

/**
 * @brief Strings, buffers and packages are built while methods run (§4.3): a String literal; a Buffer
 *        whose size an argument gives, longer than its initialiser or shorter; a VarPackage of an
 *        argument's size, padded with Uninitialized past its initialisers or cut short, each counted;
 *        a Package that nests a Package and keeps a name as a name (§5.1). The heap then holds
 *        exactly the objects the Names keep: the initialisers a VarPackage drops are freed.
 */
KERNEL_TEST(strings_buffers_and_packages_are_built_while_methods_run)
{
    static const uint8_t aml[] = {
        0x08u, 'S',   'T',   'R',   '0',   0x0Au, 0x00u, 0x08u, 'B',   'U',   'F',   '0',   0x0Au, 0x00u, 0x08u, 'B',
        'U',   'F',   '1',   0x0Au, 0x00u, 0x08u, 'V',   'P',   'K',   '0',   0x0Au, 0x00u, 0x08u, 'V',   'P',   'K',
        '1',   0x0Au, 0x00u, 0x08u, 'P',   'K',   'G',   '0',   0x0Au, 0x00u, 0x14u, 0x14u, 'M',   'S',   'T',   'R',
        0x00u, 0xA4u, 0x0Du, 0x68u, 0x65u, 0x6Cu, 0x6Cu, 0x6Fu, 0x20u, 0x77u, 0x6Fu, 0x72u, 0x6Cu, 0x64u, 0x00u, 0x14u,
        0x0Cu, 'M',   'B',   'U',   'F',   0x01u, 0xA4u, 0x11u, 0x04u, 0x68u, 0x01u, 0x02u, 0x14u, 0x14u, 'M',   'V',
        'P',   'K',   0x01u, 0xA4u, 0x13u, 0x0Cu, 0x68u, 0x0Au, 0x11u, 0x11u, 0x03u, 0x68u, 0xAAu, 0x0Du, 0x61u, 0x62u,
        0x00u, 0x14u, 0x17u, 'M',   'P',   'K',   'G',   0x00u, 0xA4u, 0x12u, 0x0Fu, 0x02u, 0x0Au, 0x22u, 0x12u, 0x0Au,
        0x02u, 0x0Du, 0x63u, 0x64u, 0x00u, 'S',   'T',   'R',   '0',   0x9Du, 'M',   'S',   'T',   'R',   'S',   'T',
        'R',   '0',   0x9Du, 'M',   'B',   'U',   'F',   0x0Au, 0x04u, 'B',   'U',   'F',   '0',   0x9Du, 'M',   'B',
        'U',   'F',   0x0Au, 0x01u, 'B',   'U',   'F',   '1',   0x9Du, 'M',   'V',   'P',   'K',   0x0Au, 0x05u, 'V',
        'P',   'K',   '0',   0x9Du, 'M',   'V',   'P',   'K',   0x0Au, 0x01u, 'V',   'P',   'K',   '1',   0x9Du, 'M',
        'P',   'K',   'G',   'P',   'K',   'G',   '0',
    };
    static const uint8_t four[] = {0x01u, 0x02u, 0x00u, 0x00u};
    static const uint8_t two[] = {0x01u, 0x02u};
    static const uint8_t padded[] = {0xAAu, 0x00u, 0x00u, 0x00u, 0x00u};

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *long_package =
        values_object("VPK0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *short_package =
        values_object("VPK1", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *nesting =
        values_object("PKG0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *inner_value = values_element(nesting, 1u);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *inner =
        values_object_of(inner_value, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE);

    kernel_test_measure(test, "live_objects", values_live_objects());
    kernel_test_check(test, values_string_is("STR0", "hello world"), "MSTR returns the String \"hello world\"");
    kernel_test_check(test, values_buffer_is("BUF0", four, sizeof(four)),
                      "Buffer (4) { 1, 2 } is 4 bytes, zero past its initialiser");
    kernel_test_check(test, values_buffer_is("BUF1", two, sizeof(two)),
                      "Buffer (1) { 1, 2 } takes the length of its initialiser");
    kernel_test_check(test,
                      long_package != NULL && long_package->length == 5u &&
                          values_value_is_integer(values_element(long_package, 0u), 0x11u) &&
                          values_value_holds(values_element(long_package, 1u),
                                             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                             padded, sizeof(padded)) &&
                          values_value_holds(values_element(long_package, 2u),
                                             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                             "ab", 2u) &&
                          values_element(long_package, 3u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_UNINITIALIZED &&
                          values_element(long_package, 4u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_UNINITIALIZED,
                      "VarPackage (5) holds its three initialisers, then two Uninitialized");
    kernel_test_check(test,
                      short_package != NULL && short_package->length == 1u &&
                          values_value_is_integer(values_element(short_package, 0u), 0x11u),
                      "VarPackage (1) keeps its first initialiser only");
    kernel_test_check(test, values_load.heap.packages_padded == 1u && values_load.heap.packages_truncated == 1u,
                      "the padded and the cut VarPackage are counted once each");
    kernel_test_check(
        test,
        nesting != NULL && nesting->length == 2u && values_value_is_integer(values_element(nesting, 0u), 0x22u) &&
            inner != NULL && inner->length == 2u &&
            values_value_holds(values_element(inner, 0u),
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, "cd", 2u) &&
            values_element(inner, 1u)->kind ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_NAME_REFERENCE,
        "the nested Package holds \"cd\", then STR0 as a name");
    kernel_test_check(test, values_live_objects() == 11u,
                      "the heap holds \\_OS_ and the ten objects the Names keep, nothing more");
    kernel_test_check(test, values_load.evaluation.failures == 0u, "nothing fails");
    values_close(test);
}

/**
 * @brief An operand converts to the Integer its operator needs (§5.2): a String reads as hexadecimal
 *        up to its first non-hexadecimal character, with a 0x prefix accepted; a Buffer reads its
 *        first 8 bytes, its first 4 under a revision-1 table; an empty String and an empty Buffer give
 *        0. Those three cases, which ACPI forbids, are counted; a Package cannot convert, and fails
 *        with type_mismatch.
 */
KERNEL_TEST(an_operand_converts_to_the_integer_its_operator_needs)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '1',   0x0Au, 0x07u, 0x08u, 'R',
        '0',   '0',   '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '3',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',
        '4',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '5',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '6',   0x0Au,
        0x07u, 0x08u, 'R',   '0',   '0',   '7',   0x0Au, 0x07u, 0x70u, 0x72u, 0x0Du, '1',   'F',   0x00u, 0x0Au, 0x01u,
        0x00u, 'R',   '0',   '0',   '0',   0x70u, 0x72u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x0Au, 0x00u, 0x00u,
        'R',   '0',   '0',   '1',   0x70u, 0x72u, 0x0Du, 0x30u, 0x78u, '1',   '0',   0x00u, 0x0Au, 0x00u, 0x00u, 'R',
        '0',   '0',   '2',   0x70u, 0x72u, 0x0Du, 0x00u, 0x0Au, 0x05u, 0x00u, 'R',   '0',   '0',   '3',   0x70u, 0x72u,
        0x0Du, '1',   '2',   'G',   '4',   0x00u, 0x0Au, 0x00u, 0x00u, 'R',   '0',   '0',   '4',   0x70u, 0x72u, 0x12u,
        0x04u, 0x01u, 0x0Au, 0x01u, 0x0Au, 0x00u, 0x00u, 'R',   '0',   '0',   '5',   0x70u, 0x72u, 0x11u, 0x0Cu, 0x0Au,
        0x09u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x00u, 0x00u, 'R',   '0',   '0',
        '6',   0x70u, 0x72u, 0x11u, 0x03u, 0x0Au, 0x00u, 0x0Au, 0x03u, 0x00u, 'R',   '0',   '0',   '7',
    };

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_integer("R000") == 0x20u, "Add (\"1F\", 1) is 0x20");
    kernel_test_check(test, values_integer("R001") == 0x0201u, "Add (Buffer { 1, 2 }, 0) is 0x0201");
    kernel_test_check(test, values_integer("R002") == 0x10u, "Add (\"0x10\", 0) is 0x10");
    kernel_test_check(test, values_integer("R003") == 5u, "Add (\"\", 5) is 5");
    kernel_test_check(test, values_integer("R004") == 0x12u, "Add (\"12G4\", 0) stops at G: 0x12");
    kernel_test_check(
        test,
        values_integer("R005") == 7u &&
            values_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH) == 1u,
        "Add (Package { 1 }, 0) fails with type_mismatch and stores nothing");
    kernel_test_check(test, values_integer("R006") == 0x0807060504030201ull, "a Buffer of 9 bytes reads its first 8");
    kernel_test_check(test, values_integer("R007") == 3u, "Add (Buffer (0) {}, 3) is 3");
    kernel_test_check(test, values_load.evaluation.forbidden_conversions == 3u,
                      "the 0x prefix, the empty String and the empty Buffer are counted");
    values_close(test);
    if (!values_run(test, 1u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_integer("R006") == 0x04030201u && values_integer("R001") == 0x0201u,
                      "under revision 1, a Buffer reads its first 4 bytes");
    values_close(test);
}

/**
 * @brief A Store to a named object of fixed type copies the source's bytes into it, keeping its size,
 *        as Windows does (§5.2): an Integer takes a String's first 8 characters or a Buffer's bytes; a
 *        String keeps its length, cut or zero-filled; a Buffer keeps its size. A Package takes a copy
 *        of a Package and refuses anything else, and a Device refuses any store, as type_mismatch;
 *        Store gives what it stored.
 */
KERNEL_TEST(a_store_to_a_named_object_copies_into_it_and_keeps_its_size)
{
    static const uint8_t aml[] = {
        0x08u, 'I',   'N',   'T',   '0',   0x0Bu, 0x34u, 0x12u, 0x08u, 'I',   'N',   'T',   '1',   0x0Au, 0x00u, 0x08u,
        'S',   'T',   'R',   '0',   0x0Du, 'A',   'B',   'C',   0x00u, 0x08u, 'S',   'T',   'R',   '1',   0x0Du, 'A',
        'B',   'C',   'D',   'E',   0x00u, 0x08u, 'B',   'U',   'F',   '0',   0x11u, 0x07u, 0x0Au, 0x04u, 0x09u, 0x09u,
        0x09u, 0x09u, 0x08u, 'B',   'U',   'F',   '1',   0x11u, 0x03u, 0x0Au, 0x01u, 0x08u, 'P',   'K',   'G',   '0',
        0x12u, 0x04u, 0x01u, 0x0Au, 0x01u, 0x08u, 'P',   'K',   'G',   '1',   0x12u, 0x04u, 0x01u, 0x0Au, 0x01u, 0x5Bu,
        0x82u, 0x05u, 'D',   'E',   'V',   '0',   0x08u, 'I',   'N',   'T',   '2',   0x0Au, 0x00u, 0x70u, 0x0Du, 'A',
        'B',   'C',   'D',   'E',   'F',   'G',   'H',   'I',   'J',   0x00u, 'I',   'N',   'T',   '0',   0x70u, 0x11u,
        0x05u, 0x0Au, 0x02u, 0x01u, 0x02u, 'I',   'N',   'T',   '1',   0x70u, 0x0Du, 'H',   'E',   'L',   'L',   'O',
        0x20u, 'W',   'O',   'R',   'L',   'D',   0x00u, 'S',   'T',   'R',   '0',   0x70u, 0x0Au, 'A',   'S',   'T',
        'R',   '1',   0x70u, 0x0Du, 'A',   'B',   0x00u, 'B',   'U',   'F',   '0',   0x70u, 0x0Bu, 0x02u, 0x01u, 'B',
        'U',   'F',   '1',   0x70u, 0x12u, 0x08u, 0x02u, 0x0Au, 0x02u, 0x0Du, 'X',   'Y',   0x00u, 'P',   'K',   'G',
        '0',   0x70u, 0x0Au, 0x05u, 'P',   'K',   'G',   '1',   0x70u, 0x0Au, 0x01u, 'D',   'E',   'V',   '0',   0x70u,
        0x70u, 0x0Au, '3',   'I',   'N',   'T',   '2',   0x5Bu, 0x31u,
    };
    static const uint8_t ascii_bytes[] = {0x41u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t buffer_four[] = {0x41u, 0x42u, 0x00u, 0x00u};
    static const uint8_t buffer_one[] = {0x02u};

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *replaced =
        values_object("PKG0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *kept =
        values_object("PKG1", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE);

    kernel_test_check(test, values_integer("INT0") == 0x4847464544434241ull,
                      "an Integer takes \"ABCDEFGHIJ\"'s first 8 characters");
    kernel_test_check(test, values_integer("INT1") == 0x0201u, "an Integer takes Buffer { 1, 2 }");
    kernel_test_check(test, values_string_is("STR0", "HEL"), "\"ABC\" takes \"HELLO WORLD\" cut to 3 characters");
    kernel_test_check(test,
                      values_value_holds(&made_up_root_node(&values_load, "STR1")->value,
                                         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                         ascii_bytes, sizeof(ascii_bytes)),
                      "\"ABCDE\" takes 0x41 and keeps its 5 bytes, zero past the first");
    kernel_test_check(test, values_buffer_is("BUF0", buffer_four, sizeof(buffer_four)),
                      "a Buffer of 4 takes \"AB\" zero-filled");
    kernel_test_check(test, values_buffer_is("BUF1", buffer_one, sizeof(buffer_one)),
                      "a Buffer of 1 takes 0x0102's low byte");
    kernel_test_check(
        test,
        replaced != NULL && replaced->length == 2u && values_value_is_integer(values_element(replaced, 0u), 2u) &&
            values_value_holds(values_element(replaced, 1u),
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, "XY", 2u),
        "a Package takes a copy of Package { 2, \"XY\" }");
    kernel_test_check(
        test,
        kept != NULL && kept->length == 1u && values_value_is_integer(values_element(kept, 0u), 1u) &&
            made_up_root_node(&values_load, "DEV0")->type ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE &&
            values_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH) == 2u,
        "a Package refuses 5 and a Device refuses 1, both as type_mismatch");
    kernel_test_check(test, values_integer("INT2") == 0x33u && values_load.evaluation.debug_stores == 1u,
                      "Store (Store (0x33, INT2), Debug) stores 0x33 twice");
    values_close(test);
}

/**
 * @brief A Store to a local or an argument overwrites it with a copy (§5.2): a local that took a
 *        named Buffer keeps its bytes after the Buffer changes, and a store to an argument leaves the
 *        caller's object alone. CopyObject gives a Name the type of what it copies.
 */
KERNEL_TEST(a_store_to_a_local_copies_and_copy_object_replaces_the_type)
{
    static const uint8_t aml[] = {
        0x08u, 'B',   'U',   'F',   '0',   0x11u, 0x06u, 0x0Au, 0x03u, 0x01u, 0x02u, 0x03u, 0x08u, 'R', 'E',
        'S',   '0',   0x0Au, 0x00u, 0x08u, 'R',   'E',   'S',   '1',   0x0Au, 0x00u, 0x08u, 'I',   'N', 'T',
        '0',   0x0Au, 0x05u, 0x14u, 0x15u, 'M',   'C',   'P',   'Y',   0x00u, 0x70u, 'B',   'U',   'F', '0',
        0x60u, 0x70u, 0x0Au, 0xFFu, 'B',   'U',   'F',   '0',   0xA4u, 0x60u, 0x14u, 0x0Eu, 'M',   'A', 'R',
        'G',   0x01u, 0x70u, 0x0Du, 'Z',   'Z',   0x00u, 0x68u, 0xA4u, 0x68u, 0x9Du, 'M',   'C',   'P', 'Y',
        'R',   'E',   'S',   '0',   0x9Du, 'M',   'A',   'R',   'G',   'B',   'U',   'F',   '0',   'R', 'E',
        'S',   '1',   0x9Du, 0x0Du, 'S',   'T',   'R',   0x00u, 'I',   'N',   'T',   '0',
    };
    static const uint8_t original[] = {0x01u, 0x02u, 0x03u};
    static const uint8_t changed[] = {0xFFu, 0x00u, 0x00u};

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_buffer_is("RES0", original, sizeof(original)),
                      "Local0 keeps the bytes BUF0 had when it was stored");
    kernel_test_check(test, values_buffer_is("BUF0", changed, sizeof(changed)),
                      "BUF0 took 0xFF and kept its 3 bytes, the store to Arg0 apart");
    kernel_test_check(test, values_string_is("RES1", "ZZ"), "Arg0 = \"ZZ\" overwrites the argument");
    kernel_test_check(test,
                      values_string_is("INT0", "STR") &&
                          made_up_root_node(&values_load, "INT0")->type ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_STRING,
                      "CopyObject (\"STR\", INT0) makes INT0 a String");
    kernel_test_check(test, values_load.evaluation.failures == 0u, "nothing fails");
    values_close(test);
}

/**
 * @brief Strings and buffers compare by their bytes, then by their lengths; in a mixed comparison the
 *        second operand converts to the first's type, counted (§5.2); LAnd and LOr read a Buffer's
 *        first 4 bytes, as Windows does.
 */
KERNEL_TEST(strings_and_buffers_compare_by_their_bytes)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '1',   0x0Au, 0x07u, 0x08u, 'R',
        '0',   '0',   '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '3',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',
        '4',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '5',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '6',   0x0Au,
        0x07u, 0x08u, 'R',   '0',   '0',   '7',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '8',   0x0Au, 0x07u, 0x08u,
        'R',   '0',   '0',   '9',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',
        '1',   '1',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '3',
        0x0Au, 0x07u, 0x70u, 0x93u, 0x0Du, 'A',   'B',   'C',   0x00u, 0x0Du, 'A',   'B',   'C',   0x00u, 'R',   '0',
        '0',   '0',   0x70u, 0x93u, 0x0Du, 'A',   'B',   'C',   0x00u, 0x0Du, 'A',   'B',   'D',   0x00u, 'R',   '0',
        '0',   '1',   0x70u, 0x95u, 0x0Du, 'A',   'B',   'C',   0x00u, 0x0Du, 'A',   'B',   'D',   0x00u, 'R',   '0',
        '0',   '2',   0x70u, 0x94u, 0x0Du, 'A',   'B',   'C',   'D',   0x00u, 0x0Du, 'A',   'B',   'C',   0x00u, 'R',
        '0',   '0',   '3',   0x70u, 0x93u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u,
        0x02u, 'R',   '0',   '0',   '4',   0x70u, 0x95u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x11u, 0x05u, 0x0Au,
        0x02u, 0x01u, 0x03u, 'R',   '0',   '0',   '5',   0x70u, 0x93u, 0x0Au, 0x1Fu, 0x0Du, '1',   'F',   0x00u, 'R',
        '0',   '0',   '6',   0x70u, 0x93u, 0x0Du, '1',   'F',   0x00u, 0x0Au, 0x1Fu, 'R',   '0',   '0',   '7',   0x70u,
        0x93u, 0x0Du, '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',   '0',
        '1',   'F',   0x00u, 0x0Au, 0x1Fu, 'R',   '0',   '0',   '8',   0x70u, 0x90u, 0x11u, 0x08u, 0x0Au, 0x05u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x01u, 0x0Au, 0x01u, 'R',   '0',   '0',   '9',   0x70u, 0x91u, 0x11u, 0x08u, 0x0Au, 0x05u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x0Au, 0x00u, 'R',   '0',   '1',   '0',   0x70u, 0x92u, 0x0Du, 0x30u, 0x00u,
        'R',   '0',   '1',   '1',   0x70u, 0x95u, 0x0Du, 'A',   'B',   0x00u, 0x0Du, 'A',   'B',   'C',   0x00u, 'R',
        '0',   '1',   '2',   0x70u, 0x93u, 0x0Du, 'A',   'B',   0x00u, 0x0Du, 'A',   'B',   'C',   0x00u, 'R',   '0',
        '1',   '3',
    };

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_integer("R000") == VALUES_ONES, "\"ABC\" equals \"ABC\"");
    kernel_test_check(test, values_integer("R001") == 0u, "\"ABC\" differs from \"ABD\"");
    kernel_test_check(test, values_integer("R002") == VALUES_ONES, "\"ABC\" is less than \"ABD\"");
    kernel_test_check(test, values_integer("R003") == VALUES_ONES, "\"ABCD\" is greater than its prefix \"ABC\"");
    kernel_test_check(test, values_integer("R004") == VALUES_ONES, "Buffer { 1, 2 } equals Buffer { 1, 2 }");
    kernel_test_check(test, values_integer("R005") == VALUES_ONES, "Buffer { 1, 2 } is less than Buffer { 1, 3 }");
    kernel_test_check(test, values_integer("R006") == VALUES_ONES, "0x1F equals \"1F\" read as an Integer");
    kernel_test_check(test, values_integer("R007") == 0u, "\"1F\" differs from 0x1F written as a String");
    kernel_test_check(test, values_integer("R008") == VALUES_ONES,
                      "\"000000000000001F\" equals 0x1F written as a String");
    kernel_test_check(test, values_load.evaluation.mixed_type_comparisons == 3u,
                      "the three mixed comparisons are counted");
    kernel_test_check(test, values_integer("R009") == 0u && values_integer("R010") == 0u,
                      "LAnd and LOr read Buffer { 0, 0, 0, 0, 1 } as 0");
    kernel_test_check(test, values_integer("R011") == VALUES_ONES, "LNot (\"0\") is True");
    kernel_test_check(test, values_integer("R012") == VALUES_ONES && values_integer("R013") == 0u,
                      "\"AB\" is less than \"ABC\", and differs from it");
    kernel_test_check(test, values_load.evaluation.failures == 0u, "nothing fails");
    values_close(test);
}

/**
 * @brief Concatenate gives the type of its first operand and converts the second to it (§5.2): two
 *        Integers make a Buffer of 16 bytes; a String takes an Integer as 16 hexadecimal digits and
 *        a Buffer as two digits per byte, separated by spaces; a Buffer takes a String with its NUL
 *        and an Integer as 8 bytes. Its target takes the result as a Store does.
 */
KERNEL_TEST(concatenate_gives_the_type_of_its_first_operand)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '1',   0x0Au, 0x00u, 0x08u,
        'R',   '0',   '0',   '2',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '3',   0x0Au, 0x00u, 0x08u, 'R',
        '0',   '0',   '4',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '5',   0x0Au, 0x00u, 0x08u, 'R',   '0',
        '0',   '6',   0x0Au, 0x00u, 0x9Du, 0x73u, 0x0Bu, 0x02u, 0x01u, 0x0Bu, 0x04u, 0x03u, 0x00u, 'R',   '0',
        '0',   '0',   0x9Du, 0x73u, 0x0Du, 'A',   'B',   0x00u, 0x0Du, 'C',   'D',   0x00u, 0x00u, 'R',   '0',
        '0',   '1',   0x9Du, 0x73u, 0x0Du, 'A',   'B',   0x00u, 0x0Au, 0x1Fu, 0x00u, 'R',   '0',   '0',   '2',
        0x9Du, 0x73u, 0x11u, 0x04u, 0x0Au, 0x01u, 0x01u, 0x0Du, 'A',   'B',   0x00u, 0x00u, 'R',   '0',   '0',
        '3',   0x9Du, 0x73u, 0x11u, 0x04u, 0x0Au, 0x01u, 0x01u, 0x0Bu, 0x02u, 0x01u, 0x00u, 'R',   '0',   '0',
        '4',   0x9Du, 0x73u, 0x0Du, 'A',   'B',   0x00u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x01u, 0xABu, 0x00u, 'R',
        '0',   '0',   '5',   0x73u, 0x0Du, 0x58u, 0x00u, 0x0Du, 0x59u, 0x00u, 'R',   '0',   '0',   '6',
    };
    static const uint8_t integers[] = {0x02u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                       0x04u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t with_string[] = {0x01u, 0x41u, 0x42u, 0x00u};
    static const uint8_t with_integer[] = {0x01u, 0x02u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_buffer_is("R000", integers, sizeof(integers)),
                      "Concatenate (0x0102, 0x0304) is a Buffer of two 8-byte integers");
    kernel_test_check(test, values_string_is("R001", "ABCD"), "Concatenate (\"AB\", \"CD\") is \"ABCD\"");
    kernel_test_check(test, values_string_is("R002", "AB000000000000001F"),
                      "Concatenate (\"AB\", 0x1F) writes 0x1F as 16 hexadecimal digits");
    kernel_test_check(test, values_buffer_is("R003", with_string, sizeof(with_string)),
                      "Concatenate (Buffer { 1 }, \"AB\") takes the String with its NUL");
    kernel_test_check(test, values_buffer_is("R004", with_integer, sizeof(with_integer)),
                      "Concatenate (Buffer { 1 }, 0x0102) takes 8 bytes of the Integer");
    kernel_test_check(test, values_string_is("R005", "AB01 AB"),
                      "Concatenate (\"AB\", Buffer { 1, 0xAB }) writes the Buffer as \"01 AB\"");
    kernel_test_check(test, values_integer("R006") == 0x5958u, "the Integer target takes \"XY\"'s bytes");
    kernel_test_check(test, values_load.evaluation.mixed_type_comparisons == 4u, "the four mixed operands are counted");
    kernel_test_check(test, values_load.evaluation.failures == 0u, "nothing fails");
    values_close(test);
}

/**
 * @brief The explicit conversions give what ACPI and Windows give: ToDecimalString and ToHexString of
 *        an Integer, a Buffer and a String; ToString up to a NUL or a length; ToBuffer of a String with
 *        its NUL and of an Integer as 8 bytes; ToInteger of a String in decimal, in hexadecimal past a
 *        0x prefix and in octal past a leading 0, past spaces and a sign, saturated at Ones, and of a
 *        Buffer.
 */
KERNEL_TEST(the_explicit_conversions_give_what_acpi_and_windows_give)
{
    static const uint8_t aml[] = {
        0x08u, 'S',   '0',   '0',   '0',   0x0Au, 0x00u, 0x08u, 'S',   '0',   '0',   '1',   0x0Au, 0x00u, 0x08u, 'S',
        '0',   '0',   '2',   0x0Au, 0x00u, 0x08u, 'S',   '0',   '0',   '3',   0x0Au, 0x00u, 0x08u, 'S',   '0',   '0',
        '4',   0x0Au, 0x00u, 0x08u, 'S',   '0',   '0',   '5',   0x0Au, 0x00u, 0x08u, 'S',   '0',   '0',   '6',   0x0Au,
        0x00u, 0x08u, 'S',   '0',   '0',   '7',   0x0Au, 0x00u, 0x08u, 'S',   '0',   '0',   '8',   0x0Au, 0x00u, 0x08u,
        'B',   '0',   '0',   '0',   0x0Au, 0x00u, 0x08u, 'B',   '0',   '0',   '1',   0x0Au, 0x00u, 0x08u, 'B',   '0',
        '0',   '2',   0x0Au, 0x00u, 0x08u, 'I',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'I',   '0',   '0',   '1',
        0x0Au, 0x07u, 0x08u, 'I',   '0',   '0',   '2',   0x0Au, 0x07u, 0x08u, 'I',   '0',   '0',   '3',   0x0Au, 0x07u,
        0x08u, 'I',   '0',   '0',   '4',   0x0Au, 0x07u, 0x08u, 'I',   '0',   '0',   '5',   0x0Au, 0x07u, 0x08u, 'I',
        '0',   '0',   '6',   0x0Au, 0x07u, 0x08u, 'I',   '0',   '0',   '7',   0x0Au, 0x07u, 0x9Du, 0x97u, 0x0Bu, 0xD2u,
        0x04u, 0x00u, 'S',   '0',   '0',   '0',   0x9Du, 0x97u, 0x11u, 0x05u, 0x0Au, 0x02u, 0x07u, 0xC8u, 0x00u, 'S',
        '0',   '0',   '1',   0x9Du, 0x98u, 0x0Au, 0x2Au, 0x00u, 'S',   '0',   '0',   '2',   0x9Du, 0x98u, 0x11u, 0x05u,
        0x0Au, 0x02u, 0x0Au, 0xFFu, 0x00u, 'S',   '0',   '0',   '3',   0x9Du, 0x98u, 0x0Du, 'X',   'Y',   0x00u, 0x00u,
        'S',   '0',   '0',   '4',   0x9Du, 0x9Cu, 0x11u, 0x07u, 0x0Au, 0x04u, 'A',   'B',   0x00u, 0x43u, 0xFFu, 0x00u,
        'S',   '0',   '0',   '5',   0x9Du, 0x9Cu, 0x11u, 0x06u, 0x0Au, 0x03u, 'A',   'B',   'C',   0x0Au, 0x02u, 0x00u,
        'S',   '0',   '0',   '6',   0x9Du, 0x97u, 0x11u, 0x03u, 0x0Au, 0x00u, 0x00u, 'S',   '0',   '0',   '7',   0x9Du,
        0x98u, 0x0Au, 0x00u, 0x00u, 'S',   '0',   '0',   '8',   0x9Du, 0x96u, 0x0Du, 'H',   'I',   0x00u, 0x00u, 'B',
        '0',   '0',   '0',   0x9Du, 0x96u, 0x0Bu, 0x02u, 0x01u, 0x00u, 'B',   '0',   '0',   '1',   0x9Du, 0x96u, 0x11u,
        0x04u, 0x0Au, 0x01u, 0x05u, 0x00u, 'B',   '0',   '0',   '2',   0x70u, 0x99u, 0x0Du, 0x20u, 0x20u, '4',   '2',
        0x00u, 0x00u, 'I',   '0',   '0',   '0',   0x70u, 0x99u, 0x0Du, 0x30u, 0x78u, '2',   'A',   0x00u, 0x00u, 'I',
        '0',   '0',   '1',   0x70u, 0x99u, 0x0Du, '0',   '5',   '2',   0x00u, 0x00u, 'I',   '0',   '0',   '2',   0x70u,
        0x99u, 0x0Du, 0x2Du, 0x31u, 0x00u, 0x00u, 'I',   '0',   '0',   '3',   0x70u, 0x99u, 0x0Du, '9',   '9',   '9',
        '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',   '9',
        '9',   '9',   '9',   '9',   0x00u, 0x00u, 'I',   '0',   '0',   '4',   0x70u, 0x99u, 0x11u, 0x05u, 0x0Au, 0x02u,
        0x2Au, 0x00u, 0x00u, 'I',   '0',   '0',   '5',   0x70u, 0x99u, 0x0Du, '4',   '2',   'A',   'B',   'C',   0x00u,
        0x00u, 'I',   '0',   '0',   '6',   0x70u, 0x99u, 0x0Au, 0x2Au, 0x00u, 'I',   '0',   '0',   '7',
    };
    static const uint8_t from_string[] = {0x48u, 0x49u, 0x00u};
    static const uint8_t from_integer[] = {0x02u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t from_buffer[] = {0x05u};

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_string_is("S000", "1234"), "ToDecimalString (1234) is \"1234\"");
    kernel_test_check(test, values_string_is("S001", "7,200"), "ToDecimalString (Buffer { 7, 200 }) is \"7,200\"");
    kernel_test_check(test, values_string_is("S002", "0x2A"), "ToHexString (0x2A) is \"0x2A\"");
    kernel_test_check(test, values_string_is("S003", "0x0A,0xFF"),
                      "ToHexString (Buffer { 0x0A, 0xFF }) is \"0x0A,0xFF\"");
    kernel_test_check(test, values_string_is("S004", "XY"), "ToHexString (\"XY\") is \"XY\"");
    kernel_test_check(test, values_string_is("S005", "AB"), "ToString stops at the NUL");
    kernel_test_check(test, values_string_is("S006", "AB"), "ToString stops at its length");
    kernel_test_check(test, values_string_is("S007", ""), "ToDecimalString of an empty Buffer is \"\"");
    kernel_test_check(test, values_string_is("S008", "0x0"), "ToHexString (0) is \"0x0\"");
    kernel_test_check(test, values_buffer_is("B000", from_string, sizeof(from_string)),
                      "ToBuffer (\"HI\") keeps the NUL");
    kernel_test_check(test, values_buffer_is("B001", from_integer, sizeof(from_integer)),
                      "ToBuffer (0x0102) is 8 bytes");
    kernel_test_check(test, values_buffer_is("B002", from_buffer, sizeof(from_buffer)),
                      "ToBuffer (Buffer { 5 }) is a copy");
    kernel_test_check(test, values_integer("I000") == 42u, "ToInteger (\"  42\") skips the spaces");
    kernel_test_check(test, values_integer("I001") == 42u, "ToInteger (\"0x2A\") reads hexadecimal");
    kernel_test_check(test, values_integer("I002") == 42u, "ToInteger (\"052\") reads octal");
    kernel_test_check(test, values_integer("I003") == VALUES_ONES, "ToInteger (\"-1\") is Ones");
    kernel_test_check(test, values_integer("I004") == VALUES_ONES, "a decimal past 64 bits saturates at Ones");
    kernel_test_check(test, values_integer("I005") == 42u, "ToInteger (Buffer { 0x2A, 0 }) is 42");
    kernel_test_check(test, values_integer("I006") == 42u, "ToInteger (\"42ABC\") stops at A");
    kernel_test_check(test, values_integer("I007") == 42u, "ToInteger (0x2A) is 0x2A");
    kernel_test_check(test, values_load.evaluation.failures == 0u, "nothing fails");
    values_close(test);
}

/**
 * @brief Mid takes a slice of a String or a Buffer, empty past its end; SizeOf reads a String's, a
 *        Buffer's or a Package's length, and fails on an Integer as type_mismatch; ObjectType gives
 *        the ACPI number of every kind of object, 0 for an uninitialized local; a Name in a method
 *        holds a Buffer whose size an argument gives.
 */
KERNEL_TEST(mid_size_of_and_object_type_read_what_they_are_given)
{
    static const uint8_t aml[] = {
        0x08u, 'I',   'N',   'T',   '0',   0x0Au, 0x01u, 0x08u, 'S',   'T',   'R',   '0',   0x0Du, 'A',   'B',   'C',
        'D',   0x00u, 0x08u, 'B',   'U',   'F',   '0',   0x11u, 0x03u, 0x0Au, 0x06u, 0x08u, 'P',   'K',   'G',   '0',
        0x12u, 0x02u, 0x03u, 0x5Bu, 0x82u, 0x13u, 'D',   'E',   'V',   '0',   0x08u, '_',   'H',   'I',   'D',   0x0Du,
        'L',   'P',   'L',   '0',   '0',   '0',   '0',   0x00u, 0x5Bu, 0x01u, 'M',   'T',   'X',   '0',   0x00u, 0x5Bu,
        0x80u, 'R',   'E',   'G',   '0',   0x00u, 0x0Bu, 0x00u, 0x10u, 0x0Au, 0x10u, 0x5Bu, 0x81u, 0x0Bu, 'R',   'E',
        'G',   '0',   0x01u, 'F',   'L',   'D',   '0',   0x08u, 0x14u, 0x09u, 'M',   'T',   'H',   'D',   0x00u, 0xA4u,
        0x0Au, 0x00u, 0x14u, 0x0Eu, 'M',   'L',   'O',   'C',   0x00u, 0x70u, 0x12u, 0x02u, 0x05u, 0x60u, 0xA4u, 0x87u,
        0x60u, 0x14u, 0x09u, 'M',   'U',   'N',   'I',   0x00u, 0xA4u, 0x8Eu, 0x61u, 0x14u, 0x14u, 'M',   'N',   'A',
        'M',   0x01u, 0x08u, 'T',   'M',   'P',   '0',   0x11u, 0x02u, 0x68u, 0xA4u, 0x87u, 'T',   'M',   'P',   '0',
        0x08u, 'M',   '0',   '0',   '0',   0x0Au, 0x00u, 0x08u, 'M',   '0',   '0',   '1',   0x0Au, 0x00u, 0x08u, 'M',
        '0',   '0',   '2',   0x0Au, 0x00u, 0x08u, 'M',   '0',   '0',   '3',   0x0Au, 0x00u, 0x08u, 'M',   '0',   '0',
        '4',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'Z',   '0',   '0',   '1',   0x0Au,
        0x00u, 0x08u, 'Z',   '0',   '0',   '2',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '0',   '3',   0x0Au, 0x00u, 0x08u,
        'Z',   '0',   '0',   '4',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '0',   '5',   0x0Au, 0x00u, 0x08u, 'Z',   '0',
        '0',   '6',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '0',   '7',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '0',   '8',
        0x0Au, 0x00u, 0x08u, 'Z',   '0',   '0',   '9',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '1',   '0',   0x0Au, 0x00u,
        0x08u, 'Z',   '0',   '1',   '1',   0x0Au, 0x00u, 0x08u, 'Z',   '0',   '1',   '2',   0x0Au, 0x07u, 0x08u, 'Z',
        '0',   '1',   '3',   0x0Au, 0x07u, 0x08u, 'Z',   '0',   '1',   '4',   0x0Au, 0x07u, 0x08u, 'Z',   '0',   '1',
        '5',   0x0Au, 0x07u, 0x9Du, 0x9Eu, 0x0Du, 'A',   'B',   'C',   'D',   'E',   'F',   0x00u, 0x0Au, 0x02u, 0x0Au,
        0x03u, 0x00u, 'M',   '0',   '0',   '0',   0x9Du, 0x9Eu, 0x0Du, 'A',   'B',   'C',   0x00u, 0x0Au, 0x01u, 0x0Au,
        0x0Au, 0x00u, 'M',   '0',   '0',   '1',   0x9Du, 0x9Eu, 0x11u, 0x06u, 0x0Au, 0x03u, 0x01u, 0x02u, 0x03u, 0x0Au,
        0x05u, 0x0Au, 0x01u, 0x00u, 'M',   '0',   '0',   '2',   0x9Du, 0x9Eu, 0x0Du, 'A',   'B',   'C',   0x00u, 0x0Au,
        0x00u, 0x0Au, 0x00u, 0x00u, 'M',   '0',   '0',   '3',   0x9Du, 0x9Eu, 0x11u, 0x07u, 0x0Au, 0x04u, 0x01u, 0x02u,
        0x03u, 0x04u, 0x0Au, 0x01u, 0x0Au, 0x02u, 0x00u, 'M',   '0',   '0',   '4',   0x70u, 0x87u, 'S',   'T',   'R',
        '0',   'Z',   '0',   '0',   '1',   0x70u, 0x87u, 'B',   'U',   'F',   '0',   'Z',   '0',   '0',   '2',   0x70u,
        0x87u, 'P',   'K',   'G',   '0',   'Z',   '0',   '0',   '3',   0x70u, 'M',   'L',   'O',   'C',   'Z',   '0',
        '0',   '4',   0x70u, 0x8Eu, 'I',   'N',   'T',   '0',   'Z',   '0',   '0',   '5',   0x70u, 0x8Eu, 'S',   'T',
        'R',   '0',   'Z',   '0',   '0',   '6',   0x70u, 0x8Eu, 'B',   'U',   'F',   '0',   'Z',   '0',   '0',   '7',
        0x70u, 0x8Eu, 'P',   'K',   'G',   '0',   'Z',   '0',   '0',   '8',   0x70u, 0x8Eu, 'D',   'E',   'V',   '0',
        'Z',   '0',   '0',   '9',   0x70u, 0x8Eu, 'M',   'T',   'H',   'D',   'Z',   '0',   '1',   '0',   0x70u, 0x8Eu,
        'M',   'T',   'X',   '0',   'Z',   '0',   '1',   '1',   0x70u, 0x8Eu, 'R',   'E',   'G',   '0',   'Z',   '0',
        '1',   '4',   0x70u, 0x8Eu, 'F',   'L',   'D',   '0',   'Z',   '0',   '1',   '2',   0x70u, 'M',   'U',   'N',
        'I',   'Z',   '0',   '1',   '3',   0x70u, 0x87u, 'I',   'N',   'T',   '0',   'Z',   '0',   '0',   '0',   0x70u,
        'M',   'N',   'A',   'M',   0x0Au, 0x09u, 'Z',   '0',   '1',   '5',
    };
    static const uint8_t slice[] = {0x02u, 0x03u};

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_check(test, values_string_is("M000", "CDE"), "Mid (\"ABCDEF\", 2, 3) is \"CDE\"");
    kernel_test_check(test, values_string_is("M001", "BC"), "Mid (\"ABC\", 1, 10) stops at the end: \"BC\"");
    kernel_test_check(test, values_buffer_is("M002", slice, 0u), "Mid past a Buffer's end is an empty Buffer");
    kernel_test_check(test, values_string_is("M003", ""), "Mid of length 0 is \"\"");
    kernel_test_check(test, values_buffer_is("M004", slice, sizeof(slice)), "Mid (Buffer { 1, 2, 3, 4 }, 1, 2)");
    kernel_test_check(test,
                      values_integer("Z001") == 4u && values_integer("Z002") == 6u && values_integer("Z003") == 3u &&
                          values_integer("Z004") == 5u,
                      "SizeOf reads 4, 6, 3 and a local's 5");
    kernel_test_check(
        test,
        values_integer("Z000") == 7u &&
            values_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH) == 1u,
        "SizeOf (INT0) fails with type_mismatch");
    kernel_test_check(test,
                      values_integer("Z005") == 1u && values_integer("Z006") == 2u && values_integer("Z007") == 3u &&
                          values_integer("Z008") == 4u && values_integer("Z009") == 6u &&
                          values_integer("Z010") == 8u && values_integer("Z011") == 9u &&
                          values_integer("Z014") == 10u && values_integer("Z012") == 5u,
                      "ObjectType gives 1, 2, 3, 4, 6, 8, 9, 10 and 5");
    kernel_test_check(test, values_integer("Z013") == 0u, "ObjectType of an uninitialized local is 0");
    kernel_test_check(test, values_integer("Z015") == 9u, "Name (TMP0, Buffer (Arg0) {}) holds 9 bytes");
    values_close(test);
}

/**
 * @brief An evaluation gives back every object it built (§3.5): a method that builds strings, buffers
 *        and nested packages, run twice, leaves the heap holding \_OS_ alone; so do table-level locals
 *        that hold a String and a Package, once their table is loaded, a method cut short in the middle
 *        of an operator, a String without its NUL, both truncated, and a VarPackage of 0xFFFFFFFF
 *        elements, refused as limit_package_elements (§6.3).
 */
KERNEL_TEST(an_evaluation_gives_back_every_object_it_built)
{
    static const uint8_t aml[] = {
        0x08u, 'R',   'E',   'S',   '0',   0x0Au, 0x07u, 0x14u, '1',   'M',   'T',   'M',   'P',   0x00u, 0x70u, 0x0Du,
        'A',   'B',   'C',   0x00u, 0x60u, 0x70u, 0x11u, 0x03u, 0x0Au, 0x08u, 0x61u, 0x70u, 0x12u, 0x10u, 0x02u, 0x0Du,
        0x58u, 0x00u, 0x12u, 0x0Au, 0x02u, 0x0Du, 0x59u, 0x00u, 0x11u, 0x04u, 0x0Au, 0x01u, 0x01u, 0x62u, 0x70u, 0x73u,
        0x60u, 0x0Du, 0x44u, 0x00u, 0x00u, 0x63u, 0xA4u, 0x87u, 0x63u, 0x70u, 'M',   'T',   'M',   'P',   'R',   'E',
        'S',   '0',   0x70u, 'M',   'T',   'M',   'P',   'R',   'E',   'S',   '0',   0x70u, 0x73u, 0x0Du, 0x5Au, 0x00u,
        0x0Du, 0x5Au, 0x00u, 0x00u, 0x60u, 0x70u, 0x12u, 0x05u, 0x01u, 0x0Du, 0x59u, 0x00u, 0x61u, 0x08u, 'R',   'E',
        'S',   '1',   0x0Au, 0x07u, 0x08u, 'R',   'E',   'S',   '2',   0x0Au, 0x07u, 0x08u, 'R',   'E',   'S',   '3',
        0x0Au, 0x07u, 0x14u, 0x10u, 'M',   'C',   'U',   'T',   0x00u, 0x70u, 0x0Du, 'A',   'B',   'C',   0x00u, 0x60u,
        0x70u, 0x73u, 0x60u, 0x70u, 'M',   'C',   'U',   'T',   'R',   'E',   'S',   '1',   0x14u, 0x0Au, 'M',   'U',
        'N',   'T',   0x00u, 0xA4u, 0x0Du, 'A',   'B',   0x70u, 'M',   'U',   'N',   'T',   'R',   'E',   'S',   '2',
        0x14u, 0x0Eu, 'M',   'V',   'P',   'C',   0x00u, 0xA4u, 0x13u, 0x06u, 0x0Cu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x70u,
        'M',   'V',   'P',   'C',   'R',   'E',   'S',   '3',
    };

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    kernel_test_measure(test, "allocations", values_load.heap.objects);
    kernel_test_check(test, values_integer("RES0") == 4u, "MTMP returns SizeOf (\"ABCD\")");
    kernel_test_check(test, values_load.heap.objects > 1u && values_live_objects() == 1u,
                      "the heap built objects and holds \\_OS_ alone afterwards");
    kernel_test_check(test,
                      values_integer("RES1") == 7u && values_integer("RES2") == 7u &&
                          values_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED) ==
                              2u,
                      "the cut Concatenate and the String without its NUL fail as truncated");
    kernel_test_check(
        test,
        values_integer("RES3") == 7u &&
            values_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PACKAGE_ELEMENTS) ==
                1u,
        "VarPackage (0xFFFFFFFF) fails with limit_package_elements");
    values_close(test);
}

/**
 * @brief The object heap reuses what it frees (§3.5): a freed block of a size class serves the next
 *        object of that class, a freed object of 4 KiB or more gives its pages back and its run to the
 *        next one, and releasing a Package frees what it nests.
 */
KERNEL_TEST(the_object_heap_reuses_what_it_frees)
{
    static const uint8_t aml[] = {0x08u, 'R', 'E', 'S', '0', 0x00u};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap = &values_load.heap;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &values_load.space;
    uint32_t first = 0u;
    uint32_t second = 0u;
    uint32_t outer = 0u;
    uint32_t inner = 0u;
    uint32_t text = 0u;

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;

    const uint32_t live = values_live_objects();

    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, 10u, &first);
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, first);
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, 12u, &second);
    kernel_test_check(test, second == first, "a String of 12 takes the block a String of 10 gave back");
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, second);
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, 300u, &first);
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, first);
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, 303u, &second);
    kernel_test_check(test, second == first, "a Buffer of 303 takes the block a Buffer of 300 gave back");
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, second);

    const uint32_t arena = space->arena_bytes;

    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, 5000u, &first);
    kernel_test_check(test, space->arena_bytes == arena + 8192u, "a Buffer of 5,000 takes two pages of its own");
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, first);
    kernel_test_check(test, space->arena_bytes == arena, "and gives them back when freed");
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, 6000u, &second);
    kernel_test_check(test, second == first, "a Buffer of 6,000 takes the run the freed one left");
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, second);
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE, 2u, &outer);
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE, 1u, &inner);
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, 3u, &text);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *outer_elements =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
             *) (advanced_configuration_and_power_interface_machine_language_object(heap, outer) + 1);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *inner_elements =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
             *) (advanced_configuration_and_power_interface_machine_language_object(heap, inner) + 1);

    outer_elements[1].kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE;
    outer_elements[1].handle = inner;
    inner_elements[0].kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING;
    inner_elements[0].handle = text;
    kernel_test_check(test, values_live_objects() == live + 3u, "the Package, its Package and its String live");
    advanced_configuration_and_power_interface_machine_language_object_release(heap, space, outer);
    kernel_test_check(test, values_live_objects() == live, "releasing the outer Package frees all three");
    values_close(test);
}

/**
 * @brief A reference count driven past 0xFFFFFFFF saturates, and the object is never freed (§3.5,
 *        §6.3): released as often as it was retained and more, it stays alive.
 */
KERNEL_TEST(a_saturated_reference_count_is_never_freed)
{
    static const uint8_t aml[] = {0x08u, 'R', 'E', 'S', '0', 0x00u};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageHeap_t *heap = &values_load.heap;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &values_load.space;
    uint32_t handle = 0u;

    if (!values_run(test, 2u, aml, sizeof(aml)))
        return;
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        heap, space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, 3u, &handle);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(heap, handle);
    const uint32_t freed = heap->objects_freed;

    object->references = 0xFFFFFFFEu;
    advanced_configuration_and_power_interface_machine_language_object_retain(heap, handle);
    advanced_configuration_and_power_interface_machine_language_object_retain(heap, handle);
    kernel_test_check(test, object->references == 0xFFFFFFFFu, "the count stops at 0xFFFFFFFF");
    for (uint32_t release = 0u; release < 3u; ++release)
        advanced_configuration_and_power_interface_machine_language_object_release(heap, space, handle);
    kernel_test_check(test, object->references == 0xFFFFFFFFu && heap->objects_freed == freed,
                      "three releases leave it saturated and alive");
    values_close(test);
}
