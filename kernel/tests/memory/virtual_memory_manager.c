#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <string.h>

KERNEL_TEST_SUITE(virtual_memory_manager, KERNEL_TEST_STAGE_INITIALIZATION);

#if !defined(LPL_KERNEL_REAL_TIME_MODE)
/**
 * @brief Whether [@p address, @p address + @p pages) lies inside the range the manager hands out.
 */
static bool virtual_memory_manager_test_is_in_range(const void *address, uint32_t pages)
{
    const uintptr_t start = (uintptr_t) address;

    return address != NULL && start >= KERNEL_VMM_DYNAMIC_START && start + pages * PAGE_SIZE <= KERNEL_VMM_DYNAMIC_END;
}
#endif

/**
 * @brief Runs of 2 and 4 pages are served inside the manager's range and freed.
 */
KERNEL_TEST(page_runs_are_served_in_range)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "checked on the server profile only");
#else
    void *const two_pages = kernel_vmm_alloc_pages(2);
    void *const four_pages = kernel_vmm_alloc_pages(4);

    kernel_test_check(test, virtual_memory_manager_test_is_in_range(two_pages, 2u),
                      "a run of 2 pages is served inside the manager's range");
    kernel_test_check(test, virtual_memory_manager_test_is_in_range(four_pages, 4u),
                      "a run of 4 pages is served inside the manager's range");
    kernel_test_check(test, two_pages != four_pages, "the two runs are distinct");

    if (two_pages)
        kernel_vmm_free_pages(two_pages, 2);
    if (four_pages)
        kernel_vmm_free_pages(four_pages, 4);
#endif
}

#define VIRTUAL_MEMORY_MANAGER_TEST_BITMAP_BYTES 32u /**< Bytes of each bitmap the first-fit test searches. */

/**
 * @brief The first run of @p run_pages free pages of @p bitmap, searched one page at a time from page 0, apart from
 *        the manager's search: what kernel_vmm_first_free_run must find.
 */
static uint32_t virtual_memory_manager_test_first_fit(const uint8_t *bitmap, uint32_t page_count, uint32_t run_pages)
{
    for (uint32_t first = 0u; run_pages != 0u && first + run_pages <= page_count; ++first)
    {
        uint32_t page = first;

        while (page < first + run_pages && (bitmap[page / 8u] & (1u << (page % 8u))) == 0u)
            ++page;
        if (page == first + run_pages)
            return first;
    }
    return KERNEL_VMM_NO_FREE_RUN;
}

/**
 * @brief The manager's first-fit search finds the run a search page by page finds, on 256 bitmaps of 256 pages whose
 *        bytes are each all taken, all free, or mixed, for every run of 1 to 24 pages, and on runs that start at a
 *        byte's first page right after taken bytes, end at the bitmap's last page, or fit nowhere.
 */
KERNEL_TEST(the_first_fit_search_finds_the_run_a_page_by_page_search_finds)
{
    uint8_t bitmap[VIRTUAL_MEMORY_MANAGER_TEST_BITMAP_BYTES];
    const uint32_t pages = VIRTUAL_MEMORY_MANAGER_TEST_BITMAP_BYTES * 8u;
    uint32_t state = 0x811C9DC5u;
    uint32_t searches = 0u;
    uint32_t agree = 0u;
    uint32_t found = 0u;

    for (uint32_t pattern = 0u; pattern < 256u; ++pattern)
    {
        for (uint32_t byte = 0u; byte < VIRTUAL_MEMORY_MANAGER_TEST_BITMAP_BYTES; ++byte)
        {
            state = state * 1664525u + 1013904223u;
            bitmap[byte] = ((state >> 28u) < 9u) ? 0xFFu : ((state >> 28u) < 11u) ? 0x00u : (uint8_t) (state >> 8u);
        }
        for (uint32_t run = 1u; run <= 24u; ++run)
        {
            const uint32_t expected = virtual_memory_manager_test_first_fit(bitmap, pages, run);

            ++searches;
            found += (expected != KERNEL_VMM_NO_FREE_RUN) ? 1u : 0u;
            agree += (kernel_vmm_first_free_run(bitmap, pages, run) == expected) ? 1u : 0u;
        }
    }
    memset(bitmap, 0xFF, sizeof(bitmap));
    bitmap[3] = 0x00u;
    bitmap[4] = 0x00u;
    kernel_test_check(test, kernel_vmm_first_free_run(bitmap, pages, 9u) == 24u,
                      "a run that starts at a byte's first page, after whole bytes taken, is found there");
    bitmap[VIRTUAL_MEMORY_MANAGER_TEST_BITMAP_BYTES - 1u] = 0x0Fu;
    kernel_test_check(test,
                      kernel_vmm_first_free_run(bitmap, pages, 9u) == 24u &&
                          kernel_vmm_first_free_run(bitmap, pages, 4u) == 24u,
                      "the first run that fits is found, not a later one");
    bitmap[3] = 0xFFu;
    bitmap[4] = 0xFFu;
    kernel_test_check(test,
                      kernel_vmm_first_free_run(bitmap, pages, 4u) == pages - 4u &&
                          kernel_vmm_first_free_run(bitmap, pages, 5u) == KERNEL_VMM_NO_FREE_RUN,
                      "a run that ends at the last page is found, and one larger than every hole is not");
    memset(bitmap, 0x00, sizeof(bitmap));
    kernel_test_check(test,
                      kernel_vmm_first_free_run(bitmap, pages, pages) == 0u &&
                          kernel_vmm_first_free_run(bitmap, pages, pages + 1u) == KERNEL_VMM_NO_FREE_RUN,
                      "a run of every page fits a bitmap with every page free, and one page more does not");
    kernel_test_check(test, kernel_vmm_first_free_run(bitmap, pages, 0u) == KERNEL_VMM_NO_FREE_RUN,
                      "a run of no page is not");
    kernel_test_check(test, agree == searches, "every search finds the run a page-by-page search finds");
    kernel_test_check(test, found > searches / 4u && found < searches,
                      "the bitmaps hold runs to find, and runs that fit nowhere");
    kernel_test_measure(test, "searches", searches);
    kernel_test_measure(test, "found", found);
}

