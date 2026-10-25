#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_references, KERNEL_TEST_STAGE_INITIALIZATION);

#define REFERENCES_TABLE_CAPACITY 2048u                 /**< Bytes of the buffer a made-up DSDT is written in. */
#define REFERENCES_ONES           0xFFFFFFFFFFFFFFFFull /**< Ones, and True, under a revision-2 table. */
#define REFERENCES_FIELD_BYTES    16u                   /**< Bytes of the Buffer the bit-exact field test walks. */
#define REFERENCES_FOLD_OFFSET    0x811C9DC5u           /**< FNV-1a's offset basis. */
#define REFERENCES_FOLD_PRIME     0x01000193u           /**< FNV-1a's prime. */

static uint8_t references_table[REFERENCES_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t references_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t references_load;

/**
 * @brief Loads @p aml as a DSDT of revision 2, running its table-level code.
 */
static bool references_run(KernelTest_t *test, const uint8_t *aml, uint32_t length)
{
    return made_up_load_table(test, &references_load, &references_host, references_table, 2u, aml, length);
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t references_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&references_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Whether a value is a String or a Buffer, as @p kind says, of exactly @p length bytes equal to @p bytes.
 */
static bool references_value_holds(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value,
                                   uint8_t kind, const void *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        (value != NULL && value->kind == kind) ?
            advanced_configuration_and_power_interface_machine_language_object(&references_load.heap, value->handle) :
            NULL;

    return object != NULL && object->length == length && memcmp(object + 1, bytes, length) == 0;
}

/**
 * @brief Whether the Name at the root called @p name holds a value of @p kind of @p length bytes equal to @p bytes.
 */
static bool references_holds(const char *name, uint8_t kind, const void *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&references_load, name);

    return node != NULL && references_value_holds(&node->value, kind, bytes, length);
}

/**
 * @brief Element @p index of the Package the Name at the root called @p name holds, or NULL.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *references_element(const char *name,
                                                                                              uint32_t index)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&references_load, name);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package =
        (node != NULL &&
         node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE) ?
            advanced_configuration_and_power_interface_machine_language_object(&references_load.heap,
                                                                               node->value.handle) :
            NULL;

    if (package == NULL || index >= package->length)
        return NULL;
    return (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1) + index;
}

/**
 * @brief How many runs failed with @p status.
 */
static uint32_t references_failed(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status)
{
    return references_load.evaluation.statuses[status];
}

/**
 * @brief Checks that every run left the evaluation's stacks empty and no release found nothing to
 *        release, then closes the load.
 */
static void references_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &references_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          references_load.temporary_count == 0u,
                      "every run leaves no frame, value, operator, block or temporary node behind");
    kernel_test_check(test, references_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    advanced_configuration_and_power_interface_machine_language_close(&references_load);
    kernel_test_check(test, references_host.outstanding_pages == 0u, "every page goes back to the host");
}

/**
 * @brief RefOf, CondRefOf and DerefOf (§5.2): DerefOf follows a reference to a name and a chain of
 *        references to its bottom, and reads the object a path String names; a store to a local that
 *        holds a reference writes through it, as does a store to an argument that holds one; CondRefOf
 *        is True for a name that exists, \_OSI included, and False for one that does not or that only
 *        an External declares; a reference to a local that a method returns gives its caller the
 *        value (§6.3); a reference to a table's local lives as long as the table's locals do; a store
 *        through a local keeps the type of what it refers to, unless the value is a reference or what
 *        it refers to holds no data, which replace the reference; one to an element that holds a
 *        reference writes through it; a reference to a local or to a name of a method that returned
 *        fails, and Return gives the caller what it referred to instead; a package element that names
 *        the name referring to it fails after 8 names (§5.1).
 */
