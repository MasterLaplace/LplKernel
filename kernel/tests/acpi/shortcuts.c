#include "fixtures/shortcuts.h"
#include "made_up_aml.h"
#include "made_up_table.h"
#include "shortcuts.h"
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
#define SHORTCUTS_KEPT_ARENA_BYTES                                                                                     \
    (16u * 1024u) /**< Bytes past the namespace's own pages in the case whose kept Buffer no longer fits. */
#define SHORTCUTS_NO_RUN_ARENA_BYTES                                                                                   \
    (4u * 1024u * 1024u) /**< Bytes the arena may hold where the heap takes every run it may hold: room for each of    \
                            them, two pages each, so that the run cap refuses the next one before the arena does. */
#define SHORTCUTS_STATUS_COUNT ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT
#define SHORTCUTS_OPCODE_COUNT ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT

/**
 * @brief How much room a load's heap has before its tables load.
 */
typedef enum ShortcutsHeap {
    SHORTCUTS_HEAP_ROOM,     /**< What the load opens with. */
    SHORTCUTS_HEAP_NO_ARENA, /**< None: the arena capped at what it holds, the run of small objects full. */
    SHORTCUTS_HEAP_NO_RUN,   /**< None: every run the heap may hold taken, the run of small objects full. */
} ShortcutsHeap_t;

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
    bool steps_over;     /**< Whether the load with its shortcuts must step over a data object, built or kept. */
    uint32_t max_nodes;  /**< The node cap the load opens with. */
} ShortcutsCase_t;

/**
 * @brief What a load ended with: what both ways must agree on, then what only the shortcuts change.
 */
