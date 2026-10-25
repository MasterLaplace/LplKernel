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
#define MACHINE_LANGUAGE_TARGET        0x90u /**< A value that says where a result goes; auxiliary is its kind. */
#define MACHINE_LANGUAGE_ELSE_BYTE     0xA1u /**< The byte of Else, which only an If reads. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t MachineLanguageStatus_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t MachineLanguageOperandKind_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t MachineLanguageLoad_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t MachineLanguageEvaluation_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageFrame_t MachineLanguageFrame_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t MachineLanguageBlock_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t MachineLanguagePendingOperator_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t MachineLanguageValue_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t MachineLanguageNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t MachineLanguageNameString_t;

/**
 * @brief Where a target value sends a result.
 */
typedef enum MachineLanguageTargetKind {
    MACHINE_LANGUAGE_TARGET_NONE,     /**< NullName: the result is dropped. */
    MACHINE_LANGUAGE_TARGET_LOCAL,    /**< A LocalX: handle is X. */
    MACHINE_LANGUAGE_TARGET_ARGUMENT, /**< An ArgX: handle is X. */
    MACHINE_LANGUAGE_TARGET_NODE,     /**< A named object: handle is its node. */
    MACHINE_LANGUAGE_TARGET_DEBUG,    /**< The Debug object. */
} MachineLanguageTargetKind_t;

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
    MachineLanguageValue_t value;

    memset(&value, 0, sizeof(value));
    value.kind = MACHINE_LANGUAGE_VALUE(INTEGER);
    value.payload = integer;
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
    MachineLanguageValue_t value;

    memset(&value, 0, sizeof(value));
    value.kind = MACHINE_LANGUAGE_TARGET;
    value.auxiliary = (uint16_t) kind;
    value.handle = handle;
    return value;
}

/**
 * @brief Pushes a value on the value stack.
 */
static MachineLanguageStatus_t machine_language_push(MachineLanguageEvaluation_t *evaluation,
                                                     const MachineLanguageValue_t *value)
{
    if (evaluation->value_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_VALUES)
        return MACHINE_LANGUAGE_STATUS(LIMIT_VALUES);
    evaluation->values[evaluation->value_count++] = *value;
    if (evaluation->value_count > evaluation->values_peak)
        evaluation->values_peak = evaluation->value_count;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief Pops the value on top of the value stack.
 */
static MachineLanguageValue_t machine_language_pop(MachineLanguageEvaluation_t *evaluation)
{
    return evaluation->values[--evaluation->value_count];
}

/**
 * @brief Hands a value to the operator waiting for it, or drops it when its term was a statement.
 */
static MachineLanguageStatus_t machine_language_deliver(MachineLanguageEvaluation_t *evaluation,
                                                        MachineLanguageValue_t value)
{
    return (machine_language_active(evaluation) != NULL) ? machine_language_push(evaluation, &value) :
                                                           MACHINE_LANGUAGE_OK;
}

/**
 * @brief The integer an operand holds.
 */
static MachineLanguageStatus_t machine_language_integer_of(const MachineLanguageValue_t *value, uint64_t *out_integer)
{
    /* TODO(#539): a String or a Buffer converts to an Integer (§5.2). */
    if (value->kind != MACHINE_LANGUAGE_VALUE(INTEGER))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    *out_integer = value->payload;
    return MACHINE_LANGUAGE_OK;
}

/**
 * @brief A local or an argument read as an operand: an uninitialized one reads as Integer 0, counted
 *        (§5.2).
 */
static MachineLanguageValue_t machine_language_read_slot(MachineLanguageEvaluation_t *evaluation,
                                                         const MachineLanguageValue_t *slot)
{
    if (slot->kind != MACHINE_LANGUAGE_VALUE(UNINITIALIZED))
        return *slot;
    ++evaluation->uninitialized_operands;
    return machine_language_integer(0u);
}

/**
 * @brief The value a named object gives where a value is read: its data object; a field waits for
 *        its slice.
 */
static MachineLanguageStatus_t machine_language_read_node(MachineLanguageLoad_t *load, uint32_t index,
                                                          MachineLanguageValue_t *out_value)
{
    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

    switch (node->value.kind)
    {
    case MACHINE_LANGUAGE_VALUE(INTEGER):
    case MACHINE_LANGUAGE_VALUE(STRING):
    case MACHINE_LANGUAGE_VALUE(BUFFER):
    case MACHINE_LANGUAGE_VALUE(PACKAGE): *out_value = node->value; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_VALUE(FIELD_UNIT):
    case MACHINE_LANGUAGE_VALUE(BUFFER_FIELD): return MACHINE_LANGUAGE_STATUS(FIELD_ACCESS_DEFERRED);
    default:
        return (node->type == MACHINE_LANGUAGE_NODE(EXTERNAL)) ? MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND) :
                                                                 MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    }
}

