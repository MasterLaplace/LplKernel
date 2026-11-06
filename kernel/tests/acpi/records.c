#include "fixtures/records.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_records, KERNEL_TEST_STAGE_INITIALIZATION);

#define RECORDS_TABLE_CAPACITY 256u /**< Bytes of the buffer the made-up DSDT is written in. */
#define RECORDS_DECLARED_BYTES                                                                                         \
    36u /**< What the declaration pass decodes of the table of decoded bytes: INT_'s 7 bytes, BUF_'s 11 but the 2 of   \
             its initialiser, DEV_'s 13, its head before the interpreter runs its body and its Name after, and LOOP's  \
             head of 7 bytes but its body of 13. */
#define RECORDS_TABLE_BYTES                                                                                            \
    51u /**< The AML bytes of the table of decoded bytes, all of which the decoding pass decodes. */
#define RECORDS_RECORDED_BYTES                                                                                         \
    8u /**< What LOOP's records are decoded from: its While's head and predicate, 6 bytes, then its Increment, 2. */
#define RECORDS_WORK_COUNTERS 5u /**< The counters the work record folds after the operations executed. */
#define RECORDS_WORK_BYTES                                                                                             \
    (4u * (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT +                                  \
           RECORDS_WORK_COUNTERS)) /**< Bytes the work record folds. */

static uint8_t records_table[RECORDS_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t records_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t records_load;
static uint8_t records_work_bytes[RECORDS_WORK_BYTES];
static uint32_t records_work_length;

/**
 * @brief FNV-1a, 32 bits, over @p length bytes, written here apart from the kernel's folds.
 */
static uint32_t records_fnv(const uint8_t *bytes, uint32_t length)
{
    uint32_t fold = 0x811C9DC5u;

    for (uint32_t byte = 0u; byte < length; ++byte)
        fold = (fold ^ bytes[byte]) * 0x01000193u;
    return fold;
}

/**
 * @brief Appends @p value to the bytes the work record is expected to fold, as four little-endian bytes.
 */
static void records_work_put(uint32_t value)
{
    for (uint32_t byte = 0u; byte < 4u; ++byte)
        records_work_bytes[records_work_length++] = (uint8_t) (value >> (8u * byte));
}

/**
 * @brief Loads the table of one object of each kind as a revision-2 DSDT on a fresh simulated host.
 */
static bool records_run(KernelTest_t *test)
{
    return made_up_load_table(test, &records_load, &records_host, records_table, 2u, records_every_kind_aml,
                              sizeof(records_every_kind_aml));
}

/**
 * @brief Closes the load and its simulated host.
 */
static void records_close(void)
{
    advanced_configuration_and_power_interface_machine_language_close(&records_load);
    advanced_configuration_and_power_interface_simulated_host_close(&records_host);
}

/**
 * @brief Loads the table of decoded bytes as a revision-2 DSDT, decoding no method body when @p skips_bodies holds as
 *        a release kernel loads, and gives the bytes the load decoded then; @p out_recorded receives those LOOP adds
 *        once it has run, which must give 3.
 */
static uint32_t records_decoded_by(KernelTest_t *test, bool skips_bodies, uint32_t *out_recorded)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint32_t decoded = 0u;

    *out_recorded = 0u;
    advanced_configuration_and_power_interface_simulated_host_open(&records_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &records_load, &records_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
    {
        records_close();
        return 0u;
    }
    records_load.skips_bodies = skips_bodies;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &records_load, made_up_definition_block(records_table, "DSDT", 2u, records_decoded_bytes_aml,
                                                sizeof(records_decoded_bytes_aml)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&records_load);
    decoded = records_load.bytes_decoded;
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(&records_load, "\\LOOP",
                                                                                           NULL, 0u, &result) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          result.payload == 3u,
                      "LOOP runs its While three times and returns 3");
    advanced_configuration_and_power_interface_machine_language_value_release(&records_load.heap, &records_load.space,
                                                                              &result);
    *out_recorded = records_load.bytes_decoded - decoded;
    records_close();
    return decoded;
}

/**
 * @brief Loads @p aml, whose PKG_ is a Package its Name keeps in its table, as a revision-2 DSDT, and folds its
 *        namespace into @p out_folds[0], the evaluations record of PKG_ into @p out_folds[1], then the namespace again,
 *        PKG_ built by that evaluation, into @p out_folds[2].
 */
