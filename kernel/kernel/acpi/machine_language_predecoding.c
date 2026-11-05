#include <kernel/acpi/machine_language_predecoding.h>

#include <stddef.h>

#define MACHINE_LANGUAGE_OK              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK
#define MACHINE_LANGUAGE_OPCODE(name)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define MACHINE_LANGUAGE_RECORD(name)    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_##name
#define MACHINE_LANGUAGE_OPERAND(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_##name
#define MACHINE_LANGUAGE_MAX_DEPTH       16u   /**< Operators nested in one expression a record decodes. */
#define MACHINE_LANGUAGE_NULL_NAME       0x00u /**< The byte of NullName, the only target an inner operator has. */
#define MACHINE_LANGUAGE_SLOT_MULTIPLIER 0x9E3779B1u /**< Mixes a statement's offset into its hash slot. */
#define MACHINE_LANGUAGE_TABLE_MULTIPLIER                                                                              \
    0x85EBCA6Bu                         /**< Mixes a table into the hash slot, by another factor than the offset's. */
#define MACHINE_LANGUAGE_SLOT_SHIFT 24u /**< Keeps the 8 high bits of the mix: one of 256 slots. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t MachineLanguagePredecoding_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecord_t MachineLanguageRecord_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t MachineLanguageRecordNode_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordOperand_t MachineLanguageRecordOperand_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordName_t MachineLanguageRecordName_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t MachineLanguageNameString_t;

_Static_assert(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT <= 256u,
               "a record keeps the opcode of its statement in a byte");

/**
 * @brief What a record's decoding works on: the cache, the record, the bytes it reads, and what the interpreter's
 *        loop holds for the statement as it would run it, values on its stack and operators pending, now and at most.
 */
typedef struct MachineLanguageRecordBuilder {
    MachineLanguagePredecoding_t *cache; /**< Where operators, constants and names go. */
    MachineLanguageRecord_t *record;     /**< The record being decoded. */
    const uint8_t *code;                 /**< The table's bytes. */
    uint8_t integer_bits;                /**< The load's integer width. */
    bool exhausted;                      /**< Whether the cache's operators, constants or names ran out. */
    uint32_t values;                     /**< Values the loop holds now. */
    uint32_t values_peak;                /**< Values it held at most. */
    uint32_t operators;                  /**< Operators it holds pending now. */
    uint32_t operators_peak;             /**< Operators it held pending at most. */
} MachineLanguageRecordBuilder_t;

/**
 * @brief The hash slot a statement's records start their search at.
 */
static uint32_t machine_language_record_slot(uint16_t table, uint32_t pc)
{
    return ((pc * MACHINE_LANGUAGE_SLOT_MULTIPLIER) ^ ((uint32_t) table * MACHINE_LANGUAGE_TABLE_MULTIPLIER)) >>
           MACHINE_LANGUAGE_SLOT_SHIFT;
}

/**
 * @brief Empties the hash and the records, word by word, the counters left alone.
 */
static void machine_language_records_empty(MachineLanguagePredecoding_t *cache)
{
    for (uint32_t word = 0u; word < sizeof(cache->slot_words) / sizeof(cache->slot_words[0]); ++word)
        cache->slot_words[word] = 0u;
    cache->record_count = 0u;
    cache->node_count = 0u;
    cache->constant_count = 0u;
    cache->name_count = 0u;
    cache->frozen_serial = 0u;
    cache->frozen_start = 0u;
}

/**
 * @brief The loop pushes @p count values for the statement.
 */
static void machine_language_record_push_values(MachineLanguageRecordBuilder_t *builder, uint32_t count)
{
    builder->values += count;
    if (builder->values > builder->values_peak)
        builder->values_peak = builder->values;
}

/**
 * @brief The loop pushes an operator for the statement.
 */
static void machine_language_record_push_operator(MachineLanguageRecordBuilder_t *builder)
{
    ++builder->operators;
    if (builder->operators > builder->operators_peak)
        builder->operators_peak = builder->operators;
}

