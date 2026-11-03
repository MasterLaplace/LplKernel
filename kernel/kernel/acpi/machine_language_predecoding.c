#include <kernel/acpi/machine_language_predecoding.h>

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

/**
 * @brief What a record's decoding works on: the cache, the record, and the bytes it reads.
 */
typedef struct MachineLanguageRecordBuilder {
    MachineLanguagePredecoding_t *cache; /**< Where operators and constants go. */
    MachineLanguageRecord_t *record;     /**< The record being decoded. */
    const uint8_t *code;                 /**< The table's bytes. */
    uint8_t integer_bits;                /**< The load's integer width. */
} MachineLanguageRecordBuilder_t;

/**
 * @brief What the interpreter's loop holds for a term at most, as it runs it: values on its stack and
 *        operators pending.
 */
typedef struct MachineLanguageRecordDepth {
    uint32_t values;    /**< Values. */
    uint32_t operators; /**< Operators. */
} MachineLanguageRecordDepth_t;

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
    uint32_t *slots = (uint32_t *) cache->slots;

    for (uint32_t word = 0u; word < sizeof(cache->slots) / sizeof(uint32_t); ++word)
        slots[word] = 0u;
    cache->record_count = 0u;
    cache->node_count = 0u;
    cache->constant_count = 0u;
    cache->frozen_serial = 0u;
    cache->frozen_start = 0u;
}

/**
 * @brief Masks a constant to the load's integer width, as the interpreter's loop masks one it reads.
 */
static uint64_t machine_language_record_mask(uint8_t integer_bits, uint64_t integer)
{
    return (integer_bits == 32u) ? (integer & 0xFFFFFFFFull) : integer;
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
 * @brief Decodes a leaf of an expression whose opcode was read: a local, an argument, or a constant,
 *        masked to the width and kept among the cache's constants.
 *
 * @return false when the opcode is no leaf, a constant's bytes pass @p limit, or the constants are full.
 */
static bool machine_language_record_leaf(MachineLanguageRecordBuilder_t *builder, uint32_t opcode, uint32_t *pc,
                                         uint32_t after, uint32_t limit, MachineLanguageRecordOperand_t *out_operand)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    uint64_t integer = 0u;

    if (opcode >= MACHINE_LANGUAGE_OPCODE(LOCAL0) && opcode <= MACHINE_LANGUAGE_OPCODE(LOCAL7))
    {
        out_operand->kind = MACHINE_LANGUAGE_OPERAND(LOCAL);
        out_operand->slot = (uint8_t) (opcode - MACHINE_LANGUAGE_OPCODE(LOCAL0));
        *pc = after;
        return true;
    }
    if (opcode >= MACHINE_LANGUAGE_OPCODE(ARGUMENT0) && opcode <= MACHINE_LANGUAGE_OPCODE(ARGUMENT6))
    {
        out_operand->kind = MACHINE_LANGUAGE_OPERAND(ARGUMENT);
        out_operand->slot = (uint8_t) (opcode - MACHINE_LANGUAGE_OPCODE(ARGUMENT0));
        *pc = after;
        return true;
    }
    if (opcode != MACHINE_LANGUAGE_OPCODE(ZERO) && opcode != MACHINE_LANGUAGE_OPCODE(ONE) &&
        opcode != MACHINE_LANGUAGE_OPCODE(ONES) && opcode != MACHINE_LANGUAGE_OPCODE(BYTE_PREFIX) &&
        opcode != MACHINE_LANGUAGE_OPCODE(WORD_PREFIX) && opcode != MACHINE_LANGUAGE_OPCODE(DOUBLE_WORD_PREFIX) &&
        opcode != MACHINE_LANGUAGE_OPCODE(QUAD_WORD_PREFIX))
        return false;
    if (cache->constant_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_CONSTANTS ||
        advanced_configuration_and_power_interface_machine_language_read_constant(builder->code, opcode, after, limit,
                                                                                  &integer, pc) != MACHINE_LANGUAGE_OK)
        return false;
    cache->constants[cache->constant_count] = machine_language_record_mask(builder->integer_bits, integer);
    out_operand->kind = MACHINE_LANGUAGE_OPERAND(CONSTANT);
    out_operand->index = (uint16_t) cache->constant_count++;
    return true;
}

static bool machine_language_record_expression(MachineLanguageRecordBuilder_t *builder, uint32_t *pc, uint32_t limit,
                                               uint32_t level, MachineLanguageRecordOperand_t *out_operand,
                                               MachineLanguageRecordDepth_t *out_depth);

/**
 * @brief Appends an operator to the record, its result the expression's operand.
 *
 * @return false when the record or the cache holds no more operators.
 */
static bool machine_language_record_append(MachineLanguageRecordBuilder_t *builder,
                                           const MachineLanguageRecordNode_t *node,
                                           MachineLanguageRecordOperand_t *out_operand)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    MachineLanguageRecord_t *record = builder->record;

    if (record->node_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NODES_PER_RECORD ||
        cache->node_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NODES)
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
                                        uint32_t level, MachineLanguageRecordOperand_t *out_operand,
                                        MachineLanguageRecordDepth_t *out_depth)
{
    MachineLanguagePredecoding_t *cache = builder->cache;
    MachineLanguageRecordOperand_t operand = {0};
    MachineLanguageRecordNode_t node = {.opcode = (uint16_t) MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT)};

    if (!machine_language_record_expression(builder, pc, limit, level + 1u, &operand, out_depth))
        return false;
    ++out_depth->operators;
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
 *        integers, whose target, if it has one, must be NullName.
 */
