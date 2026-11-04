#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

KERNEL_TEST_SUITE(host_kept_data, KERNEL_TEST_STAGE_INITIALIZATION);

#define KEPT_DATA_UNCHANGED  0xFFFFFFFFu /**< The offset of a byte left as it is. */
#define KEPT_DATA_FOLD_PRIME 0x01000193u /**< FNV-1a's prime, which the namespace's fold multiplies by. */

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
    static const uint8_t integer[] = {0x08u, 'P', 'K', 'G', 'X', 0x12u, 0x03u, 0x01u, 0x01u};
    static const uint8_t string[] = {0x08u, 'P', 'K', 'G', 'S', 0x12u, 0x05u, 0x01u, 0x0Du, 'a', 0x00u};
    static const uint8_t no_data[] = {0x08u, 'P', 'K', 'G', 'N', 0x12u, 0x05u, 0x02u, 0x0Au, 0x02u, 0x01u};
    static const uint8_t name[] = {0x08u, 'P', 'K', 'G', 'M', 0x12u, 0x07u, 0x02u, 'A', 'B', 'C', 'D', 0x01u};
    const KeptDataCase_t cases[] = {
        {"an integer cut by the table's end folds as a PkgLength past the table",
         integer, sizeof(integer),
         {8u, 0x0Eu},
         {8u, 0x00u}},
        {"a string whose NUL is gone folds as a PkgLength past the table",
         string,  sizeof(string),
         {10u, 'b'},
         {9u, 'c'}  },
        {"a byte that starts no data object folds as a PkgLength past the table",
         no_data, sizeof(no_data),
         {8u, 0x5Bu},
         {9u, 0x03u}},
        {"a name cut by its package's end folds as a PkgLength past the table",
         name,    sizeof(name),
         {6u, 0x05u},
         {11u, 'E'} },
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        kept_data_check(test, &cases[index]);
}

/**
 * @brief A kept package nested as deep as §6.1 allows, its innermost WordPrefix rewritten into a Package opcode one
 *        level deeper: it folds without writing past the fold's stack, as unreadable data, as the same element
 *        rewritten into a QWordPrefix its package cuts.
 */
KERNEL_TEST(kept_data_rewritten_one_package_deeper_than_allowed_folds_as_unreadable)
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
    if (!kernel_test_check(test, !aml.overflowed, "the nested table fits"))
        return;

    const KeptDataCase_t deep = {
        "a package one level too deep folds as a PkgLength past the table",
        aml.bytes,
        aml.length,
        {aml.length - 3u, 0x12u},
        {aml.length - 2u, 0x03u},
    };
    const KeptDataRewrite_t cut = {aml.length - 3u, 0x0Eu};
    uint32_t kept = 0u;

    kept_data_check(test, &deep);
    kernel_test_check(test,
                      kept_data_fold_after(aml.bytes, aml.length, deep.unreadable, &kept) ==
                          kept_data_fold_after(aml.bytes, aml.length, cut, &kept),
                      "a package one level too deep folds as a QWordPrefix its package cuts");
}

/**
 * @brief `Name (PKGX, Package (1) {One})`, the last node the namespace's fold walks, its One rewritten into a
 *        QWordPrefix with no bytes after it at the table's end: the fold is the intact one with the package's count
 *        and its Integer taken back out, then the count 0xFFFFFFFF, which no object has, and nothing after it.
 */
KERNEL_TEST(unreadable_kept_data_folds_as_a_count_no_object_has)
{
    static const uint8_t aml[] = {0x08u, 'P', 'K', 'G', 'X', 0x12u, 0x03u, 0x01u, 0x01u};
    static const uint8_t value[] = {0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    const KeptDataRewrite_t intact = {KEPT_DATA_UNCHANGED, 0u};
    const KeptDataRewrite_t cut = {8u, 0x0Eu};
    uint32_t kept = 0u;
    uint32_t expected = kept_data_fold_after(aml, sizeof(aml), intact, &kept);

    for (uint32_t index = sizeof(value); index != 0u; --index)
        expected = kept_data_unfold(expected, value[index - 1u]);
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        expected = (expected ^ 0xFFu) * KEPT_DATA_FOLD_PRIME;
    kernel_test_check(test, kept_data_fold_after(aml, sizeof(aml), cut, &kept) == expected,
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
    static const uint8_t buffer[] = {0x08u, 'B', 'U', 'F', 'X', 0x11u, 0x07u, 0x0Cu, 0x02u, 0x00u, 0x00u, 0x00u, 0xAAu};
    static const uint8_t package[] = {0x08u, 'V', 'P', 'K', 'G', 0x13u, 0x05u, 0x0Bu, 0x02u, 0x00u, 0x01u};
    const uint32_t string_bytes = 40000u;
    uint8_t *strings = malloc(16u + 2u * (string_bytes + 2u));
    const uint32_t strings_length = (strings != NULL) ? kept_data_two_long_strings(strings, string_bytes) : 0u;

    if (!kernel_test_check(test, strings_length != 0u, "the package of two long strings is written"))
    {
        free(strings);
        return;
    }

    const KeptDataCase_t cases[] = {
        {"a buffer past 64 KiB folds as a PkgLength past the table",             buffer, sizeof(buffer),  {10u, 0x01u}, {8u, 0x03u}},
        {"a VarPackage past 4,096 elements folds as a PkgLength past the table",
         package,                                                                        sizeof(package),
         {9u, 0x10u},
         {8u, 0x03u}                                                                                                               },
        {"a string past 64 KiB folds as a PkgLength past the table",
         strings,                                                                        strings_length,
         {11u + string_bytes, 'x'},
         {12u, 'C'}                                                                                                                },
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        kept_data_check(test, &cases[index]);
    free(strings);
}
