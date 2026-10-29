#include <kernel/acpi/machine_language.h>

#include <string.h>

#define MACHINE_LANGUAGE_CALL          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
#define MACHINE_LANGUAGE_OK            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_OPERAND(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_##kind
#define MACHINE_LANGUAGE_OPCODE(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define MACHINE_LANGUAGE_NODE(type)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_IF_BYTE       0xA0u /**< The byte of If, which opens a block a following Else depends on. */
#define MACHINE_LANGUAGE_WHILE_BYTE    0xA2u /**< The byte of While, whose block loops. */
#define MACHINE_LANGUAGE_ELSE_SKIPPED  0u    /**< The Else after an If is stepped over: the If ran its body. */
#define MACHINE_LANGUAGE_ELSE_RUN      1u    /**< The Else runs: the If's predicate was false. */
#define MACHINE_LANGUAGE_ELSE_NOT_RUN  2u    /**< The Else is declared, its code not run: the If's predicate failed. */
#define MACHINE_LANGUAGE_BLOCK_NOT_RUN ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_NOT_RUN

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t MachineLanguagePendingOperator_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t MachineLanguageBlock_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t MachineLanguageNameString_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;

/**
 * @brief Which pass a walk is, in the order they run: declarations, then the Externals no table
 *        declared a path for, then the decoding of every term.
 */
typedef enum MachineLanguagePass {
    MACHINE_LANGUAGE_PASS_DECLARE,   /**< Declares named objects; skips method bodies. */
    MACHINE_LANGUAGE_PASS_EXTERNALS, /**< Places the Externals whose path the declaration pass could not find, once
                                        every table is declared; skips method bodies, declares nothing else, records no
                                          fault. */
    MACHINE_LANGUAGE_PASS_DECODE,    /**< Decodes and counts every term; declares only the temporary nodes of method
                                          bodies. */
} MachineLanguagePass_t;

/**
 * @brief The field list being read: what declared it, where its units are, and the bit the next
 *        unit starts at.
 */
typedef struct MachineLanguageFieldList {
    uint32_t opcode;       /**< Field, IndexField or BankField, as a descriptor index. */
    uint32_t region;       /**< The region's node, or the index field's; 0 when it was not found. */
    uint32_t data_or_bank; /**< The data field's or the bank field's node, else 0. */
    uint32_t bank_value;   /**< A BankField's BankValue. */
    uint32_t bit;          /**< The bit the next unit starts at. */
    uint8_t access;        /**< The AccessType in force. */
    uint8_t update;        /**< The UpdateRule. */
    uint8_t lock;          /**< The LockRule. */
    bool builds;           /**< Whether its units get objects: not when a name it needs is missing, nor when its
                                BankValue waits for the interpreter. */
    bool deferred;         /**< Whether its units wait for the interpreter, counted each. */
} MachineLanguageFieldList_t;

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
    uint32_t last_created;   /**< The node the last declaration created, 0 when it created none. */
    uint32_t external_start; /**< The External the Externals pass places, by its opcode's offset, or 0. */
    MachineLanguageNameString_t external_name; /**< That External's NameString. */
    MachineLanguageFieldList_t field;          /**< The field list being read. */
    uint32_t else_offset; /**< Where an Else would follow the last If of table-level code the declaration pass ran. */
    uint8_t else_rule;    /**< What that Else does: a MACHINE_LANGUAGE_ELSE_* rule. */
    bool not_run_next;    /**< Whether the next block the walk opens is declared without running its code. */
    bool undeclared_next; /**< Whether the next TermList the decoding pass opens belongs to a construct the
                               declaration pass did not declare. */
    uint32_t undeclared_from; /**< The depth of the outermost such TermList open, or 0: faults inside it are
                                   counted apart. */
    bool duplicate_next;      /**< Whether the next TermList the walk opens is the body of a duplicate declaration,
                                   which no pass reads (§5.1): one the declaration pass found the name of taken, or,
                                   past it, one at table level whose name another construct declared. */
} MachineLanguageWalk_t;

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
 * @brief Whether the walk is the declaration pass at table level, where code runs as it is met.
 */
static bool machine_language_runs_code(MachineLanguageWalk_t *walk)
{
    return walk->pass == MACHINE_LANGUAGE_PASS_DECLARE && !walk->load->declares_every_branch &&
           machine_language_top_block(walk)->placement == 0u;
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
 * @brief Tells the observer, when there is one, of an opcode or a call the decoding pass decoded, and
 *        of the method whose body holds it.
 */
static void machine_language_observe(MachineLanguageWalk_t *walk, uint32_t opcode, uint32_t callee)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;

    if (load->observer != NULL)
        load->observer(load->observer_context,
                       (load->frame_count != 0u) ? load->frame_methods[load->frame_count - 1u] : 0u, opcode, callee);
}

/**
 * @brief Counts an opcode in the decoding pass.
 */
static void machine_language_count_opcode(MachineLanguageWalk_t *walk, uint32_t opcode)
{
    if (walk->pass != MACHINE_LANGUAGE_PASS_DECODE)
        return;
    ++walk->load->opcodes[opcode];
    ++walk->load->tables[walk->table].terms;
    machine_language_observe(walk, opcode, 0u);
}

/**
 * @brief Counts a fault of the declaration pass or of the Externals pass.
 */
static void machine_language_count_load_error(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                              MachineLanguageStatus_t status)
{
    if (load->load_errors++ == 0u)
        load->first_load_error = (uint8_t) status;
}

/**
 * @brief Records a fault at the walk's pc: in load_errors for the declaration pass, in its table and
 *        in decode_errors for the decoding pass, in undeclared_faults for the decoding pass inside a
 *        construct the declaration pass did not declare; the Externals pass counts its own faults
 *        where it meets them.
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

    if (walk->undeclared_from != 0u)
    {
        ++load->undeclared_faults;
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
    if (walk->undeclared_from == walk->block_count)
        walk->undeclared_from = 0u;
    --walk->block_count;
}

/**
 * @brief Drops the marks a failed predicate, a Scope the walk could not find or a duplicate declaration
 *        left for the TermList to come, once a recovery leaves the construct that TermList belongs to.
 */
static void machine_language_forget_marks(MachineLanguageWalk_t *walk)
{
    walk->not_run_next = false;
    walk->undeclared_next = false;
    walk->duplicate_next = false;
}

/**
 * @brief Goes on after a fault: past the innermost construct with a PkgLength around it, else
 *        past the rest of the table, which is also where a PkgLength crossing its parent and an
 *        exhausted arena lead (§6.2). Stepping over data, a Buffer or a Package, keeps the marks:
 *        the construct around it still opens its TermList.
 */
static void machine_language_recover(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status)
{
    const MachineLanguageBlock_t *block = machine_language_top_block(walk);

    if (status != MACHINE_LANGUAGE_STATUS(PACKAGE_LENGTH_OUT_OF_PARENT) &&
        status != MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY))
    {
        for (uint32_t index = walk->pending_count; index > block->pending_base; --index)
        {
            const MachineLanguagePendingOperator_t *pending = &walk->load->pending[index - 1u];
            const uint8_t flags =
                (pending->opcode == MACHINE_LANGUAGE_CALL) ?
                    0u :
                    advanced_configuration_and_power_interface_machine_language_descriptor(pending->opcode)->flags;

            if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH) != 0u)
            {
                if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT) == 0u)
                    machine_language_forget_marks(walk);
                walk->pc = pending->end;
                walk->pending_count = index - 1u;
                return;
            }
        }
        machine_language_forget_marks(walk);
        if (walk->block_count > 1u)
        {
            walk->pc = block->end;
            walk->pending_count = block->pending_base;
            machine_language_close_block(walk);
            return;
        }
    }
    machine_language_forget_marks(walk);
    walk->pending_count = 0u;
    while (walk->block_count != 0u)
        machine_language_close_block(walk);
}

