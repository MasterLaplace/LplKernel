#include "fixtures/predecoding.h"
#include "fixtures/common.h"
#include "made_up_aml.h"
#include "made_up_table.h"
#include "predecoding_runs.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_predecoding, KERNEL_TEST_STAGE_INITIALIZATION);

#define PREDECODING_TABLE_CAPACITY 2304u /**< Bytes of the buffer a made-up table is written in. */
#define PREDECODING_NOOPS                                                                                              \
    140u /**< Noops in the inner loop of the cache that fills: more than                                               \
              the 128 records a load keeps. */
#define PREDECODING_RECORD(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_##name
#define PREDECODING_OPERAND(name)  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECORD_OPERAND_##name
#define PREDECODING_DECLINED(name) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_DECLINED_##name
#define PREDECODING_OPCODE(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##name
#define PREDECODING_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define PREDECODING_LOADED_ADDRESS 0x00300000u /**< Where the table XDEV.MAIN's loop loads lies in memory. */
#define PREDECODING_POOL_IFS                                                                                           \
    90u /**< Ifs of three operators and three constants in the loop whose operators and                                \
            constants fill the cache's: 270 of each, past the 256. */
#define PREDECODING_SEVEN_CALLS                                                                                        \
    169u                          /**< Calls of seven arguments nested in the last that hold the value stack           \
                                      nearly full while LOOP runs. */
#define PREDECODING_SEVEN_PADS 7u /**< Adds of One around those calls, which hold one value each. */
#define PREDECODING_FIND_PC                                                                                            \
    8u /**< A statement offset whose home slot under table 1 follows its home slot under                               \
           PREDECODING_FIND_TABLE. */
#define PREDECODING_FIND_TABLE       22u  /**< The table of that second home slot. */
#define PREDECODING_FIND_FILLER      131u /**< A statement offset whose home slot under table 1 is that second one. */
#define PREDECODING_SLOT_MULTIPLIER  0x9E3779B1u /**< The cache's mix of a statement's offset, written here apart. */
#define PREDECODING_TABLE_MULTIPLIER 0x85EBCA6Bu /**< The cache's mix of a table, written here apart. */
#define PREDECODING_SLOT_BUFFER                                                                                        \
    51u /**< Bytes of SLOT's Buffer, which put its Increment 64 bytes past its call, in the same recent slot. */

typedef AdvancedConfigurationAndPowerInterfaceMachineLanguagePredecoding_t PredecodingCache_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecord_t PredecodingRecord_t;
typedef AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordName_t PredecodingName_t;

static uint8_t predecoding_table[PREDECODING_TABLE_CAPACITY];
static uint8_t predecoding_second_table[PREDECODING_TABLE_CAPACITY];
static PredecodingCache_t predecoding_cache;
static MadeUpAml_t predecoding_method;
static MadeUpAml_t predecoding_outer;
static MadeUpAml_t predecoding_inner;
static MadeUpAml_t predecoding_body;
static uint8_t predecoding_noops[256];

/**
 * @brief Where @p pattern first starts in @p bytes, or @p length when it does not.
 */
static uint32_t predecoding_offset_of(const uint8_t *bytes, uint32_t length, const uint8_t *pattern,
                                      uint32_t pattern_length)
{
    for (uint32_t offset = 0u; offset + pattern_length <= length; ++offset)
    {
        uint32_t matched = 0u;

        while (matched < pattern_length && bytes[offset + matched] == pattern[matched])
            ++matched;
        if (matched == pattern_length)
            return offset;
    }
    return length;
}

/**
 * @brief Writes INCR, then SLOT: `Local0 = 0; Local2 = 0; While (Local0 < 10) { Local2 = INCR (Local2); Local3 =
 *        Buffer (51) {}; Local0++ }; Return (Local2)`, so 10: its call, which no record runs, and its Increment,
 *        which one runs, 64 bytes apart, so that they share their offset's recent slot.
 *
 * Its whole terms come from fixtures; its other pieces are raw bytes: the While and the Buffer it opens over
 * PREDECODING_SLOT_BUFFER bytes, whose PkgLengths it writes, and the partial terms around them.
 */
static void predecoding_write_shared_slot(void)
{
    static const uint8_t head[] = {0x95u, 0x60u, 0x0Au, 0x0Au, 0x70u, 'I', 'N', 'C', 'R', 0x62u, 0x62u, 0x70u};
    static const uint8_t buffer_lead[] = {0x11u};
    static const uint8_t buffer_size[] = {0x0Au, PREDECODING_SLOT_BUFFER};
    static const uint8_t buffer_byte[] = {0x00u};
    static const uint8_t tail[] = {0x63u, 0x75u, 0x60u};
    static const uint8_t while_lead[] = {0xA2u};

    made_up_aml_begin(&predecoding_inner);
    made_up_aml_raw(&predecoding_inner, buffer_size, sizeof(buffer_size));
    for (uint32_t index = 0u; index < PREDECODING_SLOT_BUFFER; ++index)
        made_up_aml_raw(&predecoding_inner, buffer_byte, sizeof(buffer_byte));
    made_up_aml_begin(&predecoding_outer);
    made_up_aml_raw(&predecoding_outer, head, sizeof(head));
    made_up_aml_construct(&predecoding_outer, buffer_lead, sizeof(buffer_lead), &predecoding_inner);
    made_up_aml_raw(&predecoding_outer, tail, sizeof(tail));
    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, predecoding_clear_local0_local2_aml,
                    sizeof(predecoding_clear_local0_local2_aml));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, predecoding_return_local2_aml, sizeof(predecoding_return_local2_aml));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, predecoding_increment_method_aml, sizeof(predecoding_increment_method_aml));
    made_up_aml_method(&predecoding_method, "SLOT", 0x00u, &predecoding_body);
}

/**
 * @brief Writes INCR, then EVIC: `Local0 = 0; Local1 = 0; While (Local0 < 10) { Local2 = INCR (Local2); Local1++;
 *        Local4 = Buffer (51) {}; If (Local0 == 1) { Local5 = INCR (Local5) }; Local0++ }; Return (Local1)`, so 10: the
 *        call in the If, at the second turn alone, 64 bytes past the Increment of Local1, takes its recent slot.
 *
 * Its whole terms come from fixtures; its other pieces are raw bytes: the While and the Buffer it opens over
 * PREDECODING_SLOT_BUFFER bytes, whose PkgLengths it writes, and the partial terms around them.
 */
static void predecoding_write_evicted(void)
{
    static const uint8_t head[] = {0x95u, 0x60u, 0x0Au, 0x0Au, 0x70u, 'I',   'N',
                                   'C',   'R',   0x62u, 0x62u, 0x75u, 0x61u, 0x70u};
    static const uint8_t buffer_lead[] = {0x11u};
    static const uint8_t buffer_size[] = {0x0Au, PREDECODING_SLOT_BUFFER};
    static const uint8_t buffer_byte[] = {0x00u};
    static const uint8_t tail[] = {0x64u, 0xA0u, 0x0Bu, 0x93u, 0x60u, 0x01u, 0x70u, 'I',
                                   'N',   'C',   'R',   0x65u, 0x65u, 0x75u, 0x60u};
    static const uint8_t while_lead[] = {0xA2u};

    made_up_aml_begin(&predecoding_inner);
    made_up_aml_raw(&predecoding_inner, buffer_size, sizeof(buffer_size));
    for (uint32_t index = 0u; index < PREDECODING_SLOT_BUFFER; ++index)
        made_up_aml_raw(&predecoding_inner, buffer_byte, sizeof(buffer_byte));
    made_up_aml_begin(&predecoding_outer);
    made_up_aml_raw(&predecoding_outer, head, sizeof(head));
    made_up_aml_construct(&predecoding_outer, buffer_lead, sizeof(buffer_lead), &predecoding_inner);
    made_up_aml_raw(&predecoding_outer, tail, sizeof(tail));
    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, predecoding_clear_local0_local1_aml,
                    sizeof(predecoding_clear_local0_local1_aml));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, predecoding_return_local1_aml, sizeof(predecoding_return_local1_aml));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, predecoding_increment_method_aml, sizeof(predecoding_increment_method_aml));
    made_up_aml_method(&predecoding_method, "EVIC", 0x00u, &predecoding_body);
}

/**
 * @brief An External a table the loop loads fills with an Alias, as MAIN: `External (\FOO_); Scope (\FOO_) { Name
 *        (BAR_, 1) }`, DEVB.BAR_ holding 100, then `Local0 = 0; Local1 = 0; While (Local0 < 4) { Local0++; If (Local0
 * == 3) { Load (TBLR, HNDL) }; Local1 += \FOO_.BAR_ }; Return (Local1)`, the table loaded declaring `Alias (\DEVB,
 *        \FOO_)`: 1 + 1 + 100 + 100, so 202.
 *
 * It is raw bytes: iasl emits an External at the root only inside an If (Zero).
 */
