#include <kernel/acpi/machine_language.h>

#include <string.h>

#define MACHINE_LANGUAGE_CALL          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
#define MACHINE_LANGUAGE_RESULT        (MACHINE_LANGUAGE_CALL + 1u)
#define MACHINE_LANGUAGE_OK            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_OPERAND(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_##kind
#define MACHINE_LANGUAGE_OPCODE(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define MACHINE_LANGUAGE_NODE(type)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_VALUE(kind)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define MACHINE_LANGUAGE_FORM(name)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_##name
#define MACHINE_LANGUAGE_TARGET        0x90u /**< A value that says where a result goes; auxiliary is its kind. */
#define MACHINE_LANGUAGE_ELSE_BYTE     0xA1u /**< The byte of Else, which only an If reads. */
#define MACHINE_LANGUAGE_DATA_OBJECT                                                                                   \
    0x20u /**< Where a term must be a data object, as a Name's object: past every operand kind of a descriptor. */
#define MACHINE_LANGUAGE_DEBUG_OBJECT_TYPE 16u /**< What ObjectType gives for Debug (ACPI 6.6 §19.6.97). */
#define MACHINE_LANGUAGE_LOGICAL_BYTES     4u  /**< The bytes of a Buffer LAnd and LOr read, as Windows does (§5.2). */
#define MACHINE_LANGUAGE_MAX_REFERENCE_HOPS                                                                            \
    256u /**< References a dereference or a store follows: more means a cycle, since stores refuse them. */
#define MACHINE_LANGUAGE_MAX_NAME_INDIRECTIONS 8u  /**< Names inside packages a read follows (§5.1, §6.1). */
#define MACHINE_LANGUAGE_PATH_SEGMENTS         64u /**< NameSegs a path String may hold. */
#define MACHINE_LANGUAGE_MAX_CYCLE_STEPS                                                                               \
    (4u * ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES) /**< Values the walk for a reference \
                                                                                     cycle visits before it calls the  \
                                                                                     walk itself a cycle. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t MachineLanguageLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t MachineLanguageEvaluation_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageFrame_t MachineLanguageFrame_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t MachineLanguageBlock_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t MachineLanguagePendingOperator_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t MachineLanguageNameString_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t MachineLanguageHeader_t;

/**
 * @brief Where a target value sends a result.
 */
typedef enum MachineLanguageTargetKind {
    MACHINE_LANGUAGE_TARGET_NONE,     /**< NullName: the result is dropped. */
    MACHINE_LANGUAGE_TARGET_LOCAL,    /**< A LocalX: handle is X. */
    MACHINE_LANGUAGE_TARGET_ARGUMENT, /**< An ArgX: handle is X. */
    MACHINE_LANGUAGE_TARGET_NODE,     /**< A named object: handle is its node. */
    MACHINE_LANGUAGE_TARGET_DEBUG,    /**< The Debug object. */
    MACHINE_LANGUAGE_TARGET_MISSING,  /**< The SimpleName of a CondRefOf that names no object. */
} MachineLanguageTargetKind_t;

/**
 * @brief Where a chain of references ends.
 */
typedef enum MachineLanguageLocationKind {
    MACHINE_LANGUAGE_LOCATION_NODE,    /**< A named object's value. */
    MACHINE_LANGUAGE_LOCATION_FRAME,   /**< A local or an argument of a running frame. */
    MACHINE_LANGUAGE_LOCATION_ELEMENT, /**< An element of a Package. */
    MACHINE_LANGUAGE_LOCATION_BYTE,    /**< A byte of a Buffer or a String. */
} MachineLanguageLocationKind_t;

/**
 * @brief How a store reaches the object at the end of a chain of references (§5.2).
 */
typedef enum MachineLanguageStoreMode {
    MACHINE_LANGUAGE_STORE_IN_PLACE,  /**< The object takes the value's bytes and keeps its type: a store through a
                                           local or a name that holds a reference. */
    MACHINE_LANGUAGE_STORE_OVERWRITE, /**< The value replaces the object, unless the object is no data: a store
                                           through an argument that holds a reference. */
    MACHINE_LANGUAGE_STORE_REPLACE,   /**< The value replaces the object, whatever it is: CopyObject. */
} MachineLanguageStoreMode_t;

/**
 * @brief The place a chain of references ends at, and what holds it.
 */
typedef struct MachineLanguageLocation {
    uint8_t kind;                 /**< A MACHINE_LANGUAGE_LOCATION_* kind. */
    uint32_t node;                /**< The named object, for a node's value. */
    uint32_t package;             /**< The Package that holds the element, for an element. */
    MachineLanguageValue_t *slot; /**< The value itself; NULL for a byte. */
    MachineLanguageValue_t byte;  /**< The Index of the byte, for a byte. */
} MachineLanguageLocation_t;

/**
 * @brief What a block of a running frame is, and so what its end does.
 */
typedef enum MachineLanguageRunBlock {
    MACHINE_LANGUAGE_RUN_TERMS = 1u, /**< An Else body: its end does nothing. */
    MACHINE_LANGUAGE_RUN_IF,         /**< A taken If body: its end skips an Else that follows. */
    MACHINE_LANGUAGE_RUN_WHILE,      /**< A While body: its end goes back to the While. */
    MACHINE_LANGUAGE_RUN_METHOD,     /**< A method body: its end returns. */
    MACHINE_LANGUAGE_RUN_TABLE,      /**< The term of table-level code a run was asked for. */
} MachineLanguageRunBlock_t;

/**
 * @brief The frame that runs.
 */
static MachineLanguageFrame_t *machine_language_frame(MachineLanguageEvaluation_t *evaluation)
{
    return &evaluation->frames[evaluation->frame_count - 1u];
}

/**
 * @brief The construct the running frame is in.
 */
static MachineLanguageBlock_t *machine_language_block(MachineLanguageEvaluation_t *evaluation)
{
    return &evaluation->blocks[evaluation->block_count - 1u];
}

/**
 * @brief The operator waiting for operands in the construct the frame is in, or NULL.
 */
static MachineLanguagePendingOperator_t *machine_language_active(MachineLanguageEvaluation_t *evaluation)
{
    if (evaluation->operator_count <= machine_language_block(evaluation)->pending_base)
        return NULL;
    return &evaluation->operators[evaluation->operator_count - 1u];
}

/**
 * @brief The first offset the next term may not reach: the active operator's end, else the
 *        construct's.
 */
static uint32_t machine_language_limit(MachineLanguageEvaluation_t *evaluation)
{
    const MachineLanguagePendingOperator_t *active = machine_language_active(evaluation);

    return (active != NULL) ? active->end : machine_language_block(evaluation)->end;
}

/**
 * @brief The load's integer width: 32 or 64 bits.
 */
static uint8_t machine_language_bits(const MachineLanguageLoad_t *load)
{
    return (load->integer_bits == 32u) ? 32u : 64u;
}

/**
 * @brief An integer masked to the load's integer width.
 */
static uint64_t machine_language_mask(const MachineLanguageLoad_t *load, uint64_t integer)
{
    return (load->integer_bits == 32u) ? (integer & 0xFFFFFFFFull) : integer;
}

/**
 * @brief An Integer value.
 */
static MachineLanguageValue_t machine_language_integer(uint64_t integer)
{
    const MachineLanguageValue_t value = {.kind = MACHINE_LANGUAGE_VALUE(INTEGER), .payload = integer};

    return value;
}

/**
 * @brief The logical result of a test: Ones masked to the width when it holds, 0 otherwise.
 */
static MachineLanguageValue_t machine_language_truth(const MachineLanguageLoad_t *load, bool holds)
{
    return machine_language_integer(holds ? machine_language_mask(load, ~0ull) : 0u);
}

/**
 * @brief A target value.
 */
static MachineLanguageValue_t machine_language_target(MachineLanguageTargetKind_t kind, uint32_t handle)
{
    const MachineLanguageValue_t value = {.kind = MACHINE_LANGUAGE_TARGET,
                                          .auxiliary = (uint16_t) kind,
                                          .handle = handle};

    return value;
}

/**
 * @brief Gives back the reference a value holds, and leaves it Uninitialized; an Integer, which holds
 *        none, costs no call.
 */
static void machine_language_release(MachineLanguageLoad_t *load, MachineLanguageValue_t *value)
{
    if (value->kind == MACHINE_LANGUAGE_VALUE(UNINITIALIZED))
        return;
    if (value->kind == MACHINE_LANGUAGE_VALUE(INTEGER))
        value->kind = MACHINE_LANGUAGE_VALUE(UNINITIALIZED);
    else
        advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, value);
}

/**
 * @brief Takes one more reference on what a value names.
 */
static void machine_language_retain(MachineLanguageLoad_t *load, const MachineLanguageValue_t *value)
{
    advanced_configuration_and_power_interface_machine_language_value_retain(&load->heap, value);
}

/**
 * @brief The header of the object a value names.
 */
static const MachineLanguageHeader_t *machine_language_object(const MachineLanguageLoad_t *load,
                                                              const MachineLanguageValue_t *value)
{
    return advanced_configuration_and_power_interface_machine_language_object(&load->heap, value->handle);
}

/**
 * @brief Releases a value the full value stack cannot take, and says so: LIMIT_VALUES.
 */
static MachineLanguageStatus_t machine_language_refuse_value(MachineLanguageLoad_t *load, MachineLanguageValue_t *value)
{
    advanced_configuration_and_power_interface_machine_language_value_release(&load->heap, &load->space, value);
    return MACHINE_LANGUAGE_STATUS(LIMIT_VALUES);
}

/**
 * @brief Pushes a value on the value stack, which takes its reference; one that does not fit is
 *        released.
 */
static MachineLanguageStatus_t machine_language_push(MachineLanguageLoad_t *load, MachineLanguageValue_t *value)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (evaluation->value_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES)
        return machine_language_refuse_value(load, value);
    evaluation->values[evaluation->value_count++] = *value;
    if (evaluation->value_count > evaluation->values_peak)
        evaluation->values_peak = evaluation->value_count;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Pops the value on top of the value stack; its reference goes to the caller.
 */
static MachineLanguageValue_t machine_language_pop(MachineLanguageEvaluation_t *evaluation)
{
    return evaluation->values[--evaluation->value_count];
}

/**
 * @brief Hands a value, and its reference, to the operator waiting for it, or releases it when its
 *        term was a statement.
 */
static MachineLanguageStatus_t machine_language_deliver(MachineLanguageLoad_t *load, MachineLanguageValue_t value)
{
    if (machine_language_active(&load->evaluation) != NULL)
        return machine_language_push(load, &value);
    machine_language_release(load, &value);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The Integer an operand converts to (§5.2), the conversions ACPI forbids counted.
 */
static MachineLanguageStatus_t machine_language_integer_of(MachineLanguageLoad_t *load,
                                                           const MachineLanguageValue_t *value, uint64_t *out_integer)
{
    bool forbidden = false;

    if (value->kind == MACHINE_LANGUAGE_VALUE(INTEGER))
    {
        *out_integer = machine_language_mask(load, value->payload);
        return MACHINE_LANGUAGE_OK;
    }

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_to_integer(
        &load->heap, value, machine_language_bits(load), out_integer, &forbidden);

    load->evaluation.forbidden_conversions += forbidden ? 1u : 0u;
    return status;
}

/**
 * @brief A local or an argument read as an operand, its reference retained: an uninitialized one
 *        reads as Integer 0, counted (§5.2).
 */
static MachineLanguageValue_t machine_language_read_slot(MachineLanguageLoad_t *load,
                                                         const MachineLanguageValue_t *slot)
{
    if (slot->kind != MACHINE_LANGUAGE_VALUE(UNINITIALIZED))
    {
        machine_language_retain(load, slot);
        return *slot;
    }
    ++load->evaluation.uninitialized_operands;
    return machine_language_integer(0u);
}

/**
 * @brief Whether a value of @p kind is a reference: to a name, a local, an argument, an element or a
 *        byte.
 */
static bool machine_language_is_reference(uint8_t kind)
{
    return kind >= MACHINE_LANGUAGE_VALUE(REFERENCE_NODE) && kind <= MACHINE_LANGUAGE_VALUE(REFERENCE_BYTE);
}

/**
 * @brief Whether a named object of @p type holds no data, so that only CopyObject may replace it.
 */
static bool machine_language_holds_no_data(uint8_t type)
{
    switch (type)
    {
    case MACHINE_LANGUAGE_NODE(DEVICE):
    case MACHINE_LANGUAGE_NODE(EVENT):
    case MACHINE_LANGUAGE_NODE(METHOD):
    case MACHINE_LANGUAGE_NODE(MUTEX):
    case MACHINE_LANGUAGE_NODE(OPERATION_REGION):
    case MACHINE_LANGUAGE_NODE(POWER_RESOURCE):
    case MACHINE_LANGUAGE_NODE(PROCESSOR):
    case MACHINE_LANGUAGE_NODE(THERMAL_ZONE):
    case MACHINE_LANGUAGE_NODE(SCOPE): return true;
    default: return false;
    }
}

/**
 * @brief The value a named object whose value is no reference gives where a value is read, its
 *        reference retained: its data object, a buffer field's bits, or what its field unit reads.
 */
static MachineLanguageStatus_t machine_language_read_object(MachineLanguageLoad_t *load, uint32_t index,
                                                            MachineLanguageValue_t *out_value)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    switch (node->value.kind)
    {
    case MACHINE_LANGUAGE_VALUE(INTEGER):
    case MACHINE_LANGUAGE_VALUE(STRING):
    case MACHINE_LANGUAGE_VALUE(BUFFER):
    case MACHINE_LANGUAGE_VALUE(PACKAGE):
        *out_value = node->value;
        machine_language_retain(load, out_value);
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_VALUE(BUFFER_FIELD):
        return advanced_configuration_and_power_interface_machine_language_buffer_field_read(
            &load->heap, &load->space, &node->value, machine_language_bits(load), out_value);
    case MACHINE_LANGUAGE_VALUE(FIELD_UNIT):
        return advanced_configuration_and_power_interface_machine_language_field_read(load, index, out_value);
    default:
        return (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL)) ? MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND) :
                                                                 MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    }
}

/**
 * @brief The slot a local or argument target names in the running frame.
 */
static MachineLanguageValue_t *machine_language_slot(MachineLanguageEvaluation_t *evaluation,
                                                     const MachineLanguageValue_t *target)
{
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    return (target->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL) ? &frame->locals[target->handle] :
                                                                  &frame->arguments[target->handle];
}

/**
 * @brief A reference to the local or argument a target names in the running frame.
 */
static MachineLanguageValue_t machine_language_slot_reference(MachineLanguageEvaluation_t *evaluation,
                                                              const MachineLanguageValue_t *target)
{
    const MachineLanguageValue_t reference = {
        .kind = (target->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL) ? MACHINE_LANGUAGE_VALUE(REFERENCE_LOCAL) :
                                                                       MACHINE_LANGUAGE_VALUE(REFERENCE_ARGUMENT),
        .handle = target->handle,
        .payload = (evaluation->frame_count - 1u) | ((uint64_t) machine_language_frame(evaluation)->serial << 32u),
    };

    return reference;
}

/**
 * @brief The frame index and serial a reference to @p node carries in its payload: those of the frame
 *        that created it, when it is a temporary node, which dies with that frame; 0 for a node of
 *        the namespace, which lives as long as the load.
 */
