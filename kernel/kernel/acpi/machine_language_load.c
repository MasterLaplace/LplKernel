#include <kernel/acpi/log.h>
#include <kernel/acpi/machine_language.h>

#include <stddef.h>
#include <string.h>

#define MACHINE_LANGUAGE_CALL             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
#define MACHINE_LANGUAGE_OK               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_OPERAND(kind)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_##kind
#define MACHINE_LANGUAGE_OPCODE(name)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define MACHINE_LANGUAGE_NODE(type)       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_STATUS(name)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_BLOCK_NOT_RUN    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_NOT_RUN
#define MACHINE_LANGUAGE_FAULT_PATH_BYTES 128u /**< Bytes of the path the line of a fault names: 25 levels deep. */
#define MACHINE_LANGUAGE_ASKS                                                                                          \
    MACHINE_LANGUAGE_STATUS(COUNT) /**< What a step gives when its statement waits for a term the interpreter runs. */
#define MACHINE_LANGUAGE_VALUE(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define MACHINE_LANGUAGE_MAX_NESTED_PACKAGES                                                                           \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES
#define MACHINE_LANGUAGE_MAX_BUFFER_BYTES ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES
#define MACHINE_LANGUAGE_DATA_WIDTH                                                                                    \
    0x0Fu /**< Low bits of a lead byte's class: the bytes of an integer constant, its lead included. */
#define MACHINE_LANGUAGE_DATA_STRING           0x10u /**< Class of a String's lead byte. */
#define MACHINE_LANGUAGE_DATA_BUFFER           0x20u /**< Class of a Buffer's lead byte. */
#define MACHINE_LANGUAGE_DATA_PACKAGE          0x30u /**< Class of a Package's lead byte. */
#define MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE 0x40u /**< Class of a VarPackage's lead byte. */
#define MACHINE_LANGUAGE_DATA_SEGMENT          0x50u /**< Class of a NameSeg's first character. */
#define MACHINE_LANGUAGE_DATA_PREFIX           0x60u /**< Class of a NameString's root, parent, dual or multi prefix. */

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
    MACHINE_LANGUAGE_PASS_DECODE,    /**< Decodes and counts every term but a duplicate's body; declares only the
                                          temporary nodes of method bodies. */
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
    MachineLanguagePendingOperator_t *pending; /**< The load's operator stack, read through this pointer rather than
                                                    at its place in the load, for the same reason as top. */
    MachineLanguageBlock_t *top; /**< The construct the walk is in, the last of the load's block stack, kept as it
                                      opens and closes so that a step reads it without computing where it lies in
                                      the load, whose layout would then change the step's code; once every
                                      construct is closed, it stays on the first. */
    uint32_t fault_opcode;       /**< The opcode a fault at pc belongs to. */
    MachineLanguagePass_t pass;  /**< Which pass it is. */
    uint32_t last_created;       /**< The node the last declaration created, 0 when it created none. */
    uint32_t external_start;     /**< The External the Externals pass places, by its opcode's offset, or 0. */
    MachineLanguageNameString_t external_name; /**< That External's NameString. */
    MachineLanguageFieldList_t field;          /**< The field list being read. */
    bool undeclared_next;     /**< Whether the next TermList the decoding pass opens belongs to a construct the
                                   declaration pass did not declare. */
    uint32_t undeclared_from; /**< The depth of the outermost such TermList open, or 0: faults inside it are
                                   counted apart. */
    bool duplicate_next;      /**< Whether the next TermList the walk opens is the body of a duplicate declaration,
                                   which no pass reads (§5.1): one the declaration pass found the name of taken, or,
                                   past it, one at table level whose name another construct declared. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t *declaration; /**< For the statements it
                                                                                        takes for the interpreter,
                                                                                        what they did; NULL for any
                                                                                        other walk. */
    uint8_t asked;    /**< Terms it has needed the interpreter for: it stops at the first one not answered yet,
                           so those it has are all the first statement's that needed one. */
    bool closes_body; /**< Whether a TermList the statement opened reached its end. */
    bool abandons;    /**< Whether a fault abandoned the rest of the table. */
    uint32_t entered; /**< TermLists of scopes it has entered for the interpreter and not left yet (O21): its first
                           construct is then the innermost one's, which it leaves at its end. */
} MachineLanguageWalk_t;

/**
 * @brief One element of a data object kept in its table (O20), as machine_language_read_kept reads it.
 */
typedef struct MachineLanguageKeptElement {
    uint64_t integer; /**< An Integer's value, masked to its table's width. */
    uint32_t first;   /**< Where a String's characters, a Buffer's initialiser or a package's elements start. */
    uint32_t end;     /**< Where it ends: past an Integer, a name, a String's NUL, a Buffer or a package. */
    uint32_t count;   /**< A String's or a Buffer's length, or a package's element count. */
    uint8_t class;    /**< The class of its lead byte in machine_language_data_classes. */
} MachineLanguageKeptElement_t;

/**
 * @brief A package the builder of kept data fills (C4): its elements, its object, the next element, how many it
 *        holds, and where it ends.
 */
typedef struct MachineLanguageKeptFrame {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *elements; /**< Its elements, in its object. */
    uint32_t handle;                                                        /**< Its object. */
    uint32_t index;                                                         /**< The next element read. */
    uint32_t count;                                                         /**< Elements it holds. */
    uint32_t end;                                                           /**< Its PkgLength end. */
} MachineLanguageKeptFrame_t;

/**
 * @brief The construct the walk is in.
 */
static MachineLanguageBlock_t *machine_language_top_block(MachineLanguageWalk_t *walk) { return walk->top; }

/**
 * @brief The operator on top of the stack, when it belongs to the construct the walk is in.
 */
static MachineLanguagePendingOperator_t *machine_language_active_operator(MachineLanguageWalk_t *walk)
{
    if (walk->pending_count <= machine_language_top_block(walk)->pending_base)
        return NULL;
    return &walk->pending[walk->pending_count - 1u];
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
 * @brief Counts as decoded, in a build that counts what runs (§4.1), the bytes a walk went over from @p from to its pc.
 */
static void machine_language_count_decoded(MachineLanguageWalk_t *walk, uint32_t from)
{
    if (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_COUNTS_EXECUTED)
        walk->load->bytes_decoded += walk->pc - from;
}

/**
 * @brief Takes back from the bytes decoded, in a build that counts what runs (§4.1), those from @p from to @p to, which
 *        the walk steps over without decoding them.
 */
static void machine_language_count_stepped_over(MachineLanguageWalk_t *walk, uint32_t from, uint32_t to)
{
    if (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_COUNTS_EXECUTED && to > from)
        walk->load->bytes_decoded -= to - from;
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
 * @brief The method a fault of the walk names: the one whose body the decoding pass decodes, or 0 for table-level
 *        code, the scope the walk is in then naming it.
 */
static uint32_t machine_language_walk_method(const MachineLanguageWalk_t *walk)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;

    return (walk->pass == MACHINE_LANGUAGE_PASS_DECODE && load->frame_count != 0u) ?
               load->frame_methods[load->frame_count - 1u] :
               0u;
}

/**
 * @brief Reports a fault the walk meets at @p offset of its table, counted under its status and logged (§4.5), unless
 *        it is the failure the interpreter reported and goes past, whose statement the walk steps over.
 */
static void machine_language_report_walk_fault(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status,
                                               uint32_t offset)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &walk->load->evaluation;

    if (evaluation->logged_fault_table == walk->table + 1u && evaluation->logged_fault_offset == offset)
        return;
    advanced_configuration_and_power_interface_machine_language_report_fault(
        walk->load, status, walk->table + 1u, offset, machine_language_limit(walk), machine_language_walk_method(walk),
        machine_language_top_block(walk)->scope);
}

/**
 * @brief Counts a fault of the declaration pass or of the Externals pass, the first one kept, against the walk's
 *        table too, and logs it at @p offset of that table.
 */
static void machine_language_report_load_error(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status,
                                               uint32_t offset)
{
    machine_language_count_load_error(walk->load, status);
    advanced_configuration_and_power_interface_machine_language_count_table_failure(walk->load, walk->table + 1u);
    machine_language_report_walk_fault(walk, status, offset);
}

/**
 * @brief Records a fault at the walk's pc: in load_errors for the declaration pass, logged; in its table and in
 *        decode_errors for the decoding pass, logged in a method body alone, since the declaration pass logged
 *        those of table-level code, and the check of kept data logs those in the data objects it kept (O20); in
 *        undeclared_faults for the decoding pass inside a construct the declaration pass did not declare, not logged,
 *        since its names resolve against objects that do not exist; the Externals pass counts its own faults where it
 *        meets them.
 */
static void machine_language_record_fault(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;

    if (walk->pass == MACHINE_LANGUAGE_PASS_EXTERNALS)
        return;
    if (walk->pass == MACHINE_LANGUAGE_PASS_DECLARE)
    {
        machine_language_report_load_error(walk, status, walk->pc);
        return;
    }

    if (walk->undeclared_from != 0u)
    {
        ++load->undeclared_faults;
        return;
    }

    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[walk->table];

    ++load->decode_errors;
    if (load->frame_count != 0u)
        machine_language_report_walk_fault(walk, status, walk->pc);
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
    if (--walk->block_count != 0u)
        --walk->top;
}

/**
 * @brief Drops the marks a Scope the walk could not find or a duplicate declaration left for the
 *        TermList to come, once a recovery leaves the construct that TermList belongs to.
 */
static void machine_language_forget_marks(MachineLanguageWalk_t *walk)
{
    walk->undeclared_next = false;
    walk->duplicate_next = false;
}

/**
 * @brief Goes on after a fault: past the innermost construct with a PkgLength around it, else
 *        past the rest of the walk's first construct, the table or the one the statement stands in,
 *        which is also where a PkgLength crossing its parent and an exhausted arena lead, those two
 *        abandoning the rest of the table (§6.2). Stepping over data, a Buffer or a Package, keeps
 *        the marks: the construct around it still opens its TermList.
 */
static void machine_language_recover(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status)
{
    const MachineLanguageBlock_t *block = machine_language_top_block(walk);

    if (status != MACHINE_LANGUAGE_STATUS(PACKAGE_LENGTH_OUT_OF_PARENT) &&
        status != MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY))
    {
        for (uint32_t index = walk->pending_count; index > block->pending_base; --index)
        {
            const MachineLanguagePendingOperator_t *pending = &walk->pending[index - 1u];
            const uint8_t flags =
                (pending->opcode == MACHINE_LANGUAGE_CALL) ?
                    0u :
                    advanced_configuration_and_power_interface_machine_language_descriptor(pending->opcode)->flags;

            if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH) != 0u)
            {
                if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT) == 0u)
                    machine_language_forget_marks(walk);
                machine_language_count_stepped_over(walk, walk->pc, pending->end);
                walk->pc = pending->end;
                walk->pending_count = index - 1u;
                return;
            }
        }
        machine_language_forget_marks(walk);
        if (walk->block_count > 1u)
        {
            machine_language_count_stepped_over(walk, walk->pc, block->end);
            walk->pc = block->end;
            walk->pending_count = block->pending_base;
            machine_language_close_block(walk);
            return;
        }
    }
    machine_language_forget_marks(walk);
    walk->abandons = status == MACHINE_LANGUAGE_STATUS(PACKAGE_LENGTH_OUT_OF_PARENT) ||
                     status == MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY);
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
 * @brief Finds the child of @p parent named @p name, or creates it, by find_child then add_child, and reports as
 *        advanced_configuration_and_power_interface_machine_language_namespace_find_or_add_child does: the way a load
 *        whose general_load is set declares a name, so that the differential tests compare it with the way the load
 *        takes otherwise (§3.6).
 */
static MachineLanguageStatus_t
machine_language_find_then_add_child(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                     uint32_t parent, uint32_t name, uint8_t type, uint16_t owner, uint32_t *out_index,
                                     bool *out_created)
{
    const uint32_t existing =
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(space, parent, name);

    *out_created = existing == 0u;
    if (existing == 0u)
        return advanced_configuration_and_power_interface_machine_language_namespace_add_child(space, parent, name,
                                                                                               type, owner, out_index);
    *out_index = existing;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Creates the node a declaration names in the scope the walk is in, or finds the one that
 *        holds the name already: an External gives way to the object, any other existing node
 *        stays and the declaration counts as a duplicate. A NameString of one NameSeg and no prefix
 *        names a child of that scope, which needs no walk of prefixes and segments. A load whose general_load is set
 *        finds or creates the node by machine_language_find_then_add_child, any other by
 *        advanced_configuration_and_power_interface_machine_language_namespace_find_or_add_child.
 *
 * @details Always inlined, as machine_language_use_name, so that the insertion is in line in each declaration
 *          decoder and in the field list: a call costs a declaration more than the probe it makes.
 */
__attribute__((always_inline)) static inline MachineLanguageStatus_t
machine_language_declare(MachineLanguageWalk_t *walk, const MachineLanguageNameString_t *name, uint8_t type,
                         uint32_t *out_index)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t parent = machine_language_top_block(walk)->scope;
    uint32_t segment = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    const uint16_t owner = (uint16_t) (walk->table + 1u);
    bool created = false;

    if (!name->root && name->parent_prefixes == 0u && name->segment_count == 1u)
        segment = advanced_configuration_and_power_interface_machine_language_name_segment(walk->bytes, name->segments);
    else
        status = advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &load->space, parent, walk->bytes, name, &parent, &segment);
    walk->last_created = 0u;
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    status = load->general_load ?
                 machine_language_find_then_add_child(&load->space, parent, segment, type, owner, out_index, &created) :
                 advanced_configuration_and_power_interface_machine_language_namespace_find_or_add_child(
                     &load->space, parent, segment, type, owner, out_index, &created);
    if (!created)
    {
        const uint32_t existing = *out_index;
        MachineLanguageNode_t *node = machine_language_node(walk, existing);

        walk->last_created = 0u;
        if (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL) && type != MACHINE_LANGUAGE_NODE(EXTERNAL))
        {
            node->type = type;
            ++load->space.generation;
            node->owner = owner;
            node->value = (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){0};
            machine_language_count_declaration(walk);
            walk->last_created = existing;
        }
        else if (type != MACHINE_LANGUAGE_NODE(EXTERNAL))
            ++load->duplicates;
        return MACHINE_LANGUAGE_OK;
    }
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
        if (load->temporary_count > load->temporary_nodes_peak)
            load->temporary_nodes_peak = load->temporary_count;
        walk->last_created = *out_index;
    }
    return status;
}

