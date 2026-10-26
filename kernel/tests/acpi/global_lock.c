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
#define GLOBAL_LOCK_CONTROL_B    0x0640u  /**< The PM1b control port. */
#define GLOBAL_LOCK_ACCESS(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_##kind

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t global_lock_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t global_lock_load;
static uint8_t global_lock_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t global_lock_facs[64];
static MadeUpAml_t global_lock_aml;

/**
 * @brief Opens the simulated host with a FACS at @p facs, of @p length bytes, whose GlobalLock word
 *        holds @p word, a byte 0x5A to read, and a PM1a control port that reads 0x0001.
 */
static void global_lock_begin_at(uint32_t facs, const char *signature, uint32_t length, uint32_t word)
{
    static const uint8_t memory[] = {0x5Au};

    memset(global_lock_facs, 0, sizeof(global_lock_facs));
    memcpy(global_lock_facs, signature, 4u);
    memcpy(global_lock_facs + 4u, &length, sizeof(length));
    memcpy(global_lock_facs + 16u, &word, sizeof(word));
    advanced_configuration_and_power_interface_simulated_host_open(&global_lock_host);
    advanced_configuration_and_power_interface_simulated_host_place(&global_lock_host, facs, global_lock_facs,
                                                                    sizeof(global_lock_facs));
    advanced_configuration_and_power_interface_simulated_host_place(&global_lock_host, GLOBAL_LOCK_MEMORY, memory,
                                                                    sizeof(memory));
    advanced_configuration_and_power_interface_simulated_host_set_port(&global_lock_host, GLOBAL_LOCK_CONTROL, 0x0001u);
    global_lock_host.firmware_lock_address = facs + 16u;
    made_up_aml_begin(&global_lock_aml);
}

/**
 * @brief Opens the simulated host with a FACS of 64 bytes at GLOBAL_LOCK_FACS, as
 *        global_lock_begin_at does.
 */
static void global_lock_begin(const char *signature, uint32_t word)
{
    global_lock_begin_at(GLOBAL_LOCK_FACS, signature, sizeof(global_lock_facs), word);
}

/**
 * @brief Loads a table whose Lock field LK__ reads into RL__, the load given the FACS at
 *        @p firmware_control and the PM1 control @p ports before its table-level code runs.
 */