KERNEL_TEST(references_follow_names_locals_and_arguments)
{
    static const uint8_t aml[] = {
        0xA0u, 0x0Au, 0x00u, 0x15u, 0x5Cu, 'U',   'N',   'D',   'F',   0x01u, 0x00u, 0x08u, 'I',   'N',   'T',   '0',
        0x0Bu, 0x34u, 0x12u, 0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '1',   0x0Au,
        0x07u, 0x08u, 'R',   '0',   '0',   '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '3',   0x0Au, 0x07u, 0x08u,
        'R',   '0',   '0',   '4',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '5',   0x0Au, 0x07u, 0x08u, 'R',   '0',
        '0',   '6',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '7',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '8',
        0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '9',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '0',   0x0Au, 0x07u,
        0x08u, 'R',   '0',   '1',   '1',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '2',   0x0Au, 0x07u, 0x08u, 'R',
        '0',   '1',   '3',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '4',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',
        '5',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '6',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '7',   0x0Au,
        0x07u, 0x08u, 'R',   '0',   '1',   '8',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '9',   0x0Au, 0x07u, 0x08u,
        'R',   '0',   '2',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '2',   '1',   0x0Au, 0x07u, 0x5Bu, 0x82u, 0x05u,
        'D',   'E',   'V',   'R',   0x08u, 'P',   'K',   'G',   'D',   0x12u, 0x06u, 0x01u, 'D',   'E',   'V',   'R',
        0x08u, 'P',   'K',   'G',   'L',   0x12u, 0x06u, 0x01u, 'A',   'L',   'S',   'L',   0x08u, 'A',   'L',   'S',
        'L',   0x0Au, 0x00u, 0x14u, 0x10u, 'M',   'D',   'R',   'F',   0x00u, 0x70u, 0x71u, 'I',   'N',   'T',   '0',
        0x60u, 0xA4u, 0x83u, 0x60u, 0x14u, 0x14u, 'M',   'L',   'O',   'C',   0x00u, 0x70u, 0x0Au, 0x05u, 0x61u, 0x70u,
        0x71u, 0x61u, 0x60u, 0x70u, 0x0Au, 0x09u, 0x60u, 0xA4u, 0x61u, 0x14u, 0x0Au, 'M',   'S',   'E',   'T',   0x01u,
        0x70u, 0x0Au, 0x07u, 0x68u, 0x14u, 0x12u, 'M',   'A',   'R',   'G',   0x00u, 0x70u, 0x0Au, 0x01u, 0x60u, 'M',
        'S',   'E',   'T',   0x71u, 0x60u, 0xA4u, 0x60u, 0x14u, 0x15u, 'M',   'C',   'O',   'N',   0x00u, 0xA0u, 0x0Bu,
        0x5Bu, 0x12u, 'I',   'N',   'T',   '0',   0x60u, 0xA4u, 0x83u, 0x60u, 0xA4u, 0x0Au, 0x00u, 0x14u, 0x0Du, 'M',
        'R',   'E',   'T',   0x00u, 0x70u, 0x0Au, 0x42u, 0x60u, 0xA4u, 0x71u, 0x60u, 0x14u, 0x1Bu, 'M',   'C',   'H',
        'N',   0x00u, 0x70u, 0x0Au, 0x10u, 0x60u, 0x70u, 0x71u, 0x60u, 0x61u, 0x70u, 0x71u, 0x61u, 0x62u, 0x70u, 0x71u,
        0x62u, 0x63u, 0x75u, 0x63u, 0xA4u, 0x83u, 0x63u, 0x14u, 0x12u, 'M',   'S',   'T',   'R',   0x00u, 0x70u, 0x0Du,
        0x5Cu, 'I',   'N',   'T',   '0',   0x00u, 0x60u, 0xA4u, 0x83u, 0x60u, 0x14u, 0x18u, 'M',   'K',   'I',   'N',
        0x00u, 0x70u, 0x0Du, 0x61u, 0x62u, 0x63u, 0x00u, 0x61u, 0x70u, 0x71u, 0x61u, 0x60u, 0x70u, 0x0Au, 0x41u, 0x60u,
        0xA4u, 0x8Eu, 0x61u, 0x14u, 0x12u, 'M',   'K',   'R',   'F',   0x01u, 0x70u, 0x0Au, 0x99u, 0x60u, 0x70u, 0x71u,
        0x60u, 0x88u, 0x68u, 0x0Au, 0x00u, 0x00u, 0x14u, 0x12u, 'M',   'R',   'D',   'F',   0x01u, 0x70u, 0x0Au, 0x5Au,
        0x60u, 0xA4u, 0x83u, 0x83u, 0x88u, 0x68u, 0x0Au, 0x00u, 0x00u, 0x14u, 0x18u, 'M',   'D',   'N',   'G',   0x00u,
        0x70u, 0x12u, 0x04u, 0x01u, 0x0Au, 0x00u, 0x60u, 'M',   'K',   'R',   'F',   0x60u, 0xA4u, 'M',   'R',   'D',
        'F',   0x60u, 0x14u, 0x13u, 'M',   'T',   'M',   'P',   0x08u, 0x08u, 'T',   'M',   'P',   'N',   0x0Au, 0x11u,
        0xA4u, 0x71u, 'T',   'M',   'P',   'N',   0x14u, 0x18u, 'M',   'K',   'T',   'P',   0x09u, 0x08u, 'T',   'M',
        'P',   'N',   0x0Au, 0x33u, 0x70u, 0x71u, 'T',   'M',   'P',   'N',   0x88u, 0x68u, 0x0Au, 0x00u, 0x00u, 0x14u,
        0x15u, 'M',   'R',   'T',   'P',   0x09u, 0x08u, 'O',   'T',   'H',   'R',   0x0Au, 0x22u, 0xA4u, 0x83u, 0x83u,
        0x88u, 0x68u, 0x0Au, 0x00u, 0x00u, 0x14u, 0x18u, 'M',   'D',   'T',   'P',   0x00u, 0x70u, 0x12u, 0x04u, 0x01u,
        0x0Au, 0x00u, 0x60u, 'M',   'K',   'T',   'P',   0x60u, 0xA4u, 'M',   'R',   'T',   'P',   0x60u, 0x14u, 0x1Au,
        'M',   'D',   'N',   'C',   0x00u, 0x70u, 0x12u, 0x04u, 0x01u, 0x0Au, 0x00u, 0x60u, 'M',   'K',   'R',   'F',
        0x60u, 0xA4u, 0x83u, 0x83u, 0x88u, 0x60u, 0x0Au, 0x00u, 0x00u, 0x14u, '4',   'M',   'C',   'R',   '2',   0x00u,
        0x70u, 0x0Au, 0x00u, 0x61u, 0xA0u, 0x0Bu, 0x5Bu, 0x12u, 0x5Cu, 'M',   'D',   'R',   'F',   0x60u, 0x75u, 0x61u,
        0xA0u, 0x0Bu, 0x5Bu, 0x12u, 0x5Cu, 'I',   'N',   'T',   '0',   0x60u, 0x75u, 0x61u, 0xA0u, 0x0Bu, 0x5Bu, 0x12u,
        0x5Cu, 'R',   '0',   '0',   '0',   0x60u, 0x75u, 0x61u, 0xA4u, 0x72u, 0x61u, 0x83u, 0x60u, 0x00u, 0x14u, 0x2Eu,
        'M',   'C',   'R',   'B',   0x01u, 0xA0u, 0x0Au, 0x5Bu, 0x12u, 0x5Cu, '_',   'O',   'S',   'I',   0x60u, 0xA3u,
        0x70u, 0x68u, 0x60u, 0x8Au, 0x60u, 0x0Au, 0x00u, 'C',   'D',   'W',   '1',   0x7Du, 'C',   'D',   'W',   '1',
        0x0Au, 0x10u, 'C',   'D',   'W',   '1',   0xA4u, 0x83u, 0x88u, 0x60u, 0x0Au, 0x00u, 0x00u, 0x14u, 0x0Eu, 'M',
        'S',   'E',   '2',   0x01u, 0x70u, 0x0Au, 0x33u, 0x60u, 0x70u, 0x71u, 0x60u, 0x68u, 0x14u, 0x16u, 'M',   'D',
        'E',   'D',   0x00u, 0x70u, 0x0Au, 0x00u, 0x61u, 'M',   'S',   'E',   '2',   0x71u, 0x61u, 0x70u, 0x0Au, 0x44u,
        0x61u, 0xA4u, 0x61u, 0x14u, 0x14u, 'M',   'E',   'L',   'D',   0x00u, 0x88u, 'P',   'K',   'G',   'D',   0x0Au,
        0x00u, 0x60u, 0x70u, 0x0Au, 0x05u, 0x60u, 0xA4u, 0x60u, 0x14u, 0x23u, 'M',   'E',   'L',   'R',   0x00u, 0x70u,
        0x0Au, 0x05u, 0x61u, 0x70u, 0x12u, 0x04u, 0x01u, 0x0Au, 0x00u, 0x60u, 0x70u, 0x71u, 0x61u, 0x88u, 0x60u, 0x0Au,
        0x00u, 0x00u, 0x70u, 0x0Au, 0x09u, 0x88u, 0x60u, 0x0Au, 0x00u, 0x00u, 0xA4u, 0x61u, 0x70u, 'M',   'D',   'R',
        'F',   'R',   '0',   '0',   '0',   0x70u, 'M',   'L',   'O',   'C',   'R',   '0',   '0',   '1',   0x70u, 'M',
        'A',   'R',   'G',   'R',   '0',   '0',   '2',   0x70u, 'M',   'C',   'O',   'N',   'R',   '0',   '0',   '3',
        0x70u, 0x5Bu, 0x12u, 0x5Cu, 'U',   'N',   'D',   'F',   0x00u, 'R',   '0',   '0',   '4',   0x70u, 0x5Bu, 0x12u,
        0x5Cu, '_',   'O',   'S',   'I',   0x00u, 'R',   '0',   '0',   '5',   0x70u, 0x5Bu, 0x12u, 0x5Cu, 'N',   'O',
        'P',   'E',   0x00u, 'R',   '0',   '0',   '6',   0x70u, 'M',   'R',   'E',   'T',   'R',   '0',   '0',   '7',
        0x70u, 'M',   'C',   'H',   'N',   'R',   '0',   '0',   '8',   0x70u, 'M',   'S',   'T',   'R',   'R',   '0',
        '0',   '9',   0x70u, 0x0Au, 0x77u, 0x60u, 0x70u, 0x71u, 0x60u, 0x61u, 0x70u, 0x71u, 0x61u, 0x62u, 0x70u, 0x83u,
        0x62u, 'R',   '0',   '1',   '0',   0x70u, 'M',   'K',   'I',   'N',   'R',   '0',   '1',   '1',   0x70u, 'M',
        'D',   'N',   'G',   'R',   '0',   '1',   '2',   0x70u, 'M',   'T',   'M',   'P',   'R',   '0',   '1',   '3',
        0x70u, 'M',   'D',   'T',   'P',   'R',   '0',   '1',   '4',   0x70u, 'M',   'D',   'N',   'C',   'R',   '0',
        '1',   '5',   0x70u, 'M',   'E',   'L',   'R',   'R',   '0',   '1',   '6',   0x70u, 'M',   'C',   'R',   '2',
        'R',   '0',   '1',   '8',   0x70u, 'M',   'C',   'R',   'B',   0x11u, 0x07u, 0x0Au, 0x04u, 0x01u, 0x00u, 0x00u,
        0x00u, 'R',   '0',   '1',   '9',   0x70u, 'M',   'D',   'E',   'D',   'R',   '0',   '2',   '0',   0x70u, 'M',
        'E',   'L',   'D',   'R',   '0',   '2',   '1',   0x9Du, 0x88u, 'P',   'K',   'G',   'L',   0x0Au, 0x00u, 0x00u,
        'A',   'L',   'S',   'L',   0x70u, 0x83u, 0x88u, 'P',   'K',   'G',   'L',   0x0Au, 0x00u, 0x00u, 'R',   '0',
        '1',   '7',
    };

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 0x1234u, "DerefOf (RefOf (INT0)) is 0x1234");
    kernel_test_check(test, references_integer("R001") == 9u, "a store to Local0 = RefOf (Local1) sets Local1");
    kernel_test_check(test, references_integer("R002") == 7u, "Arg0 = 7 sets the caller's Local0 it refers to");
    kernel_test_check(test, references_integer("R003") == 0x1234u, "CondRefOf (INT0, Local0) stores a reference");
    kernel_test_check(test,
                      references_integer("R004") == 0u && references_integer("R005") == REFERENCES_ONES &&
                          references_integer("R006") == 0u,
                      "CondRefOf is False for an External and a missing name, True for \\_OSI");
    kernel_test_check(test, references_integer("R007") == 0x42u, "Return (RefOf (Local0)) gives the caller 0x42");
    kernel_test_check(test, references_integer("R008") == 0x11u,
                      "Increment through three references reaches Local0, and DerefOf reads it");
    kernel_test_check(test, references_integer("R009") == 0x1234u, "DerefOf (\"\\\\INT0\") reads INT0");
    kernel_test_check(test, references_integer("R010") == 0x77u,
                      "a reference to a table's Local0, made by one statement, is read by the next ones");
    kernel_test_check(test, references_integer("R011") == 2u,
                      "a store to a local that refers to a String keeps the String, as a store to it does");
    kernel_test_check(test, references_integer("R012") == 7u,
                      "a reference to the Local0 of a method that returned fails, though another frame took its place");
    kernel_test_check(test, references_integer("R013") == 0x11u,
                      "Return (RefOf (TMPN)) of a name the method created gives the caller 0x11");
    kernel_test_check(
        test, references_integer("R014") == 7u,
        "a reference to a name a method created fails once it returned, though another name took its node");
    kernel_test_check(test, references_integer("R015") == 7u,
                      "a reference to the Local0 of a method that returned fails, though no frame took its place");
    kernel_test_check(test, references_integer("R016") == 9u,
                      "a store to an element that holds a reference writes through it, as Windows does");
    kernel_test_check(test, references_integer("R018") == 0x1237u && references_integer("INT0") == 0x1234u,
                      "CondRefOf to a Local0 that refers to a method, then to INT0, replaces the reference each time");
    kernel_test_check(
        test, references_integer("R019") == 0x11u,
        "a Buffer stored to a Local0 that refers to \\_OSI replaces the reference, as firmware's _OSC needs");
    kernel_test_check(test, references_integer("R020") == 0x44u,
                      "a store to a Local1 whose reference outlived its frame replaces the reference");
    kernel_test_check(test, references_integer("R021") == 5u,
                      "a store to a Local0 that refers to an element naming a Device replaces the reference");
    kernel_test_check(
        test,
        references_integer("R017") == 7u &&
            references_failed(
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_RESOLUTION_LOOP) == 1u,
        "an element naming the name that refers to it fails after 8 names with name_resolution_loop");
    kernel_test_check(
        test,
        references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND) == 3u,
        "the three references to a returned method's local or name fail with name_not_found");
    kernel_test_check(test, references_load.evaluation.failures == 4u, "nothing else fails");
    references_close(test);
}

