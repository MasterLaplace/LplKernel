#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_global_lock, KERNEL_TEST_STAGE_INITIALIZATION);

#define GLOBAL_LOCK_FACS         0x30000u /**< Where the made-up FACS lies. */
#define GLOBAL_LOCK_WORD         0x30010u /**< Its GlobalLock word. */
#define GLOBAL_LOCK_MEMORY       0x10000u /**< The byte the Lock field reads. */
#define GLOBAL_LOCK_CONTROL      0x0604u  /**< The PM1a control port. */
#define GLOBAL_LOCK_ACCESS(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_##kind

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t global_lock_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t global_lock_load;
static uint8_t global_lock_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t global_lock_facs[64];
static MadeUpAml_t global_lock_aml;

/**
 * @brief Opens the simulated host with a FACS whose GlobalLock word holds @p word, a byte 0x5A to
 *        read, and a PM1a control port that reads 0x0001.
 */
static void global_lock_begin(const char *signature, uint32_t word)
{
    static const uint8_t memory[] = {0x5Au};
    const uint32_t length = sizeof(global_lock_facs);

    memset(global_lock_facs, 0, sizeof(global_lock_facs));
    memcpy(global_lock_facs, signature, 4u);
    memcpy(global_lock_facs + 4u, &length, sizeof(length));
    memcpy(global_lock_facs + 16u, &word, sizeof(word));
    advanced_configuration_and_power_interface_simulated_host_open(&global_lock_host);
    advanced_configuration_and_power_interface_simulated_host_place(&global_lock_host, GLOBAL_LOCK_FACS,
                                                                    global_lock_facs, sizeof(global_lock_facs));
    advanced_configuration_and_power_interface_simulated_host_place(&global_lock_host, GLOBAL_LOCK_MEMORY, memory,
                                                                    sizeof(memory));
    advanced_configuration_and_power_interface_simulated_host_set_port(&global_lock_host, GLOBAL_LOCK_CONTROL, 0x0001u);
    global_lock_host.firmware_lock_address = GLOBAL_LOCK_WORD;
    made_up_aml_begin(&global_lock_aml);
}

/**
 * @brief Loads a table whose Lock field LK__ reads into RL__, the load given the FACS at
 *        @p firmware_control before its table-level code runs.
 */
static bool global_lock_run(KernelTest_t *test, uint32_t firmware_control)
{
    static const MadeUpFieldUnit_t units[] = {
        {"LK__", 8u}
    };
    static const uint16_t ports[2] = {GLOBAL_LOCK_CONTROL, 0u};

    made_up_aml_region(&global_lock_aml, "LCKR", 0x00u, GLOBAL_LOCK_MEMORY, 1u);
    made_up_aml_field(&global_lock_aml, "LCKR", 0x11u, units, 1u);
    made_up_aml_name_integer(&global_lock_aml, "RL__", 0x55u);
    made_up_aml_store_name(&global_lock_aml, "LK__", "RL__");
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &global_lock_load, &global_lock_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
        return false;
    advanced_configuration_and_power_interface_machine_language_use_global_lock(&global_lock_load, firmware_control,
                                                                                ports);
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &global_lock_load,
        made_up_definition_block(global_lock_table, "DSDT", 2u, global_lock_aml.bytes, global_lock_aml.length));
    return true;
}

/**
 * @brief The integer RL__ holds, or 0xDEAD.
 */
static uint64_t global_lock_result(void)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        made_up_root_node(&global_lock_load, "RL__");

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Whether access @p index of the log is of @p kind, at @p address, with @p value.
 */
static bool global_lock_access(uint32_t index, uint8_t kind, uint64_t address, uint32_t value)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &global_lock_host.accesses[index];
    const bool port = kind == GLOBAL_LOCK_ACCESS(PORT_READ) || kind == GLOBAL_LOCK_ACCESS(PORT_WRITE);

    return index < global_lock_host.access_count && access->kind == kind &&
           (port ? access->port == address : access->address == address) && access->value == value;
}

/**
 * @brief The GlobalLock word as simulated memory holds it.
 */
static uint32_t global_lock_word(void)
{
    return (uint32_t) advanced_configuration_and_power_interface_simulated_host_peek(&global_lock_host,
                                                                                     GLOBAL_LOCK_WORD, 4u);
}

/**
 * @brief Closes the load, checks every map was unmapped, and gives the simulated memory back.
 */
static void global_lock_close(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_machine_language_close(&global_lock_load);
    kernel_test_check(test, global_lock_host.outstanding_maps == 0u && global_lock_host.mismatched_unmaps == 0u,
                      "every map is unmapped as it was mapped");
    advanced_configuration_and_power_interface_simulated_host_close(&global_lock_host);
}

/**
 * @brief A Lock field takes the free global lock, marking it owned, accesses its datum, then gives
 *        the lock back (ACPI 6.6 §5.2.10.1).
 */
