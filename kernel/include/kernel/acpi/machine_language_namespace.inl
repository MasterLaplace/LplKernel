#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool advanced_configuration_and_power_interface_machine_language_namespace_is_device_object(uint8_t type)
{
    return type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE ||
           type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PROCESSOR ||
           type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_THERMAL_ZONE;
}

static inline uint32_t advanced_configuration_and_power_interface_machine_language_namespace_hash(uint32_t parent,
                                                                                                  uint32_t name)
{
    uint32_t key = name ^ (parent * 0x9E3779B1u);

    key ^= key >> 16u;
    key *= 0x7FEB352Du;
    key ^= key >> 15u;
    key *= 0x846CA68Bu;
    key ^= key >> 16u;
    return key;
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
advanced_configuration_and_power_interface_machine_language_namespace_node_at(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index)
{
    return &space->runs[index / ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN]
                       [index % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN];
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
advanced_configuration_and_power_interface_machine_language_namespace_node(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index)
{
    if (index == 0u || index >= space->node_count)
        return NULL;
    return advanced_configuration_and_power_interface_machine_language_namespace_node_at(space, index);
}

__attribute__((always_inline)) static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_find_or_add_child(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name,
    uint8_t type, uint16_t owner, uint32_t *out_index, bool *out_created)
{
    const uint32_t mask = space->slot_capacity - 1u;
    const uint32_t index = space->node_count;
    uint32_t slot = advanced_configuration_and_power_interface_machine_language_namespace_hash(parent, name) & mask;

    ++space->lookups;
    for (;;)
    {
        const uint32_t found = space->slots[slot];

        ++space->probes;
        if (found == 0u)
            break;

        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node_at(space, found);

        if (node->parent == parent && node->name == name)
        {
            *out_created = false;
            *out_index = found;
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
        }
        slot = (slot + 1u) & mask;
    }
    *out_created = true;
    if (space->free_nodes != 0u ||
        index - ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT >= space->max_nodes ||
        (index + 1u) * 2u > space->slot_capacity ||
        index >= space->run_count * ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN)
        return advanced_configuration_and_power_interface_machine_language_namespace_add_child(space, parent, name,
                                                                                               type, owner, out_index);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node_at(space, index);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *parent_node =
        advanced_configuration_and_power_interface_machine_language_namespace_node_at(space, parent);
    const uint32_t first = parent_node->first_child;

    ++space->node_count;
    ++space->live_nodes;
    ++space->generation;
    node->name = name;
    node->parent = parent;
    node->first_child = 0u;
    node->next_sibling = 0u;
    node->type = type;
    node->flags = 0u;
    node->owner = owner;
    node->value.kind = 0u;
    node->value.flags = 0u;
    node->value.auxiliary = 0u;
    node->value.handle = 0u;
    node->value.payload = 0u;
    if (first == 0u)
    {
        parent_node->first_child = index;
        node->previous_sibling = index;
    }
    else
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *first_node =
            advanced_configuration_and_power_interface_machine_language_namespace_node_at(space, first);

        node->previous_sibling = first_node->previous_sibling;
        advanced_configuration_and_power_interface_machine_language_namespace_node_at(space, node->previous_sibling)
            ->next_sibling = index;
        first_node->previous_sibling = index;
    }
    space->slots[slot] = index;
    *out_index = index;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}
