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
 * @brief A device DEV0, a processor CPU0, a thermal zone TZ00, a power resource PWR0 and an Integer
 *        INT0; the table's code notifies DEV0 with 2; NTF3 notifies DEV0, CPU0 and TZ00 with 0x80 to
 *        0x82, NTFI notifies INT0, NTFP notifies PWR0, NTFL notifies DEV0 with 0x90 through a reference
 *        held in Local0, and NTFM notifies DEV0 65 times, with 0 to 64.
 */
static const uint8_t notifications_aml[] = {
    0x5Bu, 0x82u, 0x05u, 'D',   'E',   'V',   '0',   0x5Bu, 0x83u, 0x0Bu, 'C',   'P',   'U',   '0',   0x01u,
    0x10u, 0x08u, 0x00u, 0x00u, 0x06u, 0x5Bu, 0x85u, 0x05u, 'T',   'Z',   '0',   '0',   0x5Bu, 0x84u, 0x08u,
    'P',   'W',   'R',   '0',   0x00u, 0x00u, 0x00u, 0x08u, 'I',   'N',   'T',   '0',   0x0Au, 0x01u, 0x86u,
    'D',   'E',   'V',   '0',   0x0Au, 0x02u, 0x14u, 0x1Bu, 'N',   'T',   'F',   '3',   0x00u, 0x86u, 'D',
    'E',   'V',   '0',   0x0Au, 0x80u, 0x86u, 'C',   'P',   'U',   '0',   0x0Au, 0x81u, 0x86u, 'T',   'Z',
    '0',   '0',   0x0Au, 0x82u, 0x14u, 0x0Du, 'N',   'T',   'F',   'I',   0x00u, 0x86u, 'I',   'N',   'T',
    '0',   0x0Au, 0x80u, 0x14u, 0x0Du, 'N',   'T',   'F',   'P',   0x00u, 0x86u, 'P',   'W',   'R',   '0',
    0x0Au, 0x80u, 0x14u, 0x11u, 'N',   'T',   'F',   'L',   0x00u, 0x70u, 0x71u, 'D',   'E',   'V',   '0',
    0x60u, 0x86u, 0x60u, 0x0Au, 0x90u, 0x14u, 0x18u, 'N',   'T',   'F',   'M',   0x00u, 0x70u, 0x0Au, 0x00u,
    0x60u, 0xA2u, 0x0Du, 0x95u, 0x60u, 0x0Au, 0x41u, 0x86u, 'D',   'E',   'V',   '0',   0x60u, 0x75u, 0x60u,
};

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
                          notifications_load.services.notifications_delivered == 5u &&
                          notifications_load.services.notifications_dropped == 0u,
                      "five queued, five delivered, none dropped");
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
                          notifications_load.services.notification_peak == 64u,
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
 * @brief Dropping the ring takes every Notify it holds and acts on none, each counted as delivered, as
 *        the boot does with what the tables' code raised (§3.8 step 9).
 */
KERNEL_TEST(dropping_the_ring_takes_every_notify)
{
    if (!notifications_run(test))
        return;
    kernel_test_check(test, notifications_evaluate("\\NTFM") == NOTIFICATIONS_STATUS(OK), "NTFM runs");
    advanced_configuration_and_power_interface_machine_language_drop_notifications(&notifications_load);
    kernel_test_check(test, notifications_ring_is_empty() && notifications_load.services.notifications_delivered == 64u,
                      "the 64 the ring held, the table's own among them, are taken");
    notifications_close();
}
