#include "fixtures/records.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_records, KERNEL_TEST_STAGE_INITIALIZATION);

#define RECORDS_TABLE_CAPACITY 256u /**< Bytes of the buffer the made-up DSDT is written in. */
#define RECORDS_WORK_COUNTERS  5u   /**< The counters the work record folds after the operations executed. */
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
 *        Buffer's length and bytes, a Package's count and each element the same way, a nested Package depth first, a
 *        name element as its path bytes; a path that names nothing and a Device fold their status alone.
 */
KERNEL_TEST(the_evaluations_fold_covers_each_status_type_and_value)
{
    static const char *const paths[] = {"\\INT_", "\\STR_", "\\BUF_", "\\PKG_", "\\MISS", "\\DEV_", "\\MTH_"};
    static const uint8_t bytes[] = {0x00u, 0x00u, 0x01u, 0xEFu, 0xCDu, 0xABu, 0x89u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x02u, 0x02u, 0x00u, 0x00u, 0x00u, 'a',   'b',   0x00u, 0x00u, 0x03u, 0x03u,
                                    0x00u, 0x00u, 0x00u, 0x01u, 0x02u, 0x03u, 0x00u, 0x00u, 0x04u, 0x04u, 0x00u, 0x00u,
                                    0x00u, 0x05u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u,
                                    0x00u, 'c',   0x01u, 0x00u, 0x00u, 0x00u, 0x06u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x00u, 'I',   'N',   'T',   '_',   0x04u, 0x00u, 0x13u, 0x00u, 0x00u, 0x00u,
                                    0x01u, 0xF0u, 0xCDu, 0xABu, 0x89u, 0x00u, 0x00u, 0x00u, 0x00u};
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
 *        allocated, the AML bytes and the map calls, four little-endian bytes each.
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
    records_load.bytes = 0x44u;
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
