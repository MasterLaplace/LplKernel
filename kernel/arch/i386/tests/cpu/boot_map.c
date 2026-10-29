#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <string.h>

KERNEL_TEST_SUITE(boot_map, KERNEL_TEST_STAGE_INITIALIZATION);

#define BOOT_MAP_TEST_MEMORY_MAP_FLAG    (1u << 6) /**< The multiboot flag that says the memory map fields are valid. */
#define BOOT_MAP_TEST_ENTRY_SIZE_LEAST   20u       /**< The smallest entry size the kernel reads a map past. */
#define BOOT_MAP_TEST_FIRST_MEBIBYTE_END 0x00100000u /**< The end of the first MiB, which the boot map keeps. */
#define BOOT_MAP_TEST_BOOT_MAP_END       0x04000000u /**< The end of what boot.S maps, 64 MiB. */
#define BOOT_MAP_TEST_TOP_PAGE           0xFFFFF000u /**< The top page below 4 GiB, the firmware's on every PC. */
#define BOOT_MAP_TEST_KERNEL_MAP_END                                                                                   \
    (KERNEL_VMM_DYNAMIC_START - KERNEL_VIRTUAL_BASE) /**< The end of the kernel's map of physical memory, 256 MiB. */
#define BOOT_MAP_TEST_BOOT_MAP_PAGES    (BOOT_MAP_TEST_BOOT_MAP_END / PAGE_SIZE) /**< Pages boot.S maps. */
#define BOOT_MAP_TEST_ALTERNATING_COUNT 100u  /**< Entries of the made-up map that alternates RAM and reserved. */
#define BOOT_MAP_TEST_MAP_BYTES         4096u /**< Room for the largest made-up map, 100 entries of 24 bytes. */

/**
 * @brief One entry of a made-up memory map.
 */
typedef struct BootMapTestEntry {
    uint64_t base;   /**< Its first byte. */
    uint64_t length; /**< Its length in bytes. */
    uint32_t type;   /**< 1 usable RAM, 2 reserved, 3 ACPI reclaimable, 4 ACPI NVS. */
    uint32_t size;   /**< Its size field: 20, 24 with extended attributes, or below 20 when corrupt. */
} BootMapTestEntry_t;

/**
 * @brief A made-up memory map: its entries in order, or none at all.
 */
typedef struct BootMapTestMap {
    const BootMapTestEntry_t *entries; /**< Its entries, NULL for no map at all. */
    uint32_t count;                    /**< How many. */
} BootMapTestMap_t;