typedef struct ShortcutsRecord {
    uint32_t opened;           /**< 1 when the namespace opened under the case's caps, else 0. */
    uint32_t filled;           /**< Empty Strings taken to leave the heap no room before the tables load, or 0. */
    uint32_t fold;             /**< The namespace's fold. */
    uint32_t built_fold;       /**< The namespace's fold once every data object kept in its table is built (O20). */
    uint32_t unbuilt;          /**< Of those kept objects, the ones the heap could not hold. */
    uint32_t built_padded;     /**< Packages padded with Uninitialized, once every kept object is built. */
    uint32_t built_truncated;  /**< Packages whose extra initialisers were dropped, once every kept object is built. */
    uint32_t built_faults;     /**< Kept objects left Uninitialized for a fault, once every kept object is built. */
    uint32_t built_deferred;   /**< Objects that wait for the interpreter, once every kept object is built. */
    uint32_t data_kept;        /**< Data objects of a Name kept in their table (O20). */
    uint32_t data_faults;      /**< Of those, the ones the check of kept data found would not build. */
    uint32_t data_named_sizes; /**< Of those, the ones the check of kept data found hold a size a name gives. */
    uint32_t live_nodes;       /**< Nodes in the namespace. */
    uint32_t declarations;     /**< Nodes the tables created. */
    uint32_t duplicates;       /**< Declarations of a name that existed. */
    uint32_t load_errors;      /**< Faults of the declaration and Externals passes. */
    uint32_t first_load_error; /**< The first of them. */
    uint32_t values_deferred;  /**< Objects that wait for the interpreter. */
    uint32_t declared_not_run; /**< Declarations made without running their branch. */
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
    uint32_t executed[SHORTCUTS_OPCODE_COUNT];      /**< Operations table-level code ran, per opcode. */
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

/**
 * @brief What the methods of shortcuts_reach ran returned in one load, and what its kept data became.
 */
typedef struct ShortcutsReached {
    uint8_t statuses[7];        /**< Each method's status, M0__ to M6__ in turn. */
    uint64_t results[7];        /**< The Integer each returned. */
    uint32_t data_kept;         /**< Data objects the load kept in their table. */
    uint32_t data_materialized; /**< Of those, the ones the methods' name resolutions built. */
} ShortcutsReached_t;

/**
 * @brief What the first read of a kept package finds.
 */
typedef enum ShortcutsFirstRead {
    SHORTCUTS_FIRST_READ_BUILDS, /**< The package builds. */
    SHORTCUTS_FIRST_READ_FAULT,  /**< A fault in its bytes: it is left Uninitialized, counted in data_faults_read. */
    SHORTCUTS_FIRST_READ_WAITS,  /**< A term that needs the interpreter: it is left Uninitialized, counted in
                                      values_deferred. */
} ShortcutsFirstRead_t;

/**
 * @brief A Name's package whose head reads, loaded as `Name (PKG_, ...)` between the readers of
 *        shortcuts_unbuilt_readers_aml and `Name (AFTR, 7)`.
 */
typedef struct ShortcutsUnbuilt {
    const char *claim;               /**< What the package is. */
    const uint8_t *bytes;            /**< Its bytes, or NULL for a Package 33 deep around One. */
    uint32_t length;                 /**< Their length. */
    ShortcutsFirstRead_t first_read; /**< What its first read finds. */
} ShortcutsUnbuilt_t;

/**
 * @brief What the loads of shortcuts_unbuilt_cases counted, over every case.
 */
typedef struct ShortcutsUnbuiltCounts {
    uint32_t data_kept;        /**< Data objects kept in their table. */
    uint32_t data_faults;      /**< Of those, the ones the check of kept data found would not build. */
    uint32_t data_faults_read; /**< Of those, the ones whose first read found a fault. */
    uint32_t values_deferred;  /**< Objects that wait for the interpreter, once every package was read. */
} ShortcutsUnbuiltCounts_t;

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
 *
 * It is raw bytes: the fixtures are compiled at revision 1, where iasl truncates a QWord to 32 bits, and iasl raises a
 * Package's count to its initialisers.
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
 *
 * It is raw bytes: the fixtures are compiled at revision 1, where iasl truncates a QWord to 32 bits.
 */
static const uint8_t shortcuts_wide[] = {
    0x08u, 'Q',   '0',   '_',   '_',   0xFFu, 0x08u, 'Q',   '1',   '_',   '_',   0x12u,
    0x0Cu, 0x02u, 0xFFu, 0x0Eu, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
};

/**
 * @brief Data the decoder refuses: a package of an OperationRegion opcode, a package whose String has no NUL,
 *        and a duplicate Name whose package is the first's; then `Name (OK__, Package (1) {One})`,
 *        `Name (L0__, Package (1) {Local0})`, whose Local0 is no data object, and `Name (B64_, Buffer (0x10001) {})`,
 *        past the 64 KiB of §6.1.
 *
 * It is raw bytes: iasl's grammar refuses an OperationRegion opcode or Local0 as a package element, and iasl emits no
 * String without its NUL.
 */
static const uint8_t shortcuts_malformed[] = {
    0x08u, 'M', '0', '_',   '_',   0x12u, 0x08u, 0x01u, 0x5Bu, 0x80u, 'R',   'G',   'N',   '_',   0x08u, 'M',
    '1',   '_', '_', 0x12u, 0x06u, 0x01u, 0x0Du, 'a',   'b',   'c',   0x08u, 'D',   'U',   'P',   '_',   0x00u,
    0x08u, 'D', 'U', 'P',   '_',   0x12u, 0x08u, 0x01u, 0x5Bu, 0x80u, 'R',   'G',   'N',   '_',   0x08u, 'O',
    'K',   '_', '_', 0x12u, 0x03u, 0x01u, 0x01u, 0x08u, 'L',   '0',   '_',   '_',   0x12u, 0x03u, 0x01u, 0x60u,
    0x08u, 'B', '6', '4',   '_',   0x11u, 0x06u, 0x0Cu, 0x01u, 0x00u, 0x01u, 0x00u,
};

/**
 * @brief A package whose inner package's PkgLength crosses its parent, which stops the decoding of the rest of the
 *        table (§6.2), after a Name the load builds, `Name (G0__, Package (1) {One})`, and before
 *        `Name (X1__, Package (1) {...})`, which holds the byte 0x02, which starts no data object.
 *
 * It is raw bytes: iasl emits no PkgLength that crosses its parent's and no element that is the byte 0x02.
 */
static const uint8_t shortcuts_crossing[] = {
    0x08u, 'G',   '0',   '_',   '_',   0x12u, 0x03u, 0x01u, 0x01u, 0x08u, 'X', '0',   '_',   '_',   0x12u,
    0x05u, 0x01u, 0x12u, 0x09u, 0x01u, 0x00u, 0x08u, 'X',   '1',   '_',   '_', 0x12u, 0x03u, 0x01u, 0x02u,
};

/**
 * @brief `Name (VPX_, VarPackage (0x1001) {})`, one element past the 4,096 of §6.1, which no load keeps,
 *        `Name (DUP_, Package (1) {One})` then `Name (DUP_, Package (1) {...})` again, whose duplicate holds the byte
 *        0x02, which starts no data object, then `Name (CNT2, 2)` and `Name (VPK_, VarPackage (CNT2) {Package (3)
 *        {One}, ...})`, not kept for its count, whose inner Package is padded before such a byte.
 *
 * It is raw bytes: iasl emits no element that is the byte 0x02.
 */
static const uint8_t shortcuts_kept_refused_and_duplicate[] = {
    0x08u, 'V',   'P',   'X', '_', 0x13u, 0x04u, 0x0Bu, 0x01u, 0x10u, 0x08u, 'D',   'U',   'P',   '_',   0x12u, 0x03u,
    0x01u, 0x01u, 0x08u, 'D', 'U', 'P',   '_',   0x12u, 0x03u, 0x01u, 0x02u, 0x08u, 'C',   'N',   'T',   '2',   0x0Au,
    0x02u, 0x08u, 'V',   'P', 'K', '_',   0x13u, 0x0Au, 'C',   'N',   'T',   '2',   0x12u, 0x03u, 0x03u, 0x01u, 0x02u,
};

/**
 * @brief Integers of a Name the declaration pass's decoder leaves to the general path: `Name (IR__, Revision)`,
 *        `Name (IN__, IR__)`, then `Name (IO__, One)`, which it builds, `Name (IO__, Ones)` again, and a QWord whose
 *        last byte the table's end cuts, `Name (IQ__, ...)`.
 *
 * It is raw bytes: iasl emits no QWord the table's end cuts.
 */
static const uint8_t shortcuts_integers_refused[] = {
    0x08u, 'I', 'R',   '_', '_', 0x5Bu, 0x30u, 0x08u, 'I',   'N',   '_',   '_',   'I',   'R',
    '_',   '_', 0x08u, 'I', 'O', '_',   '_',   0x01u, 0x08u, 'I',   'O',   '_',   '_',   0xFFu,
    0x08u, 'I', 'Q',   '_', '_', 0x0Eu, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u,
};

/**
 * @brief `Name (HB__, Package (1) {...})`, whose inner PkgLength of 1 leaves its count outside the package.
 *
 * It is raw bytes: iasl emits no PkgLength that leaves a Package's count outside it.
 */
static const uint8_t shortcuts_head_without_count[] = {0x08u, 'H',   'B',   '_',   '_',  0x12u,
                                                       0x05u, 0x01u, 0x12u, 0x01u, 0x00u};

/**
 * @brief `Name (HD__, Package (1) {...})`, whose inner PkgLength of 3 passes its parent's end by one byte.
 *
 * It is raw bytes: iasl emits no PkgLength that passes its parent's end.
 */
static const uint8_t shortcuts_head_past_parent[] = {0x08u, 'H',   'D',   '_',   '_',  0x12u,
                                                     0x05u, 0x01u, 0x12u, 0x03u, 0x00u};

/**
 * @brief `Name (HE__, Package (1) {Package (1) {One}})`, whose inner PkgLength of 4 takes two bytes.
 *
 * It is raw bytes: iasl writes a PkgLength that one byte holds in one byte.
 */
static const uint8_t shortcuts_head_two_bytes[] = {0x08u, 'H',   'E',   '_',   '_',   0x12u, 0x07u,
                                                   0x01u, 0x12u, 0x44u, 0x00u, 0x01u, 0x01u};

/**
 * @brief `Name (HG__, Package (1) {VarPackage (One) {One}})`, a VarPackage whose PkgLength is one byte.
 *
 * It is raw bytes: iasl emits a Package, not a VarPackage, of a constant count below 256.
 */
static const uint8_t shortcuts_head_variable[] = {0x08u, 'H',   'G',   '_',   '_',   0x12u,
                                                  0x06u, 0x01u, 0x13u, 0x03u, 0x01u, 0x01u};

/**
 * @brief Statements the decoder hands back to the walk at their first operand whose outcome differs, after
 *        `OperationRegion (R0__, ...)` and `(R9__, ...)`, which it takes: `Method (M0__, 0) {Return (One)}`, then
 *        `Method (M0__, 1) {Return (Arg0)}` again, `Device (D0__) {Name (A0__, 1)}`, then
 *        `Device (D0__) {Name (A1__, 2)}` again, whose body is skipped, `Device (D7__) {}`,
 *        `Scope (D8__) {Name (X0__, 1)}` of a name never declared, `Field (NOPE, ByteAcc, NoLock, Preserve)
 *        {G0__, 8}` of a region never declared and `Field (R0__, ...) {G1__, 8}`; then, by hand, a Name `1A__`
 *        inside `Device (D9__)` and a Method `2M__`, whose NameSeg starts with a digit, which the NameString reader
 *        refuses, a Method and a Field whose PkgLength ends at their name; then, each in a Device of its own
 *        (`DA__` to `DE__`), a Method whose PkgLength ends inside its NameSeg, a Device and a Scope whose PkgLength
 *        ends inside their NameString, `Name (NS__, Alias (NS__, ALS_))`, whose data object is a statement, and
 *        `Field (NOPE)` whose PkgLength ends at its name and whose region was never declared; and a Name cut by the
 *        table's end: 15 of them through the decoder, the 2 OperationRegions and the 5 Devices among them.
 *
 * It is raw bytes: iasl emits no NameSeg that starts with a digit, no PkgLength that ends inside a name, and no term
 * the table's end cuts.
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
 * @brief OperationRegions, as iasl compiles them, the decoder taking those of one NameSeg whose offset and length need
 *        no interpreter: `OperationRegion (RB__, SystemIO, Zero, One)`, `(RW__, PCI_Config, 0x40, 0x0100)`, `(RQ__,
 *        SystemMemory, 0x0000000100000000, Ones)`, then `Name (OFS_, 0x80)`, `Name (DSDT, 0x10)`, the name the table's
 *        first bytes spell, and `OperationRegion (RN__, EmbeddedControl, OFS_, 2)`, and `Device (DR__) {OperationRegion
 *        (RD__, SystemMemory, 0x1000, 0x10)}`; the decoder hands back to the walk at their RegionSpace `OperationRegion
 *        (RB__, SystemMemory, 0x10, 1)` again, `(RX__, SystemIO, Add (OFS_, 4), 8)`, `(RM__, SystemIO, 0x60, MLEN)`
 *        after `Method (MLEN) {Return (6)}`, and `(RU__, SystemIO, UNDF, 1)` of a name never declared; the walk takes
 *        whole `OperationRegion (\RP__, SystemIO, 0x70, 2)` and `(DR__.RE__, SystemIO, 0x72, 2)`; then, by hand, each
 *        in a Device of its own (`DS__` to `DU__`), a region whose NameSeg starts with a digit, which the NameString
 *        reader refuses, one whose RegionSpace and one whose length the Device's PkgLength cuts; and a region whose
 *        NameSeg the table's end cuts: 12 of them through the decoder, the 2 Names, the Method and the 4 Devices among
 *        them.
 *
 * It is raw bytes: iasl emits no NameSeg that starts with a digit, no PkgLength that cuts a region, and no term the
 * table's end cuts.
 */
static const uint8_t shortcuts_regions[] = {
    0x5Bu, 0x80u, 'R',   'B',   '_',   '_',   0x01u, 0x00u, 0x01u, 0x5Bu, 0x80u, 'R',   'W',   '_',   '_',   0x02u,
    0x0Au, 0x40u, 0x0Bu, 0x00u, 0x01u, 0x5Bu, 0x80u, 'R',   'Q',   '_',   '_',   0x00u, 0x0Eu, 0x00u, 0x00u, 0x00u,
    0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0xFFu, 0x08u, 'O',   'F',   'S',   '_',   0x0Au, 0x80u, 0x08u, 'D',   'S',
    'D',   'T',   0x0Au, 0x10u, 0x5Bu, 0x80u, 'R',   'N',   '_',   '_',   0x03u, 'O',   'F',   'S',   '_',   0x0Au,
    0x02u, 0x5Bu, 0x82u, 0x11u, 'D',   'R',   '_',   '_',   0x5Bu, 0x80u, 'R',   'D',   '_',   '_',   0x00u, 0x0Bu,
    0x00u, 0x10u, 0x0Au, 0x10u, 0x5Bu, 0x80u, 'R',   'B',   '_',   '_',   0x00u, 0x0Au, 0x10u, 0x01u, 0x5Bu, 0x80u,
    'R',   'X',   '_',   '_',   0x01u, 0x72u, 'O',   'F',   'S',   '_',   0x0Au, 0x04u, 0x00u, 0x0Au, 0x08u, 0x14u,
    0x09u, 'M',   'L',   'E',   'N',   0x00u, 0xA4u, 0x0Au, 0x06u, 0x5Bu, 0x80u, 'R',   'M',   '_',   '_',   0x01u,
    0x0Au, 0x60u, 'M',   'L',   'E',   'N',   0x5Bu, 0x80u, 'R',   'U',   '_',   '_',   0x01u, 'U',   'N',   'D',
    'F',   0x01u, 0x5Bu, 0x80u, 0x5Cu, 'R',   'P',   '_',   '_',   0x01u, 0x0Au, 0x70u, 0x0Au, 0x02u, 0x5Bu, 0x80u,
    0x2Eu, 'D',   'R',   '_',   '_',   'R',   'E',   '_',   '_',   0x01u, 0x0Au, 0x72u, 0x0Au, 0x02u, 0x5Bu, 0x82u,
    0x10u, 'D',   'S',   '_',   '_',   0x5Bu, 0x80u, '3',   'R',   '_',   '_',   0x01u, 0x0Au, 0x70u, 0x0Au, 0x02u,
    0x5Bu, 0x82u, 0x0Bu, 'D',   'T',   '_',   '_',   0x5Bu, 0x80u, 'R',   'S',   '_',   '_',   0x5Bu, 0x82u, 0x0Fu,
    'D',   'U',   '_',   '_',   0x5Bu, 0x80u, 'R',   'L',   '_',   '_',   0x00u, 0x0Au, 0x10u, 0x0Au, 0x5Bu, 0x80u,
    'R',   'Z',
};

/**
 * @brief Kept data of edge sizes (O20): `Name (PD__, Package (3) {1})`, padded; `Name (TR__, Package (1) {1, "abc",
 *        Buffer (2) {1}, Package (1) {2}})`, whose dropped initialisers hold objects; `Name (BS__, Buffer (8) {1, 2})`,
 *        shorter than its size; `Name (BL__, Buffer (2) {1, 2, 3, 4})`, longer; `Name (E0__, Package (0) {})`,
 *        `Name (E1__, Buffer (0) {})` and `Name (E2__, Package (1) {""})`; `Name (VP__, VarPackage (0x0100) {One})`;
 *        `Name (NM__, Package (3) {\\_SB_, ^XYZW, AB__.CD__})`, a name of each prefix; and
 *        `Name (B64K, Buffer (0x10000) {})`, the largest buffer of §6.1.
 *
 * It is raw bytes: iasl raises a Package's count to its initialisers and a Buffer's size to its bytes.
 */
static const uint8_t shortcuts_kept_edges[] = {
    0x08u, 'P',   'D',   '_',   '_',   0x12u, 0x03u, 0x03u, 0x01u, 0x08u, 'T',   'R',   '_',   '_',   0x12u,
    0x12u, 0x01u, 0x01u, 0x0Du, 'a',   'b',   'c',   0x00u, 0x11u, 0x04u, 0x0Au, 0x02u, 0x01u, 0x12u, 0x04u,
    0x01u, 0x0Au, 0x02u, 0x08u, 'B',   'S',   '_',   '_',   0x11u, 0x05u, 0x0Au, 0x08u, 0x01u, 0x02u, 0x08u,
    'B',   'L',   '_',   '_',   0x11u, 0x07u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x03u, 0x04u, 0x08u, 'E',   '0',
    '_',   '_',   0x12u, 0x02u, 0x00u, 0x08u, 'E',   '1',   '_',   '_',   0x11u, 0x03u, 0x0Au, 0x00u, 0x08u,
    'E',   '2',   '_',   '_',   0x12u, 0x04u, 0x01u, 0x0Du, 0x00u, 0x08u, 'V',   'P',   '_',   '_',   0x13u,
    0x05u, 0x0Bu, 0x00u, 0x01u, 0x01u, 0x08u, 'N',   'M',   '_',   '_',   0x12u, 0x15u, 0x03u, 0x5Cu, '_',
    'S',   'B',   '_',   0x5Eu, 'X',   'Y',   'Z',   'W',   0x2Eu, 'A',   'B',   '_',   '_',   'C',   'D',
    '_',   '_',   0x08u, 'B',   '6',   '4',   'K',   0x11u, 0x06u, 0x0Cu, 0x00u, 0x00u, 0x01u, 0x00u,
};

/**
 * @brief `Package (2) {One, 2}`, which builds.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_valid[] = {0x12u, 0x05u, 0x02u, 0x01u, 0x0Au, 0x02u};

/**
 * @brief A Package whose DWord constant its PkgLength cuts after one byte.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_integer_cut[] = {0x12u, 0x04u, 0x02u, 0x0Cu, 0x01u};

/**
 * @brief A Package whose element `A!!!` starts as a NameSeg, its last three characters none.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_name_segment[] = {0x12u, 0x06u, 0x01u, 'A', '!', '!', '!'};

/**
 * @brief A Package whose inner Package's PkgLength of 9 crosses its parent's end, then a Zero at table level.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_crossing[] = {0x12u, 0x05u, 0x01u, 0x12u, 0x09u, 0x01u, 0x00u};

/**
 * @brief `Package (1) {Buffer (Add (1, 1)) {}}`, whose Buffer's size needs the interpreter.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_expression_size[] = {0x12u, 0x0Au, 0x01u, 0x11u, 0x07u, 0x72u,
                                                            0x0Au, 0x01u, 0x0Au, 0x01u, 0x00u};

/**
 * @brief A Package whose element is the byte 0x02, which starts no data object.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_invalid_opcode[] = {0x12u, 0x03u, 0x01u, 0x02u};

/**
 * @brief `Package (2) {Package (3) {One}, ...}`, whose inner Package is padded before the byte 0x02, which starts no
 *        data object.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_padded_then_fault[] = {0x12u, 0x07u, 0x02u, 0x12u, 0x03u, 0x03u, 0x01u, 0x02u};

/**
 * @brief A Package whose String `AB` its PkgLength cuts before the NUL.
 *
 * It is raw bytes: iasl emits no Package term alone at the root (§8.2).
 */
static const uint8_t shortcuts_unbuilt_string_cut[] = {0x12u, 0x05u, 0x01u, 0x0Du, 'A', 'B'};

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
 *
 * Its opcode and count are raw bytes: partial terms around what @p backwards holds.
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
 *
 * Its Zero, Name and Device opcodes are raw bytes: partial terms of a nesting whose depth the test names.
 */
static void shortcuts_write_nested(ShortcutsBackwards_t *backwards, const char *name, uint32_t depth, uint32_t devices)
{
    static const uint8_t zero[] = {0x00u};
    static const uint8_t name_opcode[] = {0x08u};
    static const uint8_t device_opcode[] = {0x5Bu, 0x82u};

    memset(backwards, 0, sizeof(*backwards));
    backwards->start = SHORTCUTS_AML_CAPACITY;
    shortcuts_prepend(backwards, shortcuts_write_nested_top_aml, sizeof(shortcuts_write_nested_top_aml));

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
 *
 * Its values array is the bytes a turn writes, not AML.
 */
static void shortcuts_change_one_byte(const uint8_t *table, uint32_t length, uint32_t offset, uint32_t turn)
{
    const uint8_t values[3] = {(uint8_t) (table[offset] ^ 0x40u), 0x00u, 0xFFu};
    const uint8_t value = values[turn % 3u];

    memcpy(shortcuts_mutant, table, length);
    shortcuts_mutant[offset] = (value == table[offset]) ? values[0] : value;
}

/**
 * @brief Builds every data object shortcuts_load kept in its table (O20), as a name resolution reaching it would.
 *
 * @return The kept objects that could not be built.
 */
static uint32_t shortcuts_build_kept_data(void)
{
    uint32_t unbuilt = 0u;

    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         index < shortcuts_load.space.node_count; ++index)
        unbuilt += (advanced_configuration_and_power_interface_machine_language_materialize(&shortcuts_load, index) ==
                    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK) ?
                       0u :
                       1u;
    return unbuilt;
}

/**
 * @brief Leaves shortcuts_load's heap no room for one more object as @p heap says: its arena capped at the bytes it
 *        holds, or every run it may hold taken by a Buffer of its own; then its run of small objects filled with empty
 *        Strings until the heap refuses one.
 *
 * @return The objects it took.
 */
static uint32_t shortcuts_fill_heap(ShortcutsHeap_t heap)
{
    uint32_t handle = 0u;
    uint32_t filled = 0u;

    shortcuts_load.space.max_arena_bytes =
        (heap == SHORTCUTS_HEAP_NO_RUN) ? SHORTCUTS_NO_RUN_ARENA_BYTES : shortcuts_load.space.arena_bytes;
    while (heap == SHORTCUTS_HEAP_NO_RUN &&
           advanced_configuration_and_power_interface_machine_language_object_allocate(
               &shortcuts_load.heap, &shortcuts_load.space,
               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LARGE_OBJECT_BYTES,
               &handle) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        ++filled;
    while (advanced_configuration_and_power_interface_machine_language_object_allocate(
               &shortcuts_load.heap, &shortcuts_load.space,
               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, 0u,
               &handle) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        ++filled;
    return filled;
}

/**
 * @brief Loads a case into shortcuts_load, with its shortcuts or without, its heap with the room @p heap says, its
 *        method bodies decoded as in the debug kernel and the host tools, and records what it ended with, then the fold
 *        and the counts once every data object it kept in its table is built; a namespace its node cap does not let
 *        open records that alone.
 */
static void shortcuts_record(const ShortcutsCase_t *load_case, bool general, ShortcutsHeap_t heap,
                             ShortcutsRecord_t *out_record)
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
    out_record->filled = (heap != SHORTCUTS_HEAP_ROOM) ? shortcuts_fill_heap(heap) : 0u;
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
    out_record->data_kept = load->data_kept;
    out_record->data_faults = load->data_faults;
    out_record->data_named_sizes = load->data_named_sizes;
    out_record->runs = load->evaluation.runs;
    out_record->failures = load->evaluation.failures;
    memcpy(out_record->statuses, load->evaluation.statuses, sizeof(out_record->statuses));
    memcpy(out_record->unimplemented, load->evaluation.unimplemented, sizeof(out_record->unimplemented));
    memcpy(out_record->executed, load->evaluation.executed, sizeof(out_record->executed));
    out_record->taken.data_stepped_over = load->data_stepped_over;
    out_record->taken.declarations_decoded_directly = load->declarations_decoded_directly;
    out_record->taken.integers_built_directly = load->integers_built_directly;
    out_record->unbuilt = shortcuts_build_kept_data();
    out_record->built_fold = advanced_configuration_and_power_interface_machine_language_namespace_fold(load);
    out_record->built_padded = load->heap.packages_padded;
    out_record->built_truncated = load->heap.packages_truncated;
    out_record->built_faults = load->data_faults_read;
    out_record->built_deferred = load->values_deferred;
    advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
    advanced_configuration_and_power_interface_simulated_host_close(&shortcuts_host);
}

/**
 * @brief Whether a case loads, its heap with the room @p heap says, to the same namespace and the same counts of what
 *        it declared with the load's shortcuts and without, the load without them taking none, and the shortcuts
 *        stepped over a data object, built (O18) or kept (O20), when the case says they must; @p out_record receives
 *        what the load with the shortcuts ended with. A case under the made-up node cap must open its namespace; under
 *        a lower cap, both loads may fail to open it alike.
 */
static bool shortcuts_agree_in_heap(KernelTest_t *test, const ShortcutsCase_t *load_case, ShortcutsHeap_t heap,
                                    ShortcutsRecord_t *out_record)
{
    static const ShortcutsTaken_t none = {0};
    ShortcutsRecord_t general;

    shortcuts_record(load_case, false, heap, out_record);
    shortcuts_record(load_case, true, heap, &general);
    if (!kernel_test_check(test, out_record->opened != 0u || load_case->max_nodes < MADE_UP_MAX_NODES,
                           "the namespace opens"))
        return false;
    return memcmp(&general.taken, &none, sizeof(none)) == 0 &&
           memcmp(out_record, &general, offsetof(ShortcutsRecord_t, taken)) == 0 &&
           (!load_case->steps_over || out_record->taken.data_stepped_over + out_record->data_kept != 0u);
}

/**
 * @brief Whether a case loads, its heap with room, as shortcuts_agree_in_heap says; @p out_record receives what the
 *        load with the shortcuts ended with, what they took included.
 */
static bool shortcuts_agree(KernelTest_t *test, const ShortcutsCase_t *load_case, ShortcutsRecord_t *out_record)
{
    return shortcuts_agree_in_heap(test, load_case, SHORTCUTS_HEAP_ROOM, out_record);
}

/**
 * @brief Opens shortcuts_load on a fresh simulated host, its arena capped at @p arena bytes, with the load's shortcuts
 *        or, when @p general holds, without.
 */
static bool shortcuts_open(KernelTest_t *test, bool general, uint32_t arena)
{
    advanced_configuration_and_power_interface_simulated_host_open(&shortcuts_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &shortcuts_load, &shortcuts_host.host, MADE_UP_MAX_NODES, arena),
                           "the namespace opens"))
    {
        advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
        advanced_configuration_and_power_interface_simulated_host_close(&shortcuts_host);
        return false;
    }
    shortcuts_load.general_load = general;
    return true;
}

