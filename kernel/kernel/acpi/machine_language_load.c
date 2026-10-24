#include <kernel/acpi/machine_language.h>

#include <string.h>

#define MACHINE_LANGUAGE_CALL          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
#define MACHINE_LANGUAGE_OK            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_OPERAND(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_##kind
#define MACHINE_LANGUAGE_OPCODE(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define MACHINE_LANGUAGE_NODE(type)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name

#if defined(LPL_KERNEL_REAL_TIME_MODE)
#    define MACHINE_LANGUAGE_BOOT_MAX_NODES ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OTHER_MAX_NODES
#    define MACHINE_LANGUAGE_BOOT_MAX_ARENA_BYTES                                                                      \
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OTHER_MAX_ARENA_BYTES
#else
#    define MACHINE_LANGUAGE_BOOT_MAX_NODES ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES
#    define MACHINE_LANGUAGE_BOOT_MAX_ARENA_BYTES                                                                      \
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES
#endif

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t MachineLanguagePendingOperator_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t MachineLanguageBlock_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t MachineLanguageNameString_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;

/**
 * @brief Which pass a walk is: the first declares and skips method bodies, the second decodes
 *        every term and counts it.
 */
typedef enum MachineLanguagePass {
    MACHINE_LANGUAGE_PASS_DECLARE,   /**< Declares named objects; skips method bodies. */
    MACHINE_LANGUAGE_PASS_EXTERNALS, /**< Places the Externals whose path the first pass could not find, once every
                                          table is declared; skips method bodies, declares nothing else, records no
                                          fault. */
    MACHINE_LANGUAGE_PASS_DECODE,    /**< Decodes and counts every term; declares only the temporary nodes of method
                                          bodies. */
} MachineLanguagePass_t;

/**
 * @brief One walk over one table: where it is, its stacks' depths, and the fault it may report.
 */
typedef struct MachineLanguageWalk {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load; /**< The load it belongs to. */
    const uint8_t *bytes;                                              /**< The table, header included. */
    uint32_t table;                                                    /**< The table's index in the load. */
    uint32_t pc;                                                       /**< The next byte to decode. */
    uint32_t pending_count;                                            /**< Operators pending. */
    uint32_t block_count;                                              /**< Constructs open, the table included. */
    uint32_t fault_opcode;                                             /**< The opcode a fault at pc belongs to. */
    MachineLanguagePass_t pass;                                        /**< Which pass it is. */
    uint32_t last_created;                     /**< The node the last declaration created, 0 when it created none. */
    uint32_t external_start;                   /**< The External the third pass places, by its opcode's offset, or 0. */
    MachineLanguageNameString_t external_name; /**< That External's NameString. */
} MachineLanguageWalk_t;

static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t machine_language_boot_load;

/**
 * @brief The construct the walk is in.
 */
static MachineLanguageBlock_t *machine_language_top_block(MachineLanguageWalk_t *walk)
{
    return &walk->load->blocks[walk->block_count - 1u];
}

/**
 * @brief The operator on top of the stack, when it belongs to the construct the walk is in.
 */
static MachineLanguagePendingOperator_t *machine_language_active_operator(MachineLanguageWalk_t *walk)
{
    if (walk->pending_count <= machine_language_top_block(walk)->pending_base)
        return NULL;
    return &walk->load->pending[walk->pending_count - 1u];
}

/**
 * @brief The first offset the next term may not reach: the active operator's end, else the
 *        construct's.
 */
static uint32_t machine_language_limit(MachineLanguageWalk_t *walk)
{
    const MachineLanguagePendingOperator_t *active = machine_language_active_operator(walk);

    return (active != NULL) ? active->end : machine_language_top_block(walk)->end;
}

/**
 * @brief The node at @p index of the walk's namespace.
 */
static MachineLanguageNode_t *machine_language_node(MachineLanguageWalk_t *walk, uint32_t index)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(&walk->load->space, index);
}

/**
 * @brief The arguments a call of node @p index takes, when it names a method or an External one.
 */
static bool machine_language_method_arguments(MachineLanguageWalk_t *walk, uint32_t index, uint32_t *out_arguments)
{
    const MachineLanguageNode_t *node = machine_language_node(walk, index);

    if (node != NULL && node->type == MACHINE_LANGUAGE_NODE(METHOD))
    {
        *out_arguments = node->value.auxiliary & 0x7u;
        return true;
    }
    if (node != NULL && node->type == MACHINE_LANGUAGE_NODE(EXTERNAL) &&
        node->value.auxiliary == MACHINE_LANGUAGE_NODE(METHOD))
    {
        *out_arguments = (uint32_t) node->value.payload & 0x7u;
        return true;
    }
    return false;
}

/**
 * @brief Counts an opcode in the second pass.
 */