static uint64_t machine_language_node_lifetime(const MachineLanguageLoad_t *load, uint32_t node)
{
    const MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    uint32_t position = load->temporary_count;

    while (position != 0u && load->temporaries[position - 1u] != node)
        --position;
    if (position == 0u)
        return 0u;

    uint32_t frame = evaluation->frame_count;

    while (frame != 0u && evaluation->frames[frame - 1u].temporary_base >= position)
        --frame;
    return (frame == 0u) ? 0u : ((frame - 1u) | ((uint64_t) evaluation->frames[frame - 1u].serial << 32u));
}

/**
 * @brief Whether the frame a reference's payload names, by index and serial, still runs.
 */
static bool machine_language_frame_runs(const MachineLanguageEvaluation_t *evaluation, uint64_t payload)
{
    const uint32_t frame = (uint32_t) payload;

    return frame < evaluation->frame_count && evaluation->frames[frame].serial == (uint32_t) (payload >> 32u);
}

/**
 * @brief The node a name a package element holds resolves to, from the scope it was read in.
 */
static MachineLanguageStatus_t machine_language_resolve_element_name(MachineLanguageLoad_t *load,
                                                                     const MachineLanguageValue_t *name_reference,
                                                                     uint32_t *out_node)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table =
        &load->tables[name_reference->auxiliary - 1u];
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        (const uint8_t *) table->header, (uint32_t) name_reference->payload, table->length, &name, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return advanced_configuration_and_power_interface_machine_language_namespace_resolve(
        &load->space, name_reference->handle, (const uint8_t *) table->header, &name, out_node);
}

/**
 * @brief The place one reference refers to, without following it further: a node's value, a local or
 *        an argument of a frame still running, an element, or a byte.
 */
static MachineLanguageStatus_t machine_language_refer(MachineLanguageLoad_t *load,
                                                      const MachineLanguageValue_t *reference,
                                                      MachineLanguageLocation_t *out_location)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t frame = (uint32_t) reference->payload;
    MachineLanguageNode_t *node = NULL;

    memset(out_location, 0, sizeof(*out_location));
    switch (reference->kind)
    {
    case MACHINE_LANGUAGE_VALUE(REFERENCE_NODE):
        node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, reference->handle);
        if (node == NULL ||
            (node->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE) != 0u ||
            (reference->payload != 0u && !machine_language_frame_runs(evaluation, reference->payload)))
            return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
        out_location->kind = MACHINE_LANGUAGE_LOCATION_NODE;
        out_location->node = reference->handle;
        out_location->slot = &node->value;
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_VALUE(REFERENCE_LOCAL):
    case MACHINE_LANGUAGE_VALUE(REFERENCE_ARGUMENT):
        if (!machine_language_frame_runs(evaluation, reference->payload))
            return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
        out_location->kind = MACHINE_LANGUAGE_LOCATION_FRAME;
        out_location->slot = (reference->kind == MACHINE_LANGUAGE_VALUE(REFERENCE_LOCAL)) ?
                                 &evaluation->frames[frame].locals[reference->handle] :
                                 &evaluation->frames[frame].arguments[reference->handle];
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_VALUE(REFERENCE_ELEMENT):
        out_location->kind = MACHINE_LANGUAGE_LOCATION_ELEMENT;
        out_location->package = reference->handle;
        out_location->slot =
            (MachineLanguageValue_t *) advanced_configuration_and_power_interface_machine_language_object_payload(
                &load->heap, reference->handle) +
            reference->payload;
        return MACHINE_LANGUAGE_OK;
    default:
        out_location->kind = MACHINE_LANGUAGE_LOCATION_BYTE;
        out_location->byte = *reference;
        return MACHINE_LANGUAGE_OK;
    }
}

/**
 * @brief Follows a reference to the place whose value is no reference (§5.2), each name a package
 *        element holds resolved when @p names asks it, at most 8 of them.
 *
 * @return NAME_NOT_FOUND when a reference outlived its frame or a name names nothing;
 *         NAME_RESOLUTION_LOOP past 8 names; REFERENCE_CYCLE past MACHINE_LANGUAGE_MAX_REFERENCE_HOPS
 *         references; OK otherwise.
 */
static MachineLanguageStatus_t machine_language_locate(MachineLanguageLoad_t *load,
                                                       const MachineLanguageValue_t *reference, bool names,
                                                       MachineLanguageLocation_t *out_location)
{
    MachineLanguageValue_t current = *reference;
    uint32_t indirections = 0u;

    for (uint32_t hop = 0u; hop < MACHINE_LANGUAGE_MAX_REFERENCE_HOPS; ++hop)
    {
        uint32_t node = 0u;
        MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

        if (current.kind == MACHINE_LANGUAGE_VALUE(NAME_REFERENCE))
        {
            if (++indirections > MACHINE_LANGUAGE_MAX_NAME_INDIRECTIONS)
                return MACHINE_LANGUAGE_STATUS(NAME_RESOLUTION_LOOP);
            status = machine_language_resolve_element_name(load, &current, &node);
            if (status != MACHINE_LANGUAGE_OK)
                return status;
            current.kind = MACHINE_LANGUAGE_VALUE(REFERENCE_NODE);
            current.handle = node;
            current.payload = 0u;
            continue;
        }
        status = machine_language_refer(load, &current, out_location);
        if (status != MACHINE_LANGUAGE_OK || out_location->slot == NULL)
            return status;
        if (!machine_language_is_reference(out_location->slot->kind) &&
            !(names && out_location->slot->kind == MACHINE_LANGUAGE_VALUE(NAME_REFERENCE)))
            return MACHINE_LANGUAGE_OK;
        current = *out_location->slot;
    }
    return MACHINE_LANGUAGE_STATUS(REFERENCE_CYCLE);
}

/**
 * @brief The byte an Index of a Buffer or a String reads, as an Integer.
 */
static MachineLanguageStatus_t machine_language_read_byte(MachineLanguageLoad_t *load,
                                                          const MachineLanguageValue_t *byte,
                                                          MachineLanguageValue_t *out_value)
{
    const MachineLanguageHeader_t *object = machine_language_object(load, byte);

    if (byte->payload >= object->length)
        return MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
    *out_value = machine_language_integer(((const uint8_t *) (object + 1))[byte->payload]);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The value a place gives where it is read, its reference retained: a named object's, a
 *        local's, an argument's, an element's, or a byte's.
 */
static MachineLanguageStatus_t machine_language_read_location(MachineLanguageLoad_t *load,
                                                              const MachineLanguageLocation_t *location,
                                                              MachineLanguageValue_t *out_value)
{
    switch (location->kind)
    {
    case MACHINE_LANGUAGE_LOCATION_BYTE: return machine_language_read_byte(load, &location->byte, out_value);
    case MACHINE_LANGUAGE_LOCATION_NODE: return machine_language_read_object(load, location->node, out_value);
    case MACHINE_LANGUAGE_LOCATION_FRAME: *out_value = machine_language_read_slot(load, location->slot); break;
    default:
        if (location->slot->kind == MACHINE_LANGUAGE_VALUE(UNINITIALIZED))
            return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
        *out_value = *location->slot;
        machine_language_retain(load, out_value);
        break;
    }
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The value at the bottom of a chain of references, its reference retained (§5.2); a value
 *        that is no reference gives itself.
 */
static MachineLanguageStatus_t machine_language_dereference(MachineLanguageLoad_t *load,
                                                            const MachineLanguageValue_t *value,
                                                            MachineLanguageValue_t *out_value)
{
    MachineLanguageLocation_t location;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (!machine_language_is_reference(value->kind) && value->kind != MACHINE_LANGUAGE_VALUE(NAME_REFERENCE))
    {
        *out_value = *value;
        machine_language_retain(load, out_value);
        return MACHINE_LANGUAGE_OK;
    }
    status = machine_language_locate(load, value, true, &location);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_read_location(load, &location, out_value) : status;
}

/**
 * @brief The value a named object gives where a value is read, its reference retained: its data
 *        object, what a reference it holds refers to, as an alias does, a buffer field's bits, or what
 *        its field unit reads.
 */
static MachineLanguageStatus_t machine_language_read_node(MachineLanguageLoad_t *load, uint32_t index,
                                                          MachineLanguageValue_t *out_value)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (machine_language_is_reference(node->value.kind))
        return machine_language_dereference(load, &node->value, out_value);
    return machine_language_read_object(load, index, out_value);
}

/**
 * @brief The value an operand holds, its reference retained: a target gives what it names, through
 *        the references a local, an argument or a name holds; a reference gives what it refers to; any
 *        other value gives itself.
 */
static MachineLanguageStatus_t machine_language_read_target(MachineLanguageLoad_t *load,
                                                            const MachineLanguageValue_t *target,
                                                            MachineLanguageValue_t *out_value)
{
    const MachineLanguageValue_t *slot = NULL;

    if (target->kind != MACHINE_LANGUAGE_TARGET)
        return machine_language_dereference(load, target, out_value);
    switch (target->auxiliary)
    {
    case MACHINE_LANGUAGE_TARGET_LOCAL:
    case MACHINE_LANGUAGE_TARGET_ARGUMENT:
        slot = machine_language_slot(&load->evaluation, target);
        if (machine_language_is_reference(slot->kind))
            return machine_language_dereference(load, slot, out_value);
        *out_value = machine_language_read_slot(load, slot);
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_TARGET_NODE: return machine_language_read_node(load, target->handle, out_value);
    default: return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    }
}

/**
 * @brief Makes a value fit to be stored: a String, a Buffer or a Package another holder shares is
 *        copied, the shared one released (§5.2); one held by @p value alone is stored as it is.
 */
static MachineLanguageStatus_t machine_language_own(MachineLanguageLoad_t *load, MachineLanguageValue_t *value)
{
    MachineLanguageValue_t copy;

    if (value->kind != MACHINE_LANGUAGE_VALUE(STRING) && value->kind != MACHINE_LANGUAGE_VALUE(BUFFER) &&
        value->kind != MACHINE_LANGUAGE_VALUE(PACKAGE))
        return MACHINE_LANGUAGE_OK;
    if (machine_language_object(load, value)->references == 1u)
        return MACHINE_LANGUAGE_OK;

    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_value_copy(&load->heap, &load->space, value, &copy);

    machine_language_release(load, value);
    if (status == MACHINE_LANGUAGE_OK)
        *value = copy;
    return status;
}

/**
 * @brief Notes a value the walk for a reference cycle has still to visit, on the free part of the
 *        value stack: the value walk grows up from @p work_base, the marked packages down from the top.
 */
static MachineLanguageStatus_t machine_language_visit_later(MachineLanguageEvaluation_t *evaluation, uint32_t *work_top,
                                                            uint32_t marked_bottom, const MachineLanguageValue_t *value)
{
    if (value->kind != MACHINE_LANGUAGE_VALUE(PACKAGE) && !machine_language_is_reference(value->kind))
        return MACHINE_LANGUAGE_OK;
    if (*work_top == marked_bottom)
        return MACHINE_LANGUAGE_STATUS(LIMIT_VALUES);
    evaluation->values[(*work_top)++] = *value;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Visits one value of the walk for a reference cycle: whether it is the place being stored to,
 *        and what it leads to. A Package is visited once, marked, its mark noted for the end.
 */
static MachineLanguageStatus_t machine_language_visit(MachineLanguageLoad_t *load,
                                                      const MachineLanguageLocation_t *location,
                                                      const MachineLanguageValue_t *value, uint32_t *work_top,
                                                      uint32_t *marked_bottom)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageLocation_t referred;
    MachineLanguageHeader_t *package = NULL;

    if (value->kind == MACHINE_LANGUAGE_VALUE(PACKAGE) || value->kind == MACHINE_LANGUAGE_VALUE(REFERENCE_ELEMENT))
    {
        package = advanced_configuration_and_power_interface_machine_language_object(&load->heap, value->handle);
        if (location->kind == MACHINE_LANGUAGE_LOCATION_ELEMENT && value->handle == location->package)
            return MACHINE_LANGUAGE_STATUS(REFERENCE_CYCLE);
        if ((package->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_VISITED) != 0u)
            return MACHINE_LANGUAGE_OK;
        if (*marked_bottom == *work_top)
            return MACHINE_LANGUAGE_STATUS(LIMIT_VALUES);
        package->flags |= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_VISITED;
        evaluation->values[--*marked_bottom].handle = value->handle;
        for (uint32_t index = 0u; index < package->length; ++index)
        {
            const MachineLanguageStatus_t status = machine_language_visit_later(
                evaluation, work_top, *marked_bottom, (const MachineLanguageValue_t *) (package + 1) + index);

            if (status != MACHINE_LANGUAGE_OK)
                return status;
        }
        return MACHINE_LANGUAGE_OK;
    }
    if (value->kind == MACHINE_LANGUAGE_VALUE(REFERENCE_BYTE) ||
        machine_language_refer(load, value, &referred) != MACHINE_LANGUAGE_OK)
        return MACHINE_LANGUAGE_OK;
    if (referred.slot == location->slot)
        return MACHINE_LANGUAGE_STATUS(REFERENCE_CYCLE);
    return machine_language_visit_later(evaluation, work_top, *marked_bottom, referred.slot);
}

/**
 * @brief Refuses a value that would make the place it is stored to reach itself (§3.5): an iterative
 *        walk over what the value leads to, through packages and references, whose work list lies on
 *        the free part of the value stack.
 *
 * @return REFERENCE_CYCLE when the walk reaches the place; LIMIT_VALUES when the free part of the value
 *         stack cannot hold it; OK otherwise.
 */
static MachineLanguageStatus_t machine_language_check_cycle(MachineLanguageLoad_t *load,
                                                            const MachineLanguageLocation_t *location,
                                                            const MachineLanguageValue_t *value)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    uint32_t work_top = evaluation->value_count;
    uint32_t marked_bottom = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES;
    MachineLanguageStatus_t status = machine_language_visit_later(evaluation, &work_top, marked_bottom, value);

    for (uint32_t step = 0u; status == MACHINE_LANGUAGE_OK && work_top != evaluation->value_count; ++step)
    {
        const MachineLanguageValue_t visited = evaluation->values[--work_top];

        status = (step < MACHINE_LANGUAGE_MAX_CYCLE_STEPS) ?
                     machine_language_visit(load, location, &visited, &work_top, &marked_bottom) :
                     MACHINE_LANGUAGE_STATUS(REFERENCE_CYCLE);
    }
    for (uint32_t marked = marked_bottom;
         marked < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES; ++marked)
        advanced_configuration_and_power_interface_machine_language_object(&load->heap,
                                                                           evaluation->values[marked].handle)
            ->flags &= (uint8_t) ~ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OBJECT_VISITED;
    return status;
}

/**
 * @brief Puts a value, whose reference it takes, in a place of variable type, as a whole: what the
 *        place held is released, the value is owned first, a named object takes the value's type, and
 *        a value that would make the place reach itself is refused.
 */
static MachineLanguageStatus_t machine_language_place(MachineLanguageLoad_t *load,
                                                      const MachineLanguageLocation_t *location,
                                                      MachineLanguageValue_t *value)
{
    MachineLanguageStatus_t status = machine_language_own(load, value);

    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_check_cycle(load, location, value);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, value);
        return status;
    }
    if (location->kind == MACHINE_LANGUAGE_LOCATION_NODE)
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, location->node)->type =
            advanced_configuration_and_power_interface_machine_language_node_type_of_value(value->kind);
    machine_language_release(load, location->slot);
    *location->slot = *value;
    value->kind = MACHINE_LANGUAGE_VALUE(UNINITIALIZED);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The place a local or an argument target names, as a location.
 */
static MachineLanguageLocation_t machine_language_frame_location(MachineLanguageEvaluation_t *evaluation,
                                                                 const MachineLanguageValue_t *target)
{
    const MachineLanguageLocation_t location = {
        .kind = MACHINE_LANGUAGE_LOCATION_FRAME,
        .slot = machine_language_slot(evaluation, target),
    };

    return location;
}