/**
 * @brief Gives shortcuts_load's pages back, then closes its simulated host.
 */
static void shortcuts_close(void)
{
    advanced_configuration_and_power_interface_machine_language_close(&shortcuts_load);
    advanced_configuration_and_power_interface_simulated_host_close(&shortcuts_host);
}

/**
 * @brief Declares the @p length bytes of @p aml in shortcuts_load as a DSDT of revision 2.
 */
static void shortcuts_declare(const uint8_t *aml, uint32_t length)
{
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &shortcuts_load, made_up_definition_block(shortcuts_dsdt, "DSDT", 2u, aml, length));
}

/**
 * @brief Loads shortcuts_reached_aml into shortcuts_load, with the load's shortcuts or without, then evaluates its
 *        methods in turn and records what they returned.
 */
static bool shortcuts_reach(KernelTest_t *test, bool general, ShortcutsReached_t *out_reached)
{
    static const char *const methods[] = {"\\M0__", "\\M1__", "\\M2__", "\\M3__", "\\M4__", "\\M5__", "\\M6__"};

    memset(out_reached, 0, sizeof(*out_reached));
    if (!shortcuts_open(test, general, MADE_UP_MAX_ARENA_BYTES))
        return false;
    shortcuts_declare(shortcuts_reached_aml, sizeof(shortcuts_reached_aml));
    for (uint32_t method = 0u; method < sizeof(methods) / sizeof(methods[0]); ++method)
    {
        AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;

        out_reached->statuses[method] = (uint8_t) advanced_configuration_and_power_interface_machine_language_evaluate(
            &shortcuts_load, methods[method], NULL, 0u, &result);
        out_reached->results[method] =
            (result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER) ?
                result.payload :
                UINT64_MAX;
        advanced_configuration_and_power_interface_machine_language_value_release(&shortcuts_load.heap,
                                                                                  &shortcuts_load.space, &result);
    }
    out_reached->data_kept = shortcuts_load.data_kept;
    out_reached->data_materialized = shortcuts_load.data_materialized;
    shortcuts_close();
    return true;
}

