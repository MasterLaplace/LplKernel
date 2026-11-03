#include "shortcuts.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_shortcuts, KERNEL_TEST_STAGE_INITIALIZATION);

#define SHORTCUTS_AML_CAPACITY 8192u /**< Bytes of AML one table of a case holds. */
#define SHORTCUTS_DEEP_DEVICES                                                                                         \
    481u /**< Devices nested around the deep case's package: with the table's own construct, 482 open, so that a       \
            package 32 deep passes the 512 of §6.1. */
#define SHORTCUTS_SAMPLE_STRIDE                                                                                        \
    16u /**< Bytes from one changed byte to the next in the sample ring 0 loads; host_shortcuts changes every byte. */
#define SHORTCUTS_STATUS_COUNT ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT
#define SHORTCUTS_OPCODE_COUNT ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT

/**
 * @brief A load made twice, with the load's shortcuts and without: a DSDT, and an SSDT that may come first.
 */
typedef struct ShortcutsCase {
    const char *claim;   /**< What the two loads agreeing shows. */
    const uint8_t *dsdt; /**< The DSDT's AML. */
    uint32_t dsdt_bytes; /**< Its length. */
    uint8_t revision;    /**< The DSDT's revision: below 2, every integer is 32 bits. */
    const uint8_t *ssdt; /**< An SSDT's AML, or NULL. */
    uint32_t ssdt_bytes; /**< Its length. */
    bool ssdt_first;     /**< Whether the SSDT is declared before the DSDT, its integers then 64 bits. */
    bool every_branch;   /**< Whether the declaration pass walks every construct itself, Device bodies included. */
    bool steps_over;     /**< Whether the load with its shortcuts must step over a data object it built. */
} ShortcutsCase_t;

/**
 * @brief What a load ended with: what both ways must agree on, then what only the shortcuts change.
 */
typedef struct ShortcutsRecord {
    uint32_t fold;                                  /**< The namespace's fold. */
    uint32_t live_nodes;                            /**< Nodes in the namespace. */
    uint32_t declarations;                          /**< Nodes the tables created. */
    uint32_t duplicates;                            /**< Declarations of a name that existed. */
    uint32_t load_errors;                           /**< Faults of the declaration and Externals passes. */
    uint32_t first_load_error;                      /**< The first of them. */
    uint32_t values_deferred;                       /**< Objects that wait for the interpreter. */
    uint32_t declared_not_run;                      /**< Declarations made without running their branch. */
    uint32_t externals_unplaced;                    /**< Externals left to the Externals pass. */
    uint32_t decode_errors;                         /**< Faults of the decoding pass. */
    uint32_t undeclared_faults;                     /**< Faults inside constructs no pass declared. */
    uint32_t table_level_code;                      /**< Statements of table-level code. */
    uint32_t packages_padded;                       /**< Packages padded with Uninitialized. */
    uint32_t packages_truncated;                    /**< Packages whose extra initialisers were dropped. */
    uint32_t runs;                                  /**< Statements of table-level code run. */
    uint32_t failures;                              /**< Of those, the ones that failed. */
    uint32_t statuses[SHORTCUTS_STATUS_COUNT];      /**< The failures, per status. */
    uint32_t unimplemented[SHORTCUTS_OPCODE_COUNT]; /**< Opcodes run but not implemented. */
    ShortcutsTaken_t taken;                         /**< What only the shortcuts change. */
} ShortcutsRecord_t;

/**
 * @brief AML written from its end, so that what is written last wraps what was written before it.
 */
typedef struct ShortcutsBackwards {
    uint8_t bytes[SHORTCUTS_AML_CAPACITY]; /**< The AML, which ends at the end of the buffer. */
    uint32_t start;                        /**< Where it starts. */
    bool overflowed;                       /**< Whether a write did not fit, and was dropped. */
} ShortcutsBackwards_t;

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t shortcuts_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t shortcuts_load;
static uint8_t shortcuts_dsdt[MADE_UP_TABLE_HEADER + SHORTCUTS_AML_CAPACITY];
static uint8_t shortcuts_ssdt[MADE_UP_TABLE_HEADER + SHORTCUTS_AML_CAPACITY];
static uint8_t shortcuts_mutant[SHORTCUTS_AML_CAPACITY];
static ShortcutsBackwards_t shortcuts_backwards;

