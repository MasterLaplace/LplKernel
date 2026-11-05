#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

KERNEL_TEST_SUITE(host_kept_data, KERNEL_TEST_STAGE_INITIALIZATION);

#define KEPT_DATA_UNCHANGED  0xFFFFFFFFu /**< The offset of a byte left as it is. */
#define KEPT_DATA_FOLD_PRIME 0x01000193u /**< FNV-1a's prime, which the namespace's fold multiplies by. */
#define KEPT_DATA_OPCODE                                                                                               \
    5u /**< Where the data a Name keeps starts in each definition block below, past its NameOp and its NameSeg. */
#define KEPT_DATA_LONG_STRING_BYTES                                                                                    \
    40000u /**< The characters of each String of kept_data_two_long_strings, two of which pass 64 KiB. */
#define KEPT_DATA_LONG_STRINGS_CAPACITY                                                                                \
    (16u + 2u * (KEPT_DATA_LONG_STRING_BYTES + 2u)) /**< Bytes that hold the AML of kept_data_two_long_strings. */

/**
 * @brief One byte of a definition block, set to another value after the load.
 */
typedef struct KeptDataRewrite {
    uint32_t offset; /**< The byte's offset in the AML, or KEPT_DATA_UNCHANGED. */
    uint8_t byte;    /**< Its new value. */
} KeptDataRewrite_t;

/**
 * @brief A definition block holding one Name's kept data, a byte whose rewrite leaves its bytes unreadable, and a
 *        byte whose rewrite leaves them readable.
 */
typedef struct KeptDataCase {
    const char *claim;            /**< That the unreadable rewrite folds as a PkgLength past the table does. */
    const uint8_t *aml;           /**< The AML. */
    uint32_t length;              /**< Its bytes. */
    KeptDataRewrite_t unreadable; /**< The rewrite that leaves the data unreadable. */
    KeptDataRewrite_t readable;   /**< A rewrite that leaves it readable, but different. */
} KeptDataCase_t;

/**
 * @brief A definition block whose Name's data the check of kept data must refuse at a bound the table's end lies just
 *        past.
 */
typedef struct KeptDataBound {
    const char *claim;  /**< The bound, and that the check stops at it. */
    const uint8_t *aml; /**< The AML. */
    uint32_t length;    /**< Its bytes. */
} KeptDataBound_t;

/**
 * @brief A definition block whose kept data each one-byte rewrite must build the same way through both builders.
 */
typedef struct KeptDataSweep {
    const char *claim;  /**< That every rewrite builds the same both ways. */
    const uint8_t *aml; /**< The AML. */
    uint32_t length;    /**< Its bytes. */
    uint8_t revision;   /**< The DSDT's revision: below 2, every integer is 32 bits. */
} KeptDataSweep_t;

/**
 * @brief What building the data a load kept ended with: what the builder of kept data and the general data decoder
 *        must agree on, then how many objects the builder of kept data built.
 */
typedef struct KeptDataBuilt {
    uint32_t kept;           /**< Data objects the load kept in its table. */
    uint32_t status;         /**< The status of the first build that failed, or OK. */
    uint32_t live_objects;   /**< Objects the heap holds once every kept object is built or has failed. */
    uint32_t object_bytes;   /**< Bytes they take. */
    uint32_t fold;           /**< The namespace's fold then. */
    uint32_t faults_read;    /**< Kept objects left Uninitialized for a fault in their bytes. */
    uint32_t deferred;       /**< Objects that wait for the interpreter, kept ones left Uninitialized included. */
    uint32_t built_directly; /**< Kept objects the builder of kept data built (C4). */
} KeptDataBuilt_t;

/**
 * @brief What a load counted of a data object kept in its table (O20), at load, in the decoding pass, then once built.
 */
typedef struct KeptDataChecked {
    uint32_t kept;        /**< Data objects the load kept in its table. */
    uint32_t faults;      /**< Of those, the ones the decoding pass found would not build. */
    uint32_t faults_read; /**< Of those, the ones whose build found a fault, left Uninitialized. */
} KeptDataChecked_t;

/**
 * @brief `Name (PKGX, Package (1) {One})`, its One the table's last byte.
 */
static const uint8_t kept_data_integer[] = {0x08u, 'P', 'K', 'G', 'X', 0x12u, 0x03u, 0x01u, 0x01u};

/**
 * @brief `Name (PKGS, Package (1) {"a"})`, its NUL the table's last byte.
 */
static const uint8_t kept_data_string[] = {0x08u, 'P', 'K', 'G', 'S', 0x12u, 0x05u, 0x01u, 0x0Du, 'a', 0x00u};

/**
 * @brief `Name (PKGN, Package (2) {2, One})`.
 */
static const uint8_t kept_data_no_data[] = {0x08u, 'P', 'K', 'G', 'N', 0x12u, 0x05u, 0x02u, 0x0Au, 0x02u, 0x01u};

/**
 * @brief `Name (PKGM, Package (2) {ABCD, One})`.
 */