static void records_package_folds(KernelTest_t *test, const uint8_t *aml, uint32_t length, uint32_t out_folds[3])
{
    static const char *const package[] = {"\\PKG_"};
    uint32_t succeeded = 0u;

    if (!made_up_load_table(test, &records_load, &records_host, records_table, 2u, aml, length))
        return;
    kernel_test_check(test, records_load.data_kept == 1u, "PKG_ is kept in its table");
    out_folds[0] = advanced_configuration_and_power_interface_machine_language_namespace_fold(&records_load);
    out_folds[1] = advanced_configuration_and_power_interface_machine_language_evaluations_fold(&records_load, package,
                                                                                                1u, &succeeded);
    out_folds[2] = advanced_configuration_and_power_interface_machine_language_namespace_fold(&records_load);
    kernel_test_check(test, succeeded == 1u && records_load.data_materialized == 1u, "PKG_ evaluates, and is built");
    records_close();
}

/**
 * @brief The work record of a fresh load of the table of one object of each kind once MTH_ has been evaluated
 *        @p evaluations times, or 0 when the table does not load.
 */
static uint32_t records_work_after(KernelTest_t *test, uint32_t evaluations)
{
    static const char *const method[] = {"\\MTH_"};
    uint32_t succeeded = 0u;
    uint32_t fold = 0u;

    if (!records_run(test))
        return 0u;
    for (uint32_t evaluation = 0u; evaluation < evaluations; ++evaluation)
        (void) advanced_configuration_and_power_interface_machine_language_evaluations_fold(&records_load, method, 1u,
                                                                                            &succeeded);
    fold = advanced_configuration_and_power_interface_machine_language_work_fold(&records_load);
    records_close();
    return fold;
}

/**
 * @brief The evaluations record equals FNV-1a over the bytes §8.7 lists, written out by hand: per path, its status
 *        in two bytes, then, once it succeeded, its result's type, then an Integer's eight bytes, a String's or a
 *        Buffer's length and bytes, a Package's count and each element the same way, its type first, a nested Package
 *        depth first, a name element as its path bytes; a path that names nothing and a Device fold their status
 *        alone.
 */
KERNEL_TEST(the_evaluations_fold_covers_each_status_type_and_value)
{
    static const char *const paths[] = {"\\INT_", "\\STR_", "\\BUF_", "\\PKG_", "\\MISS", "\\DEV_", "\\MTH_"};
    static const uint8_t bytes[] = {
        0x00u, 0x00u, 0x01u, 0xEFu, 0xCDu, 0xABu, 0x89u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u, 0x02u,
        0x00u, 0x00u, 0x00u, 'a',   'b',   0x00u, 0x00u, 0x03u, 0x03u, 0x00u, 0x00u, 0x00u, 0x01u, 0x02u, 0x03u,
        0x00u, 0x00u, 0x04u, 0x04u, 0x00u, 0x00u, 0x00u, 0x01u, 0x05u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x02u, 0x01u, 0x00u, 0x00u, 0x00u, 'c',   0x04u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x06u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x80u, 'I',   'N',   'T',   '_',   0x04u, 0x00u, 0x13u, 0x00u,
        0x00u, 0x00u, 0x01u, 0xF0u, 0xCDu, 0xABu, 0x89u, 0x00u, 0x00u, 0x00u, 0x00u};
    uint32_t succeeded = 0u;

    if (!records_run(test))
        return;

    const uint32_t fold = advanced_configuration_and_power_interface_machine_language_evaluations_fold(
        &records_load, paths, sizeof(paths) / sizeof(paths[0]), &succeeded);

    kernel_test_check(test, fold == records_fnv(bytes, sizeof(bytes)),
                      "the fold is FNV-1a over the bytes written out by hand");
    kernel_test_check(test, succeeded == 5u, "five paths evaluate, MISS names nothing and DEV_ holds no data");
    kernel_test_check(test, records_load.evaluation.frame_count == 0u && records_load.evaluation.value_count == 0u,
                      "every result is given back");
    kernel_test_measure(test, "evaluations_succeeded", succeeded);
    kernel_test_measure_hexadecimal(test, "evaluations_fold", fold);
    records_close();
}

/**
 * @brief The work record of a load whose counters are set here equals FNV-1a over the bytes §8.7 lists, written out
 *        by hand: each opcode's operations executed in descriptor order, then the lookups, the probes, the objects
 *        allocated, the bytes decoded, not the size of the tables, and the map calls, four little-endian bytes each.
 */
KERNEL_TEST(the_work_fold_covers_every_counter)
{
    const uint32_t last = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT - 1u;

    memset(&records_load, 0, sizeof(records_load));
    records_load.evaluation.executed[1] = 0x04030201u;
    records_load.evaluation.executed[last] = 7u;
    records_load.space.lookups = 0x11u;
    records_load.space.probes = 0x22u;
    records_load.heap.objects = 0x33u;
    records_load.bytes = 0x66u;
    records_load.bytes_decoded = 0x44u;
    records_load.regions.map_calls = 0x55u;
    records_work_length = 0u;
    for (uint32_t opcode = 0u; opcode <= last; ++opcode)
        records_work_put((opcode == 1u) ? 0x04030201u : ((opcode == last) ? 7u : 0u));
    records_work_put(0x11u);
    records_work_put(0x22u);
    records_work_put(0x33u);
    records_work_put(0x44u);
    records_work_put(0x55u);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_work_fold(&records_load) ==
                          records_fnv(records_work_bytes, records_work_length),
                      "the fold is FNV-1a over the bytes written out by hand");
}

