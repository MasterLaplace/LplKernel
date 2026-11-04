#include <kernel/acpi/machine_language.h>

#include <string.h>

#define MACHINE_LANGUAGE_CALL   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT
#define MACHINE_LANGUAGE_RESULT (MACHINE_LANGUAGE_CALL + 1u)
#define MACHINE_LANGUAGE_ATTACH (MACHINE_LANGUAGE_CALL + 2u) /**< Waits for what an internal frame returns. */
#define MACHINE_LANGUAGE_REGISTER                                                                                      \
    (MACHINE_LANGUAGE_CALL + 3u) /**< Drops what the _REG an internal frame runs for a new region returns. */
#define MACHINE_LANGUAGE_ANSWER                                                                                        \
    (MACHINE_LANGUAGE_CALL + 4u) /**< Hands a statement of table-level code the Integer of the term it waits for. */
#define MACHINE_LANGUAGE_LOADED                                                                                        \
    (MACHINE_LANGUAGE_CALL + 5u) /**< Waits for the frame of the table Load or LoadTable added, its node's index. */
#define MACHINE_LANGUAGE_OK            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_OPERAND(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_##kind
#define MACHINE_LANGUAGE_OPCODE(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define MACHINE_LANGUAGE_NODE(type)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MACHINE_LANGUAGE_STATUS(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define MACHINE_LANGUAGE_VALUE(kind)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_##kind
#define MACHINE_LANGUAGE_FORM(name)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_##name
#define MACHINE_LANGUAGE_RECORD(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_##name
#define MACHINE_LANGUAGE_RECORD_OPERAND(name)                                                                          \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_##name
#define MACHINE_LANGUAGE_DECLINED(reason) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_##reason
#define MACHINE_LANGUAGE_TARGET           0x90u /**< A value that says where a result goes; auxiliary is its kind. */
#define MACHINE_LANGUAGE_ELSE_BYTE        0xA1u /**< The byte of Else, which only an If reads. */
#define MACHINE_LANGUAGE_ELSE_SKIPPED     0u /**< At load, the Else after an If is stepped over: the If ran its body. */
#define MACHINE_LANGUAGE_ELSE_RUN         1u /**< At load, the Else runs: the If's predicate was false. */
#define MACHINE_LANGUAGE_ELSE_NOT_RUN     2u /**< At load, the Else is declared, not run: the predicate failed. */
#define MACHINE_LANGUAGE_DATA_OBJECT                                                                                   \
    0x20u /**< Where a term must be a data object, as a Name's object: past every operand kind of a descriptor. */
#define MACHINE_LANGUAGE_DEBUG_OBJECT_TYPE 16u /**< What ObjectType gives for Debug (ACPI 6.6 §19.6.97). */
#define MACHINE_LANGUAGE_LOGICAL_BYTES     4u  /**< The bytes of a Buffer LAnd and LOr read, as Windows does (§5.2). */
#define MACHINE_LANGUAGE_VALUES_BEFORE_LIST                                                                            \
    1u /**< The values If, While, Package and VarPackage take before their list: the predicate, or the count. */
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
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t MachineLanguagePredecoding_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecord_t MachineLanguageRecord_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t MachineLanguageRecordNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand_t MachineLanguageRecordOperand_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordName_t MachineLanguageRecordName_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t MachineLanguageNamespace_t;

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
    MACHINE_LANGUAGE_RUN_TERMS = 1u, /**< An Else body, or the TermList of a Scope, Device, Processor, PowerResource
                                          or ThermalZone in a table's code: its end does nothing. */
    MACHINE_LANGUAGE_RUN_IF,         /**< A taken If body: its end skips an Else that follows. */
    MACHINE_LANGUAGE_RUN_WHILE,      /**< A While body: its end goes back to the While. */
    MACHINE_LANGUAGE_RUN_METHOD,     /**< A method body: its end returns. */
    MACHINE_LANGUAGE_RUN_TABLE,      /**< The code of a table, or the term of it a run was asked for. */
} MachineLanguageRunBlock_t;

#define MACHINE_LANGUAGE_IS_KIND(operand, kind)                                                                        \
    (MACHINE_LANGUAGE_OPERAND(operand) == MACHINE_LANGUAGE_OPERAND(kind)) /**< Whether a descriptor's operand is of    \
                                                                             @p kind. */
#define MACHINE_LANGUAGE_HAS_FIELD_LIST(k0, k1, k2, k3, k4, k5)                                                        \
    (MACHINE_LANGUAGE_IS_KIND(k0, FIELD_LIST) || MACHINE_LANGUAGE_IS_KIND(k1, FIELD_LIST) ||                           \
     MACHINE_LANGUAGE_IS_KIND(k2, FIELD_LIST) || MACHINE_LANGUAGE_IS_KIND(k3, FIELD_LIST) ||                           \
     MACHINE_LANGUAGE_IS_KIND(k4, FIELD_LIST) ||                                                                       \
     MACHINE_LANGUAGE_IS_KIND(k5, FIELD_LIST)) /**< Whether a descriptor ends with a FieldList, whose names push the   \
                                                   node they name. */
#define MACHINE_LANGUAGE_PUSHES_NO_VALUE(operand, field_list)                                                          \
    (MACHINE_LANGUAGE_OPERAND(operand) >= MACHINE_LANGUAGE_OPERAND(ASCII_STRING) ||                                    \
     (MACHINE_LANGUAGE_IS_KIND(operand, NAME_STRING) && !(field_list))) /**< Whether an operand pushes no value: a     \
                                                                           string, a list, or a NameString outside a   \
                                                                           field list's descriptor. */
#define MACHINE_LANGUAGE_VALUELESS_OPERANDS(field_list, k0, k1, k2, k3, k4, k5)                                        \
    (uint8_t)(MACHINE_LANGUAGE_PUSHES_NO_VALUE(k0, field_list) + MACHINE_LANGUAGE_PUSHES_NO_VALUE(k1, field_list) +    \
              MACHINE_LANGUAGE_PUSHES_NO_VALUE(k2, field_list) + MACHINE_LANGUAGE_PUSHES_NO_VALUE(k3, field_list) +    \
              MACHINE_LANGUAGE_PUSHES_NO_VALUE(k4, field_list) +                                                       \
              MACHINE_LANGUAGE_PUSHES_NO_VALUE(k5, field_list)) /**< How many of six operand kinds push no value. */
#define MACHINE_LANGUAGE_OPERANDS_WITHOUT_VALUE(identifier, encoding_value, name, package_length, statement,           \
                                                data_object, opens_scope, k0, k1, k2, k3, k4, k5)                      \
    [MACHINE_LANGUAGE_OPCODE(identifier)] = MACHINE_LANGUAGE_VALUELESS_OPERANDS(                                       \
        MACHINE_LANGUAGE_HAS_FIELD_LIST(k0, k1, k2, k3, k4, k5), k0, k1, k2, k3, k4,                                   \
        k5), /**< The entry of one descriptor in machine_language_operands_without_value. */

/**
 * @brief The operands of each operator that push no value: a NameString, but those of a field list, which push the
 *        node they name, a string and the lists; every operand of a call and of the operators the interpreter makes
 *        up pushes one value.
 */
static const uint8_t machine_language_operands_without_value[MACHINE_LANGUAGE_LOADED + 1u] = {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(
        MACHINE_LANGUAGE_OPERANDS_WITHOUT_VALUE)
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(
            MACHINE_LANGUAGE_OPERANDS_WITHOUT_VALUE)};

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
 * @brief Whether the running frame holds at least @p count values: an operator that takes more would take a value
 *        below the frame, as a producer that pushed none leaves it to (§6.2).
 */
static bool machine_language_frame_holds(const MachineLanguageEvaluation_t *evaluation, uint32_t count)
{
    return evaluation->value_count - evaluation->value_floor >= count;
}

/**
 * @brief Whether the running frame holds every value @p active takes, one per operand that pushes one: at most one
 *        per operand, so a frame that holds that many, as one almost always does, needs no look at which operands
 *        push one.
 */
static bool machine_language_holds_operands(const MachineLanguageEvaluation_t *evaluation,
                                            const MachineLanguagePendingOperator_t *active)
{
    return __builtin_expect(machine_language_frame_holds(evaluation, active->operand_count), 1) ||
           machine_language_frame_holds(evaluation, (uint32_t) active->operand_count -
                                                        machine_language_operands_without_value[active->opcode]);
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
    return advanced_configuration_and_power_interface_machine_language_mask_to_width(load->integer_bits, integer);
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
 * @brief Whether LEqual, LLess or LGreater holds between two Integers already masked to the width.
 */
static bool machine_language_integers_compare(uint32_t opcode, uint64_t left, uint64_t right)
{
    return (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL)) ? left == right :
           (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS))  ? left < right :
                                                                left > right;
}

/**
 * @brief The Integer Increment, or Decrement when @p up is false, makes of @p integer, masked to the
 *        width.
 */
static uint64_t machine_language_stepped(const MachineLanguageLoad_t *load, uint64_t integer, bool up)
{
    return machine_language_mask(load, up ? integer + 1u : integer - 1u);
}

/**
 * @brief Whether an Integer stored to a local or an argument overwrites it in place (§5.2): it holds
 *        nothing or an Integer, so no object is given back and no reference gone through.
 */
static bool machine_language_overwrites_in_place(const MachineLanguageValue_t *slot)
{
    return slot->kind == MACHINE_LANGUAGE_VALUE(UNINITIALIZED) || slot->kind == MACHINE_LANGUAGE_VALUE(INTEGER);
}

/**
 * @brief Stores an Integer to a local or an argument that machine_language_overwrites_in_place accepts:
 *        the value takes its place whole, unmasked, as a Store copies a value (§5.2), and @p value is
 *        left Uninitialized, its place taken.
 */
static void machine_language_overwrite_in_place(MachineLanguageValue_t *slot, MachineLanguageValue_t *value)
{
    *slot = *value;
    value->kind = MACHINE_LANGUAGE_VALUE(UNINITIALIZED);
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
 * @brief A local or an argument read as an operand, its reference retained: an Integer holds none; an
 *        uninitialized one reads as Integer 0, counted (§5.2).
 */
static MachineLanguageValue_t machine_language_read_slot(MachineLanguageLoad_t *load,
                                                         const MachineLanguageValue_t *slot)
{
    if (slot->kind == MACHINE_LANGUAGE_VALUE(INTEGER))
        return *slot;
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
 * @brief Builds the data object a Name kept in its table (O20) once a name resolution reaches its node @p index, as
 *        every resolution that reads or writes the node's value does first; a field list, which reads only a node's
 *        type, already the type of its object, builds nothing.
 */
static MachineLanguageStatus_t machine_language_reach(MachineLanguageLoad_t *load, uint32_t index)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    return (node->value.kind == MACHINE_LANGUAGE_VALUE(KEPT_DATA)) ?
               advanced_configuration_and_power_interface_machine_language_materialize(load, index) :
               MACHINE_LANGUAGE_OK;
}

/**
 * @brief The node a name a package element holds resolves to, from the scope it was read in, its kept data built.
 */
static MachineLanguageStatus_t machine_language_resolve_element_name(MachineLanguageLoad_t *load,
                                                                     const MachineLanguageValue_t *name_reference,
                                                                     uint32_t *out_node)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table =
        &load->tables[name_reference->auxiliary - 1u];
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        (const uint8_t *) table->header, (uint32_t) name_reference->payload, table->length, &name, &after);

    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, name_reference->handle, (const uint8_t *) table->header, &name, out_node);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_reach(load, *out_node) : status;
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
    if (object == NULL)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

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
    if (object == NULL)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
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

    if (value->kind == MACHINE_LANGUAGE_VALUE(INTEGER) && target->kind == MACHINE_LANGUAGE_TARGET &&
        (target->auxiliary == MACHINE_LANGUAGE_TARGET_LOCAL || target->auxiliary == MACHINE_LANGUAGE_TARGET_ARGUMENT))
    {
        slot = machine_language_slot(evaluation, target);
        if (machine_language_overwrites_in_place(slot))
        {
            machine_language_overwrite_in_place(slot, value);
            return MACHINE_LANGUAGE_OK;
        }
    }
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
 *        reference it holds if it holds one, but for a reference to a package element, which is
 *        replaced, as Windows does; a field is written; a reference is followed and what it refers to
 *        replaced.
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
        status = (machine_language_is_reference(location.slot->kind) &&
                  location.slot->kind != MACHINE_LANGUAGE_VALUE(REFERENCE_ELEMENT)) ?
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
 * @brief Runs CopyObject's store, then hands what it copied to the operator waiting for it, as Store hands
 *        its result, since CopyObject is an expression (ACPI 6.6 §20.2.5.4) any TermArg may be; it takes the
 *        references the value and the target hold.
 *
 * @details Never inlined: machine_language_step, which takes execute and transfer inline, would then spill one
 *          more register on every step, 25,038 more instructions in W4 (§7.4), whose loop runs no CopyObject.
 */
__attribute__((noinline)) static MachineLanguageStatus_t
machine_language_copy_and_deliver(MachineLanguageLoad_t *load, MachineLanguageValue_t value,
                                  MachineLanguageValue_t *target)
{
    const bool used = machine_language_active(&load->evaluation) != NULL;
    MachineLanguageValue_t copied = value;

    if (used)
        machine_language_retain(load, &value);

    const MachineLanguageStatus_t status = machine_language_copy_object(load, &copied, target);

    if (!used)
        return status;
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &value);
        return status;
    }
    return machine_language_push(load, &value);
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
 * @brief Runs `\_OSI`, which the namespace predefines without a body, in C: it takes its argument from the top of
 *        the value stack and gives its caller the answer as a method's result (§5.4).
 */