KERNEL_TEST(a_lock_field_takes_and_releases_the_global_lock)
{
    global_lock_begin("FACS", 0u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_access(0u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_access(1u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 2u) &&
                          global_lock_access(2u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_MEMORY, 0x5Au) &&
                          global_lock_access(3u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 2u) &&
                          global_lock_access(4u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_host.access_count == 5u,
                      "the lock is read free, marked owned, the datum read, then the lock read and cleared");
    kernel_test_check(test,
                      global_lock_result() == 0x5Au && global_lock_word() == 0u &&
                          global_lock_load.regions.global_lock_acquisitions == 1u &&
                          global_lock_load.regions.global_lock_waits == 0u,
                      "RL__ reads 0x5A, the lock is free again, one acquisition without a wait");
    global_lock_close(test);
}

/**
 * @brief A lock the firmware owns is marked pending, then waited for: once the firmware releases it,
 *        the field takes it; the firmware cleared pending, so the release signals nothing.
 */
KERNEL_TEST(a_lock_the_firmware_owns_is_waited_for)
{
    global_lock_begin("FACS", 2u);
    global_lock_host.firmware_holds_for_reads = 3u;
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_access(0u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 2u) &&
                          global_lock_access(1u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 3u) &&
                          global_lock_access(2u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 3u) &&
                          global_lock_access(3u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 3u) &&
                          global_lock_access(4u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_access(5u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 2u),
                      "owned: marked pending, read until the firmware releases it, then taken");
    kernel_test_check(test,
                      global_lock_result() == 0x5Au && global_lock_word() == 0u &&
                          global_lock_load.regions.global_lock_waits == 1u &&
                          global_lock_load.regions.global_lock_releases_signalled == 0u,
                      "RL__ reads 0x5A after one wait, and the release signals nothing");
    global_lock_close(test);
}

/**
 * @brief When the firmware asks for the lock while the field owns it, the release clears pending
 *        and writes GBL_RLS to the PM1 control port.
 */
KERNEL_TEST(a_firmware_waiting_for_the_lock_is_signalled_on_release)
{
    global_lock_begin("FACS", 0u);
    global_lock_host.firmware_wants_lock = true;
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_access(3u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 3u) &&
                          global_lock_access(4u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_access(5u, GLOBAL_LOCK_ACCESS(PORT_READ), GLOBAL_LOCK_CONTROL, 0x0001u) &&
                          global_lock_access(6u, GLOBAL_LOCK_ACCESS(PORT_WRITE), GLOBAL_LOCK_CONTROL, 0x0005u) &&
                          global_lock_host.access_count == 7u,
                      "the release finds pending, clears the lock, then sets GBL_RLS in PM1a control");
    kernel_test_check(test, global_lock_load.regions.global_lock_releases_signalled == 1u, "the signal is counted");
    global_lock_close(test);
}

/**
 * @brief A lock the firmware never releases fails the access with global_lock_unavailable once the
 *        wait is spent, counted, and the field is never read.
 */
KERNEL_TEST(a_lock_the_firmware_never_releases_times_out)
{
    global_lock_begin("FACS", 2u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(
        test,
        global_lock_result() == 0x55u &&
            global_lock_load.evaluation.statuses
                    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_GLOBAL_LOCK_UNAVAILABLE] ==
                1u &&
            global_lock_load.regions.global_lock_timeouts == 1u,
        "the read fails with global_lock_unavailable, counted as a timeout");
    kernel_test_check(test,
                      global_lock_host.access_count ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_GLOBAL_LOCK_POLLS + 1u &&
                          global_lock_word() == 3u,
                      "every poll reads the lock, one exchange marks it pending, and the datum is never read");
    global_lock_close(test);
}

/**
 * @brief A FACS that is not one, by its signature, or that lies in usable memory gives no global
 *        lock: the Lock field is accessed unlocked, counted.
 */
KERNEL_TEST(a_facs_that_cannot_be_used_gives_no_global_lock)
{
    global_lock_begin("FACX", 0u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_result() == 0x5Au && global_lock_load.regions.global_lock_address == 0u &&
                          global_lock_load.regions.unlocked_lock_accesses == 1u,
                      "a wrong signature: read unlocked");
    global_lock_close(test);
    global_lock_begin("FACS", 0u);
    advanced_configuration_and_power_interface_simulated_host_describe(
        &global_lock_host, GLOBAL_LOCK_FACS, 4096u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RAM);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_result() == 0x5Au && global_lock_load.regions.global_lock_address == 0u &&
                          global_lock_load.regions.unlocked_lock_accesses == 1u,
                      "a FACS in usable memory: read unlocked");
    global_lock_close(test);
}

/**
 * @brief An IndexField declared Lock over registers that are Lock fields too takes the global lock
 *        once for the whole access: the registers nest in it.
 */
KERNEL_TEST(an_index_field_takes_the_lock_once_for_its_registers)
{
    static const MadeUpFieldUnit_t registers[] = {
        {"INDX", 8u},
        {"DATA", 8u}
    };
    static const MadeUpFieldUnit_t units[] = {
        {"IU0_", 8u}
    };
    uint32_t exchanges = 0u;

    global_lock_begin("FACS", 0u);
    made_up_aml_region(&global_lock_aml, "IDXR", 0x01u, 0x0B00u, 2u);
    made_up_aml_field(&global_lock_aml, "IDXR", 0x11u, registers, 2u);
    made_up_aml_index_field(&global_lock_aml, "INDX", "DATA", 0x11u, units, 1u);
    made_up_aml_name_integer(&global_lock_aml, "RI__", 0u);
    made_up_aml_store_name(&global_lock_aml, "IU0_", "RI__");
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    for (uint32_t index = 0u; index < global_lock_host.access_count; ++index)
        exchanges += (global_lock_host.accesses[index].kind == GLOBAL_LOCK_ACCESS(EXCHANGE)) ? 1u : 0u;
    kernel_test_check(
        test, exchanges == 4u && global_lock_load.regions.global_lock_acquisitions == 2u && global_lock_word() == 0u,
        "IU0_ and LK__ each take the lock once, their registers nesting: four exchanges in all");
    global_lock_close(test);
}