#define MACHINE_LANGUAGE_DECLARATIONS(X, argument)                                                                     \
    X(NAME, UNINITIALIZED, argument)                                                                                   \
    X(METHOD, METHOD, argument)                                                                                        \
    X(EXTERNAL, EXTERNAL, argument)                                                                                    \
    X(MUTEX, MUTEX, argument)                                                                                          \
    X(EVENT, EVENT, argument)                                                                                          \
    X(OPERATION_REGION, OPERATION_REGION, argument)                                                                    \
    X(DATA_REGION, OPERATION_REGION, argument)                                                                         \
    X(DEVICE, DEVICE, argument)                                                                                        \
    X(PROCESSOR, PROCESSOR, argument)                                                                                  \
    X(POWER_RESOURCE, POWER_RESOURCE, argument)                                                                        \
    X(THERMAL_ZONE, THERMAL_ZONE, argument)                                                                            \
    X(CREATE_BIT_FIELD, BUFFER_FIELD, argument)                                                                        \
    X(CREATE_BYTE_FIELD, BUFFER_FIELD, argument)                                                                       \
    X(CREATE_WORD_FIELD, BUFFER_FIELD, argument)                                                                       \
    X(CREATE_DOUBLE_WORD_FIELD, BUFFER_FIELD, argument)                                                                \
    X(CREATE_QUAD_WORD_FIELD, BUFFER_FIELD, argument)                                                                  \
    X(CREATE_FIELD, BUFFER_FIELD, argument) /**< Each opcode that creates a node, and the node's type: X(opcode, type, \
                                               argument), @p argument passed to every row. */
#define MACHINE_LANGUAGE_DECLARED_TYPE_CASE(opcode, type, unused)                                                      \
    case MACHINE_LANGUAGE_OPCODE(opcode):                                                                              \
        return MACHINE_LANGUAGE_NODE(type); /**< The case of                                                           \
                                               machine_language_declared_type for a                                    \
                                               row of MACHINE_LANGUAGE_DECLARATIONS. */

/**
 * @brief The node type the declaration an opcode makes creates, as MACHINE_LANGUAGE_DECLARATIONS lists it, or 0xFF
 *        when it declares nothing.
 */
static uint8_t machine_language_declared_type(uint32_t opcode)
{
    switch (opcode)
    {
        MACHINE_LANGUAGE_DECLARATIONS(MACHINE_LANGUAGE_DECLARED_TYPE_CASE, 0)
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
    const uint32_t start = walk->external_start;

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
        machine_language_report_load_error(walk, status, start);
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
        machine_language_report_load_error(walk, MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND), active->start);
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
 *
 * @details Always inlined into each declaration decoder, which knows its opcode, so that the compiler drops the
 *          branches that opcode never takes; the walk calls it out of line, through machine_language_use_operand_name.
 */
__attribute__((always_inline)) static inline MachineLanguageStatus_t
machine_language_use_name(MachineLanguageWalk_t *walk, MachineLanguagePendingOperator_t *active,
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
 * @brief machine_language_use_name, out of line, for a NameString the walk's operand step reads: the walk runs every
 *        pass and every method body, and the declaration's code in line in it costs the decoding pass of the debug
 *        kernel and the host tools more in register spills than the call it saves the declaration pass.
 */
__attribute__((noinline)) static MachineLanguageStatus_t
machine_language_use_operand_name(MachineLanguageWalk_t *walk, MachineLanguagePendingOperator_t *active,
                                  const MachineLanguageNameString_t *name)
{
    return machine_language_use_name(walk, active, name);
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

    MachineLanguagePendingOperator_t *pending = &walk->pending[walk->pending_count++];

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

    MachineLanguagePendingOperator_t *pending = &walk->pending[walk->pending_count++];

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
 *        is left to the walk, which meets it on the same bytes; any other fault is counted here, and logged at
 *        @p offset, where the declaration that builds starts.
 */
static MachineLanguageStatus_t machine_language_build_fault(MachineLanguageWalk_t *walk, MachineLanguageStatus_t status,
                                                            uint32_t offset)
{
    if (status == MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY))
        return status;
    if (status != MACHINE_LANGUAGE_OK && status != MACHINE_LANGUAGE_STATUS(TRUNCATED) &&
        status != MACHINE_LANGUAGE_STATUS(INVALID_OPCODE) &&
        status != MACHINE_LANGUAGE_STATUS(PACKAGE_LENGTH_OUT_OF_PARENT))
        machine_language_report_load_error(walk, status, offset);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Builds a Name's object from its data object, which starts at the walk's pc; one that needs
 *        the interpreter is counted and left Uninitialized. @p out_after receives the offset past the data
 *        object once it is built, and 0 when it is not.
 */
static MachineLanguageStatus_t machine_language_build_name(MachineLanguageWalk_t *walk,
                                                           const MachineLanguagePendingOperator_t *active,
                                                           uint32_t *out_after)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    MachineLanguageNode_t *node = machine_language_node(walk, active->node);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value;
    bool constant = false;
    uint32_t after = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_data(
        &load->heap, &load->space, &source, walk->pc, active->end, &value, &constant, &after);

    *out_after = 0u;
    if (status != MACHINE_LANGUAGE_OK)
        return machine_language_build_fault(walk, status, active->start);
    if (!constant)
        ++load->values_deferred;
    else
    {
        node->value = value;
        node->type = advanced_configuration_and_power_interface_machine_language_node_type_of_value(value.kind);
        *out_after = after;
    }
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Whether the walk has room for every package a data object could nest, so that decoding it as terms could meet
 *        no limit: a load keeps a Name's data object in its table only there (O20), with its shortcuts or without. The
 *        operator stack always has room, as machine_language_steps_over_data says.
 */
static bool machine_language_room_for_packages(const MachineLanguageWalk_t *walk)
{
    return walk->block_count + ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES <=
           ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BLOCKS;
}

/**
 * @brief Whether the declaration pass may step over a Name's data object it builds instead of decoding it as terms
 *        (O18, O19): unless the load takes no shortcut, and only while the walk has room for every package the object
 *        could nest.
 */
static bool machine_language_room_for_data(const MachineLanguageWalk_t *walk)
{
    return !walk->load->general_load && machine_language_room_for_packages(walk);
}

/**
 * @brief Whether the declaration pass steps over a Name's data object it has just built, which ends at
 *        @p after, instead of decoding it a second time (O18): unless the load takes no shortcut, and only while
 *        the walk has room for every package the object could nest, so that decoding it could meet no limit.
 *
 * @details The operator stack needs no such check. A Name is a statement, read only at a statement's place, where the
 *          declaration pass holds no operator below its own; decoding as terms a data object it could build holds at
 *          most one operator more at a time, a Buffer's, each package's being popped as its list opens: nowhere near
 *          the 256 of §6.1.
 */
static bool machine_language_steps_over_data(const MachineLanguageWalk_t *walk, uint32_t after)
{
    return after != 0u && machine_language_room_for_data(walk);
}

/**
 * @brief Reads an integer operand of a declaration at @p start: a constant, or, for a statement whose
 *        code the interpreter runs, what the interpreter made of it; @p out_known is false when neither
 *        gave one. A term the interpreter has not answered yet is asked for, the statement waiting.
 */
static MachineLanguageStatus_t machine_language_operand_integer(
    MachineLanguageWalk_t *walk, const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t *source,
    uint32_t start, uint32_t end, uint64_t *out_value, uint32_t *out_after, bool *out_known)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t *declaration = walk->declaration;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_integer(
        &walk->load->space, source, start, end, out_value, out_after, out_known);

    if (status != MACHINE_LANGUAGE_OK || *out_known || declaration == NULL ||
        machine_language_top_block(walk)->placement != 0u)
        return status;

    const uint32_t statement = machine_language_active_operator(walk)->start;

    if (declaration->answers_for != statement)
    {
        declaration->answers_for = statement;
        declaration->answered = 0u;
    }

    const uint32_t answer = walk->asked++;

    if (answer < declaration->answered)
    {
        *out_known = declaration->known[answer];
        *out_value = declaration->values[answer];
        *out_after = declaration->afters[answer];
        return MACHINE_LANGUAGE_OK;
    }
    declaration->asks = start;
    declaration->asks_end = end;
    return MACHINE_LANGUAGE_ASKS;
}

/**
 * @brief Gives node @p index a new region object of RegionSpace @p space: @p offset and @p length when @p known
 *        holds, else none yet, the region then counted as waiting for the interpreter.
 *
 * @return OK, or the status allocating the object gave.
 */
static MachineLanguageStatus_t machine_language_place_region(MachineLanguageWalk_t *walk, uint32_t index, uint8_t space,
                                                             bool known, uint64_t offset, uint64_t length)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t handle = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        &load->heap, &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION,
        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t), &handle);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t
             *) (advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle) + 1);
    MachineLanguageNode_t *node = machine_language_node(walk, index);

    region->space = space;
    region->state = known ? 1u : 0u;
    region->offset = known ? offset : 0u;
    region->length = known ? length : 0u;
    load->values_deferred += known ? 0u : 1u;
    node->value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION;
    node->value.handle = handle;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Builds an OperationRegion's object once its space is read: its offset and length start at
 *        the walk's pc, constants, or run by the interpreter for a statement whose code it runs, which
 *        the statement waits for; they wait for the interpreter otherwise.
 */
static MachineLanguageStatus_t machine_language_build_region(MachineLanguageWalk_t *walk,
                                                             const MachineLanguagePendingOperator_t *active)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    uint64_t offset = 0u;
    uint64_t length = 0u;
    uint32_t after = 0u;
    bool constant = false;
    MachineLanguageStatus_t status =
        machine_language_operand_integer(walk, &source, walk->pc, active->end, &offset, &after, &constant);

    if (status == MACHINE_LANGUAGE_OK && constant)
        status = machine_language_operand_integer(walk, &source, after, active->end, &length, &after, &constant);
    if (status == MACHINE_LANGUAGE_ASKS)
        return status;
    if (status == MACHINE_LANGUAGE_OK)
        status =
            machine_language_place_region(walk, active->node, walk->bytes[walk->pc - 1u], constant, offset, length);
    return machine_language_build_fault(walk, status, active->start);
}

/**
 * @brief Builds the buffer field a Create*Field just declared: the Buffer a name holds, built first when its Name
 *        kept it in its table (O20), which the field holds a reference on, and its bit offset and length, which
 *        must be constants that fit 32 bits; one past them is counted as index_out_of_range and left without an
 *        object, and one over a kept Buffer whose bytes built none, left Uninitialized, waits for the interpreter.
 *        Whether it fits its Buffer is checked at access.
 *
 * @return OK, or OUT_OF_MEMORY when the Buffer could not be built, as machine_language_build_fault gives it.
 *
 * @details Never inlined: it runs once per table-level Create*Field, and its body in line in the walk, which runs every
 *          pass and every method body, costs the decoding pass of the debug kernel and the host tools more in register
 *          spills than the call costs the declaration pass.
 */
__attribute__((noinline)) static MachineLanguageStatus_t
machine_language_build_buffer_field(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *active)
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
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (advanced_configuration_and_power_interface_machine_language_read_name_string(
            walk->bytes, active->start + (any_length ? 2u : 1u), active->end, &name, &after) == MACHINE_LANGUAGE_OK &&
        advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, source.scope, walk->bytes, &name, &buffer) == MACHINE_LANGUAGE_OK &&
        machine_language_node(walk, buffer)->type == MACHINE_LANGUAGE_NODE(BUFFER))
    {
        status = advanced_configuration_and_power_interface_machine_language_materialize(load, buffer);
        if (status != MACHINE_LANGUAGE_OK)
            return machine_language_build_fault(walk, status, active->start);
        if (machine_language_node(walk, buffer)->type == MACHINE_LANGUAGE_NODE(BUFFER) &&
            advanced_configuration_and_power_interface_machine_language_evaluate_integer(
                &load->space, &source, after, active->end, &index, &after, &constant) == MACHINE_LANGUAGE_OK &&
            constant && any_length)
            constant =
                advanced_configuration_and_power_interface_machine_language_evaluate_integer(
                    &load->space, &source, after, active->end, &length, &after, &constant) == MACHINE_LANGUAGE_OK &&
                constant;
    }
    if (!constant)
    {
        ++load->values_deferred;
        return MACHINE_LANGUAGE_OK;
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
        machine_language_report_load_error(walk, MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE), active->start);
        return MACHINE_LANGUAGE_OK;
    }

    node->value.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD;
    node->value.flags =
        any_length ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BUFFER_FIELD_READS_AS_BUFFER : 0u;
    node->value.auxiliary = widths[shape];
    node->value.handle = machine_language_node(walk, buffer)->value.handle;
    node->value.payload = (bit_offset & 0xFFFFFFFFull) | ((bit_length & 0xFFFFFFFFull) << 32u);
    advanced_configuration_and_power_interface_machine_language_value_retain(&load->heap, &node->value);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Starts the field list of a Field, an IndexField or a BankField: reads again its names, its
 *        BankValue and its FieldFlags, which the walk has just passed. A name that resolves to nothing
 *        is counted once, and the units get no object; a BankValue that is neither a constant nor run
 *        by the interpreter makes each unit wait for the interpreter.
 *
 * @return OK, or ASKS when the statement waits for the interpreter to run its BankValue.
 */
static MachineLanguageStatus_t machine_language_begin_field_list(MachineLanguageWalk_t *walk,
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
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    *field = (MachineLanguageFieldList_t){0};
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
        return MACHINE_LANGUAGE_OK;
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
        machine_language_report_load_error(walk, MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND), active->start);
        return MACHINE_LANGUAGE_OK;
    }
    if (active->opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD))
        status = machine_language_operand_integer(walk, &source, after, active->end, &bank, &after, &constant);
    if (status == MACHINE_LANGUAGE_ASKS)
        return status;
    if (active->opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD) && (status != MACHINE_LANGUAGE_OK || !constant))
    {
        field->builds = false;
        field->deferred = true;
        return MACHINE_LANGUAGE_OK;
    }
    field->bank_value = (uint32_t) bank;
    return MACHINE_LANGUAGE_OK;
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
        walk, advanced_configuration_and_power_interface_machine_language_field_unit_build(load, index, &unit),
        walk->pc);
}

/**
 * @brief The 32 bits of the four little-endian bytes at @p bytes.
 */
static uint32_t machine_language_little_endian_32(const uint8_t *bytes)
{
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8u) | ((uint32_t) bytes[2] << 16u) |
           ((uint32_t) bytes[3] << 24u);
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
        const uint32_t block = machine_language_little_endian_32(fields - 5);

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

#define MACHINE_LANGUAGE_IS_DECLARATION(opcode, type, value)                                                           \
    (value) == MACHINE_LANGUAGE_OPCODE(opcode) || /**< One row of MACHINE_LANGUAGE_DECLARES. */
