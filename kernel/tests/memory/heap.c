#include <kernel/cpu/cpu_topology.h>
#include <kernel/memory/heap.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(heap, KERNEL_TEST_STAGE_INITIALIZATION);

/** Number of size classes the server heap keeps a free count for. */
#define HEAP_TEST_SIZE_CLASS_COUNT 7u

#if !defined(LPL_KERNEL_REAL_TIME_MODE)
/**
 * @brief Allocates two blocks of @p size, frees them, allocates two again, and reports whether every
 *        step was served and went through the size-class and per-processor fast paths.
 *
 * @param size       Bytes asked for.
 * @param size_class Index of the size class that serves @p size.
 */
static bool heap_test_size_class_serves_from_its_cache(uint32_t size, uint32_t size_class)
{
    const uint32_t domain = kernel_heap_get_server_active_domain();
    const uint32_t class_hits_before = kernel_heap_get_size_class_hit_count(size_class);
    const uint32_t processor_hits_before = kernel_heap_get_server_per_cpu_hit_count(domain);
    void *const first = kmalloc(size);
    void *const second = kmalloc(size);
    const bool served = first != NULL && second != NULL && first != second;

    if (first)
        kfree(first);
    if (second)
        kfree(second);

    void *const first_again = kmalloc(size);
    void *const second_again = kmalloc(size);
    const bool served_again = first_again != NULL && second_again != NULL;

    if (first_again)
        kfree(first_again);
    if (second_again)
        kfree(second_again);

    return served && served_again && kernel_heap_get_size_class_hit_count(size_class) > class_hits_before &&
           kernel_heap_get_server_per_cpu_hit_count(domain) > processor_hits_before;
}
#endif

/**
 * @brief Small and large blocks are served and taken back, and a double free is refused once.
 *
 * @details The first claim is about the boot, not about this test: every free before it was a
 *          legitimate one, so a refused count here is the heap refusing its own blocks, the leak of
 *          a canary written on some allocation paths only (#464). Only this suite provokes refused
 *          frees, and this test is its first.
 */
KERNEL_TEST(blocks_are_served_and_taken_back)
{
    const uint32_t refused_before = kernel_heap_debug_get_rejected_free_count();
    const uint32_t double_frees_before = kernel_heap_debug_get_double_free_count();
    const uint32_t small_free_bytes_before = kernel_heap_get_small_free_bytes();
    const uint32_t large_before = kernel_heap_get_large_allocation_count();

    kernel_test_check(test, refused_before == 0u, "no free was refused before this test");

    void *const small = kmalloc(64u);
    void *const medium = kmalloc(224u);
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    void *const large = NULL;
    const uint32_t large_expected = 0u;
#else
    void *const large = kmalloc(5000u);
    const uint32_t large_expected = 1u;
#endif
    const uint32_t large_held = kernel_heap_get_large_allocation_count();

    kernel_test_check(test, small != NULL && medium != NULL, "64 and 224 bytes are served");
    kernel_test_check(test, large_expected == 0u || large != NULL, "5000 bytes are served on the server");

    if (small)
        kfree(small);
    if (large)
        kfree(large);
    if (medium)
    {
        kfree(medium);
        kfree(medium);
    }

    kernel_test_check(test, large_held - large_before == large_expected,
                      "a large allocation is counted while it is held");
    kernel_test_check(test, kernel_heap_get_large_allocation_count() == large_before, "and no longer once it is freed");
    kernel_test_check(test, kernel_heap_debug_get_rejected_free_count() == refused_before + 1u,
                      "the second free of a block is refused");
    kernel_test_check(test, kernel_heap_debug_get_double_free_count() == double_frees_before + 1u,
                      "and counted as a double free");

#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_check(test,
                      kernel_heap_get_small_free_block_count() > 0u &&
                          kernel_heap_get_small_free_bytes() >= small_free_bytes_before,
                      "the small pool gets back what it lent");
