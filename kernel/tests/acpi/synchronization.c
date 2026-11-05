#include "fixtures/synchronization.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_synchronization, KERNEL_TEST_STAGE_INITIALIZATION);

#define SYNCHRONIZATION_TABLE_CAPACITY 2048u                 /**< Bytes of the buffer a made-up DSDT is written in. */
#define SYNCHRONIZATION_ONES           0xFFFFFFFFFFFFFFFFull /**< Ones, under a revision-2 table. */
#define SYNCHRONIZATION_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name

static uint8_t synchronization_table[SYNCHRONIZATION_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t synchronization_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t synchronization_load;

/**
 * @brief Loads the table as a DSDT of @p revision on a fresh simulated host.
 */
static bool synchronization_run_revision(KernelTest_t *test, uint8_t revision)
{
    return made_up_load_table(test, &synchronization_load, &synchronization_host, synchronization_table, revision,
                              synchronization_aml, sizeof(synchronization_aml));
}

/**
 * @brief Loads the table as a DSDT of revision 2 on a fresh simulated host.
 */
static bool synchronization_run(KernelTest_t *test) { return synchronization_run_revision(test, 2u); }

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t synchronization_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        made_up_root_node(&synchronization_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Evaluates the method at @p path, which takes no argument, and gives its Integer, or 0xDEAD;
 *        its status goes to @p out_status.
 */
static uint64_t synchronization_evaluate(const char *path,
                                         AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t *out_status)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint64_t integer = 0xDEADu;

    *out_status = advanced_configuration_and_power_interface_machine_language_evaluate(&synchronization_load, path,
                                                                                       NULL, 0u, &result);
    if (*out_status == SYNCHRONIZATION_STATUS(OK) &&
        result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        integer = result.payload;
    advanced_configuration_and_power_interface_machine_language_value_release(&synchronization_load.heap,
                                                                              &synchronization_load.space, &result);
    return integer;
}

/**
 * @brief Checks that the evaluation left no mutex held and the sync level at 0, then closes the load.
 */
static void synchronization_close(KernelTest_t *test)
{
    kernel_test_check(test,
                      synchronization_load.services.held_count == 0u &&
                          synchronization_load.services.sync_level == 0u &&
                          synchronization_load.evaluation.frame_count == 0u,
                      "no mutex is held once the evaluation is over, and the sync level is back to 0");
    advanced_configuration_and_power_interface_machine_language_close(&synchronization_load);
    advanced_configuration_and_power_interface_simulated_host_close(&synchronization_host);
}

/**
 * @brief Acquire gives 0 and the evaluation that holds a mutex may acquire it again; each Release
 *        gives one acquisition back (§5.2).
 */
KERNEL_TEST(a_mutex_is_acquired_and_nested_by_its_owner)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    kernel_test_check(test, synchronization_evaluate("\\NEST", &status) == 0u && status == SYNCHRONIZATION_STATUS(OK),
                      "both acquisitions give 0");
    kernel_test_check(
        test,
        synchronization_load.services.acquisitions == 2u && synchronization_load.services.held_peak == 1u &&
            synchronization_load.services.released_at_end == 0u && synchronization_load.services.releases_ignored == 0u,
        "two acquisitions of one mutex, both released by the method, neither release ignored");
    synchronization_close(test);
}

/**
 * @brief A mutex the evaluation still holds when it ends is released then, counted, and the next
 *        evaluation acquires it again (§5.2).
 */
KERNEL_TEST(a_mutex_still_held_is_released_when_the_evaluation_ends)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    kernel_test_check(test, synchronization_evaluate("\\KEEP", &status) == 0u, "KEEP acquires MTA1");
    kernel_test_check(test, synchronization_load.services.released_at_end == 1u, "the end releases it, counted");
    kernel_test_check(test, synchronization_evaluate("\\KEEP", &status) == 0u, "the next evaluation acquires it");
    synchronization_close(test);
}

/**
 * @brief An Acquire of a mutex whose SyncLevel is below the current level is ignored, counted, and
 *        gives Ones, as uACPI answers it; its Release is then of a mutex not held, ignored too.
 */