static MachineLanguageStatus_t machine_language_operating_system_interface(MachineLanguageLoad_t *load)
{
    MachineLanguageValue_t argument = machine_language_pop(&load->evaluation);
    bool supported = false;
    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_operating_system_interface(load, &argument,
                                                                                               &supported);

    machine_language_release(load, &argument);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_truth(load, supported)) :
                                             status;
}

/**
 * @brief Pushes a frame for a method whose arguments are the top @p argument_count values, which it
 *        takes with their references; runs `\_OSI`, told by its node, in C; refuses a method whose
 *        declaration recorded no body with METHOD_WITHOUT_BODY.
 */
static MachineLanguageStatus_t machine_language_enter_method(MachineLanguageLoad_t *load, uint32_t method,
                                                             uint32_t argument_count)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, method);

    if (node->type != MACHINE_LANGUAGE_NODE(METHOD))
        return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
    if (method == load->space.operating_system_interface)
        return machine_language_operating_system_interface(load);
    if (node->value.handle == 0u)
        return MACHINE_LANGUAGE_STATUS(METHOD_WITHOUT_BODY);
    if (evaluation->frame_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);

    uint8_t saved_sync_level = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_enter_method(
        load, (uint8_t) node->value.auxiliary, &saved_sync_level);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

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
    evaluation->value_floor = evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = (uint16_t) node->value.handle;
    frame->argument_count = (uint8_t) argument_count;
    frame->saved_sync_level = saved_sync_level;
    frame->serial = ++evaluation->frame_serial;
    frame->declares = false;
    frame->looping_start = 0u;
    frame->looping_end = 0u;
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
    load->declaring -= frame->declares ? 1u : 0u;
    advanced_configuration_and_power_interface_machine_language_leave_method(load, frame->saved_sync_level);
    --evaluation->frame_count;
    evaluation->value_floor = (evaluation->frame_count != 0u) ? machine_language_frame(evaluation)->value_base : 0u;
}

/**
 * @brief Pushes the frame of a table's code at load (§3.6), from @p start to @p limit in table @p table, its names
 *        resolved from @p scope, which declares while it runs.
 */
static MachineLanguageStatus_t machine_language_push_table_frame(MachineLanguageLoad_t *load, uint32_t table,
                                                                 uint32_t scope, uint32_t start, uint32_t limit)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (evaluation->frame_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);
    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguageFrame_t *frame = &evaluation->frames[evaluation->frame_count++];

    memset(frame, 0, sizeof(*frame));
    frame->code = (const uint8_t *) load->tables[table].header;
    frame->pc = start;
    frame->end = limit;
    frame->scope = scope;
    frame->value_base = (uint16_t) evaluation->value_count;
    evaluation->value_floor = evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = (uint16_t) (table + 1u);
    frame->serial = ++evaluation->frame_serial;
    frame->statement = start;
    frame->declares = true;
    ++load->declaring;
    if (evaluation->frame_count > evaluation->frames_peak)
        evaluation->frames_peak = evaluation->frame_count;

    const MachineLanguageStatus_t status =
        machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_TABLE, start, limit, scope);

    if (status != MACHINE_LANGUAGE_OK)
        machine_language_drop_frame(load);
    return status;
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

    const MachineLanguagePendingOperator_t *caller = machine_language_active(evaluation);

    if (caller == NULL)
        return MACHINE_LANGUAGE_OK;
    evaluation->implicit_returns += (caller->opcode == MACHINE_LANGUAGE_REGISTER) ? 0u : 1u;
    return machine_language_deliver(load, machine_language_integer(0u));
}

/**
 * @brief Counts a back-edge of a While, and fails once the evaluation has taken too many.
 */
static MachineLanguageStatus_t machine_language_count_iteration(MachineLanguageEvaluation_t *evaluation)
{
    ++evaluation->back_edges;
    return (++evaluation->iterations >
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_LOOP_ITERATIONS) ?
               MACHINE_LANGUAGE_STATUS(LIMIT_LOOP_ITERATIONS) :
               MACHINE_LANGUAGE_OK;
}

/**
 * @brief Counts a failure of table-level code: in the evaluation, per status, the first one kept.
 */
static void machine_language_count_failure(MachineLanguageEvaluation_t *evaluation, MachineLanguageStatus_t status)
{
    if (evaluation->failures++ == 0u)
        evaluation->first_failure = (uint8_t) status;
    ++evaluation->statuses[status];
}

/**
 * @brief Takes the back-edge of a While of a table's code at load, whose body @p block was, the running
 *        frame's pc at its predicate: once the table's budget is spent (§6.1), the failure is counted and
 *        the code goes on past the While instead.
 */
static MachineLanguageStatus_t machine_language_take_table_back_edge(MachineLanguageEvaluation_t *evaluation,
                                                                     const MachineLanguageBlock_t *block)
{
    if (machine_language_count_iteration(evaluation) == MACHINE_LANGUAGE_OK)
        return MACHINE_LANGUAGE_OK;
    machine_language_count_failure(evaluation, MACHINE_LANGUAGE_STATUS(LIMIT_LOOP_ITERATIONS));
    machine_language_frame(evaluation)->pc = block->end;
    return MACHINE_LANGUAGE_OK;
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
 * @brief Pushes the operator of the opcode at @p start, its PkgLength end or inherited limit @p end, none of its
 *        operands decoded yet.
 */
static MachineLanguageStatus_t machine_language_push_pending(MachineLanguageEvaluation_t *evaluation, uint32_t opcode,
                                                             uint32_t start, uint32_t end)
{
    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *pending = &evaluation->operators[evaluation->operator_count++];

    pending->opcode = (uint16_t) opcode;
    pending->operands_done = 0u;
    pending->operand_count =
        advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->operand_count;
    pending->start = start;
    pending->end = end;
    pending->node = 0u;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Takes a back-edge of a method's While whose body ended at @p block: the running frame's pc goes back to
 *        the While, and the back-edge is counted.
 */
static MachineLanguageStatus_t machine_language_take_back_edge(MachineLanguageEvaluation_t *evaluation,
                                                               const MachineLanguageBlock_t *block)
{
    machine_language_frame(evaluation)->pc = block->start;
    return machine_language_count_iteration(evaluation);
}

/**
 * @brief Pushes the operator of the While at the running frame's pc, which opened @p block, and resumes at its
 *        predicate, past the While's opcode and PkgLength, so that the While is not decoded again.
 */
static MachineLanguageStatus_t machine_language_push_while(MachineLanguageEvaluation_t *evaluation,
                                                           const MachineLanguageBlock_t *block)
{
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const MachineLanguageStatus_t status =
        machine_language_push_pending(evaluation, MACHINE_LANGUAGE_OPCODE(WHILE), block->start, block->end);

    if (status == MACHINE_LANGUAGE_OK)
        frame->pc = block->start + 1u +
                    advanced_configuration_and_power_interface_machine_language_package_length_bytes(
                        frame->code[block->start + 1u]);
    return status;
}

/**
 * @brief Takes a back-edge of a method's While whose body ended at @p block: counts it, then pushes the While's
 *        operator again and resumes at its predicate.
 */
static MachineLanguageStatus_t machine_language_restart_while(MachineLanguageEvaluation_t *evaluation,
                                                              const MachineLanguageBlock_t *block)
{
    const MachineLanguageStatus_t status = machine_language_take_back_edge(evaluation, block);

    return (status == MACHINE_LANGUAGE_OK) ? machine_language_push_while(evaluation, block) : status;
}

/**
 * @brief Drops the frame of a table's code once its last construct is left, its code over (§3.6); any other
 *        frame goes on.
 */
static MachineLanguageStatus_t machine_language_close_table(MachineLanguageLoad_t *load)
{
    const MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);

    if (frame->declares && load->evaluation.block_count == frame->block_base)
        machine_language_drop_frame(load);
    return MACHINE_LANGUAGE_OK;
}

static MachineLanguageStatus_t machine_language_loop_again(MachineLanguageLoad_t *load,
                                                           const MachineLanguageBlock_t *block);

/**
 * @brief Ends the construct the running frame is in: a method returns, a While goes back to its
 *        predicate, an If skips the Else after it; in a table's code at load, the If leaves that Else
 *        to the next statement, a While past the table's budget goes on past its end, the failure
 *        counted, and the end of the code drops the table's frame.
 */
static inline MachineLanguageStatus_t machine_language_close_block(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const MachineLanguageBlock_t block = *machine_language_block(evaluation);

    if (block.kind == MACHINE_LANGUAGE_RUN_METHOD)
        return machine_language_return(load, NULL);
    --evaluation->block_count;
    if (block.kind == MACHINE_LANGUAGE_RUN_IF && frame->declares)
    {
        frame->else_offset = block.end;
        frame->else_rule = MACHINE_LANGUAGE_ELSE_SKIPPED;
        return MACHINE_LANGUAGE_OK;
    }
    if (block.kind == MACHINE_LANGUAGE_RUN_IF)
        return machine_language_skip_else(evaluation);
    if (block.kind != MACHINE_LANGUAGE_RUN_WHILE)
        return machine_language_close_table(load);
    if (!frame->declares)
        return load->predecodes_loops ? machine_language_loop_again(load, &block) :
                                        machine_language_restart_while(evaluation, &block);
    frame->pc = block.start;
    return machine_language_take_table_back_edge(evaluation, &block);
}

/**
 * @brief Runs a Break or a Continue: leaves the innermost While of the running frame, past its end
 *        or back to its predicate.
 */
static inline MachineLanguageStatus_t machine_language_leave_loop(MachineLanguageEvaluation_t *evaluation, bool again)
{
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    for (uint32_t index = evaluation->block_count; index > frame->block_base; --index)
    {
        const MachineLanguageBlock_t *block = &evaluation->blocks[index - 1u];

        if (block->kind != MACHINE_LANGUAGE_RUN_WHILE)
            continue;
        frame->pc = block->end;
        evaluation->operator_count = block->pending_base;
        evaluation->block_count = index - 1u;
        return again ? machine_language_restart_while(evaluation, block) : MACHINE_LANGUAGE_OK;
    }
    return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
}

/**
 * @brief Whether the interpreter runs an opcode that pushes an operator: control flow, Store and
 *        CopyObject, Name and the buffer fields in a method, the operators on integers, strings,
 *        buffers, packages and references, and the operating system services the needs-path closure
 *        reaches (§4.1): Notify, Sleep, Stall, Timer, Acquire, Release and Signal.
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
    case MACHINE_LANGUAGE_OPCODE(CREATE_FIELD):
    case MACHINE_LANGUAGE_OPCODE(OPERATION_REGION):
    case MACHINE_LANGUAGE_OPCODE(FIELD):
    case MACHINE_LANGUAGE_OPCODE(INDEX_FIELD):
    case MACHINE_LANGUAGE_OPCODE(BANK_FIELD):
    case MACHINE_LANGUAGE_OPCODE(NOTIFY):
    case MACHINE_LANGUAGE_OPCODE(SLEEP):
    case MACHINE_LANGUAGE_OPCODE(STALL):
    case MACHINE_LANGUAGE_OPCODE(TIMER):
    case MACHINE_LANGUAGE_OPCODE(ACQUIRE):
    case MACHINE_LANGUAGE_OPCODE(RELEASE):
    case MACHINE_LANGUAGE_OPCODE(SIGNAL):
    case MACHINE_LANGUAGE_OPCODE(LOAD):
    case MACHINE_LANGUAGE_OPCODE(LOAD_TABLE): return true;
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

    const MachineLanguageStatus_t status = machine_language_push_pending(evaluation, opcode, start, end);

    if (status == MACHINE_LANGUAGE_OK)
        frame->pc = after;
    return status;
}

/**
 * @brief Reads an integer constant: Zero, One, Ones, or a prefix and its little-endian bytes.
 */
static MachineLanguageStatus_t machine_language_constant(MachineLanguageLoad_t *load, uint32_t opcode, uint32_t after,
                                                         uint32_t limit)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    uint64_t integer = 0u;
    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_constant(
        frame->code, opcode, after, limit, &integer, &frame->pc);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
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
 * @brief Whether an internal frame already evaluates an object the attach of @p region needs: that
 *        attach then needs itself.
 */
static bool machine_language_attaching(const MachineLanguageEvaluation_t *evaluation, uint32_t region)
{
    for (uint32_t index = 0u; index < evaluation->operator_count; ++index)
        if (evaluation->operators[index].opcode == MACHINE_LANGUAGE_ATTACH &&
            evaluation->operators[index].node == region)
            return true;
    return false;
}

/**
 * @brief Takes the attach of each PCI_Config region a unit's accesses reach as far as data takes it,
 *        and names the first method one of them waits for, with its region; a region whose attach
 *        an internal frame already serves fails as region_unevaluated.
 */
static MachineLanguageStatus_t machine_language_attach_step(MachineLanguageLoad_t *load, uint32_t unit,
                                                            uint32_t *out_region, uint32_t *out_method)
{
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    *out_method = 0u;
    for (*out_region =
             advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_unattached_region(
                 load, unit);
         *out_region != 0u && status == MACHINE_LANGUAGE_OK && *out_method == 0u;
         *out_region =
             advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_unattached_region(
                 load, unit))
    {
        if (machine_language_attaching(&load->evaluation, *out_region))
            return MACHINE_LANGUAGE_STATUS(REGION_UNEVALUATED);
        status = advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_attach(
            load, *out_region, NULL, out_method);
        if (*out_method != 0u)
            return status;
    }
    return status;
}

/**
 * @brief Pushes an internal frame (§3.6): @p method, called without arguments, whose result an attach
 *        operator hands to the attach of @p region.
 */
static MachineLanguageStatus_t machine_language_push_attach(MachineLanguageLoad_t *load, uint32_t region,
                                                            uint32_t method, uint32_t start, uint32_t end)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *attach = &evaluation->operators[evaluation->operator_count++];

    attach->opcode = MACHINE_LANGUAGE_ATTACH;
    attach->operands_done = 1u;
    attach->operand_count = 1u;
    attach->start = start;
    attach->end = end;
    attach->node = region;
    ++load->regions.internal_evaluations;
    return machine_language_enter_method(load, method, 0u);
}