/**
 * @brief Names of every kind of constant data: `Name (I0__, Zero)`, `One`, `Ones`, a byte, a word, a double
 *        word and a quad word, `"abc"`, `Buffer (4) {1, 2}`, `Buffer (2) {1, 2, 3}`, `Buffer (0) {}`,
 *        `Package (3) {Ones, QWord, "x"}`, `Package (1) {1, 2, 3}`, `Package { Package { Package { I0__ } } }`,
 *        `VarPackage (2) {One}` and `Package (2) {"s", Buffer (1) {7}}`.
 */
static const uint8_t shortcuts_constants[] = {
    0x08u, 'I',   '0',   '_',   '_',   0x00u, 0x08u, 'I',   '1',   '_',   '_',   0x01u, 0x08u, 'I',   '2',
    '_',   '_',   0xFFu, 0x08u, 'I',   '3',   '_',   '_',   0x0Au, 0x12u, 0x08u, 'I',   '4',   '_',   '_',
    0x0Bu, 0x34u, 0x12u, 0x08u, 'I',   '5',   '_',   '_',   0x0Cu, 0x78u, 0x56u, 0x34u, 0x12u, 0x08u, 'I',
    '6',   '_',   '_',   0x0Eu, 0xEFu, 0xCDu, 0xABu, 0x89u, 0x67u, 0x45u, 0x23u, 0x01u, 0x08u, 'S',   '0',
    '_',   '_',   0x0Du, 'a',   'b',   'c',   0x00u, 0x08u, 'B',   '0',   '_',   '_',   0x11u, 0x05u, 0x0Au,
    0x04u, 0x01u, 0x02u, 0x08u, 'B',   '1',   '_',   '_',   0x11u, 0x06u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x03u,
    0x08u, 'B',   '2',   '_',   '_',   0x11u, 0x03u, 0x0Au, 0x00u, 0x08u, 'P',   '0',   '_',   '_',   0x12u,
    0x0Fu, 0x03u, 0xFFu, 0x0Eu, 0xEFu, 0xCDu, 0xABu, 0x89u, 0x67u, 0x45u, 0x23u, 0x01u, 0x0Du, 'x',   0x00u,
    0x08u, 'P',   '1',   '_',   '_',   0x12u, 0x07u, 0x01u, 0x01u, 0x0Au, 0x02u, 0x0Au, 0x03u, 0x08u, 'P',
    '2',   '_',   '_',   0x12u, 0x0Cu, 0x01u, 0x12u, 0x09u, 0x01u, 0x12u, 0x06u, 0x01u, 'I',   '0',   '_',
    '_',   0x08u, 'V',   '0',   '_',   '_',   0x13u, 0x04u, 0x0Au, 0x02u, 0x01u, 0x08u, 'P',   '4',   '_',
    '_',   0x12u, 0x0Au, 0x02u, 0x0Du, 's',   0x00u, 0x11u, 0x04u, 0x0Au, 0x01u, 0x07u,
};

/**
 * @brief An SSDT declared before the DSDT, whose integers are then 64 bits: `Name (Q0__, Ones)` and
 *        `Name (Q1__, Package (2) {Ones, QWord})`.
 */
static const uint8_t shortcuts_wide[] = {
    0x08u, 'Q',   '0',   '_',   '_',   0xFFu, 0x08u, 'Q',   '1',   '_',   '_',   0x12u,
    0x0Cu, 0x02u, 0xFFu, 0x0Eu, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
};

/**
 * @brief Sizes a name gives: `Name (CNT_, 4)`, `Name (B3__, Buffer (CNT_) {})`,
 *        `Name (P3__, Package (2) {Package (3) {1}, Buffer (CNT_) {}})`, which pads one package and builds the
 *        other, `Method (MCNT) { Return (4) }` and `Name (B4__, Buffer (MCNT) {})`, which waits for the
 *        interpreter; then table-level code that stores 8 to CNT_.
 */
