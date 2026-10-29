#include <kernel/acpi/machine_language_namespace.h>

#include <string.h>

#define MACHINE_LANGUAGE_NAMESPACE_FIRST_SLOTS                                                                         \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES / (uint32_t) sizeof(uint32_t))

/**
 * @brief The four characters of a NameSeg as an integer, as the decoder reads them from AML.
 */
static uint32_t machine_language_namespace_segment(const char *characters)
{
    return advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) characters, 0u);
}

/**
 * @brief The first slot a (parent, name) pair probes: a fixed multiplicative mix, the parent
 *        multiplied before it is folded in so that swapping the two does not collide.
 */
static uint32_t machine_language_namespace_hash(uint32_t parent, uint32_t name)
{
    uint32_t key = name ^ (parent * 0x9E3779B1u);

    key ^= key >> 16u;
    key *= 0x7FEB352Du;
    key ^= key >> 15u;
    key *= 0x846CA68Bu;
    key ^= key >> 16u;
    return key;
}

/**
 * @brief Pages a hash table of @p slot_capacity slots takes.
 */
static uint32_t machine_language_namespace_slot_pages(uint32_t slot_capacity)
{
    return slot_capacity / MACHINE_LANGUAGE_NAMESPACE_FIRST_SLOTS;
}

/**
 * @brief Puts node @p index in the first empty slot of its probe sequence in @p slots.
 */
static void machine_language_namespace_place(uint32_t *slots, uint32_t slot_capacity,
                                             const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node,
                                             uint32_t index)
{
    uint32_t slot = machine_language_namespace_hash(node->parent, node->name) & (slot_capacity - 1u);

    while (slots[slot] != 0u)
        slot = (slot + 1u) & (slot_capacity - 1u);
    slots[slot] = index;
}

/**
 * @brief Doubles the hash table and places every node again.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
machine_language_namespace_grow_slots(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space)
{
    const uint32_t capacity = space->slot_capacity * 2u;
    uint32_t *slots = advanced_configuration_and_power_interface_machine_language_namespace_take_pages(
        space, machine_language_namespace_slot_pages(capacity));

    if (slots == NULL)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY;
    memset(slots, 0, (size_t) capacity * sizeof(uint32_t));
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT + 1u;
         index < space->node_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

        if ((node->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE) == 0u)
            machine_language_namespace_place(slots, capacity, node, index);
    }
    advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
        space, space->slots, machine_language_namespace_slot_pages(space->slot_capacity));
    space->slots = slots;
    space->slot_capacity = capacity;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Takes node @p index out of its parent's children, keeping declaration order.
 */
static void machine_language_namespace_unlink(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                              AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node,
                                              uint32_t index)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *parent =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, node->parent);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *first =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, parent->first_child);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *next =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, node->next_sibling);

    if (parent->first_child == index)
    {
        parent->first_child = node->next_sibling;
        if (next != NULL)
            next->previous_sibling = node->previous_sibling;
        return;
    }
    advanced_configuration_and_power_interface_machine_language_namespace_node(space, node->previous_sibling)
        ->next_sibling = node->next_sibling;
    if (next != NULL)
        next->previous_sibling = node->previous_sibling;
    else
        first->previous_sibling = node->previous_sibling;
}

/**
 * @brief Empties the slot that holds node @p index, then moves back each node of the probe run
 *        after it whose own probe sequence passes the emptied slot, so that no lookup stops early.
 */
static void machine_language_namespace_unplace(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                               const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node,
                                               uint32_t index)
{
    const uint32_t mask = space->slot_capacity - 1u;
    uint32_t hole = machine_language_namespace_hash(node->parent, node->name) & mask;

    while (space->slots[hole] != index)
        hole = (hole + 1u) & mask;
    for (uint32_t slot = (hole + 1u) & mask; space->slots[slot] != 0u; slot = (slot + 1u) & mask)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *moved =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, space->slots[slot]);
        const uint32_t home = machine_language_namespace_hash(moved->parent, moved->name) & mask;

        if (((slot - home) & mask) >= ((slot - hole) & mask))
        {
            space->slots[hole] = space->slots[slot];
            hole = slot;
        }
    }
    space->slots[hole] = 0u;
}

