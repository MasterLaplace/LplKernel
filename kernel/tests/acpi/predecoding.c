#include "made_up_aml.h"
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

static uint8_t predecoding_table[PREDECODING_TABLE_CAPACITY];
static uint8_t predecoding_second_table[PREDECODING_TABLE_CAPACITY];
static PredecodingCache_t predecoding_cache;
static MadeUpAml_t predecoding_method;
static MadeUpAml_t predecoding_outer;
static MadeUpAml_t predecoding_inner;
static MadeUpAml_t predecoding_body;
static uint8_t predecoding_noops[256];

/**
 * @brief W4's loop, as LOOP: `Local0 = 0; While (Local0 < 1000) { Local0++ }; Return (Local0)`.
 */
static const uint8_t predecoding_w4[] = {
    0x14u, 0x14u, 'L',   'O',   'O',   'P',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
    0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief Every comparison and operator a record runs, as MAIN: 20 turns of `Local0++`, then Ifs on
 *        `Local0 > 15`, `== 7`, `!= 9`, `<= 3`, `>= 18`, `!(Local0 & 1)`, `(Local0 << 62) > 0`,
 *        `(Local0 << 64) == 0` and `((Local0 * 3) - 1) % 5 == 0`, each adding its own power of ten to
 *        Local1, which MAIN returns; at the fifth turn only, an If whose predicate `1 + (2 + (3 + (4 +
 *        Local0))) == 15` holds 6 values, more than the loop held before, first run from its record.
 */
static const uint8_t predecoding_comparisons[] = {
    0x14u, 0x4Eu, 0x0Bu, 'M',   'A',   'I',   'N',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u, 0x4Eu,
    0x0Au, 0x95u, 0x60u, 0x0Au, 0x14u, 0x75u, 0x60u, 0xA0u, 0x07u, 0x94u, 0x60u, 0x0Au, 0x0Fu, 0x75u, 0x61u, 0xA0u,
    0x0Au, 0x93u, 0x60u, 0x0Au, 0x07u, 0x72u, 0x61u, 0x0Au, 0x64u, 0x61u, 0xA0u, 0x0Cu, 0x92u, 0x93u, 0x60u, 0x0Au,
    0x09u, 0x72u, 0x61u, 0x0Bu, 0xE8u, 0x03u, 0x61u, 0xA0u, 0x0Cu, 0x92u, 0x94u, 0x60u, 0x0Au, 0x03u, 0x72u, 0x61u,
    0x0Bu, 0x10u, 0x27u, 0x61u, 0xA0u, 0x0Eu, 0x92u, 0x95u, 0x60u, 0x0Au, 0x12u, 0x72u, 0x61u, 0x0Cu, 0xA0u, 0x86u,
    0x01u, 0x00u, 0x61u, 0xA0u, 0x0Eu, 0x92u, 0x7Bu, 0x60u, 0x01u, 0x00u, 0x72u, 0x61u, 0x0Cu, 0x40u, 0x42u, 0x0Fu,
    0x00u, 0x61u, 0xA0u, 0x10u, 0x94u, 0x79u, 0x60u, 0x0Au, 0x3Eu, 0x00u, 0x00u, 0x72u, 0x61u, 0x0Cu, 0x80u, 0x96u,
    0x98u, 0x00u, 0x61u, 0xA0u, 0x10u, 0x93u, 0x79u, 0x60u, 0x0Au, 0x40u, 0x00u, 0x00u, 0x72u, 0x61u, 0x0Cu, 0x00u,
    0xE1u, 0xF5u, 0x05u, 0x61u, 0xA0u, 0x17u, 0x93u, 0x85u, 0x74u, 0x77u, 0x60u, 0x0Au, 0x03u, 0x00u, 0x01u, 0x00u,
    0x0Au, 0x05u, 0x00u, 0x00u, 0x72u, 0x61u, 0x0Cu, 0x00u, 0xCAu, 0x9Au, 0x3Bu, 0x61u, 0xA0u, 0x20u, 0x93u, 0x60u,
    0x0Au, 0x05u, 0xA0u, 0x1Au, 0x93u, 0x72u, 0x0Au, 0x01u, 0x72u, 0x0Au, 0x02u, 0x72u, 0x0Au, 0x03u, 0x72u, 0x0Au,
    0x04u, 0x60u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x0Fu, 0x72u, 0x61u, 0x0Au, 0x0Au, 0x61u, 0xA4u, 0x61u,
};

/**
 * @brief A revision-1 MAIN (Arg0) that counts up to its argument: `Local0 = 0; While (Local0 < Arg0)
 *        { Local0++ }; Return (Local0)`.
 */
static const uint8_t predecoding_argument[] = {
    0x14u, 0x12u, 'M',   'A',   'I',   'N',   0x01u, 0x70u, 0x00u, 0x60u,
    0xA2u, 0x06u, 0x95u, 0x60u, 0x68u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief Control flow inside loops, as MAIN (Arg0): a While (One) whose inner While breaks when Local2
 *        reaches 13, then `Local0++`, a Continue when Local0 is a multiple of 3, else `Local1 += TWCE
 *        (Local0)` (a call), a Return when Local0 is Arg0, a Break at 10.
 */
static const uint8_t predecoding_control[] = {
    0x14u, 0x0Cu, 'T',   'W',   'C',   'E',   0x01u, 0xA4u, 0x77u, 0x68u, 0x0Au, 0x02u, 0x00u, 0x14u, 0x4Eu, 0x05u,
    'M',   'A',   'I',   'N',   0x01u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0x70u, 0x00u, 0x62u, 0xA2u, 0x44u,
    0x04u, 0x01u, 0x70u, 0x00u, 0x63u, 0xA2u, 0x10u, 0x95u, 0x63u, 0x0Au, 0x05u, 0x75u, 0x63u, 0x75u, 0x62u, 0xA0u,
    0x06u, 0x93u, 0x62u, 0x0Au, 0x0Du, 0xA5u, 0x75u, 0x60u, 0xA0u, 0x09u, 0x93u, 0x85u, 0x60u, 0x0Au, 0x03u, 0x00u,
    0x00u, 0x9Fu, 0xA1u, 0x09u, 0x72u, 0x61u, 'T',   'W',   'C',   'E',   0x60u, 0x61u, 0xA0u, 0x0Eu, 0x93u, 0x60u,
    0x68u, 0xA4u, 0x72u, 0x61u, 0x77u, 0x62u, 0x0Bu, 0xE8u, 0x03u, 0x00u, 0x00u, 0xA0u, 0x07u, 0x92u, 0x95u, 0x60u,
    0x0Au, 0x0Au, 0xA5u, 0xA4u, 0x72u, 0x61u, 0x0Cu, 0x00u, 0x00u, 0x10u, 0x00u, 0x00u,
};

/**
 * @brief Operands a record hands back, as MAIN: a While whose bound is the String "4", Ifs on an
 *        uninitialized Local5, on `DerefOf (Local2)` with Local2 = RefOf (INT0), on a Buffer, and on a
 *        String; and as MODZ, a modulo by a local that reaches 0 on the third turn, which fails the
 *        evaluation.
 */
static const uint8_t predecoding_guards[] = {
    0x08u, 'I',   'N',   'T',   '0',   0x0Au, 0x05u, 0x14u, 0x42u, 0x07u, 'M',   'A',   'I',   'N',   0x00u,
    0x70u, 0x00u, 0x61u, 0x70u, 0x71u, 'I',   'N',   'T',   '0',   0x62u, 0x70u, 0x0Du, 0x34u, 0x00u, 0x63u,
    0x70u, 0x11u, 0x03u, 0x01u, 0x02u, 0x64u, 0xA2u, 0x46u, 0x04u, 0x95u, 0x61u, 0x63u, 0x75u, 0x61u, 0xA0u,
    0x09u, 0x93u, 0x65u, 0x00u, 0x75u, 'I',   'N',   'T',   '0',   0xA0u, 0x11u, 0x94u, 0x83u, 0x62u, 0x0Au,
    0x05u, 0x72u, 'I',   'N',   'T',   '0',   0x0Au, 0x0Au, 'I',   'N',   'T',   '0',   0xA0u, 0x0Fu, 0x94u,
    0x64u, 0x01u, 0x72u, 'I',   'N',   'T',   '0',   0x0Au, 0x64u, 'I',   'N',   'T',   '0',   0xA0u, 0x12u,
    0x93u, 0x63u, 0x0Du, 0x34u, 0x00u, 0x72u, 'I',   'N',   'T',   '0',   0x0Bu, 0xE8u, 0x03u, 'I',   'N',
    'T',   '0',   0xA4u, 0x72u, 'I',   'N',   'T',   '0',   0x77u, 0x61u, 0x0Cu, 0x00u, 0x00u, 0x01u, 0x00u,
    0x00u, 0x00u, 0x14u, 0x28u, 'M',   'O',   'D',   'Z',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x0Au, 0x03u,
    0x61u, 0xA2u, 0x18u, 0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u, 0x76u, 0x61u, 0xA0u, 0x0Eu, 0x93u, 0x85u,
    0x0Au, 0x0Au, 0x61u, 0x00u, 0x0Au, 0x07u, 0x72u, 0x60u, 0x0Au, 0x10u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief Operands that hold a reference, read only by records, as CALL: `Return (MAIN (RefOf (INT0)))`, MAIN (Arg0)
 *        setting Local2 to RefOf (INT0) and Local3 to Index (PKG0, 0), then running `While (Local0 < 4) { Local0++;
 *        If (Local0 == 2) { If (Local2 > Local0) { Local1++ }; If (Local3 > Local0) { Local1++ }; If (Arg0 >
 *        Local0) { Local1++ } } }`: the three Ifs are reached at the second turn, the first from records.
 */
static const uint8_t predecoding_references[] = {
    0x08u, 'I',   'N',   'T',   '0',   0x0Au, 0x09u, 0x08u, 'P',   'K',   'G',   '0',   0x12u, 0x04u, 0x01u,
    0x0Au, 0x09u, 0x14u, 0x42u, 0x04u, 'M',   'A',   'I',   'N',   0x01u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u,
    0x61u, 0x70u, 0x71u, 'I',   'N',   'T',   '0',   0x62u, 0x70u, 0x88u, 'P',   'K',   'G',   '0',   0x00u,
    0x00u, 0x63u, 0xA2u, 0x22u, 0x95u, 0x60u, 0x0Au, 0x04u, 0x75u, 0x60u, 0xA0u, 0x1Au, 0x93u, 0x60u, 0x0Au,
    0x02u, 0xA0u, 0x06u, 0x94u, 0x62u, 0x60u, 0x75u, 0x61u, 0xA0u, 0x06u, 0x94u, 0x63u, 0x60u, 0x75u, 0x61u,
    0xA0u, 0x06u, 0x94u, 0x68u, 0x60u, 0x75u, 0x61u, 0xA4u, 0x61u, 0x14u, 0x10u, 'C',   'A',   'L',   'L',
    0x00u, 0xA4u, 'M',   'A',   'I',   'N',   0x71u, 'I',   'N',   'T',   '0',
};

/**
 * @brief The operator stack nearly full, as MAIN: `Return (RECU (50))`, RECU (Arg0) returning `LOOP ()` at 0 and
 *        `RECU (Arg0 - 1) + 1 + 1 + 1 + 1` otherwise, each frame holding four Adds pending; LOOP's If at its third
 *        turn, `If (((Local0 + 1) + 1) + 1 == 6)`, needs more operators than are left.
 */
static const uint8_t predecoding_operators_nearly_full[] = {
    0x14u, 0x2Du, 'L',   'O',   'O',   'P',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u,
    0x1Eu, 0x95u, 0x60u, 0x0Au, 0x04u, 0x75u, 0x60u, 0xA0u, 0x16u, 0x93u, 0x60u, 0x0Au, 0x03u, 0xA0u,
    0x10u, 0x93u, 0x72u, 0x72u, 0x72u, 0x60u, 0x01u, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u, 0x0Au, 0x06u,
    0x75u, 0x61u, 0xA4u, 0x61u, 0x14u, 0x25u, 'R',   'E',   'C',   'U',   0x01u, 0xA0u, 0x09u, 0x93u,
    0x68u, 0x00u, 0xA4u, 'L',   'O',   'O',   'P',   0xA4u, 0x72u, 0x72u, 0x72u, 0x72u, 'R',   'E',
    'C',   'U',   0x74u, 0x68u, 0x01u, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u,
    0x14u, 0x0Du, 'M',   'A',   'I',   'N',   0x00u, 0xA4u, 'R',   'E',   'C',   'U',   0x0Au, 0x32u,
};

/**
 * @brief LOOP, whose If at its third turn holds 4 values: `Local0 = 0; Local1 = 0; While (Local0 < 4) { Local0++;
 *        If (Local0 == 3) { If (1 + (1 + (1 + Local0)) == 6) { Local1++ } } }; Return (Local1)`, then FSEV (Arg0
 *        to Arg6), which returns Arg6.
 */
static const uint8_t predecoding_values_loop[] = {
    0x14u, 0x2Du, 'L',   'O',   'O',   'P',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u,
    0x1Eu, 0x95u, 0x60u, 0x0Au, 0x04u, 0x75u, 0x60u, 0xA0u, 0x16u, 0x93u, 0x60u, 0x0Au, 0x03u, 0xA0u,
    0x10u, 0x93u, 0x72u, 0x01u, 0x72u, 0x01u, 0x72u, 0x01u, 0x60u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x06u,
    0x75u, 0x61u, 0xA4u, 0x61u, 0x14u, 0x08u, 'F',   'S',   'E',   'V',   0x07u, 0xA4u, 0x6Eu,
};

/**
 * @brief An SSDT whose method WIDE counts three turns where `Ones == 0xFFFFFFFF`: none under 64-bit
 *        integers, three under 32-bit ones.
 */
static const uint8_t predecoding_width[] = {
    0x14u, 0x21u, 'W',   'I',   'D',   'E',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u,
    0x61u, 0xA2u, 0x12u, 0x95u, 0x61u, 0x0Au, 0x03u, 0x75u, 0x61u, 0xA0u, 0x0Au, 0x93u,
    0xFFu, 0x0Cu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief Statements no record runs after ones that do, as ONCE: `Local0 = 0; Local2 = 0; While (Local0 < 4) {
 *        Local0 = INCR (Local0); If (Local0 > 99) { Noop }; Local2 = INCR (Local2) }; Return (Local0 + Local2)`,
 *        INCR (Arg0) returning `Arg0 + 1`: at each of the three turns that run from records, the While's record
 *        runs, then the first Store of a call is handed back at the back-edge, then the If's record runs and the
 *        second Store is handed back where a statement is run.
 */
static const uint8_t predecoding_once[] = {
    0x14u, 0x0Bu, 'I', 'N',   'C',   'R',   0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u, 0x14u, 0x2Cu, 'O',
    'N',   'C',   'E', 0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x62u, 0xA2u, 0x1Au, 0x95u, 0x60u, 0x0Au,
    0x04u, 0x70u, 'I', 'N',   'C',   'R',   0x60u, 0x60u, 0xA0u, 0x06u, 0x94u, 0x60u, 0x0Au, 0x63u, 0xA3u,
    0x70u, 'I',   'N', 'C',   'R',   0x62u, 0x62u, 0xA4u, 0x72u, 0x60u, 0x62u, 0x00u,
};

/**
 * @brief A revision-1 STOR (Arg0) that stores its argument whole: `Local0 = 0; While (Local0 < 2)
 *        { Local1 = Arg0; Local0++ }; Return (Local1)`.
 */
static const uint8_t predecoding_store_whole[] = {
    0x14u, 0x16u, 'S',   'T',   'O',   'R',   0x01u, 0x70u, 0x00u, 0x60u, 0xA2u, 0x0Au,
    0x95u, 0x60u, 0x0Au, 0x02u, 0x70u, 0x68u, 0x61u, 0x75u, 0x60u, 0xA4u, 0x61u,
};

/**
 * @brief Targets a record hands back, as UNIN: `Local0 = 0; Local1 = "7"; While (Local0 < 3) { Local0++;
 *        If (Local0 == 2) { Local6++; Local1 = Local0 } }; Return (Local6 + Local1)`: at the second turn,
 *        the first from records, Local6 holds nothing to step and Local1 a String to give back.
 */
static const uint8_t predecoding_targets[] = {
    0x14u, 0x26u, 'U',   'N',   'I',   'N',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x0Du, 0x37u,
    0x00u, 0x61u, 0xA2u, 0x12u, 0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u, 0xA0u, 0x0Au, 0x93u,
    0x60u, 0x0Au, 0x02u, 0x75u, 0x66u, 0x70u, 0x60u, 0x61u, 0xA4u, 0x72u, 0x66u, 0x61u, 0x00u,
};

/**
 * @brief A Store first run from a record, as PEAK: `Local0++` three times, then `While (Local0) { Local0--;
 *        If (Local1) { Local2 = Local0 }; Local1++ }; Return (Local2)`; nothing before its second turn holds
 *        2 values, and the Store, its If false at the first turn, first runs at the second, from its record.
 */
static const uint8_t predecoding_store_peak[] = {
    0x14u, 0x1Bu, 'P',   'E',   'A',   'K',   0x00u, 0x75u, 0x60u, 0x75u, 0x60u, 0x75u, 0x60u, 0xA2u,
    0x0Cu, 0x60u, 0x76u, 0x60u, 0xA0u, 0x05u, 0x61u, 0x70u, 0x60u, 0x62u, 0x75u, 0x61u, 0xA4u, 0x62u,
};

/**
 * @brief Three While that run as tight loops, as MAIN: `Local0 = 0; While (!(Local0 > 9)) { Local0 += 3 }`,
 *        `Local1 = Local0; While (Local1 > 0) { Local1-- }`, `Local2 = 0; While (Local2 < 5) { Local2 =
 *        Local2 + 2 }`, then `Return (Local0 + Local2)`: 12 and 6, so 18.
 */
static const uint8_t predecoding_tight[] = {
    0x14u, 0x34u, 'M',   'A',   'I',   'N',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u, 0x0Bu, 0x92u, 0x94u,
    0x60u, 0x0Au, 0x09u, 0x72u, 0x60u, 0x0Au, 0x03u, 0x60u, 0x70u, 0x60u, 0x61u, 0xA2u, 0x06u, 0x94u,
    0x61u, 0x00u, 0x76u, 0x61u, 0x70u, 0x00u, 0x62u, 0xA2u, 0x0Cu, 0x95u, 0x62u, 0x0Au, 0x05u, 0x70u,
    0x72u, 0x62u, 0x0Au, 0x02u, 0x00u, 0x62u, 0xA4u, 0x72u, 0x60u, 0x62u, 0x00u,
};

/**
 * @brief A tight loop whose body divides by zero at its fifth turn, as MODT: `Local0 = 0; While (Local0 <
 *        10) { Local0 += 1 + (0 % (4 - Local0)) }; Return (Local0)`.
 */
static const uint8_t predecoding_tight_modulo[] = {
    0x14u, 0x1Fu, 'M',   'O',   'D',   'T',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u, 0x13u, 0x95u, 0x60u, 0x0Au, 0x0Au,
    0x72u, 0x60u, 0x72u, 0x01u, 0x85u, 0x00u, 0x74u, 0x0Au, 0x04u, 0x60u, 0x00u, 0x00u, 0x00u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief Tight loops whose body runs first in a call deeper than the one that linked it, as PKTL: `Local0 = INNR ();
 *        Return (Local0 + (1 + (1 + INNR ())))`, INNR running `Local0 = 0; Local1 = 0; While (Local0 < 3) { While
 *        (Local1 < Local0) { Local1 += 1 }; Local0++ }; Return (Local1)`: the second call enters its tight loops at
 *        once, their records linked by the first, two values deeper, so 6.
 */
static const uint8_t predecoding_deeper_tight[] = {
    0x14u, 0x1Fu, 'I',   'N',   'N',   'R',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u, 0x10u,
    0x95u, 0x60u, 0x0Au, 0x03u, 0xA2u, 0x08u, 0x95u, 0x61u, 0x60u, 0x72u, 0x61u, 0x01u, 0x61u, 0x75u, 0x60u,
    0xA4u, 0x61u, 0x14u, 0x1Au, 'P',   'K',   'T',   'L',   0x00u, 0x70u, 'I',   'N',   'N',   'R',   0x60u,
    0xA4u, 0x72u, 0x60u, 0x72u, 0x01u, 0x72u, 0x01u, 'I',   'N',   'N',   'R',   0x00u, 0x00u, 0x00u,
};

/**
 * @brief A While whose body is a Break, as BRKB (Arg0): `Local0 = 0; Local1 = 0; While (Local0 < 4) { Local0++; While
 *        (Local1 < 5) { Break } }; Return (Arg0)`: never a tight loop.
 */
static const uint8_t predecoding_break_body[] = {
    0x14u, 0x1Du, 'B',   'R',   'K',   'B',   0x01u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u, 0x0Eu,
    0x95u, 0x60u, 0x0Au, 0x04u, 0x75u, 0x60u, 0xA2u, 0x06u, 0x95u, 0x61u, 0x0Au, 0x05u, 0xA5u, 0xA4u, 0x68u,
};

/**
 * @brief A tight loop whose bound becomes a String between two of its entries, as TOPR: `Local0 = 0; Local1 = 0;
 *        Local2 = 5; While (Local0 < 4) { Local0++; If (Local0 == 3) { Local2 = "9" }; While (Local1 < Local2) {
 *        Local1++ } }; Return (Local1)`, so 9.
 */
static const uint8_t predecoding_string_bound[] = {
    0x14u, 0x2Cu, 'T',   'O',   'P',   'R',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0x70u, 0x0Au,
    0x05u, 0x62u, 0xA2u, 0x19u, 0x95u, 0x60u, 0x0Au, 0x04u, 0x75u, 0x60u, 0xA0u, 0x0Au, 0x93u, 0x60u, 0x0Au,
    0x03u, 0x70u, 0x0Du, 0x39u, 0x00u, 0x62u, 0xA2u, 0x06u, 0x95u, 0x61u, 0x62u, 0x75u, 0x61u, 0xA4u, 0x61u,
};

/**
 * @brief A While whose predicate is an operator and no comparison, as ANDW: `Local0 = 5; While (Local0 & 0x0F) {
 *        Local0-- }; Return (Local0)`, so 0; and one whose predicate is two LNot over a comparison, as NNOT: `Local0 =
 *        0; While (!!(Local0 < 5)) { Local0++ }; Return (Local0)`, so 5: never tight loops.
 */
static const uint8_t predecoding_and_predicate[] = {
    0x14u, 0x15u, 'A',   'N',   'D',   'W',   0x00u, 0x70u, 0x0Au, 0x05u, 0x60u,
    0xA2u, 0x08u, 0x7Bu, 0x60u, 0x0Au, 0x0Fu, 0x00u, 0x76u, 0x60u, 0xA4u, 0x60u,
};
static const uint8_t predecoding_two_lnot[] = {
    0x14u, 0x15u, 'N',   'N',   'O',   'T',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
    0x09u, 0x92u, 0x92u, 0x95u, 0x60u, 0x0Au, 0x05u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief ROOM, whose inner While runs as a tight loop from its first record in a call, the If before it false at the
 *        first turn: `Local0 = 0; Local1 = 0; While (Local0 < 3) { Local0++; If (Local0 > 1) { Local1 = 0; While
 *        (Local1 < 4) { Local1 = Local1 + 1 } } }; Return (Local1)`, its body 3 values deep and its predicate 2; then
 *        FSEV (Arg0 to Arg6), which returns Arg6.
 */
static const uint8_t predecoding_room_loop[] = {
    0x14u, 0x2Au, 'R',   'O',   'O',   'M',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u,
    0xA2u, 0x1Bu, 0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u, 0xA0u, 0x13u, 0x94u, 0x60u, 0x01u,
    0x70u, 0x00u, 0x61u, 0xA2u, 0x0Bu, 0x95u, 0x61u, 0x0Au, 0x04u, 0x70u, 0x72u, 0x61u, 0x01u,
    0x00u, 0x61u, 0xA4u, 0x61u, 0x14u, 0x08u, 'F',   'S',   'E',   'V',   0x07u, 0xA4u, 0x6Eu,
};

/**
 * @brief INCR (Arg0), which returns Arg0 + 1, then REFU: `Local0 = 0; While (Local0 < 100 && One) { Local0 = INCR
 *        (Local0) }; Return (Local0)`, so 100: a While and a body no record runs, LAnd and a call staying for the loop,
 *        as `While (APMS) { Stall (One) }` of the HP machines of the corpus.
 */
static const uint8_t predecoding_refused_loop[] = {
    0x14u, 0x0Bu, 'I',   'N',   'C',   'R',   0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u, 0x14u,
    0x1Au, 'R',   'E',   'F',   'U',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u, 0x0Eu, 0x90u, 0x95u,
    0x60u, 0x0Au, 0x64u, 0x01u, 0x70u, 'I',   'N',   'C',   'R',   0x60u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief Writes INCR, then SLOT: `Local0 = 0; Local2 = 0; While (Local0 < 10) { Local2 = INCR (Local2); Local3 =
 *        Buffer (51) {}; Local0++ }; Return (Local2)`, so 10: its call, which no record runs, and its Increment,
 *        which one runs, 64 bytes apart, so that they share their offset's recent slot.
 */
static void predecoding_write_shared_slot(void)
{
    static const uint8_t increment_method[] = {0x14u, 0x0Bu, 'I',   'N',   'C',   'R',
                                               0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u};
    static const uint8_t start[] = {0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x62u};
    static const uint8_t head[] = {0x95u, 0x60u, 0x0Au, 0x0Au, 0x70u, 'I', 'N', 'C', 'R', 0x62u, 0x62u, 0x70u};
    static const uint8_t buffer_lead[] = {0x11u};
    static const uint8_t buffer_size[] = {0x0Au, PREDECODING_SLOT_BUFFER};
    static const uint8_t buffer_byte[] = {0x00u};
    static const uint8_t tail[] = {0x63u, 0x75u, 0x60u};
    static const uint8_t while_lead[] = {0xA2u};
    static const uint8_t finish[] = {0xA4u, 0x62u};

    made_up_aml_begin(&predecoding_inner);
    made_up_aml_raw(&predecoding_inner, buffer_size, sizeof(buffer_size));
    for (uint32_t index = 0u; index < PREDECODING_SLOT_BUFFER; ++index)
        made_up_aml_raw(&predecoding_inner, buffer_byte, sizeof(buffer_byte));
    made_up_aml_begin(&predecoding_outer);
    made_up_aml_raw(&predecoding_outer, head, sizeof(head));
    made_up_aml_construct(&predecoding_outer, buffer_lead, sizeof(buffer_lead), &predecoding_inner);
    made_up_aml_raw(&predecoding_outer, tail, sizeof(tail));
    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, start, sizeof(start));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, finish, sizeof(finish));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, increment_method, sizeof(increment_method));
    made_up_aml_method(&predecoding_method, "SLOT", 0x00u, &predecoding_body);
}

/**
 * @brief INCR, then BRAN: `Local0 = 0; Local1 = 0; While (Local0 < 100 && One) { If (Local0 > 50 && One) { Local1++
 *        }; Local0 = INCR (Local0) }; Return (Local1)`, so 49: a While, an If and a call no record runs, the If's body
 *        an Increment one runs, from the 52nd turn.
 */
static const uint8_t predecoding_branching_loop[] = {
    0x14u, 0x0Bu, 'I',   'N',   'C',   'R',   0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u, 0x14u,
    0x27u, 'B',   'R',   'A',   'N',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u,
    0x18u, 0x90u, 0x95u, 0x60u, 0x0Au, 0x64u, 0x01u, 0xA0u, 0x09u, 0x90u, 0x94u, 0x60u, 0x0Au,
    0x32u, 0x01u, 0x75u, 0x61u, 0x70u, 'I',   'N',   'C',   'R',   0x60u, 0x60u, 0xA4u, 0x61u,
};

/**
 * @brief INCR, then MIXD: `Local0 = 0; Local1 = 0; While (Local0 < 100 && One) { Local1++; Local0 = INCR (Local0) };
 *        Return (Local1)`, so 100: a While and a call no record runs around an Increment one runs.
 */
static const uint8_t predecoding_mixed_loop[] = {
    0x14u, 0x0Bu, 'I',   'N',   'C',   'R',   0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u, 0x14u, 0x1Fu, 'M',
    'I',   'X',   'D',   0x00u, 0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u, 0xA2u, 0x10u, 0x90u, 0x95u, 0x60u,
    0x0Au, 0x64u, 0x01u, 0x75u, 0x61u, 0x70u, 'I',   'N',   'C',   'R',   0x60u, 0x60u, 0xA4u, 0x61u,
};

/**
 * @brief INCR, then NEST: `Local1 = 0; While (Local1 < 3) { Local1++; Local0 = 0; While (Local0 < 10 && One) { Local0
 *        = INCR (Local0) } }; Return (Local0)`, so 10: an outer loop records run around an inner one no record runs.
 */
static const uint8_t predecoding_nested_refused_loop[] = {
    0x14u, 0x0Bu, 'I',   'N',   'C',   'R',   0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u, 0x14u,
    0x25u, 'N',   'E',   'S',   'T',   0x00u, 0x70u, 0x00u, 0x61u, 0xA2u, 0x19u, 0x95u, 0x61u,
    0x0Au, 0x03u, 0x75u, 0x61u, 0x70u, 0x00u, 0x60u, 0xA2u, 0x0Eu, 0x90u, 0x95u, 0x60u, 0x0Au,
    0x0Au, 0x01u, 0x70u, 'I',   'N',   'C',   'R',   0x60u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief INCR, then NEXT: `Local0 = 0; While (Local0 < 100) { Local1 = INCR (Local0); Local0++ }; Return (Local1)`,
 *        so 100: a statement no record runs, between statements records run.
 */
static const uint8_t predecoding_refused_statement[] = {
    0x14u, 0x0Bu, 'I',   'N', 'C', 'R',   0x01u, 0xA4u, 0x72u, 0x68u, 0x01u, 0x00u, 0x14u,
    0x1Au, 'N',   'E',   'X', 'T', 0x00u, 0x70u, 0x00u, 0x60u, 0xA2u, 0x0Eu, 0x95u, 0x60u,
    0x0Au, 0x64u, 0x70u, 'I', 'N', 'C',   'R',   0x60u, 0x61u, 0x75u, 0x60u, 0xA4u, 0x61u,
};

/**
 * @brief A While at offset 0 as a statement alone: `While (Local0 < 1000) { Local0++ }`.
 */
static const uint8_t predecoding_while[] = {0xA2u, 0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u};

/**
 * @brief An If at offset 0 whose predicate is `LNotEqual (Arg1, 5)`: LNot over LEqual.
 */
static const uint8_t predecoding_not_equal[] = {0xA0u, 0x08u, 0x92u, 0x93u, 0x69u, 0x0Au, 0x05u, 0x75u, 0x60u};

/**
 * @brief Statements no record runs, each at offset 0 under its own length: an If whose predicate is a
 *        name, a While whose operator stores to a local, a While whose constant is cut by its PkgLength,
 *        a While whose PkgLength passes the end, and a Continue.
 */
static const uint8_t predecoding_name_predicate[] = {0xA0u, 0x07u, 'N', 'A', 'M', 'E', 0x75u, 0x60u};
static const uint8_t predecoding_inner_target[] = {0xA2u, 0x07u, 0x72u, 0x60u, 0x01u, 0x61u, 0x75u, 0x60u};
static const uint8_t predecoding_cut_constant[] = {0xA2u, 0x04u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u};
static const uint8_t predecoding_crossing[] = {0xA2u, 0x10u, 0x95u, 0x60u, 0x01u, 0x75u, 0x60u};
static const uint8_t predecoding_continue[] = {0x9Fu};
static const uint8_t predecoding_store_debug[] = {0x70u, 0x60u, 0x5Bu, 0x31u};
static const uint8_t predecoding_add_debug[] = {0x72u, 0x60u, 0x01u, 0x5Bu, 0x31u};
static const uint8_t predecoding_increment_name[] = {0x75u, 'N', 'A', 'M', 'E'};

/**
 * @brief Statements that write a local or an argument, each at offset 0: `Local2 = Local0 + Arg1`,
 *        `Local1 = Arg0`, `Local0 = Local0 - 1` as Subtract with a target, `Arg3++` and `Local7--`.
 */
static const uint8_t predecoding_store_sum[] = {0x70u, 0x72u, 0x60u, 0x69u, 0x00u, 0x62u};
static const uint8_t predecoding_store_leaf[] = {0x70u, 0x68u, 0x61u};
static const uint8_t predecoding_subtract[] = {0x74u, 0x60u, 0x01u, 0x60u};
static const uint8_t predecoding_increment[] = {0x75u, 0x6Bu};
static const uint8_t predecoding_decrement[] = {0x76u, 0x67u};

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
    kernel_test_measure_hexadecimal(test, key, on.fold);
    return on;
}

/**
 * @brief Writes MAIN: `Local0 = 0; While (Local0 < 3) { Local0++; Local1 = 0; While (Local1 < 2)
 *        { Local1++; Noop... } }; Return (Local0 + Local1)`, its inner loop holding PREDECODING_NOOPS Noops.
 */
static void predecoding_write_full_loop(void)
{
    static const uint8_t while_lead[] = {0xA2u};
    static const uint8_t outer_head[] = {0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u, 0x70u, 0x00u, 0x61u};
    static const uint8_t inner_head[] = {0x95u, 0x61u, 0x0Au, 0x02u, 0x75u, 0x61u};
    static const uint8_t noop[] = {0xA3u};
    static const uint8_t start[] = {0x70u, 0x00u, 0x60u};
    static const uint8_t finish[] = {0xA4u, 0x72u, 0x60u, 0x61u, 0x00u};

    made_up_aml_begin(&predecoding_inner);
    made_up_aml_raw(&predecoding_inner, inner_head, sizeof(inner_head));
    for (uint32_t index = 0u; index < PREDECODING_NOOPS; ++index)
        made_up_aml_raw(&predecoding_inner, noop, sizeof(noop));
    made_up_aml_begin(&predecoding_outer);
    made_up_aml_raw(&predecoding_outer, outer_head, sizeof(outer_head));
    made_up_aml_construct(&predecoding_outer, while_lead, sizeof(while_lead), &predecoding_inner);
    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, start, sizeof(start));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, finish, sizeof(finish));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief Writes MAIN: `Local0 = 0; Local1 = 0; While (Local0 < 3) { Local0++; If (Local0 + k + (k + 1) == 0xFF)
 *        { Local1++ } for each k below PREDECODING_POOL_IFS }; Return (Local1)`, each If three operators and three
 *        constants.
 */
static void predecoding_write_pools(void)
{
    static const uint8_t while_lead[] = {0xA2u};
    static const uint8_t if_lead[] = {0xA0u};
    static const uint8_t head[] = {0x95u, 0x60u, 0x0Au, 0x03u, 0x75u, 0x60u};
    static const uint8_t start[] = {0x70u, 0x00u, 0x60u, 0x70u, 0x00u, 0x61u};
    static const uint8_t finish[] = {0xA4u, 0x61u};

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
    made_up_aml_raw(&predecoding_body, start, sizeof(start));
    made_up_aml_construct(&predecoding_body, while_lead, sizeof(while_lead), &predecoding_outer);
    made_up_aml_raw(&predecoding_body, finish, sizeof(finish));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief Writes LOOP and FSEV, then MAIN: `Return (1 + (1 + ... FSEV (1, 1, 1, 1, 1, 1, FSEV (... LOOP ()))))`,
 *        PREDECODING_SEVEN_CALLS calls in the last argument of the one around them, each holding six values while
 *        LOOP runs, under PREDECODING_SEVEN_PADS Adds: LOOP's If at its third turn needs more values than are left.
 */
static void predecoding_write_values_nearly_full(void)
{
    static const uint8_t add_one[] = {0x72u, 0x01u};
    static const uint8_t seven[] = {'F', 'S', 'E', 'V', 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u};
    static const uint8_t loop_call[] = {'L', 'O', 'O', 'P'};
    static const uint8_t null_name[] = {0x00u};
    static const uint8_t returns[] = {0xA4u};

    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, returns, sizeof(returns));
    for (uint32_t pad = 0u; pad < PREDECODING_SEVEN_PADS; ++pad)
        made_up_aml_raw(&predecoding_body, add_one, sizeof(add_one));
    for (uint32_t call = 0u; call < PREDECODING_SEVEN_CALLS; ++call)
        made_up_aml_raw(&predecoding_body, seven, sizeof(seven));
    made_up_aml_raw(&predecoding_body, loop_call, sizeof(loop_call));
    for (uint32_t pad = 0u; pad < PREDECODING_SEVEN_PADS; ++pad)
        made_up_aml_raw(&predecoding_body, null_name, sizeof(null_name));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, predecoding_values_loop, sizeof(predecoding_values_loop));
    made_up_aml_method(&predecoding_method, "MAIN", 0x00u, &predecoding_body);
}

/**
 * @brief Writes ROOM and FSEV, then MAIN: `Local0 = ROOM (); Return (1 + (1 + ... FSEV (1, 1, 1, 1, 1, 1, FSEV (...
 *        ROOM ()))))`, PREDECODING_SEVEN_CALLS calls nested in the last argument under @p pads Adds: the first call
 *        links ROOM's tight loop, the second runs it @p pads values deeper.
 */
static void predecoding_write_room(uint32_t pads)
{
    static const uint8_t first_call[] = {0x70u, 'R', 'O', 'O', 'M', 0x60u, 0xA4u};
    static const uint8_t add_one[] = {0x72u, 0x01u};
    static const uint8_t seven[] = {'F', 'S', 'E', 'V', 0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u};
    static const uint8_t room_call[] = {'R', 'O', 'O', 'M'};
    static const uint8_t null_name[] = {0x00u};

    made_up_aml_begin(&predecoding_body);
    made_up_aml_raw(&predecoding_body, first_call, sizeof(first_call));
    for (uint32_t pad = 0u; pad < pads; ++pad)
        made_up_aml_raw(&predecoding_body, add_one, sizeof(add_one));
    for (uint32_t call = 0u; call < PREDECODING_SEVEN_CALLS; ++call)
        made_up_aml_raw(&predecoding_body, seven, sizeof(seven));
    made_up_aml_raw(&predecoding_body, room_call, sizeof(room_call));
    for (uint32_t pad = 0u; pad < pads; ++pad)
        made_up_aml_raw(&predecoding_body, null_name, sizeof(null_name));
    made_up_aml_begin(&predecoding_method);
    made_up_aml_raw(&predecoding_method, predecoding_room_loop, sizeof(predecoding_room_loop));
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
    const PredecodingRecord_t *record = predecoding_build(predecoding_while, sizeof(predecoding_while));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t *node = &predecoding_cache.nodes[0];

    if (!kernel_test_check(test, record != NULL, "the record is built"))
        return;
    kernel_test_check(test,
                      record->shape == PREDECODING_RECORD(WHILE) && record->after == 7u && record->end == 9u &&
                          record->limit == sizeof(predecoding_while) && record->node_count == 1u,
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
    const PredecodingRecord_t *record = predecoding_build(predecoding_not_equal, sizeof(predecoding_not_equal));
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
    const PredecodingRecord_t *sum = predecoding_build(predecoding_store_sum, sizeof(predecoding_store_sum));
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRecordNode_t *node = &predecoding_cache.nodes[0];

    kernel_test_check(
        test,
        sum != NULL && sum->shape == PREDECODING_RECORD(STORE) && sum->after == 6u && sum->node_count == 1u &&
            node->opcode == PREDECODING_OPCODE(ADD) && node->operands[1].kind == PREDECODING_OPERAND(ARGUMENT) &&
            sum->source.kind == PREDECODING_OPERAND(RESULT) && sum->target.kind == PREDECODING_OPERAND(LOCAL) &&
            sum->target.slot == 2u && sum->values_peak == 3u && sum->operators_peak == 2u,
        "Local2 = Local0 + Arg1 stores an Add to Local2, holding 3 values and 2 operators");

    const PredecodingRecord_t *leaf = predecoding_build(predecoding_store_leaf, sizeof(predecoding_store_leaf));

    kernel_test_check(test,
                      leaf != NULL && leaf->shape == PREDECODING_RECORD(STORE) && leaf->node_count == 0u &&
                          leaf->source.kind == PREDECODING_OPERAND(ARGUMENT) && leaf->source.slot == 0u &&
                          leaf->target.kind == PREDECODING_OPERAND(LOCAL) && leaf->target.slot == 1u &&
                          leaf->values_peak == 2u && leaf->operators_peak == 1u,
                      "Local1 = Arg0 stores Arg0 itself, holding 2 values and 1 operator");

    const PredecodingRecord_t *difference = predecoding_build(predecoding_subtract, sizeof(predecoding_subtract));

    kernel_test_check(test,
                      difference != NULL && difference->shape == PREDECODING_RECORD(STORE) &&
                          difference->node_count == 1u &&
                          predecoding_cache.nodes[0].opcode == PREDECODING_OPCODE(SUBTRACT) &&
                          difference->target.kind == PREDECODING_OPERAND(LOCAL) && difference->target.slot == 0u &&
                          difference->values_peak == 3u && difference->operators_peak == 1u,
                      "Subtract (Local0, One, Local0) writes Local0, holding 3 values and 1 operator");

    const PredecodingRecord_t *up = predecoding_build(predecoding_increment, sizeof(predecoding_increment));

    kernel_test_check(test,
                      up != NULL && up->shape == PREDECODING_RECORD(INCREMENT) && up->after == 2u &&
                          up->target.kind == PREDECODING_OPERAND(ARGUMENT) && up->target.slot == 3u &&
                          up->values_peak == 1u && up->operators_peak == 1u,
                      "Arg3++ steps Arg3 up, holding 1 value and 1 operator");

    const PredecodingRecord_t *down = predecoding_build(predecoding_decrement, sizeof(predecoding_decrement));

    kernel_test_check(test,
                      down != NULL && down->shape == PREDECODING_RECORD(DECREMENT) &&
                          down->target.kind == PREDECODING_OPERAND(LOCAL) && down->target.slot == 7u,
                      "Local7-- steps Local7 down");
}

/**
 * @brief A statement no record runs stays a record the loop runs: a name in a predicate, an operator
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
        {predecoding_name_predicate, sizeof(predecoding_name_predicate)},
        {predecoding_inner_target,   sizeof(predecoding_inner_target)  },
        {predecoding_cut_constant,   sizeof(predecoding_cut_constant)  },
        {predecoding_crossing,       sizeof(predecoding_crossing)      },
        {predecoding_continue,       sizeof(predecoding_continue)      },
        {predecoding_store_debug,    sizeof(predecoding_store_debug)   },
        {predecoding_add_debug,      sizeof(predecoding_add_debug)     },
        {predecoding_increment_name, sizeof(predecoding_increment_name)},
    };
    uint32_t general = 0u;

    for (uint32_t index = 0u; index < sizeof(statements) / sizeof(statements[0]); ++index)
    {
        const PredecodingRecord_t *record = predecoding_build(statements[index].bytes, statements[index].length);

        general += (record != NULL && record->shape == PREDECODING_RECORD(GENERAL) && record->node_count == 0u &&
                    predecoding_cache.node_count == 0u && predecoding_cache.constant_count == 0u) ?
                       1u :
                       0u;
    }
    kernel_test_check(test, general == sizeof(statements) / sizeof(statements[0]),
                      "each is a record the loop runs, holding no operator and no constant");
}

/**
 * @brief The loops of window W4 run from records as the loop runs them, under revision 2 and revision 1;
 *        the control: the loop decodes its first turn, runs its second from its two records, which links
 *        them, and its 998 others as a tight loop.
 */
KERNEL_TEST(the_loop_of_window_w4_runs_from_records_as_the_loop_runs_it)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_w4, sizeof(predecoding_w4), 2u, "\\LOOP",
                                                     PREDECODING_NO_ARGUMENT, true, "fold_revision_2");

    predecoding_compare(test, predecoding_w4, sizeof(predecoding_w4), 1u, "\\LOOP", PREDECODING_NO_ARGUMENT, true,
                        "fold_revision_1");
    kernel_test_check(test, run.tight_turns == 998u, "998 turns run as a tight loop");
}

/**
 * @brief A Store copies a local or an argument whole, unmasked, from a record as from the loop (§5.2):
 *        under revision 1, STOR (0x100000001) stores its argument at its second turn, from a record, and
 *        returns 0x100000001, not 1.
 */
KERNEL_TEST(a_store_from_a_record_copies_a_wide_argument_whole)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_store_whole, sizeof(predecoding_store_whole), 1u,
                                                     "\\STOR", 0x100000001ull, true, "fold");

    kernel_test_check(test, run.result == 0x100000001ull, "STOR returns its argument whole");
}

