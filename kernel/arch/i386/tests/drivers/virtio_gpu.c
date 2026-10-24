#include <kernel/cpu/pci.h>
#include <kernel/hal/hal.h>
#include <kernel/memory/vmm.h>
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

/**
 * @brief Does a decoded window start at or past 4 GiB, beyond the physical space this kernel maps?
 */
static bool virtio_gpu_test_lies_above_4_gib(const PeripheralComponentInterconnectBaseAddressRegister_t *window)
{
    return (window->base >> 32) != 0u;
}

/**
 * @brief Measures a decoded window's slot and the two halves of its base, which one measure cannot hold.
 */
static void virtio_gpu_test_measure_window(KernelTest_t *test,
                                           const PeripheralComponentInterconnectBaseAddressRegister_t *window)
{
    kernel_test_measure(test, "window_index", window->index);
    kernel_test_measure_hexadecimal(test, "window_base_high", (uint32_t) (window->base >> 32));
    kernel_test_measure_hexadecimal(test, "window_base_low", (uint32_t) window->base);
}

/**
 * @brief Finds the first memory base address register of the probed function, read apart from the driver.
 *
 * @return false when the function has none.
 */
static bool virtio_gpu_test_first_memory_window(const hardware_abstraction_layer_virtio_gpu_info_t *info,
                                                PeripheralComponentInterconnectBaseAddressRegister_t *out_window)
{
    for (uint8_t index = 0u; index < PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT; ++index)
    {
        if (!peripheral_component_interconnect_read_base_address_register(info->bus, info->device, info->function,
                                                                          index, out_window))
            continue;
        if (!out_window->is_io)
            return true;
        if (out_window->is_64bit)
            ++index;
    }
    return false;
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

/**
 * @brief The register window is mapped below 4 GiB, and refused above it rather than cut to its low half.
 *
 * @details This kernel maps a 32-bit physical space. A window placed above 4 GiB and cut to its low
 *          32 bits maps whatever lies at that low address, uncached and writable, and the device
 *          handshake then writes into it (#569). Under QEMU, a 2 GiB and a 1 GiB 64-bit BAR beside the
 *          GPU make SeaBIOS move every 64-bit BAR above 4 GiB, the GPU's to 0xF_C000_0000:
 *          `-object memory-backend-ram,id=big,size=2G,share=on -device ivshmem-plain,memdev=big
 *          -object memory-backend-ram,id=mid,size=1G,share=on -device ivshmem-plain,memdev=mid`.
 *          With the 2 GiB one alone it lands at 0x10_0000_0000, whose low half is 0, which
 *          kernel_vmm_map_device_window refuses for a reason of its own. Below 4 GiB the same test
 *          checks that the window is still mapped where its register says.
 */
KERNEL_TEST(the_register_window_is_mapped_only_below_4_gib)
{
    hardware_abstraction_layer_virtio_gpu_info_t info;
    hardware_abstraction_layer_virtio_gpu_mapping_t mapping;
    PeripheralComponentInterconnectBaseAddressRegister_t window;

    if (!hardware_abstraction_layer_virtio_gpu_probe(&info))
    {
        kernel_test_skip(test, "no virtio GPU on the bus");
        return;
    }

    const bool mapped = hardware_abstraction_layer_virtio_gpu_map(&info, &mapping);

    if (!mapping.common.present ||
        !peripheral_component_interconnect_read_base_address_register(info.bus, info.device, info.function,
                                                                      mapping.common.bar, &window) ||
        window.is_io)
    {
        kernel_test_skip(test, "the virtio GPU names no memory window for its common configuration");
        return;
    }

    virtio_gpu_test_measure_window(test, &window);
    if (mapped)
    {
        kernel_test_measure_hexadecimal(test, "mapped_physical_base", mapping.mmio_physical_base);
        kernel_vmm_unmap_device_window(mapping.mmio_virtual_base, mapping.mmio_size);
    }

    if (virtio_gpu_test_lies_above_4_gib(&window))
    {
        kernel_test_check(test, !mapped, "a window above 4 GiB is refused");
        kernel_test_check(test, mapping.mapped == 0u && mapping.mmio_virtual_base == 0u,
                          "and the mapping records no window");
        kernel_test_check(test, mapping.window_out_of_reach != 0u, "and the driver says it is out of reach");
        kernel_test_check(test, hardware_abstraction_layer_virtio_gpu_display_window_out_of_reach(),
                          "and so did the bring-up at boot, which left the display off");
    }
    else
    {
        kernel_test_check(test, mapped, "a window below 4 GiB is mapped");
        kernel_test_check(test, mapping.mmio_physical_base == (uint32_t) window.base, "at the base its register gives");
        kernel_test_check(test, mapping.window_out_of_reach == 0u, "and is not said to be out of reach");
        kernel_test_check(test, !hardware_abstraction_layer_virtio_gpu_display_window_out_of_reach(),
                          "nor was it at boot");
    }
}

/**
 * @brief The probe records its first memory window whole below 4 GiB, and none rather than its low half above.
 *
 * @details Under QEMU the first memory window of a virtio GPU is its MSI-X table, a 32-bit BAR.
 *          `-device virtio-gpu-pci,vectors=0` drops it, and the first becomes the 64-bit BAR that
 *          holds the configuration, which the setup of the test above places above 4 GiB.
 */
KERNEL_TEST(the_probe_records_a_memory_window_only_below_4_gib)
{
    hardware_abstraction_layer_virtio_gpu_info_t info;
    PeripheralComponentInterconnectBaseAddressRegister_t window;

    if (!hardware_abstraction_layer_virtio_gpu_probe(&info))
    {
        kernel_test_skip(test, "no virtio GPU on the bus");
        return;
    }
    if (!virtio_gpu_test_first_memory_window(&info, &window))
    {
        kernel_test_skip(test, "the virtio GPU has no memory window");
        return;
    }

    virtio_gpu_test_measure_window(test, &window);
    kernel_test_measure_hexadecimal(test, "recorded_base", info.mmio_base);
    kernel_test_measure_hexadecimal(test, "recorded_size", info.mmio_size);

    if (virtio_gpu_test_lies_above_4_gib(&window))
    {
        kernel_test_check(test, info.mmio_base == 0u && info.mmio_size == 0u,
                          "a first window above 4 GiB is recorded as none, not as its low half");
    }
    else
    {
        kernel_test_check(test, info.mmio_base == (uint32_t) window.base && info.mmio_size == (uint32_t) window.size,
                          "a first window below 4 GiB is recorded whole");
        kernel_test_check(test, info.mmio_bar_index == window.index, "with its slot");
    }
}
