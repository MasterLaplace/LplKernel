#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool advanced_configuration_and_power_interface_machine_language_namespace_is_device_object(uint8_t type)
{
    return type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE ||
           type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PROCESSOR ||
           type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_THERMAL_ZONE;
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
advanced_configuration_and_power_interface_machine_language_namespace_node(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index)
{
    if (index == 0u || index >= space->node_count)
        return NULL;
    return &space->runs[index / ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN]
                       [index % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN];
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_find_or_add_child(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name,
    uint8_t type, uint16_t owner, uint32_t *out_index, bool *out_created)
{
    const uint32_t existing =
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(space, parent, name);

    *out_created = existing == 0u;
    if (existing == 0u)
        return advanced_configuration_and_power_interface_machine_language_namespace_add_child(space, parent, name,
                                                                                               type, owner, out_index);
    *out_index = existing;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}