/**
 * @brief A record hands back an Increment of a local that holds nothing, which the loop reads as 0,
 *        counted, and a Store to a local that holds a String, which the loop gives back: UNIN returns 3,
 *        from records as from the loop, the two statements handed back once each.
 */
KERNEL_TEST(a_record_hands_back_a_target_that_holds_no_integer)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_targets, sizeof(predecoding_targets), 2u,
                                                     "\\UNIN", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 3u && run.declined == 2u, "UNIN returns 3, two statements handed back");
}

/**
 * @brief A Store that a record runs first raises the most values the evaluation held, as the loop would:
 *        PEAK's Store holds its source and its target, 2 values, more than anything before it.
 */
KERNEL_TEST(a_store_first_run_from_a_record_raises_the_values_peak)
{
    predecoding_compare(test, predecoding_store_peak, sizeof(predecoding_store_peak), 2u, "\\PEAK",
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
    const PredecodingRun_t run = predecoding_compare(test, predecoding_tight, sizeof(predecoding_tight), 2u, "\\MAIN",
                                                     PREDECODING_NO_ARGUMENT, true, "fold_revision_2");

    predecoding_compare(test, predecoding_tight, sizeof(predecoding_tight), 1u, "\\MAIN", PREDECODING_NO_ARGUMENT, true,
                        "fold_revision_1");
    kernel_test_check(test, run.result == 18u && run.tight_turns == 13u, "MAIN returns 18, after 13 tight turns");
}

/**
 * @brief A tight loop whose body would divide by zero hands it back with the While's block open, and the
 *        loop fails as it fails without records: MODT runs 2 tight turns, then its modulo by zero is
 *        handed back, once.
 */
KERNEL_TEST(a_tight_loop_hands_back_a_body_that_would_fail)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_tight_modulo, sizeof(predecoding_tight_modulo),
                                                     2u, "\\MODT", PREDECODING_NO_ARGUMENT, true, "fold");

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
    const PredecodingRun_t run = predecoding_compare(test, predecoding_comparisons, sizeof(predecoding_comparisons), 2u,
                                                     "\\MAIN", PREDECODING_NO_ARGUMENT, true, "fold_revision_2");

    predecoding_compare(test, predecoding_comparisons, sizeof(predecoding_comparisons), 1u, "\\MAIN",
                        PREDECODING_NO_ARGUMENT, true, "fold_revision_1");
    kernel_test_measure(test, "records_run", run.records_run);
}