static void machine_language_count_opcode(MachineLanguageWalk_t *walk, uint32_t opcode)
{
    if (walk->pass != MACHINE_LANGUAGE_PASS_DECODE)
        return;
    ++walk->load->opcodes[opcode];
    ++walk->load->tables[walk->table].terms;
}

/**
 * @brief Counts a fault of the first pass.
 */
static void machine_language_count_load_error(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                              MachineLanguageStatus_t status)
{
    if (load->load_errors++ == 0u)
        load->first_load_error = (uint8_t) status;
}

/**
 * @brief Records a fault at the walk's pc: in load_errors for the first pass, in its table and in
 *        decode_errors for the second.
 */
static void machine_language_record_fault(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;

    if (walk->pass == MACHINE_LANGUAGE_PASS_EXTERNALS)
        return;
    if (walk->pass == MACHINE_LANGUAGE_PASS_DECLARE)
    {
        machine_language_count_load_error(load, status);
        return;
    }

    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[walk->table];

    ++load->decode_errors;
    if (table->decode_errors++ == 0u)
    {
        table->first_error = (uint8_t) status;
        table->first_error_offset = walk->pc;
        table->first_error_opcode =
            (uint16_t) ((walk->fault_opcode < MACHINE_LANGUAGE_CALL) ? walk->fault_opcode :
                                                                       MACHINE_LANGUAGE_OPCODE(INVALID));
    }
}

/**
 * @brief Closes the construct the walk is in; the end of a method body removes the temporary
 *        nodes it created, the last first.
 */
static void machine_language_close_block(MachineLanguageWalk_t *walk)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;

    if (machine_language_top_block(walk)->placement ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY)
    {
        const uint32_t base = load->temporary_bases[--load->frame_count];

        while (load->temporary_count > base)
            advanced_configuration_and_power_interface_machine_language_namespace_remove(
                &load->space, load->temporaries[--load->temporary_count]);
    }
    --walk->block_count;
}

/**
 * @brief Goes on after a fault: past the innermost construct with a PkgLength around it, else
 *        past the rest of the table, which is also where a PkgLength crossing its parent leads.
 */
static void machine_language_recover(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status)
{
    const MachineLanguageBlock_t *block = machine_language_top_block(walk);

    if (status != MACHINE_LANGUAGE_STATUS(PACKAGE_LENGTH_OUT_OF_PARENT))
    {
        for (uint32_t index = walk->pending_count; index > block->pending_base; --index)
        {
            const MachineLanguagePendingOperator_t *pending = &walk->load->pending[index - 1u];

            if (pending->opcode != MACHINE_LANGUAGE_CALL &&
                (advanced_configuration_and_power_interface_machine_language_descriptor(pending->opcode)->flags &
                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH) != 0u)
            {
                walk->pc = pending->end;
                walk->pending_count = index - 1u;
                return;
            }
        }
        if (walk->block_count > 1u)
        {
            walk->pc = block->end;
            walk->pending_count = block->pending_base;
            machine_language_close_block(walk);
            return;
        }
    }
    walk->pending_count = 0u;
    while (walk->block_count != 0u)
        machine_language_close_block(walk);
}

/**
 * @brief Creates the node a declaration names in the scope the walk is in, or finds the one that
 *        holds the name already: an External gives way to the object, any other existing node
 *        stays and the declaration counts as a duplicate.
 */
static MachineLanguageStatus_t machine_language_declare(MachineLanguageWalk_t *walk,
                                                        const MachineLanguageNameString_t *name, uint8_t type,
                                                        uint32_t *out_index)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t parent = 0u;
    uint32_t segment = 0u;
    MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &load->space, machine_language_top_block(walk)->scope, walk->bytes, name, &parent, &segment);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

    const uint32_t existing =
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent, segment);

    if (existing != 0u)
    {
        MachineLanguageNode_t *node = machine_language_node(walk, existing);

        walk->last_created = 0u;
        if (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL) && type != MACHINE_LANGUAGE_NODE(EXTERNAL))
        {
            node->type = type;
            node->owner = (uint16_t) (walk->table + 1u);
            memset(&node->value, 0, sizeof(node->value));
            ++load->declarations;
            walk->last_created = existing;
        }
        else if (type != MACHINE_LANGUAGE_NODE(EXTERNAL))
            ++load->duplicates;
        *out_index = existing;
        return MACHINE_LANGUAGE_OK;
    }
    status = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        &load->space, parent, segment, type, (uint16_t) (walk->table + 1u), out_index);
    if (status == MACHINE_LANGUAGE_OK)
        ++load->declarations;
    walk->last_created = (status == MACHINE_LANGUAGE_OK) ? *out_index : 0u;
    return status;
}

