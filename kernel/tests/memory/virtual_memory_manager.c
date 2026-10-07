#include <kernel/cpu/paging.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

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