/**
 * @brief Counts a declaration made, and, in a branch whose predicate failed, made without running.
 */
static void machine_language_count_declaration(MachineLanguageWalk_t *walk)
{
    ++walk->load->declarations;
    if (machine_language_top_block(walk)->placement == MACHINE_LANGUAGE_BLOCK_NOT_RUN)
        ++walk->load->declared_not_run;
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

    walk->last_created = 0u;
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
            machine_language_count_declaration(walk);
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
        machine_language_count_declaration(walk);
    walk->last_created = (status == MACHINE_LANGUAGE_OK) ? *out_index : 0u;
    return status;
}

/**
 * @brief The node a scope-opening declaration at table level names past the declaration pass, which
 *        created it, or 0 when it cannot be found: the decoding pass still decodes the body of such a
 *        construct, and the Externals pass skips it.
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
    return index;
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

    walk->last_created = 0u;
    *out_index = machine_language_top_block(walk)->scope;
    if (advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &load->space, *out_index, walk->bytes, name, &parent, &segment) != MACHINE_LANGUAGE_OK)
        return MACHINE_LANGUAGE_OK;

    const uint32_t existing =
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent, segment);

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
 * @brief Notes what the declaration pass did with the object a scope-opening declaration names: the body
 *        of a duplicate is marked to be skipped, and the object it names marked as having one; a Device,
 *        ThermalZone, Processor or PowerResource it created keeps the offset of its opcode, which with its
 *        owner tells the later passes which construct declared it (a Method's recorded body does the same).
 */
static void machine_language_note_scope_declaration(MachineLanguageWalk_t *walk,
                                                    const MachineLanguagePendingOperator_t *active)
{
    if (walk->last_created != active->node)
    {
        machine_language_node(walk, active->node)->flags |=
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DUPLICATED;
        walk->duplicate_next = true;
        return;
    }
    if (active->opcode != MACHINE_LANGUAGE_OPCODE(METHOD))
        machine_language_node(walk, active->node)->value.handle = active->start;
}

/**
 * @brief Whether a scope-opening declaration met past the declaration pass is a duplicate's, whose body no
 *        pass reads: the object it names had a duplicate refused, and another construct declared it, this
 *        one declaring a Method whose recorded body ends where the construct ends in its table, or another
 *        object its table created with the opcode at the construct's start. A load that refused no
 *        duplicate looks nothing up.
 */
static bool machine_language_declared_elsewhere(MachineLanguageWalk_t *walk,
                                                const MachineLanguagePendingOperator_t *active)
{
    if (walk->load->duplicates == 0u)
        return false;

    const MachineLanguageNode_t *node = machine_language_node(walk, active->node);

    if (node == NULL ||
        (node->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DUPLICATED) == 0u)
        return false;
    if (node->type != machine_language_declared_type(active->opcode))
        return true;
    if (node->type == MACHINE_LANGUAGE_NODE(METHOD))
        return node->value.handle != walk->table + 1u ||
               (uint32_t) node->value.payload + (uint32_t) (node->value.payload >> 32u) != active->end;
    return node->owner != walk->table + 1u || node->value.handle != active->start;
}

/**
 * @brief The node an External's last segment belongs to, in the Externals pass: every missing segment
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
 * @brief Places, in the Externals pass, the External whose NameString the walk kept and whose ObjectType
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
    if (status == MACHINE_LANGUAGE_OK &&
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent,
                                                                                         segment) != 0u)
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
 * @brief What an External's NameString means in the Externals pass: nothing when a node holds the name;
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
 *        source then its new name, or the name a declaration creates. The declaration pass creates
 *        permanent nodes, counts and skips alone an Alias whose source does not exist, and leaves an
 *        External it cannot place to the Externals pass, which counts it; the decoding pass creates
 *        temporary nodes in method bodies. Past the declaration pass, at
 *        table level, a construct only finds the scope it opens: the decoding pass decodes the body of
 *        one it cannot find in the scope the walk is in, the Externals pass skips it, and both skip the
 *        body of one another construct declared.
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
        if (status == MACHINE_LANGUAGE_OK ||
            (walk->pass != MACHINE_LANGUAGE_PASS_DECODE && active->opcode == MACHINE_LANGUAGE_OPCODE(SCOPE)))
            return status;
        active->node = (active->opcode == MACHINE_LANGUAGE_OPCODE(SCOPE)) ? block->scope : 0u;
        walk->undeclared_next = active->opcode == MACHINE_LANGUAGE_OPCODE(SCOPE);
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
        {
            const uint32_t found = machine_language_declared_node(walk, name);

            if (walk->pass == MACHINE_LANGUAGE_PASS_EXTERNALS && found == 0u)
                return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
            active->node = (found != 0u) ? found : block->scope;
            walk->undeclared_next = found == 0u;
            walk->duplicate_next = found != 0u && machine_language_declared_elsewhere(walk, active);
        }
        else if (walk->pass == MACHINE_LANGUAGE_PASS_EXTERNALS && active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL))
            machine_language_keep_external(walk, active, name);
        return MACHINE_LANGUAGE_OK;
    }
    if (type == MACHINE_LANGUAGE_NODE(ALIAS) && target == 0u)
    {
        if (declaring)
            machine_language_record_fault(walk, MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND));
        return MACHINE_LANGUAGE_OK;
    }
    status = declaring ? machine_language_declare(walk, name, type, &active->node) :
                         machine_language_declare_temporary(walk, name, type, &active->node);
    if (status == MACHINE_LANGUAGE_OK && declaring && opens_scope)
        machine_language_note_scope_declaration(walk, active);
    if (status != MACHINE_LANGUAGE_OK && declaring && active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL))
    {
        active->node = 0u;
        ++walk->load->externals_unplaced;
        return MACHINE_LANGUAGE_OK;
    }
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
    if (counting)
        machine_language_observe(walk, MACHINE_LANGUAGE_CALL, index);
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
 * @brief Where the walk reads a data object from: its table, the scope it is in, the integer width.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t
machine_language_source(MachineLanguageWalk_t *walk)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = {
        .bytes = walk->bytes,
        .scope = machine_language_top_block(walk)->scope,
        .table = (uint16_t) (walk->table + 1u),
        .integer_bits = (walk->load->integer_bits != 0u) ? walk->load->integer_bits : 64u,
    };

    return source;
}

/**
 * @brief Whether the declaration pass, at table level, has just created node @p index: only then does it
 *        build the node's object, so that a duplicate keeps the first one's.
 */
static bool machine_language_builds(MachineLanguageWalk_t *walk, uint32_t index)
{
    const uint8_t placement = machine_language_top_block(walk)->placement;

    return walk->pass == MACHINE_LANGUAGE_PASS_DECLARE &&
           (placement == 0u || placement == MACHINE_LANGUAGE_BLOCK_NOT_RUN) && index != 0u &&
           index == walk->last_created;
}

/**
 * @brief What a builder does with a status: an exhausted arena stops the table; a malformed operand
 *        is left to the walk, which meets it on the same bytes; any other fault is counted here.
 */
static MachineLanguageStatus_t machine_language_build_fault(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status)
{
    if (status == MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY))
        return status;
    if (status != MACHINE_LANGUAGE_OK && status != MACHINE_LANGUAGE_STATUS(TRUNCATED) &&
        status != MACHINE_LANGUAGE_STATUS(INVALID_OPCODE) &&
        status != MACHINE_LANGUAGE_STATUS(PACKAGE_LENGTH_OUT_OF_PARENT))
        machine_language_count_load_error(walk->load, status);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Builds a Name's object from its data object, which starts at the walk's pc; one that needs
 *        the interpreter is counted and left Uninitialized.
 */