/**
 * @brief The node a scope-opening declaration at table level names in the second pass, which the
 *        first pass created; the scope the walk is in when it cannot be found, so that its body
 *        still decodes.
 */
static uint32_t machine_language_declared_node(MachineLanguageWalk_t *walk, const MachineLanguageNameString_t *name)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &walk->load->space;
    const uint32_t scope = machine_language_top_block(walk)->scope;
    uint32_t parent = 0u;
    uint32_t segment = 0u;
    uint32_t index = 0u;

    if (advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            space, scope, walk->bytes, name, &parent, &segment) == MACHINE_LANGUAGE_OK)
        index =
            advanced_configuration_and_power_interface_machine_language_namespace_find_child(space, parent, segment);
    return (index != 0u) ? index : scope;
}

/**
 * @brief Creates a temporary node for a declaration a method body makes, unless a node already
 *        holds the name, and remembers it so that the end of the body removes it.
 */
static MachineLanguageStatus_t machine_language_declare_temporary(MachineLanguageWalk_t *walk,
                                                                  const MachineLanguageNameString_t *name, uint8_t type,
                                                                  uint32_t *out_index)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t parent = 0u;
    uint32_t segment = 0u;

    *out_index = machine_language_top_block(walk)->scope;
    if (advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &load->space, *out_index, walk->bytes, name, &parent, &segment) != MACHINE_LANGUAGE_OK)
        return MACHINE_LANGUAGE_OK;

    const uint32_t existing =
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent, segment);

    walk->last_created = 0u;
    if (existing != 0u)
    {
        *out_index = existing;
        return MACHINE_LANGUAGE_OK;
    }
    if (load->temporary_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_TEMPORARY_NODES);

    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            &load->space, parent, segment, type, (uint16_t) (walk->table + 1u), out_index);

    if (status == MACHINE_LANGUAGE_OK)
    {
        load->temporaries[load->temporary_count++] = *out_index;
        walk->last_created = *out_index;
    }
    return status;
}

/**
 * @brief The node type the declaration an opcode makes creates, or 0xFF when it declares nothing.
 */
static uint8_t machine_language_declared_type(uint32_t opcode)
{
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(NAME): return MACHINE_LANGUAGE_NODE(UNINITIALIZED);
    case MACHINE_LANGUAGE_OPCODE(METHOD): return MACHINE_LANGUAGE_NODE(METHOD);
    case MACHINE_LANGUAGE_OPCODE(EXTERNAL): return MACHINE_LANGUAGE_NODE(EXTERNAL);
    case MACHINE_LANGUAGE_OPCODE(MUTEX): return MACHINE_LANGUAGE_NODE(MUTEX);
    case MACHINE_LANGUAGE_OPCODE(EVENT): return MACHINE_LANGUAGE_NODE(EVENT);
    case MACHINE_LANGUAGE_OPCODE(OPERATION_REGION):
    case MACHINE_LANGUAGE_OPCODE(DATA_REGION): return MACHINE_LANGUAGE_NODE(OPERATION_REGION);
    case MACHINE_LANGUAGE_OPCODE(DEVICE): return MACHINE_LANGUAGE_NODE(DEVICE);
    case MACHINE_LANGUAGE_OPCODE(PROCESSOR): return MACHINE_LANGUAGE_NODE(PROCESSOR);
    case MACHINE_LANGUAGE_OPCODE(POWER_RESOURCE): return MACHINE_LANGUAGE_NODE(POWER_RESOURCE);
    case MACHINE_LANGUAGE_OPCODE(THERMAL_ZONE): return MACHINE_LANGUAGE_NODE(THERMAL_ZONE);
    case MACHINE_LANGUAGE_OPCODE(CREATE_BIT_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_BYTE_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_DOUBLE_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_QUAD_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_FIELD): return MACHINE_LANGUAGE_NODE(BUFFER_FIELD);
    default: return 0xFFu;
    }
}

/**
 * @brief The node an External's last segment belongs to, in the third pass: every missing segment
 *        before it is created as an External with no object, since no table declares it.
 */
