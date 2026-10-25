#include "needs_path_closure.h"

#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(host_needs_path_closure, KERNEL_TEST_STAGE_INITIALIZATION);

#define CLOSURE_TABLE_CAPACITY 256u           /**< Bytes of the buffer the made-up DSDT is written in. */
#define CLOSURE_MAX_NODES      256u           /**< The node cap of the made-up load. */
#define CLOSURE_MAX_ARENA      (256u * 1024u) /**< The arena cap of the made-up load. */

static uint8_t closure_table[CLOSURE_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t closure_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t closure_load;
static NeedsPathClosure_t closure;

/**
 * @brief Loads @p aml as the only definition block, with the closure observing, and walks its needs path.
 */
static bool closure_walk(KernelTest_t *test, const uint8_t *aml, uint32_t length)
{
    advanced_configuration_and_power_interface_simulated_host_open(&closure_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &closure_load, &closure_host.host, CLOSURE_MAX_NODES, CLOSURE_MAX_ARENA),
                           "the namespace opens"))
        return false;
    if (!kernel_test_check(test, needs_path_closure_attach(&closure, &closure_load), "the closure is allocated"))
    {
        advanced_configuration_and_power_interface_machine_language_close(&closure_load);
        return false;
    }
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &closure_load, made_up_definition_block(closure_table, "DSDT", aml, length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&closure_load);
    needs_path_closure_walk(&closure, &closure_load);
    return kernel_test_check(test, closure_load.decode_errors == 0u && closure.dropped == 0u,
                             "the table decodes and the closure keeps every call");
}

/**
 * @brief Whether the walk reached the method at the root named @p name.
 */
static bool closure_reached(const char *name)
{
    const uint32_t node = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &closure_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u));

    return node != 0u && closure.reached[node];
}

/**
 * @brief Releases the closure and closes the load.
 */
static void closure_close(void)
{
    needs_path_closure_release(&closure);
    advanced_configuration_and_power_interface_machine_language_close(&closure_load);
}

KERNEL_TEST(a_call_from_an_entry_is_followed_to_the_end)
{
    static const uint8_t aml[] = {
        0x14u, 0x0Au, '_',   'I',   'N',   'I', 0x00u, 'M',   'T',   'H',   'A',   0x14u, 0x0Au, 'M',   'T',
        'H',   'A',   0x00u, 'M',   'T',   'H', 'B',   0x14u, 0x0Au, 'M',   'T',   'H',   'B',   0x00u, 0x5Bu,
        0x21u, 0x0Au, 0x01u, 0x14u, 0x0Au, 'M', 'T',   'H',   'C',   0x00u, 0x5Bu, 0x22u, 0x0Au, 0x01u,
    };

    if (!closure_walk(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, closure.entries == 1u && closure.methods == 3u, "_INI is the one entry, 3 methods reached");
    kernel_test_check(test, closure_reached("_INI") && closure_reached("MTHA") && closure_reached("MTHB"),
                      "_INI calls MTHA, which calls MTHB");
    kernel_test_check(test, !closure_reached("MTHC"), "MTHC, which nothing calls, stays out");
    kernel_test_check(test,
                      needs_path_closure_reaches_opcode(
                          &closure, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STALL),
                      "the Stall in MTHB is reached");
    kernel_test_check(test,
                      !needs_path_closure_reaches_opcode(
                          &closure, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_SLEEP),
                      "the Sleep in MTHC is not");
    kernel_test_check(test,
                      needs_path_closure_reaches_opcode(
                          &closure, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_METHOD),
                      "the table-level code, which declares the methods, is reached");
    closure_close();
}

KERNEL_TEST(only_a_processor_s_pdc_is_an_entry)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x83u, 0x16u, 'C', 'P',   'U',   '0',   0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x14u, 0x0Au,
        '_',   'P',   'D',   'C', 0x01u, 0x5Bu, 0x21u, 0x0Au, 0x01u, 0x5Bu, 0x82u, 0x1Cu, 'C',   'P',   'U',
        '1',   0x08u, '_',   'H', 'I',   'D',   0x0Du, 'A',   'C',   'P',   'I',   '0',   '0',   '0',   '7',
        0x00u, 0x14u, 0x07u, '_', 'P',   'D',   'C',   0x01u, 0xA3u, 0x5Bu, 0x82u, 0x10u, 'D',   'E',   'V',
        '0',   0x14u, 0x0Au, '_', 'P',   'D',   'C',   0x01u, 0x5Bu, 0x22u, 0x0Au, 0x01u,
    };

    if (!closure_walk(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, closure.entries == 2u, "the _PDC of the Processor and of the ACPI0007 device");
    kernel_test_check(test,
                      needs_path_closure_reaches_opcode(
                          &closure, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STALL),
                      "the Processor's _PDC is reached");
    kernel_test_check(test,
                      needs_path_closure_reaches_opcode(
                          &closure, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP),
                      "the _PDC of the device whose _HID is ACPI0007 is reached");
    kernel_test_check(test,
                      !needs_path_closure_reaches_opcode(
                          &closure, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_SLEEP),
                      "the _PDC of DEV0, which is not a processor, is not");
    closure_close();
}
