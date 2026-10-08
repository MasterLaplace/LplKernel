#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(paging, KERNEL_TEST_STAGE_INITIALIZATION);

/** Pages in the run the shootdown test frees. */
#define PAGING_TEST_RUN_PAGES 8u

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

/**
 * @brief Freeing a run of pages sends one TLB shootdown for the whole run, and only then gives its
 *        frames back.
 *
 * @details One interrupt per page made each freed page cost a broadcast every other CPU had to
 *          answer, and the frame was free before the shootdown that dropped its translation.
 */
KERNEL_TEST(a_freed_run_costs_one_shootdown)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "checked on the server profile only");
#else
    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    void *const run = kernel_vmm_alloc_pages(PAGING_TEST_RUN_PAGES);

    if (!kernel_test_check(test, run != NULL, "a run of pages can be allocated"))
        return;

    const uint32_t first_page = (uint32_t) (uintptr_t) run;
    const uint32_t shootdowns_before = advanced_pic_ipi_get_tlb_shootdown_broadcast_count();
    const uint32_t ranges_before = kernel_vmm_get_unmapped_range_count();

    kernel_vmm_free_pages(run, PAGING_TEST_RUN_PAGES);

    const uint32_t shootdowns = advanced_pic_ipi_get_tlb_shootdown_broadcast_count() - shootdowns_before;
    const uint32_t online = cpu_topology_get_online_cpu_count();

    kernel_test_check(test, shootdowns == (online > 1u ? 1u : 0u), "freeing it sends one shootdown for the whole run");
    kernel_test_check(test, kernel_vmm_get_unmapped_range_count() == ranges_before + 1u, "and counts one range");
    kernel_test_check(
        test, !paging_is_mapped(first_page) && !paging_is_mapped(first_page + (PAGING_TEST_RUN_PAGES - 1u) * PAGE_SIZE),
        "no page of the run stays mapped");
    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before,
                      "every frame of the run goes back");
    kernel_test_measure(test, "shootdowns", shootdowns);
    kernel_test_measure(test, "online", online);
#endif
}