static const uint8_t shortcuts_named_sizes[] = {
    0x08u, 'C', 'N',   'T',   '_',   0x0Au, 0x04u, 0x08u, 'B',   '3',   '_',   '_',   0x11u, 0x05u, 'C',   'N',
    'T',   '_', 0x08u, 'P',   '3',   '_',   '_',   0x12u, 0x0Cu, 0x02u, 0x12u, 0x03u, 0x03u, 0x01u, 0x11u, 0x05u,
    'C',   'N', 'T',   '_',   0x14u, 0x09u, 'M',   'C',   'N',   'T',   0x00u, 0xA4u, 0x0Au, 0x04u, 0x08u, 'B',
    '4',   '_', '_',   0x11u, 0x05u, 'M',   'C',   'N',   'T',   0x70u, 0x0Au, 0x08u, 'C',   'N',   'T',   '_',
};

/**
 * @brief Data the decoder refuses: a package of an OperationRegion opcode, a package whose String has no NUL,
 *        and a duplicate Name whose package is the first's.
 */
static const uint8_t shortcuts_malformed[] = {
    0x08u, 'M', '0', '_',   '_',   0x12u, 0x08u, 0x01u, 0x5Bu, 0x80u, 'R',   'G',   'N',   '_',
    0x08u, 'M', '1', '_',   '_',   0x12u, 0x06u, 0x01u, 0x0Du, 'a',   'b',   'c',   0x08u, 'D',
    'U',   'P', '_', 0x00u, 0x08u, 'D',   'U',   'P',   '_',   0x12u, 0x08u, 0x01u, 0x5Bu, 0x80u,
    'R',   'G', 'N', '_',   0x08u, 'O',   'K',   '_',   '_',   0x12u, 0x03u, 0x01u, 0x01u,
};

/**
 * @brief A package whose inner package's PkgLength crosses its parent, which abandons the rest of the table
 *        (§6.2), after a Name the load builds: `Name (G0__, Package (1) {One})`.
 */
static const uint8_t shortcuts_crossing[] = {
    0x08u, 'G', '0', '_',   '_',   0x12u, 0x03u, 0x01u, 0x01u, 0x08u, 'X',
    '0',   '_', '_', 0x12u, 0x05u, 0x01u, 0x12u, 0x09u, 0x01u, 0x00u,
};

/**
 * @brief Prepends @p length bytes to @p backwards, or marks it overflowed when they do not fit.
 */
static void shortcuts_prepend(ShortcutsBackwards_t *backwards, const uint8_t *bytes, uint32_t length)
{
    if (length > backwards->start)
    {
        backwards->overflowed = true;
        return;
    }
    backwards->start -= length;
    memcpy(backwards->bytes + backwards->start, bytes, length);
}

/**
 * @brief Prepends the PkgLength of a construct whose @p content bytes follow it, in one, two or three bytes.
 */
static void shortcuts_prepend_package_length(ShortcutsBackwards_t *backwards, uint32_t content)
{
    const uint32_t follow = (content + 1u < 0x40u) ? 0u : (content + 2u < 0x1000u) ? 1u : 2u;
    const uint32_t total = content + 1u + follow;
    const uint8_t encoded[3] = {(uint8_t) ((follow == 0u) ? total : ((follow << 6u) | (total & 0x0Fu))),
                                (uint8_t) (total >> 4u), (uint8_t) (total >> 12u)};

    shortcuts_prepend(backwards, encoded, 1u + follow);
}

/**
 * @brief Wraps what @p backwards holds up to @p end in a `Package (1) {...}`.
 */
static void shortcuts_wrap_package(ShortcutsBackwards_t *backwards, uint32_t end)
{
    static const uint8_t opcode[] = {0x12u};
    static const uint8_t count[] = {0x01u};

    shortcuts_prepend(backwards, count, sizeof(count));
    shortcuts_prepend_package_length(backwards, end - backwards->start);
    shortcuts_prepend(backwards, opcode, sizeof(opcode));
}