/**
 * @brief An argument wider than a revision-1 table's integers is masked where an operator reads it:
 *        0x100000003 bounds MAIN's loop at 3 turns, from records as from the loop.
 */
KERNEL_TEST(an_operand_wider_than_the_width_is_masked_as_the_loop_masks_it)
{
    predecoding_compare(test, predecoding_argument, sizeof(predecoding_argument), 1u, "\\MAIN", 0x100000003ull, true,
                        "fold");
}

/**
 * @brief Break, Continue, a call and a Return inside nested loops leave what the loop leaves: a Return
 *        before the loop ends, at 4, and the Break at 10.
 */
KERNEL_TEST(breaks_continues_calls_and_returns_inside_loops_agree)
{
    predecoding_compare(test, predecoding_control, sizeof(predecoding_control), 2u, "\\MAIN", 4u, true, "fold_return");
    predecoding_compare(test, predecoding_control, sizeof(predecoding_control), 2u, "\\MAIN", 20u, true, "fold_break");
}

/**
 * @brief A record hands back a statement whose operand holds no Integer (a String, a Buffer, a
 *        reference, an uninitialized local), so that of MAIN's records only its Increment runs, and one
 *        whose modulo by zero would fail; the loop then runs them, and the failure is the loop's.
 */
KERNEL_TEST(a_record_hands_back_what_holds_no_integer_or_would_fail)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_guards, sizeof(predecoding_guards), 2u, "\\MAIN",
                                                     PREDECODING_NO_ARGUMENT, true, "fold_operands");
    PredecodingRun_t failed = {0};

    kernel_test_check(test, run.declined != 0u, "the statements whose operands hold no Integer are handed back");
    if (!predecoding_open(test, 2u, predecoding_guards, sizeof(predecoding_guards), true))
        return;
    predecoding_evaluate("\\MODZ", PREDECODING_NO_ARGUMENT, &failed);
    kernel_test_check(test, predecoding_load.predecoding.declined[PREDECODING_DECLINED(OPERATION)] == 1u,
                      "the modulo by zero is handed back, once");
    predecoding_close();
    predecoding_compare(test, predecoding_guards, sizeof(predecoding_guards), 2u, "\\MODZ", PREDECODING_NO_ARGUMENT,
                        true, "fold_divide_by_zero");
}