/**
 * @brief Whether a frame of the evaluation runs @p method, however it was called.
 */
static bool machine_language_running(const MachineLanguageEvaluation_t *evaluation, uint32_t method)
{
    for (uint32_t index = 0u; index < evaluation->frame_count; ++index)
        if (evaluation->frames[index].method == method)
            return true;
    return false;
}

/**
 * @brief Pushes the internal frame that runs _REG (PCI_Config, 1) for a PCI_Config region a method has
 *        just created (§5.3), once the initialisation has registered, unless that _REG already runs: a
 *        _REG that creates such a region in its own scope is not called again. A _REG that does not
 *        take two arguments is not called, and counted as failed.
 */
static MachineLanguageStatus_t machine_language_register_at_creation(MachineLanguageLoad_t *load, uint32_t region)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t method =
        load->regions.registers_at_creation ?
            advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_registration(
                load, region) :
            0u;
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, method);

    if (method == 0u || machine_language_running(evaluation, method))
        return MACHINE_LANGUAGE_OK;
    if ((node->value.auxiliary & 0x7u) != 2u)
    {
        ++load->regions.pci_registration_failures;
        return MACHINE_LANGUAGE_OK;
    }
    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *registration = &evaluation->operators[evaluation->operator_count++];

    registration->opcode = MACHINE_LANGUAGE_REGISTER;
    registration->operands_done = 1u;
    registration->operand_count = 1u;
    registration->start = 0u;
    registration->end = 0u;
    registration->node = node->parent;
    ++load->regions.pci_registrations;

    MachineLanguageValue_t space = machine_language_integer(
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PERIPHERAL_COMPONENT_INTERCONNECT_CONFIGURATION);
    MachineLanguageValue_t connected = machine_language_integer(1u);
    MachineLanguageStatus_t status = machine_language_push(load, &space);

    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_push(load, &connected);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_enter_method(load, method, 2u) : status;
}

/**
 * @brief Puts the name read at @p start back, so that it runs again once the internal frame for
 *        @p region has returned, and pushes that frame.
 */
static MachineLanguageStatus_t machine_language_wait_for_attach(MachineLanguageLoad_t *load, uint32_t region,
                                                                uint32_t method, uint32_t start, uint32_t limit)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguagePendingOperator_t *active = machine_language_active(evaluation);

    machine_language_frame(evaluation)->pc = start;
    if (active != NULL)
        --active->operands_done;
    return machine_language_push_attach(load, region, method, start, limit);
}

/**
 * @brief Runs a NameString read where @p kind is expected: a method is called, unless the operand is
 *        a SimpleName; a target position names the object; anywhere else the object gives its value.
 *        The SimpleName of a CondRefOf that names nothing, or only an External, gives a missing target.
 *        A field unit attaches its PCI_Config regions first: when that needs a method, the name waits
 *        for its internal frame and runs again after it. Kept data is built here, on the node already
 *        found, not through machine_language_reach, whose second lookup of the node every name term would pay.
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

    if (node->value.kind == MACHINE_LANGUAGE_VALUE(KEPT_DATA))
    {
        status = advanced_configuration_and_power_interface_machine_language_materialize(load, index);
        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    if (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL) && machine_language_asks_existence(evaluation))
        return machine_language_deliver(load, machine_language_target(MACHINE_LANGUAGE_TARGET_MISSING, 0u));
    if (node->type == MACHINE_LANGUAGE_NODE(FIELD_UNIT))
    {
        uint32_t region = 0u;
        uint32_t method = 0u;

        status = machine_language_attach_step(load, index, &region, &method);
        if (status != MACHINE_LANGUAGE_OK)
            return status;
        if (method != 0u)
            return machine_language_wait_for_attach(load, region, method, start, limit);
    }

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

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        frame->code, start, limit, &opcode, &after);

    if (status == MACHINE_LANGUAGE_STATUS(INVALID_OPCODE) &&
        advanced_configuration_and_power_interface_machine_language_starts_name(frame->code[start]))
        return element   ? machine_language_name_element(load) :
               data_only ? MACHINE_LANGUAGE_STATUS(INVALID_OPCODE) :
                           machine_language_name_term(load, kind);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (opcode >= MACHINE_LANGUAGE_OPCODE(LOCAL0) && opcode <= MACHINE_LANGUAGE_OPCODE(LOCAL7) && !data_only)
    {
        const uint32_t slot = opcode - MACHINE_LANGUAGE_OPCODE(LOCAL0);

        frame->pc = after;
        return machine_language_deliver(load, target ? machine_language_target(MACHINE_LANGUAGE_TARGET_LOCAL, slot) :
                                                       machine_language_read_slot(load, &frame->locals[slot]));
    }
    if (opcode >= MACHINE_LANGUAGE_OPCODE(ARGUMENT0) && opcode <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6) && !data_only)
    {
        const uint32_t slot = opcode - MACHINE_LANGUAGE_OPCODE(ARGUMENT0);

        frame->pc = after;
        return machine_language_deliver(load, target ? machine_language_target(MACHINE_LANGUAGE_TARGET_ARGUMENT, slot) :
                                                       machine_language_read_slot(load, &frame->arguments[slot]));
    }
    if (kind != MACHINE_LANGUAGE_OPERAND(NONE) && !machine_language_fits_operand(opcode, data_only))
        return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
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
 * @brief Creates the temporary node of type @p type that a declaration in a method makes; a name
 *        that exists already, such as one the same frame created in an earlier turn of a loop, fails
 *        (§5.1).
 */
static MachineLanguageStatus_t machine_language_create_temporary(MachineLanguageLoad_t *load,
                                                                 const MachineLanguageNameString_t *name, uint8_t type,
                                                                 uint32_t *out_node)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    uint32_t parent = 0u;
    uint32_t segment = 0u;
    MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_namespace_creation_place(
            &load->space, machine_language_block(evaluation)->scope, frame->code, name, &parent, &segment);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (advanced_configuration_and_power_interface_machine_language_namespace_find_child(&load->space, parent,
                                                                                         segment) != 0u)
        return MACHINE_LANGUAGE_STATUS(NAME_EXISTS);
    if (load->temporary_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TEMPORARY_NODES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_TEMPORARY_NODES);
    status = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        &load->space, parent, segment, type, frame->table, out_node);
    if (status == MACHINE_LANGUAGE_OK)
        load->temporaries[load->temporary_count++] = *out_node;
    return status;
}

/**
 * @brief Creates the temporary node a Name, an OperationRegion or a CreateXField in a method declares.
 */
static MachineLanguageStatus_t machine_language_declare_name(MachineLanguageLoad_t *load,
                                                             MachineLanguagePendingOperator_t *active)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        frame->code, frame->pc, active->end, &name, &after);

    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_create_temporary(load, &name, MACHINE_LANGUAGE_NODE(UNINITIALIZED), &active->node);
    if (status == MACHINE_LANGUAGE_OK)
        frame->pc = after;
    return status;
}

/**
 * @brief Whether an opcode declares a field list: Field, IndexField or BankField.
 */
static bool machine_language_declares_field_list(uint32_t opcode)
{
    return opcode == MACHINE_LANGUAGE_OPCODE(FIELD) || opcode == MACHINE_LANGUAGE_OPCODE(INDEX_FIELD) ||
           opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD);
}

/**
 * @brief Reads a name a field list in a method gives, its region or a register, and pushes its node as
 *        an Integer for the field list to take; a name that resolves to nothing fails.
 */
static MachineLanguageStatus_t machine_language_field_name(MachineLanguageLoad_t *load,
                                                           const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    uint32_t node = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        frame->code, frame->pc, active->end, &name, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, machine_language_block(evaluation)->scope, frame->code, &name, &node) != MACHINE_LANGUAGE_OK)
        return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);
    frame->pc = after;
    return machine_language_deliver(load, machine_language_integer(node));
}

/**
 * @brief Opens a construct of the running frame, from @p start to @p end; in a table's code at load,
 *        one past the block limit is a fault of the load, counted, and the code goes on past it (§6.2),
 *        the next statement stepping over an Else, which only the body of an If can be followed by.
 */
static inline MachineLanguageStatus_t machine_language_open_construct(MachineLanguageLoad_t *load, uint8_t kind,
                                                                      uint32_t start, uint32_t end)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const MachineLanguageStatus_t status =
        machine_language_open(evaluation, kind, start, end, machine_language_block(evaluation)->scope);

    if (status != MACHINE_LANGUAGE_STATUS(LIMIT_BLOCKS) || !frame->declares)
        return status;
    advanced_configuration_and_power_interface_machine_language_count_load_error(load, status);
    frame->pc = end;
    frame->else_rule = MACHINE_LANGUAGE_ELSE_SKIPPED;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs the TermList of an If or a While once its predicate is on the stack: a true predicate
 *        opens the body; a false one skips it, and an If then enters the Else that follows, or, in a
 *        table's code at load, lets the next statement run it.
 */
static MachineLanguageStatus_t machine_language_branch(MachineLanguageLoad_t *load,
                                                       const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const MachineLanguagePendingOperator_t branch = *active;

    if (!machine_language_frame_holds(evaluation, MACHINE_LANGUAGE_VALUES_BEFORE_LIST))
        return MACHINE_LANGUAGE_STATUS(OPERANDS_MISSING);

    MachineLanguageValue_t predicate = machine_language_pop(evaluation);
    uint64_t truth = 0u;
    const MachineLanguageStatus_t status = machine_language_integer_of(load, &predicate, &truth);

    machine_language_release(load, &predicate);
    --evaluation->operator_count;
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (truth != 0u)
        return machine_language_open_construct(
            load,
            (branch.opcode == MACHINE_LANGUAGE_OPCODE(WHILE)) ? MACHINE_LANGUAGE_RUN_WHILE : MACHINE_LANGUAGE_RUN_IF,
            branch.start, branch.end);
    frame->pc = branch.end;
    if (frame->declares)
    {
        frame->else_offset = branch.end;
        frame->else_rule =
            (branch.opcode == MACHINE_LANGUAGE_OPCODE(IF)) ? MACHINE_LANGUAGE_ELSE_RUN : MACHINE_LANGUAGE_ELSE_SKIPPED;
        return MACHINE_LANGUAGE_OK;
    }
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
    {
        if (!machine_language_frame_holds(evaluation, MACHINE_LANGUAGE_VALUES_BEFORE_LIST))
            return MACHINE_LANGUAGE_STATUS(OPERANDS_MISSING);
        active->node = evaluation->value_count + 1u;
    }
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
    case MACHINE_LANGUAGE_OPERAND(NAME_STRING):
        return machine_language_declares_field_list(active->opcode) ? machine_language_field_name(load, active) :
                                                                      machine_language_declare_name(load, active);
    case MACHINE_LANGUAGE_OPERAND(BYTE_DATA): return machine_language_fixed_operand(load, active, 1u);
    case MACHINE_LANGUAGE_OPERAND(WORD_DATA): return machine_language_fixed_operand(load, active, 2u);
    case MACHINE_LANGUAGE_OPERAND(DOUBLE_WORD_DATA): return machine_language_fixed_operand(load, active, 4u);
    case MACHINE_LANGUAGE_OPERAND(QUAD_WORD_DATA): return machine_language_fixed_operand(load, active, 8u);
    case MACHINE_LANGUAGE_OPERAND(BYTE_LIST):
    case MACHINE_LANGUAGE_OPERAND(FIELD_LIST): return MACHINE_LANGUAGE_OK;
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
        load, machine_language_integer(machine_language_stepped(load, integer, up)), &target);
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

    if (first.kind == MACHINE_LANGUAGE_VALUE(INTEGER) && second.kind == MACHINE_LANGUAGE_VALUE(INTEGER))
        return machine_language_deliver(
            load, machine_language_truth(
                      load, machine_language_integers_compare(opcode, machine_language_mask(load, first.payload),
                                                              machine_language_mask(load, second.payload))));

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
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_namespace_resolve(
            &load->space, scope, spelled, &name, out_node);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_reach(load, *out_node) : status;
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
 * @brief Runs an OperationRegion a method declares: its node, temporary, takes a region of the space,
 *        offset and length on the stack, and a PCI_Config one gets its _REG at once (§5.3).
 */
static MachineLanguageStatus_t machine_language_operation_region(MachineLanguageLoad_t *load,
                                                                 const MachineLanguagePendingOperator_t *active)
{
    uint64_t operands[3];
    MachineLanguageStatus_t status = machine_language_pop_integers(load, 3u, operands);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

    status = advanced_configuration_and_power_interface_machine_language_region_build(
        load, active->node, (uint8_t) operands[0], operands[1], operands[2]);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, active->node)->type =
        MACHINE_LANGUAGE_NODE(OPERATION_REGION);
    return machine_language_register_at_creation(load, active->node);
}