static MachineLanguageStatus_t machine_language_build_name(MachineLanguageWalk_t *walk,
                                                           const MachineLanguagePendingOperator_t *active)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    MachineLanguageNode_t *node = machine_language_node(walk, active->node);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value;
    bool constant = false;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_data(
        &load->heap, &load->space, &source, walk->pc, active->end, &value, &constant);

    if (status != MACHINE_LANGUAGE_OK)
        return machine_language_build_fault(walk, status);
    if (!constant)
        ++load->values_deferred;
    else
    {
        node->value = value;
        node->type = advanced_configuration_and_power_interface_machine_language_node_type_of_value(value.kind);
    }
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Reads an integer operand of a declaration at @p start: a constant, or, where table-level code
 *        runs, what the interpreter makes of it; @p out_known is false when neither gave one.
 */
static MachineLanguageStatus_t machine_language_operand_integer(
    MachineLanguageWalk_t *walk, const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t *source,
    uint32_t start, uint32_t end, uint64_t *out_value, uint32_t *out_after, bool *out_known)
{
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_integer(
        &walk->load->space, source, start, end, out_value, out_after, out_known);

    if (status != MACHINE_LANGUAGE_OK || *out_known || !machine_language_runs_code(walk))
        return status;
    *out_known =
        advanced_configuration_and_power_interface_machine_language_run_table_term(
            walk->load, walk->table, source->scope, start, end, true, out_value, out_after) == MACHINE_LANGUAGE_OK;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Builds an OperationRegion's object once its space is read: its offset and length start at
 *        the walk's pc, constants, or run where table-level code runs; they wait for the interpreter
 *        otherwise.
 */
static MachineLanguageStatus_t machine_language_build_region(MachineLanguageWalk_t *walk,
                                                             const MachineLanguagePendingOperator_t *active)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    MachineLanguageNode_t *node = machine_language_node(walk, active->node);
    uint64_t offset = 0u;
    uint64_t length = 0u;
    uint32_t after = 0u;
    uint32_t handle = 0u;
    bool constant = false;
    MachineLanguageStatus_t status =
        machine_language_operand_integer(walk, &source, walk->pc, active->end, &offset, &after, &constant);

    if (status == MACHINE_LANGUAGE_OK && constant)
        status = machine_language_operand_integer(walk, &source, after, active->end, &length, &after, &constant);
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_object_allocate(
            &load->heap, &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION,
            (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t), &handle);
    if (status != MACHINE_LANGUAGE_OK)
        return machine_language_build_fault(walk, status);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t
             *) (advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle) + 1);

    region->space = walk->bytes[walk->pc - 1u];
    region->state = constant ? 1u : 0u;
    region->offset = constant ? offset : 0u;
    region->length = constant ? length : 0u;
    load->values_deferred += constant ? 0u : 1u;
    node->value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION;
    node->value.handle = handle;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Builds the buffer field a Create*Field just declared: the Buffer a name holds, which the
 *        field holds a reference on, and its bit offset and length, which must be constants that fit
 *        32 bits; one past them is counted as index_out_of_range and left without an object. Whether
 *        it fits its Buffer is checked at access.
 */
static void machine_language_build_buffer_field(MachineLanguageWalk_t *walk,
                                                const MachineLanguagePendingOperator_t *active)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    const bool any_length = active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_FIELD);
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    uint32_t buffer = 0u;
    uint64_t index = 0u;
    uint64_t length = 0u;
    bool constant = false;

    if (advanced_configuration_and_power_interface_machine_language_read_name_string(
            walk->bytes, active->start + (any_length ? 2u : 1u), active->end, &name, &after) == MACHINE_LANGUAGE_OK &&
        advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, source.scope, walk->bytes, &name, &buffer) == MACHINE_LANGUAGE_OK &&
        machine_language_node(walk, buffer)->type == MACHINE_LANGUAGE_NODE(BUFFER) &&
        advanced_configuration_and_power_interface_machine_language_evaluate_integer(
            &load->space, &source, after, active->end, &index, &after, &constant) == MACHINE_LANGUAGE_OK &&
        constant && any_length)
        constant = advanced_configuration_and_power_interface_machine_language_evaluate_integer(
                       &load->space, &source, after, active->end, &length, &after, &constant) == MACHINE_LANGUAGE_OK &&
                   constant;
    if (!constant)
    {
        ++load->values_deferred;
        return;
    }

    static const uint8_t widths[] = {[0] = 1u, [1] = 1u, [2] = 2u, [3] = 4u, [4] = 8u};
    const uint32_t shape = (active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_BIT_FIELD))         ? 0u :
                           (active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_BYTE_FIELD))        ? 1u :
                           (active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_WORD_FIELD))        ? 2u :
                           (active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_DOUBLE_WORD_FIELD)) ? 3u :
                           (active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_QUAD_WORD_FIELD))   ? 4u :
                                                                                                   0u;
    const uint64_t bit_offset = (shape == 0u) ? index : index * 8u;
    const uint64_t bit_length = any_length ? length : ((shape == 0u) ? 1u : 8u * widths[shape]);
    MachineLanguageNode_t *node = machine_language_node(walk, active->node);

    if (index > 0xFFFFFFFFull || bit_offset > 0xFFFFFFFFull || bit_length > 0xFFFFFFFFull)
    {
        machine_language_count_load_error(load, MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE));
        return;
    }

    node->value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD;
    node->value.flags =
        any_length ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BUFFER_FIELD_READS_AS_BUFFER : 0u;
    node->value.auxiliary = widths[shape];
    node->value.handle = machine_language_node(walk, buffer)->value.handle;
    node->value.payload = (bit_offset & 0xFFFFFFFFull) | ((bit_length & 0xFFFFFFFFull) << 32u);
    advanced_configuration_and_power_interface_machine_language_value_retain(&load->heap, &node->value);
}

/**
 * @brief Starts the field list of a Field, an IndexField or a BankField: reads again its names, its
 *        BankValue and its FieldFlags, which the walk has just passed. A name that resolves to nothing
 *        is counted once, and the units get no object; a BankValue that is neither a constant nor run
 *        as table-level code makes each unit wait for the interpreter.
 */
static void machine_language_begin_field_list(MachineLanguageWalk_t *walk,
                                              const MachineLanguagePendingOperator_t *active)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    MachineLanguageFieldList_t *field = &walk->field;
    const uint8_t flags = walk->bytes[walk->pc - 1u];
    MachineLanguageNameString_t name;
    uint32_t length = 0u;
    uint32_t after = 0u;
    uint64_t bank = 0u;
    bool constant = false;

    memset(field, 0, sizeof(*field));
    field->builds = true;
    field->opcode = active->opcode;
    field->access = flags & 0x0Fu;
    field->lock = (flags >> 4u) & 0x1u;
    field->update = (flags >> 5u) & 0x3u;
    if (walk->pass != MACHINE_LANGUAGE_PASS_DECLARE ||
        advanced_configuration_and_power_interface_machine_language_read_package_length(
            walk->bytes, active->start + 2u, active->end, &length, &after) != MACHINE_LANGUAGE_OK ||
        advanced_configuration_and_power_interface_machine_language_read_name_string(
            walk->bytes, after, active->end, &name, &after) != MACHINE_LANGUAGE_OK)
        return;
    if (advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, source.scope, walk->bytes, &name, &field->region) != MACHINE_LANGUAGE_OK)
        field->builds = false;
    if (active->opcode != MACHINE_LANGUAGE_OPCODE(FIELD) &&
        (advanced_configuration_and_power_interface_machine_language_read_name_string(
             walk->bytes, after, active->end, &name, &after) != MACHINE_LANGUAGE_OK ||
         advanced_configuration_and_power_interface_machine_language_namespace_resolve(
             &load->space, source.scope, walk->bytes, &name, &field->data_or_bank) != MACHINE_LANGUAGE_OK))
        field->builds = false;
    if (!field->builds)
    {
        machine_language_count_load_error(load, MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND));
        return;
    }
    if (active->opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD) &&
        (machine_language_operand_integer(walk, &source, after, active->end, &bank, &after, &constant) !=
             MACHINE_LANGUAGE_OK ||
         !constant))
    {
        field->builds = false;
        field->deferred = true;
        return;
    }
    field->bank_value = (uint32_t) bank;
}