/**
 * @brief Writes into shortcuts_backwards the case of a table whose package nests @p depth deep inside @p devices
 *        nested Devices, named @p name, with `Name (TOP_, Package (1) {One})` after them.
 *
 * @return Whether it fits the buffer.
 */
static bool shortcuts_nested_case(KernelTest_t *test, ShortcutsCase_t *load_case, const char *name, uint32_t depth,
                                  uint32_t devices)
{
    shortcuts_write_nested(&shortcuts_backwards, name, depth, devices);
    if (!kernel_test_check(test, !shortcuts_backwards.overflowed, "the nested table fits its buffer"))
        return false;
    load_case->dsdt = shortcuts_backwards.bytes + shortcuts_backwards.start;
    load_case->dsdt_bytes = SHORTCUTS_AML_CAPACITY - shortcuts_backwards.start;
    return true;
}

/**
 * @brief The node at the root of shortcuts_load named @p name, or NULL when there is none.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *shortcuts_root_node(const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_node(
        &shortcuts_load.space,
        advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            &shortcuts_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u)));
}

/**
 * @brief The value of the node at the root of shortcuts_load named @p name, or NULL when there is none.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *shortcuts_root_value(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = shortcuts_root_node(name);

    return (node != NULL) ? &node->value : NULL;
}

/**
 * @brief Whether the node at the root of shortcuts_load named @p name keeps data that reads, unbuilt, as
 *        @p length elements or bytes, the first @p initialised of them those of @p initialiser.
 */
static bool shortcuts_reads_as(const char *name, uint32_t length, const uint8_t *initialiser, uint32_t initialised)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value = shortcuts_root_value(name);
    uint32_t read_length = 0u;
    const uint8_t *read_initialiser = NULL;
    uint32_t read_initialised = 0u;

    return value != NULL &&
           advanced_configuration_and_power_interface_machine_language_describe_kept_data(
               &shortcuts_load, value, &read_length, &read_initialiser, &read_initialised) &&
           read_length == length && read_initialised == initialised &&
           (initialised == 0u || memcmp(read_initialiser, initialiser, initialised) == 0);
}

/**
 * @brief Evaluates the method of shortcuts_load at @p path, with no argument.
 *
 * @return Its status; @p out_integer receives the Integer it returned, or UINT64_MAX when it returned none.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t shortcuts_evaluate(const char *path,
                                                                                        uint64_t *out_integer)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&shortcuts_load, path, NULL, 0u, &result);

    *out_integer = (result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER) ?
                       result.payload :
                       UINT64_MAX;
    advanced_configuration_and_power_interface_machine_language_value_release(&shortcuts_load.heap,
                                                                              &shortcuts_load.space, &result);
    return status;
}

/**
 * @brief Writes into shortcuts_backwards the table of @p unbuilt: shortcuts_unbuilt_readers_aml, `Name (PKG_, ...)`
 *        of its package, then `Name (AFTR, 7)`.
 *
 * Its Name opcode and NameSeg and its One are raw bytes: partial terms around the package @p unbuilt gives.
 *
 * @return Whether it fits the buffer.
 */
static bool shortcuts_unbuilt_table(KernelTest_t *test, const ShortcutsUnbuilt_t *unbuilt)
{
    static const uint8_t name[] = {0x08u, 'P', 'K', 'G', '_'};
    static const uint8_t one[] = {0x01u};

    memset(&shortcuts_backwards, 0, sizeof(shortcuts_backwards));
    shortcuts_backwards.start = SHORTCUTS_AML_CAPACITY;
    shortcuts_prepend(&shortcuts_backwards, shortcuts_unbuilt_table_after_aml,
                      sizeof(shortcuts_unbuilt_table_after_aml));

    const uint32_t end = shortcuts_backwards.start;

    shortcuts_prepend(&shortcuts_backwards, (unbuilt->bytes != NULL) ? unbuilt->bytes : one,
                      (unbuilt->bytes != NULL) ? unbuilt->length : (uint32_t) sizeof(one));
    for (uint32_t level = 0u; unbuilt->bytes == NULL &&
                              level <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES;
         ++level)
        shortcuts_wrap_package(&shortcuts_backwards, end);
    shortcuts_prepend(&shortcuts_backwards, name, sizeof(name));
    shortcuts_prepend(&shortcuts_backwards, shortcuts_unbuilt_readers_aml, sizeof(shortcuts_unbuilt_readers_aml));
    return kernel_test_check(test, !shortcuts_backwards.overflowed, "the table fits its buffer");
}

/**
 * @brief Loads the table of @p unbuilt into shortcuts_load, with the load's shortcuts or without, decodes its bodies as
 *        the debug kernel and the host tools do, then evaluates its readers, MOT_ first, and adds what the load counted
 *        to @p counts.
 *
 * @return Whether the load kept the package, with no fault and nothing waiting, its decoding pass counting it in
 *         data_faults unless it builds; whether its first read left it a Package when it builds, Uninitialized
 *         otherwise, counted in data_faults_read for a fault and in values_deferred for a term that needs the
 *         interpreter, no package counted as padded or cut; whether the readers then returned what AML reads of that
 *         object, ObjectType 4, SizeOf 2 and the element One of the Package, else ObjectType 0 and type_mismatch for
 *         SizeOf and Index; and whether CondRefOf found PKG_ and AFTR read 7.
 */
static bool shortcuts_read_unbuilt(KernelTest_t *test, const ShortcutsUnbuilt_t *unbuilt, bool general,
                                   ShortcutsUnbuiltCounts_t *counts)
{
    const bool builds = unbuilt->first_read == SHORTCUTS_FIRST_READ_BUILDS;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t mismatch =
        builds ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK :
                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH;
    uint64_t type = 0u;
    uint64_t size = 0u;
    uint64_t element = 0u;
    uint64_t found = 0u;
    uint64_t after = 0u;

    if (!shortcuts_unbuilt_table(test, unbuilt) || !shortcuts_open(test, general, MADE_UP_MAX_ARENA_BYTES))
        return false;
    shortcuts_declare(shortcuts_backwards.bytes + shortcuts_backwards.start,
                      SHORTCUTS_AML_CAPACITY - shortcuts_backwards.start);
    advanced_configuration_and_power_interface_machine_language_decode_tables(&shortcuts_load);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = shortcuts_root_node("PKG_");
    const bool kept = node != NULL &&
                      node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_KEPT_DATA &&
                      shortcuts_load.data_kept == 1u && shortcuts_load.load_errors == 0u &&
                      shortcuts_load.values_deferred == 0u && shortcuts_load.data_faults == (builds ? 0u : 1u);
    const bool typed =
        shortcuts_evaluate("\\MOT_", &type) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
        type == (builds ? 4u : 0u);
    const bool left =
        node != NULL &&
        node->type == (builds ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PACKAGE :
                                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_UNINITIALIZED);
    const bool sized = shortcuts_evaluate("\\MSZ_", &size) == mismatch && (!builds || size == 2u);
    const bool indexed = shortcuts_evaluate("\\MEL_", &element) == mismatch && (!builds || element == 1u);
    const bool exists =
        shortcuts_evaluate("\\MCR_", &found) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
        found == UINT64_MAX;
    const bool followed =
        shortcuts_evaluate("\\MAF_", &after) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
        after == 7u;
    const bool counted =
        shortcuts_load.data_materialized == (builds ? 1u : 0u) &&
        shortcuts_load.data_faults_read == ((unbuilt->first_read == SHORTCUTS_FIRST_READ_FAULT) ? 1u : 0u) &&
        shortcuts_load.values_deferred == ((unbuilt->first_read == SHORTCUTS_FIRST_READ_WAITS) ? 1u : 0u) &&
        shortcuts_load.heap.packages_padded == 0u && shortcuts_load.heap.packages_truncated == 0u;

    counts->data_kept += shortcuts_load.data_kept;
    counts->data_faults += shortcuts_load.data_faults;
    counts->data_faults_read += shortcuts_load.data_faults_read;
    counts->values_deferred += shortcuts_load.values_deferred;
    shortcuts_close();
    return kept && typed && left && sized && indexed && exists && followed && counted;
}

/**
 * @brief The bytes of @p table, and in @p out_length their length.
 */