/**
 * @brief The loop runs an operator of @p operands values, which leaves its result: one value.
 */
static void machine_language_record_run_operator(MachineLanguageRecordBuilder_t *builder, uint32_t operands)
{
    --builder->operators;
    builder->values -= operands;
    machine_language_record_push_values(builder, 1u);
}

/**
 * @brief Whether an opcode is a comparison the interpreter's loop runs on two Integers.
 */
static bool machine_language_record_compares(uint32_t opcode)
{
    return opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_EQUAL) || opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_LESS) ||
           opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_GREATER);
}

/**
 * @brief Whether an opcode is an operator on two integers with a target, which the interpreter's loop
 *        computes in one function.
 */
static bool machine_language_record_computes(uint32_t opcode)
{
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(ADD):
    case MACHINE_LANGUAGE_OPCODE(SUBTRACT):
    case MACHINE_LANGUAGE_OPCODE(MULTIPLY):
    case MACHINE_LANGUAGE_OPCODE(AND):
    case MACHINE_LANGUAGE_OPCODE(OR):
    case MACHINE_LANGUAGE_OPCODE(XOR):
    case MACHINE_LANGUAGE_OPCODE(SHIFT_LEFT):
    case MACHINE_LANGUAGE_OPCODE(SHIFT_RIGHT):
    case MACHINE_LANGUAGE_OPCODE(MOD): return true;
    default: return false;
    }
}

/**
 * @brief Decodes a local or an argument whose opcode was read.
 *
 * @return false when the opcode is neither.
 */
static bool machine_language_record_frame_slot(uint32_t opcode, MachineLanguageRecordOperand_t *out_operand)
{
    if (opcode >= MACHINE_LANGUAGE_OPCODE(LOCAL0) && opcode <= MACHINE_LANGUAGE_OPCODE(LOCAL7))
    {
        out_operand->kind = MACHINE_LANGUAGE_OPERAND(LOCAL);
        out_operand->slot = (uint8_t) (opcode - MACHINE_LANGUAGE_OPCODE(LOCAL0));
        return true;
    }
    if (opcode >= MACHINE_LANGUAGE_OPCODE(ARGUMENT0) && opcode <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6))
    {
        out_operand->kind = MACHINE_LANGUAGE_OPERAND(ARGUMENT);
        out_operand->slot = (uint8_t) (opcode - MACHINE_LANGUAGE_OPCODE(ARGUMENT0));
        return true;
    }
    return false;
}

/**
 * @brief Decodes a leaf of an expression whose opcode was read: a local, an argument, or a constant,
 *        masked to the width and kept among the cache's constants.
 *
 * @return false when the opcode is no leaf, a constant's bytes pass @p limit, or the constants are full,
 *         which the builder notes.
 */
static bool machine_language_record_leaf(MachineLanguageRecordBuilder_t *builder, uint32_t opcode, uint32_t *pc,
                                         uint32_t after, uint32_t limit, MachineLanguageRecordOperand_t *out_operand)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    uint64_t integer = 0u;

    if (machine_language_record_frame_slot(opcode, out_operand))
    {
        *pc = after;
        machine_language_record_push_values(builder, 1u);
        return true;
    }
    if (opcode != MACHINE_LANGUAGE_OPCODE(ZERO) && opcode != MACHINE_LANGUAGE_OPCODE(ONE) &&
        opcode != MACHINE_LANGUAGE_OPCODE(ONES) && opcode != MACHINE_LANGUAGE_OPCODE(BYTE_PREFIX) &&
        opcode != MACHINE_LANGUAGE_OPCODE(WORD_PREFIX) && opcode != MACHINE_LANGUAGE_OPCODE(DOUBLE_WORD_PREFIX) &&
        opcode != MACHINE_LANGUAGE_OPCODE(QUAD_WORD_PREFIX))
        return false;
    builder->exhausted =
        cache->constant_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_CONSTANTS;
    if (builder->exhausted || advanced_configuration_and_power_interface_machine_language_read_constant(
                                  builder->code, opcode, after, limit, &integer, pc) != MACHINE_LANGUAGE_OK)
        return false;
    cache->constants[cache->constant_count] =
        advanced_configuration_and_power_interface_machine_language_mask_to_width(builder->integer_bits, integer);
    out_operand->kind = MACHINE_LANGUAGE_OPERAND(CONSTANT);
    out_operand->index = (uint16_t) cache->constant_count++;
    machine_language_record_push_values(builder, 1u);
    return true;
}