/**
 * @brief Index refers to the slot of a Package, a Buffer or a String, never to a copy (§5.2): a store
 *        through it changes the named object, Increment works in place, an element that is a name is
 *        resolved when read, an index past the end fails with index_out_of_range, and a store through
 *        an argument that holds an Index overwrites the element. DerefOf (Index (...)) builds no
 *        reference, unless it names a target, as ObjectType's operand does (§7.2, O8).
 */
KERNEL_TEST(index_refers_to_the_element_not_a_copy)
{
    static const uint8_t aml[] = {
        0x08u, 'P',   'K',   'G',   '0',   0x12u, 0x08u, 0x03u, 0x0Au, 0x01u, 0x0Au, 0x02u, 0x0Au, 0x03u, 0x08u, 'B',
        'U',   'F',   '0',   0x11u, 0x06u, 0x0Au, 0x03u, 0x01u, 0x02u, 0x03u, 0x08u, 'S',   'T',   'R',   '0',   0x0Du,
        'A',   'B',   'C',   0x00u, 0x08u, 'N',   'E',   'S',   'T',   0x12u, 0x0Bu, 0x02u, 0x0Au, 0x00u, 0x12u, 0x06u,
        0x02u, 0x0Au, 0x01u, 0x0Au, 0x02u, 0x08u, 'N',   'A',   'M',   'D',   0x12u, 0x06u, 0x01u, 'I',   'N',   'T',
        '1',   0x08u, 'I',   'N',   'T',   '1',   0x0Au, 0x55u, 0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u,
        'R',   '0',   '0',   '1',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',
        '0',   '3',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '4',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '5',
        0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '6',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '7',   0x0Au, 0x07u,
        0x14u, 0x1Cu, 'M',   'I',   'D',   'X',   0x00u, 0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x02u, 0x60u, 0x70u,
        0x0Au, 0x09u, 0x60u, 0xA4u, 0x83u, 0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x02u, 0x00u, 0x14u, 0x19u, 'M',
        'I',   'N',   'C',   0x00u, 0x75u, 0x88u, 'B',   'U',   'F',   '0',   0x0Au, 0x01u, 0x00u, 0xA4u, 0x83u, 0x88u,
        'B',   'U',   'F',   '0',   0x0Au, 0x01u, 0x00u, 0x14u, 0x0Du, 'M',   'P',   'U',   'T',   0x01u, 0x70u, 0x0Du,
        'S',   'T',   'R',   0x00u, 0x68u, 0x14u, 0x1Du, 'M',   'O',   'V',   'R',   0x00u, 'M',   'P',   'U',   'T',
        0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x00u, 0x00u, 0xA4u, 0x8Eu, 0x83u, 0x88u, 'P',   'K',   'G',   '0',
        0x0Au, 0x00u, 0x00u, 0x70u, 0x0Au, 0x05u, 0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x01u, 0x00u, 0x70u, 0x0Bu,
        0xFFu, 0x01u, 0x88u, 'B',   'U',   'F',   '0',   0x0Au, 0x00u, 0x00u, 0x70u, 0x0Au, 0x58u, 0x88u, 'S',   'T',
        'R',   '0',   0x0Au, 0x01u, 0x00u, 0x70u, 0x83u, 0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x01u, 0x00u, 'R',
        '0',   '0',   '0',   0x70u, 0x83u, 0x88u, 'B',   'U',   'F',   '0',   0x0Au, 0x00u, 0x00u, 'R',   '0',   '0',
        '1',   0x70u, 'M',   'I',   'D',   'X',   'R',   '0',   '0',   '2',   0x70u, 'M',   'I',   'N',   'C',   'R',
        '0',   '0',   '3',   0x70u, 0x0Au, 0x77u, 0x88u, 0x83u, 0x88u, 'N',   'E',   'S',   'T',   0x0Au, 0x01u, 0x00u,
        0x0Au, 0x00u, 0x00u, 0x70u, 0x83u, 0x88u, 0x83u, 0x88u, 'N',   'E',   'S',   'T',   0x0Au, 0x01u, 0x00u, 0x0Au,
        0x00u, 0x00u, 'R',   '0',   '0',   '4',   0x70u, 0x83u, 0x88u, 'N',   'A',   'M',   'D',   0x0Au, 0x00u, 0x00u,
        'R',   '0',   '0',   '5',   0x70u, 0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x03u, 0x00u, 'R',   '0',   '0',
        '6',   0x70u, 'M',   'O',   'V',   'R',   'R',   '0',   '0',   '7',
    };

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 5u, "Store (5, Index (PKG0, 1)) changes PKG0");
    kernel_test_check(test, references_integer("R001") == 0xFFu, "Store (0x1FF, Index (BUF0, 0)) stores the low byte");
    kernel_test_check(
        test,
        references_holds("STR0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, "AXC", 3u),
        "Store (0x58, Index (STR0, 1)) makes \"AXC\"");
    kernel_test_check(test, references_integer("R002") == 9u, "a store to a local that holds Index (PKG0, 2) sets it");
    kernel_test_check(test, references_integer("R003") == 3u, "Increment (Index (BUF0, 1)) takes 2 to 3");
    kernel_test_check(test, references_integer("R004") == 0x77u, "a store through a nested Index changes NEST");
    kernel_test_check(test, references_integer("R005") == 0x55u, "an element naming INT1 reads INT1");
    kernel_test_check(
        test,
        references_integer("R006") == 7u &&
            references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE) ==
                1u,
        "Index (PKG0, 3) fails with index_out_of_range");
    kernel_test_check(test, references_integer("R007") == 2u, "Arg0 = \"STR\" through Index (PKG0, 0) makes a String");
    kernel_test_check(test, references_load.evaluation.references_built == 8u,
                      "eight Index results build references: DerefOf reads the others at once");
    references_close(test);
}