/**
 * @brief The value a target holds, for an operator that updates it in place.
 */
static MachineLanguageStatus_t machine_language_read_target(MachineLanguageLoad_t *load,
                                                            const MachineLanguageValue_t *target,
                                                            MachineLanguageValue_t *out_value)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    if (target->kind != MACHINE_LANGUAGE_TARGET)
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    switch (target->auxiliary)
    {
    case MACHINE_LANGUAGE_TARGET_LOCAL:
        *out_value = machine_language_read_slot(evaluation, &frame->locals[target->handle]);
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_TARGET_ARGUMENT:
        *out_value = machine_language_read_slot(evaluation, &frame->arguments[target->handle]);
        return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_TARGET_NODE: return machine_language_read_node(load, target->handle, out_value);
    default: return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    }
}

/**
 * @brief Stores a value to a named object: an Integer takes the integer, masked; a Name not built
 *        yet takes the value and its type; a field waits for its slice.
 */
static MachineLanguageStatus_t machine_language_store_node(MachineLanguageLoad_t *load, uint32_t index,
                                                           const MachineLanguageValue_t *value)
{
    MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);
    uint64_t integer = 0u;

    if (node->value.kind == MACHINE_LANGUAGE_VALUE(FIELD_UNIT) ||
        node->value.kind == MACHINE_LANGUAGE_VALUE(BUFFER_FIELD))
        return MACHINE_LANGUAGE_STATUS(FIELD_ACCESS_DEFERRED);
    if (node->type == MACHINE_LANGUAGE_NODE(UNINITIALIZED))
    {
        node->value = *value;
        node->type = advanced_configuration_and_power_interface_machine_language_node_type_of_value(value->kind);
        return MACHINE_LANGUAGE_OK;
    }
    /* TODO(#539): a store to a String, a Buffer or a Package converts to its type (§5.2). */
    if (node->type != MACHINE_LANGUAGE_NODE(INTEGER))
        return MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);

    const MachineLanguageStatus_t status = machine_language_integer_of(value, &integer);

    if (status == MACHINE_LANGUAGE_OK)
        node->value.payload = machine_language_mask(load, integer);
    return status;
}

/**
 * @brief Stores a result where a target says: NullName drops it, a local or an argument is
 *        overwritten, Debug counts it, a named object takes it; a value that is no target, such
 *        as a constant, takes nothing (§5.2).
 */
static MachineLanguageStatus_t machine_language_store(MachineLanguageLoad_t *load, const MachineLanguageValue_t *value,
                                                      const MachineLanguageValue_t *target)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    if (target->kind != MACHINE_LANGUAGE_TARGET)
        return MACHINE_LANGUAGE_OK;
    switch (target->auxiliary)
    {
    case MACHINE_LANGUAGE_TARGET_LOCAL: frame->locals[target->handle] = *value; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_TARGET_ARGUMENT: frame->arguments[target->handle] = *value; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_TARGET_DEBUG: ++evaluation->debug_stores; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_TARGET_NODE: return machine_language_store_node(load, target->handle, value);
    default: return MACHINE_LANGUAGE_OK;
    }
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
 * @brief Pushes a frame for a method whose arguments are the top @p argument_count values.
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
    if (evaluation->frame_count > evaluation->frames_peak)
        evaluation->frames_peak = evaluation->frame_count;
    return machine_language_open(evaluation, MACHINE_LANGUAGE_RUN_METHOD, body, frame->end, method);
}

/**
 * @brief Pops the running frame: removes the nodes it created, the last first, and drops what its
 *        stacks hold.
 */