static bool global_lock_run_with(KernelTest_t *test, uint32_t firmware_control, const uint16_t *ports)
{
    static const MadeUpFieldUnit_t units[] = {
        {"LK__", 8u}
    };

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
 * @brief Loads the table global_lock_run_with loads, with the PM1a and PM1b control ports of the tests.
 */
static bool global_lock_run(KernelTest_t *test, uint32_t firmware_control)
{
    static const uint16_t ports[2] = {GLOBAL_LOCK_CONTROL, GLOBAL_LOCK_CONTROL_B};

    return global_lock_run_with(test, firmware_control, ports);
}

/**
 * @brief Methods that acquire `\_GL`: TAKE acquires then releases it and returns what Acquire gave,
 *        HOLD acquires it and keeps it, NEST acquires it twice and releases it twice.
 */
static const uint8_t global_lock_acquire_aml[] = {
    0x14u, 0x1Au, 'T',   'A',   'K', 'E', 0x00u, 0x70u, 0x5Bu, 0x23u, 0x5Cu, '_',   'G', 'L', '_', 0xFFu, 0xFFu,
    0x60u, 0x5Bu, 0x27u, 0x5Cu, '_', 'G', 'L',   '_',   0xA4u, 0x60u, 0x14u, 0x10u, 'H', 'O', 'L', 'D',   0x00u,
    0xA4u, 0x5Bu, 0x23u, 0x5Cu, '_', 'G', 'L',   '_',   0xFFu, 0xFFu, 0x14u, 0x26u, 'N', 'E', 'S', 'T',   0x00u,
    0x5Bu, 0x23u, 0x5Cu, '_',   'G', 'L', '_',   0xFFu, 0xFFu, 0x5Bu, 0x23u, 0x5Cu, '_', 'G', 'L', '_',   0xFFu,
    0xFFu, 0x5Bu, 0x27u, 0x5Cu, '_', 'G', 'L',   '_',   0x5Bu, 0x27u, 0x5Cu, '_',   'G', 'L', '_',
};

/**
 * @brief Loads the methods that acquire `\_GL`, the load given the FACS at @p firmware_control and the
 *        PM1 control ports of the tests.
 */
static bool global_lock_load_methods(KernelTest_t *test, uint32_t firmware_control)
{
    static const uint16_t ports[2] = {GLOBAL_LOCK_CONTROL, GLOBAL_LOCK_CONTROL_B};

    made_up_aml_raw(&global_lock_aml, global_lock_acquire_aml, sizeof(global_lock_acquire_aml));
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
    advanced_configuration_and_power_interface_machine_language_decode_tables(&global_lock_load);
    return true;
}

/**
 * @brief Evaluates the method at @p path, which takes no argument, and gives its Integer, or 0xDEAD;
 *        its status goes to @p out_status.
 */
static uint64_t global_lock_evaluate(const char *path,
                                     AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t *out_status)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint64_t integer = 0xDEADu;

    *out_status = advanced_configuration_and_power_interface_machine_language_evaluate(&global_lock_load, path, NULL,
                                                                                       0u, &result);
    if (*out_status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
        result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        integer = result.payload;
    advanced_configuration_and_power_interface_machine_language_value_release(&global_lock_load.heap,
                                                                              &global_lock_load.space, &result);
    return integer;
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

    return index < global_lock_host.access_count &&
           index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES && access->kind == kind &&
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
    kernel_test_check(test,
                      global_lock_host.outstanding_maps == 0u && global_lock_host.mismatched_unmaps == 0u &&
                          global_lock_host.inconsistent_accesses == 0u,
                      "every access went where its mapping puts it, and every map is unmapped as it was mapped");
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
 *        and writes GBL_RLS to both PM1 control registers: what they read ORed, SLP_EN and GBL_RLS
 *        masked out, then GBL_RLS set, as ACPI writes a register grouping.
 */
KERNEL_TEST(a_firmware_waiting_for_the_lock_is_signalled_on_release)
{
    global_lock_begin("FACS", 0u);
    global_lock_host.firmware_wants_lock = true;
    advanced_configuration_and_power_interface_simulated_host_set_port(&global_lock_host, GLOBAL_LOCK_CONTROL, 0x2001u);
    advanced_configuration_and_power_interface_simulated_host_set_port(&global_lock_host, GLOBAL_LOCK_CONTROL_B,
                                                                       0x0002u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_access(3u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 3u) &&
                          global_lock_access(4u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_access(5u, GLOBAL_LOCK_ACCESS(PORT_READ), GLOBAL_LOCK_CONTROL, 0x2001u) &&
                          global_lock_access(6u, GLOBAL_LOCK_ACCESS(PORT_READ), GLOBAL_LOCK_CONTROL_B, 0x0002u) &&
                          global_lock_access(7u, GLOBAL_LOCK_ACCESS(PORT_WRITE), GLOBAL_LOCK_CONTROL, 0x0007u) &&
                          global_lock_access(8u, GLOBAL_LOCK_ACCESS(PORT_WRITE), GLOBAL_LOCK_CONTROL_B, 0x0007u) &&
                          global_lock_host.access_count == 9u,
                      "the release finds pending, clears the lock, then writes PM1a | PM1b without SLP_EN, with "
                      "GBL_RLS, to PM1a then PM1b");
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
    kernel_test_check(test,
                      global_lock_load.evaluation.field_accesses ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_GLOBAL_LOCK_POLLS,
                      "each read of the lock word counts as a field access");
    global_lock_close(test);
}

/**
 * @brief A FACS that cannot be used gives no global lock, the FACS counted as refused, and the Lock
 *        field is accessed unlocked, counted: a wrong signature, usable memory, 20 bytes, an address
 *        not aligned on 64 bytes. Without a FACS, nothing is refused.
 */
KERNEL_TEST(a_facs_that_cannot_be_used_gives_no_global_lock)
{
    static const struct {
        uint32_t facs;
        const char *signature;
        uint32_t length;
        bool usable_memory;
        const char *claim;
    } refusals[] = {
        {GLOBAL_LOCK_FACS,         "FACX", 64u, false, "a wrong signature: read unlocked, the FACS counted as refused"},
        {GLOBAL_LOCK_FACS,         "FACS", 64u, true,  "a FACS in usable memory: refused"                             },
        {GLOBAL_LOCK_FACS,         "FACS", 20u, false, "a FACS of 20 bytes, too short for the lock word: refused"     },
        {GLOBAL_LOCK_FACS + 0x20u, "FACS", 64u, false, "a FACS not aligned on 64 bytes: refused"                      },
    };

    for (uint32_t index = 0u; index < sizeof(refusals) / sizeof(refusals[0]); ++index)
    {
        global_lock_begin_at(refusals[index].facs, refusals[index].signature, refusals[index].length, 0u);
        if (refusals[index].usable_memory)
            advanced_configuration_and_power_interface_simulated_host_describe(
                &global_lock_host, refusals[index].facs, 4096u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RAM);
        if (!global_lock_run(test, refusals[index].facs))
            return;
        kernel_test_check(test,
                          global_lock_result() == 0x5Au && global_lock_load.regions.global_lock_address == 0u &&
                              global_lock_load.regions.unlocked_lock_accesses == 1u &&
                              global_lock_load.regions.facs_refused,
                          refusals[index].claim);
        global_lock_close(test);
    }
    global_lock_begin("FACS", 0u);
    if (!global_lock_run(test, 0u))
        return;
    kernel_test_check(test,
                      global_lock_load.regions.unlocked_lock_accesses == 1u && !global_lock_load.regions.facs_refused,
                      "no FACS at all: read unlocked, nothing refused");
    global_lock_close(test);
}

/**
 * @brief The release never writes GBL_RLS to a port the kernel protects, even when the FADT names it
 *        as a PM1 control register, and counts no signal when it wrote nothing.
 */
KERNEL_TEST(the_release_never_writes_a_protected_port)
{
    static const uint16_t protected_ports[2] = {0x0CF8u, 0u};

    global_lock_begin("FACS", 0u);
    global_lock_host.firmware_wants_lock = true;
    if (!global_lock_run_with(test, GLOBAL_LOCK_FACS, protected_ports))
        return;
    kernel_test_check(test,
                      global_lock_host.access_count == 5u &&
                          global_lock_load.regions.global_lock_releases_signalled == 0u && global_lock_word() == 0u,
                      "the lock is released, and no port is read or written");
    global_lock_close(test);
}

/**
 * @brief The lock keeps the bits of its word it does not own, and a pending bit left without an owner
 *        is cleared when the field takes the lock.
 */
KERNEL_TEST(the_lock_word_keeps_its_other_bits)
{
    global_lock_begin("FACS", 0x80000000u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_access(1u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0x80000002u) &&
                          global_lock_access(4u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0x80000000u),
                      "bit 31 survives the take and the release");
    global_lock_close(test);
    global_lock_begin("FACS", 0x1u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test,
                      global_lock_access(1u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0x2u) &&
                          global_lock_load.regions.global_lock_waits == 0u && global_lock_word() == 0u,
                      "pending alone is cleared as the field takes the free lock, without a wait");
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
    for (uint32_t index = 0u; index < global_lock_host.access_count &&
                              index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
         ++index)
        exchanges += (global_lock_host.accesses[index].kind == GLOBAL_LOCK_ACCESS(EXCHANGE)) ? 1u : 0u;
    kernel_test_check(
        test, exchanges == 4u && global_lock_load.regions.global_lock_acquisitions == 2u && global_lock_word() == 0u,
        "IU0_ and LK__ each take the lock once, their registers nesting: four exchanges in all");
    global_lock_close(test);
}

/**
 * @brief `Acquire (\_GL)` takes the AML mutex, then the global lock as a Lock field does, and gives 0;
 *        `Release (\_GL)` gives the lock back. A `\_GL` still held when its evaluation ends is
 *        released then, its lock with it (§5.4).
 */
KERNEL_TEST(acquiring_the_global_lock_mutex_takes_the_firmware_lock)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    global_lock_begin("FACS", 0u);
    if (!global_lock_load_methods(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test, global_lock_evaluate("\\TAKE", &status) == 0u, "Acquire (\\_GL) gives 0");
    kernel_test_check(test,
                      global_lock_access(0u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_access(1u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 2u) &&
                          global_lock_access(2u, GLOBAL_LOCK_ACCESS(MEMORY_READ), GLOBAL_LOCK_WORD, 2u) &&
                          global_lock_access(3u, GLOBAL_LOCK_ACCESS(EXCHANGE), GLOBAL_LOCK_WORD, 0u) &&
                          global_lock_host.access_count == 4u,
                      "the lock is read free, marked owned, then read and cleared by the Release");
    kernel_test_check(test, global_lock_evaluate("\\HOLD", &status) == 0u && global_lock_word() == 0u,
                      "HOLD keeps \\_GL, and the end of its evaluation gives the lock back");
    kernel_test_check(test,
                      global_lock_load.regions.global_lock_acquisitions == 2u &&
                          global_lock_load.services.released_at_end == 1u && global_lock_load.services.held_count == 0u,
                      "two acquisitions of the lock; the one HOLD kept is released at the end, counted");
    global_lock_close(test);
}

/**
 * @brief Nested acquisitions of `\_GL` take the global lock once, and the last Release gives it back.
 */
KERNEL_TEST(nested_acquisitions_of_the_global_lock_mutex_take_the_lock_once)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    global_lock_begin("FACS", 0u);
    if (!global_lock_load_methods(test, GLOBAL_LOCK_FACS))
        return;
    (void) global_lock_evaluate("\\NEST", &status);
    kernel_test_check(test,
                      status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          global_lock_load.regions.global_lock_acquisitions == 1u &&
                          global_lock_host.access_count == 4u && global_lock_word() == 0u,
                      "one take and one release of the lock word for two acquisitions");
    global_lock_close(test);
}

/**
 * @brief When the firmware asks for the lock while `\_GL` holds it, the Release signals GBL_RLS.
 */
KERNEL_TEST(releasing_the_global_lock_mutex_signals_a_waiting_firmware)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    global_lock_begin("FACS", 0u);
    global_lock_host.firmware_wants_lock = true;
    if (!global_lock_load_methods(test, GLOBAL_LOCK_FACS))
        return;
    kernel_test_check(test, global_lock_evaluate("\\TAKE", &status) == 0u, "TAKE gives 0");
    kernel_test_check(test, global_lock_load.regions.global_lock_releases_signalled == 1u && global_lock_word() == 0u,
                      "the Release found pending, cleared the lock and signalled GBL_RLS");
    global_lock_close(test);
}

/**
 * @brief Without a FACS, `Acquire (\_GL)` takes the AML mutex alone, counted, and touches no memory.
 */
KERNEL_TEST(without_a_facs_the_global_lock_mutex_takes_only_the_aml_mutex)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    global_lock_begin("FACS", 0u);
    if (!global_lock_load_methods(test, 0u))
        return;
    kernel_test_check(test,
                      global_lock_evaluate("\\TAKE", &status) == 0u && global_lock_host.access_count == 0u &&
                          global_lock_load.regions.unlocked_lock_accesses == 1u,
                      "TAKE gives 0 without reading the lock word, counted as unlocked");
    global_lock_close(test);
}

/**
 * @brief A global lock the firmware never releases fails `Acquire (\_GL)` with
 *        global_lock_unavailable, and leaves the AML mutex free.
 */
KERNEL_TEST(a_global_lock_the_firmware_never_releases_fails_the_acquire)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    global_lock_begin("FACS", 2u);
    if (!global_lock_load_methods(test, GLOBAL_LOCK_FACS))
        return;
    (void) global_lock_evaluate("\\TAKE", &status);
    kernel_test_check(
        test,
        status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_GLOBAL_LOCK_UNAVAILABLE &&
            global_lock_load.regions.global_lock_timeouts == 1u && global_lock_load.services.held_count == 0u &&
            global_lock_load.services.acquisitions == 0u,
        "TAKE fails with global_lock_unavailable, and \\_GL is not held");
    global_lock_close(test);
}