/**
 * @brief A statement a record hands back goes to the loop at once, counted once: ONCE's two Stores of a call
 *        are handed back at each of the three turns that run from records, so 6 times.
 */
KERNEL_TEST(a_statement_a_record_hands_back_is_counted_once)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_once, sizeof(predecoding_once), 2u, "\\ONCE",
                                                     PREDECODING_NO_ARGUMENT, true, "fold");

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
    static const uint8_t empty[] = {0xA3u};
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
        predecoding_declare(predecoding_table, "SSDT", 2u, predecoding_width, sizeof(predecoding_width));
        advanced_configuration_and_power_interface_machine_language_evaluate(&predecoding_load, "\\WIDE", NULL, 0u,
                                                                             &result);
        counts[predecodes][0] = result.payload;
        predecoding_declare(predecoding_second_table, "DSDT", 1u, empty, sizeof(empty));
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
}

/**
 * @brief A record hands back an operand that holds a reference, which the loop goes through: CALL's three Ifs,
 *        first reached from records, read a reference to a name, to an element and in an argument, with records
 *        as without.
 */
KERNEL_TEST(a_record_hands_back_an_operand_that_holds_a_reference)
{
    const PredecodingRun_t run = predecoding_compare(test, predecoding_references, sizeof(predecoding_references), 2u,
                                                     "\\CALL", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.declined != 0u, "the Ifs over a reference are handed back");
}

