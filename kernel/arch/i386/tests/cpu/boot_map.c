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
#define BOOT_MAP_TEST_READ_BACK_OFFSET   0x123u      /**< Where in that page the read-back window starts. */
#define BOOT_MAP_TEST_KERNEL_MAP_END                                                                                   \
    (KERNEL_VMM_DYNAMIC_START - KERNEL_VIRTUAL_BASE) /**< The end of the kernel's map of physical memory, 256 MiB. */
#define BOOT_MAP_TEST_BOOT_MAP_PAGES    (BOOT_MAP_TEST_BOOT_MAP_END / PAGE_SIZE) /**< Pages boot.S maps. */
#define BOOT_MAP_TEST_ALTERNATING_COUNT 100u        /**< Entries of the made-up map that alternates RAM and reserved. */
#define BOOT_MAP_TEST_MAP_BYTES         4096u       /**< Room for the largest made-up map, 100 entries of 24 bytes. */
#define BOOT_MAP_TEST_UNCACHED_BITS     3u          /**< PCD and PWT set, PAT clear: PAT entry 3, UC. */
#define BOOT_MAP_TEST_WINDOW_KINDS      3u          /**< A firmware window, a writable one and a device window. */
#define BOOT_MAP_TEST_NO_ENTRY_SEARCH   0xF0000000u /**< Where the test looks for a page no map entry holds. */
#define BOOT_MAP_TEST_NO_ENTRY_STEP     0x00100000u /**< The step of that search, 1 MiB. */

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
static const BootMapTestEntry_t boot_map_test_ram_in_the_last_page[] = {
    {0x100000u,   0x100000u, 1u, 20u},
    {0xFFFFF001u, 0x10u,     1u, 20u}
};
static const BootMapTestEntry_t boot_map_test_size_past_the_map[] = {
    {0x100000u, 0x100000u, 1u, 0xFFFFFFFCu}
};
static BootMapTestEntry_t boot_map_test_alternating[BOOT_MAP_TEST_ALTERNATING_COUNT];

static const BootMapTestMap_t boot_map_test_maps[] = {
    {NULL,                               0u                                                            },
    {boot_map_test_no_entry,             0u                                                            },
    {boot_map_test_no_entry,             1u                                                            },
    {boot_map_test_q35_64,               sizeof(boot_map_test_q35_64) / sizeof(boot_map_test_q35_64[0])},
    {boot_map_test_q35_48,               sizeof(boot_map_test_q35_48) / sizeof(boot_map_test_q35_48[0])},
    {boot_map_test_ram_past_64_mib,      2u                                                            },
    {boot_map_test_overlapping_ram,      2u                                                            },
    {boot_map_test_ram_under_reserved,   2u                                                            },
    {boot_map_test_mid_page_bounds,      3u                                                            },
    {boot_map_test_split_mid_page,       2u                                                            },
    {boot_map_test_crossing_4_gib,       1u                                                            },
    {boot_map_test_ending_at_4_gib,      1u                                                            },
    {boot_map_test_above_4_gib_first,    2u                                                            },
    {boot_map_test_alternating,          BOOT_MAP_TEST_ALTERNATING_COUNT                               },
    {boot_map_test_unsorted_acpi,        4u                                                            },
    {boot_map_test_extended_size,        1u                                                            },
    {boot_map_test_corrupt_size,         3u                                                            },
    {boot_map_test_low_ram_only,         1u                                                            },
    {boot_map_test_ram_in_the_last_page, 2u                                                            },
    {boot_map_test_size_past_the_map,    1u                                                            },
};
#define BOOT_MAP_TEST_MAP_COUNT    (sizeof(boot_map_test_maps) / sizeof(boot_map_test_maps[0])) /**< Made-up maps. */
#define BOOT_MAP_TEST_LOW_RAM_ONLY 17u /**< The index of the map with RAM below 1 MiB only. */
#define BOOT_MAP_TEST_Q35_64       3u  /**< The index of q35's map with 64 MiB. */

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

        if (entry->size < BOOT_MAP_TEST_ENTRY_SIZE_LEAST || entry->size > length - offset - sizeof(uint32_t))
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
 * @brief Fills the made-up map that alternates RAM and reserved entries: 256 KiB every 512 KiB from 1 MiB, one in
 *        three reserved.
 */