static MachineLanguageStatus_t machine_language_external_parent(MachineLanguageWalk_t *walk, uint32_t *out_parent)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const MachineLanguageNameString_t *name = &walk->external_name;
    uint32_t parent = name->root ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT :
                                   machine_language_top_block(walk)->scope;

    for (uint32_t climb = 0u; climb < name->parent_prefixes; ++climb)
    {
        parent = machine_language_node(walk, parent)->parent;
        if (parent == 0u)
            return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
    }
    for (uint32_t segment = 0u; segment + 1u < name->segment_count; ++segment)
    {
        const uint32_t seg = advanced_configuration_and_power_interface_machine_language_name_segment(
            walk->bytes, name->segments + segment * 4u);
        uint32_t child =
            advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent, seg);

        if (child == 0u)
        {
            const MachineLanguageStatus_t status =
                advanced_configuration_and_power_interface_machine_language_namespace_add_child(
                    &load->space, parent, seg, MACHINE_LANGUAGE_NODE(EXTERNAL), (uint16_t) (walk->table + 1u), &child);

            if (status != MACHINE_LANGUAGE_OK)
                return status;
            ++load->declarations;
        }
        parent = child;
    }
    *out_parent = parent;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Places, in the third pass, the External whose NameString the walk kept and whose ObjectType
 *        and argument count it has just read, where nothing holds its name: a method no table
 *        declares is then still called with the right number of arguments. One whose path still
 *        cannot be placed, climbing above the root, is counted.
 */
static void machine_language_place_external(MachineLanguageWalk_t *walk, uint8_t object_type, uint8_t arguments)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const uint32_t segment = advanced_configuration_and_power_interface_machine_language_name_segment(
        walk->bytes, walk->external_name.segments + (walk->external_name.segment_count - 1u) * 4u);
    uint32_t parent = 0u;
    uint32_t node = 0u;
    MachineLanguageStatus_t status = machine_language_external_parent(walk, &parent);

    walk->external_start = 0u;
    if (status == MACHINE_LANGUAGE_OK && segment == 0u)
        return;
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            &load->space, parent, segment, MACHINE_LANGUAGE_NODE(EXTERNAL), (uint16_t) (walk->table + 1u), &node);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_count_load_error(load, status);
        return;
    }
    machine_language_node(walk, node)->value.auxiliary = object_type;
    machine_language_node(walk, node)->value.payload = arguments & 0x7u;
    ++load->declarations;
}

/**
 * @brief What an External's NameString means in the third pass: nothing when a node holds the name;
 *        a NullName is counted; any other is kept until its ObjectType and argument count are read.
 */
static void machine_language_keep_external(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *active,
                                           const MachineLanguageNameString_t *name)
{
    uint32_t parent = 0u;
    uint32_t segment = 0u;

    if (name->segment_count == 0u)
    {
        machine_language_count_load_error(walk->load, MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND));
        return;
    }
    if (advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &walk->load->space, machine_language_top_block(walk)->scope, walk->bytes, name, &parent, &segment) ==
            MACHINE_LANGUAGE_OK &&
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(&walk->load->space, parent,
                                                                                         segment) != 0u)
        return;
    walk->external_name = *name;
    walk->external_start = active->start;
}

/**
 * @brief What a NameString operand of the active operator means: a Scope's target, an Alias's
 *        source then its new name, or the name a declaration creates. The first pass creates
 *        permanent nodes; the third places the Externals the first could not; the second creates
 *        temporary nodes in method bodies. Past the first pass, at table level, a construct only
 *        finds the scope it opens, and a scope that cannot be found is the one the walk is in.
 */
static MachineLanguageStatus_t machine_language_use_name(MachineLanguageWalk_t *walk,
                                                         MachineLanguagePendingOperator_t *active,
                                                         const MachineLanguageNameString_t *name)
{
    const MachineLanguageBlock_t *block = machine_language_top_block(walk);
    const bool declaring = walk->pass == MACHINE_LANGUAGE_PASS_DECLARE;
    const bool opens_scope =
        (advanced_configuration_and_power_interface_machine_language_descriptor(active->opcode)->flags &
         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_OPENS_SCOPE) != 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (active->opcode == MACHINE_LANGUAGE_OPCODE(SCOPE) ||
        (active->opcode == MACHINE_LANGUAGE_OPCODE(ALIAS) && active->operands_done == 1u))
    {
        status = advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &walk->load->space, block->scope, walk->bytes, name, &active->node);
        if (status == MACHINE_LANGUAGE_OK || (declaring && active->opcode == MACHINE_LANGUAGE_OPCODE(SCOPE)))
            return status;
        active->node = (active->opcode == MACHINE_LANGUAGE_OPCODE(SCOPE)) ? block->scope : 0u;
        return MACHINE_LANGUAGE_OK;
    }

    const uint8_t type = (active->opcode == MACHINE_LANGUAGE_OPCODE(ALIAS)) ?
                             (uint8_t) MACHINE_LANGUAGE_NODE(ALIAS) :
                             machine_language_declared_type(active->opcode);
    const uint32_t target = active->node;

    if (type == 0xFFu)
        return MACHINE_LANGUAGE_OK;
    if (!declaring && block->placement == 0u)
    {
        if (opens_scope)
            active->node = machine_language_declared_node(walk, name);
        else if (walk->pass == MACHINE_LANGUAGE_PASS_EXTERNALS && active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL))
            machine_language_keep_external(walk, active, name);
        return MACHINE_LANGUAGE_OK;
    }
    if (type == MACHINE_LANGUAGE_NODE(ALIAS) && target == 0u)
        return declaring ? MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND) : MACHINE_LANGUAGE_OK;
    status = declaring ? machine_language_declare(walk, name, type, &active->node) :
                         machine_language_declare_temporary(walk, name, type, &active->node);
    if (status == MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND) && !opens_scope)
    {
        active->node = 0u;
        if (active->opcode != MACHINE_LANGUAGE_OPCODE(EXTERNAL))
            machine_language_record_fault(walk, status);
        return MACHINE_LANGUAGE_OK;
    }
    if (status == MACHINE_LANGUAGE_OK && type == MACHINE_LANGUAGE_NODE(ALIAS) && active->node == walk->last_created)
        machine_language_node(walk, active->node)->value.handle = target;
    return status;
}