static const uint8_t predecoding_alias_fills[] = {
    0x15u, 0x5Cu, 'F',   'O',   'O',   '_',   0x06u, 0x00u, 0x10u, 0x0Cu, 0x5Cu, 'F',   'O',   'O',   '_',   0x08u,
    'B',   'A',   'R',   '_',   0x01u, 0x5Bu, 0x82u, 0x0Cu, 'D',   'E',   'V',   'B',   0x08u, 'B',   'A',   'R',
    '_',   0x0Au, 0x64u, 0x08u, 'H',   'N',   'D',   'L',   0x00u, 0x5Bu, 0x80u, 'T',   'B',   'L',   'R',   0x00u,
    0x0Cu, 0x00u, 0x00u, 0x30u, 0x00u, 0x0Au, 0x32u, 0x14u, 0x33u, 'M',   'A',   'I',   'N',   0x00u, 0x70u, 0x00u,
    0x60u, 0x70u, 0x00u, 0x61u, 0xA2u, 0x24u, 0x95u, 0x60u, 0x0Au, 0x04u, 0x75u, 0x60u, 0xA0u, 0x0Fu, 0x93u, 0x60u,
    0x0Au, 0x03u, 0x5Bu, 0x20u, 'T',   'B',   'L',   'R',   'H',   'N',   'D',   'L',   0x72u, 0x61u, 0x5Cu, 0x2Eu,
    'F',   'O',   'O',   '_',   'B',   'A',   'R',   '_',   0x61u, 0xA4u, 0x61u,
};

/**
 * @brief Statements no record runs, at offset 0 under their own lengths, raw bytes iasl does not emit: a While whose
 *        PkgLength cuts its constant, a While whose PkgLength passes the end (§8.2), and `Increment (NAME)`, whose
 *        name iasl's lexer reads as the keyword Name.
 */
static const uint8_t predecoding_cut_constant[] = {0xA2u, 0x04u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u};
static const uint8_t predecoding_crossing[] = {0xA2u, 0x10u, 0x95u, 0x60u, 0x01u, 0x75u, 0x60u};
static const uint8_t predecoding_increment_name[] = {0x75u, 'N', 'A', 'M', 'E'};

/**
 * @brief Runs @p path of @p aml with records off, then on, and checks that both left the same fold;
 *        the control: no record ran with them off, and with them on, records ran unless @p runs_records
 *        says none can. The fold is measured as @p key.
 *
 * @return What the run with records on left.
 */
static PredecodingRun_t predecoding_compare(KernelTest_t *test, const uint8_t *aml, uint32_t length, uint8_t revision,
                                            const char *path, uint64_t argument, bool runs_records, const char *key)
{
    PredecodingRun_t off = {0};
    PredecodingRun_t on = {0};

    if (!predecoding_run(test, aml, length, revision, path, argument, false, &off) ||
        !predecoding_run(test, aml, length, revision, path, argument, true, &on))
        return on;
    kernel_test_check(test, off.fold == on.fold, "the records leave what the loop leaves");
    kernel_test_check(test, off.records_run == 0u && off.declined == 0u, "no record runs with records off");
    kernel_test_check(test, runs_records == (on.records_run != 0u), "records run where they can, and only there");
    kernel_test_check(test, on.stale_names == 0u, "every name a record reads finds the node the record checked");
    kernel_test_measure_hexadecimal(test, key, on.fold);
    return on;
}

/**
 * @brief Writes MAIN: `Local0 = 0; While (Local0 < 3) { Local0++; Local1 = 0; While (Local1 < 2)
 *        { Local1++; Noop... } }; Return (Local0 + Local1)`, its inner loop holding PREDECODING_NOOPS Noops.
 *
 * Its whole terms come from fixtures; its other pieces are raw bytes: the two While it opens over PREDECODING_NOOPS
 * Noops, whose PkgLengths it writes, and their heads, partial terms.
 */
static void predecoding_write_full_loop(void)
{
    static const uint8_t while_lead[] = {0xA2u};
    static const uint8_t outer_head[] = {0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u, 0x70u, 0x00u, 0x61u};
    static const uint8_t inner_head[] = {0x95u, 0x61u, 0x0Au, 0x02u, 0x75u, 0x61u};

    made_up_aml_begin(&predecoding_inner);
    made_up_aml_raw(&predecoding_inner, inner_head, sizeof(inner_head));
    for (uint32_t index = 0u; index < PREDECODING_NOOPS; ++index)
        made_up_aml_raw(&predecoding_inner, common_noop_aml, sizeof(common_noop_aml));
    made_up_aml_begin(&predecoding_outer);
    made_up_aml_raw(&predecoding_outer, outer_head, sizeof(outer_head));
    made_up_aml_construct(&predecoding_outer, while_lead, sizeof(while_lead), &predecoding_inner);
    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, predecoding_clear_local0_aml, sizeof(predecoding_clear_local0_aml));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, predecoding_return_sum_aml, sizeof(predecoding_return_sum_aml));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief Writes MAIN: `Local0 = 0; Local1 = 0; While (Local0 < 3) { Local0++; If (Local0 + k + (k + 1) == 0xFF)
 *        { Local1++ } for each k below PREDECODING_POOL_IFS }; Return (Local1)`, each If three operators and three
 *        constants.
 *
 * Its whole terms come from fixtures; its other pieces are raw bytes: the While and the PREDECODING_POOL_IFS If it
 * opens, whose PkgLengths and constants it writes, and the While's head, a partial term.
 */
static void predecoding_write_pools(void)
{
    static const uint8_t while_lead[] = {0xA2u};
    static const uint8_t if_lead[] = {0xA0u};
    static const uint8_t head[] = {0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u};

    made_up_aml_begin(&predecoding_outer);
    made_up_aml_raw(&predecoding_outer, head, sizeof(head));
    for (uint32_t index = 0u; index < PREDECODING_POOL_IFS; ++index)
    {
        const uint8_t predicate[] = {
            0x93u, 0x72u, 0x72u, 0x60u, 0x0Au, (uint8_t) index, 0x00u, 0x0Au, (uint8_t) (index + 1u),
            0x00u, 0x0Au, 0xFFu, 0x75u, 0x61u};

        made_up_aml_begin(&predecoding_inner);
        made_up_aml_raw(&predecoding_inner, predicate, sizeof(predicate));
        made_up_aml_construct(&predecoding_outer, if_lead, sizeof(if_lead), &predecoding_inner);
    }
    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, predecoding_clear_local0_local1_aml,
                    sizeof(predecoding_clear_local0_local1_aml));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, predecoding_return_local1_aml, sizeof(predecoding_return_local1_aml));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief Writes LOOP and FSEV, then MAIN: `Return (1 + (1 + ... FSEV (1, 1, 1, 1, 1, 1, FSEV (... LOOP ()))))`,
 *        PREDECODING_SEVEN_CALLS calls in the last argument of the one around them, each holding six values while
 *        LOOP runs, under PREDECODING_SEVEN_PADS Adds: LOOP's If at its third turn needs more values than are left.
 *
 * Its innermost call comes from a fixture; its other pieces are raw bytes, partial terms it nests
 * PREDECODING_SEVEN_CALLS deep: a Return opcode, Adds of One whose second operand is what they nest, calls of FSEV
 * whose last argument is what they nest, and the NullNames that close the Adds.
 */
static void predecoding_write_values_nearly_full(void)
{
    static const uint8_t add_one[] = {0x72u, 0x01u};
    static const uint8_t seven[] = {'F', 'S', 'E', 'V', 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u};
    static const uint8_t null_name[] = {0x00u};
    static const uint8_t returns[] = {0xA4u};

    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, returns, sizeof(returns));
    for (uint32_t pad = 0u; pad < PREDECODING_SEVEN_PADS; ++pad)
        made_up_aml_raw(&predecoding_body, add_one, sizeof(add_one));
    for (uint32_t call = 0u; call < PREDECODING_SEVEN_CALLS; ++call)
        made_up_aml_raw(&predecoding_body, seven, sizeof(seven));
    made_up_aml_raw(&predecoding_body, predecoding_loop_call_aml, sizeof(predecoding_loop_call_aml));
    for (uint32_t pad = 0u; pad < PREDECODING_SEVEN_PADS; ++pad)
        made_up_aml_raw(&predecoding_body, null_name, sizeof(null_name));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, predecoding_values_loop_aml, sizeof(predecoding_values_loop_aml));
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief Writes ROOM and FSEV, then MAIN: `Local0 = ROOM (); Return (1 + (1 + ... FSEV (1, 1, 1, 1, 1, 1, FSEV (...
 *        ROOM ()))))`, PREDECODING_SEVEN_CALLS calls nested in the last argument under @p pads Adds: the first call
 *        links ROOM's tight loop, the second runs it @p pads values deeper.
 *
 * Its first Store and its innermost call come from fixtures; its other pieces are raw bytes, partial terms it nests
 * @p pads deep: a Return opcode, Adds of One whose second operand is what they nest, calls of FSEV whose last argument
 * is what they nest, and the NullNames that close the Adds.
 */