/**
 * @brief Reads the PkgLength of a reserved field or a named unit, and moves @p bit past it; a list
 *        that passes 2^32 bits, which a unit's offset cannot hold, fails as index_out_of_range.
 */
static MachineLanguageStatus_t machine_language_field_bits(const MachineLanguageFrame_t *frame, uint32_t start,
                                                           uint32_t end, uint32_t *bit, uint32_t *out_bits,
                                                           uint32_t *out_after)
{
    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_read_package_length(frame->code, start, end,
                                                                                        out_bits, out_after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (*out_bits > 0xFFFFFFFFu - *bit)
        return MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);
    *bit += *out_bits;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Where the operand of a Connection in a field list ends: a NameString, or a Buffer whose
 *        PkgLength gives its end.
 */
static MachineLanguageStatus_t machine_language_connection_end(const MachineLanguageFrame_t *frame, uint32_t start,
                                                               uint32_t end, uint32_t *out_after)
{
    MachineLanguageNameString_t name;
    uint32_t contents = 0u;

    if (start >= end)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    if (frame->code[start] == 0x11u)
        return advanced_configuration_and_power_interface_machine_language_read_package_end(frame->code, start + 1u,
                                                                                            end, out_after, &contents);
    return advanced_configuration_and_power_interface_machine_language_read_name_string(frame->code, start, end, &name,
                                                                                        out_after);
}

/**
 * @brief Creates the temporary unit a named element of a field list in a method declares, at bit
 *        @p unit->bit_offset, @p unit->bit_length bits long; an element named otherwise than by one
 *        NameSeg is a bad NameString, truncated, as at table level.
 */
static MachineLanguageStatus_t
machine_language_field_unit(MachineLanguageLoad_t *load, uint32_t start, uint32_t end,
                            const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *unit)
{
    const MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    MachineLanguageNameString_t name;
    uint32_t after = 0u;
    uint32_t node = 0u;
    MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_name_string(
        frame->code, start, end, &name, &after);

    if (status == MACHINE_LANGUAGE_OK && (name.root || name.parent_prefixes != 0u || name.segment_count != 1u))
        status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_create_temporary(load, &name, MACHINE_LANGUAGE_NODE(FIELD_UNIT), &node);
    return (status == MACHINE_LANGUAGE_OK) ?
               advanced_configuration_and_power_interface_machine_language_field_unit_build(load, node, unit) :
               status;
}

/**
 * @brief Runs a Field, an IndexField or a BankField a method declares: each named element becomes a
 *        temporary unit of the region or registers and the FieldFlags on the stack; AccessAs changes the
 *        access of the units after it; a Connection, which only GenericSerialBus and GPIO fields use,
 *        is passed over, as at table level, those spaces having no handler.
 */
static MachineLanguageStatus_t machine_language_field_list(MachineLanguageLoad_t *load,
                                                           const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    const uint32_t count = (active->opcode == MACHINE_LANGUAGE_OPCODE(BANK_FIELD))  ? 4u :
                           (active->opcode == MACHINE_LANGUAGE_OPCODE(INDEX_FIELD)) ? 3u :
                                                                                      2u;
    uint64_t operands[4] = {0u};
    MachineLanguageStatus_t status = machine_language_pop_integers(load, count, operands);
    const uint8_t flags = (uint8_t) operands[count - 1u];
    AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t unit = {
        .kind = (count == 4u) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_BANK :
                (count == 3u) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_INDEX :
                                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_NORMAL,
        .access = flags & 0x0Fu,
        .update = (flags >> 5u) & 0x3u,
        .lock = (flags >> 4u) & 0x1u,
        .region = (uint32_t) operands[0],
        .data_or_bank = (count > 2u) ? (uint32_t) operands[1] : 0u,
        .bank_value = (count == 4u) ? (uint32_t) operands[2] : 0u,
    };
    uint32_t bit = 0u;
    uint32_t pc = frame->pc;

    while (status == MACHINE_LANGUAGE_OK && pc < active->end)
    {
        const uint8_t lead = frame->code[pc];
        uint32_t bits = 0u;
        uint32_t after = 0u;

        if (lead == 0x00u)
            status = machine_language_field_bits(frame, pc + 1u, active->end, &bit, &bits, &after);
        else if (lead == 0x01u || lead == 0x03u)
        {
            after = pc + ((lead == 0x01u) ? 3u : 4u);
            if (after > active->end)
                status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
            else
                unit.access = frame->code[pc + 1u] & 0x0Fu;
        }
        else if (lead == 0x02u)
            status = machine_language_connection_end(frame, pc + 1u, active->end, &after);
        else if (pc + 4u > active->end)
            status = MACHINE_LANGUAGE_STATUS(TRUNCATED);
        else
        {
            unit.bit_offset = bit;
            status = machine_language_field_bits(frame, pc + 4u, active->end, &bit, &bits, &after);
            unit.bit_length = bits;
            if (status == MACHINE_LANGUAGE_OK)
                status = machine_language_field_unit(load, pc, pc + 4u, &unit);
        }
        pc = after;
    }
    frame->pc = active->end;
    return status;
}

/**
 * @brief The node a SuperName names, whose reference it takes: a named object, or the one a local, an
 *        argument or a reference refers to at the end of its chain (§5.2).
 */
static MachineLanguageStatus_t machine_language_named_object(MachineLanguageLoad_t *load,
                                                             MachineLanguageValue_t *operand, uint32_t *out_node)
{
    MachineLanguageValue_t reference;
    MachineLanguageLocation_t location;
    MachineLanguageStatus_t status = machine_language_reference_of(load, operand, &reference);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    status = machine_language_locate(load, &reference, true, &location);
    machine_language_release(load, &reference);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (location.kind != MACHINE_LANGUAGE_LOCATION_NODE)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    *out_node = location.node;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs Notify: the notification waits in the ring until the evaluation is over (§3.7).
 */
static MachineLanguageStatus_t machine_language_notify(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t value = machine_language_pop(evaluation);
    MachineLanguageValue_t object = machine_language_pop(evaluation);
    uint64_t integer = 0u;
    uint32_t node = 0u;
    MachineLanguageStatus_t status = machine_language_integer_of(load, &value, &integer);

    machine_language_release(load, &value);
    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_release(load, &object);
        return status;
    }
    status = machine_language_named_object(load, &object, &node);
    return (status == MACHINE_LANGUAGE_OK) ?
               advanced_configuration_and_power_interface_machine_language_notify(load, node, integer) :
               status;
}

/**
 * @brief Runs Sleep or Stall on the time its operand asks for (§5.5).
 */
static MachineLanguageStatus_t machine_language_wait(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageValue_t operand = machine_language_pop(&load->evaluation);
    uint64_t integer = 0u;
    const MachineLanguageStatus_t status = machine_language_integer_of(load, &operand, &integer);

    machine_language_release(load, &operand);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return (opcode == MACHINE_LANGUAGE_OPCODE(SLEEP)) ?
               advanced_configuration_and_power_interface_machine_language_sleep(load, integer) :
               advanced_configuration_and_power_interface_machine_language_stall(load, integer);
}

/**
 * @brief Runs Acquire: 0 when the mutex is held, Ones when the acquisition was ignored (§5.2); the
 *        timeout is never waited, since nothing else holds an AML mutex.
 */
static MachineLanguageStatus_t machine_language_acquire(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t timeout = machine_language_pop(evaluation);
    MachineLanguageValue_t object = machine_language_pop(evaluation);
    uint32_t node = 0u;
    bool acquired = false;
    MachineLanguageStatus_t status = machine_language_named_object(load, &object, &node);

    machine_language_release(load, &timeout);
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_acquire(load, node, &acquired);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_truth(load, !acquired)) :
                                             status;
}

/**
 * @brief Runs Release or Signal on the object its SuperName names (§5.2).
 */
static MachineLanguageStatus_t machine_language_synchronize(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageValue_t object = machine_language_pop(&load->evaluation);
    uint32_t node = 0u;
    const MachineLanguageStatus_t status = machine_language_named_object(load, &object, &node);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    return (opcode == MACHINE_LANGUAGE_OPCODE(RELEASE)) ?
               advanced_configuration_and_power_interface_machine_language_release(load, node) :
               advanced_configuration_and_power_interface_machine_language_signal(load, node);
}

/**
 * @brief Gives what Load or LoadTable gives once the code of the table it added, @p table, is declared: the
 *        table's load finished, True, stored to @p target first when the operator has one.
 */
static MachineLanguageStatus_t machine_language_loaded(MachineLanguageLoad_t *load, uint32_t table,
                                                       MachineLanguageValue_t *target)
{
    const MachineLanguageValue_t truth = machine_language_truth(load, true);

    advanced_configuration_and_power_interface_machine_language_finish_loaded_table(load, table);
    return (target != NULL) ? machine_language_store_and_deliver(load, truth, target) :
                              machine_language_deliver(load, truth);
}

/**
 * @brief Pushes the operator that waits for the frame of table @p table, with @p target below it when Load has one,
 *        then that frame (§3.6), in the room the operator that added the table and its operands left; pushes nothing
 *        when the evaluation has no room for the frame.
 */
static MachineLanguageStatus_t machine_language_push_loaded(MachineLanguageLoad_t *load, uint32_t table,
                                                            MachineLanguageValue_t *target)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (target != NULL)
        (void) machine_language_push(load, target);

    MachineLanguagePendingOperator_t *loaded = &evaluation->operators[evaluation->operator_count++];

    loaded->opcode = MACHINE_LANGUAGE_LOADED;
    loaded->operands_done = (target != NULL) ? 1u : 0u;
    loaded->operand_count = loaded->operands_done;
    loaded->start = 0u;
    loaded->end = 0u;
    loaded->node = table;

    const MachineLanguageStatus_t status = machine_language_push_table_frame(
        load, table, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t), load->tables[table].length);

    if (status == MACHINE_LANGUAGE_OK)
        return status;
    --evaluation->operator_count;
    if (target != NULL)
        *target = machine_language_pop(evaluation);
    return status;
}

/**
 * @brief Runs the code of the table @p table Load or LoadTable just added in a frame of the evaluation that runs the
 *        operator, which waits for it as a call waits for its method (§3.6); where the load declares every branch,
 *        or the evaluation has no room for that frame, the failure then counted, the table's code is declared
 *        without running, and the operator gives its result at once.
 */
static MachineLanguageStatus_t machine_language_run_loaded_table(MachineLanguageLoad_t *load, uint32_t table,
                                                                 MachineLanguageValue_t *target)
{
    const MachineLanguageStatus_t status =
        load->declares_every_branch ? MACHINE_LANGUAGE_OK : machine_language_push_loaded(load, table, target);

    if (status == MACHINE_LANGUAGE_OK && !load->declares_every_branch)
        return status;
    if (status != MACHINE_LANGUAGE_OK)
        machine_language_count_failure(&load->evaluation, status);
    advanced_configuration_and_power_interface_machine_language_declare_loaded_table(load, table);
    return machine_language_loaded(load, table, target);
}

/**
 * @brief Runs Load (§5.1): once the table is added, runs its code, then gives True to its target and as its
 *        result; False for a table the load holds already; an operand that names no node, as a local or an
 *        argument that holds a Buffer, is a form the kernel does not load.
 */
static MachineLanguageStatus_t machine_language_load_operator(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t source = machine_language_pop(evaluation);
    uint32_t node = 0u;
    bool loaded = false;
    MachineLanguageStatus_t status = machine_language_named_object(load, &source, &node);

    if (status == MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH))
        status = MACHINE_LANGUAGE_STATUS(LOAD_FORM_UNSUPPORTED);
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_load_from(load, node, &loaded);
    if (status == MACHINE_LANGUAGE_OK && loaded)
        return machine_language_run_loaded_table(load, load->table_count - 1u, &target);
    return machine_language_finish(load, status, machine_language_truth(load, false), &target);
}

/**
 * @brief Runs LoadTable (§5.1): its first five operands must be Strings, its RootPath and ParameterPath
 *        empty and its ParameterData Zero; once the table is added, runs its code, then gives True; False
 *        when none matches or the load holds it already.
 */
static MachineLanguageStatus_t machine_language_load_table_operator(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t operands[6];
    const uint8_t *bytes[5] = {NULL, NULL, NULL, NULL, NULL};
    uint32_t lengths[5] = {0u, 0u, 0u, 0u, 0u};
    uint8_t scratch[8];
    bool loaded = false;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    for (uint32_t index = 6u; index != 0u; --index)
        operands[index - 1u] = machine_language_pop(evaluation);
    for (uint32_t index = 0u; index < 5u && status == MACHINE_LANGUAGE_OK; ++index)
        status = (operands[index].kind == MACHINE_LANGUAGE_VALUE(STRING)) ?
                     advanced_configuration_and_power_interface_machine_language_value_bytes(
                         &load->heap, &operands[index], machine_language_bits(load), scratch, &bytes[index],
                         &lengths[index]) :
                     MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (status == MACHINE_LANGUAGE_OK &&
        (lengths[3] != 0u || lengths[4] != 0u || operands[5].kind != MACHINE_LANGUAGE_VALUE(INTEGER) ||
         operands[5].payload != 0u))
        status = MACHINE_LANGUAGE_STATUS(LOAD_FORM_UNSUPPORTED);
    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_load_from_directory(
            load, bytes[0], bytes[1], bytes[2], lengths, &loaded);
    for (uint32_t index = 0u; index < 6u; ++index)
        machine_language_release(load, &operands[index]);
    if (status == MACHINE_LANGUAGE_OK && loaded)
        return machine_language_run_loaded_table(load, load->table_count - 1u, NULL);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(load, machine_language_truth(load, false)) :
                                             status;
}