static const uint8_t *shortcuts_table_bytes(ShortcutsTable_t table, uint32_t *out_length)
{
    switch (table)
    {
    case SHORTCUTS_TABLE_STATEMENTS: *out_length = sizeof(shortcuts_statements_aml); return shortcuts_statements_aml;
    case SHORTCUTS_TABLE_REGIONS: *out_length = sizeof(shortcuts_regions); return shortcuts_regions;
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
            ShortcutsRecord_t record;

            shortcuts_change_one_byte(bytes, length, offset, every_value ? turn : offset / stride);
            out_mutations->disagreements += shortcuts_agree(test, &mutant, &record) ? 0u : 1u;
            out_mutations->taken.data_stepped_over += record.taken.data_stepped_over;
            out_mutations->taken.declarations_decoded_directly += record.taken.declarations_decoded_directly;
            out_mutations->data_kept += record.data_kept;
            out_mutations->taken.integers_built_directly += record.taken.integers_built_directly;
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
        {"data of every kind under revision 2",          shortcuts_constants,       sizeof(shortcuts_constants),       2u, NULL, 0u,                     false,
         false,                                                                                                                                                        true, MADE_UP_MAX_NODES},
        {"data of every kind under revision 1",          shortcuts_constants,       sizeof(shortcuts_constants),       1u, NULL, 0u,                     false,
         false,                                                                                                                                                        true, MADE_UP_MAX_NODES},
        {"an SSDT declared before a DSDT of revision 1", shortcuts_constants,       sizeof(shortcuts_constants),       1u,
         shortcuts_wide,                                                                                                         sizeof(shortcuts_wide), true,  false, true, MADE_UP_MAX_NODES},
        {"sizes a name gives",                           shortcuts_named_sizes_aml, sizeof(shortcuts_named_sizes_aml), 2u, NULL, 0u,                     false, false,
         true,                                                                                                                                                               MADE_UP_MAX_NODES},
        {"data the decoder refuses",                     shortcuts_malformed,       sizeof(shortcuts_malformed),       2u, NULL, 0u,                     false, false, true,
         MADE_UP_MAX_NODES                                                                                                                                                                    },
        {"a PkgLength that abandons the table",          shortcuts_crossing,        sizeof(shortcuts_crossing),        2u, NULL, 0u,                     false,
         false,                                                                                                                                                        true, MADE_UP_MAX_NODES},
        {"a package 33 deep",                            NULL,                      0u,                                2u, NULL, 0u,                     false, false, true, MADE_UP_MAX_NODES},
        {"a package 32 deep inside 480 Devices",         NULL,                      0u,                                2u, NULL, 0u,                     false, true,  true, MADE_UP_MAX_NODES},
    };
    const uint32_t count = sizeof(cases) / sizeof(cases[0]);
    uint32_t stepped_over = 0u;

    for (uint32_t index = 0u; index < count; ++index)
    {
        ShortcutsRecord_t record;

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
        kernel_test_check(test, shortcuts_agree(test, &cases[index], &record), cases[index].claim);
        stepped_over += record.taken.data_stepped_over;
    }
    kernel_test_measure(test, "data_stepped_over", stepped_over);
}

/**
 * @brief The declaration pass's decoder builds a Name's integer constant itself, as the general data decoder builds it,
 *        and steps over it (O19): a load with that decoder ends with the namespace, the faults and the counts of what
 *        it declared of a load without it, and its decoder built the integers each case counts: Zero, One, Ones and the
 *        four widths under revision 2, then under revision 1, which masks them to 32 bits, then with an SSDT declared
 *        before that DSDT, whose integers stay 64 bits; the one integer it builds among those it leaves to the general
 *        path, Revision, a name, a duplicate and a QWord whose last byte the table's end cuts; an integer inside 479
 *        Devices the declaration pass walks itself, where the walk still has room for 32 packages, and none inside 480,
 *        where it has not.
 */
KERNEL_TEST(the_declaration_decoder_builds_a_name_s_integer_as_the_data_decoder_builds_it)
{
    ShortcutsCase_t cases[] = {
        {"integers of every width under revision 2",        shortcuts_constants,        sizeof(shortcuts_constants), 2u, NULL, 0u,
         false,                                                                                                                                               false, true, MADE_UP_MAX_NODES},
        {"integers of every width under revision 1",        shortcuts_constants,        sizeof(shortcuts_constants), 1u, NULL, 0u,
         false,                                                                                                                                               false, true, MADE_UP_MAX_NODES},
        {"an SSDT declared before a DSDT of revision 1",    shortcuts_constants,        sizeof(shortcuts_constants), 1u,
         shortcuts_wide,                                                                                                       sizeof(shortcuts_wide), true,  false, true, MADE_UP_MAX_NODES},
        {"integers the decoder leaves to the general path", shortcuts_integers_refused,
         sizeof(shortcuts_integers_refused),                                                                         2u, NULL, 0u,                     false, false, true, MADE_UP_MAX_NODES},
        {"an integer inside 479 Devices",                   NULL,                       0u,                          2u, NULL, 0u,                     false, true,  true, MADE_UP_MAX_NODES},
        {"an integer inside 480 Devices",                   NULL,                       0u,                          2u, NULL, 0u,                     false, true,  true, MADE_UP_MAX_NODES},
    };
    const uint32_t devices[] = {0u, 0u, 0u, 0u, SHORTCUTS_DEEP_DEVICES - 1u, SHORTCUTS_DEEP_DEVICES};
    const uint32_t integers[] = {7u, 7u, 8u, 1u, 1u, 0u};
    uint32_t built_directly = 0u;

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        ShortcutsRecord_t record;

        if (cases[index].dsdt == NULL)
        {
            shortcuts_write_nested(&shortcuts_backwards, "DEEP", 0u, devices[index]);
            if (!kernel_test_check(test, !shortcuts_backwards.overflowed, "the nested table fits its buffer"))
                return;
            cases[index].dsdt = shortcuts_backwards.bytes + shortcuts_backwards.start;
            cases[index].dsdt_bytes = SHORTCUTS_AML_CAPACITY - shortcuts_backwards.start;
        }
        kernel_test_check(test,
                          shortcuts_agree(test, &cases[index], &record) &&
                              record.taken.integers_built_directly == integers[index],
                          cases[index].claim);
        built_directly += record.taken.integers_built_directly;
    }
    kernel_test_measure(test, "integers_built_directly", built_directly);
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
                          mutations.taken.data_stepped_over != 0u && mutations.data_kept != 0u,
                      "every change of the sample was loaded both ways, and the shortcuts stepped over data and kept "
                      "some");
    kernel_test_check(test, mutations.disagreements == 0u,
                      "every change declares the same with the shortcut and without");
    kernel_test_measure(test, "mutants", mutations.loads);
    kernel_test_measure(test, "data_stepped_over", mutations.taken.data_stepped_over);
    kernel_test_measure(test, "data_kept", mutations.data_kept);
}

/**
 * @brief The declaration pass takes a Name or a Method of one NameSeg, a Device, a ThermalZone, a Scope, a Field
 *        and an OperationRegion of one NameSeg through its own decoder (O19), which hands a statement back to the walk
 *        at its first operand whose outcome differs; a load with that decoder ends with the namespace, the faults and
 *        the counts of what it declared of a load without it, its work (lookups, probes, the stacks' peaks) falling
 *        but for the names of a region it hands back, and its decoder took the statements each table's documentation
 * counts: on statements of every shape it takes, under revision 2 then 1, on statements it hands back, on the
 * declarations of table-level code, run and declared in every branch, and on OperationRegions under revision 2, then 1,
 * then with every construct walked by the declaration pass.
 */
KERNEL_TEST(the_declaration_decoder_declares_what_the_walk_declares)
{
    const ShortcutsCase_t cases[] = {
        {"statements the decoder takes, under revision 2",                      shortcuts_statements_aml,       sizeof(shortcuts_statements_aml),
         2u,                                                                                                                                          NULL, 0u, false, false, true, MADE_UP_MAX_NODES},
        {"statements the decoder takes, under revision 1",                      shortcuts_statements_aml,       sizeof(shortcuts_statements_aml),
         1u,                                                                                                                                          NULL, 0u, false, false, true, MADE_UP_MAX_NODES},
        {"statements the decoder hands back",                                   shortcuts_handed_back,          sizeof(shortcuts_handed_back),    2u, NULL, 0u, false,
         false,                                                                                                                                                               true, MADE_UP_MAX_NODES},
        {"the declarations of table-level code it runs",                        shortcuts_table_level_code_aml,
         sizeof(shortcuts_table_level_code_aml),                                                                                                  2u, NULL, 0u, false, false, true, MADE_UP_MAX_NODES},
        {"the declarations of table-level code whose every branch it declares", shortcuts_table_level_code_aml,
         sizeof(shortcuts_table_level_code_aml),                                                                                                  2u, NULL, 0u, false, true,  true, MADE_UP_MAX_NODES},
        {"OperationRegions, under revision 2",                                  shortcuts_regions,              sizeof(shortcuts_regions),        2u, NULL, 0u, false, false,
         true,                                                                                                                                                                      MADE_UP_MAX_NODES},
        {"OperationRegions, under revision 1",                                  shortcuts_regions,              sizeof(shortcuts_regions),        1u, NULL, 0u, false, false,
         true,                                                                                                                                                                      MADE_UP_MAX_NODES},
        {"OperationRegions, every construct walked by the declaration pass",    shortcuts_regions,
         sizeof(shortcuts_regions),                                                                                                               2u, NULL, 0u, false, true,  true, MADE_UP_MAX_NODES},
    };
    const uint32_t decoded[] = {26u, 26u, 15u, 4u, 5u, 12u, 12u, 12u};
    uint32_t decoded_directly = 0u;

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        ShortcutsRecord_t record;

        kernel_test_check(test,
                          shortcuts_agree(test, &cases[index], &record) &&
                              record.taken.declarations_decoded_directly == decoded[index],
                          cases[index].claim);
        decoded_directly += record.taken.declarations_decoded_directly;
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
            "", shortcuts_statements_aml, sizeof(shortcuts_statements_aml), 2u, NULL, 0u, false, false, false, cap};
        ShortcutsRecord_t record;

        disagreements += shortcuts_agree(test, &load_case, &record) ? 0u : 1u;
        taking += (record.taken.declarations_decoded_directly != 0u) ? 1u : 0u;
    }
    kernel_test_check(test, taking != 0u && taking < SHORTCUTS_NODE_CAPS - 1u,
                      "some caps refuse the namespace, the others let the decoder take statements");
    kernel_test_check(test, disagreements == 0u, "every cap declares the same with the decoder and without");
    kernel_test_measure(test, "caps_taking_statements", taking);
}

/**
 * @brief An OperationRegion whose object finds no room in the heap stops its table, as an exhausted arena stops any
 *        table (§6.2), through the decoder as through the walk: with the heap left no room for one more object, the
 *        OperationRegions load to the same namespace, faults and counts both ways, the first region's name declared
 *        and its object refused with out_of_memory, the load's one fault, before the decoder takes any statement.
 */
KERNEL_TEST(an_operation_region_whose_object_finds_no_room_stops_its_table_both_ways)
{
    const ShortcutsCase_t load_case = {"",    shortcuts_regions, sizeof(shortcuts_regions), 2u, NULL, 0u, false, false,
                                       false, MADE_UP_MAX_NODES};
    ShortcutsRecord_t record;

    kernel_test_check(test, shortcuts_agree_in_heap(test, &load_case, SHORTCUTS_HEAP_NO_ARENA, &record),
                      "the regions declare the same with the decoder and without");
    kernel_test_check(test,
                      record.filled != 0u && record.declarations == 1u && record.load_errors == 1u &&
                          record.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY &&
                          record.taken.declarations_decoded_directly == 0u,
                      "the table stops at the first region: its name declared, its object refused with out_of_memory");
}