/**
 * @brief A Buffer stored to a Lock unit takes the global lock once for all its chunks, as ACPICA and
 *        uACPI hold it for the whole store, and the unit keeps the last chunk.
 */
KERNEL_TEST(a_buffer_stored_to_a_lock_unit_takes_the_lock_once_for_every_chunk)
{
    static const uint8_t three[] = {0x01u, 0x02u, 0x03u};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t buffer = {0};

    global_lock_begin("FACS", 0u);
    if (!global_lock_run(test, GLOBAL_LOCK_FACS))
        return;
    buffer.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER;
    advanced_configuration_and_power_interface_machine_language_object_allocate(
        &global_lock_load.heap, &global_lock_load.space, buffer.kind, 3u, &buffer.handle);
    memcpy(advanced_configuration_and_power_interface_machine_language_object(&global_lock_load.heap, buffer.handle) +
               1,
           three, 3u);
    const uint32_t before = global_lock_load.regions.global_lock_acquisitions;
    const uint32_t node = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &global_lock_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "LK__", 0u));
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_field_write(&global_lock_load, node, &buffer) ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
        "the store succeeds");
    kernel_test_check(test,
                      global_lock_load.regions.global_lock_acquisitions - before == 1u &&
                          advanced_configuration_and_power_interface_simulated_host_peek(&global_lock_host,
                                                                                         GLOBAL_LOCK_MEMORY, 1u) == 3u,
                      "one acquisition for the three chunks, and the last chunk stays");
    global_lock_close(test);
}