static bool machine_language_record_binary(MachineLanguageRecordBuilder_t *builder, uint32_t opcode, uint32_t *pc,
                                           uint32_t limit, uint32_t level, MachineLanguageRecordOperand_t *out_operand,
                                           MachineLanguageRecordDepth_t *out_depth)
{
    MachineLanguageRecordNode_t node = {.opcode = (uint16_t) opcode};
    MachineLanguageRecordDepth_t first = {0u, 0u};
    MachineLanguageRecordDepth_t second = {0u, 0u};

    if (!machine_language_record_expression(builder, pc, limit, level + 1u, &node.operands[0], &first) ||
        !machine_language_record_expression(builder, pc, limit, level + 1u, &node.operands[1], &second))
        return false;
    out_depth->values = (first.values > 1u + second.values) ? first.values : 1u + second.values;
    out_depth->operators = 1u + ((first.operators > second.operators) ? first.operators : second.operators);
    if (machine_language_record_computes(opcode))
    {
        if (*pc >= limit || builder->code[*pc] != MACHINE_LANGUAGE_NULL_NAME)
            return false;
        ++*pc;
        out_depth->values = (out_depth->values > 3u) ? out_depth->values : 3u;
    }
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
                                               uint32_t level, MachineLanguageRecordOperand_t *out_operand,
                                               MachineLanguageRecordDepth_t *out_depth)
{
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    out_depth->values = 1u;
    out_depth->operators = 0u;
    if (level > MACHINE_LANGUAGE_MAX_DEPTH || advanced_configuration_and_power_interface_machine_language_read_opcode(
                                                  builder->code, *pc, limit, &opcode, &after) != MACHINE_LANGUAGE_OK)
        return false;
    if (machine_language_record_leaf(builder, opcode, pc, after, limit, out_operand))
        return true;
    *pc = after;
    if (opcode == MACHINE_LANGUAGE_OPCODE(LOGICAL_NOT))
        return machine_language_record_not(builder, pc, limit, level, out_operand, out_depth);
    if (machine_language_record_compares(opcode) || machine_language_record_computes(opcode))
        return machine_language_record_binary(builder, opcode, pc, limit, level, out_operand, out_depth);
    return false;
}

/**
 * @brief Decodes an If or a While whose opcode ends at @p after: its PkgLength, then its predicate.
 */
static void machine_language_record_branch(MachineLanguageRecordBuilder_t *builder, uint8_t shape, uint32_t after)
{
    MachineLanguageRecord_t *record = builder->record;
    MachineLanguageRecordDepth_t depth = {0u, 0u};
    uint32_t end = 0u;
    uint32_t body = 0u;

    if (advanced_configuration_and_power_interface_machine_language_read_package_end(
            builder->code, after, record->limit, &end, &body) != MACHINE_LANGUAGE_OK ||
        !machine_language_record_expression(builder, &body, end, 0u, &record->source, &depth))
        return;
    record->after = body;
    record->end = end;
    record->values_peak = (uint8_t) depth.values;
    record->operators_peak = (uint8_t) (depth.operators + 1u);
    record->shape = shape;
}

/**
 * @brief Decodes the statement of the record: a While, an If, a Break, or any other statement, which
 *        stays a record the interpreter's loop runs.
 */
static void machine_language_record_decode(MachineLanguageRecordBuilder_t *builder)
{
    MachineLanguageRecord_t *record = builder->record;
    uint32_t opcode = MACHINE_LANGUAGE_OPCODE(INVALID);
    uint32_t after = 0u;

    if (advanced_configuration_and_power_interface_machine_language_read_opcode(
            builder->code, record->pc, record->limit, &opcode, &after) != MACHINE_LANGUAGE_OK)
        return;
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
    default: return;
    }
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
}

void advanced_configuration_and_power_interface_machine_language_predecoding_flush(MachineLanguagePredecoding_t *cache,
                                                                                   uint8_t integer_bits)
{
    cache->flushes += (cache->record_count != 0u) ? 1u : 0u;
    machine_language_records_empty(cache);
    cache->integer_bits = integer_bits;
}

uint16_t
advanced_configuration_and_power_interface_machine_language_record_find(const MachineLanguagePredecoding_t *cache,
                                                                        uint16_t table, uint32_t pc)
{
    for (uint32_t slot = machine_language_record_slot(table, pc);;
         slot = (slot + 1u) % ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_SLOTS)
    {
        const uint16_t index = cache->slots[slot];

        if (index == 0u || index > cache->record_count)
            return 0u;
        if (cache->records[index - 1u].pc == pc && cache->records[index - 1u].table == table)
            return index;
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
    const MachineLanguageRecord_t empty = {
        .pc = pc,
        .limit = limit,
        .table = table,
        .first_node = (uint16_t) nodes,
        .shape = MACHINE_LANGUAGE_RECORD(GENERAL),
    };
    MachineLanguageRecordBuilder_t builder = {cache, record, code, integer_bits};

    *record = empty;
    machine_language_record_decode(&builder);
    if (record->shape == MACHINE_LANGUAGE_RECORD(GENERAL))
    {
        *record = empty;
        cache->node_count = nodes;
        cache->constant_count = constants;
    }
    for (uint32_t node = nodes; node < cache->node_count; ++node)
        cache->fused_comparisons += cache->nodes[node].negated;
    ++cache->record_count;
    ++cache->built;
    if (cache->record_count > cache->records_peak)
        cache->records_peak = cache->record_count;
    machine_language_record_insert(cache, (uint16_t) cache->record_count);
    return (uint16_t) cache->record_count;
}