static void boot_map_test_fill_alternating(void)
{
    for (uint32_t index = 0u; index < BOOT_MAP_TEST_ALTERNATING_COUNT; ++index)
        boot_map_test_alternating[index] =
            (BootMapTestEntry_t){0x100000u + (uint64_t) index * 0x80000u, 0x40000u, index % 3u ? 1u : 2u, 20u};
}

/**
 * @brief Whether, by the test's own reading of @p map, every page of [@p first, @p end) between 1 and 64 MiB lies in
 *        RAM: no map, or an empty one, holds everything.
 */
static bool boot_map_test_map_holds_range(const uint8_t *map, uint32_t length, uint32_t first, uint32_t end)
{
    for (uint32_t page = first & ~(PAGE_SIZE - 1u);
         map && length != 0u && page < end && page < BOOT_MAP_TEST_BOOT_MAP_END; page += PAGE_SIZE)
    {
        if (page >= BOOT_MAP_TEST_FIRST_MEBIBYTE_END && !boot_map_test_map_holds_ram(map, length, page))
            return false;
    }
    return true;
}

/**
 * @brief Lays @p map out as a Multiboot memory map in boot_map_test_map_bytes, each entry its size field then that
 *        many bytes, a size below 20 moving the next entry onto its fields, one past 24 ending the map after it.
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
        length += sizeof(uint32_t) + (made_up->size <= sizeof(MemoryMapEntry_t) ? made_up->size : 20u);
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
 * @brief The type of the first entry of the bootloader's memory map that holds the whole page at @p page, read as
 *        boot_map_test_map_holds_ram reads entries, or 0 when no entry holds it whole.
 */
static uint32_t boot_map_test_page_type(uint32_t page)
{
    uint32_t length = 0u;
    const uint8_t *const map = boot_map_test_memory_map(&length);

    for (uint32_t offset = 0u; map && offset + sizeof(uint32_t) <= length;)
    {
        const MemoryMapEntry_t *const entry = (const MemoryMapEntry_t *) (map + offset);

        if (entry->size < BOOT_MAP_TEST_ENTRY_SIZE_LEAST || entry->size > length - offset - sizeof(uint32_t))
            return 0u;
        if (entry->base_addr <= page && entry->base_addr + entry->length >= (uint64_t) page + PAGE_SIZE)
            return entry->type;
        offset += sizeof(uint32_t) + entry->size;
    }
    return 0u;
}

/**
 * @brief The memory type bits a window on a page of memory map type @p type must carry past the first MiB: none,
 *        write-back, for usable RAM (1), ACPI reclaimable (3) and ACPI NVS (4) memory; uncached for any other page.
 */
static uint32_t boot_map_test_expected_bits(uint32_t type)
{
    return (type == 1u || type == 3u || type == 4u) ? 0u : BOOT_MAP_TEST_UNCACHED_BITS;
}

/**
 * @brief Maps a one-page device window on @p page and says whether it gives the page no second memory type: in the
 *        first MiB, which the kernel's map keeps, the window has that map's memory type bits; past it, the map
 *        holds no such page and the window carries the type its memory map type gives (boot_map_test_expected_bits).
 *
 * @return 1 when it agrees, 0 when it does not, -1 when no window could be mapped.
 */