#define MACHINE_LANGUAGE_DECLARES(value)                                                                               \
    (MACHINE_LANGUAGE_DECLARATIONS(MACHINE_LANGUAGE_IS_DECLARATION, value)(value) == MACHINE_LANGUAGE_OPCODE(SCOPE) || \
     (value) == MACHINE_LANGUAGE_OPCODE(ALIAS) || (value) == MACHINE_LANGUAGE_OPCODE(FIELD) ||                         \
     (value) == MACHINE_LANGUAGE_OPCODE(INDEX_FIELD) ||                                                                \
     (value) == MACHINE_LANGUAGE_OPCODE(BANK_FIELD)) /**< Whether the opcode @p value declares a name: those of        \
                                                        MACHINE_LANGUAGE_DECLARATIONS, and a Scope, an Alias or a      \
                                                        field list. */
#define MACHINE_LANGUAGE_INERT(opcode)                                                                                 \
    ((opcode) == MACHINE_LANGUAGE_OPCODE(NOOP) ||                                                                      \
     ((opcode) >= MACHINE_LANGUAGE_OPCODE(LOCAL0) &&                                                                   \
      (opcode) <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6))) /**< Whether @p opcode, not data, runs nothing: Noop, a local   \
                                                          or an argument. */
#define MACHINE_LANGUAGE_REFUSED_DECLARATION(opcode)                                                                   \
    ((opcode) == MACHINE_LANGUAGE_OPCODE(DATA_REGION)) /**< Whether @p opcode declares a name in a statement the       \
                                                          interpreter refuses to run: DataTableRegion, decoded only    \
                                                          (§1.4), whose statement fails (§4.5). */
#define MACHINE_LANGUAGE_CODE_ENTRY(identifier, encoding, name, package_length, statement, data_object, ...)           \
    [MACHINE_LANGUAGE_OPCODE(identifier)] =                                                                            \
        MACHINE_LANGUAGE_REFUSED_DECLARATION(MACHINE_LANGUAGE_OPCODE(identifier)) ||                                   \
        !((data_object) || MACHINE_LANGUAGE_DECLARES(MACHINE_LANGUAGE_OPCODE(identifier)) ||                           \
          MACHINE_LANGUAGE_INERT(MACHINE_LANGUAGE_OPCODE(identifier))), /**< The entry of                              \
                                                                           machine_language_code_opcodes for a row of  \
                                                                           the opcode X-macros. */

/**
 * @brief Whether a statement of each opcode is code the interpreter runs: one that is neither a declaration, data, a
 *        local, an argument nor Noop, and a declaration it refuses to run, which then fails; the invalid opcode is.
 */
static const bool
    machine_language_code_opcodes[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT] = {
        [MACHINE_LANGUAGE_OPCODE(INVALID)] = true,
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_CODE_ENTRY)
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(MACHINE_LANGUAGE_CODE_ENTRY)};

/**
 * @brief Whether a statement is code the interpreter runs, as machine_language_code_opcodes says of its opcode, an
 *        opcode past the last one taken as the invalid one. A name there is a call or a reference to run.
 */
static bool machine_language_is_code(bool name, uint32_t opcode)
{
    if (opcode >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT)
        opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    return name || machine_language_code_opcodes[opcode];
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
        return machine_language_name_term(walk, kind != MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME));
    }

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        walk->bytes, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    walk->fault_opcode = opcode;
    if (kind == MACHINE_LANGUAGE_OPERAND(NONE))
        machine_language_count_table_level_code(walk, false, opcode);
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
 * @brief Hands the interpreter the TermList of the scope-opening statement @p active declares in a table's code, to run
 *        in @p scope, as a load whose general_load is set does: the walk goes on past the statement, and stops there.
 */