/**
 * @brief Pushes the operator whose opcode was read at @p start, after reading its PkgLength.
 */
static MachineLanguageStatus_t machine_language_push_operator(MachineLanguageWalk_t *walk, uint32_t opcode,
                                                              uint32_t start, uint32_t after)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *descriptor =
        advanced_configuration_and_power_interface_machine_language_descriptor(opcode);
    uint32_t end = machine_language_limit(walk);

    if ((descriptor->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH) != 0u)
    {
        const MachineLanguageStatus_t status =
            advanced_configuration_and_power_interface_machine_language_read_package_end(walk->bytes, after, end, &end,
                                                                                         &after);

        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    if (descriptor->operand_count == 0u)
    {
        walk->pc = after;
        return MACHINE_LANGUAGE_OK;
    }
    if (walk->pending_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PENDING_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *pending = &walk->load->pending[walk->pending_count++];

    pending->opcode = (uint16_t) opcode;
    pending->operands_done = 0u;
    pending->operand_count = descriptor->operand_count;
    pending->start = start;
    pending->end = end;
    pending->node = 0u;
    if (walk->pending_count > walk->load->pending_peak)
        walk->load->pending_peak = walk->pending_count;
    walk->pc = after;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Reads a NameString in an operand: a call when it names a method and @p may_call holds,
 *        which pushes an operator for its arguments; a reference otherwise.
 */
static MachineLanguageStatus_t machine_language_name_term(MachineLanguageWalk_t *walk, bool may_call)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const uint32_t start = walk->pc;
    const uint32_t limit = machine_language_limit(walk);
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    uint32_t index = 0u;
    uint32_t arguments = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        walk->bytes, start, limit, &name, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    walk->pc = after;

    const bool counting = walk->pass == MACHINE_LANGUAGE_PASS_DECODE;
    const bool resolved =
        advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, machine_language_top_block(walk)->scope, walk->bytes, &name, &index) == MACHINE_LANGUAGE_OK;

    load->name_references += counting ? 1u : 0u;
    load->unresolved_names += (counting && !resolved) ? 1u : 0u;
    if (!resolved || !may_call || !machine_language_method_arguments(walk, index, &arguments))
        return MACHINE_LANGUAGE_OK;
    load->method_calls += counting ? 1u : 0u;
    if (arguments == 0u)
        return MACHINE_LANGUAGE_OK;
    if (walk->pending_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PENDING_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *pending = &load->pending[walk->pending_count++];

    pending->opcode = MACHINE_LANGUAGE_CALL;
    pending->operands_done = 0u;
    pending->operand_count = (uint8_t) arguments;
    pending->start = start;
    pending->end = limit;
    pending->node = index;
    if (walk->pending_count > load->pending_peak)
        load->pending_peak = walk->pending_count;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Decodes one term: a statement of a TermList when @p kind is NONE, else an operand of that
 *        kind, where a statement is refused and NullName is a SuperName. Any other opcode may stand
 *        where a SuperName is expected, as in ACPICA's parser: firmware ships `Store (X, Add (...))`.
 */
static MachineLanguageStatus_t
machine_language_term(MachineLanguageWalk_t *walk,
                      AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    const uint32_t start = walk->pc;
    const uint32_t limit = machine_language_limit(walk);
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (start >= limit)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);

    const uint8_t lead = walk->bytes[start];

    if (lead == 0x00u && (kind == MACHINE_LANGUAGE_OPERAND(SUPER_NAME) || kind == MACHINE_LANGUAGE_OPERAND(TARGET) ||
                          kind == MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME)))
    {
        walk->pc = start + 1u;
        return MACHINE_LANGUAGE_OK;
    }
    if (advanced_configuration_and_power_interface_machine_language_starts_name(lead))
        return machine_language_name_term(walk, kind != MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME));

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        walk->bytes, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    walk->fault_opcode = opcode;
    if (kind != MACHINE_LANGUAGE_OPERAND(NONE) &&
        (advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags &
         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_STATEMENT) != 0u)
        return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
    machine_language_count_opcode(walk, opcode);
    return machine_language_push_operator(walk, opcode, start, after);
}

