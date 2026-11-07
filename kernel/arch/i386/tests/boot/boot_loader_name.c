#include <kernel/boot/boot_module.h>
#include <kernel/cpu/paging.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(boot_loader_name, KERNEL_TEST_STAGE_BOOTED);

#define BOOT_LOADER_NAME_TEST_FLAG     (1u << 9)   /**< The multiboot flag of a boot loader name. */
#define BOOT_LOADER_NAME_TEST_MEBIBYTE 0x00100000u /**< The end of the first MiB. */

extern MultibootInfo_t *multiboot_info;

static const char boot_loader_name_test_source[] = "made-up loader"; /**< A name the kernel image holds, past 1 MiB. */

/**
 * @brief The boot loader's name is found wherever the boot map reaches it, past the first MiB too, where
 *        QEMU's `-kernel` leaves it; a block without a name, or with one past the map, has none.
 */
KERNEL_TEST(the_name_is_found_wherever_the_boot_map_reaches_it)
{
    const uint32_t physical = (uint32_t) (uintptr_t) boot_loader_name_test_source - KERNEL_VIRTUAL_BASE;
    MultibootInfo_t info = {0};

    info.flags = BOOT_LOADER_NAME_TEST_FLAG;
    info.boot_loader_name = physical;
    kernel_test_check(test, physical >= BOOT_LOADER_NAME_TEST_MEBIBYTE,
                      "the made-up name lies past the first MiB, as QEMU's does");
    kernel_test_check(test, boot_loader_name_source(&info) == boot_loader_name_test_source,
                      "a name past the first MiB is found where it lies");
    info.boot_loader_name = KERNEL_BOOT_MAPPED_END;
    kernel_test_check(test, boot_loader_name_source(&info) == NULL, "a name past the boot map is not reached");
    info.boot_loader_name = physical;
    info.flags = 0u;
    kernel_test_check(test, boot_loader_name_source(&info) == NULL, "a block whose flags hold no name has none");
    kernel_test_check(test, boot_loader_name_source(NULL) == NULL, "no block has no name");
}

/**
 * @brief The block this kernel booted with gives its boot loader's name to the boot report whenever it holds
 *        one, wherever the boot loader left it.
 */
KERNEL_TEST(the_booted_block_s_name_reaches_the_report)
{
    if (!multiboot_info || !(multiboot_info->flags & BOOT_LOADER_NAME_TEST_FLAG))
    {
        kernel_test_skip(test, "the boot loader gave no name");
        return;
    }
    kernel_test_check(test, boot_loader_name_source(multiboot_info) != NULL,
                      "the name the boot loader gave is reached where it lies");
}