/**
 * @brief Decodes a NameString at @p pc read as a value into a name of the record, kept among the cache's with where
 *        it ends, the values the loop held for the statement before reading it and, in a build that counts what the
 *        loop runs, the operators of the record it runs before it.
 *
 * @return false when the bytes hold no NameString, or the record or the cache holds no more names; the cache's, the
 *         builder notes.
 */
static bool machine_language_record_name(MachineLanguageRecordBuilder_t *builder, uint32_t *pc, uint32_t limit,
                                         MachineLanguageRecordOperand_t *out_operand)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    MachineLanguageRecord_t *record = builder->record;
    MachineLanguageNameString_t name;
    uint32_t after = 0u;

    builder->exhausted =
        cache->name_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NAMES;
    if (builder->exhausted ||
        record->name_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NAMES_PER_RECORD ||
        advanced_configuration_and_power_interface_machine_language_read_name_string(builder->code, *pc, limit, &name,
                                                                                     &after) != MACHINE_LANGUAGE_OK)
        return false;
    cache->names[cache->name_count] = (MachineLanguageRecordName_t){
        .name = name,
        .after = after,
        .values_before = (uint8_t) builder->values_peak,
        .nodes_before =
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_COUNTS_EXECUTED ? record->node_count : 0u,
    };
    if (record->name_count == 0u)
        record->first_name = (uint16_t) cache->name_count;
    out_operand->kind = MACHINE_LANGUAGE_OPERAND(NAME);
    out_operand->slot = record->name_count++;
    out_operand->index = (uint16_t) cache->name_count++;
    *pc = after;
    machine_language_record_push_values(builder, 1u);
    return true;
}

static bool machine_language_record_expression(MachineLanguageRecordBuilder_t *builder, uint32_t *pc, uint32_t limit,
                                               uint32_t level, MachineLanguageRecordOperand_t *out_operand);

/**
 * @brief Decodes the local or the argument a statement writes, at @p pc.
 *
 * @return false when @p pc holds anything else.
 */
static bool machine_language_record_target(const MachineLanguageRecordBuilder_t *builder, uint32_t *pc, uint32_t limit,
                                           MachineLanguageRecordOperand_t *out_operand)
{
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (advanced_configuration_and_power_interface_machine_language_read_opcode(builder->code, *pc, limit, &opcode,
                                                                                &after) != MACHINE_LANGUAGE_OK ||
        !machine_language_record_frame_slot(opcode, out_operand))
        return false;
    *pc = after;
    return true;
}

/**
 * @brief Appends an operator to the record, its result the expression's operand.
 *
 * @return false when the record or the cache holds no more operators; the cache's, the builder notes.
 */
static bool machine_language_record_append(MachineLanguageRecordBuilder_t *builder,
                                           const MachineLanguageRecordNode_t *node,
                                           MachineLanguageRecordOperand_t *out_operand)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    MachineLanguageRecord_t *record = builder->record;

    builder->exhausted =
        cache->node_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NODES;
    if (builder->exhausted ||
        record->node_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NODES_PER_RECORD)
        return false;
    cache->nodes[cache->node_count++] = *node;
    out_operand->kind = MACHINE_LANGUAGE_OPERAND(RESULT);
    out_operand->slot = record->node_count++;
    out_operand->index = 0u;
    return true;
}