static void predecoding_write_room(uint32_t pads)
{
    static const uint8_t returns[] = {0xA4u};
    static const uint8_t add_one[] = {0x72u, 0x01u};
    static const uint8_t seven[] = {'F', 'S', 'E', 'V', 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u};
    static const uint8_t null_name[] = {0x00u};

    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, predecoding_store_room_call_aml, sizeof(predecoding_store_room_call_aml));
    made_up_aml_raw(&predecoding_body, returns, sizeof(returns));
    for (uint32_t pad = 0u; pad < pads; ++pad)
        made_up_aml_raw(&predecoding_body, add_one, sizeof(add_one));
    for (uint32_t call = 0u; call < PREDECODING_SEVEN_CALLS; ++call)
        made_up_aml_raw(&predecoding_body, seven, sizeof(seven));
    made_up_aml_raw(&predecoding_body, predecoding_room_call_aml, sizeof(predecoding_room_call_aml));
    for (uint32_t pad = 0u; pad < pads; ++pad)
        made_up_aml_raw(&predecoding_body, null_name, sizeof(null_name));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, predecoding_room_loop_aml, sizeof(predecoding_room_loop_aml));
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief The home slot of a statement's records, its table and offset mixed as the cache mixes them, written here
 *        apart, so that a change of the mix breaks the premise of the test that uses it, loudly.
 */
static uint32_t predecoding_home_slot(uint16_t table, uint32_t pc)
{
    return ((pc * PREDECODING_SLOT_MULTIPLIER) ^ ((uint32_t) table * PREDECODING_TABLE_MULTIPLIER)) >> 24u;
}

/**
 * @brief Builds the record of the statement @p bytes holds at offset 0, under their length, in an empty
 *        cache of 64-bit integers.
 */
static const PredecodingRecord_t *predecoding_build(const uint8_t *bytes, uint32_t length)
{
    advanced_configuration_and_power_interface_machine_language_predecoding_open(&predecoding_cache);

    const uint16_t index = advanced_configuration_and_power_interface_machine_language_record_build(
        &predecoding_cache, bytes, 1u, 0u, length, 64u);

    return (index == 0u) ? NULL : &predecoding_cache.records[index - 1u];
}

/**
 * @brief A While that compares a local to a constant decodes into a record: its body after the
 *        predicate, its end, LLess over Local0 and 1000, and what the loop holds to run it, 2 values and
 *        2 operators (§3.6).
 */
KERNEL_TEST(a_while_that_compares_a_local_to_a_constant_decodes_into_a_record)
{
    const PredecodingRecord_t *record = predecoding_build(predecoding_while_aml, sizeof(predecoding_while_aml));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t *node = &predecoding_cache.nodes[0];

    if (!kernel_test_check(test, record != NULL, "the record is built"))
        return;
    kernel_test_check(test,
                      record->shape == PREDECODING_RECORD(WHILE) && record->after == 7u && record->end == 9u &&
                          record->limit == sizeof(predecoding_while_aml) && record->node_count == 1u,
                      "a While whose body starts at 7 and ends at 9, with one operator");
    kernel_test_check(test,
                      node->opcode == PREDECODING_OPCODE(LOGICAL_LESS) && node->negated == 0u &&
                          node->operands[0].kind == PREDECODING_OPERAND(LOCAL) && node->operands[0].slot == 0u &&
                          node->operands[1].kind == PREDECODING_OPERAND(CONSTANT) &&
                          predecoding_cache.constants[node->operands[1].index] == 1000u &&
                          record->source.kind == PREDECODING_OPERAND(RESULT) && record->source.slot == 0u,
                      "its predicate is LLess (Local0, 1000)");
    kernel_test_check(test, record->values_peak == 2u && record->operators_peak == 2u,
                      "the loop holds 2 values and 2 operators for it");
}

/**
 * @brief LNot over a comparison decodes as one operator, the comparison negated, counted (§7.2, O7);
 *        what the loop holds for it is what it holds for the two: 2 values and 3 operators.
 */
KERNEL_TEST(lnot_and_a_comparison_decode_as_one_operator)
{
    const PredecodingRecord_t *record = predecoding_build(predecoding_not_equal_aml, sizeof(predecoding_not_equal_aml));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t *node = &predecoding_cache.nodes[0];

    if (!kernel_test_check(test, record != NULL, "the record is built"))
        return;
    kernel_test_check(test,
                      record->shape == PREDECODING_RECORD(IF) && record->node_count == 1u &&
                          node->opcode == PREDECODING_OPCODE(LOGICAL_EQUAL) && node->negated == 1u &&
                          node->operands[0].kind == PREDECODING_OPERAND(ARGUMENT) && node->operands[0].slot == 1u &&
                          predecoding_cache.fused_comparisons == 1u,
                      "LNotEqual (Arg1, 5) is one LEqual, negated");
    kernel_test_check(test, record->values_peak == 2u && record->operators_peak == 3u,
                      "the loop holds 2 values and 3 operators for it");
}

/**
 * @brief A Store, an operator with a target, an Increment and a Decrement that write a local or an
 *        argument decode into records: their source, their target, and what the loop holds to run them,
 *        the source then the target as values, an operator's target as its third (§3.6).
 */
KERNEL_TEST(statements_that_write_a_local_or_an_argument_decode_into_records)
{
    const PredecodingRecord_t *sum = predecoding_build(predecoding_store_sum_aml, sizeof(predecoding_store_sum_aml));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t *node = &predecoding_cache.nodes[0];

    kernel_test_check(
        test,
        sum != NULL && sum->shape == PREDECODING_RECORD(STORE) && sum->after == 6u && sum->node_count == 1u &&
            node->opcode == PREDECODING_OPCODE(ADD) && node->operands[1].kind == PREDECODING_OPERAND(ARGUMENT) &&
            sum->source.kind == PREDECODING_OPERAND(RESULT) && sum->target.kind == PREDECODING_OPERAND(LOCAL) &&
            sum->target.slot == 2u && sum->values_peak == 3u && sum->operators_peak == 2u,
        "Local2 = Local0 + Arg1 stores an Add to Local2, holding 3 values and 2 operators");

    const PredecodingRecord_t *leaf = predecoding_build(predecoding_store_leaf_aml, sizeof(predecoding_store_leaf_aml));

    kernel_test_check(test,
                      leaf != NULL && leaf->shape == PREDECODING_RECORD(STORE) && leaf->node_count == 0u &&
                          leaf->source.kind == PREDECODING_OPERAND(ARGUMENT) && leaf->source.slot == 0u &&
                          leaf->target.kind == PREDECODING_OPERAND(LOCAL) && leaf->target.slot == 1u &&
                          leaf->values_peak == 2u && leaf->operators_peak == 1u,
                      "Local1 = Arg0 stores Arg0 itself, holding 2 values and 1 operator");

    const PredecodingRecord_t *difference =
        predecoding_build(predecoding_subtract_aml, sizeof(predecoding_subtract_aml));

    kernel_test_check(test,
                      difference != NULL && difference->shape == PREDECODING_RECORD(STORE) &&
                          difference->node_count == 1u &&
                          predecoding_cache.nodes[0].opcode == PREDECODING_OPCODE(SUBTRACT) &&
                          difference->target.kind == PREDECODING_OPERAND(LOCAL) && difference->target.slot == 0u &&
                          difference->values_peak == 3u && difference->operators_peak == 1u,
                      "Subtract (Local0, One, Local0) writes Local0, holding 3 values and 1 operator");

    const PredecodingRecord_t *up = predecoding_build(predecoding_increment_aml, sizeof(predecoding_increment_aml));

    kernel_test_check(test,
                      up != NULL && up->shape == PREDECODING_RECORD(INCREMENT) && up->after == 2u &&
                          up->target.kind == PREDECODING_OPERAND(ARGUMENT) && up->target.slot == 3u &&
                          up->values_peak == 1u && up->operators_peak == 1u,
                      "Arg3++ steps Arg3 up, holding 1 value and 1 operator");

    const PredecodingRecord_t *down = predecoding_build(predecoding_decrement_aml, sizeof(predecoding_decrement_aml));

    kernel_test_check(test,
                      down != NULL && down->shape == PREDECODING_RECORD(DECREMENT) &&
                          down->target.kind == PREDECODING_OPERAND(LOCAL) && down->target.slot == 7u,
                      "Local7-- steps Local7 down");
}