static int boot_map_test_window_agrees(uint32_t page)
{
    const uint32_t window = kernel_vmm_map_device_window(page, PAGE_SIZE);
    PageTableEntry_t window_entry;
    PageTableEntry_t direct_entry;

    if (window == 0u)
        return -1;
    if (!paging_get_page_table_entry(window, &window_entry))
    {
        kernel_vmm_unmap_device_window(window, PAGE_SIZE);
        return -1;
    }

    const bool direct = boot_map_test_direct_entry(page, &direct_entry);
    const bool agrees =
        page < BOOT_MAP_TEST_FIRST_MEBIBYTE_END ?
            direct && boot_map_test_memory_type_bits(&direct_entry) == boot_map_test_memory_type_bits(&window_entry) :
            !direct && boot_map_test_memory_type_bits(&window_entry) ==
                           boot_map_test_expected_bits(boot_map_test_page_type(page));

    kernel_vmm_unmap_device_window(window, PAGE_SIZE);
    return agrees ? 1 : 0;
}

/**
 * @brief Maps the first @p kinds of a one-page firmware window, writable firmware window and device window on @p page,
 *        and gives the memory type bits of each in @p out_bits, in that order; each window is unmapped before it
 *        returns.
 *
 * @return false when one of them could not be mapped or read back.
 */
static bool boot_map_test_window_kinds_bits(uint32_t page, uint32_t kinds,
                                            uint32_t out_bits[BOOT_MAP_TEST_WINDOW_KINDS])
{
    bool read = true;

    for (uint32_t kind = 0u; kind < kinds; ++kind)
    {
        const uint32_t window = kind == 0u ? kernel_vmm_map_firmware_window(page, PAGE_SIZE) :
                                kind == 1u ? kernel_vmm_map_firmware_writable_window(page, PAGE_SIZE) :
                                             kernel_vmm_map_device_window(page, PAGE_SIZE);
        PageTableEntry_t entry;

        if (window == 0u)
            return false;
        read = read && paging_get_page_table_entry(window, &entry);
        if (read)
            out_bits[kind] = boot_map_test_memory_type_bits(&entry);
        if (kind == 2u)
            kernel_vmm_unmap_device_window(window, PAGE_SIZE);
        else
            kernel_vmm_unmap_firmware_window(window, PAGE_SIZE);
    }
    return read;
}

/**
 * @brief Whether a device window of @p size bytes from @p physical_base is mapped; it is unmapped at once.
 */
static bool boot_map_test_device_window_maps(uint32_t physical_base, uint32_t size)
{
    const uint32_t window = kernel_vmm_map_device_window(physical_base, size);

    if (window != 0u)
        kernel_vmm_unmap_device_window(window, size);
    return window != 0u;
}

/**
 * @brief Checks the windows on @p page against the memory type of its memory map type: the two firmware windows always,
 *        the device window too unless the page is usable RAM past the first MiB, where it must be refused.
 *
 * @return 1 when every window carries the bits boot_map_test_expected_bits gives and a refused one is refused, 0 when
 *         one does not, -1 when a window that should map could not.
 */
static int boot_map_test_windows_agree(uint32_t page)
{
    uint32_t bits[BOOT_MAP_TEST_WINDOW_KINDS] = {0u};
    const uint32_t type = boot_map_test_page_type(page);
    const uint32_t expected = boot_map_test_expected_bits(type);
    const bool refused = type == 1u && page >= BOOT_MAP_TEST_FIRST_MEBIBYTE_END;

    if (!boot_map_test_window_kinds_bits(page, refused ? 2u : BOOT_MAP_TEST_WINDOW_KINDS, bits))
        return -1;
    if (refused && boot_map_test_device_window_maps(page, PAGE_SIZE))
        return 0;
    return bits[0] == expected && bits[1] == expected && (refused || bits[2] == expected) ? 1 : 0;
}

/**
 * @brief The first page at or past 0xF0000000, 1 MiB apart, that no entry of the bootloader's memory map holds whole,
 *        or 0 when every one is held.
 */
static uint32_t boot_map_test_page_no_entry_holds(void)
{
    for (uint32_t page = BOOT_MAP_TEST_NO_ENTRY_SEARCH; page >= BOOT_MAP_TEST_NO_ENTRY_SEARCH;
         page += BOOT_MAP_TEST_NO_ENTRY_STEP)
    {
        if (boot_map_test_page_type(page) == 0u)
            return page;
    }
    return 0u;
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
                      "and each window has the type of the memory under it");
}