/**
 * @brief A control of the work record: the same evaluations of the same table fold the same, and one more evaluation
 *        of the same method folds differently, which a load that runs nothing would not tell apart.
 */
KERNEL_TEST(one_more_evaluation_changes_the_work_fold)
{
    const uint32_t once = records_work_after(test, 1u);

    kernel_test_check(test, once == records_work_after(test, 1u), "the same evaluations fold the same");
    kernel_test_check(test, once != records_work_after(test, 2u), "one more evaluation folds differently");
    kernel_test_measure_hexadecimal(test, "work_fold", once);
}

/**
 * @brief The bytes decoded are those the walks decode and the loop records are decoded from, counted by hand from the
 *        table's bytes: the declaration pass steps over a method body and the initialiser of data it keeps in its
 *        table, the decoding pass decodes every byte, and a While that loops adds the statements its records hold,
 *        unless the boot line asks for no record (lpl.acpi.predecoding=off).
 */
KERNEL_TEST(the_bytes_decoded_leave_out_what_the_walks_step_over)
{
    uint32_t release_recorded = 0u;
    uint32_t debug_recorded = 0u;
    const uint32_t release = records_decoded_by(test, true, &release_recorded);
    const uint32_t debug = records_decoded_by(test, false, &debug_recorded);
    const uint32_t recorded = records_load.predecodes_loops ? RECORDS_RECORDED_BYTES : 0u;

    kernel_test_check(test, release == RECORDS_DECLARED_BYTES,
                      "a load that decodes no body decodes the declarations, not LOOP's body nor BUF_'s initialiser");
    kernel_test_check(test, debug == RECORDS_DECLARED_BYTES + RECORDS_TABLE_BYTES,
                      "the decoding pass decodes the whole table once more");
    kernel_test_check(test, release_recorded == recorded && debug_recorded == recorded,
                      "LOOP's records add the bytes of its While's head and predicate and of its Increment");
    kernel_test_measure(test, "bytes_decoded_release", release);
    kernel_test_measure(test, "bytes_decoded_debug", debug);
}

/**
 * @brief A package element folds its type before its value (§8.7), so that `Package (1) {4}` and
 *        `Package (1) {Buffer (4) {0, 0, 0, 0}}`, whose bytes would otherwise fold alike, fold apart in the namespace
 * and the evaluations records, and an element a package's count adds past its initialisers folds as the type 0; the
 *        evaluation of each equals FNV-1a over the bytes written out by hand, and a package kept in its table folds as
 *        the one it builds into.
 */
KERNEL_TEST(a_package_element_folds_its_type_before_its_value)
{
    static const uint8_t integer_bytes[] = {0x00u, 0x00u, 0x04u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u,
                                            0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t buffer_bytes[] = {0x00u, 0x00u, 0x04u, 0x01u, 0x00u, 0x00u, 0x00u, 0x03u,
                                           0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t padded_bytes[] = {0x00u, 0x00u, 0x04u, 0x02u, 0x00u, 0x00u, 0x00u, 0x01u, 0x04u,
                                           0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    uint32_t integer[3] = {0u};
    uint32_t buffer[3] = {0u};
    uint32_t padded[3] = {0u};

    records_package_folds(test, records_package_of_an_integer_aml, sizeof(records_package_of_an_integer_aml), integer);
    records_package_folds(test, records_package_of_a_buffer_aml, sizeof(records_package_of_a_buffer_aml), buffer);
    records_package_folds(test, records_package_padded_aml, sizeof(records_package_padded_aml), padded);
    kernel_test_check(test,
                      integer[1] == records_fnv(integer_bytes, sizeof(integer_bytes)) &&
                          buffer[1] == records_fnv(buffer_bytes, sizeof(buffer_bytes)) &&
                          padded[1] == records_fnv(padded_bytes, sizeof(padded_bytes)),
                      "each evaluation folds the bytes written out by hand, the element's type before its value");
    kernel_test_check(test, integer[0] != buffer[0] && integer[1] != buffer[1],
                      "a package of an Integer and a package of a Buffer of the same bytes fold apart");
    kernel_test_check(test, integer[0] == integer[2] && buffer[0] == buffer[2] && padded[0] == padded[2],
                      "a package kept in its table folds as the package it builds into, its padding included");
    kernel_test_measure_hexadecimal(test, "namespace_fold_integer", integer[0]);
    kernel_test_measure_hexadecimal(test, "namespace_fold_buffer", buffer[0]);
    kernel_test_measure_hexadecimal(test, "namespace_fold_padded", padded[0]);
}