/**
 * @brief Writes `Name (name, Package (1) {...})` nested @p depth packages deep around Zero, then wraps it in
 *        @p devices nested Devices; with a top-level `Name (TOP_, Package (1) {One})` after them.
 */
static void shortcuts_write_nested(ShortcutsBackwards_t *backwards, const char *name, uint32_t depth, uint32_t devices)
{
    static const uint8_t zero[] = {0x00u};
    static const uint8_t name_opcode[] = {0x08u};
    static const uint8_t device_opcode[] = {0x5Bu, 0x82u};
    static const uint8_t top[] = {0x08u, 'T', 'O', 'P', '_', 0x12u, 0x03u, 0x01u, 0x01u};

    memset(backwards, 0, sizeof(*backwards));
    backwards->start = SHORTCUTS_AML_CAPACITY;
    shortcuts_prepend(backwards, top, sizeof(top));

    const uint32_t end = backwards->start;

    shortcuts_prepend(backwards, zero, sizeof(zero));
    for (uint32_t level = 0u; level < depth; ++level)
        shortcuts_wrap_package(backwards, end);
    shortcuts_prepend(backwards, (const uint8_t *) name, 4u);
    shortcuts_prepend(backwards, name_opcode, sizeof(name_opcode));
    for (uint32_t device = 0u; device < devices; ++device)
    {
        const uint8_t segment[4] = {'D', (uint8_t) ('A' + device / 676u), (uint8_t) ('A' + (device / 26u) % 26u),
                                    (uint8_t) ('A' + device % 26u)};

        shortcuts_prepend(backwards, segment, sizeof(segment));
        shortcuts_prepend_package_length(backwards, end - backwards->start);
        shortcuts_prepend(backwards, device_opcode, sizeof(device_opcode));
    }
}

/**
 * @brief Copies the @p length bytes of @p table into shortcuts_mutant with the byte at @p offset changed to the value
 *        of @p turn: its value with one bit flipped, 0x00 or 0xFF, and the flipped value where the turn's value is
 *        the byte's own.
 */
static void shortcuts_change_one_byte(const uint8_t *table, uint32_t length, uint32_t offset, uint32_t turn)
{
    const uint8_t values[3] = {(uint8_t) (table[offset] ^ 0x40u), 0x00u, 0xFFu};
    const uint8_t value = values[turn % 3u];

    memcpy(shortcuts_mutant, table, length);
    shortcuts_mutant[offset] = (value == table[offset]) ? values[0] : value;
}

/**
 * @brief Loads a case into shortcuts_load, with its shortcuts or without, its method bodies decoded as in the
 *        debug kernel and the host tools, and records what it ended with.
 */
static bool shortcuts_record(KernelTest_t *test, const ShortcutsCase_t *load_case, bool general,
                             ShortcutsRecord_t *out_record)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = &shortcuts_load;
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *dsdt =
        made_up_definition_block(shortcuts_dsdt, "DSDT", load_case->revision, load_case->dsdt, load_case->dsdt_bytes);
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *ssdt =
        (load_case->ssdt != NULL) ?
            made_up_definition_block(shortcuts_ssdt, "SSDT", 2u, load_case->ssdt, load_case->ssdt_bytes) :
            NULL;

    advanced_configuration_and_power_interface_simulated_host_open(&shortcuts_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &shortcuts_load, &shortcuts_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
    {
        advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
        return false;
    }
    shortcuts_load.general_load = general;
    shortcuts_load.declares_every_branch = load_case->every_branch;
    if (ssdt != NULL && load_case->ssdt_first)
        (void) advanced_configuration_and_power_interface_machine_language_declare_table(&shortcuts_load, ssdt);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(&shortcuts_load, dsdt);
    if (ssdt != NULL && !load_case->ssdt_first)
        (void) advanced_configuration_and_power_interface_machine_language_declare_table(&shortcuts_load, ssdt);
    advanced_configuration_and_power_interface_machine_language_decode_tables(&shortcuts_load);
    memset(out_record, 0, sizeof(*out_record));
    out_record->fold = advanced_configuration_and_power_interface_machine_language_namespace_fold(load);
    out_record->live_nodes = load->space.live_nodes;
    out_record->declarations = load->declarations;
    out_record->duplicates = load->duplicates;
    out_record->load_errors = load->load_errors;
    out_record->first_load_error = load->first_load_error;
    out_record->values_deferred = load->values_deferred;
    out_record->declared_not_run = load->declared_not_run;
    out_record->externals_unplaced = load->externals_unplaced;
    out_record->decode_errors = load->decode_errors;
    out_record->undeclared_faults = load->undeclared_faults;
    out_record->table_level_code = load->table_level_code;
    out_record->packages_padded = load->heap.packages_padded;
    out_record->packages_truncated = load->heap.packages_truncated;
    out_record->runs = load->evaluation.runs;
    out_record->failures = load->evaluation.failures;
    memcpy(out_record->statuses, load->evaluation.statuses, sizeof(out_record->statuses));
    memcpy(out_record->unimplemented, load->evaluation.unimplemented, sizeof(out_record->unimplemented));
    out_record->taken.data_stepped_over = load->data_stepped_over;
    advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
    advanced_configuration_and_power_interface_simulated_host_close(&shortcuts_host);
    return true;
}