/**
 * @brief Decodes LNot, whose operand is at @p pc: a comparison it takes at once becomes one operator
 *        with it (O7), any other operand an LNot of its own.
 */
static bool machine_language_record_not(MachineLanguageRecordBuilder_t *builder, uint32_t *pc, uint32_t limit,
                                        uint32_t level, MachineLanguageRecordOperand_t *out_operand)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    MachineLanguageRecordOperand_t operand = {0};
    MachineLanguageRecordNode_t node = {.opcode = (uint16_t) MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT)};

    machine_language_record_push_operator(builder);
    if (!machine_language_record_expression(builder, pc, limit, level + 1u, &operand))
        return false;
    machine_language_record_run_operator(builder, 1u);
    if (operand.kind == MACHINE_LANGUAGE_OPERAND(RESULT) && operand.slot + 1u == builder->record->node_count)
    {
        MachineLanguageRecordNode_t *last = &cache->nodes[cache->node_count - 1u];

        if (machine_language_record_compares(last->opcode) && last->negated == 0u)
        {
            last->negated = 1u;
            *out_operand = operand;
            return true;
        }
    }
    node.operands[0] = operand;
    return machine_language_record_append(builder, &node, out_operand);
}

/**
 * @brief Decodes an operator with two TermArg operands at @p pc, a comparison or an operator on two
 *        integers, whose target, if it has one, is NullName, or a local or an argument that goes to
 *        @p target when @p target is not NULL.
 */
static bool machine_language_record_binary(MachineLanguageRecordBuilder_t *builder, uint32_t opcode, uint32_t *pc,
                                           uint32_t limit, uint32_t level, MachineLanguageRecordOperand_t *out_operand,
                                           MachineLanguageRecordOperand_t *target)
{
    MachineLanguageRecordNode_t node = {.opcode = (uint16_t) opcode};

    machine_language_record_push_operator(builder);
    if (!machine_language_record_expression(builder, pc, limit, level + 1u, &node.operands[0]) ||
        !machine_language_record_expression(builder, pc, limit, level + 1u, &node.operands[1]))
        return false;
    if (machine_language_record_computes(opcode))
    {
        if (*pc < limit && builder->code[*pc] == MACHINE_LANGUAGE_NULL_NAME)
            ++*pc;
        else if (target == NULL || !machine_language_record_target(builder, pc, limit, target))
            return false;
        machine_language_record_push_values(builder, 1u);
    }
    machine_language_record_run_operator(builder, machine_language_record_computes(opcode) ? 3u : 2u);
    return machine_language_record_append(builder, &node, out_operand);
}

/**
 * @brief Decodes the expression at @p pc, a TermArg that may not read past @p limit, into the record:
 *        its operand, and what the interpreter's loop holds to run it.
 *
 * @return false when it holds a term no record runs: a name, a method call, a target other than
 *         NullName, an operator the records do not run, or one nested too deep.
 */
static bool machine_language_record_expression(MachineLanguageRecordBuilder_t *builder, uint32_t *pc, uint32_t limit,
                                               uint32_t level, MachineLanguageRecordOperand_t *out_operand)
{
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (level > MACHINE_LANGUAGE_MAX_DEPTH)
        return false;
    if (*pc < limit && advanced_configuration_and_power_interface_machine_language_starts_name(builder->code[*pc]))
        return machine_language_record_name(builder, pc, limit, out_operand);
    if (advanced_configuration_and_power_interface_machine_language_read_opcode(builder->code, *pc, limit, &opcode,
                                                                                &after) != MACHINE_LANGUAGE_OK)
        return false;
    if (machine_language_record_leaf(builder, opcode, pc, after, limit, out_operand))
        return true;
    *pc = after;
    if (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT))
        return machine_language_record_not(builder, pc, limit, level, out_operand);
    if (machine_language_record_compares(opcode) || machine_language_record_computes(opcode))
        return machine_language_record_binary(builder, opcode, pc, limit, level, out_operand, NULL);
    return false;
}