static const uint8_t kept_data_name[] = {0x08u, 'P', 'K', 'G', 'M', 0x12u, 0x07u, 0x02u, 'A', 'B', 'C', 'D', 0x01u};

/**
 * @brief `Name (PKGT, Package (0x31) {ABCD, ABCD, ABCD, ABCD, ABCD, ABCD, ABCD, ABCD, ABCD, ABCD, ABCD, One, One})`,
 *        its PkgLength '0' and its count '1', so that its Package opcode rewritten into 'P' makes a NameString of it.
 */
static const uint8_t kept_data_named_package[] = {
    0x08u, 'P', 'K', 'G', 'T', 0x12u, 0x30u, 0x31u, 'A', 'B', 'C', 'D', 'A', 'B', 'C', 'D', 'A',   'B',
    'C',   'D', 'A', 'B', 'C', 'D',   'A',   'B',   'C', 'D', 'A', 'B', 'C', 'D', 'A', 'B', 'C',   'D',
    'A',   'B', 'C', 'D', 'A', 'B',   'C',   'D',   'A', 'B', 'C', 'D', 'A', 'B', 'C', 'D', 0x01u, 0x01u,
};

/**
 * @brief `Name (PKGP, Package (1) {\\ABCD})`, a name after a root prefix.
 */
static const uint8_t kept_data_prefixed_name[] = {0x08u, 'P',  'K', 'G', 'P', 0x12u, 0x07u,
                                                  0x01u, '\\', 'A', 'B', 'C', 'D'};

/**
 * @brief `Name (PKGR, Package (10) {0x12, "ab", Buffer (3) {1, 2}, Package (2) {ABCD, \\_SB_}, 0x12345678,
 *        VarPackage (2) {One, Ones}, AB__.CD__, ^XYZW, AAAABBBBCCCC, 0x0807060504030201})`: an element of each kind,
 *        a name without prefix and a name after each prefix, the last a MultiNamePrefix of three NameSegs.
 */
static const uint8_t kept_data_every_kind[] = {
    0x08u, 'P',   'K',   'G',   'R',   0x12u, 0x4Bu, 0x04u, 0x0Au, 0x0Au, 0x12u, 0x0Du, 'a',   'b', 0x00u, 0x11u, 0x05u,
    0x0Au, 0x03u, 0x01u, 0x02u, 0x12u, 0x0Bu, 0x02u, 'A',   'B',   'C',   'D',   '\\',  '_',   'S', 'B',   '_',   0x0Cu,
    0x78u, 0x56u, 0x34u, 0x12u, 0x13u, 0x05u, 0x0Au, 0x02u, 0x01u, 0xFFu, 0x2Eu, 'A',   'B',   '_', '_',   'C',   'D',
    '_',   '_',   '^',   'X',   'Y',   'Z',   'W',   0x2Fu, 0x03u, 'A',   'A',   'A',   'A',   'B', 'B',   'B',   'B',
    'C',   'C',   'C',   'C',   0x0Eu, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
};

/**
 * @brief `Name (BUFR, Buffer (0x0010) {1, 2, 3})`, shorter than its size.
 */
static const uint8_t kept_data_short_buffer[] = {0x08u, 'B',   'U',   'F',   'R',   0x11u, 0x07u,
                                                 0x0Bu, 0x10u, 0x00u, 0x01u, 0x02u, 0x03u};

/**
 * @brief `Name (VPKR, VarPackage (3) {Package (2) {One, "x"}, 0x1234})`, padded, a Package among its elements.
 */
static const uint8_t kept_data_nested_variable_package[] = {0x08u, 'V',   'P',   'K',   'R',   0x13u, 0x0Du,
                                                            0x0Au, 0x03u, 0x12u, 0x06u, 0x02u, 0x01u, 0x0Du,
                                                            'x',   0x00u, 0x0Bu, 0x34u, 0x12u};

/**
 * @brief `Name (BUFX, Buffer (0x00000002) {0xAA})`.
 */
static const uint8_t kept_data_buffer[] = {0x08u, 'B',   'U',   'F',   'X',   0x11u, 0x07u,
                                           0x0Cu, 0x02u, 0x00u, 0x00u, 0x00u, 0xAAu};

/**
 * @brief `Name (VPKG, VarPackage (0x0002) {One})`.
 */
static const uint8_t kept_data_variable_package[] = {0x08u, 'V',   'P',   'K',   'G',  0x13u,
                                                     0x05u, 0x0Bu, 0x02u, 0x00u, 0x01u};

/**
 * @brief `Name (PKGL, Package (1) {...})`, whose inner PkgLength of 1 leaves its count, the table's last byte, outside.
 */
static const uint8_t kept_data_head_without_count[] = {0x08u, 'P',   'K',   'G',   'L',  0x12u,
                                                       0x05u, 0x01u, 0x12u, 0x01u, 0x00u};

/**
 * @brief `Name (PKGO, Package (1) {...})`, whose inner PkgLength of 3 passes its parent's end, the table's, by one.
 */