/**
 * @brief On 20 made-up memory maps, the holes physical_memory_manager_boot_map_next_hole gives are exactly the pages
 *        between 1 and 64 MiB that no usable RAM entry holds whole, as the test reads each map itself.
 *
 * @details The maps: none, empty, a lone corrupt entry, q35 with 64 and 48 MiB, RAM past 64 MiB, overlapping RAM,
 *          RAM under a reserved entry, bounds and RAM within a page, RAM split mid-page, RAM crossing and ending at
 *          4 GiB, an entry above 4 GiB first, 100 alternating entries, unsorted RAM with ACPI memory, a 24-byte
 *          entry, a corrupt entry before the kernel's RAM, RAM below 1 MiB only, a RAM entry based in the last page
 *          below 4 GiB, and an entry whose size runs past the map. No boot exercises most of them: the boots with
 *          256 MiB or more leave nothing to unmap.
 */
KERNEL_TEST(the_holes_of_made_up_memory_maps_are_the_pages_no_ram_holds)
{
    boot_map_test_fill_alternating();
    uint32_t agreeing = 0u;
    uint32_t dropped = 0u;
    uint32_t first_disagreeing = 0u;

    for (uint32_t index = 0u; index < BOOT_MAP_TEST_MAP_COUNT; ++index)
    {
        const uint32_t length = boot_map_test_lay_out(&boot_map_test_maps[index]);
        const uint8_t *const map = boot_map_test_maps[index].entries ? boot_map_test_map_bytes : NULL;
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

    kernel_test_measure(test, "maps", BOOT_MAP_TEST_MAP_COUNT);
    kernel_test_measure(test, "maps_agreeing", agreeing);
    kernel_test_measure(test, "pages_dropped", dropped);
    kernel_test_measure(test, "first_disagreeing_map_one_based", first_disagreeing);
    kernel_test_check(test, agreeing == BOOT_MAP_TEST_MAP_COUNT,
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
 * @brief A device window on the top page below 4 GiB, which the memory map calls reserved and the kernel's map of
 *        physical memory does not map, is uncached: PCD and PWT select PAT entry 3, UC.
 */
KERNEL_TEST(a_device_window_past_the_kernels_map_is_uncached)
{
    const uint32_t window = kernel_vmm_map_device_window(BOOT_MAP_TEST_TOP_PAGE, PAGE_SIZE);
    PageTableEntry_t entry;
    const bool mapped = window != 0u && paging_get_page_table_entry(window, &entry);

    kernel_test_check(test, mapped, "a window is mapped on the top page below 4 GiB");
    if (mapped)
        kernel_test_check(test,
                          entry.cache_disable == 1u && entry.write_through == 1u && entry.page_attribute_table == 0u,
                          "it is uncached");
    if (window != 0u)
        kernel_vmm_unmap_device_window(window, PAGE_SIZE);
}

/**
 * @brief A firmware window, a writable firmware window and a device window on one page past the first MiB give it one
 *        memory type, which the page decides and no window's kind: write-back for usable RAM, ACPI reclaimable and
 *        ACPI NVS memory, uncached for a reserved page and for a page no entry of the memory map holds; a device
 *        window on usable RAM is refused.
 *
 * @details Intel SDM vol. 3, §14.12.4: a page mapped with two memory types is undefined behaviour. A firmware window
 *          used to be write-back whatever lay under it, and a device window uncached, so an ACPI table and an AML
 *          region on one reserved page gave it both (SeaBIOS puts its tables in reserved memory: the DSDT at
 *          0x3FE0040 with 64 MiB). Checked on the first and the last page past the first MiB of each entry of the
 *          memory map below 4 GiB, as the test reads the map, and on a page no entry holds. Only a boot with more RAM
 *          than the kernel's map of physical memory holds, 512 MiB, puts a page of memory past that map, where the
 *          write-back comes from the memory map's type rather than from the kernel's map: the record
 *          memory_past_the_kernels_map counts those pages.
 */
KERNEL_TEST(the_windows_on_one_page_give_it_one_memory_type)
{
    uint32_t length = 0u;
    const uint8_t *const map = boot_map_test_memory_map(&length);

    if (!map)
    {
        kernel_test_skip(test, "the bootloader gave no memory map");
        return;
    }

    uint32_t checked = 0u;
    uint32_t agreeing = 0u;
    uint32_t refused = 0u;
    uint32_t reserved = 0u;
    uint32_t past_the_map = 0u;
    uint32_t first_disagreeing = 0u;
    PageTableEntry_t direct_entry;
    const uint32_t no_entry = boot_map_test_page_no_entry_holds();

    for (uint32_t offset = 0u; offset + sizeof(uint32_t) <= length;)
    {
        const MemoryMapEntry_t *const entry = (const MemoryMapEntry_t *) (map + offset);

        if (entry->size < BOOT_MAP_TEST_ENTRY_SIZE_LEAST || entry->size > length - offset - sizeof(uint32_t))
            break;
        offset += sizeof(uint32_t) + entry->size;

        const uint64_t lowest = entry->base_addr > BOOT_MAP_TEST_FIRST_MEBIBYTE_END ?
                                    (entry->base_addr + PAGE_SIZE - 1u) & ~(uint64_t) (PAGE_SIZE - 1u) :
                                    BOOT_MAP_TEST_FIRST_MEBIBYTE_END;
        const uint64_t top = entry->base_addr + entry->length < 0x100000000ull ?
                                 (entry->base_addr + entry->length) & ~(uint64_t) (PAGE_SIZE - 1u) :
                                 0x100000000ull;

        if (entry->base_addr + entry->length <= entry->base_addr || top < lowest + PAGE_SIZE)
            continue;

        const uint32_t pages[2] = {(uint32_t) lowest, (uint32_t) (top - PAGE_SIZE)};

        for (uint32_t index = 0u; index < 2u; ++index)
        {
            const int agrees = boot_map_test_windows_agree(pages[index]);

            if (agrees < 0)
            {
                ++refused;
                continue;
            }
            ++checked;
            reserved += boot_map_test_expected_bits(boot_map_test_page_type(pages[index])) != 0u;
            past_the_map += boot_map_test_expected_bits(boot_map_test_page_type(pages[index])) == 0u &&
                            !boot_map_test_direct_entry(pages[index], &direct_entry);
            if (agrees)
                ++agreeing;
            else if (checked - agreeing == 1u)
                first_disagreeing = pages[index];
        }
    }
    if (no_entry != 0u)
    {
        const int agrees = boot_map_test_windows_agree(no_entry);

        refused += agrees < 0;
        checked += agrees >= 0;
        agreeing += agrees > 0;
        if (agrees == 0 && checked - agreeing == 1u)
            first_disagreeing = no_entry;
    }

    kernel_test_measure(test, "pages_checked", checked);
    kernel_test_measure(test, "pages_agreeing", agreeing);
    kernel_test_measure(test, "uncached_pages_in_the_map", reserved);
    kernel_test_measure(test, "memory_past_the_kernels_map", past_the_map);
    kernel_test_measure_hexadecimal(test, "page_no_entry_holds", no_entry);
    kernel_test_measure_hexadecimal(test, "first_disagreeing_page", first_disagreeing);
    kernel_test_check(test, reserved != 0u && no_entry != 0u,
                      "the memory map holds a page past the first MiB that is not memory, and leaves one out");
    kernel_test_check(test, refused == 0u, "every window is mapped");
    kernel_test_check(test, checked != 0u && agreeing == checked,
                      "the windows on each page carry the memory type its memory map type gives, and a device "
                      "window on usable RAM is refused");
}

/**
 * @brief The physical address of a window above 2 GiB reads back through paging_get_physical_address, its offset
 *        within the page kept: the frame's 20 bits are shifted as an unsigned value, never as an int that 0x80000 or
 *        more overflows.
 */
KERNEL_TEST(a_window_above_2_gib_reads_back_its_physical_address)
{
    const uint32_t physical = BOOT_MAP_TEST_TOP_PAGE + BOOT_MAP_TEST_READ_BACK_OFFSET;
    const uint32_t window = kernel_vmm_map_device_window(physical, sizeof(uint32_t));
    uint32_t read_back = 0u;

    kernel_test_check(test, window != 0u, "a window is mapped on the top page below 4 GiB");
    if (window == 0u)
        return;
    kernel_test_check(test, paging_get_physical_address(window, &read_back), "its address is mapped");
    kernel_test_measure_hexadecimal(test, "read_back", read_back);
    kernel_test_check(test, read_back == physical, "it reads back 0xFFFFF123");
    kernel_vmm_unmap_device_window(window, sizeof(uint32_t));
}

/**
 * @brief On the made-up memory maps, physical_memory_manager_boot_map_holds says a range is held exactly when no page
 *        of it between 1 and 64 MiB lies outside RAM, as the test reads each map; so a map that leaves the kernel's
 *        image outside RAM, RAM below 1 MiB only, is refused, and q35's with 64 MiB is not.
 */
KERNEL_TEST(a_memory_map_that_leaves_the_kernel_outside_ram_does_not_hold_it)
{
    const uint32_t kernel_end = KERNEL_VIRTUAL_END - KERNEL_VIRTUAL_BASE;
    const uint32_t ranges[][2] = {
        {BOOT_MAP_TEST_FIRST_MEBIBYTE_END, kernel_end                      },
        {0x80000u,                         BOOT_MAP_TEST_FIRST_MEBIBYTE_END},
        {0x2000000u,                       0x2100000u                      },
        {0x3FDE000u,                       0x3FE0001u                      },
        {0x1100000u,                       0x1100800u                      },
        {0x3FFF800u,                       0x4000800u                      },
        {0x4000000u,                       0x5000000u                      },
        {0x2000000u,                       0x2000000u                      },
    };
    uint32_t checked = 0u;
    uint32_t agreeing = 0u;
    uint32_t refusing = 0u;
    bool low_ram_only_refuses_the_kernel = false;
    bool q35_holds_the_kernel = false;

    boot_map_test_fill_alternating();
    for (uint32_t index = 0u; index < BOOT_MAP_TEST_MAP_COUNT; ++index)
    {
        const uint32_t length = boot_map_test_lay_out(&boot_map_test_maps[index]);
        const uint8_t *const map = boot_map_test_maps[index].entries ? boot_map_test_map_bytes : NULL;

        for (uint32_t range = 0u; range < sizeof(ranges) / sizeof(ranges[0]); ++range)
        {
            const bool holds = physical_memory_manager_boot_map_holds(map, length, ranges[range][0], ranges[range][1]);

            ++checked;
            agreeing += holds == boot_map_test_map_holds_range(map, length, ranges[range][0], ranges[range][1]);
            refusing += !holds;
            if (range == 0u && index == BOOT_MAP_TEST_LOW_RAM_ONLY)
                low_ram_only_refuses_the_kernel = !holds;
            if (range == 0u && index == BOOT_MAP_TEST_Q35_64)
                q35_holds_the_kernel = holds;
        }
    }

    kernel_test_measure(test, "ranges_checked", checked);
    kernel_test_measure(test, "ranges_agreeing", agreeing);
    kernel_test_measure(test, "ranges_refused", refusing);
    kernel_test_check(test, agreeing == checked, "each answer is the test's own reading of the map");
    kernel_test_check(test, low_ram_only_refuses_the_kernel,
                      "a map with RAM below 1 MiB only does not hold the kernel");
    kernel_test_check(test, q35_holds_the_kernel, "q35's map with 64 MiB holds the kernel");
}