/**
 * @brief Decodes an If or a While whose opcode ends at @p after: its PkgLength, then its predicate.
 */
static void machine_language_record_branch(MachineLanguageRecordBuilder_t *builder, uint8_t shape, uint32_t after)
{
    MachineLanguageRecord_t *record = builder->record;
    uint32_t end = 0u;
    uint32_t body = 0u;

    machine_language_record_push_operator(builder);
    if (advanced_configuration_and_power_interface_machine_language_read_package_end(
            builder->code, after, record->limit, &end, &body) != MACHINE_LANGUAGE_OK ||
        !machine_language_record_expression(builder, &body, end, 0u, &record->source))
        return;
    record->after = body;
    record->end = end;
    record->shape = shape;
}

/**
 * @brief Decodes a Store whose opcode ends at @p after: its source, then the local or the argument it
 *        writes; the loop holds the source, then the target, as values.
 */
static void machine_language_record_store(MachineLanguageRecordBuilder_t *builder, uint32_t after)
{
    MachineLanguageRecord_t *record = builder->record;
    uint32_t pc = after;

    machine_language_record_push_operator(builder);
    if (!machine_language_record_expression(builder, &pc, record->limit, 0u, &record->source) ||
        !machine_language_record_target(builder, &pc, record->limit, &record->target))
        return;
    machine_language_record_push_values(builder, 1u);
    record->after = pc;
    record->shape = MACHINE_LANGUAGE_RECORD(STORE);
}

/**
 * @brief Decodes an operator on two integers that stands as a statement, at @p pc: its result goes to
 *        its target, a local, an argument or NullName.
 */
static void machine_language_record_compute(MachineLanguageRecordBuilder_t *builder, uint32_t opcode, uint32_t after)
{
    MachineLanguageRecord_t *record = builder->record;
    uint32_t pc = after;

    if (!machine_language_record_binary(builder, opcode, &pc, record->limit, 0u, &record->source, &record->target))
        return;
    record->after = pc;
    record->shape = MACHINE_LANGUAGE_RECORD(STORE);
}

/**
 * @brief Decodes an Increment or a Decrement whose opcode ends at @p after: the local or the argument it
 *        steps, which the loop holds as one value.
 */
static void machine_language_record_step(MachineLanguageRecordBuilder_t *builder, uint8_t shape, uint32_t after)
{
    MachineLanguageRecord_t *record = builder->record;
    uint32_t pc = after;

    machine_language_record_push_operator(builder);
    if (!machine_language_record_target(builder, &pc, record->limit, &record->target))
        return;
    machine_language_record_push_values(builder, 1u);
    record->after = pc;
    record->shape = shape;
}

/**
 * @brief Decodes the statement of the record: a While, an If, a Break, a Store, an operator with a
 *        target, an Increment or a Decrement, or any other statement, which stays a record the
 *        interpreter's loop runs; in a build that counts what the loop runs, it keeps the opcode of a statement
 *        that is no operator of the record (§4.1).
 */
static void machine_language_record_decode(MachineLanguageRecordBuilder_t *builder)
{
    MachineLanguageRecord_t *record = builder->record;
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (advanced_configuration_and_power_interface_machine_language_read_opcode(
            builder->code, record->pc, record->limit, &opcode, &after) != MACHINE_LANGUAGE_OK)
        return;
    if (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_COUNTS_EXECUTED &&
        !machine_language_record_computes(opcode))
        record->statement = (uint8_t) opcode;
    switch (opcode)
    {
    case MACHINE_LANGUAGE_OPCODE(WHILE):
        machine_language_record_branch(builder, MACHINE_LANGUAGE_RECORD(WHILE), after);
        return;
    case MACHINE_LANGUAGE_OPCODE(IF):
        machine_language_record_branch(builder, MACHINE_LANGUAGE_RECORD(IF), after);
        return;
    case MACHINE_LANGUAGE_OPCODE(BREAK):
        record->after = after;
        record->shape = MACHINE_LANGUAGE_RECORD(BREAK);
        return;
    case MACHINE_LANGUAGE_OPCODE(STORE): machine_language_record_store(builder, after); return;
    case MACHINE_LANGUAGE_OPCODE(INCREMENT):
        machine_language_record_step(builder, MACHINE_LANGUAGE_RECORD(INCREMENT), after);
        return;
    case MACHINE_LANGUAGE_OPCODE(DECREMENT):
        machine_language_record_step(builder, MACHINE_LANGUAGE_RECORD(DECREMENT), after);
        return;
    default:
        if (machine_language_record_computes(opcode))
            machine_language_record_compute(builder, opcode, after);
        return;
    }
}