/**
 * @brief Pages of a reservation are mapped by a commit, each once, and only those: a commit of a page mapped already,
 *        or of one no reservation holds, is refused and maps nothing; freeing the reservation gives back the frames of
 *        the pages committed, the others never mapped.
 */
KERNEL_TEST(a_commit_maps_the_reserved_pages_it_names_and_no_other)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "checked on the server profile only");
#else
    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    uint8_t *const pages = kernel_vmm_reserve_pages(4u);

    if (!kernel_test_check(test, virtual_memory_manager_test_is_in_range(pages, 4u), "four pages are reserved"))
        return;

    const uint32_t first = (uint32_t) (uintptr_t) pages;

    kernel_test_check(test, !paging_is_mapped(first), "a reserved page is not mapped");
    kernel_test_check(test, kernel_vmm_commit_pages(pages + PAGE_SIZE, 2u), "two of them are committed");
    kernel_test_check(test,
                      !paging_is_mapped(first) && paging_is_mapped(first + PAGE_SIZE) &&
                          paging_is_mapped(first + 2u * PAGE_SIZE) && !paging_is_mapped(first + 3u * PAGE_SIZE),
                      "those two, and no other, are mapped");
    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before - 2u, "each took one frame");
    kernel_test_check(test,
                      !kernel_vmm_commit_pages(pages + 2u * PAGE_SIZE, 2u) && !paging_is_mapped(first + 3u * PAGE_SIZE),
                      "a commit over a page mapped already is refused, and maps the page after it neither");
    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before - 2u, "and takes no frame");
    kernel_vmm_free_pages(pages, 4u);
    kernel_test_check(test, !paging_is_mapped(first + PAGE_SIZE) && !paging_is_mapped(first + 2u * PAGE_SIZE),
                      "freeing the reservation unmaps what it committed");
    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before, "and gives back both frames");
    kernel_test_check(test, !kernel_vmm_commit_pages(pages, 1u) && !paging_is_mapped(first),
                      "a page no reservation holds any more is refused");
#endif
}

/**
 * @brief A device window reads the device, and is given back without freeing a frame.
 *
 * @details The local interrupt controller serves as the device: its version register, read through
 *          the window, is the one apic_read gives. Its frame is the controller's, so unmapping the
 *          window must not hand it to the physical memory manager, which would serve the next
 *          allocation from the controller's registers.
 */
KERNEL_TEST(a_device_window_reads_the_device_and_frees_no_frame)
{
    const uint32_t base = advanced_pic_timer_backend_get_local_apic_physical_base();

    if (base == 0u || apic_is_x2apic_active())
    {
        kernel_test_skip(test, "no local interrupt controller mapped in memory");
        return;
    }

    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    const uint32_t rejected_before = physical_memory_manager_debug_get_rejected_free_count();
    const uint32_t window = kernel_vmm_map_device_window(base + LAPIC_REG_VERSION, sizeof(uint32_t));
    uint32_t physical = 0u;

    kernel_test_check(test, window != 0u && (window & (PAGE_SIZE - 1u)) == LAPIC_REG_VERSION,
                      "the window keeps the register's offset in its page");
    if (window == 0u)
        return;
    kernel_test_check(test, paging_get_physical_address(window, &physical) && physical == base + LAPIC_REG_VERSION,
                      "it maps the register's physical address");
    kernel_test_check(test, *(volatile const uint32_t *) (uintptr_t) window == apic_read(LAPIC_REG_VERSION),
                      "the version register reads the same through it");

    kernel_vmm_unmap_device_window(window, sizeof(uint32_t));
    kernel_test_check(test, !paging_get_physical_address(window, &physical), "the window is unmapped");
    kernel_test_check(test,
                      physical_memory_manager_get_free_page_count() == free_before &&
                          physical_memory_manager_debug_get_rejected_free_count() == rejected_before,
                      "and no frame went to the physical memory manager");
}

/**
 * @brief A device window whose last byte would lie past 4 GiB is refused, and the last page below it
 *        is mapped.
 *
 * @details Each page of a window used to be computed in 32 bits: two pages from 0xFFFFF000 came back
 *          mapped, the second on physical page 0, uncached and writable.
 */
KERNEL_TEST(a_device_window_past_4_gib_is_refused)
{
    const uint32_t last_page = kernel_vmm_map_device_window(0xFFFFF000u, PAGE_SIZE);
    const uint32_t past = kernel_vmm_map_device_window(0xFFFFF000u, 2u * PAGE_SIZE);

    if (last_page != 0u)
        kernel_vmm_unmap_device_window(last_page, PAGE_SIZE);
    if (past != 0u)
        kernel_vmm_unmap_device_window(past, 2u * PAGE_SIZE);
    kernel_test_check(test, last_page != 0u, "the page from 0xFFFFF000 is mapped");
    kernel_test_check(test, past == 0u, "two pages from 0xFFFFF000 are refused");
}