/**
 * @brief The place a named object's value is, as a location.
 */
static MachineLanguageLocation_t machine_language_node_location(MachineLanguageLoad_t *load, uint32_t index)
{
    const MachineLanguageLocation_t location = {
        .kind = MACHINE_LANGUAGE_LOCATION_NODE,
        .node = index,
        .slot = &advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index)->value,
    };

    return location;
}

/**
 * @brief Copies a value's bytes into a String or a Buffer object, keeping its length: the rest is
 *        zero, and what does not fit is dropped, as Windows does (§5.2).
 */
static MachineLanguageStatus_t machine_language_copy_into(MachineLanguageLoad_t *load, uint32_t handle,
                                                          const MachineLanguageValue_t *value)
{
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    MachineLanguageHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, handle);
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_bytes(
        &load->heap, value, machine_language_bits(load), scratch, &bytes, &length);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

    const uint32_t copied = (length < object->length) ? length : object->length;

    memmove(object + 1, bytes, copied);
    memset((uint8_t *) (object + 1) + copied, 0, object->length - copied);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The Integer a value's first bytes make, as a Store to a named Integer reads them (§5.2).
 */
static MachineLanguageStatus_t machine_language_integer_from_bytes(MachineLanguageLoad_t *load,
                                                                   const MachineLanguageValue_t *value,
                                                                   uint64_t *out_integer)
{
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_bytes(
        &load->heap, value, machine_language_bits(load), scratch, &bytes, &length);

    *out_integer = 0u;
    for (uint32_t byte = 0u; status == MACHINE_LANGUAGE_OK && byte < length && byte < 8u; ++byte)
        *out_integer |= (uint64_t) bytes[byte] << (8u * byte);
    *out_integer = machine_language_mask(load, *out_integer);
    return status;
}

/**
 * @brief Writes a value into the byte an Index of a Buffer or a String refers to: an Integer's low
 *        byte, or the last byte of a String or a Buffer, as Windows writes it byte after byte.
 */
static MachineLanguageStatus_t machine_language_write_byte(MachineLanguageLoad_t *load,
                                                           const MachineLanguageValue_t *byte,
                                                           const MachineLanguageValue_t *value)
{
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    MachineLanguageHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, byte->handle);
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_bytes(
        &load->heap, value, machine_language_bits(load), scratch, &bytes, &length);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (byte->payload >= object->length)
        return MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
    if (length != 0u)
        ((uint8_t *) (object + 1))[byte->payload] =
            (value->kind == MACHINE_LANGUAGE_VALUE(INTEGER)) ? bytes[0] : bytes[length - 1u];
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Stores a value, whose reference it takes, to a named object whose value is no reference
 *        (§5.2): an Integer, a String or a Buffer takes its bytes and keeps its size, a Package takes a
 *        copy of a Package, a Name not built yet takes the value and its type, a buffer field takes
 *        its bits, a field unit is written; anything else refuses it.
 */
static MachineLanguageStatus_t machine_language_store_node(MachineLanguageLoad_t *load, uint32_t index,
                                                           MachineLanguageValue_t *value)
{
    MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
    const MachineLanguageLocation_t location = machine_language_node_location(load, index);
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    uint64_t integer = 0u;

    if (node->value.kind == MACHINE_LANGUAGE_VALUE(FIELD_UNIT))
        status = advanced_configuration_and_power_interface_machine_language_field_write(load, index, value);
    else if (node->value.kind == MACHINE_LANGUAGE_VALUE(BUFFER_FIELD))
        status = advanced_configuration_and_power_interface_machine_language_buffer_field_write(
            &load->heap, &node->value, value, machine_language_bits(load));
    else if (node->type == MACHINE_LANGUAGE_NODE(UNINITIALIZED) ||
             (node->type == MACHINE_LANGUAGE_NODE(PACKAGE) && value->kind == MACHINE_LANGUAGE_VALUE(PACKAGE)))
        return machine_language_place(load, &location, value);
    else if (node->type == MACHINE_LANGUAGE_NODE(INTEGER))
    {
        status = machine_language_integer_from_bytes(load, value, &integer);
        if (status == MACHINE_LANGUAGE_OK)
            node->value.payload = integer;
    }
    else if (node->type == MACHINE_LANGUAGE_NODE(STRING) || node->type == MACHINE_LANGUAGE_NODE(BUFFER))
        status = machine_language_copy_into(load, node->value.handle, value);
    else if (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL))
        status = MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
    machine_language_release(load, value);
    return status;
}

/**
 * @brief Stores a value, whose reference it takes, at a place a chain of references ended at, as
 *        @p mode says (§5.2): in place, a named object takes the value as a Store to it does and a
 *        local, an argument or an element of a computational type takes its bytes; overwriting or
 *        replacing, the value takes the place, except for a field, which is written, and a named
 *        object that holds no data, which only CopyObject replaces.
 */
static MachineLanguageStatus_t machine_language_store_at(MachineLanguageLoad_t *load,
                                                         const MachineLanguageLocation_t *location,
                                                         MachineLanguageValue_t *value, MachineLanguageStoreMode_t mode)
{
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    uint64_t integer = 0u;

    if (location->kind == MACHINE_LANGUAGE_LOCATION_BYTE)
        status = machine_language_write_byte(load, &location->byte, value);
    else if (location->kind == MACHINE_LANGUAGE_LOCATION_NODE)
    {
        const MachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, location->node);

        if (mode == MACHINE_LANGUAGE_STORE_IN_PLACE || node->value.kind == MACHINE_LANGUAGE_VALUE(FIELD_UNIT) ||
            node->value.kind == MACHINE_LANGUAGE_VALUE(BUFFER_FIELD))
            return machine_language_store_node(load, location->node, value);
        if (mode == MACHINE_LANGUAGE_STORE_REPLACE || !machine_language_holds_no_data(node->type))
            return machine_language_place(load, location, value);
    }
    else if (mode != MACHINE_LANGUAGE_STORE_IN_PLACE || location->slot->kind == MACHINE_LANGUAGE_VALUE(UNINITIALIZED) ||
             location->slot->kind == MACHINE_LANGUAGE_VALUE(PACKAGE))
        return machine_language_place(load, location, value);
    else if (location->slot->kind == MACHINE_LANGUAGE_VALUE(INTEGER))
    {
        status = machine_language_integer_from_bytes(load, value, &integer);
        if (status == MACHINE_LANGUAGE_OK)
            location->slot->payload = integer;
    }
    else if (location->slot->kind == MACHINE_LANGUAGE_VALUE(STRING) ||
             location->slot->kind == MACHINE_LANGUAGE_VALUE(BUFFER))
        status = machine_language_copy_into(load, location->slot->handle, value);
    machine_language_release(load, value);
    return status;
}

/**
 * @brief Stores a value, whose reference it takes, through a reference, to the place whose value is
 *        no reference, as @p mode says.
 */
static MachineLanguageStatus_t machine_language_store_through(MachineLanguageLoad_t *load,
                                                              const MachineLanguageValue_t *reference,
                                                              MachineLanguageValue_t *value,
                                                              MachineLanguageStoreMode_t mode)
{
    MachineLanguageLocation_t location;
    const MachineLanguageStatus_t status = machine_language_locate(load, reference, false, &location);

    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, value);
        return status;
    }
    return machine_language_store_at(load, &location, value, mode);
}

/**
 * @brief Stores a value, whose reference it takes, through a reference a store names as its target:
 *        an Index of a Package replaces the element, unless the element is itself a reference, which
 *        is followed with @p mode; any other reference is followed as @p mode says.
 */
static MachineLanguageStatus_t machine_language_store_to_reference(MachineLanguageLoad_t *load,
                                                                   const MachineLanguageValue_t *target,
                                                                   MachineLanguageValue_t *value,
                                                                   MachineLanguageStoreMode_t mode)
{
    MachineLanguageLocation_t location;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (target->kind != MACHINE_LANGUAGE_VALUE(REFERENCE_ELEMENT))
        return machine_language_store_through(load, target, value, mode);
    status = machine_language_refer(load, target, &location);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, value);
        return status;
    }
    if (machine_language_is_reference(location.slot->kind))
        return machine_language_store_through(load, location.slot, value, mode);
    return machine_language_place(load, &location, value);
}

/**
 * @brief Whether a store to a local that holds a reference replaces the reference rather than going
 *        through it (§5.2): the value is a reference itself, or the reference leads, through the names
 *        package elements hold, to nothing or to an object that holds no data, as firmware that stores
 *        CondRefOf twice to Local0 expects.
 */
static bool machine_language_rebinds(MachineLanguageLoad_t *load, const MachineLanguageValue_t *slot,
                                     const MachineLanguageValue_t *value)
{
    MachineLanguageLocation_t location;

    if (machine_language_is_reference(value->kind) ||
        machine_language_locate(load, slot, true, &location) != MACHINE_LANGUAGE_OK)
        return true;
    return location.kind == MACHINE_LANGUAGE_LOCATION_NODE &&
           machine_language_holds_no_data(
               advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, location.node)
                   ->type);
}

/**
 * @brief Stores a value where a target says, and takes the references both hold (§5.2): NullName
 *        drops it; a local is overwritten, unless it holds a reference to data, which takes the value
 *        in place; an argument is overwritten, or what the reference it holds refers to; a named
 *        object takes it, through the reference it holds if it holds one; Debug counts it; a
 *        reference is followed; a value that is no target, such as a constant, takes nothing.
 */
static MachineLanguageStatus_t machine_language_store(MachineLanguageLoad_t *load, MachineLanguageValue_t *value,
                                                      MachineLanguageValue_t *target)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    MachineLanguageNode_t *node = NULL;
    MachineLanguageValue_t *slot = NULL;

    if (machine_language_is_reference(target->kind))
        status = machine_language_store_to_reference(load, target, value, MACHINE_LANGUAGE_STORE_IN_PLACE);
    else if (target->kind != MACHINE_LANGUAGE_TARGET)
        machine_language_release(load, value);
    else if (target->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL ||
             target->auxiliary == MACHINE_LANGUAGE_TARGET_ARGUMENT)
    {
        slot = machine_language_slot(evaluation, target);
        if (!machine_language_is_reference(slot->kind) ||
            (target->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL && machine_language_rebinds(load, slot, value)))
        {
            const MachineLanguageLocation_t location = machine_language_frame_location(evaluation, target);

            status = machine_language_place(load, &location, value);
        }
        else
            status = machine_language_store_through(load, slot, value,
                                                    (target->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL) ?
                                                        MACHINE_LANGUAGE_STORE_IN_PLACE :
                                                        MACHINE_LANGUAGE_STORE_OVERWRITE);
    }
    else if (target->auxiliary == MACHINE_LANGUAGE_TARGET_NODE)
    {
        node = advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, target->handle);
        status = machine_language_is_reference(node->value.kind) ?
                     machine_language_store_through(load, &node->value, value, MACHINE_LANGUAGE_STORE_IN_PLACE) :
                     machine_language_store_node(load, target->handle, value);
    }
    else
    {
        evaluation->debug_stores += (target->auxiliary == MACHINE_LANGUAGE_TARGET_DEBUG) ? 1u : 0u;
        machine_language_release(load, value);
    }
    machine_language_release(load, target);
    return status;
}

/**
 * @brief Runs CopyObject's store, which takes the references the value and the target hold: a local
 *        is overwritten, even one that holds a reference; an argument is overwritten, or what the
 *        reference it holds refers to; a named object takes the value and its type, through the
 *        reference it holds if it holds one; a field is written; a reference is followed and what it
 *        refers to replaced.
 */
static MachineLanguageStatus_t machine_language_copy_object(MachineLanguageLoad_t *load, MachineLanguageValue_t *value,
                                                            MachineLanguageValue_t *target)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    MachineLanguageLocation_t location;
    MachineLanguageValue_t *slot = NULL;

    if (machine_language_is_reference(target->kind))
        status = machine_language_store_to_reference(load, target, value, MACHINE_LANGUAGE_STORE_REPLACE);
    else if (target->kind != MACHINE_LANGUAGE_TARGET || target->auxiliary == MACHINE_LANGUAGE_TARGET_NONE ||
             target->auxiliary == MACHINE_LANGUAGE_TARGET_DEBUG || target->auxiliary == MACHINE_LANGUAGE_TARGET_MISSING)
        return machine_language_store(load, value, target);
    else if (target->auxiliary == MACHINE_LANGUAGE_TARGET_NODE)
    {
        location = machine_language_node_location(load, target->handle);
        status = machine_language_is_reference(location.slot->kind) ?
                     machine_language_store_through(load, location.slot, value, MACHINE_LANGUAGE_STORE_REPLACE) :
                     machine_language_store_at(load, &location, value, MACHINE_LANGUAGE_STORE_REPLACE);
    }
    else
    {
        slot = machine_language_slot(evaluation, target);
        location = machine_language_frame_location(evaluation, target);
        status = (target->auxiliary == MACHINE_LANGUAGE_TARGET_ARGUMENT && machine_language_is_reference(slot->kind)) ?
                     machine_language_store_through(load, slot, value, MACHINE_LANGUAGE_STORE_REPLACE) :
                     machine_language_place(load, &location, value);
    }
    machine_language_release(load, target);
    return status;
}

/**
 * @brief Stores a result to its target, and hands it to the operator waiting for it, or releases it
 *        when none is; it takes the references the result and the target hold.
 */
static MachineLanguageStatus_t machine_language_store_and_deliver(MachineLanguageLoad_t *load,
                                                                  MachineLanguageValue_t result,
                                                                  MachineLanguageValue_t *target)
{
    const bool used = machine_language_active(&load->evaluation) != NULL;
    MachineLanguageValue_t stored = result;

    if (used)
        machine_language_retain(load, &result);

    const MachineLanguageStatus_t status = machine_language_store(load, &stored, target);

    if (!used)
        return status;
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &result);
        return status;
    }
    return machine_language_push(load, &result);
}

/**
 * @brief Ends an operator that stores its result: on success, the result goes to its target and to the
 *        operator waiting for it; on failure, the target is released. Takes both references.
 */
static MachineLanguageStatus_t machine_language_finish(MachineLanguageLoad_t *load, MachineLanguageStatus_t status,
                                                       MachineLanguageValue_t result, MachineLanguageValue_t *target)
{
    if (status == MACHINE_LANGUAGE_OK)
        return machine_language_store_and_deliver(load, result, target);
    machine_language_release(load, target);
    return status;
}

/**
 * @brief Opens a construct of the running frame, in the scope of the one around it.
 */