/**
 * @brief Takes the next unused node index, within the node cap, after growing the hash table or
 *        taking a run of nodes when the new node needs it.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
machine_language_namespace_new_index(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                     uint32_t *out_index)
{
    const uint32_t index = space->node_count;

    if (index - ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT >= space->max_nodes)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_NAMESPACE_NODES;
    if ((index + 1u) * 2u > space->slot_capacity &&
        machine_language_namespace_grow_slots(space) !=
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY;
    if (index / ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN == space->run_count)
    {
        if (space->run_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RUNS)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_NAMESPACE_NODES;

        AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *run =
            advanced_configuration_and_power_interface_machine_language_namespace_take_pages(
                space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RUN_PAGES);

        if (run == NULL)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY;
        space->runs[space->run_count++] = run;
    }
    ++space->node_count;
    *out_index = index;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Follows aliases from node @p index to the object they name.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
machine_language_namespace_follow_aliases(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                          uint32_t index, uint32_t *out_index)
{
    for (uint32_t hop = 0u; hop <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_ALIAS_HOPS; ++hop)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

        if (node == NULL)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;
        if (node->type != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_ALIAS)
        {
            *out_index = index;
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
        }
        index = node->value.handle;
    }
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;
}

/**
 * @brief The node a NameString's prefix starts from: the root for `\`, else @p scope climbed once
 *        for each `^`.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t machine_language_namespace_prefix_start(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t scope,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *name, uint32_t *out_start)
{
    uint32_t start = name->root ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT : scope;

    for (uint32_t climb = 0u; climb < name->parent_prefixes; ++climb)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, start);

        if (node == NULL || node->parent == 0u)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;
        start = node->parent;
    }
    *out_start = start;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Walks @p count segments of a NameString down from @p start, each one a child that must
 *        exist, through aliases.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t machine_language_namespace_walk_segments(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t start, const uint8_t *bytes,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *name, uint32_t count, uint32_t *out_index)
{
    uint32_t index = start;

    for (uint32_t segment = 0u; segment < count; ++segment)
    {
        const uint32_t child = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            space, index,
            advanced_configuration_and_power_interface_machine_language_name_segment(bytes,
                                                                                     name->segments + segment * 4u));

        if (child == 0u || machine_language_namespace_follow_aliases(space, child, &index) !=
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;
    }
    *out_index = index;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

/**
 * @brief Creates one predefined child of the root.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
machine_language_namespace_predefine(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                     const char *name, uint8_t type, uint32_t *out_index)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        machine_language_namespace_segment(name), type, 0u, out_index);
}

void *advanced_configuration_and_power_interface_machine_language_namespace_take_pages(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t count)
{
    const uint32_t bytes = count * ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES;

    if (bytes > space->max_arena_bytes - space->arena_bytes)
        return NULL;

    void *pages = space->host->allocate_pages(space->host->context, count);

    if (pages != NULL)
        space->arena_bytes += bytes;
    return pages;
}

void advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, void *pages, uint32_t count)
{
    space->host->release_pages(space->host->context, pages, count);
    space->arena_bytes -= count * ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
    const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t max_nodes, uint32_t max_arena_bytes)
{
    static const struct {
        const char *name;
        uint8_t type;
    } predefined[] = {
        {"_GPE", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_SCOPE  },
        {"_PR_", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_SCOPE  },
        {"_SB_", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE },
        {"_SI_", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_SCOPE  },
        {"_TZ_", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE },
        {"_GL_", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_MUTEX  },
        {"_OS_", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_STRING },
        {"_REV", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_INTEGER},
    };
    uint32_t index = 0u;

    memset(space, 0, sizeof(*space));
    space->host = host;
    space->max_nodes = max_nodes;
    space->max_arena_bytes = max_arena_bytes;
    space->node_count = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
    space->slots = advanced_configuration_and_power_interface_machine_language_namespace_take_pages(space, 1u);
    if (space->slots == NULL)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY;
    space->slot_capacity = MACHINE_LANGUAGE_NAMESPACE_FIRST_SLOTS;
    memset(space->slots, 0, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            space, 0u, machine_language_namespace_segment("\\___"),
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_SCOPE, 0u, &index);

    for (uint32_t entry = 0u; entry < sizeof(predefined) / sizeof(predefined[0]) &&
                              status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
         ++entry)
        status = machine_language_namespace_predefine(space, predefined[entry].name, predefined[entry].type, &index);
    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    status = machine_language_namespace_predefine(
        space, "_OSI", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD, &index);
    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->value.auxiliary = 1u;
    return status;
}

void advanced_configuration_and_power_interface_machine_language_namespace_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space)
{
    for (uint32_t run = 0u; run < space->run_count; ++run)
        advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
            space, space->runs[run], ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RUN_PAGES);
    if (space->slots != NULL)
        advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
            space, space->slots, machine_language_namespace_slot_pages(space->slot_capacity));
    space->run_count = 0u;
    space->slots = NULL;
    space->slot_capacity = 0u;
    space->node_count = 0u;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *
advanced_configuration_and_power_interface_machine_language_namespace_node(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index)
{
    if (index == 0u || index >= space->node_count)
        return NULL;
    return &space->runs[index / ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN]
                       [index % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODES_PER_RUN];
}

uint32_t advanced_configuration_and_power_interface_machine_language_namespace_find_child(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name)
{
    uint32_t slot = machine_language_namespace_hash(parent, name) & (space->slot_capacity - 1u);

    ++space->lookups;
    for (;;)
    {
        const uint32_t index = space->slots[slot];

        ++space->probes;
        if (index == 0u)
            return 0u;

        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

        if (node->parent == parent && node->name == name)
            return index;
        slot = (slot + 1u) & (space->slot_capacity - 1u);
    }
}

uint32_t advanced_configuration_and_power_interface_machine_language_namespace_next(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index, bool descend)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

    if (descend && node->first_child != 0u)
        return node->first_child;
    while (index != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT && node->next_sibling == 0u)
    {
        index = node->parent;
        node = advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);
    }
    return (index == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT) ? 0u : node->next_sibling;
}

uint32_t advanced_configuration_and_power_interface_machine_language_namespace_find_object(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name)
{
    const uint32_t child =
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(space, parent, name);
    uint32_t index = 0u;

    if (child == 0u || machine_language_namespace_follow_aliases(space, child, &index) !=
                           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return 0u;
    return (advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->type ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_EXTERNAL) ?
               0u :
               index;
}

uint32_t advanced_configuration_and_power_interface_machine_language_namespace_path(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index, char *out,
    uint32_t capacity)
{
    uint32_t depth = 0u;

    for (uint32_t at = index; at != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         at = advanced_configuration_and_power_interface_machine_language_namespace_node(space, at)->parent)
        ++depth;

    const uint32_t length = (depth == 0u) ? 1u : 5u * depth;

    if (length >= capacity)
        return 0u;
    out[0] = '\\';
    out[length] = '\0';
    for (uint32_t at = index, end = length; at != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         at = advanced_configuration_and_power_interface_machine_language_namespace_node(space, at)->parent)
    {
        const uint32_t name =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, at)->name;

        end -= 4u;
        for (uint32_t character = 0u; character < 4u; ++character)
            out[end + character] = (char) (name >> (8u * character));
        if (end > 1u)
            out[--end] = '.';
    }
    return length;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_add_child(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t parent, uint32_t name,
    uint8_t type, uint16_t owner, uint32_t *out_index)
{
    uint32_t index = space->free_nodes;

    if (index != 0u)
        space->free_nodes =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->next_sibling;
    else
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
            machine_language_namespace_new_index(space, &index);

        if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
            return status;
    }

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

    memset(node, 0, sizeof(*node));
    ++space->live_nodes;
    node->name = name;
    node->parent = parent;
    node->type = type;
    node->owner = owner;
    if (parent != 0u)
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *parent_node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, parent);

        if (parent_node->first_child == 0u)
        {
            parent_node->first_child = index;
            node->previous_sibling = index;
        }
        else
        {
            AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *first =
                advanced_configuration_and_power_interface_machine_language_namespace_node(space,
                                                                                           parent_node->first_child);

            node->previous_sibling = first->previous_sibling;
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, first->previous_sibling)
                ->next_sibling = index;
            first->previous_sibling = index;
        }
        machine_language_namespace_place(space->slots, space->slot_capacity, node, index);
    }
    *out_index = index;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

void advanced_configuration_and_power_interface_machine_language_namespace_remove(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t index)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

    machine_language_namespace_unlink(space, node, index);
    machine_language_namespace_unplace(space, node, index);
    node->flags = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE;
    --space->live_nodes;
    node->next_sibling = space->free_nodes;
    space->free_nodes = index;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_resolve(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t scope, const uint8_t *bytes,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *name, uint32_t *out_index)
{
    if (!name->root && name->parent_prefixes == 0u && name->segment_count == 1u)
    {
        const uint32_t segment =
            advanced_configuration_and_power_interface_machine_language_name_segment(bytes, name->segments);

        for (uint32_t search = scope; search != 0u;)
        {
            const uint32_t child = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                space, search, segment);

            if (child != 0u)
                return machine_language_namespace_follow_aliases(space, child, out_index);

            const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
                advanced_configuration_and_power_interface_machine_language_namespace_node(space, search);

            search = (node != NULL) ? node->parent : 0u;
        }
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;
    }
    if (!name->root && name->parent_prefixes == 0u && name->segment_count == 0u)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;

    uint32_t start = 0u;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        machine_language_namespace_prefix_start(space, scope, name, &start);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    return machine_language_namespace_walk_segments(space, start, bytes, name, name->segment_count, out_index);
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space, uint32_t scope, const uint8_t *bytes,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *name, uint32_t *out_parent,
    uint32_t *out_name)
{
    if (name->segment_count == 0u)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND;

    uint32_t start = 0u;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        machine_language_namespace_prefix_start(space, scope, name, &start);

    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        status =
            machine_language_namespace_walk_segments(space, start, bytes, name, name->segment_count - 1u, out_parent);
    if (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        *out_name = advanced_configuration_and_power_interface_machine_language_name_segment(
            bytes, name->segments + (name->segment_count - 1u) * 4u);
    return status;
}
