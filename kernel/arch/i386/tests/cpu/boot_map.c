#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(boot_map, KERNEL_TEST_STAGE_INITIALIZATION);

/** The multiboot flag that says the memory map fields are valid. */
#define BOOT_MAP_TEST_MEMORY_MAP_FLAG (1u << 6)

/** The first byte past the first mebibyte, which the boot map keeps whatever the memory map says. */
#define BOOT_MAP_TEST_FIRST_MEBIBYTE_END 0x00100000u

/** The top page below 4 GiB, the firmware's on every PC, past the kernel's map of physical memory. */
#define BOOT_MAP_TEST_TOP_PAGE 0xFFFFF000u

extern MultibootInfo_t *multiboot_info;

/**
 * @brief The memory map the bootloader handed over, or NULL when it gave none.
 */
static const uint8_t *boot_map_test_memory_map(uint32_t *out_length)
{
    if (!multiboot_info || !(multiboot_info->flags & BOOT_MAP_TEST_MEMORY_MAP_FLAG) || !multiboot_info->mmap_addr ||
        !multiboot_info->mmap_length)
        return NULL;
    *out_length = multiboot_info->mmap_length;
    return (const uint8_t *) (uintptr_t) ((uint32_t) (uintptr_t) multiboot_info->mmap_addr + KERNEL_VIRTUAL_BASE);
}

/**
 * @brief Whether one usable RAM entry (type 1) of the memory map holds the whole page at @p page.
 */
static bool boot_map_test_page_is_ram(uint32_t page)
{
    uint32_t length = 0u;
    const uint8_t *const map = boot_map_test_memory_map(&length);

    for (uint32_t offset = 0u; map && offset + sizeof(uint32_t) <= length;)
    {
        const MemoryMapEntry_t *const entry = (const MemoryMapEntry_t *) (map + offset);
        const uint64_t end = entry->base_addr + entry->length;

        if (entry->type == 1u && entry->base_addr <= page && end >= (uint64_t) page + PAGE_SIZE)
            return true;
        offset += sizeof(uint32_t) + entry->size;
    }
    return false;
}

/**
 * @brief The PAT, PCD and PWT bits of @p entry, which select its memory type.
 */
static uint32_t boot_map_test_memory_type_bits(const PageTableEntry_t *entry)
{
    return ((uint32_t) entry->page_attribute_table << 2u) | ((uint32_t) entry->cache_disable << 1u) |
           entry->write_through;
}

/**
 * @brief Whether the boot map maps the frame @p page at its higher-half address, and if so its entry.
 */
static bool boot_map_test_direct_entry(uint32_t page, PageTableEntry_t *out_entry)
{
    return paging_get_page_table_entry(page + KERNEL_VIRTUAL_BASE, out_entry) &&
           ((uint32_t) out_entry->page_frame_base << 12u) == page;
}

/**
 * @brief A device window on a page below 64 MiB that the memory map does not call RAM never sits beside a
 *        mapping of that page with another memory type in the boot map.
 *
 * @details Intel SDM vol. 3, §14.12.4: a page mapped with two memory types is undefined behaviour. The boot
 *          map used to cover 0 to 64 MiB write-back, so the uncached window AML takes on a reserved page there
 *          (5 corpus machines hold such regions below 1 MiB) was such an alias.
 */
KERNEL_TEST(a_device_window_gives_no_page_below_64_mib_a_second_memory_type)
{
    uint32_t length = 0u;

    if (!boot_map_test_memory_map(&length))
    {
        kernel_test_skip(test, "the bootloader gave no memory map");
        return;
    }

    uint32_t checked = 0u;
    uint32_t agreeing = 0u;
    uint32_t refused = 0u;

    for (uint32_t page = PAGE_SIZE; page < KERNEL_BOOT_MAPPED_END; page += PAGE_SIZE)
    {
        if (boot_map_test_page_is_ram(page))
            continue;

        const uint32_t window = kernel_vmm_map_device_window(page, PAGE_SIZE);
        PageTableEntry_t window_entry;
        PageTableEntry_t direct_entry;

        if (window == 0u || !paging_get_page_table_entry(window, &window_entry))
        {
            ++refused;
            continue;
        }
        ++checked;
        if (!boot_map_test_direct_entry(page, &direct_entry) ||
            boot_map_test_memory_type_bits(&direct_entry) == boot_map_test_memory_type_bits(&window_entry))
            ++agreeing;
        kernel_vmm_unmap_device_window(window, PAGE_SIZE);
    }

    kernel_test_measure(test, "pages_checked", checked);
    kernel_test_measure(test, "pages_agreeing", agreeing);
    kernel_test_check(test, checked != 0u, "the memory map leaves pages below 64 MiB outside RAM");
    kernel_test_check(test, refused == 0u, "a window is mapped on each of them");
    kernel_test_check(test, agreeing == checked,
                      "the boot map maps none of them with a memory type other than its device window's");
}

/**
 * @brief The boot map keeps the first mebibyte, which the kernel reads directly (VGA text, the BIOS data area,
 *        the BIOS area the root pointer is scanned in, the processor trampoline), and past it only the pages the
 *        memory map calls RAM, as Linux maps only RAM past the ISA range.
 */
KERNEL_TEST(the_boot_map_keeps_the_first_mebibyte_and_only_memory_past_it)
{
    uint32_t length = 0u;

    if (!boot_map_test_memory_map(&length))
    {
        kernel_test_skip(test, "the bootloader gave no memory map");
        return;
    }

    uint32_t kept = 0u;
    uint32_t dropped = 0u;
    uint32_t wrong = 0u;
    uint32_t first_wrong = 0u;

    for (uint32_t page = 0u; page < KERNEL_BOOT_MAPPED_END; page += PAGE_SIZE)
    {
        PageTableEntry_t entry;
        const bool expected = page < BOOT_MAP_TEST_FIRST_MEBIBYTE_END || boot_map_test_page_is_ram(page);
        const bool mapped = boot_map_test_direct_entry(page, &entry);

        if (mapped)
            ++kept;
        else
            ++dropped;
        if (mapped != expected && wrong++ == 0u)
            first_wrong = page;
    }

    kernel_test_measure(test, "pages_kept", kept);
    kernel_test_measure(test, "pages_dropped", dropped);
    kernel_test_measure_hexadecimal(test, "first_wrong_page", first_wrong);
    kernel_test_check(test, wrong == 0u,
                      "every page below 64 MiB is mapped exactly when it lies in the first MiB or in RAM");
}

/**
 * @brief A device window on a page the kernel's map of physical memory does not map is uncached: PCD and PWT
 *        select PAT entry 3, UC.
 */
KERNEL_TEST(a_device_window_past_the_kernels_map_is_uncached)
{
    const uint32_t window = kernel_vmm_map_device_window(BOOT_MAP_TEST_TOP_PAGE, PAGE_SIZE);
    PageTableEntry_t entry;
    const bool mapped = window != 0u && paging_get_page_table_entry(window, &entry);

    kernel_test_check(test, mapped, "a window is mapped on the top page below 4 GiB");
    if (!mapped)
        return;
    kernel_test_check(test, entry.cache_disable == 1u && entry.write_through == 1u && entry.page_attribute_table == 0u,
                      "it is uncached");
    kernel_vmm_unmap_device_window(window, PAGE_SIZE);
}
