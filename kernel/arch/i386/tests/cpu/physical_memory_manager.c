#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(physical_memory_manager, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief A write into a freed page is found when the page is handed out again.
 *
 * @details The freed page is poisoned; writing into it and allocating again must raise the
 *          use-after-free count. The poison exists in every build that runs tests.
 */
KERNEL_TEST(use_after_free_is_detected)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "checked on the server profile only");
#else
    const uint32_t anomalies_before = physical_memory_manager_get_uaf_anomaly_count();
    const uint32_t freed_page = physical_memory_manager_page_frame_allocate();
    const uint32_t kept_page = physical_memory_manager_page_frame_allocate();

    if (!kernel_test_check(test, freed_page != 0u && kept_page != 0u, "two pages can be allocated"))
    {
        if (freed_page)
            physical_memory_manager_page_frame_free(freed_page);
        if (kept_page)
            physical_memory_manager_page_frame_free(kept_page);
        return;
    }

    volatile uint32_t *const stale_mapping = (volatile uint32_t *) (freed_page + KERNEL_VIRTUAL_BASE);

    physical_memory_manager_page_frame_free(freed_page);
    stale_mapping[10] = 0xDEADBEEFu;

    const uint32_t handed_out_again = physical_memory_manager_page_frame_allocate();

    kernel_test_check(test, physical_memory_manager_get_uaf_anomaly_count() > anomalies_before,
                      "a write into a freed page is found when the page is handed out again");
    kernel_test_measure(test, "same_page", handed_out_again == freed_page ? 1u : 0u);

    physical_memory_manager_page_frame_free(kept_page);
    if (handed_out_again)
        physical_memory_manager_page_frame_free(handed_out_again);
#endif
}

/**
 * @brief Two buddies merge when both are free, and not before.
 *
 * @details The control comes first: with its buddy still allocated, a freed page stays free on its
 *          own. Without it, a probe that never reported a free page would pass the merge check.
 */
KERNEL_TEST(buddies_merge_when_both_are_free)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile has no buddy allocator");
#else
    uint32_t pages[128] = {0};
    uint32_t allocated = 0u;
    uint32_t first_buddy = 0u;
    uint32_t second_buddy = 0u;

    for (uint32_t index = 0u; index < 128u && !first_buddy; ++index)
    {
        pages[index] = physical_memory_manager_page_frame_allocate();
        if (!pages[index])
            break;
        allocated = index + 1u;
        for (uint32_t earlier = 0u; earlier < index; ++earlier)
        {
            if ((pages[earlier] ^ PAGE_SIZE) == pages[index])
            {
                first_buddy = pages[earlier];
                second_buddy = pages[index];
                break;
            }
        }
    }

    for (uint32_t index = 0u; index < allocated; ++index)
    {
        if (pages[index] != first_buddy && pages[index] != second_buddy)
            physical_memory_manager_page_frame_free(pages[index]);
    }

    if (!first_buddy)
    {
        kernel_test_skip(test, "no buddy pair among 128 pages");
        return;
    }

    physical_memory_manager_page_frame_free(first_buddy);
    kernel_test_check(test, physical_memory_manager_debug_is_free_block(first_buddy, 0u),
                      "a page whose buddy is still in use stays free on its own");

    physical_memory_manager_page_frame_free(second_buddy);
    kernel_test_check(test,
                      !physical_memory_manager_debug_is_free_block(first_buddy, 0u) &&
                          !physical_memory_manager_debug_is_free_block(second_buddy, 0u),
                      "freeing its buddy merges the two: neither stays free on its own");
#endif
}

/**
 * @brief Freeing every page of a scattered allocation restores the count, and a double free is
 *        refused and counted once.
 */