static MachineLanguageStatus_t machine_language_give_list_back(MachineLanguageWalk_t *walk,
                                                               const MachineLanguagePendingOperator_t *active,
                                                               uint32_t scope)
{
    walk->declaration->body = walk->pc;
    walk->declaration->scope = scope;
    --walk->pending_count;
    machine_language_count_stepped_over(walk, walk->pc, active->end);
    walk->pc = active->end;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Enters the TermList of the scope-opening statement @p active declares in a table's code, to run in @p scope,
 *        where the interpreter would run it (O21): its construct opened in the interpreter's frame, the walk's first
 *        construct becomes that TermList, whose statements the walk takes up to one the interpreter runs, and which it
 *        leaves at its end.
 */
static MachineLanguageStatus_t
machine_language_enter_list(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *active, uint32_t scope)
{
    MachineLanguageBlock_t *block = machine_language_top_block(walk);
    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_enter_table_scope(walk->load, active->start,
                                                                                      active->end, scope);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    --walk->pending_count;
    block->scope = scope;
    block->end = active->end;
    block->start = active->start;
    ++walk->entered;
    ++walk->load->scopes_entered;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Leaves, at its end, the TermList the walk entered last (O21): the interpreter's construct for it closed, the
 *        walk's first construct becomes the one around it again.
 */
static void machine_language_leave_list(MachineLanguageWalk_t *walk)
{
    MachineLanguageBlock_t *block = machine_language_top_block(walk);

    advanced_configuration_and_power_interface_machine_language_leave_table_scope(walk->load, &block->scope,
                                                                                  &block->end);
    --walk->entered;
    ++walk->load->scopes_left;
}

/**
 * @brief Opens the list that ends the active operator: a TermList, a FieldList or a
 *        PackageElementList, run as a construct up to the operator's end. A method body is skipped
 *        but in the decoding pass, and the body of a duplicate declaration in every pass. The TermList
 *        of a statement whose code the interpreter runs, which opens a scope, is entered for the interpreter
 *        (O21), or left to it by a load whose general_load is set.
 *
 * @return OK, a fault, or ASKS when a BankField waits for the interpreter to run its BankValue.
 */
static MachineLanguageStatus_t
machine_language_open_list(MachineLanguageWalk_t *walk,
                           AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    const MachineLanguagePendingOperator_t active = walk->pending[walk->pending_count - 1u];
    const MachineLanguageBlock_t *around = machine_language_top_block(walk);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    uint32_t scope = around->scope;
    uint8_t placement =
        (around->placement == MACHINE_LANGUAGE_BLOCK_NOT_RUN) ? MACHINE_LANGUAGE_BLOCK_NOT_RUN :
        (around->placement != 0u) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_IN_METHOD :
                                    0u;

    if (kind == MACHINE_LANGUAGE_OPERAND(TERM_LIST) && walk->duplicate_next)
    {
        machine_language_forget_marks(walk);
        --walk->pending_count;
        machine_language_count_stepped_over(walk, walk->pc, active.end);
        walk->pc = active.end;
        return MACHINE_LANGUAGE_OK;
    }
    if (active.opcode == MACHINE_LANGUAGE_OPCODE(METHOD))
    {
        machine_language_record_method(walk, &active);
        if (walk->pass != MACHINE_LANGUAGE_PASS_DECODE)
        {
            --walk->pending_count;
            machine_language_count_stepped_over(walk, walk->pc, active.end);
            walk->pc = active.end;
            return MACHINE_LANGUAGE_OK;
        }
        if (load->frame_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FRAMES)
            return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);
        placement = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY;
    }
    const bool opens_scope =
        (advanced_configuration_and_power_interface_machine_language_descriptor(active.opcode)->flags &
         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_OPENS_SCOPE) != 0u &&
        active.node != 0u;

    if (opens_scope)
        scope = active.node;
    if (walk->block_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BLOCKS ||
        (walk->declaration != NULL && opens_scope && walk->block_count == 1u &&
         load->evaluation.block_count ==
             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS))
        return MACHINE_LANGUAGE_STATUS(LIMIT_BLOCKS);
    if (kind == MACHINE_LANGUAGE_OPERAND(FIELD_LIST))
    {
        const MachineLanguageStatus_t status = machine_language_begin_field_list(walk, &active);

        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    if (machine_language_builds(walk, active.node))
        machine_language_build_scope_object(walk, &active);
    if (walk->declaration != NULL && opens_scope && walk->block_count == 1u)
        return load->general_load ? machine_language_give_list_back(walk, &active, scope) :
                                    machine_language_enter_list(walk, &active, scope);
    if (placement == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BLOCK_METHOD_BODY)
    {
        load->frame_methods[load->frame_count] = active.node;
        load->temporary_bases[load->frame_count++] = (uint16_t) load->temporary_count;
    }
    --walk->pending_count;

    MachineLanguageBlock_t *block = &walk->load->blocks[walk->block_count++];

    walk->top = block;

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
 * @brief Whether the three characters after the first of the NameSeg at @p offset may follow it: `A` to `Z`, `0` to
 *        `9` or `_`.
 *
 * @details Always inlined, as machine_language_holds_name_segment, and into the check of kept data, which knows the
 *          first character from its class.
 */
__attribute__((always_inline)) static inline bool machine_language_continues_name_segment(const uint8_t *bytes,
                                                                                          uint32_t offset)
{
    const uint8_t *characters = advanced_configuration_and_power_interface_machine_language_segment_characters;

    return (characters[bytes[offset + 1u]] & characters[bytes[offset + 2u]] & characters[bytes[offset + 3u]] &
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER) != 0u;
}

/**
 * @brief Whether the @p limit bytes from @p offset hold a NameString of one NameSeg, as the general NameString
 *        reader reads one: four characters of a NameSeg, the first a letter or `_`.
 *
 * @details Always inlined: the declaration decoder checks the name of almost every statement through it, and the
 *          builder of kept data each NameSeg element, and a call costs them more than the four bytes it reads.
 */
__attribute__((always_inline)) static inline bool machine_language_holds_name_segment(const uint8_t *bytes,
                                                                                      uint32_t offset, uint32_t limit)
{
    return limit - offset >= 4u &&
           advanced_configuration_and_power_interface_machine_language_starts_segment(bytes[offset]) &&
           machine_language_continues_name_segment(bytes, offset);
}

/**
 * @brief The class of each lead byte a data object of constants holds (O20): an integer constant's width, or what
 *        the byte starts; 0 for any other byte, whose object only the general path builds.
 */
static const uint8_t machine_language_data_classes[256] = {
    [0x00u] = 1u,
    [0x01u] = 1u,
    [0xFFu] = 1u,
    [0x0Au] = 2u,
    [0x0Bu] = 3u,
    [0x0Cu] = 5u,
    [0x0Eu] = 9u,
    [0x0Du] = MACHINE_LANGUAGE_DATA_STRING,
    [0x11u] = MACHINE_LANGUAGE_DATA_BUFFER,
    [0x12u] = MACHINE_LANGUAGE_DATA_PACKAGE,
    [0x13u] = MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE,
    ['\\'] = MACHINE_LANGUAGE_DATA_PREFIX,
    ['^'] = MACHINE_LANGUAGE_DATA_PREFIX,
    [0x2Eu] = MACHINE_LANGUAGE_DATA_PREFIX,
    [0x2Fu] = MACHINE_LANGUAGE_DATA_PREFIX,
    ['A'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['B'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['C'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['D'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['E'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['F'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['G'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['H'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['I'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['J'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['K'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['L'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['M'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['N'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['O'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['P'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['Q'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['R'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['S'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['T'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['U'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['V'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['W'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['X'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['Y'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['Z'] = MACHINE_LANGUAGE_DATA_SEGMENT,
    ['_'] = MACHINE_LANGUAGE_DATA_SEGMENT,
};

/**
 * @brief The Integer the constant of class @p class at @p pc holds, masked by @p mask: Zero, One, Ones, or the
 *        little-endian bytes after its prefix, read in 32-bit halves, which i686 assembles without 64-bit shifts; a
 *        mask keeps at least 32 bits, so only Ones and a quad word need it.
 */
static uint64_t machine_language_data_integer(const uint8_t *bytes, uint32_t pc, uint8_t class, uint64_t mask)
{
    const uint8_t *data = bytes + pc + 1u;

    switch (class)
    {
    case 1u: return ((bytes[pc] == 0xFFu) ? UINT64_MAX : (uint64_t) (bytes[pc] == 0x01u)) & mask;
    case 2u: return data[0];
    case 3u: return (uint32_t) data[0] | ((uint32_t) data[1] << 8u);
    case 5u: return machine_language_little_endian_32(data);
    default:
        return (((uint64_t) machine_language_little_endian_32(data + 4u) << 32u) |
                machine_language_little_endian_32(data)) &
               mask;
    }
}

/**
 * @brief Reads a Buffer's size or a VarPackage's element count at @p offset, before @p end, when it is a literal
 *        constant: Zero, One, or a byte, word or double word constant; false for any other term, which the
 *        general path reads.
 *
 * @details Always inlined, as machine_language_data_head, which calls it once per Buffer or VarPackage head.
 */
__attribute__((always_inline)) static inline bool machine_language_data_count(const uint8_t *bytes, uint32_t offset,
                                                                              uint32_t end, uint32_t *out_count,
                                                                              uint32_t *out_after)
{
    if (offset >= end)
        return false;

    const uint8_t lead = bytes[offset];
    const uint32_t width = (lead == 0x0Au) ? 1u : (lead == 0x0Bu) ? 2u : (lead == 0x0Cu) ? 4u : 0u;
    uint32_t count = (lead == 0x01u) ? 1u : 0u;

    if ((width == 0u && lead > 0x01u) || end - offset - 1u < width)
        return false;
    for (uint32_t byte = 0u; byte < width; ++byte)
        count |= (uint32_t) bytes[offset + 1u + byte] << (8u * byte);
    *out_count = count;
    *out_after = offset + 1u + width;
    return true;
}

/**
 * @brief Reads the head of the Buffer, Package or VarPackage of class @p class whose opcode is at @p offset: its
 *        PkgLength, which must end by @p end, then its size or its element count, a literal constant;
 *        @p out_end receives where the object ends, @p out_first where its initialiser or its elements start.
 *
 * @details Always inlined into the load, the check, the reader and the description of kept data: the load reads the
 *          head of each data object it keeps through it, and the builder of kept data every Buffer and package head,
 *          and a call costs them more than the head they read.
 */
__attribute__((always_inline)) static inline bool machine_language_data_head(const uint8_t *bytes, uint32_t offset,
                                                                             uint32_t end, uint8_t class,
                                                                             uint32_t *out_end, uint32_t *out_first,
                                                                             uint32_t *out_count)
{
    if (advanced_configuration_and_power_interface_machine_language_read_package_end(bytes, offset + 1u, end, out_end,
                                                                                     out_first) != MACHINE_LANGUAGE_OK)
        return false;
    if (class != MACHINE_LANGUAGE_DATA_PACKAGE)
        return machine_language_data_count(bytes, *out_first, *out_end, out_count, out_first);
    if (*out_first >= *out_end)
        return false;
    *out_count = bytes[(*out_first)++];
    return true;
}

/**
 * @brief Reads the head of the Package or VarPackage of class @p class at @p offset, whose parent ends at @p end, as
 *        machine_language_data_head reads it, its element count within the 4,096 of §6.1; a Package whose PkgLength
 *        is one byte is read without it.
 *
 * @details Always inlined into the check of kept data, which reads every inner package's head through it: a package
 *          under 64 bytes has a one-byte PkgLength, and the general head reader costs it more than its three bytes.
 */
__attribute__((always_inline)) static inline bool machine_language_package_head(const uint8_t *bytes, uint32_t offset,
                                                                                uint32_t end, uint8_t class,
                                                                                uint32_t *out_end, uint32_t *out_first,
                                                                                uint32_t *out_count)
{
    const uint32_t length = (class == MACHINE_LANGUAGE_DATA_PACKAGE && end - offset >= 3u) ? bytes[offset + 1u] : 0u;

    if ((length & 0xC0u) == 0u && length >= 2u && length <= end - offset - 1u)
    {
        *out_end = offset + 1u + length;
        *out_first = offset + 3u;
        *out_count = bytes[offset + 2u];
        return true;
    }
    return machine_language_data_head(bytes, offset, end, class, out_end, out_first, out_count) &&
           *out_count <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS;
}

/**
 * @brief Checks the Buffer whose head machine_language_data_head has read, its size @p count and its initialiser from
 *        @p first to @p end, as evaluate_data would build it: true within 64 KiB.
 */
static bool machine_language_buffer_fits(uint32_t count, uint32_t first, uint32_t end)
{
    return count <= MACHINE_LANGUAGE_MAX_BUFFER_BYTES && end - first <= MACHINE_LANGUAGE_MAX_BUFFER_BYTES;
}

/**
 * @brief Reads the head of the Buffer, Package or VarPackage of class @p class nested at @p offset of @p source, whose
 *        parent ends at @p end, as evaluate_data reads it: by machine_language_data_head or
 *        machine_language_package_head when its size or count is a literal constant, else by evaluate_integer, a name
 *        resolved in @p space from the source's scope, which then sets @p out_named; a VarPackage within the 4,096
 *        elements of §6.1. @p out_end receives where it ends, @p out_first where its initialiser or its elements
 *        start, @p out_count its size or count.
 *
 * @return Whether the head reads with no interpreter and no fault.
 */
static bool machine_language_nested_head(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                         const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t *source,
                                         uint32_t offset, uint32_t end, uint8_t class, uint32_t *out_end,
                                         uint32_t *out_first, uint32_t *out_count, bool *out_named)
{
    uint64_t count = 0u;
    bool constant = false;

    if ((class == MACHINE_LANGUAGE_DATA_BUFFER) ?
            machine_language_data_head(source->bytes, offset, end, class, out_end, out_first, out_count) :
            machine_language_package_head(source->bytes, offset, end, class, out_end, out_first, out_count))
        return true;
    if (class == MACHINE_LANGUAGE_DATA_PACKAGE ||
        advanced_configuration_and_power_interface_machine_language_read_package_end(
            source->bytes, offset + 1u, end, out_end, out_first) != MACHINE_LANGUAGE_OK ||
        *out_first >= *out_end)
        return false;

    const bool named =
        advanced_configuration_and_power_interface_machine_language_starts_name(source->bytes[*out_first]);

    if (advanced_configuration_and_power_interface_machine_language_evaluate_integer(
            space, source, *out_first, *out_end, &count, out_first, &constant) != MACHINE_LANGUAGE_OK ||
        !constant || count > UINT32_MAX ||
        (class == MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE &&
         count > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS))
        return false;
    *out_count = (uint32_t) count;
    *out_named = *out_named || named;
    return true;
}

/**
 * @brief Checks the Package, VarPackage or Buffer at @p offset of @p source, whose statement ends at @p limit, as
 *        evaluate_data would build it now, without building it (O20): true when it builds with no interpreter and no
 *        fault, its packages within the levels of §6.1, its strings and buffers within 64 KiB. A nested Buffer's size
 *        or VarPackage's count that is no literal constant is read as machine_language_nested_head reads it, which sets
 *        @p out_named when a name gives one; any other data object is refused.
 *
 * @details The open package's end stays in a local, and only the packages around it on the stack.
 */
static bool machine_language_check_data(AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space,
                                        const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t *source,
                                        uint32_t offset, uint32_t limit, bool *out_named)
{
    const uint8_t *bytes = source->bytes;
    uint32_t outer_ends[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    uint32_t outer = 0u;
    uint32_t end = 0u;
    uint32_t first = 0u;
    uint32_t count = 0u;

    if (offset >= limit)
        return false;

    const uint8_t top = machine_language_data_classes[bytes[offset]];

    if (top == MACHINE_LANGUAGE_DATA_BUFFER)
        return machine_language_data_head(bytes, offset, limit, top, &end, &first, &count) &&
               machine_language_buffer_fits(count, first, end);
    if ((top != MACHINE_LANGUAGE_DATA_PACKAGE && top != MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE) ||
        !machine_language_package_head(bytes, offset, limit, top, &end, &first, &count))
        return false;

    uint32_t pc = first;

    for (;;)
    {
        if (pc == end)
        {
            if (outer == 0u)
                return true;
            end = outer_ends[--outer];
            continue;
        }

        const uint8_t class = machine_language_data_classes[bytes[pc]];

        if ((class & MACHINE_LANGUAGE_DATA_WIDTH) != 0u)
        {
            if (end - pc < class)
                return false;
            pc += class;
            continue;
        }
        if (class == MACHINE_LANGUAGE_DATA_SEGMENT)
        {
            if (end - pc < 4u || !machine_language_continues_name_segment(bytes, pc))
                return false;
            pc += 4u;
            continue;
        }

        uint32_t inner_end = 0u;
        uint32_t inner_count = 0u;

        if (class == MACHINE_LANGUAGE_DATA_PACKAGE || class == MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE)
        {
            if (outer + 1u == MACHINE_LANGUAGE_MAX_NESTED_PACKAGES ||
                !machine_language_nested_head(space, source, pc, end, class, &inner_end, &first, &inner_count,
                                              out_named))
                return false;
            outer_ends[outer++] = end;
            end = inner_end;
            pc = first;
            continue;
        }
        if (class == MACHINE_LANGUAGE_DATA_PREFIX)
        {
            MachineLanguageNameString_t name;

            if (advanced_configuration_and_power_interface_machine_language_read_name_string(
                    bytes, pc, end, &name, &first) != MACHINE_LANGUAGE_OK)
                return false;
            pc = first;
        }
        else if (class == MACHINE_LANGUAGE_DATA_STRING)
        {
            first = ++pc;
            while (pc < end && bytes[pc] != 0x00u)
                ++pc;
            if (pc == end || pc - first > MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
                return false;
            ++pc;
        }
        else if (class == MACHINE_LANGUAGE_DATA_BUFFER)
        {
            if (!machine_language_nested_head(space, source, pc, end, class, &inner_end, &first, &inner_count,
                                              out_named) ||
                !machine_language_buffer_fits(inner_count, first, inner_end))
                return false;
            pc = inner_end;
        }
        else
            return false;
    }
}

/**
 * @brief Keeps in its table the data object at @p offset, before @p limit, of the Name whose node @p index the
 *        declaration pass has just created (O20): a Package, a VarPackage or a Buffer whose head reads, its size or
 *        count a literal constant, a VarPackage within the 4,096 elements and a Buffer within the 64 KiB of §6.1, where
 *        the walk has room for every package it could nest and the node's parent is the scope its names are read in.
 *        Every load keeps it, with its shortcuts or without: its elements are neither checked nor built, until a name
 *        resolution reaches it (C4), and a debug kernel and the host tools check them once the bodies are decoded. The
 *        node takes the object's type, and its place in the table as its value, and the walk goes on past it.
 *
 * @return Whether it kept the object.
 */
static bool machine_language_keeps_data(MachineLanguageWalk_t *walk, uint32_t index, uint32_t offset, uint32_t limit)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    MachineLanguageNode_t *node = machine_language_node(walk, index);
    const uint8_t class = machine_language_data_classes[walk->bytes[offset]];
    uint32_t after = 0u;
    uint32_t first = 0u;
    uint32_t count = 0u;

    if ((class != MACHINE_LANGUAGE_DATA_BUFFER && class != MACHINE_LANGUAGE_DATA_PACKAGE &&
         class != MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE) ||
        node->parent != machine_language_top_block(walk)->scope || !machine_language_room_for_packages(walk) ||
        !machine_language_data_head(walk->bytes, offset, limit, class, &after, &first, &count) ||
        !((class == MACHINE_LANGUAGE_DATA_BUFFER) ?
              machine_language_buffer_fits(count, first, after) :
              count <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS))
        return false;
    node->type =
        (class == MACHINE_LANGUAGE_DATA_BUFFER) ? MACHINE_LANGUAGE_NODE(BUFFER) : MACHINE_LANGUAGE_NODE(PACKAGE);
    node->value.kind = MACHINE_LANGUAGE_VALUE(KEPT_DATA);
    node->value.flags = (machine_language_source(walk).integer_bits == 32u) ?
                            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_KEPT_DATA_32_BIT_INTEGERS :
                            0u;
    node->value.auxiliary = (uint16_t) (walk->table + 1u);
    node->value.handle = 0u;
    node->value.payload = offset;
    ++load->data_kept;
    ++load->tables[walk->table].data_kept;
    machine_language_count_stepped_over(walk, first, after);
    walk->pc = after;
    return true;
}

/**
 * @brief Builds into node @p index, which the declaration pass has just created, the integer constant a Name's data
 *        object at @p offset is, as the general data decoder builds it, when its bytes end by @p limit and the walk
 *        could step over it (O18): Zero, One, Ones, or a byte, word, double word or quad word constant, masked to the
 *        load's integers. The node takes the Integer, and the walk goes on past it.
 *
 * @return Whether it built the object; any other object, and one @p limit cuts, is left to the general path.
 */
static bool machine_language_integer_data(MachineLanguageWalk_t *walk, uint32_t index, uint32_t offset, uint32_t limit)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = walk->load;
    const uint8_t class = machine_language_data_classes[walk->bytes[offset]];
    const uint64_t mask = (load->integer_bits == 32u) ? 0xFFFFFFFFull : UINT64_MAX;

    if ((class & MACHINE_LANGUAGE_DATA_WIDTH) == 0u || limit - offset < class || !machine_language_room_for_data(walk))
        return false;

    MachineLanguageNode_t *node = machine_language_node(walk, index);

    node->value = (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){
        .kind = MACHINE_LANGUAGE_VALUE(INTEGER),
        .payload = machine_language_data_integer(walk->bytes, offset, class, mask),
    };
    node->type = MACHINE_LANGUAGE_NODE(INTEGER);
    ++load->integers_built_directly;
    ++load->data_stepped_over;
    walk->pc = offset + class;
    return true;
}

/**
 * @brief Takes the data object at the walk's pc of the Name @p active declares, as the declaration pass's general path
 *        takes it, the operand of kind @p kind: kept in its table and stepped over (O20), as every load does; else
 *        built, then stepped over (O18) or decoded as terms.
 */
static MachineLanguageStatus_t
machine_language_name_data(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *active,
                           AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    uint32_t data_end = 0u;

    if (walk->pc < active->end && machine_language_keeps_data(walk, active->node, walk->pc, active->end))
        return MACHINE_LANGUAGE_OK;

    const MachineLanguageStatus_t status = machine_language_build_name(walk, active, &data_end);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (!machine_language_steps_over_data(walk, data_end))
        return machine_language_term(walk, kind);
    ++walk->load->data_stepped_over;
    walk->pc = data_end;
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
        status = machine_language_use_operand_name(walk, active, &name);
        if (status != MACHINE_LANGUAGE_OK)
            return status;
        walk->pc = after;
        if (machine_language_declared_type(active->opcode) == MACHINE_LANGUAGE_NODE(BUFFER_FIELD) &&
            machine_language_builds(walk, active->node))
            return machine_language_build_buffer_field(walk, active);
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPERAND(BYTE_LIST): walk->pc = active->end; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPERAND(TERM_LIST):
    case MACHINE_LANGUAGE_OPERAND(FIELD_LIST):
    case MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST): return machine_language_open_list(walk, kind);
    default:
        if (active->opcode == MACHINE_LANGUAGE_OPCODE(NAME) && active->operands_done == 2u &&
            machine_language_builds(walk, active->node))
            return machine_language_name_data(walk, active, kind);
        if (active->opcode == MACHINE_LANGUAGE_OPCODE(OPERATION_REGION) && active->operands_done == 3u &&
            machine_language_builds(walk, active->node))
            status = machine_language_build_region(walk, active);
        return (status == MACHINE_LANGUAGE_OK) ? machine_language_term(walk, kind) : status;
    }
}

/**
 * @brief Puts @p state on the walk's operator stack, as push_operator would have pushed it, its operands
 *        taken so far included, so that the walk goes on with the rest of the statement; machine_language_statement
 *        has checked that the stack has room for it.
 */
static void machine_language_hand_back(MachineLanguageWalk_t *walk, const MachineLanguagePendingOperator_t *state)
{
    walk->pending[walk->pending_count++] = *state;
    if (walk->pending_count > walk->load->pending_peak)
        walk->load->pending_peak = walk->pending_count;
}

/**
 * @brief Takes `Name (NameSeg, data)` at @p start, whose statement ends at @p limit, as the walk would: the
 *        name declared through use_name, then the data object built and stepped over (O18), an integer constant
 *        built here, a package or a buffer kept in its table (O20); the general path takes the rest of a statement
 *        from the first step whose outcome differs.
 */
static MachineLanguageStatus_t machine_language_name_statement(MachineLanguageWalk_t *walk, uint32_t start,
                                                               uint32_t limit)
{
    MachineLanguagePendingOperator_t state = {
        .opcode = (uint16_t) MACHINE_LANGUAGE_OPCODE(NAME),
        .operands_done = 1u,
        .operand_count = 2u,
        .start = start,
        .end = limit,
    };
    const MachineLanguageNameString_t name = {.segments = start + 1u, .segment_count = 1u};
    uint32_t after = 0u;

    walk->fault_opcode = MACHINE_LANGUAGE_OPCODE(NAME);
    walk->pc = start + 1u;

    MachineLanguageStatus_t status = machine_language_use_name(walk, &state, &name);

    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_hand_back(walk, &state);
        return status;
    }
    walk->pc = start + 5u;
    state.operands_done = 2u;
    if (machine_language_builds(walk, state.node) &&
        (machine_language_integer_data(walk, state.node, start + 5u, limit) ||
         machine_language_keeps_data(walk, state.node, start + 5u, limit)))
    {
        ++walk->load->declarations_decoded_directly;
        return MACHINE_LANGUAGE_OK;
    }
    if (machine_language_builds(walk, state.node))
        status = machine_language_build_name(walk, &state, &after);
    if (status == MACHINE_LANGUAGE_OK && machine_language_steps_over_data(walk, after))
    {
        ++walk->load->data_stepped_over;
        ++walk->load->declarations_decoded_directly;
        walk->pc = after;
        return MACHINE_LANGUAGE_OK;
    }
    machine_language_hand_back(walk, &state);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(TERM_ARGUMENT)) :
                                             status;
}

/**
 * @brief Takes `Method (NameSeg, flags) {...}` at @p start, whose PkgLength ends at @p end and whose NameString
 *        starts at @p name_start, as the walk would: the name declared through use_name, the flags read, the
 *        body recorded and skipped; the general path takes the rest of a duplicate's statement, and of one whose
 *        flags are cut.
 */
static MachineLanguageStatus_t machine_language_method_statement(MachineLanguageWalk_t *walk, uint32_t start,
                                                                 uint32_t end, uint32_t name_start)
{
    MachineLanguagePendingOperator_t state = {
        .opcode = (uint16_t) MACHINE_LANGUAGE_OPCODE(METHOD),
        .operands_done = 1u,
        .operand_count = 3u,
        .start = start,
        .end = end,
    };
    const MachineLanguageNameString_t name = {.segments = name_start, .segment_count = 1u};

    walk->fault_opcode = MACHINE_LANGUAGE_OPCODE(METHOD);
    walk->pc = name_start;

    const MachineLanguageStatus_t status = machine_language_use_name(walk, &state, &name);

    if (status != MACHINE_LANGUAGE_OK || walk->duplicate_next || end - (name_start + 4u) < 1u)
    {
        walk->pc = (status != MACHINE_LANGUAGE_OK) ? name_start : name_start + 4u;
        machine_language_hand_back(walk, &state);
        return status;
    }
    walk->pc = name_start + 5u;
    machine_language_record_method(walk, &state);
    machine_language_count_stepped_over(walk, walk->pc, end);
    walk->pc = end;
    ++walk->load->declarations_decoded_directly;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Whether the offset and the length of an OperationRegion, from @p start within @p limit, are both integers
 *        that need no interpreter, as machine_language_build_region reads them: @p out_offset and @p out_length then
 *        receive them, and @p out_after the offset past the length.
 */
static bool machine_language_region_operands(MachineLanguageWalk_t *walk, uint32_t start, uint32_t limit,
                                             uint64_t *out_offset, uint64_t *out_length, uint32_t *out_after)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = machine_language_source(walk);
    uint32_t after = 0u;
    bool offset_known = false;
    bool length_known = false;

    return advanced_configuration_and_power_interface_machine_language_evaluate_integer(
               &walk->load->space, &source, start, limit, out_offset, &after, &offset_known) == MACHINE_LANGUAGE_OK &&
           offset_known &&
           advanced_configuration_and_power_interface_machine_language_evaluate_integer(
               &walk->load->space, &source, after, limit, out_length, out_after, &length_known) ==
               MACHINE_LANGUAGE_OK &&
           length_known;
}

/**
 * @brief Takes `OperationRegion (NameSeg, RegionSpace, offset, length)` at @p start, whose statement ends at @p limit,
 *        as the walk would: the name declared through use_name, then, when the declaration pass builds the region and
 *        its offset and length are integers that need no interpreter, the two read once and the object placed as
 *        machine_language_build_region places it. The general path takes any other region from the first operand
 *        whose outcome differs, its NameString when the name cannot be used and its RegionSpace otherwise; an object
 *        the arena has no room for stops the table, and one the heap refuses otherwise is counted, as from the walk.
 */
static MachineLanguageStatus_t machine_language_region_statement(MachineLanguageWalk_t *walk, uint32_t start,
                                                                 uint32_t limit)
{
    MachineLanguagePendingOperator_t state = {
        .opcode = (uint16_t) MACHINE_LANGUAGE_OPCODE(OPERATION_REGION),
        .operands_done = 1u,
        .operand_count = 4u,
        .start = start,
        .end = limit,
    };
    const MachineLanguageNameString_t name = {.segments = start + 2u, .segment_count = 1u};
    uint64_t offset = 0u;
    uint64_t length = 0u;
    uint32_t after = 0u;

    walk->fault_opcode = MACHINE_LANGUAGE_OPCODE(OPERATION_REGION);
    walk->pc = start + 2u;

    MachineLanguageStatus_t status = machine_language_use_name(walk, &state, &name);

    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_hand_back(walk, &state);
        return status;
    }
    walk->pc = start + 6u;
    if (!machine_language_builds(walk, state.node) ||
        !machine_language_region_operands(walk, start + 7u, limit, &offset, &length, &after))
    {
        machine_language_hand_back(walk, &state);
        return MACHINE_LANGUAGE_OK;
    }
    status = machine_language_build_fault(
        walk, machine_language_place_region(walk, state.node, walk->bytes[start + 6u], true, offset, length), start);
    if (status == MACHINE_LANGUAGE_OK)
    {
        walk->pc = after;
        ++walk->load->declarations_decoded_directly;
    }
    return status;
}

/**
 * @brief Reads the NameString at @p offset, before @p limit, as the general NameString reader reads it, and one of
 *        a single NameSeg without calling it; @p out_name and @p out_after receive what that reader gives.
 *
 * @details Always inlined into the decoder of list statements: most of their names are one NameSeg, and the call costs
 *          them more than the four bytes it checks.
 */
__attribute__((always_inline)) static inline MachineLanguageStatus_t
machine_language_read_name(const uint8_t *bytes, uint32_t offset, uint32_t limit, MachineLanguageNameString_t *out_name,
                           uint32_t *out_after)
{
    if (!machine_language_holds_name_segment(bytes, offset, limit))
        return advanced_configuration_and_power_interface_machine_language_read_name_string(bytes, offset, limit,
                                                                                            out_name, out_after);
    *out_name = (MachineLanguageNameString_t){.segments = offset, .segment_count = 1u};
    *out_after = offset + 4u;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Takes a statement whose operator @p opcode ends at @p end, whose NameString starts at @p name_start, and
 *        whose operands are that NameString, the @p fixed bytes of a field list's FieldFlags or none, then a list
 *        of @p kind, as the walk would: the name read and used through use_name, the fixed bytes passed, then the
 *        operator the general path holds put on the stack and its list opened by open_list. The general path
 *        takes the rest of a statement from the first operand whose outcome differs.
 */
static MachineLanguageStatus_t
machine_language_list_statement(MachineLanguageWalk_t *walk, uint32_t opcode, uint32_t start, uint32_t end,
                                uint32_t name_start, uint32_t fixed,
                                AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    MachineLanguagePendingOperator_t state = {
        .opcode = (uint16_t) opcode,
        .operands_done = 1u,
        .operand_count = advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->operand_count,
        .start = start,
        .end = end,
    };
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    MachineLanguageStatus_t status = machine_language_read_name(walk->bytes, name_start, end, &name, &after);

    walk->fault_opcode = opcode;
    walk->pc = name_start;
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_use_name(walk, &state, &name);
    if (status != MACHINE_LANGUAGE_OK || end - after < fixed)
    {
        walk->pc = (status != MACHINE_LANGUAGE_OK) ? name_start : after;
        machine_language_hand_back(walk, &state);
        return status;
    }
    walk->pc = after + fixed;
    state.operands_done = state.operand_count;
    machine_language_hand_back(walk, &state);
    ++walk->load->declarations_decoded_directly;
    return machine_language_open_list(walk, kind);
}

/**
 * @brief Takes a Scope, a Device, a ThermalZone or a Field, @p opcode, whose operator of @p lead bytes starts at
 *        @p start, its PkgLength read within @p limit, through machine_language_list_statement with @p fixed bytes
 *        after its NameString and a list of @p kind; one whose PkgLength the general path refuses, as a term.
 */
static MachineLanguageStatus_t
machine_language_package_statement(MachineLanguageWalk_t *walk, uint32_t opcode, uint32_t start, uint32_t lead,
                                   uint32_t limit, uint32_t fixed,
                                   AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t kind)
{
    uint32_t end = 0u;
    uint32_t after = 0u;

    if (advanced_configuration_and_power_interface_machine_language_read_package_end(
            walk->bytes, start + lead, limit, &end, &after) != MACHINE_LANGUAGE_OK)
        return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
    return machine_language_list_statement(walk, opcode, start, end, after, fixed, kind);
}

/**
 * @brief Takes the statement at the walk's pc, at a statement's place: in the declaration pass, unless the load
 *        takes no shortcut, a Name or a Method of one NameSeg, a Device, a ThermalZone, a Scope, a Field or an
 *        OperationRegion of one NameSeg through its own decoder (O19); any other statement, and one whose PkgLength
 *        or NameString the general path refuses, as a term.
 *
 * @details It also takes as a term a statement that finds the operator stack full. That cannot happen today: at a
 *          statement's place the declaration pass holds no operator pending, each construct's being popped as its
 *          list opens. The check stays because the decoder writes the stack, when it hands a statement back.
 */
static MachineLanguageStatus_t machine_language_statement(MachineLanguageWalk_t *walk)
{
    const uint8_t *bytes = walk->bytes;
    const uint32_t start = walk->pc;
    const uint32_t limit = machine_language_limit(walk);
    uint32_t end = 0u;
    uint32_t after = 0u;

    if (walk->pass != MACHINE_LANGUAGE_PASS_DECLARE || walk->load->general_load || start >= limit ||
        walk->pending_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PENDING_OPERATORS)
        return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
    if (bytes[start] == 0x08u && limit - start >= 6u && machine_language_holds_name_segment(bytes, start + 1u, limit))
        return machine_language_name_statement(walk, start, limit);
    if (bytes[start] == 0x14u &&
        advanced_configuration_and_power_interface_machine_language_read_package_end(bytes, start + 1u, limit, &end,
                                                                                     &after) == MACHINE_LANGUAGE_OK &&
        machine_language_holds_name_segment(bytes, after, end))
        return machine_language_method_statement(walk, start, end, after);
    if (bytes[start] == 0x10u)
        return machine_language_package_statement(walk, MACHINE_LANGUAGE_OPCODE(SCOPE), start, 1u, limit, 0u,
                                                  MACHINE_LANGUAGE_OPERAND(TERM_LIST));
    if (bytes[start] != 0x5Bu || limit - start < 2u)
        return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
    switch (bytes[start + 1u])
    {
    case 0x82u:
        return machine_language_package_statement(walk, MACHINE_LANGUAGE_OPCODE(DEVICE), start, 2u, limit, 0u,
                                                  MACHINE_LANGUAGE_OPERAND(TERM_LIST));
    case 0x85u:
        return machine_language_package_statement(walk, MACHINE_LANGUAGE_OPCODE(THERMAL_ZONE), start, 2u, limit, 0u,
                                                  MACHINE_LANGUAGE_OPERAND(TERM_LIST));
    case 0x81u:
        return machine_language_package_statement(walk, MACHINE_LANGUAGE_OPCODE(FIELD), start, 2u, limit, 1u,
                                                  MACHINE_LANGUAGE_OPERAND(FIELD_LIST));
    case 0x80u:
        if (machine_language_holds_name_segment(bytes, start + 2u, limit))
            return machine_language_region_statement(walk, start, limit);
        return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
    default: return machine_language_term(walk, MACHINE_LANGUAGE_OPERAND(NONE));
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
    if (walk->pc == block->end && walk->entered != 0u && walk->block_count == 1u)
    {
        machine_language_leave_list(walk);
        return MACHINE_LANGUAGE_OK;
    }
    if (walk->pc == block->end)
    {
        walk->closes_body =
            walk->closes_body || (walk->block_count == 2u && block->kind == MACHINE_LANGUAGE_OPERAND(TERM_LIST));
        machine_language_close_block(walk);
        return MACHINE_LANGUAGE_OK;
    }
    if (block->kind == MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST))
        return machine_language_package_element(walk);
    if (block->kind == MACHINE_LANGUAGE_OPERAND(FIELD_LIST))
        return machine_language_field_element(walk);
    return machine_language_statement(walk);
}

/**
 * @brief Starts a walk of table @p table from @p start, in a first construct that ends at @p end, in
 *        scope @p scope, placed as @p placement says: 0 at table level, or declared without running.
 */
static void machine_language_begin_walk(MachineLanguageWalk_t *walk,
                                        AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        uint32_t table, MachineLanguagePass_t pass, uint32_t start, uint32_t end,
                                        uint32_t scope, uint8_t placement)
{
    MachineLanguageBlock_t *block = &load->blocks[0];

    walk->load = load;
    walk->bytes = (const uint8_t *) load->tables[table].header;
    walk->table = table;
    walk->pc = start;
    walk->pending_count = 0u;
    walk->block_count = 1u;
    walk->pending = load->pending;
    walk->top = block;
    walk->fault_opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    walk->pass = pass;
    walk->last_created = 0u;
    walk->external_start = 0u;
    walk->undeclared_next = false;
    walk->undeclared_from = 0u;
    walk->duplicate_next = false;
    walk->declaration = NULL;
    walk->asked = 0u;
    walk->closes_body = false;
    walk->abandons = false;
    walk->entered = 0u;
    block->kind = MACHINE_LANGUAGE_OPERAND(NONE);
    block->placement = placement;
    block->pending_base = 0u;
    block->end = end;
    block->scope = scope;
    block->start = start;
    if (load->blocks_peak == 0u)
        load->blocks_peak = 1u;
}

/**
 * @brief Whether the statement at @p pc, before @p end, starts, by its lead bytes alone, as a Name, a Method, a Scope,
 *        a Device, a Field or an OperationRegion, the most frequent of the declarations the declaration pass's decoder
 *        takes (O19), none of which the interpreter runs.
 */
static bool machine_language_leads_declaration(const uint8_t *bytes, uint32_t pc, uint32_t end)
{
    switch (bytes[pc])
    {
    case 0x08u:
    case 0x10u:
    case 0x14u: return true;
    case 0x5Bu: return pc + 1u < end && (bytes[pc + 1u] == 0x80u || bytes[pc + 1u] == 0x81u || bytes[pc + 1u] == 0x82u);
    default: return false;
    }
}

/**
 * @brief Whether a walk's first construct ends at its pc, unless it is a TermList the walk entered (O21), which it
 *        leaves instead, or the statement there is one the interpreter runs: either ends the run of declarations the
 *        walk takes for the interpreter. A statement the declaration pass's decoder takes is told by its lead bytes,
 *        unless the load takes no shortcut, which reads its opcode as any other.
 */
static bool machine_language_code_follows(MachineLanguageWalk_t *walk)
{
    const MachineLanguageBlock_t *block = machine_language_top_block(walk);
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (walk->pc >= block->end)
        return walk->entered == 0u;
    if (!walk->load->general_load && machine_language_leads_declaration(walk->bytes, walk->pc, block->end))
        return false;
    if (advanced_configuration_and_power_interface_machine_language_starts_name(walk->bytes[walk->pc]))
        return true;
    return advanced_configuration_and_power_interface_machine_language_read_opcode(
               walk->bytes, walk->pc, block->end, &opcode, &after) == MACHINE_LANGUAGE_OK &&
           machine_language_is_code(false, opcode);
}

/**
 * @brief Whether a walk that takes statements for the interpreter stops at its pc: past its first statement,
 *        once a TermList is left to the interpreter by a load whose general_load is set, at the end of its first
 *        construct but for a TermList it entered (O21), or before a statement the interpreter runs; one that
 *        declares without running stops past its first statement.
 */
static bool machine_language_statements_end(MachineLanguageWalk_t *walk, uint32_t start)
{
    return walk->block_count == 1u && walk->pending_count == 0u && walk->pc != start &&
           (walk->declaration == NULL || walk->declaration->body != 0u || machine_language_code_follows(walk));
}

/**
 * @brief Takes the steps of a walk, past every fault, recorded, until its first construct ends; with
 *        @p statements, until it has taken the statements from @p start the interpreter hands the load. The bytes it
 *        went over count as decoded, but for those its steps take back as stepped over.
 *
 * @return OK, or ASKS when a statement waits for a term the interpreter runs.
 */
static MachineLanguageStatus_t machine_language_walk(MachineLanguageWalk_t *walk, uint32_t start, bool statements)
{
    const uint32_t from = walk->pc;

    while (walk->block_count != 0u && !(statements && machine_language_statements_end(walk, start)))
    {
        const MachineLanguageStatus_t status = machine_language_step(walk);

        if (status == MACHINE_LANGUAGE_ASKS)
        {
            machine_language_count_decoded(walk, from);
            return status;
        }
        if (status != MACHINE_LANGUAGE_OK)
        {
            machine_language_record_fault(walk, status);
            machine_language_recover(walk, status);
        }
    }
    machine_language_count_decoded(walk, from);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Walks table @p table of a load in one pass, from its first term, without running its code: the
 *        declaration pass declares every branch where the load declares every branch, else each branch as one
 *        whose predicate failed (§3.6).
 */
static void machine_language_walk_without_running(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                                  uint32_t table, MachineLanguagePass_t pass)
{
    const uint32_t start = (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t);
    const bool not_run = pass == MACHINE_LANGUAGE_PASS_DECLARE && !load->declares_every_branch;
    MachineLanguageWalk_t walk;

    machine_language_begin_walk(&walk, load, table, pass, start, load->tables[table].length,
                                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                                not_run ? MACHINE_LANGUAGE_BLOCK_NOT_RUN : 0u);
    (void) machine_language_walk(&walk, start, false);
}

/**
 * @brief Runs one pass over table @p table of a load the root table lists: the declaration pass, an evaluation of
 *        its own (§6.1), hands the table's code to the interpreter, but where every branch is declared and none
 *        run, or where the evaluation has no room for the table's frame, whose code is then declared without
 *        running (§3.6).
 */
static void machine_language_walk_table(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        uint32_t table, MachineLanguagePass_t pass)
{
    if (pass != MACHINE_LANGUAGE_PASS_DECLARE)
    {
        machine_language_walk_without_running(load, table, pass);
        return;
    }
    ++load->declaring;
    advanced_configuration_and_power_interface_machine_language_begin_evaluation(load);
    if (load->declares_every_branch ||
        advanced_configuration_and_power_interface_machine_language_run_table_term(
            load, table, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
            (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t),
            load->tables[table].length) != MACHINE_LANGUAGE_OK)
        machine_language_walk_without_running(load, table, pass);
    advanced_configuration_and_power_interface_machine_language_end_evaluation(load);
    --load->declaring;
}

#define MACHINE_LANGUAGE_FOLD_OFFSET 0x811C9DC5u /**< FNV-1a's offset basis. */
#define MACHINE_LANGUAGE_FOLD_PRIME  0x01000193u /**< FNV-1a's prime. */
#define MACHINE_LANGUAGE_FOLD_UNREADABLE                                                                               \
    0xFFFFFFFFu /**< What kept data whose bytes no longer read as data folds as: a count or a length no object has. */

_Static_assert((int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER ==
                       (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_INTEGER &&
                   (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING ==
                       (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_STRING &&
                   (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER ==
                       (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_BUFFER &&
                   (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE ==
                       (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PACKAGE,
               "an evaluation's result folds its kind as the type of a node holding the same data (§8.7)");

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
 * @brief Folds @p length zero bytes into @p fold.
 */
static uint32_t machine_language_fold_zeros(uint32_t fold, uint32_t length)
{
    for (uint32_t byte = 0u; byte < length; ++byte)
        fold *= MACHINE_LANGUAGE_FOLD_PRIME;
    return fold;
}

/**
 * @brief Folds, after @p fold, kept data whose bytes no longer read as data within its table: the count or length
 *        MACHINE_LANGUAGE_FOLD_UNREADABLE and nothing after it, wherever the bytes stopped reading.
 */
static uint32_t machine_language_fold_unreadable(uint32_t fold)
{
    return machine_language_fold_unsigned(fold, MACHINE_LANGUAGE_FOLD_UNREADABLE, 4u);
}

/**
 * @brief The kind of the value an element of kept data of class @p class builds into, which a package folds before
 *        each of its elements: an Integer, a String, a Buffer, a Package, or a name.
 */
static uint8_t machine_language_kept_kind(uint8_t class)
{
    if ((class & MACHINE_LANGUAGE_DATA_WIDTH) != 0u)
        return MACHINE_LANGUAGE_VALUE(INTEGER);
    switch (class)
    {
    case MACHINE_LANGUAGE_DATA_STRING: return MACHINE_LANGUAGE_VALUE(STRING);
    case MACHINE_LANGUAGE_DATA_BUFFER: return MACHINE_LANGUAGE_VALUE(BUFFER);
    case MACHINE_LANGUAGE_DATA_PACKAGE:
    case MACHINE_LANGUAGE_DATA_VARIABLE_PACKAGE: return MACHINE_LANGUAGE_VALUE(PACKAGE);
    default: return MACHINE_LANGUAGE_VALUE(NAME_REFERENCE);
    }
}

/**
 * @brief The integers of kept data @p value hold all their bits, or 32 of them when its table was declared under a
 *        DSDT whose revision is below 2.
 */
static uint64_t machine_language_kept_mask(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value)
{
    return ((value->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_KEPT_DATA_32_BIT_INTEGERS) !=
            0u) ?
               0xFFFFFFFFull :
               UINT64_MAX;
}

/**
 * @brief Reads the element of kept data (O20) at @p pc, before @p end, inside @p depth packages, as the builder of
 *        kept data takes one, which the load did not check and AML can rewrite after the load: an integer
 *        constant whose bytes end by @p end, masked by @p mask; a NameString; a String whose NUL comes before
 *        @p end, within 64 KiB; a Buffer or a package whose head reads, its size or count a literal constant, a
 *        Buffer within 64 KiB, a package within 4,096 elements and the levels of §6.1. Nothing past @p end is read.
 *
 * @return Whether the bytes read so; @p out_element then holds the element.
 *
 * @details Always inlined: the builder of kept data reads every element through it, and a call, with the element
 *          written through memory, costs it more than the checks the element needs.
 */
__attribute__((always_inline)) static inline bool machine_language_read_kept(const uint8_t *bytes, uint32_t pc,
                                                                             uint32_t end, uint32_t depth,
                                                                             uint64_t mask,
                                                                             MachineLanguageKeptElement_t *out_element)
{
    const uint8_t class = machine_language_data_classes[bytes[pc]];
    MachineLanguageNameString_t name;

    out_element->class = class;
    if ((class & MACHINE_LANGUAGE_DATA_WIDTH) != 0u)
    {
        out_element->end = pc + class;
        if (end - pc < class)
            return false;
        out_element->integer = machine_language_data_integer(bytes, pc, class, mask);
        return true;
    }
    if (class == MACHINE_LANGUAGE_DATA_SEGMENT)
    {
        out_element->end = pc + 4u;
        return machine_language_holds_name_segment(bytes, pc, end);
    }
    if (class == MACHINE_LANGUAGE_DATA_PREFIX)
        return advanced_configuration_and_power_interface_machine_language_read_name_string(
                   bytes, pc, end, &name, &out_element->end) == MACHINE_LANGUAGE_OK;
    if (class == MACHINE_LANGUAGE_DATA_STRING)
    {
        uint32_t nul = pc + 1u;

        while (nul < end && bytes[nul] != 0x00u)
            ++nul;
        out_element->first = pc + 1u;
        out_element->count = nul - out_element->first;
        out_element->end = nul + 1u;
        return nul != end && out_element->count <= MACHINE_LANGUAGE_MAX_BUFFER_BYTES;
    }
    if (class == 0u ||
        !machine_language_data_head(bytes, pc, end, class, &out_element->end, &out_element->first, &out_element->count))
        return false;
    if (class == MACHINE_LANGUAGE_DATA_BUFFER)
    {
        const uint32_t initialised = out_element->end - out_element->first;

        out_element->count = (out_element->count > initialised) ? out_element->count : initialised;
        return out_element->count <= MACHINE_LANGUAGE_MAX_BUFFER_BYTES;
    }
    return depth < MACHINE_LANGUAGE_MAX_NESTED_PACKAGES &&
           out_element->count <=
               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS;
}

/**
 * @brief Folds a data object kept in its table (O20) as machine_language_fold_value folds the object it builds
 *        into: an Integer masked to its width, a String's or a Buffer's length and bytes, zeros past a Buffer's
 *        initialiser, a Package's count and each element it keeps, its kind first, none past its count, then a zero
 *        kind for each element its count adds past its initialisers, a name as its path bytes.
 *        Bytes that machine_language_read_kept does not read as data, which the load did not check or AML has
 *        rewritten since, fold as machine_language_fold_unreadable folds them: an object or a name cut by the end of
 *        the table or of its package, a byte that starts no data object, packages nested past the levels of §6.1, a
 *        String or a Buffer past its bytes or a VarPackage past its elements in §6.1, which the object's first read
 *        would find too, and a size or a count a name gives, which only that read reads. Nothing past the table is
 *        read.
 */
static uint32_t machine_language_fold_kept(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                           uint32_t fold,
                                           const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[value->auxiliary - 1u];
    const uint8_t *bytes = (const uint8_t *) table->header;
    const uint64_t mask = machine_language_kept_mask(value);
    uint32_t ends[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    uint32_t counts[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    uint32_t seen[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    const uint32_t before = fold;
    uint32_t depth = 0u;
    uint32_t hidden = 0u;
    uint32_t pc = (uint32_t) value->payload;

    for (;;)
    {
        const uint32_t end = (depth != 0u) ? ends[depth - 1u] : table->length;

        if (depth != 0u && pc == end)
        {
            if (hidden == 0u && seen[depth - 1u] < counts[depth - 1u])
                fold = machine_language_fold_zeros(fold, counts[depth - 1u] - seen[depth - 1u]);
            hidden = (--depth < hidden) ? 0u : hidden;
            if (depth == 0u)
                return fold;
            ++seen[depth - 1u];
            continue;
        }

        const bool folds = hidden == 0u && (depth == 0u || seen[depth - 1u] < counts[depth - 1u]);
        MachineLanguageKeptElement_t element;

        if (!machine_language_read_kept(bytes, pc, end, depth, mask, &element))
            return machine_language_fold_unreadable(before);
        if (folds && depth != 0u)
            fold = machine_language_fold_unsigned(fold, machine_language_kept_kind(element.class), 1u);
        if ((element.class & MACHINE_LANGUAGE_DATA_WIDTH) != 0u)
            fold = folds ? machine_language_fold_unsigned(fold, element.integer, 8u) : fold;
        else if (element.class == MACHINE_LANGUAGE_DATA_SEGMENT || element.class == MACHINE_LANGUAGE_DATA_PREFIX)
            fold = folds ? machine_language_fold_bytes(fold, bytes + pc, element.end - pc) : fold;
        else if (element.class == MACHINE_LANGUAGE_DATA_STRING || element.class == MACHINE_LANGUAGE_DATA_BUFFER)
        {
            const uint32_t initialised =
                (element.class == MACHINE_LANGUAGE_DATA_STRING) ? element.count : element.end - element.first;

            if (folds)
            {
                fold = machine_language_fold_unsigned(fold, element.count, 4u);
                fold = machine_language_fold_bytes(fold, bytes + element.first, initialised);
                fold = machine_language_fold_zeros(fold, element.count - initialised);
            }
        }
        else
        {
            fold = folds ? machine_language_fold_unsigned(fold, element.count, 4u) : fold;
            hidden = (folds || hidden != 0u) ? hidden : depth + 1u;
            ends[depth] = element.end;
            counts[depth] = element.count;
            seen[depth++] = 0u;
            pc = element.first;
            continue;
        }
        pc = element.end;
        if (depth == 0u)
            return fold;
        ++seen[depth - 1u];
    }
}

/**
 * @brief Gives back the packages the builder of kept data left open, innermost first, each with what it holds, as
 *        evaluate_data gives back those it abandons, and returns @p status.
 */
static MachineLanguageStatus_t
machine_language_abandon_kept(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                              const MachineLanguageKeptFrame_t *frames, uint32_t depth, MachineLanguageStatus_t status)
{
    while (depth != 0u)
        advanced_configuration_and_power_interface_machine_language_object_release(&load->heap, &load->space,
                                                                                   frames[--depth].handle);
    return status;
}

/**
 * @brief Gives @p out_value a String or a Buffer, @p kind, of @p length bytes whose first @p copied are @p bytes, the
 *        rest zero.
 */
static MachineLanguageStatus_t
machine_language_kept_bytes(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint8_t kind,
                            const uint8_t *bytes, uint32_t copied, uint32_t length,
                            AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value)
{
    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_object_allocate_copy(
            &load->heap, &load->space, kind, length, bytes, copied, &out_value->handle);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    out_value->kind = kind;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Opens @p package, a Package or a VarPackage machine_language_read_kept has read, as @p out_frame: its object
 *        of the element count its head gives, whose elements start Uninitialized.
 */
static MachineLanguageStatus_t
machine_language_open_kept(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                           const MachineLanguageKeptElement_t *package, MachineLanguageKeptFrame_t *out_frame)
{
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_object_allocate(
        &load->heap, &load->space, MACHINE_LANGUAGE_VALUE(PACKAGE), package->count, &out_frame->handle);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    out_frame->elements =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t
             *) (advanced_configuration_and_power_interface_machine_language_object(&load->heap, out_frame->handle) +
                 1);
    out_frame->index = 0u;
    out_frame->count = package->count;
    out_frame->end = package->end;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Builds the data object node @p node kept in its table (O20) with the general data decoder, as a load whose
 *        general_load is set builds it: from the same bytes, its names read in the node's parent and its integers as
 *        wide as when its table was declared, the packages it pads or cuts counted once it builds.
 *
 * @return The data decoder's status, OK otherwise, or TYPE_MISMATCH when the bytes no longer build a constant: a term
 *         needs the interpreter, which is the one cause the data decoder gives no status of its own.
 */
static MachineLanguageStatus_t
machine_language_decode_kept(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                             const MachineLanguageNode_t *node,
                             AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table =
        &load->tables[node->value.auxiliary - 1u];
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = {
        .bytes = (const uint8_t *) table->header,
        .scope = node->parent,
        .table = node->value.auxiliary,
        .integer_bits = (machine_language_kept_mask(&node->value) == UINT64_MAX) ? 64u : 32u,
    };
    bool constant = false;
    uint32_t after = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_evaluate_data(
        &load->heap, &load->space, &source, (uint32_t) node->value.payload, table->length, out_value, &constant,
        &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return constant ? MACHINE_LANGUAGE_OK : MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
}

/**
 * @brief Gives back what the builder of kept data built from bytes that do not read as kept data, which the load did
 *        not check or AML has rewritten since, then builds the object through the general data decoder, so that it
 *        ends as the general path does: the status it fails with, or the object it builds.
 */
static MachineLanguageStatus_t
machine_language_rebuild_kept(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                              const MachineLanguageNode_t *node, const MachineLanguageKeptFrame_t *frames,
                              uint32_t depth, AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value)
{
    (void) machine_language_abandon_kept(load, frames, depth, MACHINE_LANGUAGE_OK);
    return machine_language_decode_kept(load, node, out_value);
}

/**
 * @brief Builds the data object node @p node kept in its table (O20) without the general data decoder (C4): each
 *        element machine_language_read_kept reads is decoded straight into its package, its integers masked to the
 *        table's width, its names kept as names read in the node's parent, each object allocated where evaluate_data
 *        allocates it, so that the heap ends the same; once the object is built, it counts it, and the packages it
 *        padded and cut, as evaluate_data counts them. Bytes that do not read as kept data, which the load did not
 *        check or AML has rewritten since, a name where the object starts included, are built by the general data
 *        decoder instead.
 *
 * @return OK; OUT_OF_MEMORY or LIMIT_OBJECT_RUNS when the heap cannot hold an object, every object built so far
 *         given back; the general data decoder's status for bytes that do not read as kept data.
 */
static MachineLanguageStatus_t
machine_language_build_kept(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                            const MachineLanguageNode_t *node,
                            AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *out_value)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table =
        &load->tables[node->value.auxiliary - 1u];
    const uint8_t *bytes = (const uint8_t *) table->header;
    const uint64_t mask = machine_language_kept_mask(&node->value);
    MachineLanguageKeptFrame_t frames[MACHINE_LANGUAGE_MAX_NESTED_PACKAGES];
    uint32_t depth = 0u;
    uint32_t padded = 0u;
    uint32_t truncated = 0u;
    uint32_t pc = (uint32_t) node->value.payload;

    for (;;)
    {
        const uint32_t end = (depth != 0u) ? frames[depth - 1u].end : table->length;
        AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value = {0};
        MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

        if (depth != 0u && pc == end)
        {
            value.kind = MACHINE_LANGUAGE_VALUE(PACKAGE);
            value.handle = frames[--depth].handle;
            padded += (frames[depth].index < frames[depth].count) ? 1u : 0u;
            truncated += (frames[depth].index > frames[depth].count) ? 1u : 0u;
        }
        else
        {
            MachineLanguageKeptElement_t element;

            if (!machine_language_read_kept(bytes, pc, end, depth, mask, &element))
                return machine_language_rebuild_kept(load, node, frames, depth, out_value);
            if ((element.class & MACHINE_LANGUAGE_DATA_WIDTH) != 0u)
            {
                value.kind = MACHINE_LANGUAGE_VALUE(INTEGER);
                value.payload = element.integer;
            }
            else if (element.class == MACHINE_LANGUAGE_DATA_SEGMENT || element.class == MACHINE_LANGUAGE_DATA_PREFIX)
            {
                if (depth == 0u)
                    return machine_language_rebuild_kept(load, node, frames, depth, out_value);
                value.kind = MACHINE_LANGUAGE_VALUE(NAME_REFERENCE);
                value.auxiliary = node->value.auxiliary;
                value.handle = node->parent;
                value.payload = pc;
            }
            else if (element.class == MACHINE_LANGUAGE_DATA_STRING)
                status = machine_language_kept_bytes(load, MACHINE_LANGUAGE_VALUE(STRING), bytes + element.first,
                                                     element.count, element.count, &value);
            else if (element.class == MACHINE_LANGUAGE_DATA_BUFFER)
                status = machine_language_kept_bytes(load, MACHINE_LANGUAGE_VALUE(BUFFER), bytes + element.first,
                                                     element.end - element.first, element.count, &value);
            else
            {
                status = machine_language_open_kept(load, &element, &frames[depth]);
                if (status != MACHINE_LANGUAGE_OK)
                    return machine_language_abandon_kept(load, frames, depth, status);
                if (++depth > load->heap.nested_packages_peak)
                    load->heap.nested_packages_peak = depth;
                pc = element.first;
                continue;
            }
            if (status != MACHINE_LANGUAGE_OK)
                return machine_language_abandon_kept(load, frames, depth, status);
            pc = element.end;
        }
        if (depth == 0u)
        {
            ++load->data_built_directly;
            load->heap.packages_padded += padded;
            load->heap.packages_truncated += truncated;
            *out_value = value;
            return MACHINE_LANGUAGE_OK;
        }

        MachineLanguageKeptFrame_t *frame = &frames[depth - 1u];

        if (frame->index < frame->count)
            frame->elements[frame->index] = value;
        else
            advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space,
                                                                                      &value);
        ++frame->index;
    }
}

/**
 * @brief The name of the term at @p offset of a table, as the line of a fault gives it: its opcode's, `NameString` for
 *        a name, which a method call starts with, `Invalid` for a byte that starts no term, `End` at or past @p end,
 * the end of the construct the term stands in.
 */
static const char *machine_language_term_name(const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table,
                                              uint32_t offset, uint32_t end)
{
    const uint8_t *bytes = (const uint8_t *) table->header;
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (offset >= end || offset >= table->length)
        return "End";
    if (advanced_configuration_and_power_interface_machine_language_read_opcode(bytes, offset, table->length, &opcode,
                                                                                &after) == MACHINE_LANGUAGE_OK)
        return advanced_configuration_and_power_interface_machine_language_opcode_name(opcode);
    return advanced_configuration_and_power_interface_machine_language_starts_name(bytes[offset]) ? "NameString" :
                                                                                                    "Invalid";
}

/**
 * @brief Appends the field @p key with the path of @p node, or `too_deep` when MACHINE_LANGUAGE_FAULT_PATH_BYTES
 *        cannot hold it.
 */
static void machine_language_log_path(AdvancedConfigurationAndPowerInterfaceLogLine_t *line,
                                      const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                      const char *key, uint32_t node)
{
    char path[MACHINE_LANGUAGE_FAULT_PATH_BYTES];

    advanced_configuration_and_power_interface_log_text(
        line, key,
        (advanced_configuration_and_power_interface_machine_language_namespace_path(&load->space, node, path,
                                                                                    sizeof(path)) != 0u) ?
            path :
            "too_deep");
}

/**
 * @brief Logs as an error the data object node @p node keeps in its table (O20) that the check run once the bodies are
 *        decoded found would not build, beside data_faults: no pass declared its bytes, so none logged their fault.
 */
static void machine_language_log_kept_check(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                            const MachineLanguageNode_t *node)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table =
        &load->tables[node->value.auxiliary - 1u];
    const uint32_t offset = (uint32_t) node->value.payload;
    AdvancedConfigurationAndPowerInterfaceLogLine_t line;

    advanced_configuration_and_power_interface_log_open(&line, "data_faults");
    advanced_configuration_and_power_interface_log_table(&line, table->header);
    advanced_configuration_and_power_interface_log_hexadecimal(&line, "offset", offset, 8u);
    advanced_configuration_and_power_interface_log_text(&line, "opcode",
                                                        machine_language_term_name(table, offset, table->length));
    machine_language_log_path(&line, load, "scope", node->parent);
    advanced_configuration_and_power_interface_log_error(load->space.host, &line);
}

/**
 * @brief Leaves node @p node, whose kept data (O20) did not build with @p status, Uninitialized, as a load that built
 *        the object as it declared it would have left it: TYPE_MISMATCH, a term that needs the interpreter, is counted
 *        in values_deferred, any other status, a fault in its bytes, in data_faults_read.
 */
static void machine_language_leave_unbuilt(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                           MachineLanguageNode_t *node, MachineLanguageStatus_t status)
{
    const bool waits = status == MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    if (!waits)
        advanced_configuration_and_power_interface_machine_language_report_fault(
            load, status, node->value.auxiliary, (uint32_t) node->value.payload,
            load->tables[node->value.auxiliary - 1u].length, 0u, node->parent);
    node->value = (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){0};
    node->type = MACHINE_LANGUAGE_NODE(UNINITIALIZED);
    load->values_deferred += waits ? 1u : 0u;
    load->data_faults_read += waits ? 0u : 1u;
}

/**
 * @brief Checks the Package or VarPackage node @p node keeps in its table (O20) as the data decoder would build it: one
 *        that would not build is counted in data_faults, one whose nested size or count a name gives in
 *        data_named_sizes.
 */
static void machine_language_check_kept(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        uint32_t index, const MachineLanguageNode_t *node)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table =
        &load->tables[node->value.auxiliary - 1u];
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = {
        .bytes = (const uint8_t *) table->header,
        .scope = node->parent,
        .table = node->value.auxiliary,
        .integer_bits = (machine_language_kept_mask(&node->value) == UINT64_MAX) ? 64u : 32u,
    };
    bool named = false;

    if (!machine_language_check_data(&load->space, &source, (uint32_t) node->value.payload, table->length, &named))
    {
        ++load->data_faults;
        load->data_faults_signature = advanced_configuration_and_power_interface_machine_language_kept_data_signature(
            load->data_faults_signature, index);
        machine_language_log_kept_check(load, node);
    }
    else if (named)
        ++load->data_named_sizes;
}

/**
 * @brief Checks, once the decoding pass has decoded the load's tables from @p first to before @p end, each Package and
 *        VarPackage a Name keeps in one of them (O20), over the namespace's nodes, whatever fault the decoding met: a
 *        crossing PkgLength that abandons a table hides none of its kept data. A Buffer, whose head is all its check,
 *        is left alone.
 *
 * @details The nodes are read from the last, where a table just declared puts its own, and the reading stops once it
 *          has met as many kept objects of those tables as their declarations kept: none when they kept nothing. A
 *          node a freed one gave back to a later declaration may stand anywhere, and one a read has built since is no
 *          longer kept; either way the count is not reached early, and every node is read.
 */
static void machine_language_check_kept_data(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                             uint32_t first, uint32_t end)
{
    uint32_t kept = 0u;

    for (uint32_t table = first; table < end; ++table)
        kept += load->tables[table].data_kept;
    for (uint32_t index = load->space.node_count - 1u;
         kept != 0u && index > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT; --index)
    {
        const MachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

        if ((node->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE) != 0u ||
            node->value.kind != MACHINE_LANGUAGE_VALUE(KEPT_DATA) || node->value.auxiliary <= first ||
            node->value.auxiliary > end)
            continue;
        --kept;
        if (node->type == MACHINE_LANGUAGE_NODE(PACKAGE))
            machine_language_check_kept(load, index, node);
    }
}

/**
 * @brief Folds one value: an Integer, a String's or Buffer's length and bytes, a Package's count and
 *        each element, its kind first, nested packages through an explicit stack, a name element as its path bytes.
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
        fold = machine_language_fold_unsigned(fold, value->kind, 1u);
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
    if (node->value.kind == MACHINE_LANGUAGE_VALUE(KEPT_DATA))
        return machine_language_fold_kept(load, fold, &node->value);
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
    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_object_allocate_copy(
            &load->heap, &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
            (uint32_t) sizeof(operating_system) - 1u, operating_system, (uint32_t) sizeof(operating_system), &handle);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
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
    return machine_language_little_endian_32(bytes + 4u);
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
 * @brief Counts a definition block whose checksum fails, loaded all the same, and logs it as a warning that names
 *        it (§3.9, §5.1).
 */
static void machine_language_count_bad_checksum(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                                const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    AdvancedConfigurationAndPowerInterfaceLogLine_t line;

    ++load->checksum_bad_loaded;
    advanced_configuration_and_power_interface_log_open(&line, "checksum_bad_loaded");
    advanced_configuration_and_power_interface_log_table(&line, header);
    advanced_configuration_and_power_interface_log_warning(load->space.host, &line);
}

/**
 * @brief Adds a table Load or LoadTable brings to the registry, last, for the evaluation that runs the Load to run
 *        its code in a frame of its own. A table the load holds already is refused, counted.
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
    if (!table->checksum_valid)
        machine_language_count_bad_checksum(load, header);
    load->bytes += length - (uint32_t) sizeof(*header);
    ++load->tables_loaded;
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

/**
 * @brief Whether @p load can take the definition block at @p header: one is there, at least as long as its
 *        header, and the registry of loaded tables has room for it.
 */
static bool machine_language_accepts_table(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                           const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    return load->table_count != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES &&
           header != NULL && header->length >= sizeof(*header);
}

/**
 * @brief Records the definition block at @p header, which the load accepts, as a table whose checksum holds
 *        when @p checksum_valid says so, counted when it does not, then runs its declaration pass.
 */
static void machine_language_declare_accepted_table(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                                    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header,
                                                    bool checksum_valid)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[load->table_count];

    memset(table, 0, sizeof(*table));
    table->header = header;
    table->length = header->length;
    table->checksum_valid = checksum_valid;
    if (!checksum_valid)
        machine_language_count_bad_checksum(load, header);
    load->bytes += header->length - (uint32_t) sizeof(*header);
    if (memcmp(header->signature, "DSDT", sizeof(header->signature)) == 0 && load->integer_bits == 0u)
        load->integer_bits = (header->revision < 2u) ? 32u : 64u;
    machine_language_walk_table(load, load->table_count++, MACHINE_LANGUAGE_PASS_DECLARE);
}

/**
 * @brief The larger of @p first and @p second.
 */
static uint32_t machine_language_larger(uint32_t first, uint32_t second) { return (first > second) ? first : second; }

/**
 * @brief A limit whose hits @p hits_name counts, @p hits of them, and whose peak is @p peak.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t
machine_language_bound(const char *hits_name, uint32_t hits, const char *peak_name, uint32_t peak, bool comparable)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t limit = {
        .hits_name = hits_name,
        .peak_name = peak_name,
        .hits = hits,
        .peak = peak,
        .comparable = comparable,
    };

    return limit;
}

/**
 * @brief The limit a fault of @p status reaches, its hits the faults counted under that status.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t
machine_language_status_limit(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                              MachineLanguageStatus_t status, const char *peak_name, uint32_t peak, bool comparable)
{
    return machine_language_bound(advanced_configuration_and_power_interface_machine_language_status_name(status),
                                  load->faults[status], peak_name, peak, comparable);
}

bool advanced_configuration_and_power_interface_machine_language_open(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t max_nodes, uint32_t max_arena_bytes)
{
    memset(load, 0, offsetof(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t, predecoding));
    load->regions.last_reachable_address = advanced_configuration_and_power_interface_last_reachable_address(host);
    advanced_configuration_and_power_interface_machine_language_predecoding_open(&load->predecoding);
    load->predecodes_loops = !advanced_configuration_and_power_interface_boot_line_holds("lpl.acpi.predecoding=off");
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
    if (!machine_language_accepts_table(load, header))
        return false;
    machine_language_declare_accepted_table(
        load, header, advanced_configuration_and_power_interface_checksum_holds(header, header->length));
    return true;
}

void advanced_configuration_and_power_interface_machine_language_decode_tables(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    for (uint32_t table = 0u; table < load->table_count && load->externals_unplaced != 0u; ++table)
        machine_language_walk_table(load, table, MACHINE_LANGUAGE_PASS_EXTERNALS);
    for (uint32_t table = 0u; table < load->table_count && !load->skips_bodies; ++table)
        machine_language_walk_table(load, table, MACHINE_LANGUAGE_PASS_DECODE);
    if (!load->skips_bodies)
        machine_language_check_kept_data(load, 0u, load->table_count);
}

void advanced_configuration_and_power_interface_machine_language_declare_directory(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &directory->fixed_description;
    uint16_t control_ports[2];

    advanced_configuration_and_power_interface_power_management_control_ports(fadt, control_ports);
    load->directory = directory;
    advanced_configuration_and_power_interface_machine_language_use_global_lock(
        load, fadt->hardware_reduced ? 0u : directory->firmware_control_physical_address, control_ports);
    const AdvancedConfigurationAndPowerInterfaceTable_t *dsdt =
        advanced_configuration_and_power_interface_find_entry_in_directory(directory, "DSDT");

    if (dsdt != NULL && machine_language_accepts_table(load, dsdt->header))
        machine_language_declare_accepted_table(load, dsdt->header, dsdt->checksum_valid);
    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceTable_t *entry = &directory->tables[index];

        if (memcmp(entry->signature, "SSDT", sizeof(entry->signature)) == 0 &&
            machine_language_accepts_table(load, entry->header))
            machine_language_declare_accepted_table(load, entry->header, entry->checksum_valid);
    }
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

void advanced_configuration_and_power_interface_machine_language_declare_loaded_table(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table)
{
    ++load->declaring;
    machine_language_walk_without_running(load, table, MACHINE_LANGUAGE_PASS_DECLARE);
    --load->declaring;
}

void advanced_configuration_and_power_interface_machine_language_finish_loaded_table(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table)
{
    if (load->externals_unplaced != 0u)
        machine_language_walk_without_running(load, table, MACHINE_LANGUAGE_PASS_EXTERNALS);
    if (load->skips_bodies)
        return;
    machine_language_walk_without_running(load, table, MACHINE_LANGUAGE_PASS_DECODE);
    machine_language_check_kept_data(load, table, table + 1u);
}

void advanced_configuration_and_power_interface_machine_language_declare_statement(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table, uint32_t scope, uint32_t start,
    uint32_t limit, bool not_run)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t *declaration = &load->declaration;
    const bool resumes = !not_run && declaration->asks != 0u;
    MachineLanguageWalk_t walk;

    machine_language_begin_walk(&walk, load, table, MACHINE_LANGUAGE_PASS_DECLARE, start, limit, scope,
                                not_run ? MACHINE_LANGUAGE_BLOCK_NOT_RUN : 0u);
    walk.declaration = not_run ? NULL : declaration;
    if (resumes)
    {
        load->pending[0] = declaration->waiting;
        walk.pending_count = 1u;
        walk.pc = declaration->resumes;
        walk.last_created = declaration->waiting.node;
    }
    declaration->asks = 0u;
    declaration->body = 0u;
    if (machine_language_walk(&walk, start, true) == MACHINE_LANGUAGE_ASKS)
    {
        declaration->waiting = load->pending[walk.pending_count - 1u];
        --declaration->waiting.operands_done;
        declaration->resumes = walk.pc;
        return;
    }
    declaration->answers_for = 0u;
    declaration->after = (walk.block_count != 0u) ? walk.pc : limit;
    declaration->closed = walk.closes_body;
    declaration->leaves = walk.block_count == 0u && !walk.abandons;
    declaration->abandons = walk.block_count == 0u && walk.abandons;
}

bool advanced_configuration_and_power_interface_machine_language_runs_at_table_level(uint32_t opcode)
{
    return machine_language_is_code(false, opcode);
}

void advanced_configuration_and_power_interface_machine_language_count_load_error(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, MachineLanguageStatus_t status)
{
    machine_language_count_load_error(load, status);
}

void advanced_configuration_and_power_interface_machine_language_count_table_failure(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table)
{
    if (table == 0u || table > load->table_count || load->tables[table - 1u].load_failed)
        return;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *entry = &load->tables[table - 1u];

    entry->load_failed = true;
    load->table_load_failed +=
        (entry->origin == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_STATIC) ? 1u : 0u;
}

void advanced_configuration_and_power_interface_machine_language_report_fault(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, MachineLanguageStatus_t status, uint32_t table,
    uint32_t offset, uint32_t end, uint32_t method, uint32_t scope)
{
    AdvancedConfigurationAndPowerInterfaceLogLine_t line;

    ++load->faults[status];
    advanced_configuration_and_power_interface_log_open(
        &line, advanced_configuration_and_power_interface_machine_language_status_name(status));
    if (table == 0u || table > load->table_count)
        advanced_configuration_and_power_interface_log_text(&line, "table", "none");
    else
    {
        advanced_configuration_and_power_interface_log_table(&line, load->tables[table - 1u].header);
        advanced_configuration_and_power_interface_log_hexadecimal(&line, "offset", offset, 8u);
        advanced_configuration_and_power_interface_log_text(
            &line, "opcode", machine_language_term_name(&load->tables[table - 1u], offset, end));
    }
    if (method != 0u)
        machine_language_log_path(&line, load, "method", method);
    else
        machine_language_log_path(&line, load, "scope", scope);
    advanced_configuration_and_power_interface_log_error(load->space.host, &line);
}

void advanced_configuration_and_power_interface_machine_language_report_object_fault(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, MachineLanguageStatus_t status, uint32_t node)
{
    AdvancedConfigurationAndPowerInterfaceLogLine_t line;

    ++load->faults[status];
    advanced_configuration_and_power_interface_log_open(
        &line, advanced_configuration_and_power_interface_machine_language_status_name(status));
    machine_language_log_path(&line, load, "object", node);
    advanced_configuration_and_power_interface_log_error(load->space.host, &line);
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

void advanced_configuration_and_power_interface_machine_language_log_fatal(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t table, uint32_t offset,
    uint32_t method, uint32_t scope)
{
    AdvancedConfigurationAndPowerInterfaceLogLine_t line;

    advanced_configuration_and_power_interface_log_open(&line, "fatals");
    advanced_configuration_and_power_interface_log_table(&line, load->tables[table - 1u].header);
    advanced_configuration_and_power_interface_log_hexadecimal(&line, "offset", offset, 8u);
    advanced_configuration_and_power_interface_log_text(
        &line, "opcode",
        advanced_configuration_and_power_interface_machine_language_opcode_name(MACHINE_LANGUAGE_OPCODE(FATAL)));
    if (method != 0u)
        machine_language_log_path(&line, load, "method", method);
    else
        machine_language_log_path(&line, load, "scope", scope);
    advanced_configuration_and_power_interface_log_error(load->space.host, &line);
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_materialize(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t index)
{
    MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node == NULL || node->value.kind != MACHINE_LANGUAGE_VALUE(KEPT_DATA))
        return MACHINE_LANGUAGE_OK;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value;
    const MachineLanguageStatus_t status = load->general_load ? machine_language_decode_kept(load, node, &value) :
                                                                machine_language_build_kept(load, node, &value);

    if (status == MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY) || status == MACHINE_LANGUAGE_STATUS(LIMIT_OBJECT_RUNS))
        return status;
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_leave_unbuilt(load, node, status);
        return MACHINE_LANGUAGE_OK;
    }
    node->value = value;
    ++load->data_materialized;
    return MACHINE_LANGUAGE_OK;
}

uint32_t advanced_configuration_and_power_interface_machine_language_kept_data_signature(uint32_t signature,
                                                                                         uint32_t index)
{
    uint32_t fold = 0x811C9DC5u;

    for (uint32_t byte = 0u; byte < 4u; ++byte)
        fold = (fold ^ ((index >> (8u * byte)) & 0xFFu)) * 0x01000193u;
    return signature + fold;
}

bool advanced_configuration_and_power_interface_machine_language_describe_kept_data(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value, uint32_t *out_length,
    const uint8_t **out_initialiser, uint32_t *out_initialised)
{
    if (value->kind != MACHINE_LANGUAGE_VALUE(KEPT_DATA))
        return false;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[value->auxiliary - 1u];
    const uint8_t *bytes = (const uint8_t *) table->header;
    const uint32_t offset = (uint32_t) value->payload;
    const uint8_t class = machine_language_data_classes[bytes[offset]];
    uint32_t object_end = 0u;
    uint32_t first = 0u;
    uint32_t count = 0u;

    if (!machine_language_data_head(bytes, offset, table->length, class, &object_end, &first, &count))
        return false;
    *out_length = count;
    *out_initialiser = NULL;
    *out_initialised = 0u;
    if (class == MACHINE_LANGUAGE_DATA_BUFFER)
    {
        *out_initialiser = bytes + first;
        *out_initialised = object_end - first;
        *out_length = (count > *out_initialised) ? count : *out_initialised;
    }
    return true;
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

uint32_t advanced_configuration_and_power_interface_machine_language_evaluations_fold(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const char *const *paths, uint32_t count,
    uint32_t *out_succeeded)
{
    uint32_t fold = MACHINE_LANGUAGE_FOLD_OFFSET;

    *out_succeeded = 0u;
    for (uint32_t path = 0u; path < count; ++path)
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
        const MachineLanguageStatus_t status =
            advanced_configuration_and_power_interface_machine_language_evaluate(load, paths[path], NULL, 0u, &result);

        fold = machine_language_fold_unsigned(fold, status, 2u);
        if (status == MACHINE_LANGUAGE_OK)
        {
            fold = machine_language_fold_unsigned(fold, result.kind, 1u);
            fold = machine_language_fold_value(load, fold, &result);
            ++*out_succeeded;
        }
        advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, &result);
    }
    return fold;
}

uint32_t advanced_configuration_and_power_interface_machine_language_work_fold(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    uint32_t fold = MACHINE_LANGUAGE_FOLD_OFFSET;

    for (uint32_t opcode = 0u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        fold = machine_language_fold_unsigned(fold, load->evaluation.executed[opcode], 4u);
    fold = machine_language_fold_unsigned(fold, load->space.lookups, 4u);
    fold = machine_language_fold_unsigned(fold, load->space.probes, 4u);
    fold = machine_language_fold_unsigned(fold, load->heap.objects, 4u);
    fold = machine_language_fold_unsigned(fold, load->bytes_decoded, 4u);
    return machine_language_fold_unsigned(fold, load->regions.map_calls, 4u);
}

void advanced_configuration_and_power_interface_machine_language_limits(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t
        out_limits[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS])
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint64_t time_requested = load->services.time_requested_peak;
    uint32_t index = 0u;

    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES), "frames_peak",
                                                        evaluation->frames_peak, true);
    out_limits[index++] =
        machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_BLOCKS), "blocks_peak",
                                      machine_language_larger(evaluation->blocks_peak, load->blocks_peak), true);
    out_limits[index++] =
        machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS), "pending_operators_peak",
                                      machine_language_larger(evaluation->operators_peak, load->pending_peak), true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_VALUES), "values_peak",
                                                        evaluation->values_peak, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_TEMPORARY_NODES),
                                                        "temporary_nodes_peak", load->temporary_nodes_peak, true);
    out_limits[index++] =
        machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_INTERNAL_NESTING), "internal_nesting_peak",
                                      load->regions.internal_nesting_peak, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_LOOP_ITERATIONS),
                                                        "loop_iterations_peak", evaluation->iterations_peak, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_FIELD_ACCESSES),
                                                        "field_accesses_peak", load->regions.field_accesses_peak, true);
    out_limits[index++] =
        machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_TIME_REQUESTED), "time_requested_peak_us",
                                      (time_requested > UINT32_MAX) ? UINT32_MAX : (uint32_t) time_requested, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_PACKAGE_ELEMENTS),
                                                        "package_elements_peak", load->heap.length_peaks[2], true);
    out_limits[index++] = machine_language_status_limit(
        load, MACHINE_LANGUAGE_STATUS(LIMIT_BUFFER_BYTES), "buffer_bytes_peak",
        (load->heap.length_peaks[0] > load->heap.length_peaks[1]) ? load->heap.length_peaks[0] :
                                                                    load->heap.length_peaks[1],
        true);
    out_limits[index++] =
        machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(NAME_RESOLUTION_LOOP), "name_indirections_peak",
                                      evaluation->name_indirections_peak, true);
    out_limits[index++] =
        machine_language_bound(NULL, 0u, "nested_packages_peak", load->heap.nested_packages_peak, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_OBJECT_RUNS),
                                                        "object_runs_peak", load->heap.run_count, false);
    out_limits[index++] = machine_language_status_limit(
        load, MACHINE_LANGUAGE_STATUS(LIMIT_NAMESPACE_NODES), "namespace_nodes_peak",
        load->space.node_count - ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY),
                                                        "arena_bytes_peak", load->space.arena_bytes_peak, false);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_TABLES), "tables_peak",
                                                        load->table_count, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_TABLE_BYTES),
                                                        "table_bytes_peak", load->table_bytes_copied, true);
    out_limits[index++] = machine_language_status_limit(load, MACHINE_LANGUAGE_STATUS(LIMIT_HELD_MUTEXES),
                                                        "held_mutexes_peak", load->services.held_peak, true);
    out_limits[index++] = machine_language_bound("notifications_dropped", load->services.notifications_dropped,
                                                 "notifications_peak", load->services.notification_peak, true);
    out_limits[index] = machine_language_bound(NULL, 0u, "mappings_peak", load->regions.mapping_count, true);
}