/**
 * @brief An OperationRegion whose object the heap refuses for want of a run is counted, and its table goes on, through
 *        the decoder as through the walk: with every run the heap may hold taken and its run of small objects full,
 *        the OperationRegions load to the same namespace, faults and counts both ways, the first fault
 *        limit_object_runs, one for each of the 5 regions the decoder takes and for each of the 9 others, and the
 *        decoder takes its 12 statements.
 */
KERNEL_TEST(an_operation_region_whose_object_finds_no_run_is_counted_both_ways)
{
    const ShortcutsCase_t load_case = {"",    shortcuts_regions, sizeof(shortcuts_regions), 2u, NULL, 0u, false, false,
                                       false, MADE_UP_MAX_NODES};
    ShortcutsRecord_t record;

    kernel_test_check(test, shortcuts_agree_in_heap(test, &load_case, SHORTCUTS_HEAP_NO_RUN, &record),
                      "the regions declare the same with the decoder and without");
    kernel_test_check(test,
                      record.filled != 0u && record.load_errors == 14u &&
                          record.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_OBJECT_RUNS &&
                          record.taken.declarations_decoded_directly == 12u,
                      "every region's object is refused with limit_object_runs and counted, and the table goes on");
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

/**
 * @brief A Name's Package, VarPackage or Buffer whose head reads stays in its table until a name resolution reaches it
 *        (O20), with the load's shortcuts and without: both loads end with the same namespace, faults and counts, the
 *        decoding pass's included, and no package counted as padded or cut at load, then, once every kept object is
 *        built, the same fold, the same packages padded and cut, and the same objects left Uninitialized. The
 *        declaration pass keeps the 3 Buffers, 4 Packages and 1
 *        VarPackage of the table of data, under revision 2 then 1, and the SSDT's Package declared before a DSDT of
 *        revision 1, none of which the decoding pass counts; of the sizes a name gives, the Package whose inner Buffer
 *        takes one, counted in data_named_sizes, but not a Buffer whose own size a name gives; of the data the decoder
 *        refuses, the 4 Packages, 3 of which the decoding pass counts in data_faults, but not the Buffer past 64 KiB;
 *        the Package before a PkgLength that stops the decoding of its table, that one and the faulty one after it,
 *        both counted, though the decoding pass never reaches the second; not a VarPackage past the
 *        4,096 elements of §6.1, nor VPK_, whose failed build counts no package padded, and the first of two
 *        `Name (DUP_)`, the decoding pass leaving the duplicate's package, which would not build, alone; the package
 *        33 deep, which needs the interpreter, counted, and TOP_ after it;
 *        and TOP_ after one 32 deep inside 480 Devices, which is decoded as terms.
 */
KERNEL_TEST(a_name_s_constant_package_or_buffer_stays_in_its_table_until_a_name_reaches_it)
{
    ShortcutsCase_t cases[] = {
        {"data of every kind under revision 2",              shortcuts_constants,                  sizeof(shortcuts_constants),       2u, NULL, 0u,                     false,
         false,                                                                                                                                                                       true, MADE_UP_MAX_NODES},
        {"data of every kind under revision 1",              shortcuts_constants,                  sizeof(shortcuts_constants),       1u, NULL, 0u,                     false,
         false,                                                                                                                                                                       true, MADE_UP_MAX_NODES},
        {"an SSDT declared before a DSDT of revision 1",     shortcuts_constants,                  sizeof(shortcuts_constants),       1u,
         shortcuts_wide,                                                                                                                        sizeof(shortcuts_wide), true,  false, true, MADE_UP_MAX_NODES},
        {"sizes a name gives",                               shortcuts_named_sizes_aml,            sizeof(shortcuts_named_sizes_aml), 2u, NULL, 0u,                     false, false,
         true,                                                                                                                                                                              MADE_UP_MAX_NODES},
        {"data the decoder refuses",                         shortcuts_malformed,                  sizeof(shortcuts_malformed),       2u, NULL, 0u,                     false, false, true,
         MADE_UP_MAX_NODES                                                                                                                                                                                   },
        {"a PkgLength that abandons the table",              shortcuts_crossing,                   sizeof(shortcuts_crossing),        2u, NULL, 0u,                     false,
         false,                                                                                                                                                                       true, MADE_UP_MAX_NODES},
        {"a VarPackage past 4,096 elements and a duplicate", shortcuts_kept_refused_and_duplicate,
         sizeof(shortcuts_kept_refused_and_duplicate),                                                                                2u, NULL, 0u,                     false, false, true, MADE_UP_MAX_NODES},
        {"a package 33 deep",                                NULL,                                 0u,                                2u, NULL, 0u,                     false, false, true, MADE_UP_MAX_NODES},
        {"a package 32 deep inside 480 Devices",             NULL,                                 0u,                                2u, NULL, 0u,                     false, true,  true, MADE_UP_MAX_NODES},
    };
    const uint32_t kept[] = {8u, 8u, 9u, 1u, 4u, 3u, 1u, 2u, 1u};
    const uint32_t faults[] = {0u, 0u, 0u, 0u, 3u, 2u, 0u, 1u, 0u};
    const uint32_t named[] = {0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u};
    const uint32_t count = sizeof(cases) / sizeof(cases[0]);
    uint32_t data_kept = 0u;
    uint32_t data_faults = 0u;

    for (uint32_t index = 0u; index < count; ++index)
    {
        ShortcutsRecord_t record;
        const bool deepest = index + 1u == count;

        if (cases[index].dsdt == NULL &&
            !shortcuts_nested_case(test, &cases[index], deepest ? "DEEP" : "N33_",
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES +
                                       (deepest ? 0u : 1u),
                                   deepest ? SHORTCUTS_DEEP_DEVICES : 0u))
            return;
        kernel_test_check(test,
                          shortcuts_agree(test, &cases[index], &record) && record.data_kept == kept[index] &&
                              record.data_faults == faults[index] && record.data_named_sizes == named[index] &&
                              record.packages_padded == 0u && record.packages_truncated == 0u,
                          cases[index].claim);
        data_kept += record.data_kept;
        data_faults += record.data_faults;
    }
    kernel_test_measure(test, "data_kept", data_kept);
    kernel_test_measure(test, "data_faults", data_faults);
}

/**
 * @brief The check of kept data (O20), which the decoding pass runs on each package the load kept, reads a Package's
 *        one-byte PkgLength without the general head reader, as that reader reads it: each case loads to the
 *        namespace, the faults and the counts of a load without the shortcuts, keeps its outer package, whose head
 *        reads, and the decoding pass counts in data_faults the inner heads it refuses: a package whose PkgLength of 2
 *        holds only its count, accepted; one whose PkgLength of 1 leaves its count outside, refused; one whose
 *        PkgLength of 63, the most one byte holds, reaches its parent's end exactly, accepted; one whose PkgLength
 *        passes its parent's end by one, refused; one whose PkgLength takes two bytes, and a VarPackage, both read by
 *        the general reader, accepted; a VarPackage of the 4,096 elements §6.1 allows, accepted, and one of 4,097,
 *        refused.
 */
KERNEL_TEST(a_kept_package_s_one_byte_head_is_read_as_the_head_reader_reads_it)
{
    const ShortcutsCase_t cases[] = {
        {"a PkgLength that holds only the count",           shortcuts_head_count_only_aml,        sizeof(shortcuts_head_count_only_aml),
         2u,                                                                                                                                    NULL, 0u, false, false, false, MADE_UP_MAX_NODES},
        {"a PkgLength that leaves the count outside",       shortcuts_head_without_count,
         sizeof(shortcuts_head_without_count),                                                                                              2u, NULL, 0u, false, false, false, MADE_UP_MAX_NODES},
        {"a PkgLength of 63 that reaches the parent's end", shortcuts_head_widest_aml,
         sizeof(shortcuts_head_widest_aml),                                                                                                 2u, NULL, 0u, false, false, false, MADE_UP_MAX_NODES},
        {"a PkgLength one past the parent's end",           shortcuts_head_past_parent,           sizeof(shortcuts_head_past_parent),       2u,
         NULL,                                                                                                                                        0u, false, false, false, MADE_UP_MAX_NODES},
        {"a PkgLength of two bytes",                        shortcuts_head_two_bytes,             sizeof(shortcuts_head_two_bytes),         2u, NULL, 0u, false,
         false,                                                                                                                                                         false, MADE_UP_MAX_NODES},
        {"a VarPackage",                                    shortcuts_head_variable,              sizeof(shortcuts_head_variable),          2u, NULL, 0u, false, false, false,
         MADE_UP_MAX_NODES                                                                                                                                                                      },
        {"a VarPackage of 4,096 elements",                  shortcuts_head_most_elements_aml,     sizeof(shortcuts_head_most_elements_aml),
         2u,                                                                                                                                    NULL, 0u, false, false, false, MADE_UP_MAX_NODES},
        {"a VarPackage of 4,097 elements",                  shortcuts_head_too_many_elements_aml,
         sizeof(shortcuts_head_too_many_elements_aml),                                                                                      2u, NULL, 0u, false, false, false, MADE_UP_MAX_NODES},
    };
    const uint32_t refused[] = {0u, 1u, 0u, 1u, 0u, 0u, 0u, 1u};
    uint32_t data_kept = 0u;
    uint32_t data_faults = 0u;

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        ShortcutsRecord_t record;

        kernel_test_check(test,
                          shortcuts_agree(test, &cases[index], &record) && record.data_kept == 1u &&
                              record.data_faults == refused[index],
                          cases[index].claim);
        data_kept += record.data_kept;
        data_faults += record.data_faults;
    }
    kernel_test_measure(test, "data_kept", data_kept);
    kernel_test_measure(test, "data_faults", data_faults);
}