/**
 * @brief Builds the unit a field list just declared, at the bit the list has reached.
 */
static MachineLanguageStatus_t machine_language_build_field_unit(MachineLanguageWalk_t *walk, uint32_t index,
                                                                 uint32_t bit_length)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const MachineLanguageFieldList_t *field = &walk->field;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t unit = {
        .kind = (field->opcode == MACHINE_LANGUAGE_OPCODE(INDEX_FIELD)) ?
                    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_INDEX :
                (field->opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD)) ?
                    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_BANK :
                    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_NORMAL,
        .access = field->access,
        .update = field->update,
        .lock = field->lock,
        .region = field->region,
        .data_or_bank = field->data_or_bank,
        .bank_value = field->bank_value,
        .bit_offset = field->bit,
        .bit_length = bit_length,
    };

    load->values_deferred += field->deferred ? 1u : 0u;
    if (!field->builds)
        return MACHINE_LANGUAGE_OK;
    return machine_language_build_fault(
        walk, advanced_configuration_and_power_interface_machine_language_field_unit_build(load, index, &unit));
}

/**
 * @brief Builds what a Processor or a PowerResource holds in its node, from the fixed fields the
 *        walk has just passed: a Processor's ID, block address and block length, a PowerResource's
 *        system level and resource order.
 */
static void machine_language_build_scope_object(MachineLanguageWalk_t *walk,
                                                const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageNode_t *node = machine_language_node(walk, active->node);
    const uint8_t *fields = walk->bytes + walk->pc;

    if (active->opcode == MACHINE_LANGUAGE_OPCODE(PROCESSOR))
    {
        const uint32_t block = (uint32_t) fields[-5] | ((uint32_t) fields[-4] << 8u) | ((uint32_t) fields[-3] << 16u) |
                               ((uint32_t) fields[-2] << 24u);

        node->value.payload =
            block | ((uint64_t) fields[-1] << 32u) |
            ((uint64_t) fields[-6]
             << ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PROCESSOR_IDENTIFIER_SHIFT);
    }
    else if (active->opcode == MACHINE_LANGUAGE_OPCODE(POWER_RESOURCE))
    {
        node->value.auxiliary = fields[-3];
        node->value.payload = (uint64_t) fields[-2] | ((uint64_t) fields[-1] << 8u);
    }
}

/**
 * @brief Whether a statement is code the interpreter runs: one that is neither a declaration, data,
 *        a local, an argument nor Noop. A name there is a call or a reference to run.
 */
static bool machine_language_is_code(bool name, uint32_t opcode)
{
    const uint8_t flags = advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags;
    const bool declares = machine_language_declared_type(opcode) != 0xFFu || opcode == MACHINE_LANGUAGE_OPCODE(SCOPE) ||
                          opcode == MACHINE_LANGUAGE_OPCODE(ALIAS) || opcode == MACHINE_LANGUAGE_OPCODE(FIELD) ||
                          opcode == MACHINE_LANGUAGE_OPCODE(INDEX_FIELD) ||
                          opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD);
    const bool inert = (flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT) != 0u ||
                       opcode == MACHINE_LANGUAGE_OPCODE(NOOP) ||
                       (opcode >= MACHINE_LANGUAGE_OPCODE(LOCAL0) && opcode <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6));

    return name || (!declares && !inert);
}

/**
 * @brief Counts, in the decoding pass, a statement at table level that the interpreter runs.
 */
static void machine_language_count_table_level_code(MachineLanguageWalk_t *walk, bool name, uint32_t opcode)
{
    if (walk->pass == MACHINE_LANGUAGE_PASS_DECODE && machine_language_top_block(walk)->placement == 0u &&
        machine_language_is_code(name, opcode))
        ++walk->load->table_level_code;
}

/**
 * @brief Runs a statement of table-level code in the interpreter: past it when it succeeds; a failed
 *        one, counted by the interpreter, is then decoded like any other, to step over it.
 */
static bool machine_language_run_statement(MachineLanguageWalk_t *walk, uint32_t start)
{
    uint32_t after = 0u;

    if (advanced_configuration_and_power_interface_machine_language_run_table_term(
            walk->load, walk->table, machine_language_top_block(walk)->scope, start, machine_language_limit(walk),
            false, NULL, &after) != MACHINE_LANGUAGE_OK)
        return false;
    walk->pc = after;
    return true;
}

/**
 * @brief Steps over a construct with a PkgLength whose opcode ends at @p after.
 */
static MachineLanguageStatus_t machine_language_skip_construct(MachineLanguageWalk_t *walk, uint32_t after)
{
    uint32_t end = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_package_end(
        walk->bytes, after, machine_language_limit(walk), &end, &after);

    if (status == MACHINE_LANGUAGE_OK)
        walk->pc = end;
    return status;
}

/**
 * @brief Counts a back-edge of table-level code against the table's budget; past it, counts the
 *        failure.
 *
 * @return Whether the loop may go back to its predicate.
 */
static bool machine_language_take_table_back_edge(MachineLanguageWalk_t *walk)
{
    if (advanced_configuration_and_power_interface_machine_language_count_back_edge(walk->load) == MACHINE_LANGUAGE_OK)
        return true;
    advanced_configuration_and_power_interface_machine_language_count_failure(
        walk->load, MACHINE_LANGUAGE_STATUS(LIMIT_LOOP_ITERATIONS));
    return false;
}

/**
 * @brief Runs a Break or a Continue of table-level code: leaves the innermost While of table-level
 *        code around it, past its end or back to its predicate, a back-edge counted against the
 *        table's budget, past its end too once the budget is spent; one outside any While is skipped.
 */
static void machine_language_leave_table_loop(MachineLanguageWalk_t *walk, bool again)
{
    for (uint32_t index = walk->block_count; index > 1u; --index)
    {
        const MachineLanguageBlock_t block = walk->load->blocks[index - 1u];

        if (block.placement != 0u)
            return;
        if (block.kind != MACHINE_LANGUAGE_OPERAND(TERM_LIST) ||
            walk->bytes[block.start] != MACHINE_LANGUAGE_WHILE_BYTE)
            continue;
        while (walk->block_count >= index)
            machine_language_close_block(walk);
        walk->pending_count = block.pending_base;
        walk->pc = (again && machine_language_take_table_back_edge(walk)) ? block.start : block.end;
        return;
    }
}

/**
 * @brief What the end of a construct of table-level code does in the declaration pass: a run If steps over
 *        the Else after it, an If whose predicate failed declares that Else without running it, a
 *        While goes back to its predicate until it has looped too often.
 */