static MachineLanguageStatus_t machine_language_open(MachineLanguageEvaluation_t *evaluation, uint8_t kind,
                                                     uint32_t start, uint32_t end, uint32_t scope)
{
    if (evaluation->block_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_BLOCKS);

    MachineLanguageBlock_t *block = &evaluation->blocks[evaluation->block_count++];

    block->kind = kind;
    block->placement = 0u;
    block->pending_base = (uint16_t) evaluation->operator_count;
    block->end = end;
    block->scope = scope;
    block->start = start;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Pushes a frame for a method whose arguments are the top @p argument_count values, which it
 *        takes with their references.
 */
static MachineLanguageStatus_t machine_language_enter_method(MachineLanguageLoad_t *load, uint32_t method,
                                                             uint32_t argument_count)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, method);

    if (node->type != MACHINE_LANGUAGE_NODE(METHOD))
        return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
    /* TODO(#540): \_OSI, the one method without a body, runs in C. */
    if (node->value.handle == 0u)
        return MACHINE_LANGUAGE_STATUS(UNIMPLEMENTED_OPERATOR);
    if (evaluation->frame_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);

    const uint32_t body = (uint32_t) node->value.payload;
    MachineLanguageFrame_t *frame = &evaluation->frames[evaluation->frame_count++];

    evaluation->value_count -= argument_count;
    frame->code = (const uint8_t *) load->tables[node->value.handle - 1u].header;
    frame->pc = body;
    frame->end = body + (uint32_t) (node->value.payload >> 32u);
    frame->method = method;
    frame->scope = method;
    memset(frame->arguments, 0, sizeof(frame->arguments));
    memset(frame->locals, 0, sizeof(frame->locals));
    memcpy(frame->arguments, &evaluation->values[evaluation->value_count],
           argument_count * sizeof(MachineLanguageValue_t));
    frame->value_base = (uint16_t) evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = (uint16_t) node->value.handle;
    frame->argument_count = (uint8_t) argument_count;
    frame->reserved = 0u;
    frame->serial = ++evaluation->frame_serial;
    if (evaluation->frame_count > evaluation->frames_peak)
        evaluation->frames_peak = evaluation->frame_count;
    return machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_METHOD, body, frame->end, method);
}

/**
 * @brief Pops the running frame: removes the nodes it created, the last first, and releases what
 *        they, its stacks, its locals and its arguments hold.
 */
static void machine_language_drop_frame(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    while (load->temporary_count > frame->temporary_base)
    {
        const uint32_t temporary = load->temporaries[--load->temporary_count];

        machine_language_release(
            load, &advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, temporary)
                       ->value);
        advanced_configuration_and_power_interface_machine_language_namespace_remove(&load->space, temporary);
    }
    while (evaluation->value_count > frame->value_base)
        machine_language_release(load, &evaluation->values[--evaluation->value_count]);
    for (uint32_t slot = 0u; slot < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LOCALS; ++slot)
        machine_language_release(load, &frame->locals[slot]);
    for (uint32_t slot = 0u; slot < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ARGUMENTS; ++slot)
        machine_language_release(load, &frame->arguments[slot]);
    evaluation->operator_count = frame->operator_base;
    evaluation->block_count = frame->block_base;
    --evaluation->frame_count;
}

/**
 * @brief Ends the running method and gives its caller the result: @p value, whose reference it
 *        takes, a reference to one of the method's own locals, arguments or temporary nodes replaced
 *        by what it refers to, which outlives it (§5.2); or for a method that ended without Return
 *        while its caller uses the result, Integer 0, counted.
 */
static MachineLanguageStatus_t machine_language_return(MachineLanguageLoad_t *load, MachineLanguageValue_t *value)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t copy;

    if (value != NULL &&
        (value->kind == MACHINE_LANGUAGE_VALUE(REFERENCE_LOCAL) ||
         value->kind == MACHINE_LANGUAGE_VALUE(REFERENCE_ARGUMENT) ||
         (value->kind == MACHINE_LANGUAGE_VALUE(REFERENCE_NODE) && value->payload != 0u)) &&
        (uint32_t) value->payload == evaluation->frame_count - 1u)
    {
        const MachineLanguageStatus_t status = machine_language_dereference(load, value, &copy);

        if (status != MACHINE_LANGUAGE_OK)
            return status;
        *value = copy;
    }
    machine_language_drop_frame(load);
    if (value != NULL)
        return machine_language_deliver(load, *value);
    if (machine_language_active(evaluation) == NULL)
        return MACHINE_LANGUAGE_OK;
    ++evaluation->implicit_returns;
    return machine_language_deliver(load, machine_language_integer(0u));
}

/**
 * @brief Counts a back-edge of a While, and fails once the evaluation has taken too many.
 */
static MachineLanguageStatus_t machine_language_count_iteration(MachineLanguageEvaluation_t *evaluation)
{
    return (++evaluation->iterations >
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS) ?
               MACHINE_LANGUAGE_STATUS(LIMIT_LOOP_ITERATIONS) :
               MACHINE_LANGUAGE_OK;
}

/**
 * @brief Moves the running frame past an Else that starts at its pc, if one does.
 */
static MachineLanguageStatus_t machine_language_skip_else(MachineLanguageEvaluation_t *evaluation)
{
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const uint32_t limit = machine_language_limit(evaluation);
    uint32_t end = 0u;
    uint32_t after = 0u;

    if (frame->pc >= limit || frame->code[frame->pc] != MACHINE_LANGUAGE_ELSE_BYTE)
        return MACHINE_LANGUAGE_OK;

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_package_end(
        frame->code, frame->pc + 1u, limit, &end, &after);

    if (status == MACHINE_LANGUAGE_OK)
        frame->pc = end;
    return status;
}

/**
 * @brief Enters the body of an Else that starts at the running frame's pc, after an If not taken.
 */
static MachineLanguageStatus_t machine_language_enter_else(MachineLanguageEvaluation_t *evaluation)
{
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const uint32_t limit = machine_language_limit(evaluation);
    const uint32_t start = frame->pc;
    uint32_t end = 0u;
    uint32_t after = 0u;

    if (start >= limit || frame->code[start] != MACHINE_LANGUAGE_ELSE_BYTE)
        return MACHINE_LANGUAGE_OK;

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_package_end(
        frame->code, start + 1u, limit, &end, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    frame->pc = after;
    return machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_TERMS, start, end,
                                 machine_language_block(evaluation)->scope);
}

/**
 * @brief Ends the construct the running frame is in: a method returns, a While goes back to its
 *        predicate, an If skips the Else after it.
 */
static MachineLanguageStatus_t machine_language_close_block(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguageBlock_t block = *machine_language_block(evaluation);

    if (block.kind == MACHINE_LANGUAGE_RUN_METHOD)
        return machine_language_return(load, NULL);
    --evaluation->block_count;
    if (block.kind == MACHINE_LANGUAGE_RUN_IF)
        return machine_language_skip_else(evaluation);
    if (block.kind != MACHINE_LANGUAGE_RUN_WHILE)
        return MACHINE_LANGUAGE_OK;
    machine_language_frame(evaluation)->pc = block.start;
    return machine_language_count_iteration(evaluation);
}

/**
 * @brief Runs a Break or a Continue: leaves the innermost While of the running frame, past its end
 *        or back to its predicate.
 */
static MachineLanguageStatus_t machine_language_leave_loop(MachineLanguageEvaluation_t *evaluation, bool again)
{
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    for (uint32_t index = evaluation->block_count; index > frame->block_base; --index)
    {
        const MachineLanguageBlock_t *block = &evaluation->blocks[index - 1u];

        if (block->kind != MACHINE_LANGUAGE_RUN_WHILE)
            continue;
        frame->pc = again ? block->start : block->end;
        evaluation->operator_count = block->pending_base;
        evaluation->block_count = index - 1u;
        return again ? machine_language_count_iteration(evaluation) : MACHINE_LANGUAGE_OK;
    }
    return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
}

/**
 * @brief Whether the interpreter runs an opcode that pushes an operator: control flow, Store and
 *        CopyObject, Name and the buffer fields in a method, and the operators on integers, strings,
 *        buffers, packages and references the needs-path closure reaches (§4.1).
 */
static bool machine_language_runs(uint32_t opcode)
{
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(IF):
    case MACHINE_LANGUAGE_OPCODE(WHILE):
    case MACHINE_LANGUAGE_OPCODE(RETURN):
    case MACHINE_LANGUAGE_OPCODE(NAME):
    case MACHINE_LANGUAGE_OPCODE(STORE):
    case MACHINE_LANGUAGE_OPCODE(COPY_OBJECT):
    case MACHINE_LANGUAGE_OPCODE(BUFFER):
    case MACHINE_LANGUAGE_OPCODE(PACKAGE):
    case MACHINE_LANGUAGE_OPCODE(VARIABLE_PACKAGE):
    case MACHINE_LANGUAGE_OPCODE(ADD):
    case MACHINE_LANGUAGE_OPCODE(SUBTRACT):
    case MACHINE_LANGUAGE_OPCODE(MULTIPLY):
    case MACHINE_LANGUAGE_OPCODE(DIVIDE):
    case MACHINE_LANGUAGE_OPCODE(MOD):
    case MACHINE_LANGUAGE_OPCODE(INCREMENT):
    case MACHINE_LANGUAGE_OPCODE(DECREMENT):
    case MACHINE_LANGUAGE_OPCODE(SHIFT_LEFT):
    case MACHINE_LANGUAGE_OPCODE(SHIFT_RIGHT):
    case MACHINE_LANGUAGE_OPCODE(AND):
    case MACHINE_LANGUAGE_OPCODE(OR):
    case MACHINE_LANGUAGE_OPCODE(XOR):
    case MACHINE_LANGUAGE_OPCODE(NOT):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_AND):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_OR):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_GREATER):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS):
    case MACHINE_LANGUAGE_OPCODE(CONCATENATE):
    case MACHINE_LANGUAGE_OPCODE(SIZE_OF):
    case MACHINE_LANGUAGE_OPCODE(OBJECT_TYPE):
    case MACHINE_LANGUAGE_OPCODE(TO_BUFFER):
    case MACHINE_LANGUAGE_OPCODE(TO_DECIMAL_STRING):
    case MACHINE_LANGUAGE_OPCODE(TO_HEX_STRING):
    case MACHINE_LANGUAGE_OPCODE(TO_INTEGER):
    case MACHINE_LANGUAGE_OPCODE(TO_STRING):
    case MACHINE_LANGUAGE_OPCODE(MID):
    case MACHINE_LANGUAGE_OPCODE(REFERENCE_OF):
    case MACHINE_LANGUAGE_OPCODE(CONDITIONAL_REFERENCE_OF):
    case MACHINE_LANGUAGE_OPCODE(DEREFERENCE_OF):
    case MACHINE_LANGUAGE_OPCODE(INDEX):
    case MACHINE_LANGUAGE_OPCODE(MATCH):
    case MACHINE_LANGUAGE_OPCODE(CREATE_BIT_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_BYTE_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_DOUBLE_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_QUAD_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_FIELD): return true;
    default: return false;
    }
}

/**
 * @brief Pushes the operator whose opcode was read at @p start, after reading its PkgLength.
 */
static MachineLanguageStatus_t machine_language_push_operator(MachineLanguageEvaluation_t *evaluation, uint32_t opcode,
                                                              uint32_t start, uint32_t after)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *descriptor =
        advanced_configuration_and_power_interface_machine_language_descriptor(opcode);
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    uint32_t end = machine_language_limit(evaluation);

    if ((descriptor->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH) != 0u)
    {
        const MachineLanguageStatus_t status =
            advanced_configuration_and_power_interface_machine_language_read_package_end(frame->code, after, end, &end,
                                                                                         &after);

        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *pending = &evaluation->operators[evaluation->operator_count++];

    pending->opcode = (uint16_t) opcode;
    pending->operands_done = 0u;
    pending->operand_count = descriptor->operand_count;
    pending->start = start;
    pending->end = end;
    pending->node = 0u;
    frame->pc = after;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Reads an integer constant: Zero, One, Ones, or a prefix and its little-endian bytes.
 */
static MachineLanguageStatus_t machine_language_constant(MachineLanguageLoad_t *load, uint32_t opcode, uint32_t after,
                                                         uint32_t limit)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    const uint32_t width = (opcode == MACHINE_LANGUAGE_OPCODE(BYTE_PREFIX))        ? 1u :
                           (opcode == MACHINE_LANGUAGE_OPCODE(WORD_PREFIX))        ? 2u :
                           (opcode == MACHINE_LANGUAGE_OPCODE(DOUBLE_WORD_PREFIX)) ? 4u :
                           (opcode == MACHINE_LANGUAGE_OPCODE(QUAD_WORD_PREFIX))   ? 8u :
                                                                                     0u;
    uint64_t integer = (opcode == MACHINE_LANGUAGE_OPCODE(ONE)) ? 1u : 0u;

    if (limit - after < width)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    for (uint32_t byte = 0u; byte < width; ++byte)
        integer |= (uint64_t) frame->code[after + byte] << (8u * byte);
    if (opcode == MACHINE_LANGUAGE_OPCODE(ONES))
        integer = ~0ull;
    frame->pc = after + width;
    return machine_language_deliver(load, machine_language_integer(machine_language_mask(load, integer)));
}

/**
 * @brief Builds the String whose characters start at @p after, up to their NUL.
 */
static MachineLanguageStatus_t machine_language_string_literal(MachineLanguageLoad_t *load, uint32_t after,
                                                               uint32_t limit)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    uint32_t end = after;
    MachineLanguageValue_t value;

    while (end < limit && frame->code[end] != 0x00u)
        ++end;
    if (end == limit)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        &load->heap, &load->space, MACHINE_LANGUAGE_VALUE(STRING), end - after, &value);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    memcpy(advanced_configuration_and_power_interface_machine_language_object_payload(&load->heap, value.handle),
           frame->code + after, end - after);
    frame->pc = end + 1u;
    return machine_language_deliver(load, value);
}

/**
 * @brief Reads a name that stands as a package element: it stays a name, resolved when the element is
 *        read (§5.1).
 */
static MachineLanguageStatus_t machine_language_name_element(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    MachineLanguageNameString_t name;
    MachineLanguageValue_t value;
    uint32_t after = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        frame->code, frame->pc, machine_language_limit(evaluation), &name, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    value.kind = MACHINE_LANGUAGE_VALUE(NAME_REFERENCE);
    value.flags = 0u;
    value.auxiliary = frame->table;
    value.handle = machine_language_block(evaluation)->scope;
    value.payload = frame->pc;
    frame->pc = after;
    return machine_language_deliver(load, value);
}

/**
 * @brief Whether the name being read is the SimpleName of a CondRefOf, which asks whether it names an
 *        object rather than failing when it does not.
 */
static bool machine_language_asks_existence(MachineLanguageEvaluation_t *evaluation)
{
    const MachineLanguagePendingOperator_t *active = machine_language_active(evaluation);

    return active != NULL && active->opcode == MACHINE_LANGUAGE_OPCODE(CONDITIONAL_REFERENCE_OF) &&
           active->operands_done == 1u;
}

/**
 * @brief Runs a NameString read where @p kind is expected: a method is called, unless the operand is
 *        a SimpleName; a target position names the object; anywhere else the object gives its value.
 *        The SimpleName of a CondRefOf that names nothing, or only an External, gives a missing target.
 */
static MachineLanguageStatus_t machine_language_name_term(MachineLanguageLoad_t *load, uint32_t kind)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const uint32_t start = frame->pc;
    const uint32_t limit = machine_language_limit(evaluation);
    const bool target = kind == MACHINE_LANGUAGE_OPERAND(SUPER_NAME) || kind == MACHINE_LANGUAGE_OPERAND(TARGET) ||
                        kind == MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME);
    MachineLanguageNameString_t name;
    MachineLanguageValue_t value;
    uint32_t after = 0u;
    uint32_t index = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        frame->code, start, limit, &name, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    frame->pc = after;
    if (advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, machine_language_block(evaluation)->scope, frame->code, &name, &index) != MACHINE_LANGUAGE_OK)
        return machine_language_asks_existence(evaluation) ?
                   machine_language_deliver(load, machine_language_target(MACHINE_LANGUAGE_TARGET_MISSING, 0u)) :
                   MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);

    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL) && machine_language_asks_existence(evaluation))
        return machine_language_deliver(load, machine_language_target(MACHINE_LANGUAGE_TARGET_MISSING, 0u));

    if (node->type == MACHINE_LANGUAGE_NODE(METHOD) && kind != MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME))
    {
        const uint32_t arguments = node->value.auxiliary & 0x7u;

        if (arguments == 0u)
            return machine_language_enter_method(load, index, 0u);
        if (evaluation->operator_count ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
            return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

        MachineLanguagePendingOperator_t *call = &evaluation->operators[evaluation->operator_count++];

        call->opcode = MACHINE_LANGUAGE_CALL;
        call->operands_done = 0u;
        call->operand_count = (uint8_t) arguments;
        call->start = start;
        call->end = limit;
        call->node = index;
        return MACHINE_LANGUAGE_OK;
    }
    if (target)
        return machine_language_deliver(load, machine_language_target(MACHINE_LANGUAGE_TARGET_NODE, index));
    status = machine_language_read_node(load, index, &value);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, value) : status;
}