/**
 * @brief Decodes one element of a PackageElementList: a name, never called, or a data object.
 */
static MachineLanguageStatus_t machine_language_package_element(MachineLanguageWalk_t *walk)
{
    const uint32_t start = walk->pc;
    const uint32_t limit = machine_language_limit(walk);
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (advanced_configuration_and_power_interface_machine_language_starts_name(walk->bytes[start]))
        return machine_language_name_term(walk, false);

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        walk->bytes, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    walk->fault_opcode = opcode;
    if ((advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags &
         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT) == 0u)
        return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
    machine_language_count_opcode(walk, opcode);
    return machine_language_push_operator(walk, opcode, start, after);
}

/**
 * @brief Decodes one element of a FieldList: a reserved field, an AccessAs, a Connection to a name
 *        or a buffer, an ExtendedAccessAs, or a named field unit, which the first pass declares.
 */
static MachineLanguageStatus_t machine_language_field_element(MachineLanguageWalk_t *walk)
{
    const uint32_t start = walk->pc;
    const uint32_t limit = machine_language_limit(walk);
    const uint8_t lead = walk->bytes[start];
    uint32_t bit_length = 0u;
    uint32_t after = 0u;
    uint32_t index = 0u;
    MachineLanguageNameString_t name;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    switch (lead)
    {
    case 0x00u:
        status = advanced_configuration_and_power_interface_machine_language_read_package_length(
            walk->bytes, start + 1u, limit, &bit_length, &after);
        break;
    case 0x01u:
    case 0x03u:
        after = start + ((lead == 0x01u) ? 3u : 4u);
        if (after > limit)
            status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
        break;
    case 0x02u:
        if (limit - start < 2u)
            return MACHINE_LANGUAGE_STATUS(TRUNCATED);
        walk->pc = start + 1u;
        if (walk->bytes[start + 1u] == 0x11u)
            return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(TERM_ARGUMENT));
        return machine_language_name_term(walk, false);
    default:
        status = advanced_configuration_and_power_interface_machine_language_read_name_string(walk->bytes, start, limit,
                                                                                              &name, &after);
        if (status == MACHINE_LANGUAGE_OK &&
            (name.root || name.parent_prefixes != 0u || name.segment_count != 1u || name.segments != start))
            status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
        if (status == MACHINE_LANGUAGE_OK)
            status = advanced_configuration_and_power_interface_machine_language_read_package_length(
                walk->bytes, after, limit, &bit_length, &after);
        if (status == MACHINE_LANGUAGE_OK && walk->pass == MACHINE_LANGUAGE_PASS_DECLARE)
            status = machine_language_declare(walk, &name, MACHINE_LANGUAGE_NODE(FIELD_UNIT), &index);
        else if (status == MACHINE_LANGUAGE_OK && machine_language_top_block(walk)->placement != 0u)
            status = machine_language_declare_temporary(walk, &name, MACHINE_LANGUAGE_NODE(FIELD_UNIT), &index);
        break;
    }
    if (status == MACHINE_LANGUAGE_OK)
        walk->pc = after;
    return status;
}

/**
 * @brief Records a method's flags, table and body in its node, unless the node was declared before:
 *        in the first pass for a permanent method, in the second for one a method body declares.
 */
static void machine_language_record_method(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageNode_t *node = machine_language_node(walk, active->node);

    if (node == NULL || node->type != MACHINE_LANGUAGE_NODE(METHOD) || node->value.payload != 0u)
        return;
    node->value.auxiliary = walk->bytes[walk->pc - 1u];
    node->value.handle = walk->table + 1u;
    node->value.payload = (uint64_t) walk->pc | ((uint64_t) (active->end - walk->pc) << 32u);
}

/**
 * @brief Opens the list that ends the active operator: a TermList, a FieldList or a
 *        PackageElementList, run as a construct up to the operator's end. A method body is skipped
 *        in the first pass.
 */