#else
    uint32_t size_class_free_blocks = 0u;

    (void) small_free_bytes_before;
    for (uint32_t size_class = 0u; size_class < HEAP_TEST_SIZE_CLASS_COUNT; ++size_class)
        size_class_free_blocks += kernel_heap_get_size_class_free_count(size_class);
    kernel_test_check(test, kernel_heap_get_small_free_block_count() > 0u || size_class_free_blocks > 0u,
                      "the heap has free blocks to serve after the frees");
#endif
}

/**
 * @brief A fresh block is poisoned, and a block whose canary was overwritten is refused when freed.
 */
KERNEL_TEST(poison_and_canary)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "checked on the server profile only");
#else
    void *const sensitive = kmalloc_sensitive(100u);
    uint8_t *const normal = (uint8_t *) kmalloc(32u);
    bool poisoned = (normal != NULL);

    kernel_test_check(test, sensitive != NULL && normal != NULL, "a sensitive and a normal block are served");
    for (uint32_t index = 0u; normal && index < 32u; ++index)
        poisoned = poisoned && normal[index] == 0xCCu;
    kernel_test_check(test, poisoned, "a fresh block reads as poison, not as what its last owner left");

    const uint32_t refused_before = kernel_heap_debug_get_rejected_free_count();

    if (normal)
    {
        ((KernelHeapBlock_t *) normal - 1)->canary = 0xBADu;
        kfree(normal);
    }
    if (sensitive)
        kfree(sensitive);

    kernel_test_check(test, kernel_heap_debug_get_rejected_free_count() > refused_before,
                      "a block whose canary was overwritten is refused when freed");
#endif
}

/**
 * @brief Blocks of 8, 64 and 256 bytes come from their size class and the processor's cache, and
 *        serving them never takes from another domain.
 */
KERNEL_TEST(size_classes_serve_from_the_fast_path)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile serves small blocks from its slab caches");
#else
    const uint32_t domain = kernel_heap_get_server_active_domain();
    const uint32_t remote_probes_before = kernel_heap_get_server_domain_remote_probe_count(domain);
    const uint32_t remote_hits_before = kernel_heap_get_server_domain_remote_hit_count(domain);

    kernel_test_check(test, kernel_heap_get_server_domain_count() >= 1u && domain == 0u,
                      "the heap has a domain, and the running processor uses the first");
    kernel_test_check(test, heap_test_size_class_serves_from_its_cache(8u, 0u),
                      "8-byte blocks are served twice through the fast paths");
    kernel_test_check(test, heap_test_size_class_serves_from_its_cache(64u, 3u),
                      "64-byte blocks are served twice through the fast paths");
    kernel_test_check(test, heap_test_size_class_serves_from_its_cache(256u, 5u),
                      "256-byte blocks are served twice through the fast paths");
    kernel_test_check(test,
                      kernel_heap_get_server_domain_remote_probe_count(domain) >= remote_probes_before &&
                          kernel_heap_get_server_domain_remote_hit_count(domain) == remote_hits_before,
                      "a domain with blocks of its own takes none from another");
#endif
}

/**
 * @brief A domain with nothing cached takes the block a neighbour freed.
 */
KERNEL_TEST(empty_domain_takes_from_its_neighbour)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile has a single heap domain");
#else
    if (kernel_heap_get_server_domain_count() < 2u)
    {
        kernel_test_skip(test, "a single heap domain");
        return;
    }

    const bool forced_to_first = cpu_topology_debug_force_logical_slot(0u);
    void *const seed = kmalloc(64u);

    if (seed)
        kfree(seed);

    const uint32_t remote_probes_before = kernel_heap_get_server_domain_remote_probe_count(1u);
    const uint32_t remote_hits_before = kernel_heap_get_server_domain_remote_hit_count(1u);
    const bool forced_to_second = cpu_topology_debug_force_logical_slot(1u);
    void *const borrowed = kmalloc(64u);
    const uint32_t remote_probes_after = kernel_heap_get_server_domain_remote_probe_count(1u);
    const uint32_t remote_hits_after = kernel_heap_get_server_domain_remote_hit_count(1u);

    if (borrowed)
        kfree(borrowed);
    cpu_topology_debug_clear_forced_logical_slot();

    void *const settle = kmalloc(8u);

    if (settle)
        kfree(settle);

    kernel_test_check(test, forced_to_first && forced_to_second, "the running processor can be moved between slots");
    kernel_test_check(test, borrowed != NULL, "the second domain is served");
    kernel_test_check(test, remote_probes_after > remote_probes_before && remote_hits_after > remote_hits_before,
                      "by the block the first domain freed");
    kernel_test_check(test, cpu_topology_is_forced() == 0u && kernel_heap_get_server_active_domain() == 0u,
                      "the running processor is back on the first domain");