static const uint8_t kept_data_head_past_parent[] = {0x08u, 'P',   'K',   'G',   'O',  0x12u,
                                                     0x05u, 0x01u, 0x12u, 0x03u, 0x00u};

/**
 * @brief `Name (PKGC, Package (1) {AB})`, whose NameSeg element the table's end cuts after two characters.
 */
static const uint8_t kept_data_cut_name[] = {0x08u, 'P', 'K', 'G', 'C', 0x12u, 0x04u, 0x01u, 'A', 'B'};

/**
 * @brief Loads the definition block @p aml from a copy of exactly its size, so that the address sanitizer catches a
 *        read past its end, applies @p rewrite after the load, as AML writing its own table through a SystemMemory
 *        region would, and returns the namespace's fold. @p out_kept receives how many data objects the load kept in
 *        the table.
 */
static uint32_t kept_data_fold_after(const uint8_t *aml, uint32_t length, KeptDataRewrite_t rewrite, uint32_t *out_kept)
{
    uint8_t *copy = malloc(MADE_UP_TABLE_HEADER + length);
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t host;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;
    uint32_t fold = 0u;

    if (copy == NULL)
        return 0u;
    advanced_configuration_and_power_interface_simulated_host_open(&host);
    advanced_configuration_and_power_interface_machine_language_open(&load, &host.host, MADE_UP_MAX_NODES,
                                                                     MADE_UP_MAX_ARENA_BYTES);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &load, made_up_definition_block(copy, "DSDT", 2u, aml, length));
    if (rewrite.offset != KEPT_DATA_UNCHANGED)
        copy[MADE_UP_TABLE_HEADER + rewrite.offset] = rewrite.byte;
    fold = advanced_configuration_and_power_interface_machine_language_namespace_fold(&load);
    *out_kept = load.data_kept;
    advanced_configuration_and_power_interface_machine_language_close(&load);
    advanced_configuration_and_power_interface_simulated_host_close(&host);
    free(copy);
    return fold;
}

/**
 * @brief Loads the definition block @p aml from a copy of exactly its size, so that the address sanitizer catches a
 *        read past its end, decodes its bodies as the debug kernel and the host tools do, then builds every data object
 *        it kept in its table, as a name resolution reaching it would; @p out_checked receives what it counted.
 *
 * @return Whether the copy could be made.
 */
static bool kept_data_check_then_build(const uint8_t *aml, uint32_t length, KeptDataChecked_t *out_checked)
{
    uint8_t *copy = malloc(MADE_UP_TABLE_HEADER + length);
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t host;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;

    memset(out_checked, 0, sizeof(*out_checked));
    if (copy == NULL)
        return false;
    advanced_configuration_and_power_interface_simulated_host_open(&host);
    advanced_configuration_and_power_interface_machine_language_open(&load, &host.host, MADE_UP_MAX_NODES,
                                                                     MADE_UP_MAX_ARENA_BYTES);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &load, made_up_definition_block(copy, "DSDT", 2u, aml, length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&load);
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         index < load.space.node_count; ++index)
        (void) advanced_configuration_and_power_interface_machine_language_materialize(&load, index);
    out_checked->kept = load.data_kept;
    out_checked->faults = load.data_faults;
    out_checked->faults_read = load.data_faults_read;
    advanced_configuration_and_power_interface_machine_language_close(&load);
    advanced_configuration_and_power_interface_simulated_host_close(&host);
    free(copy);
    return true;
}

/**
 * @brief `Name (BUF0, Buffer (4) {1, 2})`, whose PkgLength, at offset 6, a rewrite makes cross the table's end.
 */
static const uint8_t kept_data_buffer_for_field[] = {0x08u, 'B',   'U',   'F',   '0',  0x11u,
                                                     0x05u, 0x0Au, 0x04u, 0x01u, 0x02u};

/**
 * @brief `CreateByteField (BUF0, Zero, BYT0)` at table level, then `Name (LAST, One)`.
 */
static const uint8_t kept_data_field_over_buffer[] = {0x8Cu, 'B', 'U',   'F', '0', 0x00u, 'B', 'Y',
                                                      'T',   '0', 0x08u, 'L', 'A', 'S',   'T', 0x01u};

/**
 * @brief Takes @p byte, the last byte folded, back out of the FNV-1a fold @p fold, through the prime's inverse
 *        modulo 2^32, which each Newton step doubles the correct bits of.
 *
 * @return The fold before @p byte.
 */
static uint32_t kept_data_unfold(uint32_t fold, uint8_t byte)
{
    uint32_t inverse = KEPT_DATA_FOLD_PRIME;

    for (uint32_t step = 0u; step < 5u; ++step)
        inverse *= 2u - KEPT_DATA_FOLD_PRIME * inverse;
    return (fold * inverse) ^ byte;
}

/**
 * @brief Checks that the kept data of @p load_case, once its unreadable rewrite is applied, folds without reading past
 *        its table, as the same data whose PkgLength, at offset 6, no longer reads within the table, and apart from
 *        the intact data and from its readable rewrite.
 */