/**
 * @brief Match gives the first element from the start that both tests accept, each element converted
 *        to the match object's type, and Ones when none does.
 */
KERNEL_TEST(match_gives_the_first_element_both_tests_accept)
{
    static const uint8_t aml[] = {
        0x08u, 'P',   'K',   'G',   '0',   0x12u, 0x12u, 0x06u, 0x0Au, 0x01u, 0x0Au, 0x05u, 0x0Du, 0x58u, 0x00u, 0x0Au,
        0x09u, 0x0Au, 0x05u, 0x11u, 0x04u, 0x0Au, 0x01u, 0x07u, 0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x00u, 0x08u,
        'R',   '0',   '0',   '1',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '2',   0x0Au, 0x00u, 0x08u, 'R',   '0',
        '0',   '3',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '4',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '5',
        0x0Au, 0x00u, 0x70u, 0x89u, 'P',   'K',   'G',   '0',   0x01u, 0x0Au, 0x05u, 0x00u, 0x0Au, 0x00u, 0x0Au, 0x00u,
        'R',   '0',   '0',   '0',   0x70u, 0x89u, 'P',   'K',   'G',   '0',   0x01u, 0x0Au, 0x05u, 0x00u, 0x0Au, 0x00u,
        0x0Au, 0x02u, 'R',   '0',   '0',   '1',   0x70u, 0x89u, 'P',   'K',   'G',   '0',   0x05u, 0x0Au, 0x04u, 0x03u,
        0x0Au, 0x09u, 0x0Au, 0x00u, 'R',   '0',   '0',   '2',   0x70u, 0x89u, 'P',   'K',   'G',   '0',   0x04u, 0x0Au,
        0x09u, 0x00u, 0x0Au, 0x00u, 0x0Au, 0x00u, 'R',   '0',   '0',   '3',   0x70u, 0x89u, 'P',   'K',   'G',   '0',
        0x01u, 0x0Au, 0x08u, 0x00u, 0x0Au, 0x00u, 0x0Au, 0x00u, 'R',   '0',   '0',   '4',   0x70u, 0x89u, 'P',   'K',
        'G',   '0',   0x01u, 0x0Au, 0x07u, 0x00u, 0x0Au, 0x00u, 0x0Au, 0x00u, 'R',   '0',   '0',   '5',
    };

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 1u, "the first 5 is at 1");
    kernel_test_check(test, references_integer("R001") == 4u, "from 2, the next 5 is at 4");
    kernel_test_check(test, references_integer("R002") == 1u, "the first element above 4 and below 9 is at 1");
    kernel_test_check(test, references_integer("R003") == 3u, "the first element of 9 or more is at 3");
    kernel_test_check(test, references_integer("R004") == REFERENCES_ONES, "no element equals 8");
    kernel_test_check(test, references_integer("R005") == 5u, "Buffer { 7 } converts to 7, at 5");
    kernel_test_check(test, references_load.evaluation.failures == 0u, "nothing fails");
    references_close(test);
}