static void machine_language_after_block(MachineLanguageWalk_t *walk, const MachineLanguageBlock_t *closed)
{
    if (walk->block_count == 0u || !machine_language_runs_code(walk) ||
        closed->kind != MACHINE_LANGUAGE_OPERAND(TERM_LIST))
        return;
    if (walk->bytes[closed->start] == MACHINE_LANGUAGE_IF_BYTE)
    {
        walk->else_offset = closed->end;
        walk->else_rule = (closed->placement == MACHINE_LANGUAGE_BLOCK_NOT_RUN) ? MACHINE_LANGUAGE_ELSE_NOT_RUN :
                                                                                  MACHINE_LANGUAGE_ELSE_SKIPPED;
        return;
    }
    if (closed->placement != 0u)
        return;
    if (walk->bytes[closed->start] != MACHINE_LANGUAGE_WHILE_BYTE)
        return;
    if (machine_language_take_table_back_edge(walk))
        walk->pc = closed->start;
}

/**
 * @brief Runs the predicate of an If or a While of table-level code in the interpreter: a true one
 *        lets the walk open the body; a false one steps over it, and an If's lets the Else after it
 *        run. A predicate that fails is decoded instead, and its body declared without running its
 *        code, the Else after an If too, so that the namespace loses nothing the interpreter cannot
 *        run yet.
 *
 * @return false when the predicate failed and the walk should decode it.
 */
static bool machine_language_run_predicate(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *active)
{
    uint64_t truth = 0u;
    uint32_t after = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_run_table_term(
        walk->load, walk->table, machine_language_top_block(walk)->scope, walk->pc, active->end, true, &truth, &after);

    if (status != MACHINE_LANGUAGE_OK)
    {
        walk->not_run_next = true;
        return false;
    }
    if (truth != 0u)
    {
        walk->pc = after;
        return true;
    }
    walk->else_offset = active->end;
    walk->else_rule =
        (active->opcode == MACHINE_LANGUAGE_OPCODE(IF)) ? MACHINE_LANGUAGE_ELSE_RUN : MACHINE_LANGUAGE_ELSE_SKIPPED;
    walk->pc = active->end;
    --walk->pending_count;
    return true;
}

/**
 * @brief What a statement of table-level code means in the declaration pass, before it is decoded: an Else
 *        runs only after a false If; Break and Continue leave a While; an If or a While is decoded,
 *        its predicate run when the walk reaches it; any other code runs in the interpreter.
 *
 * @return COUNT when the walk should decode the statement; OK or a fault when it was dealt with.
 */
static MachineLanguageStatus_t machine_language_table_level_control(MachineLanguageWalk_t *walk, uint32_t opcode,
                                                                    uint32_t start, uint32_t after)
{
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(ELSE):
        if (start != walk->else_offset || walk->else_rule == MACHINE_LANGUAGE_ELSE_SKIPPED)
            return machine_language_skip_construct(walk, after);
        walk->not_run_next = walk->else_rule == MACHINE_LANGUAGE_ELSE_NOT_RUN;
        return MACHINE_LANGUAGE_STATUS(COUNT);
    case MACHINE_LANGUAGE_OPCODE(BREAK):
    case MACHINE_LANGUAGE_OPCODE(CONTINUE):
        walk->pc = after;
        machine_language_leave_table_loop(walk, opcode == MACHINE_LANGUAGE_OPCODE(CONTINUE));
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPCODE(IF):
    case MACHINE_LANGUAGE_OPCODE(WHILE): return MACHINE_LANGUAGE_STATUS(COUNT);
    default:
        return (machine_language_is_code(false, opcode) && machine_language_run_statement(walk, start)) ?
                   MACHINE_LANGUAGE_OK :
                   MACHINE_LANGUAGE_STATUS(COUNT);
    }
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
    {
        if (kind == MACHINE_LANGUAGE_OPERAND(NONE))
            machine_language_count_table_level_code(walk, true, MACHINE_LANGUAGE_OPCODE(INVALID));
        if (kind == MACHINE_LANGUAGE_OPERAND(NONE) && machine_language_runs_code(walk) &&
            machine_language_run_statement(walk, start))
            return MACHINE_LANGUAGE_OK;
        return machine_language_name_term(walk, kind != MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME));
    }

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        walk->bytes, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    walk->fault_opcode = opcode;
    if (kind == MACHINE_LANGUAGE_OPERAND(NONE))
        machine_language_count_table_level_code(walk, false, opcode);
    if (kind == MACHINE_LANGUAGE_OPERAND(NONE) && machine_language_runs_code(walk))
    {
        const MachineLanguageStatus_t control = machine_language_table_level_control(walk, opcode, start, after);

        if (control != MACHINE_LANGUAGE_STATUS(COUNT))
            return control;
    }
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
 * @brief Moves the field list past @p bit_length bits; in the declaration pass, a list that passes 2^32
 *        bits, which a unit's offset cannot hold, is refused as index_out_of_range.
 */
static MachineLanguageStatus_t machine_language_advance_field(MachineLanguageWalk_t *walk, uint32_t bit_length)
{
    if (walk->pass == MACHINE_LANGUAGE_PASS_DECLARE && bit_length > 0xFFFFFFFFu - walk->field.bit)
        return MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
    walk->field.bit += bit_length;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Decodes one element of a FieldList: a reserved field, an AccessAs, a Connection to a name
 *        or a buffer, an ExtendedAccessAs, or a named field unit, which the declaration pass declares.
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
        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_advance_field(walk, bit_length);
        break;
    case 0x01u:
    case 0x03u:
        after = start + ((lead == 0x01u) ? 3u : 4u);
        if (after > limit)
            status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
        else
            walk->field.access = walk->bytes[start + 1u] & 0x0Fu;
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
        if (status == MACHINE_LANGUAGE_OK && walk->pass == MACHINE_LANGUAGE_PASS_DECLARE &&
            bit_length > 0xFFFFFFFFu - walk->field.bit)
            status = MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
        if (status == MACHINE_LANGUAGE_OK && walk->pass == MACHINE_LANGUAGE_PASS_DECLARE)
        {
            status = machine_language_declare(walk, &name, MACHINE_LANGUAGE_NODE(FIELD_UNIT), &index);
            if (status == MACHINE_LANGUAGE_OK && machine_language_builds(walk, index))
                status = machine_language_build_field_unit(walk, index, bit_length);
        }
        else if (status == MACHINE_LANGUAGE_OK && machine_language_top_block(walk)->placement != 0u)
            status = machine_language_declare_temporary(walk, &name, MACHINE_LANGUAGE_NODE(FIELD_UNIT), &index);
        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_advance_field(walk, bit_length);
        break;
    }
    if (status == MACHINE_LANGUAGE_OK)
        walk->pc = after;
    return status;
}

/**
 * @brief Records a method's flags, table and body in its node, unless the node was declared before:
 *        in the declaration pass for a permanent method, in the decoding pass for one a method body
 *        declares.
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
 *        but in the decoding pass, and the body of a duplicate declaration in every pass.
 */