static void kept_data_check(KernelTest_t *test, const KeptDataCase_t *load_case)
{
    const KeptDataRewrite_t intact = {KEPT_DATA_UNCHANGED, 0u};
    const KeptDataRewrite_t head = {6u, 0xFFu};
    uint32_t kept = 0u;
    const uint32_t intact_fold = kept_data_fold_after(load_case->aml, load_case->length, intact, &kept);
    const uint32_t unreadable_fold =
        kept_data_fold_after(load_case->aml, load_case->length, load_case->unreadable, &kept);
    const uint32_t head_fold = kept_data_fold_after(load_case->aml, load_case->length, head, &kept);
    const uint32_t readable_fold = kept_data_fold_after(load_case->aml, load_case->length, load_case->readable, &kept);

    kernel_test_check(test, kept == 1u, "the load kept the data in its table");
    kernel_test_check(test, unreadable_fold == head_fold, load_case->claim);
    kernel_test_check(
        test, unreadable_fold != intact_fold && unreadable_fold != readable_fold && readable_fold != intact_fold,
        "unreadable data folds apart from the intact data and from a change that still reads");
}

/**
 * @brief Kept packages whose table AML rewrote after the load, four of the ways their bytes can stop reading as
 *        data, each folding without reading past the table, as unreadable data wherever it stopped reading:
 *        `Name (PKGX, Package (1) {One})`, its One now a QWordPrefix with no bytes after it at the table's end;
 *        `Name (PKGS, Package (1) {"a"})`, its NUL, the table's last byte, now 'b';
 *        `Name (PKGN, Package (2) {2, One})`, its BytePrefix now an ExtOpPrefix, which starts no data object;
 *        `Name (PKGM, Package (2) {ABCD, One})`, its PkgLength now ending the package inside ABCD.
 */
