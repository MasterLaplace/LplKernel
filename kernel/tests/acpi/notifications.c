#include "fixtures/notifications.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_notifications, KERNEL_TEST_STAGE_INITIALIZATION);

#define NOTIFICATIONS_TABLE_CAPACITY 512u /**< Bytes of the buffer a made-up DSDT is written in. */
#define NOTIFICATIONS_STATUS(name)   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name

static uint8_t notifications_table[NOTIFICATIONS_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t notifications_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t notifications_load;

/**
 * @brief Loads the table as a DSDT of revision 2 on a fresh simulated host.
 */
static bool notifications_run(KernelTest_t *test)
{
    return made_up_load_table(test, &notifications_load, &notifications_host, notifications_table, 2u,
                              notifications_aml, sizeof(notifications_aml));
}

/**
 * @brief Evaluates the method at @p path, which takes no argument, and gives its status.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t notifications_evaluate(const char *path)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&notifications_load, path, NULL, 0u,
                                                                             &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&notifications_load.heap,
                                                                              &notifications_load.space, &result);
    return status;
}

/**
 * @brief Whether the next Notify the ring gives is of @p value to the root's child @p name.
 */
static bool notifications_next_is(const char *name, uint32_t value)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        made_up_root_node(&notifications_load, name);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t notification;

    return advanced_configuration_and_power_interface_machine_language_take_notification(&notifications_load,
                                                                                         &notification) &&
           node != NULL &&
           advanced_configuration_and_power_interface_machine_language_namespace_node(&notifications_load.space,
                                                                                      notification.node) == node &&
           notification.value == value;
}

/**
 * @brief Whether the ring gives nothing more.
 */
static bool notifications_ring_is_empty(void)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNotification_t notification;

    return !advanced_configuration_and_power_interface_machine_language_take_notification(&notifications_load,
                                                                                          &notification);
}

/**
 * @brief Closes the load and gives the simulated memory back.
 */
static void notifications_close(void)
{
    advanced_configuration_and_power_interface_machine_language_close(&notifications_load);
    advanced_configuration_and_power_interface_simulated_host_close(&notifications_host);
}

/**
 * @brief A Notify waits in the ring until its evaluation is over, then the ring gives the
 *        notifications in the order they were raised: the table's own, then a device's, a processor's
 *        and a thermal zone's, and one made through a reference in a local (§3.7).
 */
KERNEL_TEST(a_notify_waits_in_the_ring_and_comes_out_in_order)
{
    if (!notifications_run(test))
        return;
    kernel_test_check(test, notifications_next_is("DEV0", 2u) && notifications_ring_is_empty(),
                      "the table's code queued one Notify of DEV0");
    kernel_test_check(test, notifications_evaluate("\\NTF3") == NOTIFICATIONS_STATUS(OK), "NTF3 runs");
    kernel_test_check(test, notifications_evaluate("\\NTFL") == NOTIFICATIONS_STATUS(OK), "NTFL runs");
    kernel_test_check(test,
                      notifications_next_is("DEV0", 0x80u) && notifications_next_is("CPU0", 0x81u) &&
                          notifications_next_is("TZ00", 0x82u) && notifications_next_is("DEV0", 0x90u) &&
                          notifications_ring_is_empty(),
                      "the ring gives DEV0, CPU0, TZ00, then DEV0 again through Local0, and nothing more");
    kernel_test_check(test,
                      notifications_load.services.notifications == 5u &&
                          notifications_load.services.notifications_taken == 5u &&
                          notifications_load.services.notifications_dropped == 0u,
                      "five queued, five taken, none dropped");
    notifications_close();
}

/**
 * @brief The ring holds 64 notifications: a 65th is dropped, counted, and the 64 kept come out
 *        first in, first out (§6.1).
 */
KERNEL_TEST(the_ring_drops_the_65th_notify)
{
    bool in_order = true;

    if (!notifications_run(test))
        return;
    kernel_test_check(test, notifications_next_is("DEV0", 2u), "the table's own Notify is taken first");
    kernel_test_check(test, notifications_evaluate("\\NTFM") == NOTIFICATIONS_STATUS(OK),
                      "NTFM runs: a full ring never fails the evaluation");
    for (uint32_t value = 0u; value < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NOTIFICATIONS;
         ++value)
        in_order = in_order && notifications_next_is("DEV0", value);
    kernel_test_check(test, in_order && notifications_ring_is_empty(), "the values 0 to 63 come out, in order");
    kernel_test_check(test,
                      notifications_load.services.notifications_dropped == 1u &&
                          made_up_limit_peak(&notifications_load, "notifications_peak") == 64u,
                      "the 65th is dropped and counted; the ring held 64 at most");
    notifications_close();
}

/**
 * @brief Notify of an object that is no device, processor or thermal zone fails with type_mismatch,
 *        and queues nothing.
 */
KERNEL_TEST(a_notify_of_another_object_is_refused)
{
    if (!notifications_run(test))
        return;
    kernel_test_check(test, notifications_next_is("DEV0", 2u), "the table's own Notify is taken");
    kernel_test_check(test, notifications_evaluate("\\NTFI") == NOTIFICATIONS_STATUS(TYPE_MISMATCH),
                      "an Integer is refused");
    kernel_test_check(test, notifications_evaluate("\\NTFP") == NOTIFICATIONS_STATUS(TYPE_MISMATCH),
                      "a power resource is refused");
    kernel_test_check(test, notifications_ring_is_empty() && notifications_load.services.notifications == 1u,
                      "nothing more was queued");
    notifications_close();
}

/**
 * @brief Dropping the ring takes every Notify it holds and acts on none, each counted as taken, as
 *        the boot does with what the tables' code raised (§3.8 step 9).
 */
KERNEL_TEST(dropping_the_ring_takes_every_notify)
{
    if (!notifications_run(test))
        return;
    kernel_test_check(test, notifications_evaluate("\\NTFM") == NOTIFICATIONS_STATUS(OK), "NTFM runs");
    advanced_configuration_and_power_interface_machine_language_drop_notifications(&notifications_load);
    kernel_test_check(test, notifications_ring_is_empty() && notifications_load.services.notifications_taken == 64u,
                      "the 64 the ring held, the table's own among them, are taken");
    notifications_close();
}
