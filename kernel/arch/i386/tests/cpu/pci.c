#include <kernel/cpu/pci.h>
#include <kernel/testing/test.h>

/**
 * @brief A memory Base Address Register decoded by hand, as the PCI layer would decode it.
 */
static PeripheralComponentInterconnectBaseAddressRegister_t
peripheral_component_interconnect_test_memory_window(uint64_t base, uint64_t size)
{
    return (PeripheralComponentInterconnectBaseAddressRegister_t){.is_64bit = 1u, .base = base, .size = size};
}

KERNEL_TEST_SUITE(peripheral_component_interconnect, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief A memory window is reachable when its base lies below 4 GiB, and an I/O window never is.
 *
 * @details This kernel maps a 32-bit physical space (#569). The windows are decoded by hand, so the
 *          decision is checked on every boot, with no device: a window at 0xF_C000_0000, whose low
 *          half a driver would otherwise map, and one at exactly 4 GiB are refused; one at 0xFE000000
 *          and the last page below 4 GiB are not.
 */
KERNEL_TEST(a_memory_window_is_reachable_only_below_4_gib)
{
    const PeripheralComponentInterconnectBaseAddressRegister_t io_window = {.is_io = 1u,
                                                                            .base = 0xC000u,
                                                                            .size = 0x40u};
    const PeripheralComponentInterconnectBaseAddressRegister_t high_window =
        peripheral_component_interconnect_test_memory_window(0xFC0000000ull, 0x4000u);
    const PeripheralComponentInterconnectBaseAddressRegister_t window_at_4_gib =
        peripheral_component_interconnect_test_memory_window(0x100000000ull, 0x4000u);
    const PeripheralComponentInterconnectBaseAddressRegister_t low_window =
        peripheral_component_interconnect_test_memory_window(0xFE000000u, 0x4000u);
    const PeripheralComponentInterconnectBaseAddressRegister_t last_page =
        peripheral_component_interconnect_test_memory_window(0xFFFFF000u, 0x1000u);

    kernel_test_check(test, !peripheral_component_interconnect_memory_window_is_reachable(&io_window),
                      "an I/O window is not a memory window this kernel maps");
    kernel_test_check(test, !peripheral_component_interconnect_memory_window_is_reachable(&high_window),
                      "a window at 0xF_C000_0000 is out of reach");
    kernel_test_check(test, !peripheral_component_interconnect_memory_window_is_reachable(&window_at_4_gib),
                      "a window at exactly 4 GiB is out of reach");
    kernel_test_check(test, peripheral_component_interconnect_memory_window_is_reachable(&low_window),
                      "a window at 0xFE000000 is reachable");
    kernel_test_check(test, peripheral_component_interconnect_memory_window_is_reachable(&last_page),
                      "the last page below 4 GiB is reachable");
}