/**
 * @brief Runs Store or CopyObject: the target takes the value, and each gives it as its result.
 */
static MachineLanguageStatus_t machine_language_transfer(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t value = machine_language_pop(evaluation);

    if (opcode == MACHINE_LANGUAGE_OPCODE(STORE))
        return machine_language_store_and_deliver(load, value, &target);
    return machine_language_copy_and_deliver(load, value, &target);
}

/**
 * @brief Hands what an internal frame returned to the attach that waited for it, and pushes the next
 *        internal frame that attach needs, if any.
 */
static MachineLanguageStatus_t machine_language_attach_result(MachineLanguageLoad_t *load,
                                                              const MachineLanguagePendingOperator_t *attach)
{
    MachineLanguageValue_t value = machine_language_pop(&load->evaluation);
    uint32_t method = 0u;
    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_attach(
            load, attach->node, &value, &method);

    machine_language_release(load, &value);
    if (status != MACHINE_LANGUAGE_OK || method == 0u)
        return status;
    return machine_language_push_attach(load, attach->node, method, attach->start, attach->end);
}

/**
 * @brief Pushes the operator that runs the term the statement of a table's code waits for, which
 *        load->declaration names, and counts it as a run.
 */
static MachineLanguageStatus_t machine_language_ask(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t *declaration = &load->declaration;

    if (evaluation->operator_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS)
        return MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS);

    MachineLanguagePendingOperator_t *answer = &evaluation->operators[evaluation->operator_count++];

    answer->opcode = (uint16_t) MACHINE_LANGUAGE_ANSWER;
    answer->operands_done = 0u;
    answer->operand_count = 1u;
    answer->start = declaration->asks;
    answer->end = declaration->asks_end;
    answer->node = 0u;
    machine_language_frame(evaluation)->pc = declaration->asks;
    ++evaluation->runs;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Hands the load the statement of a table's code the running frame is at, a statement it does not
 *        run itself, then goes on as the load says: past the statement, into the TermList of the scope it
 *        declared, at the term it waits for, past the construct a fault left, the next statement stepping
 *        over the Else after it when it is the body of an If, or past the table, whose frame it drops.
 *
 * @param load    The load.
 * @param not_run Whether the statement's code is declared without running it.
 */
static MachineLanguageStatus_t machine_language_declare(MachineLanguageLoad_t *load, bool not_run)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const MachineLanguageBlock_t *block = machine_language_block(evaluation);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t *declaration = &load->declaration;

    advanced_configuration_and_power_interface_machine_language_declare_statement(
        load, frame->table - 1u, block->scope, frame->statement, block->end, not_run);
    if (declaration->asks != 0u)
        return machine_language_ask(load);
    if (declaration->abandons)
    {
        machine_language_drop_frame(load);
        return MACHINE_LANGUAGE_OK;
    }
    if (declaration->leaves)
    {
        frame->pc = block->end;
        if (block->kind == MACHINE_LANGUAGE_RUN_IF)
            frame->else_rule = MACHINE_LANGUAGE_ELSE_SKIPPED;
        --evaluation->block_count;
        return machine_language_close_table(load);
    }
    frame->pc = (declaration->body != 0u) ? declaration->body : declaration->after;
    return (declaration->body != 0u) ? machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_TERMS, frame->statement,
                                                             declaration->after, declaration->scope) :
                                       MACHINE_LANGUAGE_OK;
}

/**
 * @brief Gives the statement of a table's code that waits for a term what the term gave, or that it
 *        failed, in the order the statement asked.
 */
static void machine_language_record_answer(MachineLanguageLoad_t *load, bool known, uint64_t integer)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageDeclaration_t *declaration = &load->declaration;
    const uint32_t answer = declaration->answered;

    if (answer == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLARATION_ANSWERS)
        return;
    declaration->known[answer] = known;
    declaration->values[answer] = integer;
    declaration->afters[answer] = machine_language_frame(&load->evaluation)->pc;
    ++declaration->answered;
}

/**
 * @brief Gives the statement of a table's code that waits for a term the Integer the term gave, and lets
 *        the statement go on.
 */
static MachineLanguageStatus_t machine_language_answer(MachineLanguageLoad_t *load)
{
    MachineLanguageValue_t value = machine_language_pop(&load->evaluation);
    uint64_t integer = 0u;
    const MachineLanguageStatus_t status = machine_language_integer_of(load, &value, &integer);

    machine_language_release(load, &value);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    machine_language_record_answer(load, true, integer);
    return machine_language_declare(load, false);
}

/**
 * @brief Runs a statement of a table's code at load in the interpreter, counted as a run.
 */
static MachineLanguageStatus_t machine_language_run_table_statement(MachineLanguageLoad_t *load)
{
    ++load->evaluation.runs;
    return machine_language_term(load, MACHINE_LANGUAGE_OPERAND(NONE));
}

/**
 * @brief Runs an Else of a table's code at load as the If before it says (§3.6): its body runs after a
 *        false predicate, is declared without running after a failed one, and is stepped over otherwise.
 *        One whose PkgLength does not decode is handed to the load, which counts the fault.
 */
static MachineLanguageStatus_t machine_language_table_else(MachineLanguageLoad_t *load, uint32_t after, uint32_t limit)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    const uint32_t start = frame->pc;
    const uint8_t rule = (start == frame->else_offset) ? frame->else_rule : MACHINE_LANGUAGE_ELSE_SKIPPED;
    uint32_t end = 0u;

    if (rule == MACHINE_LANGUAGE_ELSE_NOT_RUN)
        return machine_language_declare(load, true);
    if (advanced_configuration_and_power_interface_machine_language_read_package_end(frame->code, after, limit, &end,
                                                                                     &after) != MACHINE_LANGUAGE_OK)
        return machine_language_declare(load, false);
    frame->pc = (rule == MACHINE_LANGUAGE_ELSE_RUN) ? after : end;
    return (rule == MACHINE_LANGUAGE_ELSE_RUN) ?
               machine_language_open_construct(load, MACHINE_LANGUAGE_RUN_TERMS, start, end) :
               MACHINE_LANGUAGE_OK;
}

/**
 * @brief Runs a Break or a Continue of a table's code at load: leaves the innermost While around it, past
 *        its end or back to its predicate, a back-edge counted against the table's budget, past its end too
 *        once the budget is spent; one outside any While does nothing.
 */
static MachineLanguageStatus_t machine_language_leave_table_loop(MachineLanguageLoad_t *load, bool again)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    for (uint32_t index = evaluation->block_count; index > frame->block_base; --index)
    {
        const MachineLanguageBlock_t block = evaluation->blocks[index - 1u];

        if (block.kind != MACHINE_LANGUAGE_RUN_WHILE)
            continue;
        evaluation->operator_count = block.pending_base;
        evaluation->block_count = index - 1u;
        frame->pc = again ? block.start : block.end;
        return again ? machine_language_take_table_back_edge(evaluation, &block) : MACHINE_LANGUAGE_OK;
    }
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Takes the next statement of a table's code at load (§3.6): the interpreter runs code, an If's
 *        or a While's predicate included, Break and Continue, and an Else as the If before it says; it
 *        hands the load any other statement, and one whose opcode or PkgLength does not decode.
 */
static MachineLanguageStatus_t machine_language_table_statement(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const uint32_t start = frame->pc;
    const uint32_t limit = machine_language_block(evaluation)->end;
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;
    uint32_t end = 0u;

    frame->statement = start;
    load->declaration.asks = 0u;
    if (advanced_configuration_and_power_interface_machine_language_starts_name(frame->code[start]))
        return machine_language_run_table_statement(load);
    if (advanced_configuration_and_power_interface_machine_language_read_opcode(frame->code, start, limit, &opcode,
                                                                                &after) != MACHINE_LANGUAGE_OK)
        return machine_language_declare(load, false);
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(IF):
    case MACHINE_LANGUAGE_OPCODE(WHILE):
        return (advanced_configuration_and_power_interface_machine_language_read_package_end(
                    frame->code, after, limit, &end, &after) == MACHINE_LANGUAGE_OK) ?
                   machine_language_run_table_statement(load) :
                   machine_language_declare(load, false);
    case MACHINE_LANGUAGE_OPCODE(ELSE): return machine_language_table_else(load, after, limit);
    case MACHINE_LANGUAGE_OPCODE(BREAK):
    case MACHINE_LANGUAGE_OPCODE(CONTINUE):
        frame->pc = after;
        return machine_language_leave_table_loop(load, opcode == MACHINE_LANGUAGE_OPCODE(CONTINUE));
    default:
        return advanced_configuration_and_power_interface_machine_language_runs_at_table_level(opcode) ?
                   machine_language_run_table_statement(load) :
                   machine_language_declare(load, false);
    }
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
    if (!machine_language_holds_operands(evaluation, &operator_))
        return MACHINE_LANGUAGE_STATUS(OPERANDS_MISSING);
    switch (operator_.opcode)
    {
    case MACHINE_LANGUAGE_CALL: return machine_language_enter_method(load, operator_.node, operator_.operand_count);
    case MACHINE_LANGUAGE_ATTACH: return machine_language_attach_result(load, &operator_);
    case MACHINE_LANGUAGE_ANSWER: return machine_language_answer(load);
    case MACHINE_LANGUAGE_LOADED: {
        MachineLanguageValue_t target = {0};

        if (operator_.operand_count != 0u)
            target = machine_language_pop(evaluation);
        return machine_language_loaded(load, operator_.node, (operator_.operand_count != 0u) ? &target : NULL);
    }
    case MACHINE_LANGUAGE_REGISTER: {
        MachineLanguageValue_t value = machine_language_pop(evaluation);

        machine_language_release(load, &value);
        return MACHINE_LANGUAGE_OK;
    }
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
    case MACHINE_LANGUAGE_OPCODE(OPERATION_REGION): return machine_language_operation_region(load, &operator_);
    case MACHINE_LANGUAGE_OPCODE(FIELD):
    case MACHINE_LANGUAGE_OPCODE(INDEX_FIELD):
    case MACHINE_LANGUAGE_OPCODE(BANK_FIELD): return machine_language_field_list(load, &operator_);
    case MACHINE_LANGUAGE_OPCODE(NOTIFY): return machine_language_notify(load);
    case MACHINE_LANGUAGE_OPCODE(SLEEP):
    case MACHINE_LANGUAGE_OPCODE(STALL): return machine_language_wait(load, operator_.opcode);
    case MACHINE_LANGUAGE_OPCODE(TIMER):
        return machine_language_deliver(
            load, machine_language_integer(advanced_configuration_and_power_interface_machine_language_timer(load)));
    case MACHINE_LANGUAGE_OPCODE(ACQUIRE): return machine_language_acquire(load);
    case MACHINE_LANGUAGE_OPCODE(RELEASE):
    case MACHINE_LANGUAGE_OPCODE(SIGNAL): return machine_language_synchronize(load, operator_.opcode);
    case MACHINE_LANGUAGE_OPCODE(LOAD): return machine_language_load_operator(load);
    case MACHINE_LANGUAGE_OPCODE(LOAD_TABLE): return machine_language_load_table_operator(load);
    default: return machine_language_compute(load, operator_.opcode);
    }
}

/**
 * @brief The Integer an operand of a record gives, read as the loop reads an operator's operand: a
 *        constant, what an earlier operator of the record gave, or a local or an argument that holds an
 *        Integer, masked to the width.
 *
 * @return false when the local or the argument holds no Integer, which the loop would convert or count.
 */
static bool machine_language_record_operand(const MachineLanguageLoad_t *load, const MachineLanguageFrame_t *frame,
                                            const MachineLanguageRecordOperand_t *operand, const uint64_t *results,
                                            uint64_t *out_integer)
{
    const MachineLanguageValue_t *slot = NULL;

    switch ((AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperandKind_t) operand->kind)
    {
    case MACHINE_LANGUAGE_RECORD_OPERAND(CONSTANT):
        *out_integer = load->predecoding.constants[operand->index];
        return true;
    case MACHINE_LANGUAGE_RECORD_OPERAND(RESULT): *out_integer = results[operand->slot]; return true;
    case MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL): slot = &frame->locals[operand->slot]; break;
    case MACHINE_LANGUAGE_RECORD_OPERAND(ARGUMENT): slot = &frame->arguments[operand->slot]; break;
    case MACHINE_LANGUAGE_RECORD_OPERAND(NAME): slot = &load->predecoding.name_values[operand->slot]; break;
    case MACHINE_LANGUAGE_RECORD_OPERAND(NONE): return false;
    }
    if (slot->kind != MACHINE_LANGUAGE_VALUE(INTEGER))
        return false;
    *out_integer = machine_language_mask(load, slot->payload);
    return true;
}

/**
 * @brief Runs the operators of a record's expression in the loop's order, then gives the Integer of its
 *        source: an If's or a While's predicate, or what an operator stores; changes nothing.
 *
 * @return false, @p out_reason saying why, when an operand holds no Integer or an operator would fail.
 */