#endif
}

/**
 * @brief Each processor slot allocates from its own domain.
 */
KERNEL_TEST(each_slot_allocates_from_its_domain)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the client profile has a single heap domain");
#else
    if (kernel_heap_get_server_domain_count() < 2u)
    {
        kernel_test_skip(test, "a single heap domain");
        return;
    }

    const bool forced_to_first = cpu_topology_debug_force_logical_slot(0u);
    const uint32_t first_domain = kernel_heap_get_server_active_domain();
    void *const from_first = kmalloc(64u);
    const bool forced_to_second = cpu_topology_debug_force_logical_slot(1u);
    const uint32_t second_domain = kernel_heap_get_server_active_domain();
    void *const from_second = kmalloc(64u);
    void *const from_second_again = kmalloc(64u);

    cpu_topology_debug_clear_forced_logical_slot();

    const uint32_t domain_after = kernel_heap_get_server_active_domain();

    if (from_first)
        kfree(from_first);
    if (from_second)
        kfree(from_second);
    if (from_second_again)
        kfree(from_second_again);

    kernel_test_check(test, forced_to_first && forced_to_second, "the running processor can be moved between slots");
    kernel_test_check(test, first_domain == 0u && second_domain == 1u, "slot 0 uses domain 0 and slot 1 domain 1");
    kernel_test_check(test, from_first != NULL && from_second != NULL && from_second_again != NULL,
                      "both domains serve");
    kernel_test_check(test, domain_after == 0u, "releasing the slot puts the processor back on domain 0");
#endif
}

/**
 * @brief A thousand 512-byte blocks, each freed before the next, are all served.
 */
KERNEL_TEST(thousand_round_trips_are_served)
{
    uint32_t round_trips = 0u;

    for (; round_trips < 1000u; ++round_trips)
    {
        void *const block = kmalloc(512u);

        if (!block)
            break;
        kfree(block);
    }

    kernel_test_check(test, round_trips == 1000u, "a thousand 512-byte round trips are all served");
}

/**
 * @brief Inside the hot loop a bounded request is served, and one that would grow the heap is refused
 *        and counted.
 *
 * @details A 64-byte request is a slab class and must be served; a 64 MiB one would grow the heap.
 */
KERNEL_TEST(hot_loop_serves_bounded_requests_only)
{
#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    kernel_test_skip(test, "the hot-loop rule belongs to the client profile");
#else
    const uint32_t violations_before = kernel_heap_get_hot_loop_violation_count();
    const uint32_t bounded_before = kernel_heap_get_hot_loop_bounded_count();
    void *const held = kmalloc(100u);

    kernel_heap_hot_loop_enter();
    void *const bounded = kmalloc(64u);
    void *const unbounded = kmalloc(64u * 1024u * 1024u);
    if (held)
        kfree(held);
    if (bounded)
        kfree(bounded);
    kernel_heap_hot_loop_leave();

    kernel_test_check(test, held != NULL, "a block is served before the loop");
    kernel_test_check(test, bounded != NULL, "a bounded request is served inside the loop");
    kernel_test_check(test, unbounded == NULL, "a request that would grow the heap is refused");
    kernel_test_check(test, kernel_heap_get_hot_loop_depth() == 0u, "leaving the loop closes it");
    kernel_test_check(test, kernel_heap_get_hot_loop_violation_count() == violations_before + 1u,
                      "the refusal is counted once");
    kernel_test_check(test, kernel_heap_get_hot_loop_bounded_count() >= bounded_before + 3u,
                      "the bounded operations are counted");
#endif
}
