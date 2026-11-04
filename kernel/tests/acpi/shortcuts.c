#include "shortcuts.h"
#include "made_up_aml.h"
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
    480u /**< Devices nested around the deep case's package: with the table's own construct, 481 open, so that a       \
            package 32 deep passes the 512 of §6.1 by one, the guard's own boundary. */
#define SHORTCUTS_NODE_CAPS                                                                                            \
    80u /**< One past the highest node cap the statements load under, from 1, which the predefined objects alone       \
           pass, to one that holds every name. */
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
    uint32_t max_nodes;  /**< The node cap the load opens with. */
} ShortcutsCase_t;

/**
 * @brief What a load ended with: what both ways must agree on, then what only the shortcuts change.
 */
typedef struct ShortcutsRecord {
    uint32_t opened;                                /**< 1 when the namespace opened under the case's caps, else 0. */
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
 *        and a duplicate Name whose package is the first's; then `Name (OK__, Package (1) {One})` and
 *        `Name (L0__, Package (1) {Local0})`, whose Local0 is no data object.
 */
static const uint8_t shortcuts_malformed[] = {
    0x08u, 'M', '0', '_',   '_',   0x12u, 0x08u, 0x01u, 0x5Bu, 0x80u, 'R',   'G', 'N',   '_',   0x08u, 'M',
    '1',   '_', '_', 0x12u, 0x06u, 0x01u, 0x0Du, 'a',   'b',   'c',   0x08u, 'D', 'U',   'P',   '_',   0x00u,
    0x08u, 'D', 'U', 'P',   '_',   0x12u, 0x08u, 0x01u, 0x5Bu, 0x80u, 'R',   'G', 'N',   '_',   0x08u, 'O',
    'K',   '_', '_', 0x12u, 0x03u, 0x01u, 0x01u, 0x08u, 'L',   '0',   '_',   '_', 0x12u, 0x03u, 0x01u, 0x60u,
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
 * @brief Statements the declaration pass's decoder takes (O19), as iasl compiles them: `Name (N0__, One)`,
 *        `Name (N1__, Package (2) {1, "a"})`, `Method (M0__, 0) {Return (One)}`,
 *        `Method (M1__, 7, Serialized, 15) {Return (Arg6)}`, `OperationRegion (R0__, SystemMemory, 0x100000, 0x20)`,
 *        `Field (R0__, ByteAcc, NoLock, Preserve) {F0__, 8, , 4, F1__, 4, Offset (4), F2__, 32}`,
 *        `Field (R0__, AnyAcc, Lock, WriteAsZeros) {AccessAs (DWordAcc), F3__, 32}`, `Device (D0__)` holding
 *        `Name (_HID, "LPL0001")`, `Method (_STA) {Return (0x0F)}`, `Device (D1__) {Name (_ADR, Zero)}`,
 *        `ThermalZone (TZ1_) {}` and `Scope (^D0__) {Name (S2__, 6)}`, then `Scope (D0__) {Name (S0__, 2)}`,
 *        `Scope (\_SB) {Device (D2__) {}}`, `Scope (\D0__.D1__) {Name (S1__, 3)}`,
 *        `ThermalZone (TZ0_) {Method (_TMP) {Return (3000)}}`, `Device (\_SB.D3__) {}`, `Name (\_SB.N2__, 4)`,
 *        `Method (\_SB.M2__) {}` and `Scope (\) {Name (RT__, 5)}`: 25 of them through the decoder, the
 *        OperationRegion and the Name and the Method of a path through the walk.
 */
static const uint8_t shortcuts_statements[] = {
    0x08u, 'N',   '0',   '_',   '_',   0x01u, 0x08u, 'N',   '1',   '_',   '_',   0x12u, 0x06u, 0x02u, 0x01u, 0x0Du,
    0x61u, 0x00u, 0x14u, 0x08u, 'M',   '0',   '_',   '_',   0x00u, 0xA4u, 0x01u, 0x14u, 0x08u, 'M',   '1',   '_',
    '_',   0xFFu, 0xA4u, 0x6Eu, 0x5Bu, 0x80u, 'R',   '0',   '_',   '_',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x10u, 0x00u,
    0x0Au, 0x20u, 0x5Bu, 0x81u, 0x19u, 'R',   '0',   '_',   '_',   0x01u, 'F',   '0',   '_',   '_',   0x08u, 0x00u,
    0x04u, 'F',   '1',   '_',   '_',   0x04u, 0x00u, 0x10u, 'F',   '2',   '_',   '_',   0x20u, 0x5Bu, 0x81u, 0x0Eu,
    'R',   '0',   '_',   '_',   0x50u, 0x01u, 0x03u, 0x00u, 'F',   '3',   '_',   '_',   0x20u, 0x5Bu, 0x82u, 0x3Fu,
    'D',   '0',   '_',   '_',   0x08u, '_',   'H',   'I',   'D',   0x0Du, 'L',   'P',   'L',   '0',   '0',   '0',
    '1',   0x00u, 0x14u, 0x09u, '_',   'S',   'T',   'A',   0x00u, 0xA4u, 0x0Au, 0x0Fu, 0x5Bu, 0x82u, 0x0Bu, 'D',
    '1',   '_',   '_',   0x08u, '_',   'A',   'D',   'R',   0x00u, 0x5Bu, 0x85u, 0x05u, 'T',   'Z',   '1',   '_',
    0x10u, 0x0Du, 0x5Eu, 'D',   '0',   '_',   '_',   0x08u, 'S',   '2',   '_',   '_',   0x0Au, 0x06u, 0x10u, 0x0Cu,
    'D',   '0',   '_',   '_',   0x08u, 'S',   '0',   '_',   '_',   0x0Au, 0x02u, 0x10u, 0x0Cu, '_',   'S',   'B',
    '_',   0x5Bu, 0x82u, 0x05u, 'D',   '2',   '_',   '_',   0x10u, 0x11u, 0x2Eu, 'D',   '0',   '_',   '_',   'D',
    '1',   '_',   '_',   0x08u, 'S',   '1',   '_',   '_',   0x0Au, 0x03u, 0x5Bu, 0x85u, 0x10u, 'T',   'Z',   '0',
    '_',   0x14u, 0x0Au, '_',   'T',   'M',   'P',   0x00u, 0xA4u, 0x0Bu, 0xB8u, 0x0Bu, 0x5Bu, 0x82u, 0x0Au, 0x2Eu,
    '_',   'S',   'B',   '_',   'D',   '3',   '_',   '_',   0x08u, 0x2Eu, '_',   'S',   'B',   '_',   'N',   '2',
    '_',   '_',   0x0Au, 0x04u, 0x14u, 0x0Bu, 0x2Eu, '_',   'S',   'B',   '_',   'M',   '2',   '_',   '_',   0x00u,
    0x10u, 0x0Au, 0x5Cu, 0x00u, 0x08u, 'R',   'T',   '_',   '_',   0x0Au, 0x05u,
};

/**
 * @brief Statements the decoder hands back to the walk at their first operand whose outcome differs:
 *        `OperationRegion (R0__, ...)` and `(R9__, ...)`, `Method (M0__, 0) {Return (One)}`, then
 *        `Method (M0__, 1) {Return (Arg0)}` again, `Device (D0__) {Name (A0__, 1)}`, then
 *        `Device (D0__) {Name (A1__, 2)}` again, whose body is skipped, `Device (D7__) {}`,
 *        `Scope (D8__) {Name (X0__, 1)}` of a name never declared, `Field (NOPE, ByteAcc, NoLock, Preserve)
 *        {G0__, 8}` of a region never declared and `Field (R0__, ...) {G1__, 8}`; then, by hand, a Name `1A__`
 *        inside `Device (D9__)` and a Method `2M__`, whose NameSeg starts with a digit, which the NameString reader
 *        refuses, a Method and a Field whose PkgLength ends at their name; then, each in a Device of its own
 *        (`DA__` to `DE__`), a Method whose PkgLength ends inside its NameSeg, a Device and a Scope whose PkgLength
 *        ends inside their NameString, `Name (NS__, Alias (NS__, ALS_))`, whose data object is a statement, and
 *        `Field (NOPE)` whose PkgLength ends at its name and whose region was never declared; and a Name cut by the
 *        table's end: 13 of them through the decoder, the 5 Devices among them.
 */
static const uint8_t shortcuts_handed_back[] = {
    0x5Bu, 0x80u, 'R',   '0',   '_',   '_',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x10u, 0x00u, 0x0Au, 0x20u, 0x5Bu, 0x80u,
    'R',   '9',   '_',   '_',   0x00u, 0x0Cu, 0x00u, 0x00u, 0x20u, 0x00u, 0x0Au, 0x20u, 0x14u, 0x08u, 'M',   '0',
    '_',   '_',   0x00u, 0xA4u, 0x01u, 0x14u, 0x08u, 'M',   '0',   '_',   '_',   0x01u, 0xA4u, 0x68u, 0x5Bu, 0x82u,
    0x0Bu, 'D',   '0',   '_',   '_',   0x08u, 'A',   '0',   '_',   '_',   0x01u, 0x5Bu, 0x82u, 0x0Cu, 'D',   '0',
    '_',   '_',   0x08u, 'A',   '1',   '_',   '_',   0x0Au, 0x02u, 0x5Bu, 0x82u, 0x05u, 'D',   '7',   '_',   '_',
    0x10u, 0x0Bu, 'D',   '8',   '_',   '_',   0x08u, 'X',   '0',   '_',   '_',   0x01u, 0x5Bu, 0x81u, 0x0Bu, 'N',
    'O',   'P',   'E',   0x01u, 'G',   '0',   '_',   '_',   0x08u, 0x5Bu, 0x81u, 0x0Bu, 'R',   '0',   '_',   '_',
    0x01u, 'G',   '1',   '_',   '_',   0x08u, 0x5Bu, 0x82u, 0x0Bu, 'D',   '9',   '_',   '_',   0x08u, '1',   'A',
    '_',   '_',   0x01u, 0x14u, 0x06u, '2',   'M',   '_',   '_',   0x00u, 0x14u, 0x05u, 'M',   'C',   'U',   'T',
    0x5Bu, 0x81u, 0x05u, 'R',   '0',   '_',   '_',   0x5Bu, 0x82u, 0x0Bu, 'D',   'A',   '_',   '_',   0x14u, 0x03u,
    'M',   'X',   '_',   '_',   0x5Bu, 0x82u, 0x0Cu, 'D',   'B',   '_',   '_',   0x5Bu, 0x82u, 0x03u, 'D',   'X',
    '_',   '_',   0x5Bu, 0x82u, 0x0Bu, 'D',   'C',   '_',   '_',   0x10u, 0x04u, 0x5Cu, 'S',   'B',   '_',   0x5Bu,
    0x82u, 0x13u, 'D',   'D',   '_',   '_',   0x08u, 'N',   'S',   '_',   '_',   0x06u, 'N',   'S',   '_',   '_',
    'A',   'L',   'S',   '_',   0x5Bu, 0x82u, 0x0Cu, 'D',   'E',   '_',   '_',   0x5Bu, 0x81u, 0x05u, 'N',   'O',
    'P',   'E',   0x08u, 'N',   'C',   'U',   'T',
};

/**
 * @brief Statements inside table-level code: `Name (N0__, One)`, `If (N0__) {Device (D0__) {Name (N1__, 1)}
 *        Method (M0__) {}} Else {Name (N2__, 2)}` and `Store (2, N0__)`: through the decoder, the 4 declarations
 *        of the code the load runs, the Else stepped over, and the 5 of both branches where it declares every
 *        branch.
 */
static const uint8_t shortcuts_table_level_code[] = {
    0x08u, 'N',   '0',   '_',   '_', 0x01u, 0xA0u, 0x19u, 'N',   '0',   '_',   '_',   0x5Bu, 0x82u, 0x0Bu, 'D',
    '0',   '_',   '_',   0x08u, 'N', '1',   '_',   '_',   0x01u, 0x14u, 0x06u, 'M',   '0',   '_',   '_',   0x00u,
    0xA1u, 0x08u, 0x08u, 'N',   '2', '_',   '_',   0x0Au, 0x02u, 0x70u, 0x0Au, 0x02u, 'N',   '0',   '_',   '_',
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
    uint8_t encoded[MADE_UP_AML_PACKAGE_LENGTH_BYTES];
    const uint32_t bytes = made_up_aml_encode_package_length(content, true, encoded);

    shortcuts_prepend(backwards, encoded, bytes);
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
 *        debug kernel and the host tools, and records what it ended with; a namespace its node cap does not let
 *        open records that alone.
 */
static void shortcuts_record(const ShortcutsCase_t *load_case, bool general, ShortcutsRecord_t *out_record)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = &shortcuts_load;
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *dsdt =
        made_up_definition_block(shortcuts_dsdt, "DSDT", load_case->revision, load_case->dsdt, load_case->dsdt_bytes);
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *ssdt =
        (load_case->ssdt != NULL) ?
            made_up_definition_block(shortcuts_ssdt, "SSDT", 2u, load_case->ssdt, load_case->ssdt_bytes) :
            NULL;

    memset(out_record, 0, sizeof(*out_record));
    advanced_configuration_and_power_interface_simulated_host_open(&shortcuts_host);
    if (!advanced_configuration_and_power_interface_machine_language_open(
            &shortcuts_load, &shortcuts_host.host, load_case->max_nodes, MADE_UP_MAX_ARENA_BYTES))
    {
        advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
        advanced_configuration_and_power_interface_simulated_host_close(&shortcuts_host);
        return;
    }
    out_record->opened = 1u;
    shortcuts_load.general_load = general;
    shortcuts_load.declares_every_branch = load_case->every_branch;
    if (ssdt != NULL && load_case->ssdt_first)
        (void) advanced_configuration_and_power_interface_machine_language_declare_table(&shortcuts_load, ssdt);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(&shortcuts_load, dsdt);
    if (ssdt != NULL && !load_case->ssdt_first)
        (void) advanced_configuration_and_power_interface_machine_language_declare_table(&shortcuts_load, ssdt);
    advanced_configuration_and_power_interface_machine_language_decode_tables(&shortcuts_load);
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
    out_record->taken.declarations_decoded_directly = load->declarations_decoded_directly;
    advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
    advanced_configuration_and_power_interface_simulated_host_close(&shortcuts_host);
}

/**
 * @brief Whether a case loads to the same namespace and the same counts of what it declared with the load's
 *        shortcuts and without, the load without them taking none, and the shortcuts stepped over a data object
 *        when the case says they must; @p out_taken receives what the shortcuts took. A case under the made-up
 *        node cap must open its namespace; under a lower cap, both loads may fail to open it alike.
 */
static bool shortcuts_agree(KernelTest_t *test, const ShortcutsCase_t *load_case, ShortcutsTaken_t *out_taken)
{
    static const ShortcutsTaken_t none = {0};
    ShortcutsRecord_t shortcut;
    ShortcutsRecord_t general;

    memset(out_taken, 0, sizeof(*out_taken));
    shortcuts_record(load_case, false, &shortcut);
    shortcuts_record(load_case, true, &general);
    if (!kernel_test_check(test, shortcut.opened != 0u || load_case->max_nodes < MADE_UP_MAX_NODES,
                           "the namespace opens"))
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
    case SHORTCUTS_TABLE_STATEMENTS: *out_length = sizeof(shortcuts_statements); return shortcuts_statements;
    case SHORTCUTS_TABLE_DATA:
    default: *out_length = sizeof(shortcuts_constants); return shortcuts_constants;
    }
}

void shortcuts_change_bytes(KernelTest_t *test, ShortcutsTable_t table, uint32_t stride, bool every_value,
                            ShortcutsMutations_t *out_mutations)
{
    uint32_t length = 0u;
    const uint8_t *bytes = shortcuts_table_bytes(table, &length);
    const ShortcutsCase_t mutant = {"", shortcuts_mutant, length, 2u, NULL, 0u, false, false, false, MADE_UP_MAX_NODES};

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
            out_mutations->taken.declarations_decoded_directly += taken.declarations_decoded_directly;
            ++out_mutations->loads;
        }
    }
}