/**
 * @brief Whether a case loads to the same namespace and the same counts with the load's shortcuts and without,
 *        the load without them taking none, and the shortcuts stepped over a data object when the case says they
 *        must; @p out_taken receives what the shortcuts took.
 */
static bool shortcuts_agree(KernelTest_t *test, const ShortcutsCase_t *load_case, ShortcutsTaken_t *out_taken)
{
    static const ShortcutsTaken_t none = {0};
    ShortcutsRecord_t shortcut;
    ShortcutsRecord_t general;

    memset(out_taken, 0, sizeof(*out_taken));
    if (!shortcuts_record(test, load_case, false, &shortcut) || !shortcuts_record(test, load_case, true, &general))
        return false;
    *out_taken = shortcut.taken;
    return memcmp(&general.taken, &none, sizeof(none)) == 0 &&
           memcmp(&shortcut, &general, offsetof(ShortcutsRecord_t, taken)) == 0 &&
           (!load_case->steps_over || out_taken->data_stepped_over != 0u);
}

/**
 * @brief The bytes of @p table, and in @p out_length their length.
 */
static const uint8_t *shortcuts_table_bytes(ShortcutsTable_t table, uint32_t *out_length)
{
    switch (table)
    {
    case SHORTCUTS_TABLE_DATA:
    default: *out_length = sizeof(shortcuts_constants); return shortcuts_constants;
    }
}

void shortcuts_change_bytes(KernelTest_t *test, ShortcutsTable_t table, uint32_t stride, bool every_value,
                            ShortcutsMutations_t *out_mutations)
{
    uint32_t length = 0u;
    const uint8_t *bytes = shortcuts_table_bytes(table, &length);
    const ShortcutsCase_t mutant = {"", shortcuts_mutant, length, 2u, NULL, 0u, false, false, false};

    memset(out_mutations, 0, sizeof(*out_mutations));
    out_mutations->bytes = length;
    for (uint32_t offset = 0u; offset < length; offset += stride)
    {
        for (uint32_t turn = 0u; turn < (every_value ? 3u : 1u); ++turn)
        {
            ShortcutsTaken_t taken;

            shortcuts_change_one_byte(bytes, length, offset, every_value ? turn : offset / stride);
            out_mutations->disagreements += shortcuts_agree(test, &mutant, &taken) ? 0u : 1u;
            out_mutations->taken.data_stepped_over += taken.data_stepped_over;
            ++out_mutations->loads;
        }
    }
}

/**
 * @brief The declaration pass steps over a Name's data object it has built instead of decoding it again (O18),
 *        and a load with that shortcut ends with the namespace, the faults and the counts of a load without it:
 *        on data of every kind under revision 2, then 1, an SSDT declared before the DSDT, sizes a name gives,
 *        data the decoder refuses, a PkgLength that abandons the table, a package 33 deep, which needs the
 *        interpreter, and a package 32 deep inside 481 Devices, where decoding it meets the limit of 512 open
 *        constructs. In every case the shortcut steps over at least one data object, in these two the Name
 *        after the deep one.
 */