KERNEL_TEST(kept_data_whose_table_was_rewritten_folds_as_unreadable_without_reading_past_it)
{
    const KeptDataCase_t cases[] = {
        {"an integer cut by the table's end folds as a PkgLength past the table",
         kept_data_integer, sizeof(kept_data_integer),
         {8u, 0x0Eu},
         {8u, 0x00u}},
        {"a string whose NUL is gone folds as a PkgLength past the table",
         kept_data_string,  sizeof(kept_data_string),
         {10u, 'b'},
         {9u, 'c'}  },
        {"a byte that starts no data object folds as a PkgLength past the table",
         kept_data_no_data, sizeof(kept_data_no_data),
         {8u, 0x5Bu},
         {9u, 0x03u}},
        {"a name cut by its package's end folds as a PkgLength past the table",
         kept_data_name,    sizeof(kept_data_name),
         {6u, 0x05u},
         {11u, 'E'} },
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        kept_data_check(test, &cases[index]);
}

/**
 * @brief Writes `Name (PKGD, Package (1) {Package (1) {... Package (1) {One, 0x0002}}})`, its packages nested as deep
 *        as §6.1 allows, its innermost WordPrefix the third byte from its end.
 *
 * @return The AML, or NULL when it does not fit.
 */
static const MadeUpAml_t *kept_data_deep(void)
{
    static const uint8_t innermost[] = {0x01u, 0x0Bu, 0x02u, 0x00u};
    static const uint8_t name[] = {0x08u, 'P', 'K', 'G', 'D'};
    static const uint8_t package = 0x12u;
    static const uint8_t count = 0x01u;
    static MadeUpAml_t levels[2];
    static MadeUpAml_t aml;
    uint32_t inner = 0u;

    made_up_aml_begin(&levels[0]);
    made_up_aml_raw(&levels[0], innermost, sizeof(innermost));
    for (uint32_t level = 1u; level < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_NESTED_PACKAGES;
         ++level)
    {
        made_up_aml_begin(&levels[1u - inner]);
        made_up_aml_raw(&levels[1u - inner], &count, 1u);
        made_up_aml_construct(&levels[1u - inner], &package, 1u, &levels[inner]);
        inner = 1u - inner;
    }
    made_up_aml_begin(&aml);
    made_up_aml_raw(&aml, name, sizeof(name));
    made_up_aml_construct(&aml, &package, 1u, &levels[inner]);
    return aml.overflowed ? NULL : &aml;
}

/**
 * @brief A kept package nested as deep as §6.1 allows, its innermost WordPrefix rewritten into a Package opcode one
 *        level deeper: it folds without writing past the fold's stack, as unreadable data, as the same element
 *        rewritten into a QWordPrefix its package cuts.
 */
KERNEL_TEST(kept_data_rewritten_one_package_deeper_than_allowed_folds_as_unreadable)
{
    const MadeUpAml_t *aml = kept_data_deep();

    if (!kernel_test_check(test, aml != NULL, "the nested table fits"))
        return;

    const KeptDataCase_t deep = {
        "a package one level too deep folds as a PkgLength past the table",
        aml->bytes,
        aml->length,
        {aml->length - 3u, 0x12u},
        {aml->length - 2u, 0x03u},
    };
    const KeptDataRewrite_t cut = {aml->length - 3u, 0x0Eu};
    uint32_t kept = 0u;

    kept_data_check(test, &deep);
    kernel_test_check(test,
                      kept_data_fold_after(aml->bytes, aml->length, deep.unreadable, &kept) ==
                          kept_data_fold_after(aml->bytes, aml->length, cut, &kept),
                      "a package one level too deep folds as a QWordPrefix its package cuts");
}

/**
 * @brief `Name (PKGX, Package (1) {One})`, the last node the namespace's fold walks, its One rewritten into a
 *        QWordPrefix with no bytes after it at the table's end: the fold is the intact one with the package's count
 *        and its Integer taken back out, then the count 0xFFFFFFFF, which no object has, and nothing after it.
 */
KERNEL_TEST(unreadable_kept_data_folds_as_a_count_no_object_has)
{
    static const uint8_t value[] = {0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    const KeptDataRewrite_t intact = {KEPT_DATA_UNCHANGED, 0u};
    const KeptDataRewrite_t cut = {8u, 0x0Eu};
    uint32_t kept = 0u;
    uint32_t expected = kept_data_fold_after(kept_data_integer, sizeof(kept_data_integer), intact, &kept);

    for (uint32_t index = sizeof(value); index != 0u; --index)
        expected = kept_data_unfold(expected, value[index - 1u]);
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        expected = (expected ^ 0xFFu) * KEPT_DATA_FOLD_PRIME;
    kernel_test_check(test, kept_data_fold_after(kept_data_integer, sizeof(kept_data_integer), cut, &kept) == expected,
                      "unreadable kept data folds as the count 0xFFFFFFFF and nothing after it");
}

/**
 * @brief Writes `Name (PKGL, Package (2) {"A...", "B..."})` into @p aml, each String @p string_bytes characters long,
 *        its PkgLength in three bytes, none of them zero.
 *
 * @return The bytes written, or 0 when the package's length does not fit three PkgLength bytes without a zero.
 */
static uint32_t kept_data_two_long_strings(uint8_t *aml, uint32_t string_bytes)
{
    static const uint8_t head[] = {0x08u, 'P', 'K', 'G', 'L', 0x12u};
    const uint32_t package_bytes = 3u + 1u + 2u * (1u + string_bytes + 1u);
    uint32_t at = sizeof(head);

    if (package_bytes >= (1u << 20) || (package_bytes & 0x0Fu) == 0u || ((package_bytes >> 4) & 0xFFu) == 0u ||
        (package_bytes >> 12) == 0u)
        return 0u;
    memcpy(aml, head, sizeof(head));
    aml[at++] = (uint8_t) (0x80u | (package_bytes & 0x0Fu));
    aml[at++] = (uint8_t) (package_bytes >> 4);
    aml[at++] = (uint8_t) (package_bytes >> 12);
    aml[at++] = 0x02u;
    for (uint8_t character = 'A'; character <= 'B'; ++character)
    {
        aml[at++] = 0x0Du;
        memset(aml + at, character, string_bytes);
        at += string_bytes;
        aml[at++] = 0x00u;
    }
    return at;
}

/**
 * @brief Kept data whose table AML rewrote past the sizes of §6.1, which machine_language_check_data refuses to keep
 *        and the object it would build refuses to hold, each folding as unreadable data, as a PkgLength past the
 *        table does: `Name (BUFX, Buffer (0x00000002) {0xAA})`, its size now 0x00010002;
 *        `Name (VPKG, VarPackage (0x0002) {One})`, its count now 0x1002; `Name (PKGL, Package (2) {"A...", "B..."})`,
 *        two Strings of 40,000 characters, the NUL of the first now 'x', which makes one String of 80,002.
 */
KERNEL_TEST(kept_data_rewritten_past_the_sizes_of_its_objects_folds_as_unreadable)
{
    const uint32_t string_bytes = KEPT_DATA_LONG_STRING_BYTES;
    uint8_t *strings = malloc(KEPT_DATA_LONG_STRINGS_CAPACITY);
    const uint32_t strings_length = (strings != NULL) ? kept_data_two_long_strings(strings, string_bytes) : 0u;

    if (!kernel_test_check(test, strings_length != 0u, "the package of two long strings is written"))
    {
        free(strings);
        return;
    }

    const KeptDataCase_t cases[] = {
        {"a buffer past 64 KiB folds as a PkgLength past the table",
         kept_data_buffer,           sizeof(kept_data_buffer),
         {10u, 0x01u},
         {8u, 0x03u}},
        {"a VarPackage past 4,096 elements folds as a PkgLength past the table",
         kept_data_variable_package, sizeof(kept_data_variable_package),
         {9u, 0x10u},
         {8u, 0x03u}},
        {"a string past 64 KiB folds as a PkgLength past the table",
         strings,                    strings_length,
         {11u + string_bytes, 'x'},
         {12u, 'C'} },
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        kept_data_check(test, &cases[index]);
    free(strings);
}

/**
 * @brief Loads the definition block @p aml, a DSDT of revision @p revision, from a copy of exactly its size, so that
 *        the address sanitizer catches a read past its end, with the load's shortcuts, so that it keeps its data;
 *        applies @p rewrite after the load, then builds every kept object as a name reaching it does: through the
 *        general data decoder when @p general holds, through the builder of kept data otherwise. @p out_built receives
 *        what that ended with.
 *
 * @return Whether the copy could be made.
 */
static bool kept_data_build_after(const uint8_t *aml, uint32_t length, uint8_t revision, KeptDataRewrite_t rewrite,
                                  bool general, KeptDataBuilt_t *out_built)
{
    uint8_t *copy = malloc(MADE_UP_TABLE_HEADER + length);
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t host;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;

    memset(out_built, 0, sizeof(*out_built));
    out_built->status = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    if (copy == NULL)
        return false;
    advanced_configuration_and_power_interface_simulated_host_open(&host);
    advanced_configuration_and_power_interface_machine_language_open(&load, &host.host, MADE_UP_MAX_NODES,
                                                                     MADE_UP_MAX_ARENA_BYTES);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &load, made_up_definition_block(copy, "DSDT", revision, aml, length));
    if (rewrite.offset != KEPT_DATA_UNCHANGED)
        copy[MADE_UP_TABLE_HEADER + rewrite.offset] = rewrite.byte;
    load.general_load = general;
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         index < load.space.node_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
            advanced_configuration_and_power_interface_machine_language_materialize(&load, index);

        if (out_built->status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
            out_built->status = (uint32_t) status;
    }
    out_built->kept = load.data_kept;
    out_built->live_objects = load.heap.objects - load.heap.objects_freed;
    out_built->object_bytes = load.heap.object_bytes;
    out_built->fold = advanced_configuration_and_power_interface_machine_language_namespace_fold(&load);
    out_built->faults_read = load.data_faults_read;
    out_built->deferred = load.values_deferred;
    out_built->built_directly = load.data_built_directly;
    advanced_configuration_and_power_interface_machine_language_close(&load);
    advanced_configuration_and_power_interface_simulated_host_close(&host);
    free(copy);
    return true;
}

/**
 * @brief Whether the kept data of @p aml, once @p rewrite is applied, builds the same through the builder of kept
 *        data as through the general data decoder: kept by the load, the same status, the same objects and bytes
 *        left, the same fold; the builder of kept data building @p built_directly of them itself, the general data
 *        decoder none.
 */
static bool kept_data_builds_the_same(const uint8_t *aml, uint32_t length, KeptDataRewrite_t rewrite,
                                      uint32_t built_directly)
{
    KeptDataBuilt_t direct;
    KeptDataBuilt_t general;

    return kept_data_build_after(aml, length, 2u, rewrite, false, &direct) &&
           kept_data_build_after(aml, length, 2u, rewrite, true, &general) && direct.kept == 1u &&
           memcmp(&direct, &general, offsetof(KeptDataBuilt_t, built_directly)) == 0 && general.built_directly == 0u &&
           direct.built_directly == built_directly;
}

/**
 * @brief Kept data whose table AML rewrote after the load builds through the builder of kept data (C4) as through the
 *        general data decoder, with nothing read past the table: a rewrite after which its bytes no longer read as
 *        kept data, which the builder hands to the general data decoder, builds none of it through the builder; a
 *        rewrite after which they still read, the builder builds. The rewrites are those the fold of kept data meets,
 *        each case's PkgLength rewritten past the table, `Name (PKGP, Package (1) {\\ABCD})`, its B now 0x01, and
 *        `Name (PKGT, Package (0x31) {ABCD, ..., One, One})`, its Package opcode now 'P', which makes the object a
 *        NameString, P01A, which only a package may hold.
 */
KERNEL_TEST(kept_data_whose_table_was_rewritten_builds_through_its_builder_as_through_the_general_decoder)
{
    const KeptDataRewrite_t head = {6u, 0xFFu};
    const MadeUpAml_t *deep = kept_data_deep();
    uint8_t *strings = malloc(KEPT_DATA_LONG_STRINGS_CAPACITY);
    const uint32_t strings_length =
        (strings != NULL) ? kept_data_two_long_strings(strings, KEPT_DATA_LONG_STRING_BYTES) : 0u;

    if (!kernel_test_check(test, deep != NULL && strings_length != 0u,
                           "the nested table and the package of two long strings are written"))
    {
        free(strings);
        return;
    }

    const KeptDataCase_t cases[] = {
        {"an integer cut by the table's end builds the same through both",
         kept_data_integer,          sizeof(kept_data_integer),
         {8u, 0x0Eu},
         {8u, 0x00u}               },
        {"a string whose NUL is gone builds the same through both",
         kept_data_string,           sizeof(kept_data_string),
         {10u, 'b'},
         {9u, 'c'}                 },
        {"a byte that starts no data object builds the same through both",
         kept_data_no_data,          sizeof(kept_data_no_data),
         {8u, 0x5Bu},
         {9u, 0x03u}               },
        {"a name cut by its package's end builds the same through both",
         kept_data_name,             sizeof(kept_data_name),
         {6u, 0x05u},
         {11u, 'E'}                },
        {"a package one level too deep builds the same through both",
         deep->bytes,
         deep->length,
         {deep->length - 3u, 0x12u},
         {deep->length - 2u, 0x03u}},
        {"a buffer past 64 KiB builds the same through both",
         kept_data_buffer,           sizeof(kept_data_buffer),
         {10u, 0x01u},
         {8u, 0x03u}               },
        {"a VarPackage past 4,096 elements builds the same through both",
         kept_data_variable_package, sizeof(kept_data_variable_package),
         {9u, 0x10u},
         {8u, 0x03u}               },
        {"a string past 64 KiB builds the same through both",
         strings,                    strings_length,
         {11u + KEPT_DATA_LONG_STRING_BYTES, 'x'},
         {12u, 'C'}                },
        {"a name after a prefix that is no longer one builds the same through both",
         kept_data_prefixed_name,    sizeof(kept_data_prefixed_name),
         {10u, 0x01u},
         {10u, 'E'}                },
        {"a package rewritten into a NameString builds the same through both",
         kept_data_named_package,    sizeof(kept_data_named_package),
         {5u, 'P'},
         {9u, 'E'}                 },
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        kernel_test_check(
            test,
            kept_data_builds_the_same(cases[index].aml, cases[index].length, cases[index].unreadable, 0u) &&
                kept_data_builds_the_same(cases[index].aml, cases[index].length, head, 0u) &&
                kept_data_builds_the_same(cases[index].aml, cases[index].length, cases[index].readable, 1u),
            cases[index].claim);
    free(strings);
}

/**
 * @brief Counts the rewrites of one byte of the kept data of @p sweep, from its opcode to the table's end, to each of
 *        the 256 values, that do not build the same through the builder of kept data as through the general data
 *        decoder, as kept_data_builds_the_same compares them. @p out_rewrites receives how many rewrites it built,
 *        @p out_built_directly how many of them the builder built itself.
 *
 * @return The rewrites that built differently, or whose copy could not be made.
 */
static uint32_t kept_data_one_byte_rewrites_that_differ(const KeptDataSweep_t *sweep, uint32_t *out_rewrites,
                                                        uint32_t *out_built_directly)
{
    uint32_t differ = 0u;

    *out_rewrites = 0u;
    *out_built_directly = 0u;
    for (uint32_t offset = KEPT_DATA_OPCODE; offset < sweep->length; ++offset)
        for (uint32_t byte = 0u; byte <= UINT8_MAX; ++byte)
        {
            const KeptDataRewrite_t rewrite = {offset, (uint8_t) byte};
            KeptDataBuilt_t direct;
            KeptDataBuilt_t general;
            const bool built =
                kept_data_build_after(sweep->aml, sweep->length, sweep->revision, rewrite, false, &direct) &&
                kept_data_build_after(sweep->aml, sweep->length, sweep->revision, rewrite, true, &general);

            differ += (!built || direct.kept != 1u ||
                       memcmp(&direct, &general, offsetof(KeptDataBuilt_t, built_directly)) != 0 ||
                       general.built_directly != 0u) ?
                          1u :
                          0u;
            ++*out_rewrites;
            *out_built_directly += direct.built_directly;
        }
    return differ;
}

/**
 * @brief Every rewrite of one byte of kept data, from its opcode to the table's end, to each of the 256 values,
 *        builds the same through the builder of kept data (C4) as through the general data decoder, with nothing read
 *        past the table, under a DSDT of revision 2 then 1: an element of each kind with a name after each prefix, a
 *        Buffer shorter than its size, and a padded VarPackage that holds a Package. Of each, some rewrites the
 *        builder builds itself, and the others it hands to the general data decoder.
 */
KERNEL_TEST(every_one_byte_rewrite_of_kept_data_builds_the_same_through_its_builder_and_the_general_decoder)
{
    const KeptDataSweep_t sweeps[] = {
        {"each rewrite of an element of each kind builds the same both ways under revision 2",           kept_data_every_kind,
         sizeof(kept_data_every_kind),                                                                                                                                    2u},
        {"each rewrite of an element of each kind builds the same both ways under revision 1",           kept_data_every_kind,
         sizeof(kept_data_every_kind),                                                                                                                                    1u},
        {"each rewrite of a Buffer shorter than its size builds the same both ways under revision 2",
         kept_data_short_buffer,                                                                                               sizeof(kept_data_short_buffer),            2u},
        {"each rewrite of a Buffer shorter than its size builds the same both ways under revision 1",
         kept_data_short_buffer,                                                                                               sizeof(kept_data_short_buffer),            1u},
        {"each rewrite of a VarPackage that holds a Package builds the same both ways under revision 2",
         kept_data_nested_variable_package,                                                                                    sizeof(kept_data_nested_variable_package), 2u},
        {"each rewrite of a VarPackage that holds a Package builds the same both ways under revision 1",
         kept_data_nested_variable_package,                                                                                    sizeof(kept_data_nested_variable_package), 1u},
    };
    uint32_t rewrites = 0u;
    uint32_t built_directly = 0u;

    for (uint32_t index = 0u; index < sizeof(sweeps) / sizeof(sweeps[0]); ++index)
    {
        uint32_t built = 0u;
        uint32_t made = 0u;

        kernel_test_check(test, kept_data_one_byte_rewrites_that_differ(&sweeps[index], &made, &built) == 0u,
                          sweeps[index].claim);
        kernel_test_check(test, built != 0u && built < made,
                          "the builder builds some rewrites and hands the others to the general data decoder");
        rewrites += made;
        built_directly += built;
    }
    kernel_test_measure(test, "rewrites", rewrites);
    kernel_test_measure(test, "built_directly", built_directly);
}

/**
 * @brief The check of kept data (O20), which the decoding pass runs, and the builder of kept data (C4), which the
 *        object's first read runs, stop at each bound of a package's elements without reading a byte past the table,
 *        loaded from a copy of exactly its size: a package head whose PkgLength of 1 leaves its count outside, one
 *        whose PkgLength passes its parent's end, and a NameSeg element the table's end cuts. The load keeps each
 *        package, whose own head reads; the decoding pass counts it in data_faults, and its build in data_faults_read.
 */
KERNEL_TEST(the_kept_data_check_stops_at_each_bound_without_reading_past_the_table)
{
    const KeptDataBound_t cases[] = {
        {"a PkgLength of 1 is a fault found without reading past the table",                 kept_data_head_without_count,
         sizeof(kept_data_head_without_count)},
        {"a PkgLength past its parent is a fault found without reading past the table",      kept_data_head_past_parent,
         sizeof(kept_data_head_past_parent)  },
        {"a NameSeg cut by the table's end is a fault found without reading past the table", kept_data_cut_name,
         sizeof(kept_data_cut_name)          },
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        KeptDataChecked_t checked;

        kernel_test_check(test,
                          kept_data_check_then_build(cases[index].aml, cases[index].length, &checked) &&
                              checked.kept == 1u && checked.faults == 1u && checked.faults_read == 1u,
                          cases[index].claim);
    }
}

/**
 * @brief A table-level buffer field over a kept Buffer whose bytes AML rewrote so that they build no object waits for
 *        the interpreter, as one over a Name left Uninitialized at load does (O20): BUF0 kept, its PkgLength then made
 *        to cross the table's end, an SSDT's `CreateByteField (BUF0, Zero, BYT0)` leaves BUF0 Uninitialized, counted
 *        as a fault, declares BYT0 without an object, counted as waiting, and the SSDT goes on to LAST.
 */
KERNEL_TEST(a_buffer_field_over_kept_data_that_builds_nothing_waits_for_the_interpreter)
{
    uint8_t *dsdt = malloc(MADE_UP_TABLE_HEADER + sizeof(kept_data_buffer_for_field));
    uint8_t *ssdt = malloc(MADE_UP_TABLE_HEADER + sizeof(kept_data_field_over_buffer));
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t host;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;

    if (!kernel_test_check(test, dsdt != NULL && ssdt != NULL, "the tables are allocated"))
    {
        free(dsdt);
        free(ssdt);
        return;
    }
    advanced_configuration_and_power_interface_simulated_host_open(&host);
    advanced_configuration_and_power_interface_machine_language_open(&load, &host.host, MADE_UP_MAX_NODES,
                                                                     MADE_UP_MAX_ARENA_BYTES);
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &load,
        made_up_definition_block(dsdt, "DSDT", 2u, kept_data_buffer_for_field, sizeof(kept_data_buffer_for_field)));
    dsdt[MADE_UP_TABLE_HEADER + 6u] = 0x3Fu;
    (void) advanced_configuration_and_power_interface_machine_language_declare_table(
        &load,
        made_up_definition_block(ssdt, "SSDT", 2u, kept_data_field_over_buffer, sizeof(kept_data_field_over_buffer)));

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *buffer = made_up_root_node(&load, "BUF0");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *field = made_up_root_node(&load, "BYT0");

    kernel_test_check(test,
                      load.data_kept == 1u && load.data_faults_read == 1u && buffer != NULL &&
                          buffer->type ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_UNINITIALIZED,
                      "BUF0 is left Uninitialized, counted as a fault");
    kernel_test_check(test,
                      field != NULL &&
                          field->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD &&
                          load.values_deferred == 1u && load.load_errors == 0u,
                      "BYT0 is declared without an object, waiting for the interpreter, and nothing is a load error");
    kernel_test_check(test, made_up_root_node(&load, "LAST") != NULL, "the SSDT goes on to LAST");
    advanced_configuration_and_power_interface_machine_language_close(&load);
    advanced_configuration_and_power_interface_simulated_host_close(&host);
    free(dsdt);
    free(ssdt);
}
