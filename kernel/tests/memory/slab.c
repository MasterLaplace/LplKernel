#include <kernel/memory/slab.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(slab, KERNEL_TEST_STAGE_INITIALIZATION);

#if defined(LPL_KERNEL_REAL_TIME_MODE)
/**
 * @brief Two objects of @p size are served apart and given back, and a pointer the cache never
 *        handed out is refused.
 */
static bool slab_test_cache_round_trip(uint32_t size)
{
    const uint32_t free_before = kernel_slab_get_free_count(size);
    void *const first = kernel_slab_alloc(size);
    void *const second = kernel_slab_alloc(size);
    const bool served = first != NULL && second != NULL && first != second;

    if (first)
        kernel_slab_free(first);
    if (second)
        kernel_slab_free(second);

    volatile uint32_t not_from_the_cache = 0u;

    return served && kernel_slab_get_free_count(size) == free_before && !kernel_slab_free((void *) &not_from_the_cache);
}
#endif

/**
 * @brief Each cache serves two objects apart, takes them back, and refuses a foreign pointer.
 */
KERNEL_TEST(caches_serve_and_take_back)
{
#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the server profile serves small blocks from the heap's size classes");
#else
    kernel_test_check(test, slab_test_cache_round_trip(KERNEL_SLAB_SIZE_SMALL),
                      "the 16-byte cache serves, takes back and refuses a foreign pointer");
    kernel_test_check(test, slab_test_cache_round_trip(KERNEL_SLAB_SIZE_MEDIUM),
                      "the 64-byte cache serves, takes back and refuses a foreign pointer");
    kernel_test_check(test, slab_test_cache_round_trip(KERNEL_SLAB_SIZE_LARGE),
                      "the 256-byte cache serves, takes back and refuses a foreign pointer");
#endif
}