KERNEL_TEST(scattered_frees_restore_the_count)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile has no buddy allocator");
#else
    uint32_t pages[96] = {0};
    uint32_t allocated = 0u;
    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    const uint32_t rejected_before = physical_memory_manager_debug_get_rejected_free_count();
    const uint32_t double_frees_before = physical_memory_manager_debug_get_double_free_count();

    while (allocated < 96u && (pages[allocated] = physical_memory_manager_page_frame_allocate()) != 0u)
        ++allocated;

    if (!kernel_test_check(test, allocated >= 3u, "at least three pages can be allocated"))
    {
        for (uint32_t index = 0u; index < allocated; ++index)
            physical_memory_manager_page_frame_free(pages[index]);
        return;
    }

    for (uint32_t index = 1u; index < allocated; index += 2u)
        physical_memory_manager_page_frame_free(pages[index]);
    for (uint32_t index = 0u; index < allocated; index += 2u)
        physical_memory_manager_page_frame_free(pages[index]);
    physical_memory_manager_page_frame_free(pages[1]);

    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before,
                      "freeing every page, odd ones first, restores the free count");
    kernel_test_check(test, physical_memory_manager_debug_get_rejected_free_count() == rejected_before + 1u,
                      "the second free of a page is refused");
    kernel_test_check(test, physical_memory_manager_debug_get_double_free_count() == double_frees_before + 1u,
                      "and counted as a double free");
    kernel_test_measure(test, "allocated", allocated);
#endif
}

/**
 * @brief A block of order 1 takes two pages, gives them back, and can be allocated again.
 */
KERNEL_TEST(order_one_block_round_trip)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile has no buddy allocator");
#else
    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    const uint32_t block = physical_memory_manager_page_frame_allocate_order(1u);

    if (!kernel_test_check(test, block != 0u, "a block of two pages can be allocated"))
        return;

    kernel_test_check(test, physical_memory_manager_get_free_page_count() + 2u == free_before,
                      "allocating it takes two pages");
    physical_memory_manager_page_frame_free_order(block, 1u);
    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before, "freeing it gives both back");

    const uint32_t again = physical_memory_manager_page_frame_allocate_order(1u);

    kernel_test_check(test, again != 0u, "it can be allocated again");
    if (again)
        physical_memory_manager_page_frame_free_order(again, 1u);
#endif
}

/**
 * @brief The low watermark follows allocations down, and the high one never falls.
 */
KERNEL_TEST(watermarks_follow_the_free_count)
{
    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    const uint32_t high_before = physical_memory_manager_get_watermark_high();
    const uint32_t low_before = physical_memory_manager_get_watermark_low();
    uint32_t pages[8] = {0};
    uint32_t allocated = 0u;

    while (allocated < 8u && (pages[allocated] = physical_memory_manager_page_frame_allocate()) != 0u)
        ++allocated;

    const uint32_t low_after_allocating = physical_memory_manager_get_watermark_low();

    for (uint32_t index = 0u; index < allocated; ++index)
        physical_memory_manager_page_frame_free(pages[index]);

    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before,
                      "freeing what was allocated restores the free count");
    kernel_test_check(test, allocated == 0u || low_after_allocating <= low_before,
                      "the low watermark does not rise while pages are taken");
    kernel_test_check(test, physical_memory_manager_get_watermark_high() >= high_before,
                      "the high watermark does not fall");
}

/**
 * @brief The fragmentation ratio stays a percentage, and freeing every other page then the rest
 *        restores the free count.
 */
KERNEL_TEST(fragmentation_ratio_stays_a_percentage)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile has no buddy allocator");
#else
    const uint32_t ratio_before = physical_memory_manager_get_fragmentation_ratio();
    const uint32_t free_before = physical_memory_manager_get_free_page_count();
    uint32_t pages[32] = {0};
    uint32_t allocated = 0u;

    while (allocated < 32u && (pages[allocated] = physical_memory_manager_page_frame_allocate()) != 0u)
        ++allocated;
    for (uint32_t index = 1u; index < allocated; index += 2u)
        physical_memory_manager_page_frame_free(pages[index]);

    const uint32_t ratio_scattered = physical_memory_manager_get_fragmentation_ratio();

    for (uint32_t index = 0u; index < allocated; index += 2u)
        physical_memory_manager_page_frame_free(pages[index]);

    const uint32_t ratio_after = physical_memory_manager_get_fragmentation_ratio();

    kernel_test_check(test, ratio_before <= 100u && ratio_scattered <= 100u && ratio_after <= 100u,
                      "the fragmentation ratio is a percentage before, during and after");
    kernel_test_check(test, physical_memory_manager_get_free_page_count() == free_before,
                      "freeing every page restores the free count");
    kernel_test_measure(test, "ratio_scattered", ratio_scattered);
#endif
}