static bool machine_language_record_integer(const MachineLanguageLoad_t *load, const MachineLanguageFrame_t *frame,
                                            const MachineLanguageRecord_t *record, uint64_t *out_integer,
                                            uint8_t *out_reason)
{
    uint64_t results[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NODES_PER_RECORD];

    *out_reason = MACHINE_LANGUAGE_DECLINED(OPERAND);
    for (uint32_t index = 0u; index < record->node_count; ++index)
    {
        const MachineLanguageRecordNode_t *node = &load->predecoding.nodes[record->first_node + index];
        uint64_t left = 0u;
        uint64_t right = 0u;

        if (!machine_language_record_operand(load, frame, &node->operands[0], results, &left))
            return false;
        if (node->opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT))
        {
            results[index] = machine_language_truth(load, left == 0u).payload;
            continue;
        }
        if (!machine_language_record_operand(load, frame, &node->operands[1], results, &right))
            return false;
        if (node->opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL) ||
            node->opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS) ||
            node->opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_GREATER))
            results[index] = machine_language_truth(load, machine_language_integers_compare(
                                                              node->opcode, left, right) != (node->negated != 0u))
                                 .payload;
        else if (machine_language_binary(load, node->opcode, left, right, &results[index]) != MACHINE_LANGUAGE_OK)
        {
            *out_reason = MACHINE_LANGUAGE_DECLINED(OPERATION);
            return false;
        }
    }
    return machine_language_record_operand(load, frame, &record->source, results, out_integer);
}

/**
 * @brief The local or the argument a record's operand names.
 */
static MachineLanguageValue_t *machine_language_record_local_or_argument(MachineLanguageFrame_t *frame,
                                                                         const MachineLanguageRecordOperand_t *operand)
{
    return (operand->kind == MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL)) ? &frame->locals[operand->slot] :
                                                                       &frame->arguments[operand->slot];
}

/**
 * @brief The value a Store's record stores: a local, an argument or a name read that holds an Integer, whole and
 *        unmasked, as a Store copies a value (§5.2); otherwise the Integer its expression gives.
 *
 * @return false, @p out_reason saying why, when an operand holds no Integer or an operator would fail.
 */
static bool machine_language_record_value(const MachineLanguageLoad_t *load, MachineLanguageFrame_t *frame,
                                          const MachineLanguageRecord_t *record, MachineLanguageValue_t *out_value,
                                          uint8_t *out_reason)
{
    uint64_t integer = 0u;

    if (record->source.kind == MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL) ||
        record->source.kind == MACHINE_LANGUAGE_RECORD_OPERAND(ARGUMENT) ||
        record->source.kind == MACHINE_LANGUAGE_RECORD_OPERAND(NAME))
    {
        const MachineLanguageValue_t *slot = (record->source.kind == MACHINE_LANGUAGE_RECORD_OPERAND(NAME)) ?
                                                 &load->predecoding.name_values[record->source.slot] :
                                                 machine_language_record_local_or_argument(frame, &record->source);

        *out_reason = MACHINE_LANGUAGE_DECLINED(OPERAND);
        if (slot->kind != MACHINE_LANGUAGE_VALUE(INTEGER))
            return false;
        *out_value = *slot;
        return true;
    }
    if (!machine_language_record_integer(load, frame, record, &integer, out_reason))
        return false;
    *out_value = machine_language_integer(integer);
    return true;
}

/**
 * @brief Runs a Store, an operator with a target, an Increment or a Decrement from its record: its value,
 *        then written to the local or the argument it targets as machine_language_store writes an
 *        Integer there, in place; an operator whose target is NullName writes nothing.
 *
 * @return false, @p out_reason saying why, nothing changed, when an operand holds no Integer, an operator
 *         would fail, or the target holds what is not overwritten in place, or no Integer to step.
 */
static bool machine_language_record_write(const MachineLanguageLoad_t *load, MachineLanguageFrame_t *frame,
                                          const MachineLanguageRecord_t *record, uint8_t *out_reason)
{
    MachineLanguageValue_t value;
    MachineLanguageValue_t *slot = NULL;

    if (record->shape == MACHINE_LANGUAGE_RECORD(STORE))
    {
        if (!machine_language_record_value(load, frame, record, &value, out_reason))
            return false;
        if (record->target.kind == MACHINE_LANGUAGE_RECORD_OPERAND(NONE))
            return true;
        slot = machine_language_record_local_or_argument(frame, &record->target);
    }
    else
    {
        slot = machine_language_record_local_or_argument(frame, &record->target);
        *out_reason = MACHINE_LANGUAGE_DECLINED(TARGET);
        if (slot->kind != MACHINE_LANGUAGE_VALUE(INTEGER))
            return false;
        value = machine_language_integer(machine_language_stepped(load, machine_language_mask(load, slot->payload),
                                                                  record->shape == MACHINE_LANGUAGE_RECORD(INCREMENT)));
    }
    *out_reason = MACHINE_LANGUAGE_DECLINED(TARGET);
    if (!machine_language_overwrites_in_place(slot))
        return false;
    machine_language_overwrite_in_place(slot, &value);
    return true;
}

/**
 * @brief Runs a statement that writes from its record, the frame then past it.
 *
 * @return false, the reason counted, when the record hands the statement back, nothing changed.
 */
static bool machine_language_record_statement(MachineLanguageLoad_t *load, MachineLanguageFrame_t *frame,
                                              const MachineLanguageRecord_t *record)
{
    uint8_t reason = MACHINE_LANGUAGE_DECLINED(OPERAND);

    if (!machine_language_record_write(load, frame, record, &reason))
    {
        ++load->predecoding.declined[reason];
        return false;
    }
    frame->pc = record->after;
    return true;
}

/**
 * @brief Whether a name a record reads names what the record reads as an Integer: an object that is no method and no
 *        field unit and holds an Integer, or a field unit whose read gives an Integer and whose PCI_Config regions
 *        wait for no attach.
 */
static bool machine_language_names_an_integer(const MachineLanguageLoad_t *load, uint32_t index)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    if (node->type == MACHINE_LANGUAGE_NODE(FIELD_UNIT))
        return advanced_configuration_and_power_interface_machine_language_field_reads_integer(load, index) &&
               advanced_configuration_and_power_interface_machine_language_peripheral_component_interconnect_unattached_region(
                   load, index) == 0u;
    return node->type != MACHINE_LANGUAGE_NODE(METHOD) && node->value.kind == MACHINE_LANGUAGE_VALUE(INTEGER);
}

/**
 * @brief The node a name of a record names from @p scope: the one the name's entry checked, while the namespace's
 *        generation and the scope hold; otherwise resolved again, counted nowhere, and kept in the entry.
 *
 * @return 0 when the name names nothing.
 */
static uint32_t machine_language_record_name_node(MachineLanguageLoad_t *load, const uint8_t *code,
                                                  MachineLanguageRecordName_t *entry, uint32_t scope)
{
    MachineLanguageNamespace_t *space = &load->space;
    const uint32_t lookups = space->lookups;
    const uint32_t probes = space->probes;
    uint32_t index = 0u;

    if (entry->node != 0u && entry->generation == space->generation && entry->scope == scope)
        return entry->node;

    const MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_namespace_resolve(space, scope, code, &entry->name,
                                                                                      &index);

    space->lookups = lookups;
    space->probes = probes;
    entry->node = (status == MACHINE_LANGUAGE_OK) ? index : 0u;
    entry->generation = space->generation;
    entry->scope = scope;
    return entry->node;
}

/**
 * @brief Whether a local or an argument a record's operand reads holds an Integer, as every operand of a record that
 *        reads names must before the record reads one; any other operand passes.
 */
static bool machine_language_record_operand_holds(const MachineLanguageFrame_t *frame,
                                                  const MachineLanguageRecordOperand_t *operand)
{
    if (operand->kind != MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL) &&
        operand->kind != MACHINE_LANGUAGE_RECORD_OPERAND(ARGUMENT))
        return true;
    return ((operand->kind == MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL)) ? frame->locals[operand->slot] :
                                                                        frame->arguments[operand->slot])
               .kind == MACHINE_LANGUAGE_VALUE(INTEGER);
}

/**
 * @brief Whether a record that reads names runs its statement once it has read them (§3.6), checked before any
 *        effect: each name names an object read as an Integer, each local or argument operand holds an Integer, a
 *        Store's target takes an Integer in place, and an If or a While finds room for its block.
 *
 * @return false, @p out_reason saying why, when the loop must run the statement.
 */
static bool machine_language_record_names_hold(MachineLanguageLoad_t *load, const MachineLanguageFrame_t *frame,
                                               const MachineLanguageRecord_t *record, uint32_t scope,
                                               uint8_t *out_reason)
{
    MachineLanguagePredecoding_t *cache = &load->predecoding;

    *out_reason = MACHINE_LANGUAGE_DECLINED(NAME);
    for (uint32_t name = 0u; name < record->name_count; ++name)
    {
        const uint32_t index =
            machine_language_record_name_node(load, frame->code, &cache->names[record->first_name + name], scope);

        if (index == 0u || !machine_language_names_an_integer(load, index))
            return false;
    }
    *out_reason = MACHINE_LANGUAGE_DECLINED(OPERAND);
    if (!machine_language_record_operand_holds(frame, &record->source))
        return false;
    for (uint32_t index = 0u; index < record->node_count; ++index)
    {
        const MachineLanguageRecordNode_t *node = &cache->nodes[record->first_node + index];

        if (!machine_language_record_operand_holds(frame, &node->operands[0]) ||
            !machine_language_record_operand_holds(frame, &node->operands[1]))
            return false;
    }
    *out_reason = MACHINE_LANGUAGE_DECLINED(TARGET);
    if (record->target.kind == MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL) ||
        record->target.kind == MACHINE_LANGUAGE_RECORD_OPERAND(ARGUMENT))
    {
        const MachineLanguageValue_t *slot = (record->target.kind == MACHINE_LANGUAGE_RECORD_OPERAND(LOCAL)) ?
                                                 &frame->locals[record->target.slot] :
                                                 &frame->arguments[record->target.slot];

        if (!machine_language_overwrites_in_place(slot))
            return false;
    }
    *out_reason = MACHINE_LANGUAGE_DECLINED(ROOM);
    return (record->shape != MACHINE_LANGUAGE_RECORD(WHILE) && record->shape != MACHINE_LANGUAGE_RECORD(IF)) ||
           load->evaluation.block_count !=
               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS;
}

/**
 * @brief Reads the names of a record in the loop's order (§3.6): each resolved as the loop resolves it, counted, the
 *        node the record checked read whatever that resolution found, a difference counted as a stale name; the
 *        record's first effect, after which nothing hands its statement back.
 *
 * @return OK; or what a read failed with, the frame's pc past the name and the values peak at what the loop held
 *         before reading it, as the loop leaves them.
 */
static MachineLanguageStatus_t machine_language_record_read_names(MachineLanguageLoad_t *load,
                                                                  MachineLanguageFrame_t *frame,
                                                                  const MachineLanguageRecord_t *record, uint32_t scope)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguagePredecoding_t *cache = &load->predecoding;

    for (uint32_t name = 0u; name < record->name_count; ++name)
    {
        const MachineLanguageRecordName_t *entry = &cache->names[record->first_name + name];
        uint32_t index = 0u;

        if (advanced_configuration_and_power_interface_machine_language_namespace_resolve(
                &load->space, scope, frame->code, &entry->name, &index) != MACHINE_LANGUAGE_OK ||
            index != entry->node)
            ++cache->stale_names;

        const MachineLanguageStatus_t status = machine_language_read_node(load, entry->node, &cache->name_values[name]);

        if (status != MACHINE_LANGUAGE_OK)
        {
            frame->pc = entry->after;
            if (evaluation->value_count + entry->values_before > evaluation->values_peak)
                evaluation->values_peak = evaluation->value_count + entry->values_before;
            return status;
        }
    }
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief The record of the statement at the running frame's pc, found by @p index when it holds that
 *        statement's key, as the recent record of its offset otherwise, searched in the cache's hash when it is
 *        not, built when the cache holds none: NULL, the reason counted, when a record built under another end of
 *        the construct is found, or when the cache is full, which empties it and freezes the frame's loop (§6.1).
 */
static MachineLanguageRecord_t *machine_language_record_at(MachineLanguageLoad_t *load, MachineLanguageFrame_t *frame,
                                                           const MachineLanguageBlock_t *block, uint16_t *index,
                                                           uint16_t previous)
{
    MachineLanguagePredecoding_t *cache = &load->predecoding;

    if (*index == 0u || cache->records[*index - 1u].pc != frame->pc ||
        cache->records[*index - 1u].table != frame->table)
    {
        *index =
            advanced_configuration_and_power_interface_machine_language_record_recent(cache, frame->table, frame->pc);
        if (*index == 0u)
        {
            ++cache->searches;
            *index =
                advanced_configuration_and_power_interface_machine_language_record_find(cache, frame->table, frame->pc);
        }
        if (*index == 0u && !(cache->frozen_serial == frame->serial && cache->frozen_start == frame->looping_start))
            *index = advanced_configuration_and_power_interface_machine_language_record_build(
                cache, frame->code, frame->table, frame->pc, block->end, load->integer_bits);
        if (*index == 0u)
        {
            if (!(cache->frozen_serial == frame->serial && cache->frozen_start == frame->looping_start))
            {
                advanced_configuration_and_power_interface_machine_language_predecoding_flush(cache,
                                                                                              load->integer_bits);
                cache->frozen_serial = frame->serial;
                cache->frozen_start = frame->looping_start;
            }
            ++cache->declined[MACHINE_LANGUAGE_DECLINED(FULL)];
            return NULL;
        }
        if (previous != 0u)
            advanced_configuration_and_power_interface_machine_language_record_link(cache, previous, *index);
    }
    if (cache->records[*index - 1u].limit == block->end)
        return &cache->records[*index - 1u];
    ++cache->declined[MACHINE_LANGUAGE_DECLINED(LIMIT)];
    return NULL;
}

