#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(paging, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Searches the 4 MiB slots from @p first for one whose first two pages are unmapped.
 *
 * @return The slot's base address, or 0 when every slot is taken.
 */
static uint32_t paging_test_find_unmapped_slot(uint32_t first)
{
    for (uint32_t candidate = first; candidate <= 0xFF800000u; candidate += 0x00400000u)
    {
        if (!paging_is_mapped(candidate) && !paging_is_mapped(candidate + PAGE_SIZE))
            return candidate;
    }
    return 0u;
}

/**
 * @brief A page table the kernel created for a mapping is reclaimed once the slot is empty again.
 *
 * @note Manual: it maps pages behind the back of the virtual memory manager, inside the range that
 *       manager hands out (KERNEL_VMM_DYNAMIC_START to KERNEL_VMM_DYNAMIC_END), and was switched off
 *       when that manager arrived (609efac).
 */
KERNEL_TEST_MANUAL(empty_page_table_is_reclaimed, "maps pages inside the range the virtual memory manager hands out")
{
    const uint32_t slot = paging_test_find_unmapped_slot(KERNEL_VMM_DYNAMIC_START);

    if (!slot)
    {
        kernel_test_skip(test, "no unmapped 4 MiB slot");
        return;
    }

    const uint32_t tables_before = paging_get_runtime_owned_page_table_count();
    const uint32_t first_page = physical_memory_manager_page_frame_allocate();
    const uint32_t second_page = physical_memory_manager_page_frame_allocate();
    PageDirectoryEntry_t directory_flags = {0};
    PageTableEntry_t table_flags = {0};

    directory_flags.present = 1;
    directory_flags.read_write = 1;
    table_flags.present = 1;
    table_flags.read_write = 1;

    const bool first_mapped =
        first_page && second_page && paging_map_page(slot, first_page, directory_flags, table_flags);
    const bool second_mapped =
        first_mapped && paging_map_page(slot + PAGE_SIZE, second_page, directory_flags, table_flags);
    const uint32_t tables_mapped = paging_get_runtime_owned_page_table_count();
    const bool second_unmapped = second_mapped && paging_unmap_page(slot + PAGE_SIZE);
    const bool first_unmapped = first_mapped && paging_unmap_page(slot);

    if (first_page)
        physical_memory_manager_page_frame_free(first_page);
    if (second_page)
        physical_memory_manager_page_frame_free(second_page);

    kernel_test_check(test, first_mapped && second_mapped, "two pages of an empty slot can be mapped");
    kernel_test_check(test, tables_mapped > tables_before, "mapping them creates a page table");
    kernel_test_check(test, first_unmapped && second_unmapped, "both can be unmapped");
    kernel_test_check(test, paging_get_runtime_owned_page_table_count() == tables_before,
                      "unmapping the last page reclaims the table");
}
