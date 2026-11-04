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