static MachineLanguageStatus_t
machine_language_open_list(MachineLanguageWalk_t *walk,
                           AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    const MachineLanguagePendingOperator_t active = walk->load->pending[walk->pending_count - 1u];
    const MachineLanguageBlock_t *around = machine_language_top_block(walk);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t scope = around->scope;
    uint8_t placement =
        (around->placement != 0u) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_IN_METHOD : 0u;

    if (active.opcode == MACHINE_LANGUAGE_OPCODE(METHOD))
    {
        machine_language_record_method(walk, &active);
        if (walk->pass != MACHINE_LANGUAGE_PASS_DECODE)
        {
            --walk->pending_count;
            walk->pc = active.end;
            return MACHINE_LANGUAGE_OK;
        }
        if (load->frame_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FRAMES)
            return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);
        placement = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY;
    }
    if ((advanced_configuration_and_power_interface_machine_language_descriptor(active.opcode)->flags &
         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_OPENS_SCOPE) != 0u &&
        active.node != 0u)
        scope = active.node;
    if (walk->block_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BLOCKS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_BLOCKS);
    if (placement == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY)
        load->temporary_bases[load->frame_count++] = (uint16_t) load->temporary_count;
    --walk->pending_count;

    MachineLanguageBlock_t *block = &walk->load->blocks[walk->block_count++];

    block->kind = (uint8_t) kind;
    block->placement = placement;
    block->pending_base = (uint16_t) walk->pending_count;
    block->end = active.end;
    block->scope = scope;
    block->start = active.start;
    if (walk->block_count > walk->load->blocks_peak)
        walk->load->blocks_peak = walk->block_count;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Decodes the next operand of the active operator.
 */
static MachineLanguageStatus_t machine_language_operand(MachineLanguageWalk_t *walk,
                                                        MachineLanguagePendingOperator_t *active)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind =
        (active->opcode == MACHINE_LANGUAGE_CALL) ?
            MACHINE_LANGUAGE_OPERAND(TERM_ARGUMENT) :
            advanced_configuration_and_power_interface_machine_language_operand_kind(
                advanced_configuration_and_power_interface_machine_language_descriptor(active->opcode),
                active->operands_done);
    static const uint8_t fixed_widths[] = {
        [MACHINE_LANGUAGE_OPERAND(BYTE_DATA)] = 1u,
        [MACHINE_LANGUAGE_OPERAND(WORD_DATA)] = 2u,
        [MACHINE_LANGUAGE_OPERAND(DOUBLE_WORD_DATA)] = 4u,
        [MACHINE_LANGUAGE_OPERAND(QUAD_WORD_DATA)] = 8u,
    };
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    walk->fault_opcode = active->opcode;
    ++active->operands_done;
    switch (kind)
    {
    case MACHINE_LANGUAGE_OPERAND(BYTE_DATA):
    case MACHINE_LANGUAGE_OPERAND(WORD_DATA):
    case MACHINE_LANGUAGE_OPERAND(DOUBLE_WORD_DATA):
    case MACHINE_LANGUAGE_OPERAND(QUAD_WORD_DATA):
        if (active->end - walk->pc < fixed_widths[kind])
            return MACHINE_LANGUAGE_STATUS(TRUNCATED);
        walk->pc += fixed_widths[kind];
        if (active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL) && active->operands_done == 3u &&
            walk->external_start == active->start && walk->external_start != 0u)
            machine_language_place_external(walk, walk->bytes[walk->pc - 2u], walk->bytes[walk->pc - 1u]);
        else if (active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL) && active->operands_done == 3u)
        {
            MachineLanguageNode_t *node = machine_language_node(walk, active->node);

            if (node != NULL && node->type == MACHINE_LANGUAGE_NODE(EXTERNAL))
            {
                node->value.auxiliary = walk->bytes[walk->pc - 2u];
                node->value.payload = walk->bytes[walk->pc - 1u] & 0x7u;
            }
        }
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPERAND(ASCII_STRING):
        for (uint32_t offset = walk->pc; offset < active->end; ++offset)
        {
            if (walk->bytes[offset] == 0x00u)
            {
                walk->pc = offset + 1u;
                return MACHINE_LANGUAGE_OK;
            }
        }
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    case MACHINE_LANGUAGE_OPERAND(NAME_STRING):
        status = advanced_configuration_and_power_interface_machine_language_read_name_string(
            walk->bytes, walk->pc, active->end, &name, &after);
        if (status != MACHINE_LANGUAGE_OK)
            return status;
        status = machine_language_use_name(walk, active, &name);
        if (status == MACHINE_LANGUAGE_OK)
            walk->pc = after;
        return status;
    case MACHINE_LANGUAGE_OPERAND(BYTE_LIST): walk->pc = active->end; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPERAND(TERM_LIST):
    case MACHINE_LANGUAGE_OPERAND(FIELD_LIST):
    case MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST): return machine_language_open_list(walk, kind);
    default: return machine_language_term(walk, kind);
    }
}

/**
 * @brief Takes one step of a walk: an operand of the active operator, the end of an operator or of
 *        a construct, or the next item of the construct.
 */