KERNEL_TEST(a_name_s_data_object_decoded_once_declares_what_two_decodings_declare)
{
    ShortcutsCase_t cases[] = {
        {"data of every kind under revision 2",          shortcuts_constants,   sizeof(shortcuts_constants),   2u, NULL, 0u,                     false,
         false,                                                                                                                                                true},
        {"data of every kind under revision 1",          shortcuts_constants,   sizeof(shortcuts_constants),   1u, NULL, 0u,                     false,
         false,                                                                                                                                                true},
        {"an SSDT declared before a DSDT of revision 1", shortcuts_constants,   sizeof(shortcuts_constants),   1u,
         shortcuts_wide,                                                                                                 sizeof(shortcuts_wide), true,  false, true},
        {"sizes a name gives",                           shortcuts_named_sizes, sizeof(shortcuts_named_sizes), 2u, NULL, 0u,                     false, false, true},
        {"data the decoder refuses",                     shortcuts_malformed,   sizeof(shortcuts_malformed),   2u, NULL, 0u,                     false, false,
         true                                                                                                                                                      },
        {"a PkgLength that abandons the table",          shortcuts_crossing,    sizeof(shortcuts_crossing),    2u, NULL, 0u,                     false,
         false,                                                                                                                                                true},
        {"a package 33 deep",                            NULL,                  0u,                            2u, NULL, 0u,                     false, false, true},
        {"a package 32 deep inside 481 Devices",         NULL,                  0u,                            2u, NULL, 0u,                     false, true,  true},
    };
    const uint32_t count = sizeof(cases) / sizeof(cases[0]);
    uint32_t stepped_over = 0u;

    for (uint32_t index = 0u; index < count; ++index)
    {
        ShortcutsTaken_t taken;

        if (cases[index].dsdt == NULL)
        {
            shortcuts_write_nested(&shortcuts_backwards, (index + 1u == count) ? "DEEP" : "N33_",
                                   (index + 1u == count) ?
                                       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES :
                                       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES +
                                           1u,
                                   (index + 1u == count) ? SHORTCUTS_DEEP_DEVICES : 0u);
            if (!kernel_test_check(test, !shortcuts_backwards.overflowed, "the nested table fits its buffer"))
                return;
            cases[index].dsdt = shortcuts_backwards.bytes + shortcuts_backwards.start;
            cases[index].dsdt_bytes = SHORTCUTS_AML_CAPACITY - shortcuts_backwards.start;
        }
        kernel_test_check(test, shortcuts_agree(test, &cases[index], &taken), cases[index].claim);
        stepped_over += taken.data_stepped_over;
    }
    kernel_test_measure(test, "data_stepped_over", stepped_over);
}

/**
 * @brief A sample of one-byte changes of a table of data, every 16th byte changed once, to its value with one bit
 *        flipped, to 0x00 or to 0xFF in turn, loads to the same namespace, faults and counts with the load's
 *        shortcuts and without, in ring 0 as on the host; host_shortcuts changes every byte to each of the three.
 */
KERNEL_TEST(a_sample_of_one_byte_changes_of_a_table_of_data_declares_the_same_both_ways)
{
    ShortcutsMutations_t mutations;

    shortcuts_change_bytes(test, SHORTCUTS_TABLE_DATA, SHORTCUTS_SAMPLE_STRIDE, false, &mutations);
    kernel_test_check(test,
                      mutations.loads == (mutations.bytes + SHORTCUTS_SAMPLE_STRIDE - 1u) / SHORTCUTS_SAMPLE_STRIDE &&
                          mutations.taken.data_stepped_over != 0u,
                      "every change of the sample was loaded both ways, and the shortcut stepped over data");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the shortcut and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "data_stepped_over", mutations.taken.data_stepped_over);
}
