#include <kernel/memory/tlsf.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(two_level_segregated_fit, KERNEL_TEST_STAGE_INITIALIZATION);

/** Worst-case cycles above which the test adds a note: emulation exceeds it whenever the host
    preempts the guest in the middle of an operation, so it cannot be a check. */
#define TWO_LEVEL_SEGREGATED_FIT_TEST_CYCLE_BOUND 5000000u

/**
 * @brief Blocks of several sizes are served and owned, a freed block's space is served again, and
 *        freeing everything restores the free bytes.
 */
KERNEL_TEST(allocate_and_free)
{
#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the allocator belongs to the client profile");
#else
    if (!kernel_test_check(test, kernel_tlsf_is_initialized(), "the allocator is initialised"))
        return;

    const uint32_t free_before = kernel_tlsf_get_free_bytes();
    const uint32_t allocations_before = kernel_tlsf_get_alloc_count();
    void *const small = kernel_tlsf_alloc(128);
    void *const medium = kernel_tlsf_alloc(1024);
    void *const large = kernel_tlsf_alloc(4096);

    kernel_test_check(test, small && medium && large, "128, 1024 and 4096 bytes are served");
    kernel_test_check(test, kernel_tlsf_owns(small) && kernel_tlsf_owns(medium) && kernel_tlsf_owns(large),
                      "the allocator owns every block it served");

    kernel_tlsf_free(medium);
    void *const first_in_the_gap = kernel_tlsf_alloc(256);
    void *const second_in_the_gap = kernel_tlsf_alloc(256);

    kernel_tlsf_free(small);
    kernel_tlsf_free(large);
    kernel_tlsf_free(first_in_the_gap);
    kernel_tlsf_free(second_in_the_gap);

    const uint32_t worst_allocation = kernel_tlsf_get_wcet_alloc_cycles();
    const uint32_t worst_free = kernel_tlsf_get_wcet_free_cycles();

    kernel_test_check(test, kernel_tlsf_get_free_bytes() == free_before, "freeing every block restores the free bytes");
    kernel_test_check(test, kernel_tlsf_get_alloc_count() == allocations_before + 5u, "every allocation is counted");
    kernel_test_check(test, worst_allocation > 0u && worst_free > 0u,
                      "the worst case of an allocation and of a free is measured");
    kernel_test_measure(test, "worst_allocation_cycles", worst_allocation);
    kernel_test_measure(test, "worst_free_cycles", worst_free);
    if (worst_allocation >= TWO_LEVEL_SEGREGATED_FIT_TEST_CYCLE_BOUND ||
        worst_free >= TWO_LEVEL_SEGREGATED_FIT_TEST_CYCLE_BOUND)
        kernel_test_note(test, "a worst case is above 5 million cycles");
#endif
}

/**
 * @brief After 128 blocks are freed in two interleaved passes, the free space is whole again.
 */
KERNEL_TEST(interleaved_frees_coalesce)
{
#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the allocator belongs to the client profile");
#else
    if (!kernel_test_check(test, kernel_tlsf_is_initialized(), "the allocator is initialised"))
        return;

    const uint32_t free_before = kernel_tlsf_get_free_bytes();
    void *blocks[128] = {0};
    uint32_t served = 0u;

    while (served < 128u && (blocks[served] = kernel_tlsf_alloc(64u + (served % 4u) * 32u)) != NULL)
        ++served;
    for (uint32_t index = 1u; index < served; index += 2u)
        kernel_tlsf_free(blocks[index]);
    for (uint32_t index = 0u; index < served; index += 2u)
        kernel_tlsf_free(blocks[index]);

    kernel_test_check(test, served == 128u, "128 blocks of mixed sizes are served");
    kernel_test_check(test, kernel_tlsf_get_free_bytes() == free_before,
                      "freeing them in two interleaved passes leaves the free space whole");
#endif
}
