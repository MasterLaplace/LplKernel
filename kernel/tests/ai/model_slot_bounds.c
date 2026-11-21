#include <kernel/ai/model_slot.h>
#include <kernel/cpu/paging.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(model_slot_bounds, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief An image is taken only when it lies wholly inside the 64 MiB boot.S maps: one ending on the map's last byte
 *        is, one byte longer, one starting past the map, one below the higher half, or one whose end would pass
 *        4 GiB is not.
 *
 * @details Past the map the physical memory manager maps no page of a boot module, so a read there faults: the slot
 *          names such an image unreachable before reading a byte of it.
 */
KERNEL_TEST(an_image_is_taken_only_inside_the_boot_map)
{
    const uint32_t map_end = KERNEL_VIRTUAL_BASE + KERNEL_BOOT_MAPPED_END;
    const uint32_t mebibyte = 0x00100000u;

    kernel_test_check(test, kernel_model_slot_is_mapped_at_boot(map_end - mebibyte, mebibyte),
                      "a MiB ending on the map's last byte");
    kernel_test_check(test, !kernel_model_slot_is_mapped_at_boot(map_end - mebibyte, mebibyte + 1u), "one byte longer");
    kernel_test_check(test, !kernel_model_slot_is_mapped_at_boot(map_end, 1u), "a byte past the map");
    kernel_test_check(test, !kernel_model_slot_is_mapped_at_boot(KERNEL_VIRTUAL_BASE - PAGE_SIZE, PAGE_SIZE),
                      "a page below the higher half");
    kernel_test_check(test, !kernel_model_slot_is_mapped_at_boot(0xFFFFF000u, 0x2000u), "an end past 4 GiB");
}