KERNEL_TEST(an_acquire_below_the_current_sync_level_is_ignored)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    kernel_test_check(test, synchronization_evaluate("\\LOWR", &status) == SYNCHRONIZATION_ONES,
                      "MTA1 (level 1) under MTX2 (level 2) gives Ones");
    kernel_test_check(test,
                      synchronization_load.services.acquisitions_ignored == 1u &&
                          synchronization_load.services.releases_ignored == 1u &&
                          synchronization_load.services.released_at_end == 0u,
                      "the acquisition and its release are ignored, counted, and MTX2 is released by the method");
    synchronization_close(test);
}

/**
 * @brief A Release of a mutex not held is ignored and counted; one out of order at the current level
 *        is accepted; one at another level is ignored, and the mutex stays held until the end.
 */
KERNEL_TEST(a_release_out_of_order_is_accepted_only_at_the_current_level)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    (void) synchronization_evaluate("\\UNHD", &status);
    kernel_test_check(test,
                      status == SYNCHRONIZATION_STATUS(OK) && synchronization_load.services.releases_ignored == 1u,
                      "a mutex never acquired: its release is ignored");
    kernel_test_check(
        test,
        synchronization_evaluate("\\SAME", &status) == 0u && status == SYNCHRONIZATION_STATUS(OK) &&
            synchronization_load.services.releases_ignored == 1u && synchronization_load.services.released_at_end == 0u,
        "MTA1 released before MTB1, both at level 1: accepted, the level back to 0 for MTX0, nothing left "
        "held");
    (void) synchronization_evaluate("\\DIFF", &status);
    kernel_test_check(test,
                      status == SYNCHRONIZATION_STATUS(OK) && synchronization_load.services.releases_ignored == 2u &&
                          synchronization_load.services.released_at_end == 2u,
                      "MTA1 released under MTX2: ignored, both released when the evaluation ends");
    synchronization_close(test);
}

/**
 * @brief An evaluation holds at most 32 mutexes: the 33rd Acquire fails with limit_held_mutexes, and
 *        the 32 held are released (§6.1).
 */
KERNEL_TEST(the_33rd_mutex_held_is_refused)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    (void) synchronization_evaluate("\\MANY", &status);
    kernel_test_check(test, status == SYNCHRONIZATION_STATUS(LIMIT_HELD_MUTEXES), "MANY fails with limit_held_mutexes");
    kernel_test_check(
        test, synchronization_load.services.held_peak == 32u && synchronization_load.services.released_at_end == 32u,
        "32 were held, and released when the evaluation failed");
    kernel_test_measure(test, "held_peak", synchronization_load.services.held_peak);
    synchronization_close(test);
}

/**
 * @brief Acquire and Signal of an object of another type fail with type_mismatch; a mutex named
 *        through a reference held in a local is acquired.
 */
KERNEL_TEST(acquire_and_signal_refuse_an_object_of_another_type)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    (void) synchronization_evaluate("\\NOTM", &status);
    kernel_test_check(test, status == SYNCHRONIZATION_STATUS(TYPE_MISMATCH), "Acquire (INT0) fails with type_mismatch");
    (void) synchronization_evaluate("\\SIGI", &status);
    kernel_test_check(test, status == SYNCHRONIZATION_STATUS(TYPE_MISMATCH), "Signal (INT0) fails with type_mismatch");
    kernel_test_check(test,
                      synchronization_evaluate("\\VIAR", &status) == 0u && status == SYNCHRONIZATION_STATUS(OK) &&
                          synchronization_load.services.acquisitions == 1u,
                      "Acquire (Local0), Local0 holding RefOf (MTX0), acquires MTX0");
    synchronization_close(test);
}

/**
 * @brief Signal counts the Event's pending signals up, and Wait takes one of them (§5.2).
 */