/**
 * @brief Whether an opcode may stand as an operand: no statement, and a data object where @p data_only
 *        asks for one.
 */
static bool machine_language_fits_operand(uint32_t opcode, bool data_only)
{
    const uint8_t flags = advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags;

    if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_STATEMENT) != 0u)
        return false;
    return !data_only || (flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT) != 0u;
}

/**
 * @brief Runs one term of the running frame where @p kind is expected: a statement of a TermList
 *        when @p kind is NONE, where a statement is refused otherwise; a package element or a data
 *        object (MACHINE_LANGUAGE_DATA_OBJECT) must be a data object, and an element may be a name.
 */
static MachineLanguageStatus_t machine_language_term(MachineLanguageLoad_t *load, uint32_t kind)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const uint32_t start = frame->pc;
    const uint32_t limit = machine_language_limit(evaluation);
    const bool target = kind == MACHINE_LANGUAGE_OPERAND(SUPER_NAME) || kind == MACHINE_LANGUAGE_OPERAND(TARGET) ||
                        kind == MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME);
    const bool element = kind == MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST);
    const bool data_only = element || kind == MACHINE_LANGUAGE_DATA_OBJECT;
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (start >= limit)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    if (frame->code[start] == 0x00u && target)
    {
        frame->pc = start + 1u;
        return machine_language_deliver(load, machine_language_target(MACHINE_LANGUAGE_TARGET_NONE, 0u));
    }
    if (advanced_configuration_and_power_interface_machine_language_starts_name(frame->code[start]))
        return element   ? machine_language_name_element(load) :
               data_only ? MACHINE_LANGUAGE_STATUS(INVALID_OPCODE) :
                           machine_language_name_term(load, kind);

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        frame->code, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (kind != MACHINE_LANGUAGE_OPERAND(NONE) && !machine_language_fits_operand(opcode, data_only))
        return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
    if (opcode >= MACHINE_LANGUAGE_OPCODE(LOCAL0) && opcode <= MACHINE_LANGUAGE_OPCODE(LOCAL7))
    {
        const uint32_t slot = opcode - MACHINE_LANGUAGE_OPCODE(LOCAL0);

        frame->pc = after;
        return machine_language_deliver(load, target ? machine_language_target(MACHINE_LANGUAGE_TARGET_LOCAL, slot) :
                                                       machine_language_read_slot(load, &frame->locals[slot]));
    }
    if (opcode >= MACHINE_LANGUAGE_OPCODE(ARGUMENT0) && opcode <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6))
    {
        const uint32_t slot = opcode - MACHINE_LANGUAGE_OPCODE(ARGUMENT0);

        frame->pc = after;
        return machine_language_deliver(load, target ? machine_language_target(MACHINE_LANGUAGE_TARGET_ARGUMENT, slot) :
                                                       machine_language_read_slot(load, &frame->arguments[slot]));
    }
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(ZERO):
    case MACHINE_LANGUAGE_OPCODE(ONE):
    case MACHINE_LANGUAGE_OPCODE(ONES):
    case MACHINE_LANGUAGE_OPCODE(BYTE_PREFIX):
    case MACHINE_LANGUAGE_OPCODE(WORD_PREFIX):
    case MACHINE_LANGUAGE_OPCODE(DOUBLE_WORD_PREFIX):
    case MACHINE_LANGUAGE_OPCODE(QUAD_WORD_PREFIX): return machine_language_constant(load, opcode, after, limit);
    case MACHINE_LANGUAGE_OPCODE(STRING_PREFIX): return machine_language_string_literal(load, after, limit);
    case MACHINE_LANGUAGE_OPCODE(DEBUG):
        frame->pc = after;
        return target ? machine_language_deliver(load, machine_language_target(MACHINE_LANGUAGE_TARGET_DEBUG, 0u)) :
                        MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    case MACHINE_LANGUAGE_OPCODE(NOOP): frame->pc = after; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPCODE(BREAK):
    case MACHINE_LANGUAGE_OPCODE(CONTINUE):
        frame->pc = after;
        return machine_language_leave_loop(evaluation, opcode == MACHINE_LANGUAGE_OPCODE(CONTINUE));
    case MACHINE_LANGUAGE_OPCODE(ELSE): return machine_language_skip_else(evaluation);
    default: break;
    }
    if (!machine_language_runs(opcode))
    {
        ++evaluation->unimplemented[opcode];
        return MACHINE_LANGUAGE_STATUS(UNIMPLEMENTED_OPERATOR);
    }
    return machine_language_push_operator(evaluation, opcode, start, after);
}

/**
 * @brief Creates the temporary node a Name in a method declares; a name that exists already, such
 *        as one the same frame created in an earlier turn of a loop, fails (§5.1).
 */
static MachineLanguageStatus_t machine_language_declare_name(MachineLanguageLoad_t *load,
                                                             MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    uint32_t parent = 0u;
    uint32_t segment = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        frame->code, frame->pc, active->end, &name, &after);

    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &load->space, machine_language_block(evaluation)->scope, frame->code, &name, &parent, &segment);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent,
                                                                                         segment) != 0u)
        return MACHINE_LANGUAGE_STATUS(NAME_EXISTS);
    if (load->temporary_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_TEMPORARY_NODES);
    status = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        &load->space, parent, segment, MACHINE_LANGUAGE_NODE(UNINITIALIZED), frame->table, &active->node);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    load->temporaries[load->temporary_count++] = active->node;
    frame->pc = after;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs the TermList of an If or a While once its predicate is on the stack: a true predicate
 *        opens the body; a false one skips it, and an If then enters the Else that follows.
 */
static MachineLanguageStatus_t machine_language_branch(MachineLanguageLoad_t *load,
                                                       const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguagePendingOperator_t branch = *active;
    MachineLanguageValue_t predicate = machine_language_pop(evaluation);
    uint64_t truth = 0u;
    const MachineLanguageStatus_t status = machine_language_integer_of(load, &predicate, &truth);

    machine_language_release(load, &predicate);
    --evaluation->operator_count;
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (truth != 0u)
        return machine_language_open(evaluation,
                                     (branch.opcode == MACHINE_LANGUAGE_OPCODE(WHILE)) ? MACHINE_LANGUAGE_RUN_WHILE :
                                                                                         MACHINE_LANGUAGE_RUN_IF,
                                     branch.start, branch.end, machine_language_block(evaluation)->scope);
    machine_language_frame(evaluation)->pc = branch.end;
    return (branch.opcode == MACHINE_LANGUAGE_OPCODE(WHILE)) ? MACHINE_LANGUAGE_OK :
                                                               machine_language_enter_else(evaluation);
}

/**
 * @brief Reads a ByteData, WordData, DWordData or QWordData operand as an Integer.
 */
static MachineLanguageStatus_t machine_language_fixed_operand(MachineLanguageLoad_t *load,
                                                              const MachineLanguagePendingOperator_t *active,
                                                              uint32_t width)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    uint64_t integer = 0u;

    if (active->end - frame->pc < width)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    for (uint32_t byte = 0u; byte < width; ++byte)
        integer |= (uint64_t) frame->code[frame->pc + byte] << (8u * byte);
    frame->pc += width;
    return machine_language_deliver(load, machine_language_integer(integer));
}

/**
 * @brief Runs the next element of a PackageElementList, the values gathering on the stack above the
 *        count; the list is done when its end is reached.
 */
static MachineLanguageStatus_t machine_language_package_elements(MachineLanguageLoad_t *load,
                                                                 MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (active->node == 0u)
        active->node = evaluation->value_count + 1u;
    if (machine_language_frame(evaluation)->pc < active->end)
        return machine_language_term(load, MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST));
    ++active->operands_done;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs the next operand of the active operator.
 */
static MachineLanguageStatus_t machine_language_operand(MachineLanguageLoad_t *load,
                                                        MachineLanguagePendingOperator_t *active)
{
    const uint32_t kind =
        (active->opcode >= MACHINE_LANGUAGE_CALL) ?
            MACHINE_LANGUAGE_OPERAND(TERM_ARGUMENT) :
            advanced_configuration_and_power_interface_machine_language_operand_kind(
                advanced_configuration_and_power_interface_machine_language_descriptor(active->opcode),
                active->operands_done);

    if (kind == MACHINE_LANGUAGE_OPERAND(PACKAGE_ELEMENT_LIST))
        return machine_language_package_elements(load, active);
    ++active->operands_done;
    switch (kind)
    {
    case MACHINE_LANGUAGE_OPERAND(TERM_LIST): return machine_language_branch(load, active);
    case MACHINE_LANGUAGE_OPERAND(NAME_STRING): return machine_language_declare_name(load, active);
    case MACHINE_LANGUAGE_OPERAND(BYTE_DATA): return machine_language_fixed_operand(load, active, 1u);
    case MACHINE_LANGUAGE_OPERAND(WORD_DATA): return machine_language_fixed_operand(load, active, 2u);
    case MACHINE_LANGUAGE_OPERAND(DOUBLE_WORD_DATA): return machine_language_fixed_operand(load, active, 4u);
    case MACHINE_LANGUAGE_OPERAND(QUAD_WORD_DATA): return machine_language_fixed_operand(load, active, 8u);
    case MACHINE_LANGUAGE_OPERAND(BYTE_LIST): return MACHINE_LANGUAGE_OK;
    default: break;
    }
    return machine_language_term(
        load, (active->opcode == MACHINE_LANGUAGE_OPCODE(NAME)) ? MACHINE_LANGUAGE_DATA_OBJECT : kind);
}

/**
 * @brief Computes a binary integer operator on two operands masked to the width: a shift by the
 *        width or more gives 0, a modulo by zero fails (§5.2).
 */
static MachineLanguageStatus_t machine_language_binary(const MachineLanguageLoad_t *load, uint32_t opcode,
                                                       uint64_t left, uint64_t right, uint64_t *out_result)
{
    const uint64_t width = machine_language_bits(load);

    left = machine_language_mask(load, left);
    right = machine_language_mask(load, right);
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(ADD): *out_result = left + right; break;
    case MACHINE_LANGUAGE_OPCODE(SUBTRACT): *out_result = left - right; break;
    case MACHINE_LANGUAGE_OPCODE(MULTIPLY): *out_result = left * right; break;
    case MACHINE_LANGUAGE_OPCODE(AND): *out_result = left & right; break;
    case MACHINE_LANGUAGE_OPCODE(OR): *out_result = left | right; break;
    case MACHINE_LANGUAGE_OPCODE(XOR): *out_result = left ^ right; break;
    case MACHINE_LANGUAGE_OPCODE(SHIFT_LEFT): *out_result = (right >= width) ? 0u : left << right; break;
    case MACHINE_LANGUAGE_OPCODE(SHIFT_RIGHT): *out_result = (right >= width) ? 0u : left >> right; break;
    default:
        if (right == 0u)
            return MACHINE_LANGUAGE_STATUS(DIVIDE_BY_ZERO);
        *out_result = left % right;
        break;
    }
    *out_result = machine_language_mask(load, *out_result);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Pops the top @p count values as integers, the deepest first, and releases them.
 */
static MachineLanguageStatus_t machine_language_pop_integers(MachineLanguageLoad_t *load, uint32_t count,
                                                             uint64_t *out_integers)
{
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    for (uint32_t index = count; index > 0u; --index)
    {
        MachineLanguageValue_t value = machine_language_pop(&load->evaluation);

        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_integer_of(load, &value, &out_integers[index - 1u]);
        machine_language_release(load, &value);
    }
    return status;
}

/**
 * @brief Runs Divide: the remainder and the quotient go to their targets, the quotient is the result;
 *        a division by zero fails (§5.2).
 */
static MachineLanguageStatus_t machine_language_divide(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t quotient_target = machine_language_pop(evaluation);
    MachineLanguageValue_t remainder_target = machine_language_pop(evaluation);
    uint64_t operands[2];
    MachineLanguageStatus_t status = machine_language_pop_integers(load, 2u, operands);
    const uint64_t divisor = machine_language_mask(load, operands[1]);

    if (status == MACHINE_LANGUAGE_OK && divisor == 0u)
        status = MACHINE_LANGUAGE_STATUS(DIVIDE_BY_ZERO);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &remainder_target);
        machine_language_release(load, &quotient_target);
        return status;
    }

    const uint64_t dividend = machine_language_mask(load, operands[0]);
    MachineLanguageValue_t remainder = machine_language_integer(dividend % divisor);

    status = machine_language_store(load, &remainder, &remainder_target);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &quotient_target);
        return status;
    }
    return machine_language_store_and_deliver(load, machine_language_integer(dividend / divisor), &quotient_target);
}

/**
 * @brief Runs Increment or Decrement on the object its SuperName names, in place.
 */
static MachineLanguageStatus_t machine_language_step_target(MachineLanguageLoad_t *load, bool up)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t current;
    uint64_t integer = 0u;
    MachineLanguageStatus_t status =
        (target.kind == MACHINE_LANGUAGE_TARGET || machine_language_is_reference(target.kind)) ?
            machine_language_read_target(load, &target, &current) :
            MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    if (status == MACHINE_LANGUAGE_OK)
    {
        status = machine_language_integer_of(load, &current, &integer);
        machine_language_release(load, &current);
    }
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &target);
        return status;
    }
    return machine_language_store_and_deliver(
        load, machine_language_integer(machine_language_mask(load, up ? integer + 1u : integer - 1u)), &target);
}

/**
 * @brief Compares two values (§5.2): a String or a Buffer by its bytes, then its length, an Integer by
 *        its value; the second converts to the first's type, which is counted when it is another.
 *
 * @return OK, and in @p out_order below, at or above zero as the first is less, equal or greater.
 */
static MachineLanguageStatus_t machine_language_order(MachineLanguageLoad_t *load, const MachineLanguageValue_t *first,
                                                      const MachineLanguageValue_t *second, int32_t *out_order)
{
    MachineLanguageValue_t converted;
    uint64_t left = 0u;
    uint64_t right = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    load->evaluation.mixed_type_comparisons += (first->kind != second->kind) ? 1u : 0u;
    if (first->kind == MACHINE_LANGUAGE_VALUE(STRING) || first->kind == MACHINE_LANGUAGE_VALUE(BUFFER))
    {
        status = (first->kind == MACHINE_LANGUAGE_VALUE(STRING)) ?
                     advanced_configuration_and_power_interface_machine_language_value_to_string(
                         &load->heap, &load->space, second, MACHINE_LANGUAGE_FORM(IMPLICIT),
                         machine_language_bits(load), &converted) :
                     advanced_configuration_and_power_interface_machine_language_value_to_buffer(
                         &load->heap, &load->space, second, machine_language_bits(load), &converted);
        if (status != MACHINE_LANGUAGE_OK)
            return status;

        const MachineLanguageHeader_t *left_object = machine_language_object(load, first);
        const MachineLanguageHeader_t *right_object = machine_language_object(load, &converted);
        const uint32_t common =
            (left_object->length < right_object->length) ? left_object->length : right_object->length;
        const int compared = memcmp(left_object + 1, right_object + 1, common);

        *out_order = (compared != 0)                              ? compared :
                     (left_object->length < right_object->length) ? -1 :
                     (left_object->length > right_object->length) ? 1 :
                                                                    0;
        machine_language_release(load, &converted);
        return MACHINE_LANGUAGE_OK;
    }
    status = machine_language_integer_of(load, first, &left);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_integer_of(load, second, &right);
    *out_order = (left < right) ? -1 : (left > right) ? 1 : 0;
    return status;
}