/**
 * @brief A statement no record runs stays a record the loop runs: a modulo in a statement that reads a name,
 *        whose divisor is known only once the name is read, a statement that reads five names, an operator
 *        that stores to a local inside a predicate, a constant its PkgLength cuts, a PkgLength that passes
 *        the end of the construct around it, a Continue, a Store and an operator to Debug, an Increment of
 *        a name.
 */
KERNEL_TEST(a_statement_no_record_runs_stays_for_the_loop)
{
    static const struct {
        const uint8_t *bytes;
        uint32_t length;
    } statements[] = {
        {predecoding_name_modulo_aml,  sizeof(predecoding_name_modulo_aml) },
        {predecoding_five_names_aml,   sizeof(predecoding_five_names_aml)  },
        {predecoding_inner_target_aml, sizeof(predecoding_inner_target_aml)},
        {predecoding_cut_constant,     sizeof(predecoding_cut_constant)    },
        {predecoding_crossing,         sizeof(predecoding_crossing)        },
        {predecoding_continue_aml,     sizeof(predecoding_continue_aml)    },
        {predecoding_store_debug_aml,  sizeof(predecoding_store_debug_aml) },
        {predecoding_add_debug_aml,    sizeof(predecoding_add_debug_aml)   },
        {predecoding_increment_name,   sizeof(predecoding_increment_name)  },
    };
    uint32_t general = 0u;

    for (uint32_t index = 0u; index < sizeof(statements) / sizeof(statements[0]); ++index)
    {
        const PredecodingRecord_t *record = predecoding_build(statements[index].bytes, statements[index].length);

        general += (record != NULL && record->shape == PREDECODING_RECORD(GENERAL) && record->node_count == 0u &&
                    predecoding_cache.node_count == 0u && predecoding_cache.constant_count == 0u &&
                    predecoding_cache.name_count == 0u && record->name_count == 0u) ?
                       1u :
                       0u;
    }
    kernel_test_check(test, general == sizeof(statements) / sizeof(statements[0]),
                      "each is a record the loop runs, holding no operator, no constant and no name");
}

/**
 * @brief The loops of window W4 run from records as the loop runs them, under revision 2 and revision 1;
 *        the control: the loop decodes its first turn, runs its second from its two records, which links
 *        them, and its 998 others as a tight loop.
 */
KERNEL_TEST(the_loop_of_window_w4_runs_from_records_as_the_loop_runs_it)
{
    const PredecodingRun_t run = predecoding_compare(test, common_w4_loop_aml, sizeof(common_w4_loop_aml), 2u, "\\LOOP",
                                                     PREDECODING_NO_ARGUMENT, true, "fold_revision_2");

    predecoding_compare(test, common_w4_loop_aml, sizeof(common_w4_loop_aml), 1u, "\\LOOP", PREDECODING_NO_ARGUMENT,
                        true, "fold_revision_1");
    kernel_test_check(test, run.tight_turns == 998u, "998 turns run as a tight loop");
}

/**
 * @brief A Store copies a local or an argument whole, unmasked, from a record as from the loop (§5.2):
 *        under revision 1, STOR (0x100000001) stores its argument at its second turn, from a record, and
 *        returns 0x100000001, not 1.
 */
KERNEL_TEST(a_store_from_a_record_copies_a_wide_argument_whole)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_store_whole_aml, sizeof(predecoding_store_whole_aml), 1u, "\\STOR",
                            0x100000001ull, true, "fold");

    kernel_test_check(test, run.result == 0x100000001ull, "STOR returns its argument whole");
}

/**
 * @brief A record hands back an Increment of a local that holds nothing, which the loop reads as 0,
 *        counted, and a Store to a local that holds a String, which the loop gives back: UNIN returns 3,
 *        from records as from the loop, the two statements handed back once each.
 */
KERNEL_TEST(a_record_hands_back_a_target_that_holds_no_integer)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_targets_aml, sizeof(predecoding_targets_aml), 2u,
                                                     "\\UNIN", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 3u && run.declined == 2u, "UNIN returns 3, two statements handed back");
}

/**
 * @brief A Store that a record runs first raises the most values the evaluation held, as the loop would:
 *        PEAK's Store holds its source and its target, 2 values, more than anything before it.
 */
KERNEL_TEST(a_store_first_run_from_a_record_raises_the_values_peak)
{
    predecoding_compare(test, predecoding_store_peak_aml, sizeof(predecoding_store_peak_aml), 2u, "\\PEAK",
                        PREDECODING_NO_ARGUMENT, true, "fold");
}

/**
 * @brief While whose predicate compares two leaves and whose body writes a local run as tight loops, as
 *        the loop runs them: an LNot over LGreater with an Add, a Decrement, a Store of an Add, under both
 *        widths. The control: MAIN returns 18, and each loop runs from its third turn as a tight loop, so
 *        2, 10 and 1 turns.
 */
KERNEL_TEST(while_with_a_one_statement_body_run_as_tight_loops_as_the_loop_runs_them)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_tight_aml, sizeof(predecoding_tight_aml), 2u,
                                                     "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold_revision_2");

    predecoding_compare(test, predecoding_tight_aml, sizeof(predecoding_tight_aml), 1u, "\\MAIN",
                        PREDECODING_NO_ARGUMENT, true, "fold_revision_1");
    kernel_test_check(test, run.result == 18u && run.tight_turns == 13u, "MAIN returns 18, after 13 tight turns");
}

/**
 * @brief A tight loop whose body would divide by zero hands it back with the While's block open, and the
 *        loop fails as it fails without records: MODT runs 2 tight turns, then its modulo by zero is
 *        handed back, once.
 */
KERNEL_TEST(a_tight_loop_hands_back_a_body_that_would_fail)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_tight_modulo_aml, sizeof(predecoding_tight_modulo_aml), 2u, "\\MODT",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.tight_turns == 2u && run.declined == 1u,
                      "two tight turns, then the modulo by zero handed back");
}

/**
 * @brief Every comparison, LNot over a comparison and over another operator, and the operators on two
 *        integers, shifts past the width included, run from records as the loop runs them, under both
 *        widths.
 */
KERNEL_TEST(comparisons_and_operators_run_from_records_as_the_loop_runs_them)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_comparisons_aml, sizeof(predecoding_comparisons_aml), 2u, "\\MAIN",
                            PREDECODING_NO_ARGUMENT, true, "fold_revision_2");

    predecoding_compare(test, predecoding_comparisons_aml, sizeof(predecoding_comparisons_aml), 1u, "\\MAIN",
                        PREDECODING_NO_ARGUMENT, true, "fold_revision_1");
    kernel_test_measure(test, "records_run", run.records_run);
}

/**
 * @brief An argument wider than a revision-1 table's integers is masked where an operator reads it:
 *        0x100000003 bounds MAIN's loop at 3 turns, from records as from the loop.
 */
KERNEL_TEST(an_operand_wider_than_the_width_is_masked_as_the_loop_masks_it)
{
    predecoding_compare(test, predecoding_argument_aml, sizeof(predecoding_argument_aml), 1u, "\\MAIN", 0x100000003ull,
                        true, "fold");
}

/**
 * @brief Break, Continue, a call and a Return inside nested loops leave what the loop leaves: a Return
 *        before the loop ends, at 4, and the Break at 10.
 */
KERNEL_TEST(breaks_continues_calls_and_returns_inside_loops_agree)
{
    predecoding_compare(test, predecoding_control_aml, sizeof(predecoding_control_aml), 2u, "\\MAIN", 4u, true,
                        "fold_return");
    predecoding_compare(test, predecoding_control_aml, sizeof(predecoding_control_aml), 2u, "\\MAIN", 20u, true,
                        "fold_break");
}

/**
 * @brief A record hands back a statement whose operand holds no Integer (a String, a Buffer, a
 *        reference, an uninitialized local), so that of MAIN's records only its Increment runs, and one
 *        whose modulo by zero would fail; the loop then runs them, and the failure is the loop's.
 */
KERNEL_TEST(a_record_hands_back_what_holds_no_integer_or_would_fail)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_guards_aml, sizeof(predecoding_guards_aml), 2u,
                                                     "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold_operands");
    PredecodingRun_t failed = {0};

    kernel_test_check(test, run.declined != 0u, "the statements whose operands hold no Integer are handed back");
    if (!predecoding_open(test, 2u, predecoding_guards_aml, sizeof(predecoding_guards_aml), true))
        return;
    predecoding_evaluate("\\MODZ", PREDECODING_NO_ARGUMENT, &failed);
    kernel_test_check(test, predecoding_load.predecoding.declined[PREDECODING_DECLINED(OPERATION)] == 1u,
                      "the modulo by zero is handed back, once");
    predecoding_close();
    predecoding_compare(test, predecoding_guards_aml, sizeof(predecoding_guards_aml), 2u, "\\MODZ",
                        PREDECODING_NO_ARGUMENT, true, "fold_divide_by_zero");
}