KERNEL_TEST(signal_counts_an_event_up)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *event = NULL;

    if (!synchronization_run(test))
        return;
    (void) synchronization_evaluate("\\SIGN", &status);
    event = made_up_root_node(&synchronization_load, "EVT0");
    kernel_test_check(test,
                      status == SYNCHRONIZATION_STATUS(OK) && event != NULL && event->value.payload == 2u &&
                          synchronization_load.services.signals == 2u,
                      "two signals pend on EVT0");
    kernel_test_check(test,
                      synchronization_evaluate("\\EWAI", &status) == 0u && status == SYNCHRONIZATION_STATUS(OK) &&
                          event->value.payload == 1u,
                      "Wait takes one of them and gives 0: one still pends");
    synchronization_close(test);
}

/**
 * @brief Wait consumes a pending signal and gives 0, or reports a timeout at once, True, whatever the timeout it was
 *        given, which is evaluated; Reset clears every pending signal; both refuse an object that is no Event (§5.2).
 *        True is Ones at the width of the table's integers (§5.2).
 */
KERNEL_TEST(wait_consumes_a_signal_and_reset_clears_them)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *event = NULL;

    if (!synchronization_run(test))
        return;
    (void) synchronization_evaluate("\\CONS", &status);
    event = made_up_root_node(&synchronization_load, "EVT0");
    kernel_test_check(test,
                      status == SYNCHRONIZATION_STATUS(OK) && synchronization_integer("WAI0") == 0u &&
                          synchronization_integer("WAI1") == 0u,
                      "the first two Waits each take one of the two signals and give 0");
    kernel_test_check(
        test, synchronization_integer("WAI2") == SYNCHRONIZATION_ONES && event != NULL && event->value.payload == 0u,
        "the third, with no signal left, times out at once and gives True");
    kernel_test_check(test, synchronization_integer("CNT0") == 1u, "the second Wait's timeout called TMO_");
    kernel_test_check(test,
                      synchronization_evaluate("\\RSTE", &status) == SYNCHRONIZATION_ONES &&
                          status == SYNCHRONIZATION_STATUS(OK) && event != NULL && event->value.payload == 0u,
                      "Reset clears both signals: the Wait after it times out");
    (void) synchronization_evaluate("\\RSTI", &status);
    kernel_test_check(test, status == SYNCHRONIZATION_STATUS(TYPE_MISMATCH), "Reset of an Integer is refused");
    (void) synchronization_evaluate("\\WAII", &status);
    kernel_test_check(test, status == SYNCHRONIZATION_STATUS(TYPE_MISMATCH), "Wait on an Integer is refused");
    synchronization_close(test);
    if (!synchronization_run_revision(test, 1u))
        return;
    kernel_test_check(
        test, synchronization_evaluate("\\RSTE", &status) == 0xFFFFFFFFu && status == SYNCHRONIZATION_STATUS(OK),
        "under revision 1, the timeout's True is 0xFFFFFFFF");
    synchronization_close(test);
}

/**
 * @brief A Serialized method raises the sync level to its own while it runs: called below the current
 *        level it fails with mutex_order; it may call itself; inside one of level 3, an Acquire of a
 *        mutex of level 2 is ignored, and once it returns the level it found is back (§5.2).
 */
KERNEL_TEST(a_serialized_method_raises_the_sync_level_while_it_runs)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status = SYNCHRONIZATION_STATUS(OK);

    if (!synchronization_run(test))
        return;
    (void) synchronization_evaluate("\\SERL", &status);
    kernel_test_check(test, status == SYNCHRONIZATION_STATUS(MUTEX_ORDER),
                      "SER0, of level 0, called while MTA1 holds level 1, fails with mutex_order");
    kernel_test_check(test, synchronization_evaluate("\\SER0", &status) == 1u && status == SYNCHRONIZATION_STATUS(OK),
                      "SER0 called at level 0 runs");
    kernel_test_check(test,
                      synchronization_evaluate("\\SR3_", &status) == SYNCHRONIZATION_ONES &&
                          status == SYNCHRONIZATION_STATUS(OK) &&
                          synchronization_load.services.acquisitions_ignored == 1u,
                      "SER3 calls itself twice, then its Acquire of MTX2, of level 2, is ignored and gives Ones");
    kernel_test_check(test, synchronization_evaluate("\\AFTR", &status) == 0u && status == SYNCHRONIZATION_STATUS(OK),
                      "once SER3 has returned, the level is back to 0 and MTX2 is acquired");
    synchronization_close(test);
}
