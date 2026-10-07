#include <kernel/lib/asmutils.h>
#include <kernel/memory/frame_arena.h>
#include <kernel/memory/pool_allocator.h>
#include <kernel/memory/ring_buffer.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(real_time_allocators, KERNEL_TEST_STAGE_INITIALIZATION);

/** Cycles a frame-arena or pool operation usually stays under; a reading above it is reported. */
#define REAL_TIME_ALLOCATORS_TEST_USUAL_CYCLE_BOUND 1500u

/**
 * @brief The worst case of every frame-arena and pool operation has been measured.
 *
 * @details The test runs each operation once first, so it does not depend on another test. The
 *          bound is reported, not checked: under emulation a single preempted operation exceeds it.
 */
KERNEL_TEST(worst_cases_are_measured)
{
    if (!kernel_test_check(test, kernel_frame_arena_is_initialized() && kernel_pool_allocator_is_initialized(),
                           "the frame arena and the pool are initialised"))
        return;

    kernel_frame_arena_reset();
    (void) kernel_frame_arena_alloc(64u, 8u);
    kernel_frame_arena_reset();
    kernel_pool_free(kernel_pool_alloc());

    const uint32_t arena_allocation = kernel_frame_arena_get_wcet_alloc_cycles();
    const uint32_t arena_reset = kernel_frame_arena_get_wcet_reset_cycles();
    const uint32_t pool_allocation = kernel_pool_get_wcet_alloc_cycles();
    const uint32_t pool_free = kernel_pool_get_wcet_free_cycles();

    kernel_test_check(test, arena_allocation > 0u && arena_reset > 0u,
                      "the worst frame-arena allocation and reset were measured");
    kernel_test_check(test, pool_allocation > 0u && pool_free > 0u, "the worst pool allocation and free were measured");
    kernel_test_measure(test, "arena_allocation_cycles", arena_allocation);
    kernel_test_measure(test, "arena_reset_cycles", arena_reset);
    kernel_test_measure(test, "pool_allocation_cycles", pool_allocation);
    kernel_test_measure(test, "pool_free_cycles", pool_free);
    if (arena_allocation >= REAL_TIME_ALLOCATORS_TEST_USUAL_CYCLE_BOUND ||
        arena_reset >= REAL_TIME_ALLOCATORS_TEST_USUAL_CYCLE_BOUND ||
        pool_allocation >= REAL_TIME_ALLOCATORS_TEST_USUAL_CYCLE_BOUND ||
        pool_free >= REAL_TIME_ALLOCATORS_TEST_USUAL_CYCLE_BOUND)
        kernel_test_note(test, "a worst case is above the usual 1500 cycles");
}

/**
 * @brief A hundred frame-arena allocations are served, and their spread of cycles is reported.
 *
 * @details The spread is reported, not asserted: emulation makes it unstable. A slowest allocation
 *          more than 30 % above the fastest is noted.
 */
KERNEL_TEST(hundred_frame_allocations_are_timed)
{
    if (!kernel_test_check(test, kernel_frame_arena_is_initialized(), "the frame arena is initialised"))
        return;

    uint32_t fastest = 0xFFFFFFFFu;
    uint32_t slowest = 0u;
    uint32_t served = 0u;

    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(1024u * 1024u);
    for (; served < 100u; ++served)
    {
        const uint32_t start = asmutils_read_timestamp_counter_low();
        void *const block = kernel_frame_arena_alloc(64u, 8u);
        const uint32_t cycles = asmutils_read_timestamp_counter_low() - start;

        if (!block)
            break;
        fastest = (cycles < fastest) ? cycles : fastest;
        slowest = (cycles > slowest) ? cycles : slowest;
    }
    kernel_frame_arena_reset();
    kernel_frame_arena_set_frame_budget(0u);

    kernel_test_check(test, served == 100u, "a hundred 64-byte allocations are served within a 1 MiB budget");
    kernel_test_measure(test, "fastest_cycles", fastest);
    kernel_test_measure(test, "slowest_cycles", slowest);
    if (fastest > 0u && slowest * 10u >= fastest * 13u)
        kernel_test_note(test, "the slowest allocation is more than 30 % above the fastest");
}

/**
 * @brief Five hundred iterations of a hot loop each get a frame block, borrow and return a pool
 *        object, and pass one event through the ring.
 */
KERNEL_TEST(combined_hot_loop_keeps_serving)
{
    if (!kernel_test_check(test,
                           kernel_frame_arena_is_initialized() && kernel_pool_allocator_is_initialized() &&
                               kernel_ring_buffer_is_initialized(),
                           "the frame arena, the pool and the ring are initialised"))
        return;

    uint32_t iterations = 0u;

    kernel_frame_arena_reset();
    for (; iterations < 500u; ++iterations)
    {
        void *const frame_block = kernel_frame_arena_alloc(16u, 8u);
        void *const pool_object = kernel_pool_alloc();
        uint8_t event = 0xAFu;
        const bool event_passed = kernel_ring_buffer_enqueue(&event, 1u) && kernel_ring_buffer_dequeue(&event, 1u);

        if (pool_object)
            kernel_pool_free(pool_object);
        if (!frame_block || !pool_object || !event_passed)
            break;
    }
    kernel_frame_arena_reset();

    kernel_test_check(test, iterations == 500u, "every iteration is served by all three");
}