/**
 * @brief Whether the statement at @p pc, which no record runs, always goes on to the statement after it: a name, a
 *        call among them, or an operator that opens no construct and is no Break, Continue or Return.
 */
static bool machine_language_record_goes_on(const uint8_t *code, uint32_t pc, uint32_t limit)
{
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (pc < limit && advanced_configuration_and_power_interface_machine_language_starts_name(code[pc]))
        return true;
    if (advanced_configuration_and_power_interface_machine_language_read_opcode(code, pc, limit, &opcode, &after) !=
        MACHINE_LANGUAGE_OK)
        return false;
    return opcode != MACHINE_LANGUAGE_OPCODE(BREAK) && opcode != MACHINE_LANGUAGE_OPCODE(CONTINUE) &&
           opcode != MACHINE_LANGUAGE_OPCODE(RETURN) &&
           (advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags &
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH) == 0u;
}

/**
 * @brief Whether a record that reads names holds a modulo, whose divisor would be known only once a name is read,
 *        past the record's first effect.
 */
static bool machine_language_record_reads_into_a_modulo(const MachineLanguagePredecoding_t *cache,
                                                        const MachineLanguageRecord_t *record)
{
    if (record->name_count == 0u)
        return false;
    for (uint32_t index = 0u; index < record->node_count; ++index)
        if (cache->nodes[record->first_node + index].opcode == MACHINE_LANGUAGE_OPCODE(MOD))
            return true;
    return false;
}

/**
 * @brief Whether a While and the record of its body make a tight loop (§3.6): the While's predicate compares two
 *        leaves, and its body is one statement that writes and ends where the While does.
 */
static bool machine_language_record_makes_tight(const MachineLanguagePredecoding_t *cache,
                                                const MachineLanguageRecord_t *loop,
                                                const MachineLanguageRecord_t *body)
{
    const MachineLanguageRecordNode_t *node = &cache->nodes[loop->first_node];

    return loop->shape == MACHINE_LANGUAGE_RECORD(WHILE) && loop->node_count == 1u && loop->name_count == 0u &&
           body->name_count == 0u && machine_language_record_compares(node->opcode) &&
           node->operands[0].kind != MACHINE_LANGUAGE_OPERAND(RESULT) &&
           node->operands[1].kind != MACHINE_LANGUAGE_OPERAND(RESULT) && body->after == loop->end &&
           body->limit == loop->end &&
           advanced_configuration_and_power_interface_machine_language_record_writes(body->shape);
}

/**
 * @brief Puts the record just built in the hash.
 */
static void machine_language_record_insert(MachineLanguagePredecoding_t *cache, uint16_t index)
{
    const MachineLanguageRecord_t *record = &cache->records[index - 1u];
    uint32_t slot = machine_language_record_slot(record->table, record->pc);

    while (cache->slots[slot] != 0u)
        slot = (slot + 1u) % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_SLOTS;
    cache->slots[slot] = index;
    cache->recent[record->pc % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECENT_RECORDS] = index;
}