static void machine_language_drop_frame(MachineLanguageLoad_t *load)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguageFrame_t *frame = machine_language_frame(evaluation);

    while (load->temporary_count > frame->temporary_base)
        advanced_configuration_and_power_interface_machine_language_namespace_remove(
            &load->space, load->temporaries[--load->temporary_count]);
    evaluation->value_count = frame->value_base;
    evaluation->operator_count = frame->operator_base;
    evaluation->block_count = frame->block_base;
    --evaluation->frame_count;
}

/**
 * @brief Ends the running method and gives its caller the result: @p value, or for a method that
 *        ended without Return while its caller uses the result, Integer 0, counted (§5.2).
 */
static MachineLanguageStatus_t machine_language_return(MachineLanguageLoad_t *load, const MachineLanguageValue_t *value)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    machine_language_drop_frame(load);
    if (machine_language_active(evaluation) == NULL)
        return MACHINE_LANGUAGE_OK;
    if (value != NULL)
        return machine_language_push(evaluation, value);
    ++evaluation->implicit_returns;
    return machine_language_deliver(evaluation, machine_language_integer(0u));
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
 * @brief Whether the interpreter runs an opcode that pushes an operator: control flow, Store, the
 *        integer operators the needs-path closure reaches (§4.1), and Name in a method.
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
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS): return true;
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
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
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
    return machine_language_deliver(evaluation, machine_language_integer(machine_language_mask(load, integer)));
}

/**
 * @brief Runs a NameString read where @p kind is expected: a method is called, unless the operand is
 *        a SimpleName; a target position names the object; anywhere else the object gives its value.
 */
static MachineLanguageStatus_t machine_language_name_term(MachineLanguageLoad_t *load,
                                                          MachineLanguageOperandKind_t kind)
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
        return MACHINE_LANGUAGE_STATUS(NAME_NOT_FOUND);

    const MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

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
        return machine_language_deliver(evaluation, machine_language_target(MACHINE_LANGUAGE_TARGET_NODE, index));
    status = machine_language_read_node(load, index, &value);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(evaluation, value) : status;
}

/**
 * @brief Runs one term of the running frame where @p kind is expected: a statement of a TermList
 *        when @p kind is NONE, where a statement is refused otherwise.
 */