/**
 * @brief A Name's package whose head reads is kept in its table (O20), its elements unchecked at load: the decoding
 *        pass of a debug kernel and of the host tools counts in data_faults each one that will not build, and its first
 *        read leaves its node Uninitialized, as a load that builds it at once leaves it, a fault counted in
 *        data_faults_read and a term that needs the interpreter in values_deferred. AML then reads it so: ObjectType
 *        0, SizeOf and Index failing with type_mismatch, CondRefOf finding it; and the table goes on past it, past a
 *        PkgLength that crosses its parent too. The packages: one whose DWord is cut, one of a NameSeg of invalid
 *        characters, one whose inner PkgLength crosses it, one whose Buffer's size needs the interpreter, one of the
 *        byte 0x02, one 33 deep, one whose String is cut before its NUL, and one whose inner Package is padded before
 *        such a byte, which counts no package padded; and `Package (2) {One, 2}`, which builds. Each loads with the
 *        load's shortcuts and without, alike.
 */
KERNEL_TEST(a_kept_package_whose_elements_do_not_build_is_counted_at_boot_and_left_untyped_at_its_first_read)
{
    static const ShortcutsUnbuilt_t cases[] = {
        {"a Package that builds",                     shortcuts_unbuilt_valid,             sizeof(shortcuts_unbuilt_valid),
         SHORTCUTS_FIRST_READ_BUILDS                                                                                                                         },
        {"a DWord cut",                               shortcuts_unbuilt_integer_cut,       sizeof(shortcuts_unbuilt_integer_cut),
         SHORTCUTS_FIRST_READ_FAULT                                                                                                                          },
        {"a NameSeg of invalid characters",           shortcuts_unbuilt_name_segment,      sizeof(shortcuts_unbuilt_name_segment),
         SHORTCUTS_FIRST_READ_FAULT                                                                                                                          },
        {"a PkgLength that crosses its parent",       shortcuts_unbuilt_crossing,          sizeof(shortcuts_unbuilt_crossing),
         SHORTCUTS_FIRST_READ_FAULT                                                                                                                          },
        {"a Buffer whose size needs the interpreter", shortcuts_unbuilt_expression_size,
         sizeof(shortcuts_unbuilt_expression_size),                                                                                SHORTCUTS_FIRST_READ_WAITS},
        {"a byte that starts no data object",         shortcuts_unbuilt_invalid_opcode,
         sizeof(shortcuts_unbuilt_invalid_opcode),                                                                                 SHORTCUTS_FIRST_READ_FAULT},
        {"a Package 33 deep",                         NULL,                                0u,                                     SHORTCUTS_FIRST_READ_WAITS},
        {"a String cut before its NUL",               shortcuts_unbuilt_string_cut,        sizeof(shortcuts_unbuilt_string_cut),
         SHORTCUTS_FIRST_READ_FAULT                                                                                                                          },
        {"a Package padded before a fault",           shortcuts_unbuilt_padded_then_fault,
         sizeof(shortcuts_unbuilt_padded_then_fault),                                                                              SHORTCUTS_FIRST_READ_FAULT},
    };
    ShortcutsUnbuiltCounts_t counts = {0};

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        const bool shortcut = shortcuts_read_unbuilt(test, &cases[index], false, &counts);
        const bool general = shortcuts_read_unbuilt(test, &cases[index], true, &counts);

        kernel_test_check(test, shortcut && general, cases[index].claim);
    }
    kernel_test_check(
        test,
        counts.data_kept == 18u && counts.data_faults == 16u && counts.data_faults_read == 12u &&
            counts.values_deferred == 4u,
        "both ways, the 9 packages are kept, 8 counted at boot, 6 faults and 2 waits at their first read");
    kernel_test_measure(test, "data_kept", counts.data_kept);
    kernel_test_measure(test, "data_faults", counts.data_faults);
    kernel_test_measure(test, "data_faults_read", counts.data_faults_read);
    kernel_test_measure(test, "values_deferred", counts.values_deferred);
}

/**
 * @brief A kept package (O20) whose Buffer takes its size from a name reads that name at its first read, where a load
 *        that built it read the name as it declared it (I7): PNS_'s `Buffer (CNT_)` is 8 bytes once table-level code
 *        has stored 8 to CNT_, not 4; PLT_'s `Buffer (LATE)`, whose name is declared after it, is 2 bytes, not left
 *        Uninitialized for want of LATE. The decoding pass counts both in data_named_sizes, and in data_faults PVC_,
 *        whose `VarPackage (BIG_)` passes the 4,096 elements of §6.1; with the load's shortcuts and without, alike.
 */
KERNEL_TEST(a_kept_package_s_size_a_name_gives_is_read_at_its_first_read_and_counted_at_boot)
{
    uint32_t named = 0u;

    for (uint32_t way = 0u; way < 2u; ++way)
    {
        uint64_t current = 0u;
        uint64_t late = 0u;

        if (!shortcuts_open(test, way != 0u, MADE_UP_MAX_ARENA_BYTES))
            return;
        shortcuts_declare(shortcuts_nested_named_sizes_aml, sizeof(shortcuts_nested_named_sizes_aml));
        advanced_configuration_and_power_interface_machine_language_decode_tables(&shortcuts_load);
        kernel_test_check(test,
                          shortcuts_load.data_kept == 3u && shortcuts_load.values_deferred == 0u &&
                              shortcuts_load.load_errors == 0u,
                          "the three packages are kept at load, none waiting");
        kernel_test_check(test, shortcuts_load.data_named_sizes == 2u && shortcuts_load.data_faults == 1u,
                          "the decoding pass counts the two sizes it reads, and PVC_'s past 4,096 elements as a fault");
        kernel_test_check(test,
                          shortcuts_evaluate("\\MNS_", &current) ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                              current == 8u,
                          "PNS_'s Buffer takes the 8 CNT_ holds when it is read");
        kernel_test_check(test,
                          shortcuts_evaluate("\\MLT_", &late) ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                              late == 2u,
                          "PLT_'s Buffer takes the 2 LATE holds");
        named += shortcuts_load.data_named_sizes;
        shortcuts_close();
    }
    kernel_test_measure(test, "data_named_sizes", named);
}

/**
 * @brief Every name resolution that reads or writes a data object kept in its table builds it first (O20), and each
 *        method returns the same whether the builder of kept data (C4) or the general data decoder builds what it
 *        reaches: a Store to a Buffer never read then its byte 1 (0x12), a RefOf then a DerefOf of a Package (3), a
 *        package element that names a kept Package (0x22), a DerefOf of a path String (0x33), a Store to an element,
 *        which the next method still reads (7, 7), and the SizeOf of a Buffer (2). Both loads keep the 7 objects, and
 *        the methods build each once.
 *
 * Its succeeded array is the statuses the test expects, not AML.
 */
KERNEL_TEST(a_name_resolution_builds_the_kept_data_it_reaches)
{
    static const uint64_t expected[7] = {0x12u, 3u, 0x22u, 0x33u, 7u, 7u, 2u};
    static const uint8_t succeeded[7] = {0};
    ShortcutsReached_t shortcut;
    ShortcutsReached_t general;

    if (!shortcuts_reach(test, false, &shortcut) || !shortcuts_reach(test, true, &general))
        return;
    kernel_test_check(test,
                      memcmp(general.statuses, succeeded, sizeof(succeeded)) == 0 &&
                          memcmp(general.results, expected, sizeof(expected)) == 0,
                      "each method returns its value where the general data decoder builds every object");
    kernel_test_check(test,
                      memcmp(shortcut.statuses, general.statuses, sizeof(general.statuses)) == 0 &&
                          memcmp(shortcut.results, general.results, sizeof(general.results)) == 0,
                      "each method returns the same where the builder of kept data builds them");
    kernel_test_check(test,
                      general.data_kept == 7u && shortcut.data_kept == 7u && general.data_materialized == 7u &&
                          shortcut.data_materialized == 7u,
                      "both loads keep the 7 objects, and the methods build each once");
    kernel_test_measure(test, "data_kept", shortcut.data_kept);
    kernel_test_measure(test, "data_materialized", shortcut.data_materialized);
}

/**
 * @brief A kept data object that no longer fits when a name first reaches it fails that evaluation with
 *        out_of_memory, and leaves the namespace whole (O20): with no more than 16 KiB past the namespace's own
 *        pages, `Name (BIG_, Buffer (0x8000) {})` loads with the names after it, the method that reads it fails
 *        twice, BIG_ still kept, and INT_ still reads 5.
 */
KERNEL_TEST(a_kept_object_that_no_longer_fits_fails_the_evaluation_that_reaches_it)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint32_t arena = 0u;
    uint32_t nodes = 0u;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t statuses[2];

    if (!shortcuts_open(test, false, MADE_UP_MAX_ARENA_BYTES))
        return;
    arena = shortcuts_load.space.arena_bytes;
    shortcuts_close();
    if (!shortcuts_open(test, false, arena + SHORTCUTS_KEPT_ARENA_BYTES))
        return;
    shortcuts_declare(shortcuts_too_large_aml, sizeof(shortcuts_too_large_aml));
    nodes = shortcuts_load.space.live_nodes;
    for (uint32_t turn = 0u; turn < 2u; ++turn)
    {
        statuses[turn] = advanced_configuration_and_power_interface_machine_language_evaluate(&shortcuts_load, "\\MBIG",
                                                                                              NULL, 0u, &result);
        advanced_configuration_and_power_interface_machine_language_value_release(&shortcuts_load.heap,
                                                                                  &shortcuts_load.space, &result);
    }
    kernel_test_check(test,
                      shortcuts_load.load_errors == 0u && shortcuts_load.data_kept == 1u &&
                          advanced_configuration_and_power_interface_machine_language_evaluate(
                              &shortcuts_load, "\\LAST", NULL, 0u, &result) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
                      "the table loads whole, BIG_ kept, LAST declared");
    kernel_test_check(test,
                      statuses[0] == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY &&
                          statuses[1] == statuses[0] && shortcuts_load.data_materialized == 0u,
                      "MBIG fails with out_of_memory each time, BIG_ never built");
    kernel_test_check(test,
                      shortcuts_load.space.live_nodes == nodes &&
                          advanced_configuration_and_power_interface_machine_language_evaluate(
                              &shortcuts_load, "\\INT_", NULL, 0u, &result) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          result.payload == 5u,
                      "the namespace stays whole, and INT_ still reads 5");
    shortcuts_close();
}

/**
 * @brief A table-level buffer field over kept data that no longer fits stops its table, as an exhausted arena stops any
 *        table (O20, §6.2): with no more than 16 KiB past the namespace's own pages, `Name (BIG_, Buffer (0x8000) {})`
 *        is kept, `CreateByteField (BIG_, Zero, BYT_)` fails to build it with out_of_memory, BYT_ declared without an
 *        object, and `Name (LAST, One)` after it is not declared; BIG_ stays kept.
 */
