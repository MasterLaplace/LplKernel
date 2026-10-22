#include <kernel/cpu/pci.h>
#include <kernel/drivers/nvme.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(non_volatile_memory_express, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The controller comes out of reset with its admin queues, and identifies itself and namespace 1.
 *
 * @details Skipped on a machine with no NVM Express controller. A boot that attaches one names this
 *          test on `lpl.test.require=`, so a skip there fails.
 */
KERNEL_TEST(the_controller_comes_up_and_identifies_its_namespace)
{
    const bool identified = non_volatile_memory_express_initialize();
    const NonVolatileMemoryExpressState_t *const state = non_volatile_memory_express_state();

    if (!state->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return;
    }

    kernel_test_check(test, state->controller_ready, "the controller came out of reset with its admin queues");
    kernel_test_check(test, identified && state->identified, "it answered Identify for itself and for namespace 1");
    kernel_test_check(test, state->version >= 0x00010000u, "its version is 1.0 or later");
    kernel_test_check(test, state->namespace_count >= 1u, "it has at least one namespace");
    kernel_test_check(test, state->namespace_blocks > 0u && state->block_bytes >= 512u,
                      "namespace 1 has blocks of at least 512 bytes");
    kernel_test_check(test, state->serial_number[0] != '\0' && state->model_number[0] != '\0',
                      "it gave a serial number and a model number");
    if (state->failure != NULL)
        kernel_test_note(test, state->failure);

    kernel_test_measure_hexadecimal(test, "version", state->version);
    kernel_test_measure(test, "maximum_queue_entries", state->maximum_queue_entries);
    kernel_test_measure(test, "doorbell_stride_bytes", state->doorbell_stride_bytes);
    kernel_test_measure(test, "ready_timeout_milliseconds", state->ready_timeout_milliseconds);
    kernel_test_measure(test, "namespace_count", state->namespace_count);
    kernel_test_measure(test, "namespace_blocks", (uint32_t) state->namespace_blocks);
    kernel_test_measure(test, "block_bytes", state->block_bytes);
}

/**
 * @brief A second bring-up stops the running controller, and starts it again with fresh admin queues.
 *
 * @details Whatever the firmware left, this bring-up finds the controller running, with queue
 *          positions the controller remembers, so it checks that the reset makes both sides start over.
 *          SeaBIOS leaves it running too, with its own admin queues: under QEMU the first bring-up
 *          already depends on the reset.
 */
KERNEL_TEST(a_second_bring_up_resets_the_running_controller)
{
    if (!non_volatile_memory_express_state()->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return;
    }

    const bool first = non_volatile_memory_express_initialize();
    const bool second = non_volatile_memory_express_initialize();

    kernel_test_check(test, first, "the first bring-up identifies the controller");
    kernel_test_check(test, second && non_volatile_memory_express_state()->identified,
                      "and so does a second one, from a running controller");
    if (non_volatile_memory_express_state()->failure != NULL)
        kernel_test_note(test, non_volatile_memory_express_state()->failure);
}

/**
 * @brief The bring-up turns bus mastering on itself, whatever the firmware left.
 *
 * @details Firmware that boots from NVM Express turns bus mastering on, so the other tests pass
 *          whether the driver does or not. This one turns it off first: without it the controller
 *          never reads its queues, and Identify times out.
 */
KERNEL_TEST(the_bring_up_turns_bus_mastering_on_itself)
{
    const PeripheralComponentInterconnectDevice_t *const device = peripheral_component_interconnect_find_by_class(
        NON_VOLATILE_MEMORY_EXPRESS_CLASS, NON_VOLATILE_MEMORY_EXPRESS_SUBCLASS);

    if (device == NULL || !non_volatile_memory_express_state()->controller_present)
    {
        kernel_test_skip(test, "no NVM Express controller on the bus");
        return;
    }

    const uint16_t command = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);
    peripheral_component_interconnect_config_write_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND,
        (uint16_t) (command & ~PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER));

    const bool identified = non_volatile_memory_express_initialize();
    const uint16_t after = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);

    kernel_test_check(test, (after & PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER) != 0u,
                      "bus mastering is on again after the bring-up");
    kernel_test_check(test, identified, "and the controller answered Identify through its queues");
    if (non_volatile_memory_express_state()->failure != NULL)
        kernel_test_note(test, non_volatile_memory_express_state()->failure);
}