static MachineLanguageStatus_t machine_language_term(MachineLanguageLoad_t *load, MachineLanguageOperandKind_t kind)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const uint32_t start = frame->pc;
    const uint32_t limit = machine_language_limit(evaluation);
    const bool target = kind == MACHINE_LANGUAGE_OPERAND(SUPER_NAME) || kind == MACHINE_LANGUAGE_OPERAND(TARGET) ||
                        kind == MACHINE_LANGUAGE_OPERAND(SIMPLE_NAME);
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (start >= limit)
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    if (frame->code[start] == 0x00u && target)
    {
        frame->pc = start + 1u;
        return machine_language_deliver(evaluation, machine_language_target(MACHINE_LANGUAGE_TARGET_NONE, 0u));
    }
    if (advanced_configuration_and_power_interface_machine_language_starts_name(frame->code[start]))
        return machine_language_name_term(load, kind);

    const MachineLanguageStatus_t status = advanced_configuration_and_power_interface_machine_language_read_opcode(
        frame->code, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    if (kind != MACHINE_LANGUAGE_OPERAND(NONE) &&
        (advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags &
         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_STATEMENT) != 0u)
        return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
    if (opcode >= MACHINE_LANGUAGE_OPCODE(LOCAL0) && opcode <= MACHINE_LANGUAGE_OPCODE(LOCAL7))
    {
        const uint32_t slot = opcode - MACHINE_LANGUAGE_OPCODE(LOCAL0);

        frame->pc = after;
        return machine_language_deliver(evaluation, target ?
                                                        machine_language_target(MACHINE_LANGUAGE_TARGET_LOCAL, slot) :
                                                        machine_language_read_slot(evaluation, &frame->locals[slot]));
    }
    if (opcode >= MACHINE_LANGUAGE_OPCODE(ARGUMENT0) && opcode <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6))
    {
        const uint32_t slot = opcode - MACHINE_LANGUAGE_OPCODE(ARGUMENT0);

        frame->pc = after;
        return machine_language_deliver(evaluation,
                                        target ? machine_language_target(MACHINE_LANGUAGE_TARGET_ARGUMENT, slot) :
                                                 machine_language_read_slot(evaluation, &frame->arguments[slot]));
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
    case MACHINE_LANGUAGE_OPCODE(DEBUG):
        frame->pc = after;
        return target ?
                   machine_language_deliver(evaluation, machine_language_target(MACHINE_LANGUAGE_TARGET_DEBUG, 0u)) :
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
 * @brief Where a data object that starts at @p start ends: a constant's last byte, a String's NUL,
 *        a Buffer's or Package's PkgLength end.
 */
static MachineLanguageStatus_t machine_language_data_object_end(const uint8_t *code, uint32_t start, uint32_t limit,
                                                                uint32_t *out_end)
{
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;
    MachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_read_opcode(code, start, limit, &opcode, &after);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(ZERO):
    case MACHINE_LANGUAGE_OPCODE(ONE):
    case MACHINE_LANGUAGE_OPCODE(ONES): *out_end = after; return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPCODE(BYTE_PREFIX): *out_end = after + 1u; break;
    case MACHINE_LANGUAGE_OPCODE(WORD_PREFIX): *out_end = after + 2u; break;
    case MACHINE_LANGUAGE_OPCODE(DOUBLE_WORD_PREFIX): *out_end = after + 4u; break;
    case MACHINE_LANGUAGE_OPCODE(QUAD_WORD_PREFIX): *out_end = after + 8u; break;
    case MACHINE_LANGUAGE_OPCODE(STRING_PREFIX):
        for (uint32_t offset = after; offset < limit; ++offset)
        {
            if (code[offset] == 0x00u)
            {
                *out_end = offset + 1u;
                return MACHINE_LANGUAGE_OK;
            }
        }
        return MACHINE_LANGUAGE_STATUS(TRUNCATED);
    case MACHINE_LANGUAGE_OPCODE(BUFFER):
    case MACHINE_LANGUAGE_OPCODE(PACKAGE):
    case MACHINE_LANGUAGE_OPCODE(VARIABLE_PACKAGE):
        status = advanced_configuration_and_power_interface_machine_language_read_package_end(code, after, limit,
                                                                                              out_end, &after);
        return status;
    default: return MACHINE_LANGUAGE_STATUS(INVALID_OPCODE);
    }
    return (*out_end <= limit) ? MACHINE_LANGUAGE_OK : MACHINE_LANGUAGE_STATUS(TRUNCATED);
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
 * @brief Gives the node a Name in a method declared its data object, which needs no interpreter.
 */
static MachineLanguageStatus_t machine_language_name_object(MachineLanguageLoad_t *load,
                                                            const MachineLanguagePendingOperator_t *active)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    MachineLanguageFrame_t *frame = machine_language_frame(evaluation);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageSource_t source = {
        .bytes = frame->code,
        .scope = machine_language_block(evaluation)->scope,
        .table = frame->table,
        .integer_bits = (load->integer_bits != 0u) ? load->integer_bits : 64u,
    };
    MachineLanguageValue_t value;
    uint32_t end = 0u;
    bool constant = false;
    MachineLanguageStatus_t status = machine_language_data_object_end(frame->code, frame->pc, active->end, &end);

    if (status == MACHINE_LANGUAGE_OK)
        status = advanced_configuration_and_power_interface_machine_language_evaluate_data(
            &load->heap, &load->space, &source, frame->pc, end, &value, &constant);
    if (status != MACHINE_LANGUAGE_OK)
        return status;
    /* TODO(#539): a Buffer or a Package sized at run time. */
    if (!constant)
        return MACHINE_LANGUAGE_STATUS(UNIMPLEMENTED_OPERATOR);

    MachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, active->node);

    node->value = value;
    node->type = advanced_configuration_and_power_interface_machine_language_node_type_of_value(value.kind);
    frame->pc = end;
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
    const MachineLanguageValue_t predicate = machine_language_pop(evaluation);
    uint64_t truth = 0u;
    const MachineLanguageStatus_t status = machine_language_integer_of(&predicate, &truth);

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
 * @brief Runs the next operand of the active operator.
 */
