#include "needs_path_closure.h"

#include <kernel/acpi/machine_language_objects.h>

#include <stdlib.h>
#include <string.h>

/**
 * @brief Records an opcode in the bits of its method, or a call in the list.
 */
static void needs_path_closure_observe(void *context, uint32_t method, uint32_t opcode, uint32_t callee)
{
    NeedsPathClosure_t *closure = context;

    if (method >= closure->node_capacity || callee >= closure->node_capacity)
    {
        ++closure->dropped;
        return;
    }
    if (opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT)
    {
        closure->opcodes[method][opcode / 32u] |= 1u << (opcode % 32u);
        return;
    }
    if (closure->call_count == closure->call_capacity)
    {
        const uint32_t capacity = (closure->call_capacity != 0u) ? closure->call_capacity * 2u : 1024u;
        uint32_t *calls = realloc(closure->calls, (size_t) capacity * 2u * sizeof(uint32_t));

        if (calls == NULL)
        {
            ++closure->dropped;
            return;
        }
        closure->calls = calls;
        closure->call_capacity = capacity;
    }
    closure->calls[closure->call_count * 2u] = method;
    closure->calls[closure->call_count * 2u + 1u] = callee;
    ++closure->call_count;
}

/**
 * @brief Whether node @p index is a processor: a Processor object, or a Device whose _HID is the
 *        string "ACPI0007".
 */
static bool needs_path_closure_is_processor(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                            uint32_t index)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node == NULL || node->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PROCESSOR)
        return node != NULL;
    if (node->type != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE)
        return false;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *hardware =
        advanced_configuration_and_power_interface_machine_language_namespace_node(
            &load->space, advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                              &load->space, index,
                              advanced_configuration_and_power_interface_machine_language_name_segment(
                                  (const uint8_t *) "_HID", 0u)));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *text =
        (hardware != NULL &&
         hardware->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING) ?
            advanced_configuration_and_power_interface_machine_language_object(&load->heap, hardware->value.handle) :
            NULL;

    return text != NULL && text->length == 8u && memcmp(text + 1, "ACPI0007", 8u) == 0;
}

/**
 * @brief Whether a method is an entry of the needs path: `_INI`, `_REG`, `_STA` and `_CST`
 *        anywhere, `_OSC` of `\_SB_` or of a processor, `_PDC` of a processor.
 */
static bool needs_path_closure_is_entry(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node)
{
    char name[4];

    memcpy(name, &node->name, sizeof(name));
    if (memcmp(name, "_INI", 4u) == 0 || memcmp(name, "_REG", 4u) == 0 || memcmp(name, "_STA", 4u) == 0 ||
        memcmp(name, "_CST", 4u) == 0)
        return true;
    if (memcmp(name, "_PDC", 4u) == 0)
        return needs_path_closure_is_processor(load, node->parent);
    if (memcmp(name, "_OSC", 4u) != 0)
        return false;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *parent =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node->parent);

    return needs_path_closure_is_processor(load, node->parent) ||
           (parent != NULL && parent->parent == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT &&
            memcmp(&parent->name, "_SB_", 4u) == 0);
}

/**
 * @brief Marks the table-level code and every live method that is an entry; counts the entries.
 */
static void needs_path_closure_mark_entries(NeedsPathClosure_t *closure,
                                            AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    closure->reached[0] = true;
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT + 1u;
         index < load->space.node_count && index < closure->node_capacity; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

        if (node->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD &&
            (node->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE) == 0u &&
            needs_path_closure_is_entry(load, node))
        {
            closure->reached[index] = true;
            ++closure->entries;
        }
    }
}

/**
 * @brief Marks every callee of a reached caller, until a sweep of the calls reaches nothing new.
 */
static void needs_path_closure_follow_calls(NeedsPathClosure_t *closure)
{
    bool grew = true;

    while (grew)
    {
        grew = false;
        for (uint32_t call = 0u; call < closure->call_count; ++call)
        {
            const uint32_t callee = closure->calls[call * 2u + 1u];

            if (closure->reached[closure->calls[call * 2u]] && !closure->reached[callee])
                grew = closure->reached[callee] = true;
        }
    }
}

bool needs_path_closure_attach(NeedsPathClosure_t *closure,
                               AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    memset(closure, 0, sizeof(*closure));
    closure->node_capacity = load->space.max_nodes + 2u;
    closure->opcodes = calloc(closure->node_capacity, sizeof(*closure->opcodes));
    closure->reached = calloc(closure->node_capacity, sizeof(*closure->reached));
    if (closure->opcodes == NULL || closure->reached == NULL)
    {
        needs_path_closure_release(closure);
        return false;
    }
    load->observer = needs_path_closure_observe;
    load->observer_context = closure;
    return true;
}

void needs_path_closure_walk(NeedsPathClosure_t *closure,
                             AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    memset(closure->reached, 0, closure->node_capacity * sizeof(*closure->reached));
    memset(closure->reached_opcodes, 0, sizeof(closure->reached_opcodes));
    closure->entries = 0u;
    closure->methods = 0u;
    needs_path_closure_mark_entries(closure, load);
    needs_path_closure_follow_calls(closure);
    for (uint32_t index = 0u; index < closure->node_capacity; ++index)
    {
        if (!closure->reached[index])
            continue;
        closure->methods += (index != 0u) ? 1u : 0u;
        for (uint32_t word = 0u; word < NEEDS_PATH_CLOSURE_OPCODE_WORDS; ++word)
            closure->reached_opcodes[word] |= closure->opcodes[index][word];
    }
}

bool needs_path_closure_reaches_opcode(const NeedsPathClosure_t *closure, uint32_t opcode)
{
    return opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT &&
           ((closure->reached_opcodes[opcode / 32u] >> (opcode % 32u)) & 1u) != 0u;
}

void needs_path_closure_release(NeedsPathClosure_t *closure)
{
    free(closure->opcodes);
    free(closure->reached);
    free(closure->calls);
    memset(closure, 0, sizeof(*closure));
}