void advanced_configuration_and_power_interface_machine_language_predecoding_open(MachineLanguagePredecoding_t *cache)
{
    machine_language_records_empty(cache);
    cache->integer_bits = 0u;
    cache->records_run = 0u;
    for (uint32_t reason = 0u; reason < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINES; ++reason)
        cache->declined[reason] = 0u;
    cache->built = 0u;
    cache->flushes = 0u;
    cache->records_peak = 0u;
    cache->fused_comparisons = 0u;
    cache->tight_turns = 0u;
    cache->searches = 0u;
    cache->handed_back_loops = 0u;
    cache->stale_names = 0u;
    cache->watched_serial = 0u;
    cache->watched_start = 0u;
}

void advanced_configuration_and_power_interface_machine_language_predecoding_flush(MachineLanguagePredecoding_t *cache,
                                                                                   uint8_t integer_bits)
{
    cache->flushes += (cache->record_count != 0u) ? 1u : 0u;
    machine_language_records_empty(cache);
    cache->integer_bits = integer_bits;
}

void advanced_configuration_and_power_interface_machine_language_record_link(MachineLanguagePredecoding_t *cache,
                                                                             uint16_t from, uint16_t to)
{
    MachineLanguageRecord_t *record = &cache->records[from - 1u];

    record->next = to;
    record->tight = machine_language_record_makes_tight(cache, record, &cache->records[to - 1u]) ? 1u : 0u;
}

uint16_t advanced_configuration_and_power_interface_machine_language_record_find(MachineLanguagePredecoding_t *cache,
                                                                                 uint16_t table, uint32_t pc)
{
    for (uint32_t slot = machine_language_record_slot(table, pc);;
         slot = (slot + 1u) % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_SLOTS)
    {
        const uint16_t index = cache->slots[slot];

        if (index == 0u || index > cache->record_count)
            return 0u;
        if (cache->records[index - 1u].pc == pc && cache->records[index - 1u].table == table)
        {
            cache->recent[pc % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECENT_RECORDS] = index;
            return index;
        }
    }
}

uint16_t advanced_configuration_and_power_interface_machine_language_record_build(MachineLanguagePredecoding_t *cache,
                                                                                  const uint8_t *code, uint16_t table,
                                                                                  uint32_t pc, uint32_t limit,
                                                                                  uint8_t integer_bits)
{
    if (cache->record_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORDS)
        return 0u;

    MachineLanguageRecord_t *record = &cache->records[cache->record_count];
    const uint32_t nodes = cache->node_count;
    const uint32_t constants = cache->constant_count;
    const uint32_t names = cache->name_count;
    const MachineLanguageRecord_t empty = {
        .pc = pc,
        .limit = limit,
        .table = table,
        .first_node = (uint16_t) nodes,
        .shape = MACHINE_LANGUAGE_RECORD(GENERAL),
    };
    MachineLanguageRecordBuilder_t builder = {.cache = cache,
                                              .record = record,
                                              .code = code,
                                              .integer_bits = integer_bits};

    *record = empty;
    machine_language_record_decode(&builder);
    record->values_peak = (uint8_t) builder.values_peak;
    record->operators_peak = (uint8_t) builder.operators_peak;
    if (record->shape == MACHINE_LANGUAGE_RECORD(GENERAL) || builder.exhausted ||
        machine_language_record_reads_into_a_modulo(cache, record))
    {
        *record = empty;
        record->goes_on = machine_language_record_goes_on(code, pc, limit) ? 1u : 0u;
        cache->node_count = nodes;
        cache->constant_count = constants;
        cache->name_count = names;
    }
    if (builder.exhausted)
        return 0u;
    for (uint32_t node = nodes; node < cache->node_count; ++node)
        cache->fused_comparisons += cache->nodes[node].negated;
    ++cache->record_count;
    ++cache->built;
    if (cache->record_count > cache->records_peak)
        cache->records_peak = cache->record_count;
    machine_language_record_insert(cache, (uint16_t) cache->record_count);
    return (uint16_t) cache->record_count;
}