/**
 * @brief A statement a record hands back goes to the loop at once, counted once: ONCE's two Stores of a call
 *        are handed back at each of the three turns that run from records, so 6 times.
 */
KERNEL_TEST(a_statement_a_record_hands_back_is_counted_once)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_once_aml, sizeof(predecoding_once_aml), 2u,
                                                     "\\ONCE", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.declined == 6u,
                      "each Store of a call is handed back once per turn, at the back-edge or where a statement runs");
    kernel_test_measure(test, "declined", run.declined);
}

/**
 * @brief Records built under 64-bit integers are emptied when the DSDT makes them 32-bit: WIDE, in an SSDT
 *        declared first, counts no turn, then three once a revision-1 DSDT is declared, with records as
 *        without.
 */
KERNEL_TEST(records_of_one_integer_width_are_emptied_when_the_width_changes)
{
    uint32_t folds[2] = {0u, 0u};
    uint64_t counts[2][2] = {
        {0u, 0u},
        {0u, 0u}
    };

    for (uint32_t predecodes = 0u; predecodes < 2u; ++predecodes)
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result = {0};
        PredecodingRun_t run = {0};

        if (!predecoding_open_empty(test, predecodes != 0u))
            return;
        predecoding_declare(predecoding_table, "SSDT", 2u, predecoding_width_aml, sizeof(predecoding_width_aml));
        advanced_configuration_and_power_interface_machine_language_evaluate(&predecoding_load, "\\WIDE", NULL, 0u,
                                                                             &result);
        counts[predecodes][0] = result.payload;
        predecoding_declare(predecoding_second_table, "DSDT", 1u, common_noop_aml, sizeof(common_noop_aml));
        predecoding_evaluate("\\WIDE", PREDECODING_NO_ARGUMENT, &run);
        advanced_configuration_and_power_interface_machine_language_evaluate(&predecoding_load, "\\WIDE", NULL, 0u,
                                                                             &result);
        counts[predecodes][1] = result.payload;
        folds[predecodes] = run.fold;
        predecoding_close();
    }
    kernel_test_check(test, counts[0][0] == 0u && counts[0][1] == 3u, "the loop counts no turn, then three");
    kernel_test_check(test, counts[1][0] == counts[0][0] && counts[1][1] == counts[0][1] && folds[1] == folds[0],
                      "records count as the loop counts");
    kernel_test_measure_hexadecimal(test, "fold", folds[1]);
}

/**
 * @brief A loop whose statements fill the cache empties it, counted, and builds no more record for that
 *        loop: its statements are handed back, and it leaves what the loop leaves. The inner loop fills it
 *        first; the outer one, which becomes the frame's loop at its back-edge, fills it once more, then
 *        builds nothing either (§6.1).
 */
KERNEL_TEST(a_full_cache_is_emptied_and_its_loop_builds_no_more_record)
{
    predecoding_write_full_loop();

    if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
        return;

    const PredecodingRun_t run = predecoding_compare(test, predecoding_method.bytes, predecoding_method.length, 2u,
                                                     "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.flushes == 2u,
                      "the cache is emptied once per loop that fills it: the inner, the outer");
    kernel_test_check(test, run.built == 256u, "each loop builds the 128 records it fills the cache with, then none");
    kernel_test_check(test, run.records_peak == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORDS,
                      "the cache held its 128 records at most");
    kernel_test_measure(test, "declined", run.declined);
}

/**
 * @brief A loop whose statements fill the cache's operators and constants before its records empties it, counted,
 *        as a full cache of records does, and builds no more record (§6.1): MAIN's 90 Ifs hold 270 of each, past
 *        the 256, with records as without.
 */
KERNEL_TEST(full_operators_or_constants_empty_the_cache)
{
    predecoding_write_pools();

    if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
        return;

    const PredecodingRun_t run = predecoding_compare(test, predecoding_method.bytes, predecoding_method.length, 2u,
                                                     "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.flushes == 1u, "the cache is emptied once, its operators full");
    kernel_test_measure(test, "record_operators_peak", run.nodes_peak);
    kernel_test_measure(test, "record_constants_peak", run.constants_peak);
    kernel_test_check(test,
                      run.nodes_peak == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NODES &&
                          run.constants_peak ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_CONSTANTS,
                      "the records held their 256 operators and 256 constants at most");
}

/**
 * @brief A tight loop run from records built under fewer pending operators counts the operators its body holds above
 *        those it finds (§6.1): LOOP's second call, under four pending Adds, leaves the operators' peak the loop
 * leaves.
 */
KERNEL_TEST(a_tight_loop_run_deeper_counts_its_body_s_operators)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_tight_loop_deeper_aml, sizeof(predecoding_tight_loop_deeper_aml), 2u,
                            "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test,
                      run.status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          run.result == 9u && run.tight_turns != 0u,
                      "MAIN gives 3 + 6, the second call's loop run as a tight loop");
}

/**
 * @brief A loop whose statements read more names than the records of a load hold empties the cache, counted, and
 *        builds no more record (§6.1): MAIN's 65 statements read one name each, past the 64, with records as without.
 */
KERNEL_TEST(full_names_empty_the_cache)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_names_full_aml, sizeof(predecoding_names_full_aml), 2u, "\\MAIN",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(
        test, run.status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK && run.result == 130u,
        "MAIN adds the 65 names twice");
    kernel_test_check(test,
                      run.flushes == 1u &&
                          run.names_peak ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_RECORD_NAMES,
                      "the cache is emptied once, its 64 names read");
}

/**
 * @brief A record hands back an operand that holds a reference, which the loop goes through: CALL's three Ifs,
 *        first reached from records, read a reference to a name, to an element and in an argument, with records
 *        as without.
 */
KERNEL_TEST(a_record_hands_back_an_operand_that_holds_a_reference)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_references_aml, sizeof(predecoding_references_aml), 2u, "\\CALL",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.declined != 0u, "the Ifs over a reference are handed back");
}

/**
 * @brief A record hands back a statement that needs more operators, or more values, than the evaluation has
 *        left, and the loop then fails as it fails without records: MAIN with the operator stack nearly full,
 *        then with the value stack nearly full.
 */
KERNEL_TEST(a_record_hands_back_what_the_stacks_have_no_room_for)
{
    predecoding_compare(test, predecoding_operators_nearly_full_aml, sizeof(predecoding_operators_nearly_full_aml), 2u,
                        "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold_operators");
    predecoding_write_values_nearly_full();
    if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
        return;
    predecoding_compare(test, predecoding_method.bytes, predecoding_method.length, 2u, "\\MAIN",
                        PREDECODING_NO_ARGUMENT, true, "fold_values");
}

/**
 * @brief A record is found by its table and its offset, never by its offset alone: with the record of offset 8
 *        of table 1 in the slot after the home slot of offset 8 of table 22, which offset 131 of table 1 fills,
 *        offset 8 of table 22 finds no record.
 */
KERNEL_TEST(a_record_is_found_by_its_table_and_its_offset)
{
    PredecodingCache_t *cache = &predecoding_cache;
    uint16_t filler = 0u;
    uint16_t record = 0u;

    for (uint32_t index = 0u; index < sizeof(predecoding_noops); ++index)
        predecoding_noops[index] = 0xA3u;
    if (!kernel_test_check(test,
                           predecoding_home_slot(1u, PREDECODING_FIND_PC) ==
                                   predecoding_home_slot(PREDECODING_FIND_TABLE, PREDECODING_FIND_PC) + 1u &&
                               predecoding_home_slot(1u, PREDECODING_FIND_FILLER) ==
                                   predecoding_home_slot(PREDECODING_FIND_TABLE, PREDECODING_FIND_PC),
                           "the premise: the two home slots meet, the first filled"))
        return;
    advanced_configuration_and_power_interface_machine_language_predecoding_open(cache);
    filler = advanced_configuration_and_power_interface_machine_language_record_build(
        cache, predecoding_noops, 1u, PREDECODING_FIND_FILLER, sizeof(predecoding_noops), 64u);
    record = advanced_configuration_and_power_interface_machine_language_record_build(
        cache, predecoding_noops, 1u, PREDECODING_FIND_PC, sizeof(predecoding_noops), 64u);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_record_find(
                          cache, 1u, PREDECODING_FIND_FILLER) == filler &&
                          advanced_configuration_and_power_interface_machine_language_record_find(
                              cache, 1u, PREDECODING_FIND_PC) == record,
                      "each record of table 1 is found by its offset");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_record_find(
                          cache, PREDECODING_FIND_TABLE, PREDECODING_FIND_PC) == 0u,
                      "the same offset of table 22 finds none");
}