/**
 * @brief Buffer fields read and write their bits (§5.2): the fixed-width ones read as Integers,
 *        CreateField as a Buffer; a write changes only the field's bits; a field over an argument
 *        writes the argument's Buffer, as _OSC does; CreateField past the Buffer fails with
 *        index_out_of_range (§6.3), as does a read of a field past its Buffer; a field of length 0
 *        reads as an empty Buffer and writes nothing; a field that takes its own Buffer takes it as it
 *        was before the store.
 */
KERNEL_TEST(buffer_fields_read_and_write_their_bits)
{
    static const uint8_t aml[] = {
        0x08u, 'B',   'U',   'F',   '0',   0x11u, 0x0Bu, 0x0Au, 0x08u, 0x01u, 0x23u, 0x45u, 0x67u, 0x89u, 0xABu, 0xCDu,
        0xEFu, 0x8Cu, 'B',   'U',   'F',   '0',   0x0Au, 0x01u, 'B',   'Y',   'T',   '0',   0x8Bu, 'B',   'U',   'F',
        '0',   0x0Au, 0x02u, 'W',   'R',   'D',   '0',   0x8Au, 'B',   'U',   'F',   '0',   0x0Au, 0x04u, 'D',   'W',
        'D',   '0',   0x8Fu, 'B',   'U',   'F',   '0',   0x0Au, 0x00u, 'Q',   'W',   'D',   '0',   0x8Du, 'B',   'U',
        'F',   '0',   0x0Au, 0x09u, 'B',   'I',   'T',   '0',   0x5Bu, 0x13u, 'B',   'U',   'F',   '0',   0x0Au, 0x04u,
        0x0Au, 0x0Cu, 'F',   'L',   'D',   '0',   0x08u, 'B',   'U',   'F',   '1',   0x11u, 0x03u, 0x0Au, 0x04u, 0x8Au,
        'B',   'U',   'F',   '1',   0x0Au, 0x02u, 'P',   'A',   'S',   'T',   0x08u, 'B',   'U',   'F',   '2',   0x11u,
        0x05u, 0x0Au, 0x02u, 0xFFu, 0x00u, 0x5Bu, 0x13u, 'B',   'U',   'F',   '2',   0x0Au, 0x04u, 0x0Au, 0x0Cu, 'F',
        'L',   'D',   '2',   0x08u, 'B',   'U',   'F',   '3',   0x11u, 0x07u, 0x0Au, 0x04u, 0x11u, 0x22u, '3',   'D',
        0x5Bu, 0x13u, 'B',   'U',   'F',   '3',   0x0Au, 0x0Cu, 0x0Au, 0x0Cu, 'F',   'L',   'D',   '3',   0x08u, 'R',
        '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '1',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',
        '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '3',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '4',   0x0Au,
        0x07u, 0x08u, 'R',   '0',   '0',   '5',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '6',   0x0Au, 0x00u, 0x08u,
        'R',   '0',   '0',   '7',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '8',   0x0Au, 0x07u, 0x08u, 'R',   '0',
        '0',   '9',   0x0Au, 0x00u, 0x08u, 'R',   '0',   '1',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '1',   '1',
        0x0Au, 0x00u, 0x08u, 'R',   '0',   '1',   '2',   0x0Au, 0x00u, 0x14u, 0x23u, 'M',   'O',   'S',   'C',   0x01u,
        0x8Au, 0x68u, 0x0Au, 0x00u, 'C',   'D',   'W',   '1',   0x8Au, 0x68u, 0x0Au, 0x04u, 'C',   'D',   'W',   '2',
        0x7Du, 'C',   'D',   'W',   '1',   0x0Au, 0x10u, 'C',   'D',   'W',   '2',   0xA4u, 0x68u, 0x14u, 0x1Au, 'M',
        'B',   'I',   'G',   0x00u, 0x5Bu, 0x13u, 'B',   'U',   'F',   '1',   0x0Cu, 0xF0u, 0xFFu, 0xFFu, 0xFFu, 0x0Au,
        0x20u, 'F',   'B',   'I',   'G',   0xA4u, 0x0Au, 0x00u, 0x14u, 0x20u, 'M',   'E',   'M',   'P',   0x00u, 0x5Bu,
        0x13u, 'B',   'U',   'F',   '1',   0x0Au, 0x08u, 0x0Au, 0x00u, 'F',   'E',   'M',   'P',   0x70u, 0x0Au, 0xFFu,
        'F',   'E',   'M',   'P',   0xA4u, 'F',   'E',   'M',   'P',   0x70u, 'B',   'Y',   'T',   '0',   'R',   '0',
        '0',   '0',   0x70u, 'W',   'R',   'D',   '0',   'R',   '0',   '0',   '1',   0x70u, 'D',   'W',   'D',   '0',
        'R',   '0',   '0',   '2',   0x70u, 'Q',   'W',   'D',   '0',   'R',   '0',   '0',   '3',   0x70u, 'B',   'I',
        'T',   '0',   'R',   '0',   '0',   '4',   0x9Du, 'F',   'L',   'D',   '0',   'R',   '0',   '0',   '5',   0x70u,
        0x0Au, 0xAAu, 'B',   'Y',   'T',   '0',   0x70u, 0x00u, 'B',   'I',   'T',   '0',   0x70u, 0x0Bu, 'Z',   'Z',
        'W',   'R',   'D',   '0',   0x9Du, 'M',   'O',   'S',   'C',   0x11u, 0x0Bu, 0x0Au, 0x08u, 0x01u, 0x02u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 'R',   '0',   '0',   '6',   0x9Du, 'B',   'U',   'F',   '0',   'R',   '0',
        '0',   '7',   0x70u, 'M',   'B',   'I',   'G',   'R',   '0',   '0',   '8',   0x9Du, 'M',   'E',   'M',   'P',
        'R',   '0',   '0',   '9',   0x70u, 'P',   'A',   'S',   'T',   'R',   '0',   '1',   '0',   0x70u, 'B',   'U',
        'F',   '2',   'F',   'L',   'D',   '2',   0x9Du, 'B',   'U',   'F',   '2',   'R',   '0',   '1',   '1',   0x70u,
        'B',   'U',   'F',   '3',   'F',   'L',   'D',   '3',   0x9Du, 'B',   'U',   'F',   '3',   'R',   '0',   '1',
        '2',
    };
    static const uint8_t created[] = {0x30u, 0x02u};
    static const uint8_t answered[] = {0x01u, 0x02u, 0x00u, 0x00u, 0x11u, 0x02u, 0x00u, 0x00u};
    static const uint8_t written[] = {0x01u, 0xA8u, 0x5Au, 0x5Au, 0x89u, 0xABu, 0xCDu, 0xEFu};
    static const uint8_t zeros[] = {0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t overlapped[] = {0xFFu, 0x0Fu};
    static const uint8_t ahead[] = {0x11u, 0x32u, 0x43u, 0x44u};

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 0x23u, "CreateByteField reads byte 1");
    kernel_test_check(test, references_integer("R001") == 0x6745u, "CreateWordField reads bytes 2 and 3");
    kernel_test_check(test, references_integer("R002") == 0xEFCDAB89u, "CreateDWordField reads bytes 4 to 7");
    kernel_test_check(test, references_integer("R003") == 0xEFCDAB8967452301ull, "CreateQWordField reads all 8");
    kernel_test_check(test, references_integer("R004") == 1u, "CreateBitField reads bit 9");
    kernel_test_check(test,
                      references_holds("R005", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       created, sizeof(created)),
                      "CreateField (4, 12) reads Buffer { 0x30, 0x02 }");
    kernel_test_check(test,
                      references_holds("R007", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       written, sizeof(written)),
                      "the byte, bit and word writes change their bits only");
    kernel_test_check(test,
                      references_holds("R006", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       answered, sizeof(answered)),
                      "CDW2 over Arg0 takes CDW1 | 0x10, and the method returns the Buffer it wrote");
    kernel_test_check(test, references_integer("R008") == 7u, "CreateField (BUF1, 0xFFFFFFF0, 0x20, FBIG) fails");
    kernel_test_check(
        test,
        references_integer("R010") == 7u &&
            references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE) ==
                2u,
        "PAST, 32 bits from byte 2 of a 4-byte Buffer, fails when read: both with index_out_of_range");
    kernel_test_check(
        test,
        references_holds("R009", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, zeros, 0u) &&
            references_holds("BUF1", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, zeros,
                             sizeof(zeros)),
        "a field of length 0 reads as an empty Buffer and writes nothing");
    kernel_test_check(test,
                      references_holds("R011", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       overlapped, sizeof(overlapped)),
                      "FLD2, bits 4 to 15 of BUF2, takes BUF2's first 12 bits as they were before the store");
    kernel_test_check(
        test,
        references_holds("R012", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, ahead,
                         sizeof(ahead)),
        "FLD3, bits 12 to 23 of BUF3, takes the low 12 bits of BUF3's last two bytes, which lie ahead of it");
    references_close(test);
}