KERNEL_TEST(a_buffer_field_over_kept_data_that_no_longer_fits_stops_its_table)
{
    uint32_t arena = 0u;

    if (!shortcuts_open(test, false, MADE_UP_MAX_ARENA_BYTES))
        return;
    arena = shortcuts_load.space.arena_bytes;
    shortcuts_close();
    if (!shortcuts_open(test, false, arena + SHORTCUTS_KEPT_ARENA_BYTES))
        return;
    shortcuts_declare(shortcuts_field_too_large_aml, sizeof(shortcuts_field_too_large_aml));

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *big = shortcuts_root_value("BIG_");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *field = shortcuts_root_value("BYT_");

    kernel_test_check(test,
                      shortcuts_load.load_errors == 1u &&
                          shortcuts_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY,
                      "the buffer field's out_of_memory is the load's one fault");
    kernel_test_check(test, field != NULL && field->handle == 0u && shortcuts_root_value("LAST") == NULL,
                      "the table stops there: BYT_ is declared without an object, and LAST is not declared");
    kernel_test_check(test,
                      shortcuts_load.data_kept == 1u && shortcuts_load.data_materialized == 0u && big != NULL &&
                          big->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_KEPT_DATA,
                      "BIG_ stays kept, never built");
    shortcuts_close();
}

/**
 * @brief A data object kept in its table (O20) reads, unbuilt, as what a namespace dump prints of the object it
 *        builds into: `Buffer (4) {1, 2}` as 4 bytes that start with 1 and 2, `Buffer (2) {1, 2, 3}` as its 3 bytes,
 *        `Buffer (0) {}` as none, `Package (3) {...}` and `Package (1) {1, 2, 3}` as 3 and 1 elements,
 *        `VarPackage (2) {One}` as 2; an Integer reads as no kept data.
 *
 * Its first and longer arrays are the bytes the test expects, not AML.
 */
KERNEL_TEST(kept_data_reads_unbuilt_as_the_object_it_builds_into)
{
    static const uint8_t first[] = {0x01u, 0x02u};
    static const uint8_t longer[] = {0x01u, 0x02u, 0x03u};
    uint32_t length = 0u;
    const uint8_t *initialiser = NULL;
    uint32_t initialised = 0u;

    if (!shortcuts_open(test, false, MADE_UP_MAX_ARENA_BYTES))
        return;
    shortcuts_declare(shortcuts_constants, sizeof(shortcuts_constants));
    kernel_test_check(test,
                      shortcuts_reads_as("B0__", 4u, first, sizeof(first)) &&
                          shortcuts_reads_as("B1__", 3u, longer, sizeof(longer)) &&
                          shortcuts_reads_as("B2__", 0u, NULL, 0u),
                      "each Buffer reads as its length and its initialiser");
    kernel_test_check(test,
                      shortcuts_reads_as("P0__", 3u, NULL, 0u) && shortcuts_reads_as("P1__", 1u, NULL, 0u) &&
                          shortcuts_reads_as("V0__", 2u, NULL, 0u),
                      "each Package and VarPackage reads as its element count");
    kernel_test_check(test,
                      shortcuts_root_value("I3__") != NULL &&
                          !advanced_configuration_and_power_interface_machine_language_describe_kept_data(
                              &shortcuts_load, shortcuts_root_value("I3__"), &length, &initialiser, &initialised),
                      "an Integer reads as no kept data");
    shortcuts_close();
}

/**
 * @brief A table whose kept data both builders build, and the arena they build it in.
 */
typedef struct ShortcutsBuildCase {
    const char *claim;  /**< What the two builders agreeing shows. */
    const uint8_t *aml; /**< The DSDT's AML. */
    uint32_t length;    /**< Its length. */
    uint8_t revision;   /**< The DSDT's revision: below 2, every integer is 32 bits. */
    bool wide_first;    /**< Whether shortcuts_wide is declared first, an SSDT whose integers are then 64 bits. */
    bool capped;        /**< Whether the arena holds 16 KiB past the namespace's own pages only. */
} ShortcutsBuildCase_t;

/**
 * @brief What building every data object a load kept ended with: what both builders must agree on, then which
 *        builder built them.
 */
typedef struct ShortcutsBuilt {
    uint32_t data_kept;           /**< Data objects the load kept in its table. */
    uint32_t data_materialized;   /**< Of those, the ones built. */
    uint32_t failed;              /**< Builds that failed. */
    uint32_t first_failure;       /**< The status of the first, or OK. */
    uint32_t fold;                /**< The namespace's fold once every kept object is built or has failed. */
    uint32_t objects;             /**< Objects the heap allocated. */
    uint32_t objects_freed;       /**< Of those, the ones freed. */
    uint32_t object_bytes;        /**< Bytes the live objects take. */
    uint32_t packages_padded;     /**< Packages padded, at load. */
    uint32_t packages_truncated;  /**< Packages cut, at load. */
    uint32_t data_built_directly; /**< Kept objects the builder of kept data built (C4). */
} ShortcutsBuilt_t;

/**
 * @brief Loads a case into shortcuts_load with the load's shortcuts, so that it keeps its constant packages and
 *        buffers, then builds every kept object in node order, through the general data decoder when @p general
 *        holds and through the builder of kept data otherwise, and records what that ended with.
 */
static bool shortcuts_build_kept(KernelTest_t *test, const ShortcutsBuildCase_t *build_case, bool general,
                                 ShortcutsBuilt_t *out_built)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load = &shortcuts_load;
    uint32_t arena = MADE_UP_MAX_ARENA_BYTES;

    memset(out_built, 0, sizeof(*out_built));
    if (build_case->capped)
    {
        if (!shortcuts_open(test, false, MADE_UP_MAX_ARENA_BYTES))
            return false;
        arena = shortcuts_load.space.arena_bytes + SHORTCUTS_KEPT_ARENA_BYTES;
        shortcuts_close();
    }
    if (!shortcuts_open(test, false, arena))
        return false;
    if (build_case->wide_first)
        (void) advanced_configuration_and_power_interface_machine_language_declare_table(
            &shortcuts_load,
            made_up_definition_block(shortcuts_ssdt, "SSDT", 2u, shortcuts_wide, sizeof(shortcuts_wide)));
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &shortcuts_load,
        made_up_definition_block(shortcuts_dsdt, "DSDT", build_case->revision, build_case->aml, build_case->length));
    shortcuts_load.general_load = general;
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         index < shortcuts_load.space.node_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
            advanced_configuration_and_power_interface_machine_language_materialize(&shortcuts_load, index);

        out_built->first_failure = (out_built->failed == 0u) ? (uint32_t) status : out_built->first_failure;
        out_built->failed +=
            (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK) ? 1u : 0u;
    }
    out_built->data_kept = load->data_kept;
    out_built->data_materialized = load->data_materialized;
    out_built->fold = advanced_configuration_and_power_interface_machine_language_namespace_fold(load);
    out_built->objects = load->heap.objects;
    out_built->objects_freed = load->heap.objects_freed;
    out_built->object_bytes = load->heap.object_bytes;
    out_built->packages_padded = load->heap.packages_padded;
    out_built->packages_truncated = load->heap.packages_truncated;
    out_built->data_built_directly = load->data_built_directly;
    shortcuts_close();
    return true;
}

/**
 * @brief Each data object a Name keeps in its table builds the same when a name first reaches it through the builder
 *        of kept data (C4) as through the general data decoder, which a load whose general_load is set uses: the
 *        same namespace fold, the same objects allocated, freed and held, the same counts and the same failures; on
 *        data of every kind under revision 2 then 1, an SSDT declared before the DSDT, kept data of edge sizes under
 *        revision 2 then 1, a package 32 deep, and a package whose third Buffer no longer fits the arena, which both
 *        builders refuse with out_of_memory, everything built so far given back. Only the builder of kept data counts
 *        what it built.
 */
KERNEL_TEST(kept_data_builds_the_same_through_its_own_builder_and_the_general_decoder)
{
    ShortcutsBuildCase_t cases[] = {
        {"data of every kind under revision 2",          shortcuts_constants,          sizeof(shortcuts_constants),  2u, false, false},
        {"data of every kind under revision 1",          shortcuts_constants,          sizeof(shortcuts_constants),  1u, false, false},
        {"an SSDT declared before a DSDT of revision 1", shortcuts_constants,          sizeof(shortcuts_constants),  1u, true,
         false                                                                                                                       },
        {"kept data of edge sizes under revision 2",     shortcuts_kept_edges,         sizeof(shortcuts_kept_edges), 2u, false,
         false                                                                                                                       },
        {"kept data of edge sizes under revision 1",     shortcuts_kept_edges,         sizeof(shortcuts_kept_edges), 1u, false,
         false                                                                                                                       },
        {"a package 32 deep",                            NULL,                         0u,                           2u, false, false},
        {"a package whose third Buffer does not fit",    shortcuts_kept_too_large_aml,
         sizeof(shortcuts_kept_too_large_aml),                                                                       2u, false, true },
    };
    const uint32_t count = sizeof(cases) / sizeof(cases[0]);
    uint32_t built_directly = 0u;

    for (uint32_t index = 0u; index < count; ++index)
    {
        ShortcutsBuilt_t direct;
        ShortcutsBuilt_t general;

        if (cases[index].aml == NULL)
        {
            shortcuts_write_nested(&shortcuts_backwards, "DEEP",
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES, 0u);
            if (!kernel_test_check(test, !shortcuts_backwards.overflowed, "the nested table fits its buffer"))
                return;
            cases[index].aml = shortcuts_backwards.bytes + shortcuts_backwards.start;
            cases[index].length = SHORTCUTS_AML_CAPACITY - shortcuts_backwards.start;
        }
        if (!shortcuts_build_kept(test, &cases[index], false, &direct) ||
            !shortcuts_build_kept(test, &cases[index], true, &general))
            return;
        kernel_test_check(
            test,
            direct.data_kept != 0u && memcmp(&direct, &general, offsetof(ShortcutsBuilt_t, data_built_directly)) == 0 &&
                general.data_built_directly == 0u && direct.data_built_directly == direct.data_materialized &&
                (cases[index].capped ?
                     direct.first_failure ==
                         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY :
                     direct.failed == 0u && direct.data_materialized == direct.data_kept),
            cases[index].claim);
        built_directly += direct.data_built_directly;
    }
    kernel_test_check(test, built_directly != 0u, "the builder of kept data built some");
    kernel_test_measure(test, "data_built_directly", built_directly);
}
