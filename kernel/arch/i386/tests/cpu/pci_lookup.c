#include <kernel/cpu/pci.h>
#include <kernel/testing/test.h>

#include <stddef.h>

KERNEL_TEST_SUITE(peripheral_component_interconnect_lookup, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief A lookup by vendor and a list of device IDs finds the first device the bus lists with them,
 *        and nothing for an empty list or for IDs no device has.
 *
 * @details The device looked for is the bus's first one, whatever it is, so the test holds on any machine
 *          whose scan found a device; the IDs no device has are the ones a missing function reads back.
 */
KERNEL_TEST(a_lookup_by_identifiers_finds_the_first_device_that_has_them)
{
    const PeripheralComponentInterconnectDevice_t *const first = peripheral_component_interconnect_get_device(0u);

    if (first == NULL)
    {
        kernel_test_skip(test, "the scan found no device");
        return;
    }

    const uint16_t wanted[] = {PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID, first->device_id};
    const uint16_t none[] = {PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID};
    const PeripheralComponentInterconnectDevice_t *const found =
        peripheral_component_interconnect_find_by_identifiers(first->vendor_id, wanted, 2u);

    kernel_test_check(test, found == first,
                      "the bus's first device is found by its vendor and its ID, second in the list");
    kernel_test_check(test, peripheral_component_interconnect_find_by_identifiers(first->vendor_id, wanted, 0u) == NULL,
                      "an empty list finds nothing");
    kernel_test_check(test, peripheral_component_interconnect_find_by_identifiers(first->vendor_id, none, 1u) == NULL,
                      "an ID no device has finds nothing");
    kernel_test_check(test,
                      peripheral_component_interconnect_find_by_identifiers(
                          PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID, wanted, 2u) == NULL,
                      "nor does a vendor no device has");
}