static MachineLanguageStatus_t
machine_language_open_list(MachineLanguageWalk_t *walk,
                           AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    const MachineLanguagePendingOperator_t active = walk->load->pending[walk->pending_count - 1u];
    const MachineLanguageBlock_t *around = machine_language_top_block(walk);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t scope = around->scope;
    const bool not_run = around->placement == MACHINE_LANGUAGE_BLOCK_NOT_RUN ||
                         (walk->not_run_next && kind == MACHINE_LANGUAGE_OPERAND(TERM_LIST));
    uint8_t placement = not_run ? MACHINE_LANGUAGE_BLOCK_NOT_RUN :
                        (around->placement != 0u) ?
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_IN_METHOD :
                                  0u;

    if (kind == MACHINE_LANGUAGE_OPERAND(TERM_LIST) && walk->duplicate_next)
    {
        machine_language_forget_marks(walk);
        --walk->pending_count;
        walk->pc = active.end;
        return MACHINE_LANGUAGE_OK;
    }
    walk->not_run_next = walk->not_run_next && kind != MACHINE_LANGUAGE_OPERAND(TERM_LIST);

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
    if (kind == MACHINE_LANGUAGE_OPERAND(FIELD_LIST))
        machine_language_begin_field_list(walk, &active);
    if (machine_language_builds(walk, active.node))
        machine_language_build_scope_object(walk, &active);
    if (placement == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY)
    {
        load->frame_methods[load->frame_count] = active.node;
        load->temporary_bases[load->frame_count++] = (uint16_t) load->temporary_count;
    }
    --walk->pending_count;

    MachineLanguageBlock_t *block = &walk->load->blocks[walk->block_count++];

    if (walk->undeclared_next && kind == MACHINE_LANGUAGE_OPERAND(TERM_LIST) && walk->undeclared_from == 0u)
        walk->undeclared_from = walk->block_count;
    walk->undeclared_next = walk->undeclared_next && kind != MACHINE_LANGUAGE_OPERAND(TERM_LIST);
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
        if (active->opcode == MACHINE_LANGUAGE_OPCODE(MUTEX) && machine_language_builds(walk, active->node))
            machine_language_node(walk, active->node)->value.auxiliary = walk->bytes[walk->pc - 1u] & 0x0Fu;
        if (active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL) && active->operands_done == 3u &&
            walk->external_start == active->start && walk->external_start != 0u)
            machine_language_place_external(walk, walk->bytes[walk->pc - 2u], walk->bytes[walk->pc - 1u]);
        else if (active->opcode == MACHINE_LANGUAGE_OPCODE(EXTERNAL) && active->operands_done == 3u)
        {
            MachineLanguageNode_t *node = machine_language_node(walk, active->node);

            if (node != NULL && node->type == MACHINE_LANGUAGE_NODE(EXTERNAL) && active->node == walk->last_created)
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
        if (status != MACHINE_LANGUAGE_OK)
            return status;
        walk->pc = after;
        if (machine_language_declared_type(active->opcode) == MACHINE_LANGUAGE_NODE(BUFFER_FIELD) &&
            machine_language_builds(walk, active->node))
            machine_language_build_buffer_field(walk, active);
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPERAND(BYTE_LIST): walk->pc = active->end; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPERAND(TERM_LIST):
    case MACHINE_LANGUAGE_OPERAND(FIELD_LIST):
    case MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST): return machine_language_open_list(walk, kind);
    default:
        if ((active->opcode == MACHINE_LANGUAGE_OPCODE(IF) || active->opcode == MACHINE_LANGUAGE_OPCODE(WHILE)) &&
            active->operands_done == 1u && machine_language_runs_code(walk) &&
            machine_language_run_predicate(walk, active))
            return MACHINE_LANGUAGE_OK;
        if (active->opcode == MACHINE_LANGUAGE_OPCODE(NAME) && active->operands_done == 2u &&
            machine_language_builds(walk, active->node))
            status = machine_language_build_name(walk, active);
        else if (active->opcode == MACHINE_LANGUAGE_OPCODE(OPERATION_REGION) && active->operands_done == 3u &&
                 machine_language_builds(walk, active->node))
            status = machine_language_build_region(walk, active);
        return (status == MACHINE_LANGUAGE_OK) ? machine_language_term(walk, kind) : status;
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
        const MachineLanguageBlock_t closed = *block;

        machine_language_close_block(walk);
        machine_language_after_block(walk, &closed);
        return MACHINE_LANGUAGE_OK;
    }
    if (block->kind == MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST))
        return machine_language_package_element(walk);
    if (block->kind == MACHINE_LANGUAGE_OPERAND(FIELD_LIST))
        return machine_language_field_element(walk);
    return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
}

/**
 * @brief Runs one pass over table @p table of a load: the declaration pass of a table the root table
 *        lists is an evaluation of its own (§6.1), that of a table Load or LoadTable adds belongs to the
 *        evaluation that runs it, so that @p alone is false then.
 */
static void machine_language_walk_table(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        uint32_t table, MachineLanguagePass_t pass, bool alone)
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

    if (pass == MACHINE_LANGUAGE_PASS_DECLARE)
    {
        ++load->declaring;
        if (alone)
            advanced_configuration_and_power_interface_machine_language_begin_evaluation(load);
    }
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
    if (pass == MACHINE_LANGUAGE_PASS_DECLARE)
    {
        advanced_configuration_and_power_interface_machine_language_release_table_locals(load);
        if (alone)
            advanced_configuration_and_power_interface_machine_language_end_evaluation(load);
        --load->declaring;
    }
}

#define MACHINE_LANGUAGE_FOLD_OFFSET 0x811C9DC5u /**< FNV-1a's offset basis. */
#define MACHINE_LANGUAGE_FOLD_PRIME  0x01000193u /**< FNV-1a's prime. */

/**
 * @brief Folds @p width little-endian bytes of @p value into @p fold.
 */
static uint32_t machine_language_fold_unsigned(uint32_t fold, uint64_t value, uint32_t width)
{
    for (uint32_t byte = 0u; byte < width; ++byte)
        fold = (fold ^ (uint32_t) ((value >> (8u * byte)) & 0xFFu)) * MACHINE_LANGUAGE_FOLD_PRIME;
    return fold;
}

/**
 * @brief Folds @p length bytes into @p fold.
 */
static uint32_t machine_language_fold_bytes(uint32_t fold, const uint8_t *bytes, uint32_t length)
{
    for (uint32_t byte = 0u; byte < length; ++byte)
        fold = (fold ^ bytes[byte]) * MACHINE_LANGUAGE_FOLD_PRIME;
    return fold;
}

/**
 * @brief Folds one value: an Integer, a String's or Buffer's length and bytes, a Package's count and
 *        each element, nested packages through an explicit stack, a name element as its path bytes.
 */
static uint32_t machine_language_fold_value(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                            uint32_t fold,
                                            const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value)
{
    struct {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *elements;
        uint32_t index;
        uint32_t count;
    } stack[32];
    uint32_t depth = 0u;

    for (;;)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
            advanced_configuration_and_power_interface_machine_language_object(&load->heap, value->handle);

        switch (value->kind)
        {
        case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER:
            fold = machine_language_fold_unsigned(fold, value->payload, 8u);
            break;
        case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING:
        case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER:
            fold = machine_language_fold_unsigned(fold, object->length, 4u);
            fold = machine_language_fold_bytes(fold, (const uint8_t *) (object + 1), object->length);
            break;
        case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_NAME_REFERENCE: {
            const uint8_t *bytes = (const uint8_t *) load->tables[value->auxiliary - 1u].header;
            AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;
            uint32_t after = 0u;

            if (advanced_configuration_and_power_interface_machine_language_read_name_string(
                    bytes, (uint32_t) value->payload, load->tables[value->auxiliary - 1u].length, &name, &after) ==
                MACHINE_LANGUAGE_OK)
                fold = machine_language_fold_bytes(fold, bytes + value->payload, after - (uint32_t) value->payload);
            break;
        }
        case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE:
            fold = machine_language_fold_unsigned(fold, object->length, 4u);
            if (depth < sizeof(stack) / sizeof(stack[0]))
            {
                stack[depth].elements =
                    (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (object + 1);
                stack[depth].index = 0u;
                stack[depth].count = object->length;
                ++depth;
            }
            break;
        default: break;
        }
        while (depth != 0u && stack[depth - 1u].index == stack[depth - 1u].count)
            --depth;
        if (depth == 0u)
            return fold;
        value = &stack[depth - 1u].elements[stack[depth - 1u].index++];
    }
}

