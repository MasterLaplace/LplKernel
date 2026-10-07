#include <kernel/memory/ring_buffer.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(ring_buffer, KERNEL_TEST_STAGE_INITIALIZATION);

/** Most values the order test pushes; the boot's ring is smaller. */
#define RING_TEST_MAXIMUM_VALUES 256u

/**
 * @brief Enqueues @p count consecutive values starting at @p first.
 *
 * @return true when every one was accepted.
 */
static bool ring_test_enqueue_run(uint32_t first, uint32_t count)
{
    for (uint32_t value = first; value < first + count; ++value)
    {
        if (!kernel_ring_buffer_enqueue(&value, sizeof(value)))
            return false;
    }
    return true;
}

/**
 * @brief Dequeues @p count values and checks they are the consecutive values starting at @p first.
 */
static bool ring_test_dequeue_run(uint32_t first, uint32_t count)
{
    for (uint32_t expected = first; expected < first + count; ++expected)
    {
        uint32_t observed = 0xFFFFFFFFu;

        if (!kernel_ring_buffer_dequeue(&observed, sizeof(observed)) || observed != expected)
            return false;
    }
    return true;
}

/**
 * @brief Values come out in the order they went in, across the wrap, and a full or an empty ring
 *        refuses rather than overwriting or inventing.
 */
KERNEL_TEST(first_in_first_out_across_the_wrap)
{
    if (!kernel_test_check(test, kernel_ring_buffer_is_initialized(), "the ring is initialised"))
        return;

    const uint32_t capacity = kernel_ring_buffer_get_capacity();
    const uint32_t half = (capacity / 2u) ? (capacity / 2u) : 1u;
    const uint32_t enqueued_before = kernel_ring_buffer_get_enqueue_count();
    const uint32_t dequeued_before = kernel_ring_buffer_get_dequeue_count();
    const uint32_t refused_enqueues_before = kernel_ring_buffer_get_failed_enqueue_count();
    const uint32_t refused_dequeues_before = kernel_ring_buffer_get_failed_dequeue_count();
    uint32_t marker = 0xDEADBEEFu;

    if (!kernel_test_check(test, capacity <= RING_TEST_MAXIMUM_VALUES, "the ring is no larger than the test expects"))
        return;

    kernel_test_check(test, kernel_ring_buffer_get_mode() == KERNEL_RING_BUFFER_MODE_SPSC,
                      "the boot's ring has one producer and one consumer");
    kernel_test_check(test, ring_test_enqueue_run(0u, capacity), "the ring takes as many values as it holds");
    kernel_test_check(test, !kernel_ring_buffer_enqueue(&marker, sizeof(marker)), "a full ring refuses one more");
    kernel_test_check(test, ring_test_dequeue_run(0u, half), "the first half comes out in order");
    kernel_test_check(test, ring_test_enqueue_run(capacity, half), "the freed half takes new values");
    kernel_test_check(test, ring_test_dequeue_run(half, capacity),
                      "everything left comes out in order, across the wrap");
    kernel_test_check(test, !kernel_ring_buffer_dequeue(&marker, sizeof(marker)), "an empty ring refuses to dequeue");
    kernel_test_check(test,
                      kernel_ring_buffer_get_enqueue_count() == enqueued_before + capacity + half &&
                          kernel_ring_buffer_get_dequeue_count() == dequeued_before + capacity + half,
                      "every accepted operation is counted");
    kernel_test_check(test,
                      kernel_ring_buffer_get_failed_enqueue_count() == refused_enqueues_before + 1u &&
                          kernel_ring_buffer_get_failed_dequeue_count() == refused_dequeues_before + 1u,
                      "each refusal is counted once");
    kernel_test_check(test, kernel_ring_buffer_get_count() == 0u, "the ring ends empty");
}

/**
 * @brief Ten thousand values go through one at a time, each coming out as it went in.
 */
KERNEL_TEST(ten_thousand_round_trips)
{
    if (!kernel_test_check(test, kernel_ring_buffer_is_initialized(), "the ring is initialised"))
        return;

    uint32_t round_trips = 0u;

    while (round_trips < 10000u && ring_test_enqueue_run(round_trips, 1u) && ring_test_dequeue_run(round_trips, 1u))
        ++round_trips;

    kernel_test_check(test, round_trips == 10000u, "ten thousand values come out as they went in");
}