/**
 * @brief A record hands back a statement that needs more operators, or more values, than the evaluation has
 *        left, and the loop then fails as it fails without records: MAIN with the operator stack nearly full,
 *        then with the value stack nearly full.
 */
KERNEL_TEST(a_record_hands_back_what_the_stacks_have_no_room_for)
{
    predecoding_compare(test, predecoding_operators_nearly_full, sizeof(predecoding_operators_nearly_full), 2u,
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
    const PredecodingRun_t run = predecoding_compare(test, predecoding_deeper_tight, sizeof(predecoding_deeper_tight),
                                                     2u, "\\PKTL", PREDECODING_NO_ARGUMENT, true, "fold");

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
    const PredecodingRun_t breaks = predecoding_compare(test, predecoding_break_body, sizeof(predecoding_break_body),
                                                        2u, "\\BRKB", 7u, true, "fold_break");
    const PredecodingRun_t ands =
        predecoding_compare(test, predecoding_and_predicate, sizeof(predecoding_and_predicate), 2u, "\\ANDW",
                            PREDECODING_NO_ARGUMENT, true, "fold_and");
    const PredecodingRun_t nots = predecoding_compare(test, predecoding_two_lnot, sizeof(predecoding_two_lnot), 2u,
                                                      "\\NNOT", PREDECODING_NO_ARGUMENT, true, "fold_lnot");
    const PredecodingRun_t strings =
        predecoding_compare(test, predecoding_string_bound, sizeof(predecoding_string_bound), 2u, "\\TOPR",
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
    const PredecodingRun_t run = predecoding_compare(test, predecoding_refused_loop, sizeof(predecoding_refused_loop),
                                                     2u, "\\REFU", PREDECODING_NO_ARGUMENT, false, "fold");

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
        predecoding_compare(test, predecoding_refused_statement, sizeof(predecoding_refused_statement), 2u, "\\NEXT",
                            PREDECODING_NO_ARGUMENT, true, "fold");

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
    predecoding_write_shared_slot();
    if (!kernel_test_check(test, !predecoding_method.overflowed, "the made-up AML fits"))
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
    const PredecodingRun_t run = predecoding_compare(test, predecoding_refused_loop, sizeof(predecoding_refused_loop),
                                                     2u, "\\REFU", PREDECODING_NO_ARGUMENT, false, "fold");

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
        predecoding_compare(test, predecoding_branching_loop, sizeof(predecoding_branching_loop), 2u, "\\BRAN",
                            PREDECODING_NO_ARGUMENT, true, "fold_branching");
    const PredecodingRun_t mixed = predecoding_compare(test, predecoding_mixed_loop, sizeof(predecoding_mixed_loop), 2u,
                                                       "\\MIXD", PREDECODING_NO_ARGUMENT, true, "fold_mixed");

    kernel_test_check(test, branching.result == 49u && branching.records_run == 49u && branching.loops == 0u,
                      "BRAN returns 49, its Increment run from records 49 times, its loop never handed back whole");
    kernel_test_check(test, mixed.result == 100u && mixed.records_run == 99u && mixed.loops == 0u,
                      "MIXD returns 100, its Increment run from records 99 times, its loop never handed back whole");
}

/**
 * @brief A While is handed back whole only while it is the frame's loop (§3.6): NEST's inner While, the frame's loop in
 *        its first run, is handed back whole at its third back-edge after 5 statements; in each of the two runs after
 *        it, inside the outer loop the frame's loop has become, its While goes to the loop at its 10 back-edges and its
 *        call at its 10 turns, and the While once more when the outer loop's records reach it, 47 times in all; the
 * outer While, Increment and Store run from records at its first two back-edges and its While at the third, 7 times;
 * the five statements are searched once each, and NEST returns 10 with records as without.
 */
KERNEL_TEST(a_loop_inside_the_frame_s_loop_is_never_handed_back_whole)
{
    const PredecodingRun_t run =
        predecoding_compare(test, predecoding_nested_refused_loop, sizeof(predecoding_nested_refused_loop), 2u,
                            "\\NEST", PREDECODING_NO_ARGUMENT, true, "fold");

    kernel_test_check(test, run.result == 10u && run.declined == 47u && run.records_run == 7u && run.loops == 1u,
                      "NEST returns 10, 47 statements handed back, 7 records run, one loop handed back whole");
    kernel_test_check(test, run.searches == 5u, "each of the five statements is searched once");
}