static const BootMapTestEntry_t boot_map_test_q35_64[] = {
    {0x0u,        0x9FC00u,   1u, 20u},
    {0x9FC00u,    0x400u,     2u, 20u},
    {0xF0000u,    0x10000u,   2u, 20u},
    {0x100000u,   0x3EDF000u, 1u, 20u},
    {0x3FDF000u,  0x21000u,   2u, 20u},
    {0xFFFC0000u, 0x40000u,   2u, 20u},
};
static const BootMapTestEntry_t boot_map_test_q35_48[] = {
    {0x0u,       0x9FC00u,   1u, 20u},
    {0x100000u,  0x2EDF000u, 1u, 20u},
    {0x2FDF000u, 0x21000u,   2u, 20u}
};
static const BootMapTestEntry_t boot_map_test_ram_past_64_mib[] = {
    {0x0u,      0x9FC00u,   1u, 20u},
    {0x100000u, 0xFF00000u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_overlapping_ram[] = {
    {0x100000u,  0x1F00000u, 1u, 20u},
    {0x1000000u, 0x3000000u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_ram_under_reserved[] = {
    {0x100000u, 0x3F00000u, 1u, 20u},
    {0x800000u, 0x100000u,  2u, 20u}
};
static const BootMapTestEntry_t boot_map_test_mid_page_bounds[] = {
    {0x100800u,  0x1000000u, 1u, 20u},
    {0x1100000u, 0x800u,     1u, 20u},
    {0x1200000u, 0x2E00800u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_split_mid_page[] = {
    {0x100000u, 0x100800u,  1u, 20u},
    {0x200800u, 0x1E00000u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_crossing_4_gib[] = {
    {0x100000u, 0x100000000ull, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_ending_at_4_gib[] = {
    {0x100000u, 0xFFF00000u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_above_4_gib_first[] = {
    {0x100000000ull, 0x100000000ull, 1u, 20u},
    {0x100000u,      0x1000000u,     1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_unsorted_acpi[] = {
    {0x2000000u, 0x2000000u, 1u, 20u},
    {0x100000u,  0x800000u,  1u, 20u},
    {0x1000000u, 0x800000u,  3u, 20u},
    {0x1800000u, 0x800000u,  4u, 20u}
};
static const BootMapTestEntry_t boot_map_test_extended_size[] = {
    {0x100000u, 0x3F00000u, 1u, 24u}
};
static const BootMapTestEntry_t boot_map_test_corrupt_size[] = {
    {0x0u,      0x9FC00u,   1u, 20u},
    {0x9FC00u,  0x400u,     2u, 0u },
    {0x100000u, 0x3F00000u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_low_ram_only[] = {
    {0x0u, 0x80000u, 1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_no_entry[] = {
    {0x0u, 0x0u, 0u, 0u}
};
static BootMapTestEntry_t boot_map_test_alternating[BOOT_MAP_TEST_ALTERNATING_COUNT];

static uint8_t boot_map_test_map_bytes[BOOT_MAP_TEST_MAP_BYTES];
static uint32_t boot_map_test_dropped[BOOT_MAP_TEST_BOOT_MAP_PAGES / 32u];

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
 * @brief Lays @p map out as a Multiboot memory map in boot_map_test_map_bytes, each entry its size field then that
 *        many bytes, a corrupt size moving the next entry onto its fields.
 *
 * @return Its length in bytes.
 */
static uint32_t boot_map_test_lay_out(const BootMapTestMap_t *map)
{
    uint32_t length = 0u;

    for (uint32_t index = 0u; index < map->count; ++index)
    {
        const BootMapTestEntry_t *const made_up = &map->entries[index];
        MemoryMapEntry_t entry = {.size = made_up->size,
                                  .base_addr = made_up->base,
                                  .length = made_up->length,
                                  .type = made_up->type};

        memset(boot_map_test_map_bytes + length, 0, sizeof(uint32_t) + 24u);
        memcpy(boot_map_test_map_bytes + length, &entry, sizeof(entry));
        length += sizeof(uint32_t) + made_up->size;
    }
    return length;
}

/**
 * @brief Marks in boot_map_test_dropped every page of every run physical_memory_manager_boot_map_next_hole gives for
 *        a map of @p length bytes, and says whether each run is well formed: past the first MiB, not empty, within
 *        the boot map, after the previous one.
 */
static bool boot_map_test_mark_holes(const uint8_t *map, uint32_t length)
{
    uint32_t first = 0u;
    uint32_t end = 0u;

    memset(boot_map_test_dropped, 0, sizeof(boot_map_test_dropped));
    for (uint32_t from = 0u; physical_memory_manager_boot_map_next_hole(map, length, from, &first, &end); from = end)
    {
        if (first < from || first < BOOT_MAP_TEST_FIRST_MEBIBYTE_END || end <= first ||
            end > BOOT_MAP_TEST_BOOT_MAP_END || (first | end) & (PAGE_SIZE - 1u))
            return false;
        for (uint32_t page = first; page < end; page += PAGE_SIZE)
            boot_map_test_dropped[page / PAGE_SIZE / 32u] |= 1u << (page / PAGE_SIZE % 32u);
    }
    return true;
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
 * @brief On 18 made-up memory maps, the holes physical_memory_manager_boot_map_next_hole gives are exactly the pages
 *        between 1 and 64 MiB that no usable RAM entry holds whole, as the test reads each map itself.
 *
 * @details The maps: none, empty, a lone corrupt entry, q35 with 64 and 48 MiB, RAM past 64 MiB, overlapping RAM,
 *          RAM under a reserved entry, bounds and RAM within a page, RAM split mid-page, RAM crossing and ending at
 *          4 GiB, an entry above 4 GiB first, 100 alternating entries, unsorted RAM with ACPI memory, a 24-byte
 *          entry, a corrupt entry before the kernel's RAM, and RAM below 1 MiB only. No boot exercises most of them:
 *          the boots with 256 MiB or more leave nothing to unmap.
 */
KERNEL_TEST(the_holes_of_made_up_memory_maps_are_the_pages_no_ram_holds)
{
    for (uint32_t index = 0u; index < BOOT_MAP_TEST_ALTERNATING_COUNT; ++index)
        boot_map_test_alternating[index] =
            (BootMapTestEntry_t){0x100000u + (uint64_t) index * 0x80000u, 0x40000u, index % 3u ? 1u : 2u, 20u};

    const BootMapTestMap_t maps[] = {
        {NULL,                             0u                                                            },
        {boot_map_test_no_entry,           0u                                                            },
        {boot_map_test_no_entry,           1u                                                            },
        {boot_map_test_q35_64,             sizeof(boot_map_test_q35_64) / sizeof(boot_map_test_q35_64[0])},
        {boot_map_test_q35_48,             sizeof(boot_map_test_q35_48) / sizeof(boot_map_test_q35_48[0])},
        {boot_map_test_ram_past_64_mib,    2u                                                            },
        {boot_map_test_overlapping_ram,    2u                                                            },
        {boot_map_test_ram_under_reserved, 2u                                                            },
        {boot_map_test_mid_page_bounds,    3u                                                            },
        {boot_map_test_split_mid_page,     2u                                                            },
        {boot_map_test_crossing_4_gib,     1u                                                            },
        {boot_map_test_ending_at_4_gib,    1u                                                            },
        {boot_map_test_above_4_gib_first,  2u                                                            },
        {boot_map_test_alternating,        BOOT_MAP_TEST_ALTERNATING_COUNT                               },
        {boot_map_test_unsorted_acpi,      4u                                                            },
        {boot_map_test_extended_size,      1u                                                            },
        {boot_map_test_corrupt_size,       3u                                                            },
        {boot_map_test_low_ram_only,       1u                                                            },
    };
    uint32_t agreeing = 0u;
    uint32_t dropped = 0u;
    uint32_t first_disagreeing = 0u;

    for (uint32_t index = 0u; index < sizeof(maps) / sizeof(maps[0]); ++index)
    {
        const uint32_t length = boot_map_test_lay_out(&maps[index]);
        const uint8_t *const map = maps[index].entries ? boot_map_test_map_bytes : NULL;
        bool agrees = boot_map_test_mark_holes(map, length);

        for (uint32_t page = 0u; agrees && page < BOOT_MAP_TEST_BOOT_MAP_END; page += PAGE_SIZE)
        {
            const bool marked = (boot_map_test_dropped[page / PAGE_SIZE / 32u] >> (page / PAGE_SIZE % 32u)) & 1u;
            const bool expected = map && length != 0u && page >= BOOT_MAP_TEST_FIRST_MEBIBYTE_END &&
                                  !boot_map_test_map_holds_ram(map, length, page);

            dropped += marked;
            agrees = marked == expected;
        }
        if (agrees)
            ++agreeing;
        else if (first_disagreeing == 0u)
            first_disagreeing = index + 1u;
    }

    kernel_test_measure(test, "maps", sizeof(maps) / sizeof(maps[0]));
    kernel_test_measure(test, "maps_agreeing", agreeing);
    kernel_test_measure(test, "pages_dropped", dropped);
    kernel_test_measure(test, "first_disagreeing_map_one_based", first_disagreeing);
    kernel_test_check(test, agreeing == sizeof(maps) / sizeof(maps[0]),
                      "each map's holes are its pages between 1 and 64 MiB that no RAM entry holds whole");
}

/**
 * @brief The boot map keeps the first mebibyte, which the kernel reads directly (VGA text, the BIOS data area,
 *        the BIOS area the root pointer is scanned in, the processor trampoline), and past it only the pages the
 *        memory map calls RAM, as Linux maps only RAM past the first MiB.
 *
 * @details Skipped when the memory map leaves no page between 1 and 64 MiB outside RAM, as QEMU's with 256 MiB or
 *          more: `qemu-system-i386 -machine q35 -m 64 -kernel kernel/lpl.kernel -append lpl.test=boot_map.*` runs it
 *          on QEMU's reserved top of RAM, and 48 MiB on a hole as well.
 */
KERNEL_TEST(the_boot_map_keeps_the_first_mebibyte_and_only_memory_past_it)
{
    uint32_t length = 0u;

    if (!boot_map_test_memory_map(&length))
    {
        kernel_test_skip(test, "the bootloader gave no memory map");
        return;
    }

    uint32_t outside_ram = 0u;

    for (uint32_t page = BOOT_MAP_TEST_FIRST_MEBIBYTE_END; page < BOOT_MAP_TEST_BOOT_MAP_END; page += PAGE_SIZE)
        outside_ram += !boot_map_test_page_is_ram(page);
    if (outside_ram == 0u)
    {
        kernel_test_skip(test, "the memory map leaves no page between 1 and 64 MiB outside RAM: boot with -m 64");
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
