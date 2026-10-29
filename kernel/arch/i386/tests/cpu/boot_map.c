#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(boot_map, KERNEL_TEST_STAGE_INITIALIZATION);

#define BOOT_MAP_TEST_MEMORY_MAP_FLAG    (1u << 6) /**< The multiboot flag that says the memory map fields are valid. */
#define BOOT_MAP_TEST_ENTRY_SIZE_LEAST   20u       /**< The smallest entry size the kernel reads a map past. */
#define BOOT_MAP_TEST_FIRST_MEBIBYTE_END 0x00100000u /**< The end of the first MiB, which the boot map keeps. */
#define BOOT_MAP_TEST_BOOT_MAP_END       0x04000000u /**< The end of what boot.S maps, 64 MiB. */
#define BOOT_MAP_TEST_TOP_PAGE           0xFFFFF000u /**< The top page below 4 GiB, the firmware's on every PC. */
#define BOOT_MAP_TEST_KERNEL_MAP_END                                                                                   \
    (KERNEL_VMM_DYNAMIC_START - KERNEL_VIRTUAL_BASE) /**< The end of the kernel's map of physical memory, 256 MiB. */

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
 * @brief Whether one usable RAM entry (type 1) of the Multiboot memory map @p map holds the whole page at @p page.
 *
 * @details Entries are read as Multiboot lays them out, each a size then that many bytes, up to the first one whose
 *          size is below 20 or that runs past @p length.
 */
static bool boot_map_test_map_holds_ram(const uint8_t *map, uint32_t length, uint32_t page)
{
    for (uint32_t offset = 0u; map && offset + sizeof(uint32_t) <= length;)
    {
        const MemoryMapEntry_t *const entry = (const MemoryMapEntry_t *) (map + offset);

        if (entry->size < BOOT_MAP_TEST_ENTRY_SIZE_LEAST || offset + sizeof(uint32_t) + entry->size > length)
            return false;
        if (entry->type == 1u && entry->base_addr <= page &&
            entry->base_addr + entry->length >= (uint64_t) page + PAGE_SIZE)
            return true;
        offset += sizeof(uint32_t) + entry->size;
    }
    return false;
}

/**
 * @brief Whether the bootloader's memory map calls the whole page at @p page usable RAM.
 */
static bool boot_map_test_page_is_ram(uint32_t page)
{
    uint32_t length = 0u;
    const uint8_t *const map = boot_map_test_memory_map(&length);

    return boot_map_test_map_holds_ram(map, length, page);
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
 * @brief Whether the kernel's map of physical memory maps the frame @p page at its higher-half address, and if so
 *        its entry.
 */
static bool boot_map_test_direct_entry(uint32_t page, PageTableEntry_t *out_entry)
{
    return paging_get_page_table_entry(page + KERNEL_VIRTUAL_BASE, out_entry) &&
           ((uint32_t) out_entry->page_frame_base << 12u) == page;
}

/**
 * @brief Maps a one-page device window on @p page and says whether it gives the page no second memory type: in the
 *        first MiB, which the kernel's map keeps, the window has that map's memory type bits; past it, the map
 *        holds no such page and the window is uncached, PCD and PWT set and PAT clear (PAT entry 3, UC).
 *
 * @return 1 when it agrees, 0 when it does not, -1 when no window could be mapped.
 */
static int boot_map_test_window_agrees(uint32_t page)
{
    const uint32_t window = kernel_vmm_map_device_window(page, PAGE_SIZE);
    PageTableEntry_t window_entry;
    PageTableEntry_t direct_entry;

    if (window == 0u || !paging_get_page_table_entry(window, &window_entry))
        return -1;

    const bool direct = boot_map_test_direct_entry(page, &direct_entry);
    const bool agrees =
        page < BOOT_MAP_TEST_FIRST_MEBIBYTE_END ?
            direct && boot_map_test_memory_type_bits(&direct_entry) == boot_map_test_memory_type_bits(&window_entry) :
            !direct && window_entry.cache_disable == 1u && window_entry.write_through == 1u &&
                window_entry.page_attribute_table == 0u;

    kernel_vmm_unmap_device_window(window, PAGE_SIZE);
    return agrees ? 1 : 0;
}

/**
 * @brief A device window on a page below 256 MiB that the memory map does not call RAM gives that page no second
 *        memory type beside the kernel's map of physical memory.
 *
 * @details Intel SDM vol. 3, §14.12.4: a page mapped with two memory types is undefined behaviour. The boot map used
 *          to cover 0 to 64 MiB write-back, so the uncached window AML takes on a reserved page there (5 corpus
 *          machines hold such regions below 1 MiB) was such an alias. Every such page of the first MiB is checked;
 *          past it, the first and the last page of each run of them, since the map holds them by runs.
 */
KERNEL_TEST(no_page_below_256_mib_gets_a_second_memory_type_from_a_device_window)
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
    uint32_t runs = 0u;
    uint32_t first_disagreeing = 0u;

    for (uint32_t page = PAGE_SIZE; page < BOOT_MAP_TEST_KERNEL_MAP_END; page += PAGE_SIZE)
    {
        if (boot_map_test_page_is_ram(page))
            continue;

        const bool run_starts = page <= BOOT_MAP_TEST_FIRST_MEBIBYTE_END || boot_map_test_page_is_ram(page - PAGE_SIZE);
        const bool run_ends =
            page + PAGE_SIZE >= BOOT_MAP_TEST_KERNEL_MAP_END || boot_map_test_page_is_ram(page + PAGE_SIZE);

        if (page >= BOOT_MAP_TEST_FIRST_MEBIBYTE_END && !run_starts && !run_ends)
            continue;
        if (page >= BOOT_MAP_TEST_FIRST_MEBIBYTE_END && run_starts)
            ++runs;

        const int agrees = boot_map_test_window_agrees(page);

        if (agrees < 0)
        {
            ++refused;
            continue;
        }
        ++checked;
        if (agrees)
            ++agreeing;
        else if (checked - agreeing == 1u)
            first_disagreeing = page;
    }

    kernel_test_measure(test, "pages_checked", checked);
    kernel_test_measure(test, "pages_agreeing", agreeing);
    kernel_test_measure(test, "runs_past_the_first_mebibyte", runs);
    kernel_test_measure_hexadecimal(test, "first_disagreeing_page", first_disagreeing);
    kernel_test_check(test, checked != 0u, "the memory map leaves pages below 256 MiB outside RAM");
    kernel_test_check(test, refused == 0u, "a window is mapped on each of them");
    kernel_test_check(test, agreeing == checked,
                      "in the first MiB each window has the map's memory type; past it the map holds none of them, "
                      "and each window is uncached");
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

    for (uint32_t page = 0u; page < BOOT_MAP_TEST_BOOT_MAP_END; page += PAGE_SIZE)
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