/**
 * @brief Folds what one node holds besides a value: a method's flags and body length, a region's
 *        space, offset and length, a field unit's layout, a Processor's ID and block.
 */
static uint32_t machine_language_fold_node(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                           uint32_t fold, const MachineLanguageNode_t *node)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, node->value.handle);

    if (node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region =
            (const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *) (object + 1);

        fold = machine_language_fold_unsigned(fold, region->space, 1u);
        fold = machine_language_fold_unsigned(fold, region->offset, 8u);
        return machine_language_fold_unsigned(fold, region->length, 8u);
    }
    if (node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *unit =
            (const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *) (object + 1);

        fold = machine_language_fold_unsigned(fold, unit->kind, 1u);
        fold = machine_language_fold_unsigned(fold, unit->bit_offset, 4u);
        fold = machine_language_fold_unsigned(fold, unit->bit_length, 4u);
        fold = machine_language_fold_unsigned(fold, unit->access, 1u);
        return machine_language_fold_unsigned(fold, unit->update, 1u);
    }
    if (node->type == MACHINE_LANGUAGE_NODE(METHOD))
    {
        fold = machine_language_fold_unsigned(fold, node->value.auxiliary, 1u);
        return machine_language_fold_unsigned(fold, node->value.payload >> 32u, 4u);
    }
    if (node->type == MACHINE_LANGUAGE_NODE(PROCESSOR))
    {
        fold = machine_language_fold_unsigned(
            fold,
            node->value.payload >>
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PROCESSOR_IDENTIFIER_SHIFT,
            1u);
        fold = machine_language_fold_unsigned(fold, node->value.payload, 4u);
        return machine_language_fold_unsigned(fold, node->value.payload >> 32u, 1u);
    }
    return machine_language_fold_value(load, fold, &node->value);
}

/**
 * @brief Gives the predefined `\_OS_` and `\_REV` the answers real firmware expects (§5.4):
 *        "Microsoft Windows NT" and 2.
 */
static MachineLanguageStatus_t
machine_language_answer_identity(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    static const char operating_system[] = "Microsoft Windows NT";
    MachineLanguageNode_t *revision = advanced_configuration_and_power_interface_machine_language_namespace_node(
        &load->space,
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_REV", 0u)));
    MachineLanguageNode_t *name = advanced_configuration_and_power_interface_machine_language_namespace_node(
        &load->space,
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_OS_", 0u)));
    uint32_t handle = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        &load->heap, &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
        (uint32_t) sizeof(operating_system) - 1u, &handle);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    memcpy(advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle) + 1,
           operating_system, sizeof(operating_system));
    name->value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING;
    name->value.handle = handle;
    revision->value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER;
    revision->value.payload = 2u;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The length the header at @p bytes gives, little-endian.
 */
static uint32_t machine_language_header_length(const uint8_t *bytes)
{
    return (uint32_t) bytes[4] | ((uint32_t) bytes[5] << 8u) | ((uint32_t) bytes[6] << 16u) |
           ((uint32_t) bytes[7] << 24u);
}

/**
 * @brief Whether the load holds a table of @p length bytes equal to @p header's: ACPICA compares two
 *        tables over their whole length.
 */
static bool machine_language_holds_table(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                         const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header,
                                         uint32_t length)
{
    for (uint32_t table = 0u; table < load->table_count; ++table)
        if (load->tables[table].length == length && memcmp(load->tables[table].header, header, length) == 0)
            return true;
    return false;
}

/**
 * @brief Adds a table Load or LoadTable brings to the registry, then runs its passes inside the
 *        evaluation that runs the Load: its declarations, the Externals they could not place, its
 *        decoding. A table the load holds already is refused, counted.
 */
static MachineLanguageStatus_t
machine_language_add_table(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                           const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header, uint32_t length,
                           uint64_t physical_address, uint8_t origin, uint16_t pages, bool *out_added)
{
    *out_added = false;
    if (machine_language_holds_table(load, header, length))
    {
        ++load->load_duplicates;
        return MACHINE_LANGUAGE_OK;
    }
    if (load->table_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_TABLES);

    const uint32_t index = load->table_count++;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[index];

    memset(table, 0, sizeof(*table));
    table->header = header;
    table->physical_address = physical_address;
    table->length = length;
    table->checksum_valid = advanced_configuration_and_power_interface_checksum_holds(header, length);
    table->origin = origin;
    table->pages = pages;
    load->checksum_bad_loaded += table->checksum_valid ? 0u : 1u;
    load->bytes += length - (uint32_t) sizeof(*header);
    ++load->tables_loaded;
    machine_language_walk_table(load, index, MACHINE_LANGUAGE_PASS_DECLARE, false);
    if (load->externals_unplaced != 0u)
        machine_language_walk_table(load, index, MACHINE_LANGUAGE_PASS_EXTERNALS, false);
    machine_language_walk_table(load, index, MACHINE_LANGUAGE_PASS_DECODE, false);
    *out_added = true;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Takes pages of the namespace arena for the copy of a table of @p length bytes, within the
 *        bytes Load may copy.
 */
static MachineLanguageStatus_t
machine_language_take_copy(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t length,
                           uint8_t **out_copy, uint16_t *out_pages)
{
    const uint32_t pages = (length + ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES - 1u) /
                           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES;

    if (length > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLE_BYTES - load->table_bytes_copied)
        return MACHINE_LANGUAGE_STATUS(LIMIT_TABLE_BYTES);
    *out_copy = advanced_configuration_and_power_interface_machine_language_namespace_take_pages(&load->space, pages);
    if (*out_copy == NULL)
        return MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY);
    *out_pages = (uint16_t) pages;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Adds the copy Load made of a table of @p length bytes, counted; gives its pages back when it
 *        is refused or cannot be added.
 */
static MachineLanguageStatus_t
machine_language_keep_copy(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint8_t *copy,
                           uint32_t length, uint16_t pages, uint64_t physical_address, bool *out_loaded)
{
    const MachineLanguageStatus_t status = machine_language_add_table(
        load, (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) copy, length, physical_address,
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_LOAD, pages, out_loaded);

    if (status == MACHINE_LANGUAGE_OK && *out_loaded)
        load->table_bytes_copied += length;
    else
        advanced_configuration_and_power_interface_machine_language_namespace_give_pages(&load->space, copy, pages);
    return status;
}

/**
 * @brief Runs Load on a region: SystemMemory only, the table wholly inside it, read through the mapping
 *        cache, its header first.
 */
static MachineLanguageStatus_t
machine_language_load_region(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                             const MachineLanguageNode_t *node, bool *out_loaded)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region =
        (const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t
             *) (advanced_configuration_and_power_interface_machine_language_object(&load->heap, node->value.handle) +
                 1);
    uint8_t header[sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t)];
    uint8_t *copy = NULL;
    uint16_t pages = 0u;

    if (region->space != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (region->state == 0u)
        return MACHINE_LANGUAGE_STATUS(REGION_UNEVALUATED);
    if (region->length < sizeof(header))
        return MACHINE_LANGUAGE_STATUS(REGION_BOUNDS);

    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_memory_read(
        load, region->offset, (uint32_t) sizeof(header), header);
    const uint32_t length = machine_language_header_length(header);

    if (status == MACHINE_LANGUAGE_OK && (length < sizeof(header) || length > region->length))
        status = MACHINE_LANGUAGE_STATUS(REGION_BOUNDS);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_take_copy(load, length, &copy, &pages);
    if (status == MACHINE_LANGUAGE_OK)
        status =
            advanced_configuration_and_power_interface_machine_language_memory_read(load, region->offset, length, copy);
    if (status == MACHINE_LANGUAGE_OK)
        return machine_language_keep_copy(load, copy, length, pages, region->offset, out_loaded);
    if (copy != NULL)
        advanced_configuration_and_power_interface_machine_language_namespace_give_pages(&load->space, copy, pages);
    return status;
}

/**
 * @brief Runs Load on a field unit: the unit read as a Buffer, the table wholly inside it.
 */
static MachineLanguageStatus_t
machine_language_load_field(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t source,
                            bool *out_loaded)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value = {0};
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t available = 0u;
    uint32_t length = 0u;
    uint8_t *copy = NULL;
    uint16_t pages = 0u;
    MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_field_read(load, source, &value);

    if (status == MACHINE_LANGUAGE_OK)
        status = (value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER) ?
                     advanced_configuration_and_power_interface_machine_language_value_bytes(
                         &load->heap, &value, 64u, scratch, &bytes, &available) :
                     MACHINE_LANGUAGE_STATUS(REGION_BOUNDS);
    if (status == MACHINE_LANGUAGE_OK)
    {
        length = (available >= sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t)) ?
                     machine_language_header_length(bytes) :
                     0u;
        if (length < sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) || length > available)
            status = MACHINE_LANGUAGE_STATUS(REGION_BOUNDS);
    }
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_take_copy(load, length, &copy, &pages);
    if (status == MACHINE_LANGUAGE_OK)
        memcpy(copy, bytes, length);
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &value);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_keep_copy(load, copy, length, pages, 0u, out_loaded) :
                                             status;
}