/**
 * @brief A tight loop that a deeper call enters at once, its records linked by an earlier one, holds the values of
 *        its body as the loop holds them (§3.6): PKTL returns 6, with records as without, after tight turns.
 */
KERNEL_TEST(a_tight_loop_entered_deeper_holds_its_body_s_values)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_deeper_tight_aml, sizeof(predecoding_deeper_tight_aml), 2u, "\\PKTL",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 6u && run.tight_turns != 0u, "PKTL returns 6, after tight turns");
}

/**
 * @brief A While runs as a tight loop only when its predicate compares two leaves and its body writes: a Break as
 *        the body (BRKB), an operator as the predicate (ANDW), two LNot over a comparison (NNOT) run no tight turn,
 *        with records as without; a bound that becomes a String between two entries is handed back at the second
 *        (TOPR), after tight turns at the first.
 */
KERNEL_TEST(only_a_comparison_of_two_leaves_over_a_writing_body_is_a_tight_loop)
{
    const PredecodingRun_t breaks = predecoding_compare(
        test, predecoding_break_body_aml, sizeof(predecoding_break_body_aml), 2u, "\\BRKB", 7u, true, "fold_break");
    const PredecodingRun_t ands =
        predecoding_compare(test, predecoding_and_predicate_aml, sizeof(predecoding_and_predicate_aml), 2u, "\\ANDW",
                            PREDECODING_NO_ARGUMENT, true, "fold_and");
    const PredecodingRun_t nots = predecoding_compare(test, predecoding_two_lnot_aml, sizeof(predecoding_two_lnot_aml),
                                                      2u, "\\NNOT", PREDECODING_NO_ARGUMENT, true, "fold_lnot");
    const PredecodingRun_t strings =
        predecoding_compare(test, predecoding_string_bound_aml, sizeof(predecoding_string_bound_aml), 2u, "\\TOPR",
                            PREDECODING_NO_ARGUMENT, true, "fold_string");

    kernel_test_check(test, breaks.result == 7u && breaks.tight_turns == 0u, "BRKB returns 7, no tight turn");
    kernel_test_check(test, ands.result == 0u && ands.tight_turns == 0u, "ANDW returns 0, no tight turn");
    kernel_test_check(test, nots.result == 5u && nots.tight_turns == 0u, "NNOT returns 5, no tight turn");
    kernel_test_check(test, strings.result == 9u && strings.tight_turns != 0u, "TOPR returns 9, after tight turns");
}

/**
 * @brief A tight loop whose body needs more values than the evaluation has left is handed back, and the loop fails
 *        as it fails without records (§6.1): ROOM's inner While, linked by a first call, entered from records two
 *        values short of its body in a second, deeper by 1,014 values and some Adds, the loop then failing with
 *        limit_values, with records as without. The control: one depth of those tried fails so.
 */
KERNEL_TEST(a_tight_loop_whose_body_has_no_room_is_handed_back)
{
    static const char *const keys[] = {"fold_5_pads", "fold_6_pads", "fold_7_pads", "fold_8_pads", "fold_9_pads"};
    uint32_t short_of_room = 0u;

    for (uint32_t pads = 5u; pads < 10u; ++pads)
    {
        predecoding_write_room(pads);
        if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
            return;

        const PredecodingRun_t run = predecoding_compare(test, predecoding_method.bytes, predecoding_method.length, 2u,
                                                         "\\MAIN", PREDECODING_NO_ARGUMENT, true, keys[pads - 5u]);

        short_of_room +=
            (run.status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_VALUES) ? 1u : 0u;
    }
    kernel_test_check(test, short_of_room != 0u, "a depth leaves the body no room, and the loop fails there");
}

/**
 * @brief A statement handed back at once is searched for its record once, then handed back without a search (§3.6):
 *        REFU's While, handed back at its back-edges, and its body, at the turns after the first, are searched once
 *        each, and REFU returns 100 with records as without, no record run.
 */
KERNEL_TEST(a_statement_handed_back_at_once_is_searched_once)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_refused_loop_aml, sizeof(predecoding_refused_loop_aml), 2u, "\\REFU",
                            PREDECODING_NO_ARGUMENT, false, "fold");

    kernel_test_check(test, run.result == 100u && run.declined != 0u, "REFU returns 100, its statements handed back");
    kernel_test_check(test, run.searches == 2u, "each of the two statements is searched once");
    kernel_test_measure(test, "searches", run.searches);
}

/**
 * @brief A statement records reach after a statement handed back, or after a back-edge, is searched for its record
 *        once (§3.6): NEXT's While, call and Increment are searched once each over 100 turns, its call handed back at
 *        the 99 turns after the first, and NEXT returns 100 with records as without.
 */
KERNEL_TEST(a_statement_after_one_handed_back_is_searched_once)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_refused_statement_aml, sizeof(predecoding_refused_statement_aml), 2u,
                            "\\NEXT", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 100u && run.declined == 99u,
                      "NEXT returns 100, its call handed back 99 times");
    kernel_test_check(test, run.searches == 3u, "each of the three statements is searched once");
    kernel_test_measure(test, "searches", run.searches);
}

/**
 * @brief Two statements whose offsets share a recent slot each find their own record (§3.6): SLOT's call, handed back
 *        after the 9 back-edges that go on, and its Buffer at the 9 turns after the first, 18 times, its Increment run
 *        at the 9 turns after the first and its While at the 10 back-edges, 19 records, the four statements searched
 *        once each, and SLOT returns 10 with records as without.
 */
KERNEL_TEST(statements_that_share_a_recent_slot_find_their_own_records)
{
    static const uint8_t call[] = {0x70u, 'I', 'N', 'C', 'R', 0x62u, 0x62u};
    static const uint8_t increment[] = {0x75u, 0x60u};

    predecoding_write_shared_slot();
    if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
        return;

    const uint32_t call_offset =
        predecoding_offset_of(predecoding_method.bytes, predecoding_method.length, call, sizeof(call));
    const uint32_t increment_offset =
        predecoding_offset_of(predecoding_method.bytes, predecoding_method.length, increment, sizeof(increment));

    if (!kernel_test_check(test,
                           increment_offset < predecoding_method.length && increment_offset > call_offset &&
                               (increment_offset - call_offset) %
                                       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_RECENT_RECORDS ==
                                   0u,
                           "the premise: the call and the Increment share a recent slot"))
        return;

    const PredecodingRun_t run = predecoding_compare(test, predecoding_method.bytes, predecoding_method.length, 2u,
                                                     "\\SLOT", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 10u && run.records_run == 19u && run.declined == 18u,
                      "SLOT returns 10, 19 records run and 18 statements handed back");
    kernel_test_check(test, run.searches == 4u, "each of the four statements is searched once");
}

/**
 * @brief A While whose statements all went to the loop for a whole turn, each going on to the next, asks its records
 *        no more (§3.6): REFU's While goes to the loop at its first three back-edges and its body at the second and
 *        third turns, 5 times, then its turns run without records, and REFU returns 100 with records as without.
 */
KERNEL_TEST(a_loop_whose_statements_all_go_to_the_loop_stops_asking)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_refused_loop_aml, sizeof(predecoding_refused_loop_aml), 2u, "\\REFU",
                            PREDECODING_NO_ARGUMENT, false, "fold");

    kernel_test_check(test, run.result == 100u && run.declined == 5u && run.loops == 1u,
                      "REFU returns 100, its statements handed back 5 times, then its loop whole");
    kernel_test_measure(test, "declined", run.declined);
}

/**
 * @brief A While whose turn may run a record keeps asking its records (§3.6): BRAN's If, which no record runs, decides
 *        whether its Increment runs, which a record runs at the 49 turns from the 52nd, and MIXD's Increment runs from
 *        a record at the 99 turns after the first; BRAN returns 49 and MIXD 100, with records as without.
 */
KERNEL_TEST(a_loop_whose_turn_may_run_a_record_keeps_asking)
{
    const PredecodingRun_t branching =
        predecoding_compare(test, predecoding_branching_loop_aml, sizeof(predecoding_branching_loop_aml), 2u, "\\BRAN",
                            PREDECODING_NO_ARGUMENT, true, "fold_branching");
    const PredecodingRun_t mixed =
        predecoding_compare(test, predecoding_mixed_loop_aml, sizeof(predecoding_mixed_loop_aml), 2u, "\\MIXD",
                            PREDECODING_NO_ARGUMENT, true, "fold_mixed");

    kernel_test_check(test, branching.result == 49u && branching.records_run == 49u && branching.loops == 0u,
                      "BRAN returns 49, its Increment run from records 49 times, its loop never handed back whole");
    kernel_test_check(test, mixed.result == 100u && mixed.records_run == 99u && mixed.loops == 0u,
                      "MIXD returns 100, its Increment run from records 99 times, its loop never handed back whole");
}