/**
 * @brief The declaration pass steps over a Name's data object it has built instead of decoding it again (O18),
 *        and a load with that shortcut ends with the namespace, the faults and the counts of what it declared of a
 *        load without it, its work (lookups, probes, the stacks' peaks) falling:
 *        on data of every kind under revision 2, then 1, an SSDT declared before the DSDT, sizes a name gives,
 *        data the decoder refuses, a PkgLength that abandons the table, a package 33 deep, which needs the
 *        interpreter, and a package 32 deep inside 480 Devices, where decoding it meets the limit of 512 open
 *        constructs. In every case the shortcut steps over at least one data object, in these two the Name
 *        after the deep one.
 */
KERNEL_TEST(a_name_s_data_object_decoded_once_declares_what_two_decodings_declare)
{
    ShortcutsCase_t cases[] = {
        {"data of every kind under revision 2",          shortcuts_constants,   sizeof(shortcuts_constants),   2u, NULL, 0u,                     false,
         false,                                                                                                                                                true, MADE_UP_MAX_NODES},
        {"data of every kind under revision 1",          shortcuts_constants,   sizeof(shortcuts_constants),   1u, NULL, 0u,                     false,
         false,                                                                                                                                                true, MADE_UP_MAX_NODES},
        {"an SSDT declared before a DSDT of revision 1", shortcuts_constants,   sizeof(shortcuts_constants),   1u,
         shortcuts_wide,                                                                                                 sizeof(shortcuts_wide), true,  false, true, MADE_UP_MAX_NODES},
        {"sizes a name gives",                           shortcuts_named_sizes, sizeof(shortcuts_named_sizes), 2u, NULL, 0u,                     false, false, true,
         MADE_UP_MAX_NODES                                                                                                                                                            },
        {"data the decoder refuses",                     shortcuts_malformed,   sizeof(shortcuts_malformed),   2u, NULL, 0u,                     false, false, true,
         MADE_UP_MAX_NODES                                                                                                                                                            },
        {"a PkgLength that abandons the table",          shortcuts_crossing,    sizeof(shortcuts_crossing),    2u, NULL, 0u,                     false,
         false,                                                                                                                                                true, MADE_UP_MAX_NODES},
        {"a package 33 deep",                            NULL,                  0u,                            2u, NULL, 0u,                     false, false, true, MADE_UP_MAX_NODES},
        {"a package 32 deep inside 480 Devices",         NULL,                  0u,                            2u, NULL, 0u,                     false, true,  true, MADE_UP_MAX_NODES},
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
 *        flipped, to 0x00 or to 0xFF in turn, loads to the same namespace, faults and counts of what it declared
 *        with the load's shortcuts and without, in ring 0 as on the host; host_shortcuts changes every byte to each
 *        of the three.
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

/**
 * @brief The declaration pass takes a Name or a Method of one NameSeg, a Device, a ThermalZone, a Scope and a
 *        Field through its own decoder (O19), which hands a statement back to the walk at its first operand whose
 *        outcome differs; a load with that decoder ends with the namespace, the faults and the counts of what it
 *        declared of a load without it, its work (lookups, probes, the stacks' peaks) falling, and its decoder took
 *        the statements each table's documentation counts: on statements of every shape it takes, under revision 2
 *        then 1, on statements it hands back, and on the declarations of table-level code, run and declared in every
 *        branch.
 */
KERNEL_TEST(the_declaration_decoder_declares_what_the_walk_declares)
{
    const ShortcutsCase_t cases[] = {
        {"statements the decoder takes, under revision 2",                      shortcuts_statements,       sizeof(shortcuts_statements),       2u, NULL,
         0u,                                                                                                                                                  false, false, true, MADE_UP_MAX_NODES},
        {"statements the decoder takes, under revision 1",                      shortcuts_statements,       sizeof(shortcuts_statements),       1u, NULL,
         0u,                                                                                                                                                  false, false, true, MADE_UP_MAX_NODES},
        {"statements the decoder hands back",                                   shortcuts_handed_back,      sizeof(shortcuts_handed_back),      2u, NULL, 0u, false,
         false,                                                                                                                                                             true, MADE_UP_MAX_NODES},
        {"the declarations of table-level code it runs",                        shortcuts_table_level_code, sizeof(shortcuts_table_level_code),
         2u,                                                                                                                                        NULL, 0u, false, false, true, MADE_UP_MAX_NODES},
        {"the declarations of table-level code whose every branch it declares", shortcuts_table_level_code,
         sizeof(shortcuts_table_level_code),                                                                                                    2u, NULL, 0u, false, true,  true, MADE_UP_MAX_NODES},
    };
    const uint32_t decoded[] = {25u, 25u, 13u, 4u, 5u};
    uint32_t decoded_directly = 0u;

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        ShortcutsTaken_t taken;

        kernel_test_check(
            test, shortcuts_agree(test, &cases[index], &taken) && taken.declarations_decoded_directly == decoded[index],
            cases[index].claim);
        decoded_directly += taken.declarations_decoded_directly;
    }
    kernel_test_measure(test, "declarations_decoded_directly", decoded_directly);
}

/**
 * @brief The namespace full at every statement: the statements the decoder takes load under each node cap from 1 to
 *        79 to the same namespace, faults and counts of what they declared with the load's shortcuts and without,
 *        whether the cap refuses the predefined objects, so that neither load opens its namespace, the name of a
 *        Name, a Method or a construct the decoder takes, or none.
 */
KERNEL_TEST(the_declaration_decoder_declares_what_the_walk_declares_under_every_node_cap)
{
    uint32_t disagreements = 0u;
    uint32_t taking = 0u;

    for (uint32_t cap = 1u; cap < SHORTCUTS_NODE_CAPS; ++cap)
    {
        const ShortcutsCase_t load_case = {
            "", shortcuts_statements, sizeof(shortcuts_statements), 2u, NULL, 0u, false, false, false, cap};
        ShortcutsTaken_t taken;

        disagreements += shortcuts_agree(test, &load_case, &taken) ? 0u : 1u;
        taking += (taken.declarations_decoded_directly != 0u) ? 1u : 0u;
    }
    kernel_test_check(test, taking != 0u && taking < SHORTCUTS_NODE_CAPS - 1u,
                      "some caps refuse the namespace, the others let the decoder take statements");
    kernel_test_check(test, disagreements == 0u, "every cap declares the same with the decoder and without");
    kernel_test_measure(test, "caps_taking_statements", taking);
}

/**
 * @brief A sample of one-byte changes of the statements the decoder takes, every 16th byte changed once, to its value
 *        with one bit flipped, to 0x00 or to 0xFF in turn, loads to the same namespace, faults and counts of what it
 *        declared with the load's shortcuts and without, in ring 0 as on the host; host_shortcuts changes every byte
 *        to each of the three.
 */
KERNEL_TEST(a_sample_of_one_byte_changes_of_a_table_of_statements_declares_the_same_both_ways)
{
    ShortcutsMutations_t mutations;

    shortcuts_change_bytes(test, SHORTCUTS_TABLE_STATEMENTS, SHORTCUTS_SAMPLE_STRIDE, false, &mutations);
    kernel_test_check(test,
                      mutations.loads == (mutations.bytes + SHORTCUTS_SAMPLE_STRIDE - 1u) / SHORTCUTS_SAMPLE_STRIDE &&
                          mutations.taken.declarations_decoded_directly != 0u,
                      "every change of the sample was loaded both ways, and the decoder took statements");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the decoder and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "declarations_decoded_directly", mutations.taken.declarations_decoded_directly);
}