/**
 * @brief Runs LEqual, LGreater or LLess on two values of any computational type.
 */
static MachineLanguageStatus_t machine_language_compare(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t second = machine_language_pop(evaluation);
    MachineLanguageValue_t first = machine_language_pop(evaluation);
    int32_t order = 0;
    const MachineLanguageStatus_t status = machine_language_order(load, &first, &second, &order);

    machine_language_release(load, &first);
    machine_language_release(load, &second);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return machine_language_deliver(
        load, machine_language_truth(load, (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL)) ? order == 0 :
                                           (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS))  ? order < 0 :
                                                                                                order > 0));
}

/**
 * @brief The Integer an operand of LAnd or LOr gives: a Buffer's first 4 bytes, as Windows reads them
 *        (§5.2), or the Integer it converts to.
 */
static MachineLanguageStatus_t machine_language_logical_operand(MachineLanguageLoad_t *load,
                                                                const MachineLanguageValue_t *value,
                                                                uint64_t *out_integer)
{
    if (value->kind != MACHINE_LANGUAGE_VALUE(BUFFER))
        return machine_language_integer_of(load, value, out_integer);

    const MachineLanguageHeader_t *buffer = machine_language_object(load, value);

    *out_integer = 0u;
    for (uint32_t byte = 0u; byte < buffer->length && byte < MACHINE_LANGUAGE_LOGICAL_BYTES; ++byte)
        *out_integer |= (uint64_t) ((const uint8_t *) (buffer + 1))[byte] << (8u * byte);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs LAnd or LOr.
 */
static MachineLanguageStatus_t machine_language_logical(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t second = machine_language_pop(evaluation);
    MachineLanguageValue_t first = machine_language_pop(evaluation);
    uint64_t left = 0u;
    uint64_t right = 0u;
    MachineLanguageStatus_t status = machine_language_logical_operand(load, &first, &left);

    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_logical_operand(load, &second, &right);
    machine_language_release(load, &first);
    machine_language_release(load, &second);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return machine_language_deliver(load,
                                    machine_language_truth(load, (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_AND)) ?
                                                                     (left != 0u && right != 0u) :
                                                                     (left != 0u || right != 0u)));
}

/**
 * @brief Runs an operator that computes an integer from integers, and stores it to its target when it
 *        has one.
 */
static MachineLanguageStatus_t machine_language_compute(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t operand_count =
        advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->operand_count;
    const bool stores = advanced_configuration_and_power_interface_machine_language_operand_kind(
                            advanced_configuration_and_power_interface_machine_language_descriptor(opcode),
                            operand_count - 1u) == MACHINE_LANGUAGE_OPERAND(TARGET);
    MachineLanguageValue_t target =
        stores ? machine_language_pop(evaluation) : machine_language_target(MACHINE_LANGUAGE_TARGET_NONE, 0u);
    uint64_t operands[2] = {0u, 0u};
    uint64_t integer = 0u;
    MachineLanguageValue_t result;
    MachineLanguageStatus_t status = machine_language_pop_integers(load, operand_count - (stores ? 1u : 0u), operands);

    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &target);
        return status;
    }
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(NOT):
        result = machine_language_integer(machine_language_mask(load, ~operands[0]));
        break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT): result = machine_language_truth(load, operands[0] == 0u); break;
    default:
        status = machine_language_binary(load, opcode, operands[0], operands[1], &integer);
        result = machine_language_integer(integer);
        break;
    }
    return machine_language_finish(load, status, result, &target);
}

/**
 * @brief Runs Buffer once its size is on the stack: as many bytes as the size or the initialiser
 *        that follows, the larger, the rest zero.
 */
static MachineLanguageStatus_t machine_language_buffer(MachineLanguageLoad_t *load,
                                                       const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    MachineLanguageValue_t size_value = machine_language_pop(&load->evaluation);
    MachineLanguageValue_t buffer;
    uint64_t size = 0u;
    MachineLanguageStatus_t status = machine_language_integer_of(load, &size_value, &size);

    machine_language_release(load, &size_value);
    if (status != MACHINE_LANGUAGE_OK)
        return status;

    const uint32_t initialiser = active->end - frame->pc;
    const uint64_t length = (size > initialiser) ? size : initialiser;

    if (length > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_BUFFER_BYTES);
    status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        &load->heap, &load->space, MACHINE_LANGUAGE_VALUE(BUFFER), (uint32_t) length, &buffer);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    memcpy(advanced_configuration_and_power_interface_machine_language_object_payload(&load->heap, buffer.handle),
           frame->code + frame->pc, initialiser);
    frame->pc = active->end;
    return machine_language_deliver(load, buffer);
}

/**
 * @brief Runs Package or VarPackage once its elements are on the stack above its count: fewer
 *        initialisers than elements are padded with Uninitialized, more are dropped, both counted.
 */
static MachineLanguageStatus_t machine_language_package(MachineLanguageLoad_t *load,
                                                        const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t first = active->node - 1u;
    const uint32_t initialisers = evaluation->value_count - first;
    const uint32_t cap = (active->opcode == MACHINE_LANGUAGE_OPCODE(PACKAGE)) ?
                             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_PACKAGE_ELEMENTS :
                             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VARIABLE_PACKAGE_ELEMENTS;
    MachineLanguageValue_t package;
    uint64_t count = 0u;
    MachineLanguageStatus_t status = machine_language_integer_of(load, &evaluation->values[first - 1u], &count);

    if (status == MACHINE_LANGUAGE_OK && count > cap)
        status = MACHINE_LANGUAGE_STATUS(LIMIT_PACKAGE_ELEMENTS);
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_allocate(
            &load->heap, &load->space, MACHINE_LANGUAGE_VALUE(PACKAGE), (uint32_t) count, &package);
    if (status != MACHINE_LANGUAGE_OK)
        return status;

    MachineLanguageValue_t *elements =
        (MachineLanguageValue_t *) advanced_configuration_and_power_interface_machine_language_object_payload(
            &load->heap, package.handle);

    for (uint32_t index = 0u; index < initialisers; ++index)
    {
        if (index < count)
            elements[index] = evaluation->values[first + index];
        else
            machine_language_release(load, &evaluation->values[first + index]);
    }
    load->heap.packages_padded += (initialisers < count) ? 1u : 0u;
    load->heap.packages_truncated += (initialisers > count) ? 1u : 0u;
    evaluation->value_count = first - 1u;
    machine_language_release(load, &evaluation->values[first - 1u]);
    return machine_language_deliver(load, package);
}

/**
 * @brief Gives the node a Name in a method declared the data object on the stack, and its type.
 */
static MachineLanguageStatus_t machine_language_name(MachineLanguageLoad_t *load,
                                                     const MachineLanguagePendingOperator_t *active)
{
    const MachineLanguageLocation_t location = machine_language_node_location(load, active->node);
    MachineLanguageValue_t value = machine_language_pop(&load->evaluation);

    return machine_language_place(load, &location, &value);
}

/**
 * @brief Joins two runs of bytes into a new String or Buffer.
 */
static MachineLanguageStatus_t machine_language_join(MachineLanguageLoad_t *load, uint8_t kind,
                                                     const uint8_t *first_bytes, uint32_t first_length,
                                                     const uint8_t *second_bytes, uint32_t second_length,
                                                     MachineLanguageValue_t *out_value)
{
    const uint64_t length = (uint64_t) first_length + second_length;

    if (length > ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_BUFFER_BYTES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_BUFFER_BYTES);

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_value_allocate(
        &load->heap, &load->space, kind, (uint32_t) length, out_value);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

    uint8_t *bytes =
        advanced_configuration_and_power_interface_machine_language_object_payload(&load->heap, out_value->handle);

    memcpy(bytes, first_bytes, first_length);
    if (second_length != 0u)
        memcpy(bytes + first_length, second_bytes, second_length);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Converts Concatenate's second operand to what its first one dictates (§5.2): an Integer to
 *        an Integer, a String to a String, a Buffer to a Buffer.
 */
static MachineLanguageStatus_t machine_language_concatenation_operand(MachineLanguageLoad_t *load, uint8_t kind,
                                                                      const MachineLanguageValue_t *value,
                                                                      MachineLanguageValue_t *out_value)
{
    uint64_t integer = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (kind == MACHINE_LANGUAGE_VALUE(STRING))
        return advanced_configuration_and_power_interface_machine_language_value_to_string(
            &load->heap, &load->space, value, MACHINE_LANGUAGE_FORM(IMPLICIT), machine_language_bits(load), out_value);
    if (kind == MACHINE_LANGUAGE_VALUE(BUFFER))
        return advanced_configuration_and_power_interface_machine_language_value_to_buffer(
            &load->heap, &load->space, value, machine_language_bits(load), out_value);
    status = machine_language_integer_of(load, value, &integer);
    *out_value = machine_language_integer(integer);
    return status;
}

/**
 * @brief Runs Concatenate: the result takes the type of the first operand, a Buffer for an Integer.
 */
static MachineLanguageStatus_t machine_language_concatenate(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t second = machine_language_pop(evaluation);
    MachineLanguageValue_t first = machine_language_pop(evaluation);
    MachineLanguageValue_t converted = {0};
    MachineLanguageValue_t result = {0};
    uint8_t first_scratch[8];
    uint8_t second_scratch[8];
    const uint8_t *first_bytes = NULL;
    const uint8_t *second_bytes = NULL;
    uint32_t first_length = 0u;
    uint32_t second_length = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    if (first.kind == MACHINE_LANGUAGE_VALUE(INTEGER) || first.kind == MACHINE_LANGUAGE_VALUE(STRING) ||
        first.kind == MACHINE_LANGUAGE_VALUE(BUFFER))
    {
        evaluation->mixed_type_comparisons += (first.kind != second.kind) ? 1u : 0u;
        status = machine_language_concatenation_operand(load, first.kind, &second, &converted);
    }
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_bytes(
            &load->heap, &first, machine_language_bits(load), first_scratch, &first_bytes, &first_length);
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_bytes(
            &load->heap, &converted, machine_language_bits(load), second_scratch, &second_bytes, &second_length);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_join(load,
                                       (first.kind == MACHINE_LANGUAGE_VALUE(STRING)) ? MACHINE_LANGUAGE_VALUE(STRING) :
                                                                                        MACHINE_LANGUAGE_VALUE(BUFFER),
                                       first_bytes, first_length, second_bytes, second_length, &result);
    machine_language_release(load, &first);
    machine_language_release(load, &second);
    machine_language_release(load, &converted);
    return machine_language_finish(load, status, result, &target);
}

/**
 * @brief Runs ToInteger, ToBuffer, ToHexString or ToDecimalString, and stores the result to its
 *        target.
 */
static MachineLanguageStatus_t machine_language_convert(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t source = machine_language_pop(evaluation);
    MachineLanguageValue_t result = {0};
    uint64_t integer = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(TO_INTEGER):
        status = advanced_configuration_and_power_interface_machine_language_value_to_integer_explicitly(
            &load->heap, &source, machine_language_bits(load), &integer);
        result = machine_language_integer(integer);
        break;
    case MACHINE_LANGUAGE_OPCODE(TO_BUFFER):
        status = advanced_configuration_and_power_interface_machine_language_value_to_buffer(
            &load->heap, &load->space, &source, machine_language_bits(load), &result);
        break;
    default:
        status = advanced_configuration_and_power_interface_machine_language_value_to_string(
            &load->heap, &load->space, &source,
            (opcode == MACHINE_LANGUAGE_OPCODE(TO_HEX_STRING)) ? MACHINE_LANGUAGE_FORM(HEXADECIMAL) :
                                                                 MACHINE_LANGUAGE_FORM(DECIMAL),
            machine_language_bits(load), &result);
        break;
    }
    machine_language_release(load, &source);
    return machine_language_finish(load, status, result, &target);
}

/**
 * @brief Runs ToString: the characters of a Buffer up to its first NUL or the length asked, Ones for
 *        no limit.
 */
static MachineLanguageStatus_t machine_language_to_string(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t limit = machine_language_pop(evaluation);
    MachineLanguageValue_t source = machine_language_pop(evaluation);
    MachineLanguageValue_t result = {0};
    uint8_t scratch[8];
    const uint8_t *bytes = NULL;
    uint32_t length = 0u;
    uint64_t wanted = 0u;
    MachineLanguageStatus_t status = machine_language_integer_of(load, &limit, &wanted);

    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_value_bytes(
            &load->heap, &source, machine_language_bits(load), scratch, &bytes, &length);
    if (status == MACHINE_LANGUAGE_OK)
    {
        uint32_t characters = 0u;

        while (characters < length && characters < wanted && bytes[characters] != 0x00u)
            ++characters;
        status = machine_language_join(load, MACHINE_LANGUAGE_VALUE(STRING), bytes, characters, NULL, 0u, &result);
    }
    machine_language_release(load, &source);
    machine_language_release(load, &limit);
    return machine_language_finish(load, status, result, &target);
}

/**
 * @brief Runs Mid: the slice of a String or a Buffer that starts at the index and runs the length
 *        asked, cut at its end; past its end, an empty one. An Integer is read as a Buffer (§5.2).
 */
static MachineLanguageStatus_t machine_language_mid(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    uint64_t slice[2] = {0u, 0u};
    MachineLanguageStatus_t status = machine_language_pop_integers(load, 2u, slice);
    MachineLanguageValue_t source = machine_language_pop(evaluation);
    MachineLanguageValue_t result = {0};

    if (status == MACHINE_LANGUAGE_OK && source.kind == MACHINE_LANGUAGE_VALUE(INTEGER))
    {
        const MachineLanguageValue_t integer = source;

        status = advanced_configuration_and_power_interface_machine_language_value_to_buffer(
            &load->heap, &load->space, &integer, machine_language_bits(load), &source);
    }
    if (status == MACHINE_LANGUAGE_OK && source.kind != MACHINE_LANGUAGE_VALUE(STRING) &&
        source.kind != MACHINE_LANGUAGE_VALUE(BUFFER))
        status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (status == MACHINE_LANGUAGE_OK)
    {
        const MachineLanguageHeader_t *object = machine_language_object(load, &source);
        const uint64_t start = (slice[0] < object->length) ? slice[0] : object->length;
        const uint64_t left = object->length - start;
        const uint64_t length = (slice[1] < left) ? slice[1] : left;

        status = machine_language_join(load, source.kind, (const uint8_t *) (object + 1) + start, (uint32_t) length,
                                       NULL, 0u, &result);
    }
    machine_language_release(load, &source);
    return machine_language_finish(load, status, result, &target);
}

/**
 * @brief Runs SizeOf: a String's characters, a Buffer's bytes, a Package's elements.
 */
static MachineLanguageStatus_t machine_language_size_of(MachineLanguageLoad_t *load)
{
    MachineLanguageValue_t operand = machine_language_pop(&load->evaluation);
    MachineLanguageValue_t value;
    MachineLanguageStatus_t status = machine_language_read_target(load, &operand, &value);
    uint64_t size = 0u;

    machine_language_release(load, &operand);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (value.kind == MACHINE_LANGUAGE_VALUE(STRING) || value.kind == MACHINE_LANGUAGE_VALUE(BUFFER) ||
        value.kind == MACHINE_LANGUAGE_VALUE(PACKAGE))
        size = machine_language_object(load, &value)->length;
    else
        status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    machine_language_release(load, &value);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_integer(size)) : status;
}