/**
 * @brief Runs an If or a While from its record: its predicate, then its body opened or stepped over, an
 *        If's Else entered as the loop enters it.
 *
 * @return false, the reason counted, when the record hands the statement back, nothing changed.
 */
static bool machine_language_record_branch(MachineLanguageLoad_t *load, MachineLanguageFrame_t *frame,
                                           const MachineLanguageRecord_t *record, MachineLanguageStatus_t *out_status)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    uint64_t truth = 0u;
    uint8_t reason = MACHINE_LANGUAGE_DECLINED(OPERAND);

    if (!machine_language_record_integer(load, frame, record, &truth, &reason) ||
        (truth != 0u &&
         evaluation->block_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS))
    {
        ++load->predecoding.declined[(truth != 0u) ? MACHINE_LANGUAGE_DECLINED(ROOM) : reason];
        return false;
    }
    if (truth != 0u)
    {
        *out_status = machine_language_open(
            evaluation,
            (record->shape == MACHINE_LANGUAGE_RECORD(WHILE)) ? MACHINE_LANGUAGE_RUN_WHILE : MACHINE_LANGUAGE_RUN_IF,
            record->pc, record->end, machine_language_block(evaluation)->scope);
        frame->pc = record->after;
        return true;
    }
    frame->pc = record->end;
    if (record->shape == MACHINE_LANGUAGE_RECORD(IF))
        *out_status = machine_language_enter_else(evaluation);
    return true;
}

/**
 * @brief The record of a While's body when the While runs as a tight loop now (§3.6): the While was linked to a body
 *        that makes it one, and the body fits the room the stacks have left, which changes from one call to the
 *        next, as a block would, were the body handed back; NULL otherwise.
 */
static const MachineLanguageRecord_t *machine_language_tight_body(const MachineLanguageEvaluation_t *evaluation,
                                                                  const MachineLanguagePredecoding_t *cache,
                                                                  const MachineLanguageRecord_t *loop,
                                                                  uint32_t value_room, uint32_t operator_room)
{
    const MachineLanguageRecord_t *body = &cache->records[loop->next - 1u];

    if (loop->tight == 0u || body->values_peak > value_room || body->operators_peak > operator_room ||
        evaluation->block_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS)
        return NULL;
    return body;
}

/**
 * @brief Runs a While that machine_language_tight_body accepts, turn after turn as its two records would
 *        run: the predicate, the body, the back-edge, no block opened; the frame is left as the records
 *        would leave it, the While's block open when its body is handed back.
 *
 * @param load          The load.
 * @param frame         The running frame, at the While.
 * @param loop          The While's record.
 * @param body          Its body's record.
 * @param inout_values  The most values the records run so far held, raised by the two records.
 * @param inout_ran     Set when a record ran.
 * @param out_status    Receives what a back-edge past the evaluation's budget failed with.
 * @return true when the predicate failed, the frame past the While, or a back-edge failed; false, the
 *         reason counted, when a record handed its statement back.
 */
static bool machine_language_run_tight_while(MachineLanguageLoad_t *load, MachineLanguageFrame_t *frame,
                                             const MachineLanguageRecord_t *loop, const MachineLanguageRecord_t *body,
                                             uint32_t *inout_values, bool *inout_ran,
                                             MachineLanguageStatus_t *out_status)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguagePredecoding_t *cache = &load->predecoding;
    const MachineLanguageRecordNode_t *node = &cache->nodes[loop->first_node];
    const bool negated = node->negated != 0u;
    uint8_t reason = MACHINE_LANGUAGE_DECLINED(OPERAND);
    uint32_t turns = 0u;
    uint32_t ran = 0u;
    uint64_t left = 0u;
    uint64_t right = 0u;
    bool goes_on = true;

    for (;;)
    {
        if (!machine_language_record_operand(load, frame, &node->operands[0], NULL, &left) ||
            !machine_language_record_operand(load, frame, &node->operands[1], NULL, &right))
        {
            ++cache->declined[MACHINE_LANGUAGE_DECLINED(OPERAND)];
            frame->pc = loop->pc;
            goes_on = false;
            break;
        }
        ++ran;
        if (machine_language_integers_compare(node->opcode, left, right) == negated)
        {
            frame->pc = loop->end;
            break;
        }
        if (!machine_language_record_write(load, frame, body, &reason))
        {
            ++cache->declined[reason];
            *out_status = machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_WHILE, loop->pc, loop->end,
                                                machine_language_block(evaluation)->scope);
            frame->pc = loop->after;
            goes_on = false;
            break;
        }
        ++turns;
        *out_status = machine_language_count_iteration(evaluation);
        if (*out_status != MACHINE_LANGUAGE_OK)
        {
            frame->pc = loop->pc;
            break;
        }
    }
    cache->records_run += ran + turns;
    cache->tight_turns += turns;
    *inout_ran = *inout_ran || ran != 0u;
    if (ran != 0u && loop->values_peak > *inout_values)
        *inout_values = loop->values_peak;
    if (turns != 0u && body->values_peak > *inout_values)
        *inout_values = body->values_peak;
    return goes_on;
}

/**
 * @brief Runs statements of the running frame from records while its pc lies in the While of the frame
 *        known to loop (§3.6), which ends the watch of a loop: each record runs its statement as the loop would, or
 *        hands it back, nothing changed; a construct's end the records reach is closed as the loop closes it, a
 *        While's taking its back-edge.
 *
 * @param load    The load.
 * @param first   The record of the statement at the frame's pc, plus 1, when known; 0 otherwise.
 * @param out_ran Receives whether a statement or a back-edge ran.
 * @return OK; or what a back-edge past the evaluation's budget, or a function of the loop a record
 *         calls, failed with.
 */
static MachineLanguageStatus_t machine_language_run_records(MachineLanguageLoad_t *load, uint16_t first, bool *out_ran)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    MachineLanguagePredecoding_t *cache = &load->predecoding;
    const uint32_t value_room =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES - evaluation->value_count;
    const uint32_t operator_room =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS -
        evaluation->operator_count;
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;
    uint32_t values_peak = 0u;
    uint16_t index = first;
    uint16_t previous = 0u;

    *out_ran = false;
    cache->watched_serial = 0u;
    if (cache->integer_bits != load->integer_bits)
    {
        advanced_configuration_and_power_interface_machine_language_predecoding_flush(cache, load->integer_bits);
        index = 0u;
    }
    while (status == MACHINE_LANGUAGE_OK)
    {
        const MachineLanguageBlock_t block = *machine_language_block(evaluation);

        if (frame->pc >= block.end)
        {
            if (block.kind == MACHINE_LANGUAGE_RUN_WHILE && block.start >= frame->looping_start &&
                block.end <= frame->looping_end)
            {
                --evaluation->block_count;
                status = machine_language_take_back_edge(evaluation, &block);
                *out_ran = true;
            }
            else if (block.kind == MACHINE_LANGUAGE_RUN_IF || block.kind == MACHINE_LANGUAGE_RUN_TERMS)
            {
                --evaluation->block_count;
                status = (block.kind == MACHINE_LANGUAGE_RUN_IF) ? machine_language_skip_else(evaluation) :
                                                                   MACHINE_LANGUAGE_OK;
            }
            else
                break;
            previous = 0u;
            continue;
        }
        if (frame->pc - frame->looping_start >= frame->looping_end - frame->looping_start)
            break;

        const MachineLanguageRecord_t *record = machine_language_record_at(load, frame, &block, &index, previous);
        const MachineLanguageRecord_t *body = NULL;

        if (record == NULL)
            break;
        if (record->shape == MACHINE_LANGUAGE_RECORD(GENERAL) || record->values_peak > value_room ||
            record->operators_peak > operator_room)
        {
            ++cache->declined[(record->shape == MACHINE_LANGUAGE_RECORD(GENERAL)) ? MACHINE_LANGUAGE_DECLINED(SHAPE) :
                                                                                    MACHINE_LANGUAGE_DECLINED(ROOM)];
            break;
        }
        if (record->name_count != 0u)
        {
            uint8_t reason = MACHINE_LANGUAGE_DECLINED(NAME);

            if (!machine_language_record_names_hold(load, frame, record, block.scope, &reason))
            {
                ++cache->declined[reason];
                break;
            }
            status = machine_language_record_read_names(load, frame, record, block.scope);
            if (status != MACHINE_LANGUAGE_OK)
                break;
        }
        if (record->tight != 0u)
            body = machine_language_tight_body(evaluation, cache, record, value_room, operator_room);
        if (body != NULL)
        {
            if (!machine_language_run_tight_while(load, frame, record, body, &values_peak, out_ran, &status))
                break;
            previous = 0u;
            index = 0u;
            continue;
        }
        if (record->shape == MACHINE_LANGUAGE_RECORD(BREAK))
        {
            frame->pc = record->after;
            status = machine_language_leave_loop(evaluation, false);
        }
        else if (advanced_configuration_and_power_interface_machine_language_record_writes(record->shape))
        {
            if (!machine_language_record_statement(load, frame, record))
                break;
        }
        else if (!machine_language_record_branch(load, frame, record, &status))
            break;
        ++cache->records_run;
        values_peak = (record->values_peak > values_peak) ? record->values_peak : values_peak;
        *out_ran = true;
        previous = (frame->pc == record->after) ? index : 0u;
        index = (frame->pc == record->after) ? record->next : 0u;
    }
    if (evaluation->value_count + values_peak > evaluation->values_peak)
        evaluation->values_peak = evaluation->value_count + values_peak;
    return status;
}

/**
 * @brief Whether the statement at the running frame's pc goes to the loop before any record runs (§3.6): the
 *        recent record of its offset holds it and runs nothing, whatever the end of the construct around it, the
 *        statement counted as handed back.
 *
 * @param cache     The cache.
 * @param frame     The running frame.
 * @param out_index Receives the statement's recent record, plus 1; 0 when the slot holds another, or none.
 * @return Whether the statement was handed back.
 */
static bool machine_language_records_hand_back(MachineLanguagePredecoding_t *cache, const MachineLanguageFrame_t *frame,
                                               uint16_t *out_index)
{
    *out_index =
        advanced_configuration_and_power_interface_machine_language_record_recent(cache, frame->table, frame->pc);
    if (*out_index == 0u || cache->records[*out_index - 1u].shape != MACHINE_LANGUAGE_RECORD(GENERAL))
        return false;
    ++cache->declined[MACHINE_LANGUAGE_DECLINED(SHAPE)];
    return true;
}

/**
 * @brief Watches a While whose record hands it back at once, at a back-edge (§3.6): when it is the frame's loop and
 *        every statement of the turn that ends went to the loop at once, each going on to the next, so will every turn
 *        after it, and the frame's loop is left empty at the While's start, which its turns then ask no record from;
 *        otherwise the turn that starts is watched, when the While is the frame's loop.
 */
static void machine_language_watch_loop(MachineLanguagePredecoding_t *cache, MachineLanguageFrame_t *frame,
                                        const MachineLanguageBlock_t *block)
{
    if (frame->looping_start != block->start || frame->looping_end != block->end)
    {
        cache->watched_serial = 0u;
        return;
    }
    if (cache->watched_serial == frame->serial && cache->watched_start == block->start)
    {
        frame->looping_end = block->start;
        ++cache->handed_back_loops;
        return;
    }
    cache->watched_serial = frame->serial;
    cache->watched_start = block->start;
}

/**
 * @brief Takes the back-edge of a method's While that @p block was, then runs the While's statements from
 *        records (§3.6): a While whose statements all go to the loop, the frame's loop left empty at its start, has
 *        its operator pushed as the loop pushes it; otherwise the While becomes the frame's loop unless the frame's
 *        loop holds it already; when the record of the While hands it back at once, the While is watched and its
 *        operator pushed, and the statement the records stopped at later, unless a construct's end, is decoded at once.
 */
static MachineLanguageStatus_t machine_language_loop_again(MachineLanguageLoad_t *load,
                                                           const MachineLanguageBlock_t *block)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    uint16_t first = 0u;
    bool ran = false;
    MachineLanguageStatus_t status = machine_language_take_back_edge(evaluation, block);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (frame->looping_start == block->start && frame->looping_end == block->start)
        return machine_language_push_while(evaluation, block);
    if (frame->looping_end == 0u || block->start < frame->looping_start || block->end > frame->looping_end)
    {
        frame->looping_start = block->start;
        frame->looping_end = block->end;
    }
    if (machine_language_records_hand_back(&load->predecoding, frame, &first))
    {
        machine_language_watch_loop(&load->predecoding, frame, block);
        return machine_language_push_while(evaluation, block);
    }
    status = machine_language_run_records(load, first, &ran);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (!ran)
        return machine_language_push_while(evaluation, block);
    return (frame->pc >= machine_language_block(evaluation)->end) ?
               MACHINE_LANGUAGE_OK :
               machine_language_term(load, MACHINE_LANGUAGE_OPERAND(NONE));
}

