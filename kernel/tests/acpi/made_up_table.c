#include "made_up_table.h"

#include <string.h>

const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
made_up_definition_block(uint8_t *table, const char *signature, uint8_t revision, const uint8_t *aml, uint32_t length)
{
    const uint32_t total = MADE_UP_TABLE_HEADER + length;
    uint8_t sum = 0u;

    memset(table, 0, MADE_UP_TABLE_HEADER);
    memcpy(table, signature, 4u);
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        table[4u + byte] = (uint8_t) (total >> (8u * byte));
    table[8] = revision;
    memcpy(table + 10, "LPLTST", 6u);
    memcpy(table + 16, "MADEUPML", 8u);
    memcpy(table + MADE_UP_TABLE_HEADER, aml, length);
    for (uint32_t byte = 0u; byte < total; ++byte)
        sum = (uint8_t) (sum + table[byte]);
    table[9] = (uint8_t) (0u - sum);
    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) table;
}

bool made_up_load_table(KernelTest_t *test, AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                        AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *host, uint8_t *table, uint8_t revision,
                        const uint8_t *aml, uint32_t length)
{
    advanced_configuration_and_power_interface_simulated_host_open(host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               load, &host->host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
    {
        advanced_configuration_and_power_interface_machine_language_close(load);
        return false;
    }
    advanced_configuration_and_power_interface_machine_language_declare_table(
        load, made_up_definition_block(table, "DSDT", revision, aml, length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(load);
    return true;
}

const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
made_up_root_node(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(
        &load->space,
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u)));
}