/**
 * @brief A store that would make an object reach itself fails with reference_cycle (§3.5): a package
 *        that would refer to its own name, directly or through another package, a temporary package
 *        that would refer to its local, a local that would refer to itself; references to the same
 *        package from two elements are no cycle.
 */
KERNEL_TEST(a_store_that_would_close_a_cycle_is_refused)
{
    static const uint8_t aml[] = {
        0x08u, 'P',   'K',   'G',   '0',   0x12u, 0x06u, 0x02u, 0x0Au, 0x00u, 0x0Au, 0x00u, 0x08u, 'P',   'K',   'G',
        '1',   0x12u, 0x06u, 0x02u, 0x0Au, 0x00u, 0x0Au, 0x00u, 0x08u, 'P',   'K',   'G',   '2',   0x12u, 0x06u, 0x02u,
        0x0Au, 0x00u, 0x0Au, 0x00u, 0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '1',
        0x0Au, 0x07u, 0x14u, 0x18u, 'M',   'L',   'O',   'C',   0x00u, 0x70u, 0x12u, 0x04u, 0x01u, 0x0Au, 0x00u, 0x60u,
        0x70u, 0x71u, 0x60u, 0x88u, 0x60u, 0x0Au, 0x00u, 0x00u, 0xA4u, 0x0Au, 0x01u, 0x14u, 0x11u, 'M',   'S',   'L',
        'F',   0x00u, 0x70u, 0x0Au, 0x01u, 0x60u, 0x70u, 0x71u, 0x60u, 0x60u, 0xA4u, 0x0Au, 0x01u, 0x70u, 0x71u, 'P',
        'K',   'G',   '0',   0x88u, 'P',   'K',   'G',   '0',   0x0Au, 0x00u, 0x00u, 0x70u, 0x71u, 'P',   'K',   'G',
        '0',   0x88u, 'P',   'K',   'G',   '1',   0x0Au, 0x00u, 0x00u, 0x70u, 0x71u, 'P',   'K',   'G',   '1',   0x88u,
        'P',   'K',   'G',   '0',   0x0Au, 0x01u, 0x00u, 0x70u, 0x71u, 'P',   'K',   'G',   '1',   0x88u, 'P',   'K',
        'G',   '2',   0x0Au, 0x00u, 0x00u, 0x70u, 0x71u, 'P',   'K',   'G',   '1',   0x88u, 'P',   'K',   'G',   '2',
        0x0Au, 0x01u, 0x00u, 0x70u, 'M',   'L',   'O',   'C',   'R',   '0',   '0',   '0',   0x70u, 'M',   'S',   'L',
        'F',   'R',   '0',   '0',   '1',
    };

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(
        test,
        references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_REFERENCE_CYCLE) == 4u,
        "the four cyclic stores fail with reference_cycle");
    kernel_test_check(test,
                      references_element("PKG0", 0u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          references_element("PKG0", 1u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
                      "PKG0 keeps its two Integers");
    kernel_test_check(test,
                      references_element("PKG1", 0u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE &&
                          references_element("PKG2", 0u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE &&
                          references_element("PKG2", 1u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE,
                      "PKG1 refers to PKG0, and PKG2 twice to PKG1");
    kernel_test_check(test, references_integer("R000") == 7u && references_integer("R001") == 7u,
                      "the two methods that close a cycle store nothing");
    references_close(test);
}

/**
 * @brief A Name that holds a reference is an alias of what it refers to (§5.2): it reads as its
 *        target, a store to it reaches the target, ObjectType gives the target's type; a store through
 *        a reference to an argument writes into the object the caller shares with it, its size kept.
 */
KERNEL_TEST(a_name_that_holds_a_reference_is_an_alias)
{
    static const uint8_t aml[] = {
        0x08u, 'T',   'G',   'T',   '0',   0x0Bu, 0x34u, 0x12u, 0x08u, 'A',   'L',   'S',   '0',   0x0Au, 0x00u, 0x08u,
        'S',   'T',   'R',   '0',   0x0Du, 'A',   'B',   'C',   'D',   'E',   'F',   'G',   'H',   0x00u, 0x08u, 'R',
        '0',   '0',   '0',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '1',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',
        '2',   0x0Au, 0x07u, 0x08u, 'R',   '0',   '0',   '3',   0x0Au, 0x07u, 0x14u, 0x09u, 'M',   'S',   'E',   'T',
        0x02u, 0x70u, 0x68u, 0x69u, 0x14u, 0x11u, 'M',   'P',   'U',   'T',   0x01u, 0x70u, 0x71u, 0x68u, 0x60u, 0x70u,
        0x0Cu, 'A',   'B',   'C',   'D',   0x60u, 'M',   'S',   'E',   'T',   0x71u, 'T',   'G',   'T',   '0',   0x71u,
        'A',   'L',   'S',   '0',   0x70u, 'A',   'L',   'S',   '0',   'R',   '0',   '0',   '0',   0x70u, 0x0Au, 0x05u,
        'A',   'L',   'S',   '0',   0x70u, 'T',   'G',   'T',   '0',   'R',   '0',   '0',   '1',   0x70u, 0x8Eu, 'A',
        'L',   'S',   '0',   'R',   '0',   '0',   '2',   'M',   'P',   'U',   'T',   'S',   'T',   'R',   '0',   0x70u,
        0x87u, 'S',   'T',   'R',   '0',   'R',   '0',   '0',   '3',
    };
    static const uint8_t shared[] = {0x41u, 0x42u, 0x43u, 0x44u, 0x00u, 0x00u, 0x00u, 0x00u};

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 0x1234u, "ALS0 reads as TGT0");
    kernel_test_check(test, references_integer("R001") == 5u, "Store (5, ALS0) sets TGT0");
    kernel_test_check(test, references_integer("R002") == 1u, "ObjectType (ALS0) is TGT0's");
    kernel_test_check(test,
                      references_holds("STR0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                       shared, sizeof(shared)) &&
                          references_integer("R003") == 8u,
                      "STR0 takes 0x44434241's bytes through Arg0 and keeps its 8 characters");
    kernel_test_check(test, references_load.evaluation.failures == 0u, "nothing fails");
    references_close(test);
}

/**
 * @brief CopyObject to a Name that refers to a package element replaces the reference, as Windows
 *        does, where a Name that refers to a named object has its target replaced: the element keeps
 *        its value, and the Name takes the copied object.
 */
KERNEL_TEST(copy_object_replaces_a_name_that_refers_to_a_package_element)
{
    static const uint8_t aml[] = {
        0x08u, 'P',   'K',   'G',   '0',   0x12u, 0x07u, 0x03u, 0x01u, 0x0Au, 0x02u, 0x0Au, 0x03u, 0x08u, 'A',
        'L',   'S',   '2',   0x00u, 0x08u, 'R',   '0',   '0',   '0',   0x0Au, 0x07u, 0x14u, 0x09u, 'W',   'R',
        'T',   'E',   0x02u, 0x70u, 0x68u, 0x69u, 'W',   'R',   'T',   'E',   0x88u, 'P',   'K',   'G',   '0',
        0x01u, 0x00u, 0x71u, 'A',   'L',   'S',   '2',   0x9Du, 0x0Du, 'a',   'b',   0x00u, 'A',   'L',   'S',
        '2',   0x70u, 0x83u, 0x88u, 'P',   'K',   'G',   '0',   0x01u, 0x00u, 'R',   '0',   '0',   '0',
    };

    if (!references_run(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 2u, "PKG0[1] keeps 2");
    kernel_test_check(
        test,
        references_holds("ALS2", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, "ab", 2u),
        "ALS2 holds the String \"ab\"");
    kernel_test_check(test, references_load.evaluation.failures == 0u, "nothing fails");
    references_close(test);
}

/**
 * @brief The bits a field of @p length bits at bit @p offset reads from a 16-byte Buffer, computed
 *        with two 64-bit words rather than bit by bit.
 */
static uint64_t references_model_read(const uint8_t *bytes, uint32_t offset, uint32_t length)
{
    uint64_t low = 0u;
    uint64_t high = 0u;

    for (uint32_t byte = 0u; byte < 8u; ++byte)
    {
        low |= (uint64_t) bytes[byte] << (8u * byte);
        high |= (uint64_t) bytes[8u + byte] << (8u * byte);
    }

    const uint64_t window = (offset == 0u) ? low : ((low >> offset) | (high << (64u - offset)));

    return (length == 64u) ? window : (window & ((1ull << length) - 1u));
}

/**
 * @brief Writes into @p bytes, a copy of the 16-byte Buffer, what a field of @p length bits at bit
 *        @p offset leaves after taking @p value, computed with two 64-bit words.
 */
static void references_model_write(uint8_t *bytes, uint32_t offset, uint32_t length, uint64_t value)
{
    const uint64_t mask = (length == 64u) ? ~0ull : ((1ull << length) - 1u);
    uint64_t low = 0u;
    uint64_t high = 0u;

    for (uint32_t byte = 0u; byte < 8u; ++byte)
    {
        low |= (uint64_t) bytes[byte] << (8u * byte);
        high |= (uint64_t) bytes[8u + byte] << (8u * byte);
    }
    value &= mask;
    low = (low & ~(mask << offset)) | (value << offset);
    if (offset != 0u)
        high = (high & ~(mask >> (64u - offset))) | (value >> (64u - offset));
    for (uint32_t byte = 0u; byte < 8u; ++byte)
    {
        bytes[byte] = (uint8_t) (low >> (8u * byte));
        bytes[8u + byte] = (uint8_t) (high >> (8u * byte));
    }
}

/**
 * @brief Buffer fields read and write exactly at every bit offset from 0 to 63 and every length from
 *        1 to 64 (§6.3, §8.2): each read equals the bits two 64-bit words give, each write leaves the
 *        Buffer as they do. The reads are folded, so that the host and ring 0 compare them.
 */
KERNEL_TEST(buffer_fields_read_and_write_every_bit_offset_and_length)
{
    static const uint8_t aml[] = {0x08u, 'R', 'E', 'S', '0', 0x00u};
    uint8_t pattern[REFERENCES_FIELD_BYTES];
    uint8_t expected[REFERENCES_FIELD_BYTES];
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t buffer;
    uint32_t wrong_reads = 0u;
    uint32_t wrong_writes = 0u;
    uint32_t fold = REFERENCES_FOLD_OFFSET;

    if (!references_run(test, aml, sizeof(aml)))
        return;
    for (uint32_t byte = 0u; byte < REFERENCES_FIELD_BYTES; ++byte)
        pattern[byte] = (uint8_t) (0x5Bu + 0x1Du * byte);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_value_allocate(
                               &references_load.heap, &references_load.space,
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                               REFERENCES_FIELD_BYTES,
                               &buffer) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
                           "the Buffer is allocated"))
        return;

    uint8_t *bytes = advanced_configuration_and_power_interface_machine_language_object_payload(&references_load.heap,
                                                                                                buffer.handle);

    for (uint32_t offset = 0u; offset < 64u; ++offset)
    {
        for (uint32_t length = 1u; length <= 64u; ++length)
        {
            const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t field = {
                .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD,
                .auxiliary = 1u,
                .handle = buffer.handle,
                .payload = offset | ((uint64_t) length << 32u),
            };
            const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t source = {
                .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
                .payload = 0xA5C3F00F96E1B42Dull ^ ((uint64_t) offset << 40u) ^ length,
            };
            AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t read;

            memcpy(bytes, pattern, sizeof(pattern));
            advanced_configuration_and_power_interface_machine_language_buffer_field_read(
                &references_load.heap, &references_load.space, &field, 64u, &read);
            wrong_reads += (read.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER ||
                            read.payload != references_model_read(pattern, offset, length)) ?
                               1u :
                               0u;
            for (uint32_t byte = 0u; byte < 8u; ++byte)
                fold = (fold ^ (uint8_t) (read.payload >> (8u * byte))) * REFERENCES_FOLD_PRIME;
            memcpy(expected, pattern, sizeof(pattern));
            references_model_write(expected, offset, length, source.payload);
            advanced_configuration_and_power_interface_machine_language_buffer_field_write(&references_load.heap,
                                                                                           &field, &source, 64u);
            wrong_writes += (memcmp(bytes, expected, sizeof(expected)) != 0) ? 1u : 0u;
        }
    }
    kernel_test_measure(test, "fold", fold);
    kernel_test_check(test, wrong_reads == 0u, "the 4,096 reads equal the model's");
    kernel_test_check(test, wrong_writes == 0u, "the 4,096 writes leave the Buffer as the model does");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &buffer);
    references_close(test);
}

/**
 * @brief An evaluation by path runs a method with the arguments it is given and returns its result,
 *        reads a named object's value, and refuses a path that names nothing, a malformed path and the
 *        wrong number of arguments.
 */
KERNEL_TEST(an_evaluation_by_path_runs_its_method_or_reads_its_object)
{
    static const uint8_t aml[] = {
        0x08u, 'S', 'T',   'R',   '0', 0x0Du, 'X', 'Y', 0x00u, 0x10u, 0x12u, 0x5Cu, '_',   'S',
        'B',   '_', 0x14u, 0x0Bu, 'M', 'C',   'A', 'T', 0x02u, 0xA4u, 0x73u, 0x68u, 0x69u, 0x00u,
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t arguments[2];
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status;

    if (!references_run(test, aml, sizeof(aml)))
        return;
    advanced_configuration_and_power_interface_machine_language_value_to_string(
        &references_load.heap, &references_load.space,
        &(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){
            .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
            .payload = 0xABu},
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_HEXADECIMAL, 64u, &arguments[0]);
    arguments[1] = (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){
        .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
        .payload = 0x2Au};
    status = advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\_SB_.MCAT",
                                                                                  arguments, 2u, &result);
    kernel_test_check(
        test,
        status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            references_value_holds(&result, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                   "0xAB000000000000002A", 20u),
        "\\_SB_.MCAT (\"0xAB\", 0x2A) returns their concatenation");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &result);
    status = advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\STR0", NULL, 0u,
                                                                                  &result);
    kernel_test_check(
        test,
        status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            references_value_holds(&result, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                   "XY", 2u),
        "\\STR0 reads \"XY\"");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &result);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\NOPE",
                                                                                           NULL, 0u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND,
                      "\\NOPE names nothing");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &references_load, "_SB_.MCAT", NULL, 0u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a path that does not start at the root is malformed");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\STR0.",
                                                                                           NULL, 0u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a path that ends with a dot is malformed");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &references_load, "\\_SB_.MCAT", arguments, 1u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH,
                      "MCAT takes two arguments, not one");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &arguments[0]);
    references_close(test);
}