/**
 * @brief A While is handed back whole only while it is the frame's loop (§3.6): NEST's inner While, the frame's loop in
 *        its first run, is handed back whole at its third back-edge after 5 statements; in each of the two runs after
 *        it, inside the outer loop the frame's loop has become, the While goes to the loop where it starts and at its
 *        10 back-edges, and its call at its 10 turns, and RSET once more after the outer loop's records, 49 times in
 *        all; the outer While, Increment and Store run from records at its first two back-edges and its While at the
 *        third, 7 times; the six statements are searched once each, and NEST returns 10 with records as without.
 */
KERNEL_TEST(a_loop_is_handed_back_whole_only_while_it_is_the_frame_s_loop)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_nested_refused_loop_aml, sizeof(predecoding_nested_refused_loop_aml), 2u,
                            "\\NEST", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 10u && run.declined == 49u && run.records_run == 7u && run.loops == 1u,
                      "NEST returns 10, 49 statements handed back, 7 records run, one loop handed back whole");
    kernel_test_check(test, run.searches == 6u, "each of the six statements is searched once");
}

/**
 * @brief A statement evicted from its recent slot takes it back when searched (§3.6): EVIC's Increment of Local1, whose
 *        slot the call in its If takes at the second turn, is searched once more at the third, then found in its slot
 *        at the seven after it; the seven statements, eight searches, and EVIC returns 10 with records as without.
 */
KERNEL_TEST(a_record_evicted_from_its_recent_slot_takes_it_back)
{
    predecoding_write_evicted();
    if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
        return;

    const PredecodingRun_t run = predecoding_compare(test, predecoding_method.bytes, predecoding_method.length, 2u,
                                                     "\\EVIC", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 10u, "EVIC returns 10");
    kernel_test_check(test, run.searches == 8u, "seven statements, the evicted one searched twice");
}

/**
 * @brief A statement finds the record of its own table, never one at the same offset of another (§3.6): LPA_ of the
 *        DSDT adds and LPB_ of the SSDT subtracts at the same offsets, and in one load they return 5 and -5, with
 *        records as without, both after records ran.
 */
KERNEL_TEST(a_statement_finds_the_record_of_its_own_table)
{
    PredecodingRun_t runs[2][2] = {
        {{0}, {0}},
        {{0}, {0}}
    };

    for (uint32_t predecodes = 0u; predecodes < 2u; ++predecodes)
    {
        if (!predecoding_open_empty(test, predecodes != 0u))
            return;
        predecoding_declare(predecoding_table, "DSDT", 2u, predecoding_adding_table_aml,
                            sizeof(predecoding_adding_table_aml));
        predecoding_declare(predecoding_second_table, "SSDT", 2u, predecoding_subtracting_table_aml,
                            sizeof(predecoding_subtracting_table_aml));
        predecoding_evaluate("\\LPA_", PREDECODING_NO_ARGUMENT, &runs[predecodes][0]);
        predecoding_evaluate("\\LPB_", PREDECODING_NO_ARGUMENT, &runs[predecodes][1]);
        predecoding_close();
    }
    kernel_test_check(test, runs[0][0].result == 5u && runs[0][1].result == ~0ull - 4u, "LPA_ returns 5, LPB_ -5");
    kernel_test_check(test, runs[1][0].fold == runs[0][0].fold && runs[1][1].fold == runs[0][1].fold,
                      "the records leave what the loop leaves, in both tables");
    kernel_test_check(test, runs[1][0].records_run != 0u && runs[1][1].records_run > runs[1][0].records_run,
                      "records ran in both tables");
    kernel_test_measure_hexadecimal(test, "fold", runs[1][1].fold);
}

/**
 * @brief A frame's loop is handed back whole on its own turns alone, never on what a deeper frame watched (§3.6):
 *        RECU (1) calls RECU (0), which runs the same While and leaves it watched at its last back-edge; RECU (1)'s
 *        While, whose If may not go on, is never handed back whole, and RECU returns 1 with records as without.
 */
KERNEL_TEST(a_loop_is_handed_back_whole_on_its_own_frame_s_turns)
{
    const PredecodingRun_t run = predecoding_compare(
        test, predecoding_recursive_loop_aml, sizeof(predecoding_recursive_loop_aml), 2u, "\\RECU", 1u, true, "fold");

    kernel_test_check(test, run.result == 1u && run.loops == 0u, "RECU returns 1, its loop never handed back whole");
}

/**
 * @brief Names in an expression decode into named operands (§3.6): `If (LEqual (And (STS_, 0x10), FLAG))` reads
 *        STS_, then FLAG once the loop holds 3 values for it, the And's operands and NullName; `Local1 =
 *        \_SB.PCI0.ECRD` stores a path of three segments from the root. A record is 40 bytes, a name 28.
 */
KERNEL_TEST(names_in_an_expression_decode_into_named_operands)
{
    const PredecodingRecord_t *record =
        predecoding_build(predecoding_name_compare_aml, sizeof(predecoding_name_compare_aml));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t *nodes = predecoding_cache.nodes;
    const PredecodingName_t *names = predecoding_cache.names;

    kernel_test_measure(test, "record_bytes", (uint32_t) sizeof(PredecodingRecord_t));
    kernel_test_measure(test, "name_bytes", (uint32_t) sizeof(PredecodingName_t));
    kernel_test_check(test, sizeof(PredecodingRecord_t) == 40u && sizeof(PredecodingName_t) == 28u,
                      "a record is 40 bytes and a name 28, on both targets");
    if (!kernel_test_check(test, record != NULL, "the record is built"))
        return;
    kernel_test_check(test,
                      record->shape == PREDECODING_RECORD(IF) && record->node_count == 2u && record->name_count == 2u &&
                          record->first_name == 0u && nodes[0].opcode == PREDECODING_OPCODE(AND) &&
                          nodes[0].operands[0].kind == PREDECODING_OPERAND(NAME) && nodes[0].operands[0].slot == 0u &&
                          nodes[0].operands[0].index == 0u && nodes[1].opcode == PREDECODING_OPCODE(LOGICAL_EQUAL) &&
                          nodes[1].operands[1].kind == PREDECODING_OPERAND(NAME) && nodes[1].operands[1].slot == 1u &&
                          nodes[1].operands[1].index == 1u,
                      "an If over LEqual of an And of STS_ and FLAG, the names its first and second");
    kernel_test_check(test,
                      names[0].name.segments == 4u && names[0].name.segment_count == 1u && names[0].after == 8u &&
                          names[0].values_before == 0u && names[1].name.segments == 11u && names[1].after == 15u &&
                          names[1].values_before == 3u && record->values_peak == 3u && record->operators_peak == 3u,
                      "STS_ is read first, FLAG once the loop holds 3 values; the If holds 3 values and 3 operators");

    const PredecodingRecord_t *path = predecoding_build(predecoding_name_path_aml, sizeof(predecoding_name_path_aml));

    kernel_test_check(test,
                      path != NULL && path->shape == PREDECODING_RECORD(STORE) && path->after == 17u &&
                          path->source.kind == PREDECODING_OPERAND(NAME) &&
                          path->target.kind == PREDECODING_OPERAND(LOCAL) && path->target.slot == 1u &&
                          path->name_count == 1u && names[0].name.root && names[0].name.segment_count == 3u &&
                          names[0].name.segments == 4u && names[0].after == 16u && path->values_peak == 2u,
                      "Local1 = \\_SB.PCI0.ECRD stores a path of three segments from the root");
}

/**
 * @brief A loop that reads fields and an Integer by name runs from records as the loop runs it (§3.6), the
 *        field written through the loop at each turn: MAIN returns 23. The control: only the five writes to
 *        CNT_ are handed back, and the 18 reads, three per turn, each go by one datum (O12), with records as
 *        without.
 */
KERNEL_TEST(a_loop_that_reads_fields_runs_from_records_as_the_loop_runs_it)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_fields_aml, sizeof(predecoding_fields_aml), 2u,
                                                     "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 23u, "MAIN returns 23");
    kernel_test_check(test, run.declined == 5u && run.declined_names == 0u,
                      "only the writes to CNT_ are handed back, one per turn run from records");
    kernel_test_check(test, run.field_reads == 18u && run.field_fast_path == 18u,
                      "the 18 field reads each go by one datum");
    kernel_test_measure(test, "field_fast_path", run.field_fast_path);
}

