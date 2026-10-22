#include <kernel/cpu/pci.h>
#include <kernel/hal/hal.h>
#include <kernel/testing/test.h>

#include <stddef.h>

/** The virtio vendor identifier. */
#define VIRTIO_GPU_TEST_VENDOR 0x1AF4u

/** PCI class of a display controller. */
#define VIRTIO_GPU_TEST_DISPLAY_CLASS 0x03u

/**
 * @brief The first virtio function of the display class on the bus, or NULL.
 */
static const PeripheralComponentInterconnectDevice_t *virtio_gpu_test_find(void)
{
    const uint32_t count = peripheral_component_interconnect_get_device_count();

    for (uint32_t i = 0u; i < count; ++i)
    {
        const PeripheralComponentInterconnectDevice_t *const device = peripheral_component_interconnect_get_device(i);

        if (device != NULL && device->vendor_id == VIRTIO_GPU_TEST_VENDOR &&
            device->class_code == VIRTIO_GPU_TEST_DISPLAY_CLASS)
            return device;
    }
    return NULL;
}

KERNEL_TEST_SUITE(virtio_gpu, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The GPU this kernel polls asserts no pin interrupt, and masters the bus for its queues.
 *
 * @details The driver polls its queues and never reads the ISR status that de-asserts the line.
 *          With its pin interrupt on, the line stays asserted after the first completion, and a
 *          driver sharing it never gets out of its handler: under QEMU the HDA controller shares
 *          IRQ 11 with the GPU, and a boot with both hung in the power floor session (#525).
 */
KERNEL_TEST(a_polled_gpu_keeps_its_interrupt_line_quiet)
{
    const PeripheralComponentInterconnectDevice_t *const device = virtio_gpu_test_find();

    if (device == NULL)
    {
        kernel_test_skip(test, "no virtio GPU on the bus");
        return;
    }
    if (!hardware_abstraction_layer_virtio_gpu_display_active())
    {
        kernel_test_skip(test, "the virtio GPU was not brought up as the display");
        return;
    }

    const uint16_t command = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);

    kernel_test_check(test, (command & PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_INTERRUPT_DISABLE) != 0u,
                      "its pin interrupt is off");
    kernel_test_check(test, (command & PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER) != 0u,
                      "it may master the bus, which its queues need");
    kernel_test_measure_hexadecimal(test, "command", command);
}