/**
 * @brief The ObjectType number of a value's kind: its own for the data objects, 0 for anything else.
 */
static uint64_t machine_language_value_type(uint8_t kind)
{
    return (kind <= MACHINE_LANGUAGE_VALUE(PACKAGE)) ? kind : 0u;
}

/**
 * @brief The ObjectType number of what lies at a place: a named object's type, a byte's buffer field,
 *        a local's, an argument's or an element's data type.
 */
static uint64_t machine_language_location_type(const MachineLanguageLoad_t *load,
                                               const MachineLanguageLocation_t *location)
{
    uint8_t node_type = 0u;

    if (location->kind == MACHINE_LANGUAGE_LOCATION_BYTE)
        return MACHINE_LANGUAGE_NODE(BUFFER_FIELD);
    if (location->kind != MACHINE_LANGUAGE_LOCATION_NODE)
        return machine_language_value_type(location->slot->kind);
    node_type =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, location->node)->type;
    return (node_type <= MACHINE_LANGUAGE_NODE(BUFFER_FIELD)) ? node_type : 0u;
}

/**
 * @brief Runs ObjectType: the ACPI number of what a SimpleName names (ACPI 6.6 §19.6.97), through the
 *        references a local, an argument or a name holds; 0 for an uninitialized local and for the
 *        scopes and Externals that are no object.
 */
static MachineLanguageStatus_t machine_language_object_type(MachineLanguageLoad_t *load)
{
    MachineLanguageValue_t operand = machine_language_pop(&load->evaluation);
    MachineLanguageLocation_t location;
    const MachineLanguageValue_t *reference = machine_language_is_reference(operand.kind) ? &operand : NULL;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    uint64_t type = machine_language_value_type(operand.kind);

    if (operand.kind == MACHINE_LANGUAGE_TARGET && operand.auxiliary == MACHINE_LANGUAGE_TARGET_DEBUG)
        type = MACHINE_LANGUAGE_DEBUG_OBJECT_TYPE;
    else if (operand.kind == MACHINE_LANGUAGE_TARGET && (operand.auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL ||
                                                         operand.auxiliary == MACHINE_LANGUAGE_TARGET_ARGUMENT))
        location = machine_language_frame_location(&load->evaluation, &operand);
    else if (operand.kind == MACHINE_LANGUAGE_TARGET && operand.auxiliary == MACHINE_LANGUAGE_TARGET_NODE)
        location = machine_language_node_location(load, operand.handle);
    else if (operand.kind == MACHINE_LANGUAGE_TARGET)
        type = 0u;
    if (operand.kind == MACHINE_LANGUAGE_TARGET &&
        (operand.auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL || operand.auxiliary == MACHINE_LANGUAGE_TARGET_ARGUMENT ||
         operand.auxiliary == MACHINE_LANGUAGE_TARGET_NODE))
    {
        reference = machine_language_is_reference(location.slot->kind) ? location.slot : NULL;
        type = machine_language_location_type(load, &location);
    }
    if (reference != NULL)
    {
        status = machine_language_locate(load, reference, true, &location);
        type = (status == MACHINE_LANGUAGE_OK) ? machine_language_location_type(load, &location) : 0u;
    }
    machine_language_release(load, &operand);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_integer(type)) : status;
}

/**
 * @brief Writes the NameString a path String spells, such as `\_SB_.PCI0` or `^FOO`, into @p out: a
 *        root or parent prefixes, then its NameSegs, each padded with `_` to 4 characters.
 *
 * @return TRUNCATED for a path with no segment, an empty segment, one longer than 4 characters, or
 *         more than MACHINE_LANGUAGE_PATH_SEGMENTS of them; OK otherwise.
 */
static MachineLanguageStatus_t machine_language_spell_path(const uint8_t *text, uint32_t length, uint8_t *out,
                                                           uint32_t *out_length)
{
    uint32_t read = 0u;
    uint32_t written = 0u;
    uint32_t segments = 0u;
    uint8_t names[MACHINE_LANGUAGE_PATH_SEGMENTS * 4u];

    if (length != 0u && text[0] == '\\')
        out[written++] = text[read++];
    while (read < length && text[read] == '^' && written < 16u)
        out[written++] = text[read++];
    while (read < length && segments < MACHINE_LANGUAGE_PATH_SEGMENTS)
    {
        uint32_t characters = 0u;

        while (read < length && text[read] != '.' && characters < 4u)
            names[segments * 4u + characters++] = text[read++];
        if (characters == 0u || (read < length && text[read] != '.'))
            return MACHINE_LANGUAGE_STATUS(TRUNCATED);
        while (characters < 4u)
            names[segments * 4u + characters++] = '_';
        ++segments;
        read += (read < length) ? 1u : 0u;
    }
    if (segments == 0u || read < length || text[length - 1u] == '.')
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    if (segments == 2u)
        out[written++] = 0x2Eu;
    else if (segments > 2u)
    {
        out[written++] = 0x2Fu;
        out[written++] = (uint8_t) segments;
    }
    memcpy(out + written, names, segments * 4u);
    *out_length = written + segments * 4u;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The node a path String names from @p scope, as a NameString in AML would.
 */
static MachineLanguageStatus_t machine_language_resolve_path(MachineLanguageLoad_t *load, const uint8_t *text,
                                                             uint32_t length, uint32_t scope, uint32_t *out_node)
{
    uint8_t spelled[2u + 16u + MACHINE_LANGUAGE_PATH_SEGMENTS * 4u];
    uint32_t spelled_length = 0u;
    uint32_t after = 0u;
    MachineLanguageNameString_t name;
    MachineLanguageStatus_t status = machine_language_spell_path(text, length, spelled, &spelled_length);

    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_read_name_string(
            spelled, 0u, spelled_length, &name, &after);
    if (status == MACHINE_LANGUAGE_OK && after != spelled_length)
        status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return advanced_configuration_and_power_interface_machine_language_namespace_resolve(&load->space, scope, spelled,
                                                                                         &name, out_node);
}

/**
 * @brief The operand kind the value about to be delivered fills: the active operator's current
 *        operand, NONE when no operator waits for it.
 */
static uint32_t machine_language_receiving_kind(MachineLanguageEvaluation_t *evaluation)
{
    const MachineLanguagePendingOperator_t *active = machine_language_active(evaluation);

    if (active == NULL)
        return MACHINE_LANGUAGE_OPERAND(NONE);
    if (active->opcode >= MACHINE_LANGUAGE_CALL)
        return MACHINE_LANGUAGE_OPERAND(TERM_ARGUMENT);
    return advanced_configuration_and_power_interface_machine_language_operand_kind(
        advanced_configuration_and_power_interface_machine_language_descriptor(active->opcode),
        active->operands_done - 1u);
}

/**
 * @brief Whether the value about to be delivered stands where an object is named rather than read: a
 *        SuperName, a Target or a SimpleName.
 */
static bool machine_language_names_target(MachineLanguageEvaluation_t *evaluation)
{
    const uint32_t kind = machine_language_receiving_kind(evaluation);

    return kind == MACHINE_LANGUAGE_OPERAND(SUPER_NAME) || kind == MACHINE_LANGUAGE_OPERAND(TARGET) ||
           kind == MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME);
}

/**
 * @brief The reference RefOf makes of an operand, whose reference it takes: a local, an argument or a
 *        named object; a reference gives itself.
 */
static MachineLanguageStatus_t machine_language_reference_of(MachineLanguageLoad_t *load,
                                                             MachineLanguageValue_t *operand,
                                                             MachineLanguageValue_t *out_reference)
{
    memset(out_reference, 0, sizeof(*out_reference));
    if (machine_language_is_reference(operand->kind))
    {
        *out_reference = *operand;
        operand->kind = MACHINE_LANGUAGE_VALUE(UNINITIALIZED);
        return MACHINE_LANGUAGE_OK;
    }
    if (operand->kind == MACHINE_LANGUAGE_TARGET &&
        (operand->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL || operand->auxiliary == MACHINE_LANGUAGE_TARGET_ARGUMENT))
    {
        *out_reference = machine_language_slot_reference(&load->evaluation, operand);
        return MACHINE_LANGUAGE_OK;
    }
    if (operand->kind == MACHINE_LANGUAGE_TARGET && operand->auxiliary == MACHINE_LANGUAGE_TARGET_NODE)
    {
        out_reference->kind = MACHINE_LANGUAGE_VALUE(REFERENCE_NODE);
        out_reference->handle = operand->handle;
        out_reference->payload = machine_language_node_lifetime(load, operand->handle);
        return MACHINE_LANGUAGE_OK;
    }
    machine_language_release(load, operand);
    return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
}

/**
 * @brief Runs RefOf.
 */
static MachineLanguageStatus_t machine_language_run_reference_of(MachineLanguageLoad_t *load)
{
    MachineLanguageValue_t operand = machine_language_pop(&load->evaluation);
    MachineLanguageValue_t reference;
    const MachineLanguageStatus_t status = machine_language_reference_of(load, &operand, &reference);

    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, reference) : status;
}

/**
 * @brief Runs CondRefOf: False for a name that names no object, an External's included, the target
 *        left alone; True otherwise, the target given the reference.
 */
static MachineLanguageStatus_t machine_language_conditional_reference_of(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t operand = machine_language_pop(evaluation);
    MachineLanguageValue_t reference;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (operand.kind == MACHINE_LANGUAGE_TARGET && operand.auxiliary == MACHINE_LANGUAGE_TARGET_MISSING)
    {
        machine_language_release(load, &target);
        return machine_language_deliver(load, machine_language_truth(load, false));
    }
    status = machine_language_reference_of(load, &operand, &reference);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_store(load, &reference, &target);
    else
        machine_language_release(load, &target);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_truth(load, true)) :
                                             status;
}

/**
 * @brief Runs DerefOf: a reference gives what lies at the bottom of its chain, a path String the
 *        object it names (§4.2); where an object is named rather than read, the reference, or the
 *        named object, is the result.
 */
static MachineLanguageStatus_t machine_language_dereference_of(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t operand = machine_language_pop(evaluation);
    MachineLanguageValue_t value;
    uint32_t node = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    if (machine_language_is_reference(operand.kind))
    {
        if (machine_language_names_target(evaluation))
            return machine_language_deliver(load, operand);
        status = machine_language_dereference(load, &operand, &value);
    }
    else if (operand.kind == MACHINE_LANGUAGE_VALUE(STRING))
    {
        const MachineLanguageHeader_t *path = machine_language_object(load, &operand);

        status = machine_language_resolve_path(load, (const uint8_t *) (path + 1), path->length,
                                               machine_language_block(evaluation)->scope, &node);
        if (status == MACHINE_LANGUAGE_OK && machine_language_names_target(evaluation))
            value = machine_language_target(MACHINE_LANGUAGE_TARGET_NODE, node);
        else if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_read_node(load, node, &value);
    }
    machine_language_release(load, &operand);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, value) : status;
}

/**
 * @brief Gives DerefOf (Index (...)) its result in DerefOf's place (§7.2, O8): what the reference
 *        refers to, read at once and the reference, whose count it takes, released; where DerefOf
 *        stands for a target, the reference itself, counted.
 */
static MachineLanguageStatus_t machine_language_read_at_once(MachineLanguageLoad_t *load,
                                                             MachineLanguageValue_t *reference)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t value;

    --evaluation->operator_count;
    if (machine_language_names_target(evaluation))
    {
        ++evaluation->references_built;
        return machine_language_deliver(load, *reference);
    }

    const MachineLanguageStatus_t status = machine_language_dereference(load, reference, &value);

    machine_language_release(load, reference);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, value) : status;
}

/**
 * @brief Runs Index: a reference to an element of a Package, or to a byte of a Buffer or a String,
 *        never a copy (§5.2), counted; DerefOf (Index (...)) builds none (§7.2, O8).
 */
static MachineLanguageStatus_t machine_language_index(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t position = machine_language_pop(evaluation);
    MachineLanguageValue_t operand = machine_language_pop(evaluation);
    MachineLanguageValue_t source;
    MachineLanguageValue_t reference = {0};
    uint64_t index = 0u;
    MachineLanguageStatus_t status = machine_language_integer_of(load, &position, &index);
    const MachineLanguagePendingOperator_t *receiver = machine_language_active(evaluation);

    machine_language_release(load, &position);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_dereference(load, &operand, &source);
    machine_language_release(load, &operand);
    if (status == MACHINE_LANGUAGE_OK)
    {
        if (source.kind != MACHINE_LANGUAGE_VALUE(PACKAGE) && source.kind != MACHINE_LANGUAGE_VALUE(BUFFER) &&
            source.kind != MACHINE_LANGUAGE_VALUE(STRING))
            status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
        else if (index >= machine_language_object(load, &source)->length)
            status = MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
        if (status != MACHINE_LANGUAGE_OK)
            machine_language_release(load, &source);
    }
    if (status != MACHINE_LANGUAGE_OK)
        return machine_language_finish(load, status, reference, &target);
    reference.kind = (source.kind == MACHINE_LANGUAGE_VALUE(PACKAGE)) ? MACHINE_LANGUAGE_VALUE(REFERENCE_ELEMENT) :
                                                                        MACHINE_LANGUAGE_VALUE(REFERENCE_BYTE);
    reference.handle = source.handle;
    reference.payload = index;
    if (receiver != NULL && receiver->opcode == MACHINE_LANGUAGE_OPCODE(DEREFERENCE_OF) &&
        target.kind == MACHINE_LANGUAGE_TARGET && target.auxiliary == MACHINE_LANGUAGE_TARGET_NONE)
        return machine_language_read_at_once(load, &reference);
    ++evaluation->references_built;
    return machine_language_store_and_deliver(load, reference, &target);
}

/**
 * @brief Whether an element passes one test of Match: always for MTR, else as it compares with the
 *        match object it is converted to the type of; false when it cannot be converted.
 */
static bool machine_language_matches(MachineLanguageLoad_t *load, uint64_t test, const MachineLanguageValue_t *element,
                                     const MachineLanguageValue_t *object)
{
    int32_t order = 0;

    if (test == 0u)
        return true;
    if (machine_language_order(load, object, element, &order) != MACHINE_LANGUAGE_OK)
        return false;
    switch (test)
    {
    case 1u: return order == 0;
    case 2u: return order >= 0;
    case 3u: return order > 0;
    case 4u: return order <= 0;
    default: return order < 0;
    }
}

/**
 * @brief Runs Match: the index of the first element from the start that passes both tests, each
 *        element read and converted to its match object's type, those that cannot be skipped; Ones
 *        when none passes.
 */