static MachineLanguageStatus_t machine_language_operand(MachineLanguageLoad_t *load,
                                                        MachineLanguagePendingOperator_t *active)
{
    const MachineLanguageOperandKind_t kind =
        (active->opcode >= MACHINE_LANGUAGE_CALL) ?
            MACHINE_LANGUAGE_OPERAND(TERM_ARGUMENT) :
            advanced_configuration_and_power_interface_machine_language_operand_kind(
                advanced_configuration_and_power_interface_machine_language_descriptor(active->opcode),
                active->operands_done);

    ++active->operands_done;
    if (kind == MACHINE_LANGUAGE_OPERAND(TERM_LIST))
        return machine_language_branch(load, active);
    if (kind == MACHINE_LANGUAGE_OPERAND(NAME_STRING))
        return machine_language_declare_name(load, active);
    if (active->opcode == MACHINE_LANGUAGE_OPCODE(NAME))
        return machine_language_name_object(load, active);
    return machine_language_term(load, kind);
}

/**
 * @brief Computes a binary integer operator on two operands masked to the width: a shift by the
 *        width or more gives 0, a modulo by zero fails (§5.2).
 */
static MachineLanguageStatus_t machine_language_binary(const MachineLanguageLoad_t *load, uint32_t opcode,
                                                       uint64_t left, uint64_t right, uint64_t *out_result)
{
    const uint64_t width = (load->integer_bits == 32u) ? 32u : 64u;

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
 * @brief Pops the top @p count values as integers, the deepest first.
 */
static MachineLanguageStatus_t machine_language_pop_integers(MachineLanguageEvaluation_t *evaluation, uint32_t count,
                                                             uint64_t *out_integers)
{
    MachineLanguageStatus_t status = MACHINE_LANGUAGE_OK;

    for (uint32_t index = count; index > 0u; --index)
    {
        const MachineLanguageValue_t value = machine_language_pop(evaluation);

        if (status == MACHINE_LANGUAGE_OK)
            status = machine_language_integer_of(&value, &out_integers[index - 1u]);
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
    const MachineLanguageValue_t quotient_target = machine_language_pop(evaluation);
    const MachineLanguageValue_t remainder_target = machine_language_pop(evaluation);
    uint64_t operands[2];
    MachineLanguageStatus_t status = machine_language_pop_integers(evaluation, 2u, operands);

    if (status != MACHINE_LANGUAGE_OK)
        return status;

    const uint64_t dividend = machine_language_mask(load, operands[0]);
    const uint64_t divisor = machine_language_mask(load, operands[1]);

    if (divisor == 0u)
        return MACHINE_LANGUAGE_STATUS(DIVIDE_BY_ZERO);

    const MachineLanguageValue_t remainder = machine_language_integer(dividend % divisor);
    const MachineLanguageValue_t quotient = machine_language_integer(dividend / divisor);

    status = machine_language_store(load, &remainder, &remainder_target);
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_store(load, &quotient, &quotient_target);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(evaluation, quotient) : status;
}

/**
 * @brief Runs Increment or Decrement on the object its SuperName names, in place.
 */
static MachineLanguageStatus_t machine_language_step_target(MachineLanguageLoad_t *load, bool up)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const MachineLanguageValue_t target = machine_language_pop(evaluation);
    MachineLanguageValue_t current;
    uint64_t integer = 0u;
    MachineLanguageStatus_t status = machine_language_read_target(load, &target, &current);

    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_integer_of(&current, &integer);
    if (status != MACHINE_LANGUAGE_OK)
        return status;

    const MachineLanguageValue_t result =
        machine_language_integer(machine_language_mask(load, up ? integer + 1u : integer - 1u));

    status = machine_language_store(load, &result, &target);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(evaluation, result) : status;
}

/**
 * @brief Runs an operator that computes an integer, the logical ones included, and stores it to its
 *        target when it has one.
 */
static MachineLanguageStatus_t machine_language_compute(MachineLanguageLoad_t *load, uint32_t opcode)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const uint32_t operand_count =
        advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->operand_count;
    const bool stores = advanced_configuration_and_power_interface_machine_language_operand_kind(
                            advanced_configuration_and_power_interface_machine_language_descriptor(opcode),
                            operand_count - 1u) == MACHINE_LANGUAGE_OPERAND(TARGET);
    const MachineLanguageValue_t target =
        stores ? machine_language_pop(evaluation) : machine_language_target(MACHINE_LANGUAGE_TARGET_NONE, 0u);
    uint64_t operands[2] = {0u, 0u};
    uint64_t integer = 0u;
    MachineLanguageValue_t result;
    MachineLanguageStatus_t status =
        machine_language_pop_integers(evaluation, operand_count - (stores ? 1u : 0u), operands);

    if (status != MACHINE_LANGUAGE_OK)
        return status;
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(NOT):
        result = machine_language_integer(machine_language_mask(load, ~operands[0]));
        break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT): result = machine_language_truth(load, operands[0] == 0u); break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_AND):
        result = machine_language_truth(load, operands[0] != 0u && operands[1] != 0u);
        break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_OR):
        result = machine_language_truth(load, operands[0] != 0u || operands[1] != 0u);
        break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL):
        result = machine_language_truth(load, operands[0] == operands[1]);
        break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_GREATER):
        result = machine_language_truth(load, operands[0] > operands[1]);
        break;
    case MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS): result = machine_language_truth(load, operands[0] < operands[1]); break;
    default:
        status = machine_language_binary(load, opcode, operands[0], operands[1], &integer);
        result = machine_language_integer(integer);
        break;
    }
    if (status == MACHINE_LANGUAGE_OK)
        status = machine_language_store(load, &result, &target);
    return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(evaluation, result) : status;
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
    case MACHINE_LANGUAGE_OPCODE(NAME): return MACHINE_LANGUAGE_OK;
    case MACHINE_LANGUAGE_OPCODE(RETURN): {
        const MachineLanguageValue_t value = machine_language_pop(evaluation);

        return machine_language_return(load, &value);
    }
    case MACHINE_LANGUAGE_OPCODE(STORE): {
        const MachineLanguageValue_t target = machine_language_pop(evaluation);
        const MachineLanguageValue_t value = machine_language_pop(evaluation);
        const MachineLanguageStatus_t status = machine_language_store(load, &value, &target);

        return (status == MACHINE_LANGUAGE_OK) ? machine_language_deliver(evaluation, value) : status;
    }
    case MACHINE_LANGUAGE_OPCODE(INCREMENT): return machine_language_step_target(load, true);
    case MACHINE_LANGUAGE_OPCODE(DECREMENT): return machine_language_step_target(load, false);
    case MACHINE_LANGUAGE_OPCODE(DIVIDE): return machine_language_divide(load);
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
        return MACHINE_LANGUAGE_STATUS(LIMIT_FRAMES);

    MachineLanguageFrame_t *frame = &evaluation->frames[evaluation->frame_count++];

    if (evaluation->table_locals_owner != table + 1u)
        memset(evaluation->table_locals, 0, sizeof(evaluation->table_locals));
    frame->code = (const uint8_t *) load->tables[table].header;
    frame->pc = start;
    frame->end = limit;
    frame->method = 0u;
    frame->scope = scope;
    memset(frame->arguments, 0, sizeof(frame->arguments));
    memcpy(frame->locals, evaluation->table_locals, sizeof(frame->locals));
    frame->value_base = (uint16_t) evaluation->value_count;
    frame->operator_base = (uint16_t) evaluation->operator_count;
    frame->block_base = (uint16_t) evaluation->block_count;
    frame->temporary_base = (uint16_t) load->temporary_count;
    frame->table = (uint16_t) (table + 1u);
    frame->argument_count = 0u;
    frame->reserved = 0u;
    if (evaluation->frame_count > evaluation->frames_peak)
        evaluation->frames_peak = evaluation->frame_count;
    evaluation->iterations = 0u;
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
                     machine_language_integer_of(&evaluation->values[evaluation->value_count - 1u], out_integer) :
                     MACHINE_LANGUAGE_STATUS(TYPE_MISMATCH);
    *out_after = frame->pc;
    while (evaluation->frame_count > base + 1u)
        machine_language_drop_frame(load);
    memcpy(evaluation->table_locals, frame->locals, sizeof(evaluation->table_locals));
    evaluation->table_locals_owner = table + 1u;
    machine_language_drop_frame(load);
    if (status != MACHINE_LANGUAGE_OK)
        advanced_configuration_and_power_interface_machine_language_count_failure(load, status);
    return status;
}

void advanced_configuration_and_power_interface_machine_language_count_failure(MachineLanguageLoad_t *load,
                                                                               MachineLanguageStatus_t status)
{
    MachineLanguageEvaluation_t *evaluation = &load->evaluation;

    if (evaluation->failures++ == 0u)
        evaluation->first_failure = (uint8_t) status;
    ++evaluation->statuses[status];
}