/**
 * @brief A record stores an Integer it reads by name whole, unmasked, as a Store copies a value (§5.2), and
 *        masks it where an operator reads it: under revision 1, STOR (0x100000001) returns 0x100000001, its
 *        two statements run from records, none handed back.
 */
KERNEL_TEST(a_store_from_a_record_copies_a_named_integer_whole)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_named_whole_aml, sizeof(predecoding_named_whole_aml), 1u, "\\STOR",
                            0x100000001ull, true, "fold");

    kernel_test_check(test, run.result == 0x100000001ull, "STOR returns its argument whole");
    kernel_test_check(test, run.declined == 0u, "no statement is handed back");
}

/**
 * @brief A record reads the type of the object its name names at each turn: once CopyObject makes NAMX a String,
 *        the Add that reads it is handed back, and the loop converts it. COPY returns 24; the control: the
 *        CopyObject and the two Adds after it are handed back, those two for their name.
 */
KERNEL_TEST(a_name_whose_object_becomes_a_string_is_handed_back)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_copied_aml, sizeof(predecoding_copied_aml), 2u,
                                                     "\\COPY", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 24u, "COPY returns 24");
    kernel_test_check(test, run.declined == 3u && run.declined_names == 2u,
                      "the CopyObject, then the two Adds that read a String, are handed back");
}

/**
 * @brief A name the loop declares masks the one its records read, and goes when the method returns (§3.6):
 *        MASK, evaluated twice in one load, returns 202 both times, with records as without. The control: each
 *        evaluation hands back its Name alone, and every name read finds the node its record checked.
 */
KERNEL_TEST(a_name_the_loop_declares_masks_the_one_its_records_read)
{
    PredecodingRun_t runs[2][2] = {
        {{0}, {0}},
        {{0}, {0}}
    };

    for (uint32_t predecodes = 0u; predecodes < 2u; ++predecodes)
    {
        if (!predecoding_open(test, 2u, predecoding_masked_aml, sizeof(predecoding_masked_aml), predecodes != 0u))
            return;
        predecoding_evaluate("\\MASK", PREDECODING_NO_ARGUMENT, &runs[predecodes][0]);
        predecoding_evaluate("\\MASK", PREDECODING_NO_ARGUMENT, &runs[predecodes][1]);
        predecoding_close();
    }
    kernel_test_check(test, runs[0][0].result == 202u && runs[0][1].result == 202u, "MASK returns 202 twice");
    kernel_test_check(test, runs[1][0].fold == runs[0][0].fold && runs[1][1].fold == runs[0][1].fold,
                      "the records leave what the loop leaves, both times");
    kernel_test_check(test, runs[1][0].declined == 1u && runs[1][1].declined == 2u,
                      "each evaluation hands its Name back, and nothing else");
    kernel_test_check(test, runs[1][1].stale_names == 0u, "every name read finds the node its record checked");
    kernel_test_measure_hexadecimal(test, "fold", runs[1][1].fold);
}

/**
 * @brief A table the loop loads masks the name its records read (§3.6): \XDEV.MAIN returns 202, with records
 *        as without. The control: the Load alone is handed back, and every name read finds the node its
 *        record checked.
 */
KERNEL_TEST(a_table_the_loop_loads_masks_the_name_its_records_read)
{
    PredecodingRun_t runs[2] = {{0}, {0}};

    for (uint32_t predecodes = 0u; predecodes < 2u; ++predecodes)
    {
        const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = made_up_definition_block(
            predecoding_second_table, "SSDT", 2u, predecoding_loaded_aml, sizeof(predecoding_loaded_aml));

        if (!predecoding_open(test, 2u, predecoding_loads_aml, sizeof(predecoding_loads_aml), predecodes != 0u))
            return;
        predecoding_poke(PREDECODING_LOADED_ADDRESS, predecoding_second_table, header->length);
        predecoding_evaluate("\\XDEV.MAIN", PREDECODING_NO_ARGUMENT, &runs[predecodes]);
        predecoding_close();
    }
    kernel_test_check(test, runs[0].result == 202u, "MAIN returns 202");
    kernel_test_check(test, runs[1].fold == runs[0].fold, "the records leave what the loop leaves");
    kernel_test_check(test, runs[1].declined == 1u && runs[1].stale_names == 0u,
                      "the Load alone is handed back, and every name read finds the node its record checked");
    kernel_test_measure_hexadecimal(test, "fold", runs[1].fold);
}

/**
 * @brief A field read that fails in a statement a record runs fails as it fails in the loop, its status
 *        returned, after the record's first effect (§3.6): FAIL fails with space_unsupported, its values peak
 *        at the 3 the loop held before the read, with records as without. The control: no statement is handed
 *        back.
 */
KERNEL_TEST(a_field_read_that_fails_in_a_record_fails_as_in_the_loop)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_failing_field_aml, sizeof(predecoding_failing_field_aml), 2u, "\\FAIL",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.status == PREDECODING_STATUS(SPACE_UNSUPPORTED), "FAIL fails with space_unsupported");
    kernel_test_check(test, run.declined == 0u, "no statement is handed back: the record reads the field itself");
}

/**
 * @brief A record hands back a statement whose name names a method, which the loop calls, or a field whose read
 *        gives a Buffer: DECL returns 3, with records as without. The control: the Add and the Store are handed
 *        back at each of the two turns run from records, for their name.
 */
KERNEL_TEST(a_record_hands_back_a_name_that_names_a_method_or_a_wide_field)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_unread_names_aml, sizeof(predecoding_unread_names_aml), 1u, "\\DECL",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 3u, "DECL returns 3");
    kernel_test_check(test, run.declined == 4u && run.declined_names == 4u,
                      "the Add of a method and the Store of a wide field are handed back, twice each");
}

/**
 * @brief A record hands back a field whose PCI_Config region waits for its attach, which the loop makes: PCIR
 *        reads VID0 first at its second turn, from records, with records as without. The control: that read
 *        alone is handed back, the third turn's runs from its record.
 */
KERNEL_TEST(a_record_hands_back_a_field_whose_region_waits_for_its_attach)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_unattached_aml, sizeof(predecoding_unattached_aml), 2u, "\\PCIR",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.declined == 1u && run.declined_names == 1u,
                      "the first read of VID0 is handed back, for its attach");
}

/**
 * @brief A record masks an Integer it reads by name where an operator reads it, as the loop masks it (§5.2): under
 *        revision 1, MASK (0x100000001) finds WIDE equal to 1 at both turns, the second from its record, and returns
 *        2, with records as without, nothing handed back.
 */
KERNEL_TEST(a_name_an_operator_reads_is_masked_as_the_loop_masks_it)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_named_masked_aml, sizeof(predecoding_named_masked_aml), 1u, "\\MASK",
                            0x100000001ull, true, "fold");

    kernel_test_check(test, run.result == 2u && run.declined == 0u, "MASK returns 2, nothing handed back");
}

/**
 * @brief A While whose predicate reads a name never runs as a tight loop, which would resolve that name once (§3.6):
 *        TIGN returns 5 with records as without, its lookups counted at each turn. The control: no tight turn.
 */
KERNEL_TEST(a_while_whose_predicate_reads_a_name_is_no_tight_loop)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_tight_name_aml, sizeof(predecoding_tight_name_aml), 2u, "\\TIGN",
                            PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 5u && run.tight_turns == 0u, "TIGN returns 5, no tight turn");
}

/**
 * @brief An External that a declaration fills changes what a name resolves to, and the generation moves with it
 *        (§3.6): MAIN reads \FOO_.BAR_ from 1 to 100 once a table it loads makes \FOO_ an Alias of \DEVB, with records
 *        as without. The control: every name read finds the node its record checked.
 */
KERNEL_TEST(an_external_an_alias_fills_moves_what_a_record_reads)
{
    PredecodingRun_t runs[2] = {{0}, {0}};

    for (uint32_t predecodes = 0u; predecodes < 2u; ++predecodes)
    {
        const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = made_up_definition_block(
            predecoding_second_table, "SSDT", 2u, predecoding_alias_loaded_aml, sizeof(predecoding_alias_loaded_aml));

        if (!predecoding_open(test, 2u, predecoding_alias_fills, sizeof(predecoding_alias_fills), predecodes != 0u))
            return;
        predecoding_poke(PREDECODING_LOADED_ADDRESS, predecoding_second_table, header->length);
        predecoding_evaluate("\\MAIN", PREDECODING_NO_ARGUMENT, &runs[predecodes]);
        predecoding_close();
    }
    kernel_test_check(test, runs[0].result == 202u, "MAIN returns 202");
    kernel_test_check(test, runs[1].fold == runs[0].fold && runs[1].stale_names == 0u,
                      "the records leave what the loop leaves, every name read finding the node its record checked");
    kernel_test_measure_hexadecimal(test, "fold", runs[1].fold);
}