static MachineLanguageStatus_t machine_language_match(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t operands[6];
    MachineLanguageValue_t package = {0};
    uint64_t start = 0u;
    uint64_t found = machine_language_mask(load, ~0ull);
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    for (uint32_t index = 6u; index > 0u; --index)
        operands[index - 1u] = machine_language_pop(evaluation);
    status = machine_language_integer_of(load, &operands[5], &start);
    if (status == MACHINE_LANGUAGE_OK && (operands[1].payload > 5u || operands[3].payload > 5u))
        status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_dereference(load, &operands[0], &package);
    if (status == MACHINE_LANGUAGE_OK && package.kind != MACHINE_LANGUAGE_VALUE(PACKAGE))
        status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    for (uint64_t index = start;
         status == MACHINE_LANGUAGE_OK && index < machine_language_object(load, &package)->length; ++index)
    {
        MachineLanguageValue_t element;

        if (machine_language_dereference(
                load,
                (const MachineLanguageValue_t *)
                        advanced_configuration_and_power_interface_machine_language_object_payload(&load->heap,
                                                                                                   package.handle) +
                    index,
                &element) != MACHINE_LANGUAGE_OK)
            continue;

        const bool computational = element.kind == MACHINE_LANGUAGE_VALUE(INTEGER) ||
                                   element.kind == MACHINE_LANGUAGE_VALUE(STRING) ||
                                   element.kind == MACHINE_LANGUAGE_VALUE(BUFFER);
        const bool passes = computational &&
                            machine_language_matches(load, operands[1].payload, &element, &operands[2]) &&
                            machine_language_matches(load, operands[3].payload, &element, &operands[4]);

        machine_language_release(load, &element);
        if (passes)
        {
            found = index;
            break;
        }
    }
    machine_language_release(load, &package);
    for (uint32_t index = 0u; index < 6u; ++index)
        machine_language_release(load, &operands[index]);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_integer(found)) : status;
}

/**
 * @brief The bit offset, length and access width in bytes of the buffer field a Create*Field opcode
 *        makes from its index and, for CreateField, its length.
 */
static void machine_language_field_shape(uint32_t opcode, uint64_t index, uint64_t length, uint64_t *out_offset,
                                         uint64_t *out_length, uint16_t *out_width)
{
    static const uint8_t widths[] = {1u, 2u, 4u, 8u};
    const uint32_t shape = (opcode == MACHINE_LANGUAGE_OPCODE(CREATE_BYTE_FIELD))        ? 0u :
                           (opcode == MACHINE_LANGUAGE_OPCODE(CREATE_WORD_FIELD))        ? 1u :
                           (opcode == MACHINE_LANGUAGE_OPCODE(CREATE_DOUBLE_WORD_FIELD)) ? 2u :
                                                                                           3u;

    *out_width = 1u;
    if (opcode == MACHINE_LANGUAGE_OPCODE(CREATE_FIELD) || opcode == MACHINE_LANGUAGE_OPCODE(CREATE_BIT_FIELD))
    {
        *out_offset = index;
        *out_length = (opcode == MACHINE_LANGUAGE_OPCODE(CREATE_FIELD)) ? length : 1u;
        return;
    }
    *out_width = widths[shape];
    *out_offset = (index > 0xFFFFFFFFull) ? ~0ull : index * 8u;
    *out_length = 8u * widths[shape];
}

/**
 * @brief Runs CreateBitField, CreateByteField, CreateWordField, CreateDWordField, CreateQWordField or
 *        CreateField in a method: the node its NameString declared becomes a buffer field over the
 *        Buffer its source gives, which it holds a reference on; one that passes the Buffer's end, or
 *        32 bits, fails with index_out_of_range (§5.2, §6.3).
 */
static MachineLanguageStatus_t machine_language_create_field(MachineLanguageLoad_t *load,
                                                             const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const bool any_length = active->opcode == MACHINE_LANGUAGE_OPCODE(CREATE_FIELD);
    MachineLanguageValue_t length_value = any_length ? machine_language_pop(evaluation) : machine_language_integer(0u);
    MachineLanguageValue_t index_value = machine_language_pop(evaluation);
    MachineLanguageValue_t operand = machine_language_pop(evaluation);
    MachineLanguageValue_t buffer = {0};
    uint64_t length = 0u;
    uint64_t index = 0u;
    uint64_t bit_offset = 0u;
    uint64_t bit_length = 0u;
    uint16_t width = 1u;
    MachineLanguageStatus_t status = machine_language_integer_of(load, &length_value, &length);

    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_integer_of(load, &index_value, &index);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_dereference(load, &operand, &buffer);
    machine_language_release(load, &length_value);
    machine_language_release(load, &index_value);
    machine_language_release(load, &operand);
    if (status == MACHINE_LANGUAGE_OK && buffer.kind != MACHINE_LANGUAGE_VALUE(BUFFER))
        status = MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    machine_language_field_shape(active->opcode, index, length, &bit_offset, &bit_length, &width);
    if (status == MACHINE_LANGUAGE_OK &&
        (bit_offset > 0xFFFFFFFFull || bit_length > 0xFFFFFFFFull ||
         bit_offset + bit_length > (uint64_t) machine_language_object(load, &buffer)->length * 8u))
        status = MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &buffer);
        return status;
    }

    MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, active->node);

    node->value = buffer;
    node->value.kind = MACHINE_LANGUAGE_VALUE(BUFFER_FIELD);
    node->value.flags =
        any_length ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BUFFER_FIELD_READS_AS_BUFFER : 0u;
    node->value.auxiliary = width;
    node->value.payload = bit_offset | (bit_length << 32u);
    node->type = MACHINE_LANGUAGE_NODE(BUFFER_FIELD);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs Store or CopyObject: the target takes the value, and Store gives it as its result.
 */
static MachineLanguageStatus_t machine_language_transfer(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t value = machine_language_pop(evaluation);

    if (opcode == MACHINE_LANGUAGE_OPCODE(STORE))
        return machine_language_store_and_deliver(load, value, &target);
    return machine_language_copy_object(load, &value, &target);
}

/**
 * @brief Runs the active operator once its operands are on the stack.
 */
static MachineLanguageStatus_t machine_language_execute(MachineLanguageLoad_t *load,
                                                        const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguagePendingOperator_t operator_ = *active;

    --evaluation->operator_count;
    switch (operator_.opcode)
    {
    case MACHINE_LANGUAGE_CALL: return machine_language_enter_method(load, operator_.node, operator_.operand_count);
    case MACHINE_LANGUAGE_OPCODE(NAME): return machine_language_name(load, &operator_);
    case MACHINE_LANGUAGE_OPCODE(RETURN): {
        MachineLanguageValue_t value = machine_language_pop(evaluation);

        if (machine_language_frame(evaluation)->method == 0u)
        {
            machine_language_release(load, &value);
            return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
        }
        return machine_language_return(load, &value);
    }
    case MACHINE_LANGUAGE_OPCODE(STORE):
    case MACHINE_LANGUAGE_OPCODE(COPY_OBJECT): return machine_language_transfer(load, operator_.opcode);
    case MACHINE_LANGUAGE_OPCODE(BUFFER): return machine_language_buffer(load, &operator_);
    case MACHINE_LANGUAGE_OPCODE(PACKAGE):
    case MACHINE_LANGUAGE_OPCODE(VARIABLE_PACKAGE): return machine_language_package(load, &operator_);
    case MACHINE_LANGUAGE_OPCODE(INCREMENT): return machine_language_step_target(load, true);
    case MACHINE_LANGUAGE_OPCODE(DECREMENT): return machine_language_step_target(load, false);
    case MACHINE_LANGUAGE_OPCODE(DIVIDE): return machine_language_divide(load);
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_GREATER):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS): return machine_language_compare(load, operator_.opcode);
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_AND):
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_OR): return machine_language_logical(load, operator_.opcode);
    case MACHINE_LANGUAGE_OPCODE(CONCATENATE): return machine_language_concatenate(load);
    case MACHINE_LANGUAGE_OPCODE(TO_INTEGER):
    case MACHINE_LANGUAGE_OPCODE(TO_BUFFER):
    case MACHINE_LANGUAGE_OPCODE(TO_HEX_STRING):
    case MACHINE_LANGUAGE_OPCODE(TO_DECIMAL_STRING): return machine_language_convert(load, operator_.opcode);
    case MACHINE_LANGUAGE_OPCODE(TO_STRING): return machine_language_to_string(load);
    case MACHINE_LANGUAGE_OPCODE(MID): return machine_language_mid(load);
    case MACHINE_LANGUAGE_OPCODE(SIZE_OF): return machine_language_size_of(load);
    case MACHINE_LANGUAGE_OPCODE(OBJECT_TYPE): return machine_language_object_type(load);
    case MACHINE_LANGUAGE_OPCODE(REFERENCE_OF): return machine_language_run_reference_of(load);
    case MACHINE_LANGUAGE_OPCODE(CONDITIONAL_REFERENCE_OF): return machine_language_conditional_reference_of(load);
    case MACHINE_LANGUAGE_OPCODE(DEREFERENCE_OF): return machine_language_dereference_of(load);
    case MACHINE_LANGUAGE_OPCODE(INDEX): return machine_language_index(load);
    case MACHINE_LANGUAGE_OPCODE(MATCH): return machine_language_match(load);
    case MACHINE_LANGUAGE_OPCODE(CREATE_BIT_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_BYTE_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_DOUBLE_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_QUAD_WORD_FIELD):
    case MACHINE_LANGUAGE_OPCODE(CREATE_FIELD): return machine_language_create_field(load, &operator_);
    default: return machine_language_compute(load, operator_.opcode);
    }
}

/**
 * @brief Takes one step: an operand of the active operator, the operator itself once its operands
 *        are there, the end of a construct, or the next term.
 */
static MachineLanguageStatus_t machine_language_step(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguagePendingOperator_t *active = machine_language_active(evaluation);

    if (active != NULL)
        return (active->operands_done < active->operand_count) ? machine_language_operand(load, active) :
                                                                 machine_language_execute(load, active);
    if (machine_language_frame(evaluation)->pc >= machine_language_block(evaluation)->end)
        return machine_language_close_block(load);
    return machine_language_term(load, MACHINE_LANGUAGE_OPERAND(NONE));
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_run_table_term(
    MachineLanguageLoad_t *load, uint32_t table, uint32_t scope, uint32_t start, uint32_t limit, bool value,
    uint64_t *out_integer, uint32_t *out_after)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t base = evaluation->frame_count;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    if (base == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES ||
        evaluation->operator_count ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
    {
        status = (base == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES) ?
                     MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES) :
                     MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);
        advanced_configuration_and_power_interface_machine_language_count_failure(load, status);
        return status;
    }
    if (evaluation->table_locals_owner != table + 1u)
    {
        advanced_configuration_and_power_interface_machine_language_release_table_locals(load);
        evaluation->table_locals_serial = ++evaluation->frame_serial;
    }

    MachineLanguageFrame_t *frame = &evaluation->frames[evaluation->frame_count++];

    frame->code = (const uint8_t *) load->tables[table].header;
    frame->pc = start;
    frame->end = limit;
    frame->method = 0u;
    frame->scope = scope;
    memset(frame->arguments, 0, sizeof(frame->arguments));
    memcpy(frame->locals, evaluation->table_locals, sizeof(frame->locals));
    memset(evaluation->table_locals, 0, sizeof(evaluation->table_locals));
    frame->value_base = (uint16_t) evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = (uint16_t) (table + 1u);
    frame->argument_count = 0u;
    frame->reserved = 0u;
    frame->serial = evaluation->table_locals_serial;
    if (evaluation->frame_count > evaluation->frames_peak)
        evaluation->frames_peak = evaluation->frame_count;
    ++evaluation->runs;
    status = machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_TABLE, start, limit, scope);
    if (status == MACHINE_LANGUAGE_OK && value)
    {
        MachineLanguagePendingOperator_t *result = &evaluation->operators[evaluation->operator_count++];

        result->opcode = (uint16_t) MACHINE_LANGUAGE_RESULT;
        result->operands_done = 0u;
        result->operand_count = 1u;
        result->start = start;
        result->end = limit;
        result->node = 0u;
    }

    const uint32_t settled_operators = frame->operator_base + (value ? 1u : 0u);

    while (status == MACHINE_LANGUAGE_OK &&
           !(evaluation->frame_count == base + 1u && evaluation->block_count == frame->block_base + 1u &&
             evaluation->operator_count == settled_operators && frame->pc != start))
        status = machine_language_step(load);
    if (status == MACHINE_LANGUAGE_OK && value)
        status = (evaluation->value_count > frame->value_base) ?
                     machine_language_integer_of(load, &evaluation->values[evaluation->value_count - 1u], out_integer) :
                     MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    *out_after = frame->pc;
    while (evaluation->frame_count > base + 1u)
        machine_language_drop_frame(load);
    memcpy(evaluation->table_locals, frame->locals, sizeof(evaluation->table_locals));
    memset(frame->locals, 0, sizeof(frame->locals));
    evaluation->table_locals_owner = table + 1u;
    machine_language_drop_frame(load);
    if (status != MACHINE_LANGUAGE_OK)
        advanced_configuration_and_power_interface_machine_language_count_failure(load, status);
    return status;
}

void advanced_configuration_and_power_interface_machine_language_release_table_locals(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    for (uint32_t slot = 0u; slot < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LOCALS; ++slot)
        machine_language_release(load, &evaluation->table_locals[slot]);
    evaluation->table_locals_owner = 0u;
}

MachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_count_back_edge(MachineLanguageLoad_t *load)
{
    return machine_language_count_iteration(&load->evaluation);
}

void advanced_configuration_and_power_interface_machine_language_count_failure(MachineLanguageLoad_t *load,
                                                                               MachineLanguageStatus_t status)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (evaluation->failures++ == 0u)
        evaluation->first_failure = (uint8_t) status;
    ++evaluation->statuses[status];
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_evaluate(
    MachineLanguageLoad_t *load, const char *path, const MachineLanguageValue_t *arguments, uint32_t argument_count,
    MachineLanguageValue_t *out_result)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t base = evaluation->frame_count;
    uint32_t index = 0u;
    uint32_t length = 0u;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_STATUS(TRUNCATED);

    memset(out_result, 0, sizeof(*out_result));
    while (path[length] != '\0' && length < 0xFFFFu)
        ++length;
    if (length != 0u && path[0] == '\\')
        status =
            machine_language_resolve_path(load, (const uint8_t *) path, length,
                                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, &index);
    if (status != MACHINE_LANGUAGE_OK)
        return status;

    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node->type != MACHINE_LANGUAGE_NODE(METHOD))
        return machine_language_read_node(load, index, out_result);
    if (argument_count != (node->value.auxiliary & 0x7u))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (base == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);
    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);
    if (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES - evaluation->value_count <
        argument_count + 1u)
        return MACHINE_LANGUAGE_STATUS(LIMIT_VALUES);

    MachineLanguageFrame_t *frame = &evaluation->frames[evaluation->frame_count++];

    memset(frame, 0, sizeof(*frame));
    frame->code = (const uint8_t *) load->tables[node->value.handle - 1u].header;
    frame->scope = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
    frame->value_base = (uint16_t) evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = (uint16_t) node->value.handle;
    frame->serial = ++evaluation->frame_serial;
    evaluation->iterations = 0u;
    status = machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_TABLE, 0u, 0u,
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT);
    if (status == MACHINE_LANGUAGE_OK)
    {
        MachineLanguagePendingOperator_t *result = &evaluation->operators[evaluation->operator_count++];

        memset(result, 0, sizeof(*result));
        result->opcode = (uint16_t) MACHINE_LANGUAGE_RESULT;
        result->operands_done = 1u;
        result->operand_count = 1u;
        for (uint32_t argument = 0u; argument < argument_count; ++argument)
        {
            MachineLanguageValue_t copy = arguments[argument];

            machine_language_retain(load, &copy);
            status = machine_language_push(load, &copy);
        }
    }
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_enter_method(load, index, argument_count);
    while (status == MACHINE_LANGUAGE_OK && evaluation->frame_count > base + 1u)
        status = machine_language_step(load);
    if (status == MACHINE_LANGUAGE_OK)
        *out_result = machine_language_pop(evaluation);
    while (evaluation->frame_count > base + 1u)
        machine_language_drop_frame(load);
    machine_language_drop_frame(load);
    return status;
}