static MachineLanguageStatus_t machine_language_step(MachineLanguageWalk_t *walk)
{
    MachineLanguagePendingOperator_t *active = machine_language_active_operator(walk);
    const MachineLanguageBlock_t *block = machine_language_top_block(walk);

    walk->fault_opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    if (active != NULL)
    {
        if (active->operands_done < active->operand_count)
            return machine_language_operand(walk, active);
        --walk->pending_count;
        return MACHINE_LANGUAGE_OK;
    }
    if (walk->pc == block->end)
    {
        machine_language_close_block(walk);
        return MACHINE_LANGUAGE_OK;
    }
    if (block->kind == MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST))
        return machine_language_package_element(walk);
    if (block->kind == MACHINE_LANGUAGE_OPERAND(FIELD_LIST))
        return machine_language_field_element(walk);
    return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
}

/**
 * @brief Runs one pass over table @p table of a load.
 */
static void machine_language_walk_table(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        uint32_t table, MachineLanguagePass_t pass)
{
    MachineLanguageWalk_t walk = {
        .load = load,
        .bytes = (const uint8_t *) load->tables[table].header,
        .table = table,
        .pc = (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t),
        .pending_count = 0u,
        .block_count = 1u,
        .fault_opcode = MACHINE_LANGUAGE_OPCODE(INVALID),
        .pass = pass,
    };
    MachineLanguageBlock_t *block = &load->blocks[0];

    block->kind = MACHINE_LANGUAGE_OPERAND(NONE);
    block->placement = 0u;
    block->pending_base = 0u;
    block->end = load->tables[table].length;
    block->scope = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
    block->start = walk.pc;
    if (load->blocks_peak == 0u)
        load->blocks_peak = 1u;
    while (walk.block_count != 0u)
    {
        const MachineLanguageStatus_t status = machine_language_step(&walk);

        if (status != MACHINE_LANGUAGE_OK)
        {
            machine_language_record_fault(&walk, status);
            machine_language_recover(&walk, status);
        }
    }
}

bool advanced_configuration_and_power_interface_machine_language_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t max_nodes, uint32_t max_arena_bytes)
{
    memset(load, 0, sizeof(*load));
    load->status = (uint8_t) advanced_configuration_and_power_interface_machine_language_namespace_open(
        &load->space, host, max_nodes, max_arena_bytes);
    return load->status == MACHINE_LANGUAGE_OK;
}

bool advanced_configuration_and_power_interface_machine_language_declare_table(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    if (load->table_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES || header == NULL ||
        header->length < sizeof(*header))
        return false;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[load->table_count];

    memset(table, 0, sizeof(*table));
    table->header = header;
    table->length = header->length;
    table->checksum_valid = advanced_configuration_and_power_interface_checksum_holds(header, header->length);
    load->checksum_bad_loaded += table->checksum_valid ? 0u : 1u;
    load->bytes += header->length - (uint32_t) sizeof(*header);
    if (memcmp(header->signature, "DSDT", sizeof(header->signature)) == 0 && load->integer_bits == 0u)
        load->integer_bits = (header->revision < 2u) ? 32u : 64u;
    machine_language_walk_table(load, load->table_count++, MACHINE_LANGUAGE_PASS_DECLARE);
    return true;
}

void advanced_configuration_and_power_interface_machine_language_decode_tables(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    for (uint32_t table = 0u; table < load->table_count; ++table)
        machine_language_walk_table(load, table, MACHINE_LANGUAGE_PASS_EXTERNALS);
    for (uint32_t table = 0u; table < load->table_count; ++table)
        machine_language_walk_table(load, table, MACHINE_LANGUAGE_PASS_DECODE);
}

void advanced_configuration_and_power_interface_machine_language_load_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        load, advanced_configuration_and_power_interface_find_in_directory(directory, "DSDT"));
    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        if (memcmp(directory->tables[index].signature, "SSDT", sizeof(directory->tables[index].signature)) == 0)
            (void) advanced_configuration_and_power_interface_machine_language_declare_table(
                load, directory->tables[index].header);
    }
    advanced_configuration_and_power_interface_machine_language_decode_tables(load);
}

void advanced_configuration_and_power_interface_machine_language_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    advanced_configuration_and_power_interface_machine_language_namespace_close(&load->space);
}

void advanced_configuration_and_power_interface_machine_language_initialize(void)
{
    if (advanced_configuration_and_power_interface_machine_language_open(
            &machine_language_boot_load, &advanced_configuration_and_power_interface_machine_host,
            MACHINE_LANGUAGE_BOOT_MAX_NODES, MACHINE_LANGUAGE_BOOT_MAX_ARENA_BYTES))
        advanced_configuration_and_power_interface_machine_language_load_directory(
            &machine_language_boot_load, advanced_configuration_and_power_interface_directory());
}

const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *
advanced_configuration_and_power_interface_machine_language(void)
{
    return &machine_language_boot_load;
}
