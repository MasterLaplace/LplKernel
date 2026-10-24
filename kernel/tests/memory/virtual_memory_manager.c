#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
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
 * @brief A device window whose last byte would lie past 4 GiB is refused.
 *
 * @details Each page of a window used to be computed in 32 bits: two pages from 0xFFFFF000 came back
 *          mapped, the second on physical page 0, uncached and writable.
 */
KERNEL_TEST(a_device_window_past_4_gib_is_refused)
{
    const uint32_t window = kernel_vmm_map_device_window(0xFFFFF000u, 2u * PAGE_SIZE);

    if (window != 0u)
        kernel_vmm_unmap_device_window(window, 2u * PAGE_SIZE);
    kernel_test_check(test, window == 0u, "two pages from 0xFFFFF000 are refused");
}