/**
 * @brief Whether the @p length bytes at @p wanted, padded with zeros to @p width, equal the @p width
 *        bytes of @p field; an empty one matches any, as in ACPICA.
 */
static bool machine_language_identifier_matches(const char *field, const uint8_t *wanted, uint32_t length,
                                                uint32_t width)
{
    if (length == 0u)
        return true;
    for (uint32_t index = 0u; index < width; ++index)
        if ((uint8_t) field[index] != ((index < length) ? wanted[index] : 0u))
            return false;
    return true;
}

bool advanced_configuration_and_power_interface_machine_language_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t max_nodes, uint32_t max_arena_bytes)
{
    memset(load, 0, sizeof(*load));
    load->status = (uint8_t) advanced_configuration_and_power_interface_machine_language_namespace_open(
        &load->space, host, max_nodes, max_arena_bytes);
    if (load->status == MACHINE_LANGUAGE_OK)
        load->status = (uint8_t) machine_language_answer_identity(load);
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
    machine_language_walk_table(load, load->table_count++, MACHINE_LANGUAGE_PASS_DECLARE, true);
    return true;
}

void advanced_configuration_and_power_interface_machine_language_decode_tables(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    for (uint32_t table = 0u; table < load->table_count && load->externals_unplaced != 0u; ++table)
        machine_language_walk_table(load, table, MACHINE_LANGUAGE_PASS_EXTERNALS, true);
    for (uint32_t table = 0u; table < load->table_count; ++table)
        machine_language_walk_table(load, table, MACHINE_LANGUAGE_PASS_DECODE, true);
}

void advanced_configuration_and_power_interface_machine_language_declare_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &directory->fixed_description;
    const uint16_t control_ports[2] = {
        advanced_configuration_and_power_interface_input_output_port(&fadt->power_management_control_blocks[0], 2u),
        advanced_configuration_and_power_interface_input_output_port(&fadt->power_management_control_blocks[1], 2u),
    };

    advanced_configuration_and_power_interface_machine_language_use_global_lock(
        load, fadt->hardware_reduced ? 0u : directory->firmware_control_physical_address, control_ports);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        load, advanced_configuration_and_power_interface_find_in_directory(directory, "DSDT"));
    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        if (memcmp(directory->tables[index].signature, "SSDT", sizeof(directory->tables[index].signature)) == 0)
            (void) advanced_configuration_and_power_interface_machine_language_declare_table(
                load, directory->tables[index].header);
    }
    load->directory = directory;
}

void advanced_configuration_and_power_interface_machine_language_load_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    advanced_configuration_and_power_interface_machine_language_declare_directory(load, directory);
    advanced_configuration_and_power_interface_machine_language_decode_tables(load);
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_load_from(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t source, bool *out_loaded)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, source);

    *out_loaded = false;
    if (load->declaring != 0u)
        return MACHINE_LANGUAGE_STATUS(LOAD_FORM_UNSUPPORTED);
    switch (node->value.kind)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION:
        return machine_language_load_region(load, node, out_loaded);
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT:
        return machine_language_load_field(load, source, out_loaded);
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER:
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD:
        return MACHINE_LANGUAGE_STATUS(LOAD_FORM_UNSUPPORTED);
    default: return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    }
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_load_from_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const uint8_t *signature, const uint8_t *oem,
    const uint8_t *oem_table, const uint32_t lengths[3], bool *out_loaded)
{
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory = load->directory;

    *out_loaded = false;
    if (load->declaring != 0u)
        return MACHINE_LANGUAGE_STATUS(LOAD_FORM_UNSUPPORTED);
    if (lengths[0] != sizeof(directory->tables[0].signature) || lengths[1] > 6u || lengths[2] > 8u)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    for (uint32_t index = 0u; directory != NULL && index < directory->table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = directory->tables[index].header;

        if (header != NULL && memcmp(header->signature, signature, lengths[0]) == 0 &&
            machine_language_identifier_matches(header->oem_id, oem, lengths[1], sizeof(header->oem_id)) &&
            machine_language_identifier_matches(header->oem_table_id, oem_table, lengths[2],
                                                sizeof(header->oem_table_id)))
            return machine_language_add_table(
                load, header, directory->tables[index].length, 0u,
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_LOAD_TABLE, 0u, out_loaded);
    }
    ++load->load_tables_missing;
    return MACHINE_LANGUAGE_OK;
}

void advanced_configuration_and_power_interface_machine_language_close(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    advanced_configuration_and_power_interface_machine_language_regions_close(load);
    advanced_configuration_and_power_interface_machine_language_heap_close(&load->heap, &load->space);
    for (uint32_t table = 0u; table < load->table_count; ++table)
        if (load->tables[table].pages != 0u)
            advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
                &load->space, (void *) (uintptr_t) load->tables[table].header, load->tables[table].pages);
    advanced_configuration_and_power_interface_machine_language_namespace_close(&load->space);
}

uint32_t advanced_configuration_and_power_interface_machine_language_namespace_fold(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &load->space;
    uint32_t fold = MACHINE_LANGUAGE_FOLD_OFFSET;
    uint32_t depth = 0u;
    uint32_t index = advanced_configuration_and_power_interface_machine_language_namespace_node(
                         space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT)
                         ->first_child;

    while (index != 0u)
    {
        const MachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

        if (node->type != MACHINE_LANGUAGE_NODE(EXTERNAL))
        {
            fold = machine_language_fold_unsigned(fold, depth, 1u);
            fold = machine_language_fold_unsigned(fold, node->name, 4u);
            fold = machine_language_fold_unsigned(fold, node->type, 1u);
            fold = machine_language_fold_node(load, fold, node);
        }
        if (node->first_child != 0u)
        {
            ++depth;
            index = node->first_child;
            continue;
        }
        while (index != 0u &&
               advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->next_sibling ==
                   0u)
        {
            index = advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->parent;
            if (index == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT)
                index = 0u;
            else
                --depth;
        }
        if (index != 0u)
            index =
                advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->next_sibling;
    }
    return fold;
}
