#include <kernel/memory/spsc_ring.h>
#include <kernel/testing/test.h>

#include <stddef.h>

KERNEL_TEST_SUITE(spsc_ring, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @brief Pushes @p count consecutive values from @p first, run by run, and publishes each run.
 *
 * @return How many were pushed before the ring was full.
 */
static uint32_t spsc_ring_test_push_run(KernelSpscRing_t *ring, uint32_t *slots, uint32_t first, uint32_t count)
{
    uint32_t pushed = 0u;

    while (pushed < count)
    {
        uint32_t first_slot = 0u;
        const uint32_t run = kernel_spsc_ring_reserve(ring, count - pushed, &first_slot);

        if (run == 0u)
            break;
        for (uint32_t offset = 0u; offset < run; ++offset)
            slots[first_slot + offset] = first + pushed + offset;
        kernel_spsc_ring_publish(ring, run);
        pushed += run;
    }
    return pushed;
}

/**
 * @brief Pops @p count values, run by run, and checks they are the consecutive values from @p first.
 */
static bool spsc_ring_test_pop_run(KernelSpscRing_t *ring, const uint32_t *slots, uint32_t first, uint32_t count)
{
    uint32_t popped = 0u;

    while (popped < count)
    {
        uint32_t first_slot = 0u;
        const uint32_t run = kernel_spsc_ring_peek(ring, count - popped, &first_slot);

        if (run == 0u)
            return false;
        for (uint32_t offset = 0u; offset < run; ++offset)
        {
            if (slots[first_slot + offset] != first + popped + offset)
                return false;
        }
        kernel_spsc_ring_release(ring, run);
        popped += run;
    }
    return true;
}

/**
 * @brief A ring of N slots holds N elements, refuses the next one, and hands them back oldest first.
 */
KERNEL_TEST(holds_as_many_elements_as_it_has_slots)
{
    static KernelSpscRing_t ring = KERNEL_SPSC_RING_INITIALIZER(4u);
    uint32_t slots[4] = {0u};
    uint32_t first_slot = 0u;

    kernel_test_check(test, kernel_spsc_ring_capacity(&ring) == 4u && kernel_spsc_ring_size(&ring) == 0u,
                      "the static initializer gives an empty ring of four slots");
    kernel_test_check(test, kernel_spsc_ring_peek(&ring, 1u, &first_slot) == 0u, "an empty ring has nothing to read");
    kernel_test_check(test, spsc_ring_test_push_run(&ring, slots, 10u, 4u) == 4u && kernel_spsc_ring_size(&ring) == 4u,
                      "four slots take four elements");
    kernel_test_check(test, kernel_spsc_ring_reserve(&ring, 1u, &first_slot) == 0u, "a full ring reserves nothing");

    kernel_spsc_ring_count_rejected(&ring, 1u);
    kernel_test_check(test, kernel_spsc_ring_rejected_count(&ring) == 1u, "and the producer counts what it dropped");
    kernel_test_check(test, spsc_ring_test_pop_run(&ring, slots, 10u, 4u) && kernel_spsc_ring_size(&ring) == 0u,
                      "the four come back oldest first");
}

/**
 * @brief A reservation and a peek stop at the end of the array, and the next call goes on from slot 0.
 */
KERNEL_TEST(runs_stop_at_the_end_of_the_array)
{
    KernelSpscRing_t ring;
    uint32_t slots[8] = {0u};
    uint32_t first_slot = 0u;

    kernel_test_check(test, kernel_spsc_ring_initialize(&ring, 8u), "eight is a capacity");
    kernel_test_check(
        test, spsc_ring_test_push_run(&ring, slots, 0u, 6u) == 6u && spsc_ring_test_pop_run(&ring, slots, 0u, 6u),
        "six go through, so the next slot is the seventh");
    kernel_test_check(test, kernel_spsc_ring_reserve(&ring, 5u, &first_slot) == 2u && first_slot == 6u,
                      "five wanted from slot 6 give the two before the end");
    kernel_spsc_ring_publish(&ring, 2u);
    kernel_test_check(test, kernel_spsc_ring_reserve(&ring, 3u, &first_slot) == 3u && first_slot == 0u,
                      "and the next three start again at slot 0");
    kernel_spsc_ring_publish(&ring, 3u);
    kernel_test_check(test, kernel_spsc_ring_peek(&ring, 5u, &first_slot) == 2u && first_slot == 6u,
                      "a peek of five gives the two before the end");
    kernel_spsc_ring_release(&ring, 2u);
    kernel_test_check(test, kernel_spsc_ring_peek(&ring, 5u, &first_slot) == 3u && first_slot == 0u,
                      "then the three from slot 0");
}

/**
 * @brief Order holds across a thousand wraps, with runs of one to seven elements.
 */
KERNEL_TEST(keeps_the_order_across_many_wraps)
{
    KernelSpscRing_t ring;
    uint32_t slots[8] = {0u};
    uint32_t next = 0u;
    bool in_order = kernel_spsc_ring_initialize(&ring, 8u);

    for (uint32_t round = 0u; round < 1000u && in_order; ++round)
    {
        const uint32_t count = 1u + round % 7u;

        in_order = spsc_ring_test_push_run(&ring, slots, next, count) == count &&
                   spsc_ring_test_pop_run(&ring, slots, next, count);
        next += count;
    }
    kernel_test_check(test, in_order && kernel_spsc_ring_size(&ring) == 0u, "four thousand elements come out in order");
}

/**
 * @brief The consumer reaches each ready element by its distance from the oldest, before releasing any.
 */
KERNEL_TEST(slot_at_reaches_the_ready_elements_by_offset)
{
    KernelSpscRing_t ring;
    uint32_t slots[4] = {0u};

    kernel_test_check(test, kernel_spsc_ring_initialize(&ring, 4u), "four is a capacity");
    kernel_test_check(
        test, spsc_ring_test_push_run(&ring, slots, 0u, 3u) == 3u && spsc_ring_test_pop_run(&ring, slots, 0u, 3u),
        "three go through, so the oldest is in slot 3");
    kernel_test_check(test,
                      spsc_ring_test_push_run(&ring, slots, 40u, 3u) == 3u && kernel_spsc_ring_ready_count(&ring) == 3u,
                      "three more are ready");
    kernel_test_check(test,
                      slots[kernel_spsc_ring_slot_at(&ring, 0u)] == 40u &&
                          slots[kernel_spsc_ring_slot_at(&ring, 1u)] == 41u &&
                          slots[kernel_spsc_ring_slot_at(&ring, 2u)] == 42u,
                      "offsets 0, 1 and 2 reach them across the wrap");
}

/**
 * @brief A capacity the free-running indices cannot mask is refused, and the ring is left as it was.
 */
KERNEL_TEST(refuses_a_capacity_that_is_not_a_power_of_two)
{
    KernelSpscRing_t ring;

    kernel_test_check(test, kernel_spsc_ring_initialize(&ring, 16u), "sixteen is a capacity");
    kernel_test_check(test,
                      !kernel_spsc_ring_initialize(&ring, 0u) && !kernel_spsc_ring_initialize(&ring, 12u) &&
                          !kernel_spsc_ring_initialize(&ring, 0x80000001u),
                      "zero, twelve and 2^31 + 1 are refused");
    kernel_test_check(test, kernel_spsc_ring_capacity(&ring) == 16u, "and the ring keeps its sixteen slots");
    kernel_test_check(test, kernel_spsc_ring_initialize(&ring, KERNEL_SPSC_RING_MAX_CAPACITY),
                      "2^31 is the largest capacity");
}

/**
 * @brief What the producer writes and what the consumer writes are one interference span apart.
 */
KERNEL_TEST(the_two_ends_never_share_a_line)
{
    static KernelSpscRing_t ring = KERNEL_SPSC_RING_INITIALIZER(2u);
    const uintptr_t producer = (uintptr_t) &ring.producer;
    const uintptr_t consumer = (uintptr_t) &ring.consumer;

    kernel_test_check(test,
                      producer % KERNEL_SPSC_RING_INTERFERENCE_SIZE == 0u &&
                          consumer % KERNEL_SPSC_RING_INTERFERENCE_SIZE == 0u,
                      "each end starts an interference span");
    kernel_test_check(test, consumer - producer == KERNEL_SPSC_RING_INTERFERENCE_SIZE,
                      "and the consumer's starts where the producer's ends");
}