/**
 * @brief Runs the statement at the running frame's pc: a table's code hands it to the load; a method's
 *        runs from records while its pc lies in the frame's loop, unless the recent record of its offset hands it
 *        back at once, which ends the watch of a loop when the statement may not go on to the next, and the statement
 *        they stop at, unless a construct's end, is decoded at once, so that a statement a record hands back is never
 *        offered to the records twice.
 */
static MachineLanguageStatus_t machine_language_statement(MachineLanguageLoad_t *load)
{
    MachineLanguageFrame_t *frame = machine_language_frame(&load->evaluation);
    uint16_t first = 0u;
    bool ran = false;

    if (frame->declares)
        return machine_language_table_statement(load);
    if (frame->pc - frame->looping_start < frame->looping_end - frame->looping_start && load->predecodes_loops)
    {
        if (machine_language_records_hand_back(&load->predecoding, frame, &first))
        {
            if (load->predecoding.records[first - 1u].goes_on == 0u)
                load->predecoding.watched_serial = 0u;
            return machine_language_term(load, MACHINE_LANGUAGE_OPERAND(NONE));
        }

        const MachineLanguageStatus_t status = machine_language_run_records(load, first, &ran);

        if (status != MACHINE_LANGUAGE_OK || frame->pc >= machine_language_block(&load->evaluation)->end)
            return status;
    }
    return machine_language_term(load, MACHINE_LANGUAGE_OPERAND(NONE));
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
    return machine_language_statement(load);
}

/**
 * @brief Whether a status says the evaluation ran out of something (§6.1), which no attach may hide.
 */
static bool machine_language_exhausted(MachineLanguageStatus_t status)
{
    switch (status)
    {
    case MACHINE_LANGUAGE_STATUS(OUT_OF_MEMORY):
    case MACHINE_LANGUAGE_STATUS(LIMIT_NAMESPACE_NODES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_BLOCKS):
    case MACHINE_LANGUAGE_STATUS(LIMIT_PENDING_OPERATORS):
    case MACHINE_LANGUAGE_STATUS(LIMIT_TEMPORARY_NODES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_PACKAGE_ELEMENTS):
    case MACHINE_LANGUAGE_STATUS(LIMIT_BUFFER_BYTES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_OBJECT_RUNS):
    case MACHINE_LANGUAGE_STATUS(LIMIT_VALUES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_LOOP_ITERATIONS):
    case MACHINE_LANGUAGE_STATUS(LIMIT_FIELD_ACCESSES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_TIME_REQUESTED):
    case MACHINE_LANGUAGE_STATUS(LIMIT_HELD_MUTEXES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_TABLES):
    case MACHINE_LANGUAGE_STATUS(LIMIT_TABLE_BYTES): return true;
    default: return false;
    }
}

/**
 * @brief Goes on past a failure of the code of the table frame @p base runs at load, counted (§3.6): every
 *        frame above it dropped, the statement that failed is handed to the load, which steps over it,
 *        or declares an If's or a While's TermList without running its code when its predicate failed, the
 *        Else after such an If then declared the same way, or stepped over when a fault cut that TermList
 *        short; a term a statement waits for gives it nothing, and the statement goes on.
 */
static MachineLanguageStatus_t machine_language_table_failure(MachineLanguageLoad_t *load, uint32_t base,
                                                              MachineLanguageStatus_t status)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = &evaluation->frames[base];
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    machine_language_count_failure(evaluation, status);
    while (evaluation->frame_count > base + 1u)
        machine_language_drop_frame(load);
    while (evaluation->value_count > frame->value_base)
        machine_language_release(load, &evaluation->values[--evaluation->value_count]);
    evaluation->operator_count = machine_language_block(evaluation)->pending_base;
    if (load->declaration.asks != 0u)
    {
        machine_language_record_answer(load, false, 0u);
        return machine_language_declare(load, false);
    }
    (void) advanced_configuration_and_power_interface_machine_language_read_opcode(
        frame->code, frame->statement, machine_language_block(evaluation)->end, &opcode, &after);
    status = machine_language_declare(load, opcode == MACHINE_LANGUAGE_OPCODE(IF) ||
                                                opcode == MACHINE_LANGUAGE_OPCODE(WHILE));
    if (opcode == MACHINE_LANGUAGE_OPCODE(IF))
    {
        frame->else_offset = load->declaration.after;
        frame->else_rule = load->declaration.closed ? MACHINE_LANGUAGE_ELSE_NOT_RUN : MACHINE_LANGUAGE_ELSE_SKIPPED;
    }
    return status;
}

/**
 * @brief Hands the internal operator in @p slot an absent value for the internal frame above it that failed: the
 *        frames above that operator are dropped, the failure counted.
 */
static MachineLanguageStatus_t machine_language_fail_internal(MachineLanguageLoad_t *load, uint32_t slot)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageValue_t absent = {0};

    while (machine_language_frame(evaluation)->operator_base >= slot)
        machine_language_drop_frame(load);
    evaluation->operator_count = slot;
    if (evaluation->operators[slot - 1u].opcode == MACHINE_LANGUAGE_REGISTER)
        ++load->regions.pci_registration_failures;
    else
        ++load->regions.internal_failures;
    return machine_language_push(load, &absent);
}

/**
 * @brief Goes on past a failure met above frame @p base, at the innermost of what takes it: an internal operator,
 *        unless the evaluation ran out of something, then, when the operator cannot take it either, or there is
 *        none, the frame of a table's code, which goes on past its statement (§3.6), or drops itself, counted,
 *        when it cannot; with neither, the failure stands.
 */
static MachineLanguageStatus_t machine_language_take_failure(MachineLanguageLoad_t *load, uint32_t base,
                                                             MachineLanguageStatus_t status)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    uint32_t table = evaluation->frame_count;

    while (table > base && !evaluation->frames[table - 1u].declares)
        --table;

    const uint32_t floor = evaluation->frames[(table > base) ? table - 1u : base].operator_base;
    uint32_t slot = evaluation->operator_count;

    while (slot > floor && evaluation->operators[slot - 1u].opcode != MACHINE_LANGUAGE_ATTACH &&
           evaluation->operators[slot - 1u].opcode != MACHINE_LANGUAGE_REGISTER)
        --slot;
    if (slot > floor && !machine_language_exhausted(status))
        status = machine_language_fail_internal(load, slot);
    if (status == MACHINE_LANGUAGE_OK || table == base)
        return status;
    status = machine_language_table_failure(load, table - 1u, status);
    if (status == MACHINE_LANGUAGE_OK)
        return status;
    while (evaluation->frame_count >= table)
        machine_language_drop_frame(load);
    machine_language_count_failure(evaluation, status);
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Takes one step of the frames above @p base; a failure inside an internal frame, unless it is
 *        a limit, drops the frames above its attach or registration operator and hands it an
 *        Uninitialized value, so that the object it evaluates is absent, as ACPICA and uACPI take a
 *        failed _ADR, _SEG or _BBN, and a failed _REG is dropped (§3.6), counted; one inside the frame of a
 *        table's code, whatever loop runs it, goes on past that code's statement.
 */
static MachineLanguageStatus_t machine_language_step_above(MachineLanguageLoad_t *load, uint32_t base)
{
    const MachineLanguageStatus_t status = machine_language_step(load);

    return (status == MACHINE_LANGUAGE_OK) ? status : machine_language_take_failure(load, base, status);
}

/**
 * @brief Pushes the frame a host-initiated evaluation runs from: no code of its own, in the scope of
 *        the root, given the table of the method it calls, or 0 for one without a body, `\_OSI` included.
 */
static MachineLanguageStatus_t machine_language_push_host_frame(MachineLanguageLoad_t *load, uint16_t table)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (evaluation->frame_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_FRAMES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);

    MachineLanguageFrame_t *frame = &evaluation->frames[evaluation->frame_count++];

    memset(frame, 0, sizeof(*frame));
    frame->code = (table != 0u) ? (const uint8_t *) load->tables[table - 1u].header : NULL;
    frame->scope = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
    frame->value_base = (uint16_t) evaluation->value_count;
    evaluation->value_floor = evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = table;
    frame->serial = ++evaluation->frame_serial;
    return machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_TABLE, 0u, 0u,
                                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT);
}

/**
 * @brief Attaches the PCI_Config regions a unit the host reads reaches: each method an attach waits
 *        for runs in an internal frame above a host frame, until the attach needs no more; one whose call
 *        fails, as one without a body does, a limit apart, is an absent object, as it is when AML reads the
 *        unit (§3.6).
 */
static MachineLanguageStatus_t machine_language_attach_for_host(MachineLanguageLoad_t *load, uint32_t unit)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t base = evaluation->frame_count;
    uint32_t region = 0u;
    uint32_t method = 0u;
    MachineLanguageStatus_t status = machine_language_attach_step(load, unit, &region, &method);

    while (status == MACHINE_LANGUAGE_OK && method != 0u)
    {
        const uint32_t slot = evaluation->operator_count;

        status = machine_language_push_host_frame(
            load, advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, method)
                      ->value.handle);
        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_push_attach(load, region, method, 0u, 0u);
        if (status != MACHINE_LANGUAGE_OK && evaluation->operator_count > slot)
            status = machine_language_take_failure(load, base, status);
        while (status == MACHINE_LANGUAGE_OK && evaluation->operator_count > slot)
            status = machine_language_step_above(load, base);
        while (evaluation->frame_count > base)
            machine_language_drop_frame(load);
        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_attach_step(load, unit, &region, &method);
    }
    return status;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_run_table_term(
    MachineLanguageLoad_t *load, uint32_t table, uint32_t scope, uint32_t start, uint32_t limit)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t base = evaluation->frame_count;
    MachineLanguageStatus_t status = machine_language_push_table_frame(load, table, scope, start, limit);

    if (status != MACHINE_LANGUAGE_OK)
    {
        machine_language_count_failure(evaluation, status);
        return status;
    }
    while (status == MACHINE_LANGUAGE_OK && evaluation->frame_count > base)
        status = machine_language_step_above(load, base);
    while (evaluation->frame_count > base)
        machine_language_drop_frame(load);
    return MACHINE_LANGUAGE_OK;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_evaluate_node(
    MachineLanguageLoad_t *load, uint32_t index, const MachineLanguageValue_t *arguments, uint32_t argument_count,
    MachineLanguageValue_t *out_result)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t base = evaluation->frame_count;
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    memset(out_result, 0, sizeof(*out_result));
    status = machine_language_reach(load, index);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (node->type == MACHINE_LANGUAGE_NODE(FIELD_UNIT))
    {
        advanced_configuration_and_power_interface_machine_language_begin_evaluation(load);
        status = machine_language_attach_for_host(load, index);
        advanced_configuration_and_power_interface_machine_language_end_evaluation(load);
        if (status != MACHINE_LANGUAGE_OK)
            return status;
    }
    if (node->type != MACHINE_LANGUAGE_NODE(METHOD))
        return machine_language_read_node(load, index, out_result);
    if (node->value.handle == 0u && index != load->space.operating_system_interface)
        return MACHINE_LANGUAGE_STATUS(METHOD_WITHOUT_BODY);
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

    advanced_configuration_and_power_interface_machine_language_begin_evaluation(load);
    status = machine_language_push_host_frame(load, node->value.handle);
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
        status = machine_language_step_above(load, base);
    if (status == MACHINE_LANGUAGE_OK && !machine_language_frame_holds(evaluation, 1u))
        status = MACHINE_LANGUAGE_STATUS(OPERANDS_MISSING);
    if (status == MACHINE_LANGUAGE_OK)
        *out_result = machine_language_pop(evaluation);
    while (evaluation->frame_count > base + 1u)
        machine_language_drop_frame(load);
    machine_language_drop_frame(load);
    advanced_configuration_and_power_interface_machine_language_end_evaluation(load);
    return status;
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_evaluate(
    MachineLanguageLoad_t *load, const char *path, const MachineLanguageValue_t *arguments, uint32_t argument_count,
    MachineLanguageValue_t *out_result)
{
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
    return advanced_configuration_and_power_interface_machine_language_evaluate_node(load, index, arguments,
                                                                                     argument_count, out_result);
}

MachineLanguageStatus_t advanced_configuration_and_power_interface_machine_language_package_element(
    MachineLanguageLoad_t *load, const MachineLanguageValue_t *package, uint32_t element,
    MachineLanguageValue_t *out_value)
{
    uint32_t node = 0u;

    memset(out_value, 0, sizeof(*out_value));
    if (package->kind != MACHINE_LANGUAGE_VALUE(PACKAGE))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (element >= machine_language_object(load, package)->length)
        return MACHINE_LANGUAGE_STATUS(INDEX_OUT_OF_RANGE);

    const MachineLanguageValue_t *slot =
        (const MachineLanguageValue_t *) advanced_configuration_and_power_interface_machine_language_object_payload(
            &load->heap, package->handle) +
        element;

    if (slot->kind == MACHINE_LANGUAGE_VALUE(UNINITIALIZED))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    if (slot->kind != MACHINE_LANGUAGE_VALUE(NAME_REFERENCE))
    {
        *out_value = *slot;
        machine_language_retain(load, out_value);
        return MACHINE_LANGUAGE_OK;
    }

    const MachineLanguageStatus_t status = machine_language_resolve_element_name(load, slot, &node);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node)->type ==
        MACHINE_LANGUAGE_NODE(METHOD))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    return advanced_configuration_and_power_interface_machine_language_evaluate_node(load, node, NULL, 0u, out_value);
}
