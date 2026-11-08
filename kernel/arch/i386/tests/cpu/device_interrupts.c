#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(device_interrupts, KERNEL_TEST_STAGE_PUBLISHED);

#define DEVICE_INTERRUPTS_TEST_WAIT_MICROSECONDS 100000u /**< How long a sent vector may take to arrive. */
#define DEVICE_INTERRUPTS_TEST_SELF              1u      /**< The ICR shorthand that names the sender. */

static volatile uint32_t device_interrupts_test_runs = 0u;   /**< Runs of the test handler. */
static void *volatile device_interrupts_test_context = NULL; /**< The context it last received. */

/**
 * @brief Counts its run and keeps the context it received.
 */
static void device_interrupts_test_handler(void *context)
{
    ++device_interrupts_test_runs;
    device_interrupts_test_context = context;
}

/**
 * @brief Waits until @p vector has been counted on @p slot more than @p before times, a bounded while.
 *
 * @return The count once it moved, or when the wait ran out.
 */
static uint32_t device_interrupts_test_wait(uint8_t vector, uint32_t slot, uint32_t before)
{
    InterruptRequestDeadline_t deadline;

    interrupt_request_deadline_start(&deadline, DEVICE_INTERRUPTS_TEST_WAIT_MICROSECONDS);
    while (device_interrupt_count(vector, slot) == before && !interrupt_request_deadline_passed(&deadline))
        ;
    return device_interrupt_count(vector, slot);
}

/**
 * @brief The vectors handed out are the 32 from 0x50, each given once, until none is left; a released one is handed
 *        out again, and a NULL handler gets none.
 */
KERNEL_TEST(vectors_are_given_once_until_none_is_left)
{
    uint8_t vectors[DEVICE_INTERRUPT_VECTOR_COUNT] = {0u};
    uint32_t given = 0u;
    uint32_t in_range = 0u;
    uint32_t repeated = 0u;
    uint8_t again = 0u;

    kernel_test_check(test, !device_interrupt_allocate(NULL, NULL, &again), "a NULL handler gets no vector");
    while (given < DEVICE_INTERRUPT_VECTOR_COUNT &&
           device_interrupt_allocate(device_interrupts_test_handler, NULL, &vectors[given]))
        ++given;
    for (uint32_t index = 0u; index < given; ++index)
    {
        in_range += (vectors[index] >= DEVICE_INTERRUPT_VECTOR_BASE &&
                     vectors[index] < DEVICE_INTERRUPT_VECTOR_BASE + DEVICE_INTERRUPT_VECTOR_COUNT) ?
                        1u :
                        0u;
        for (uint32_t other = 0u; other < index; ++other)
            repeated += vectors[other] == vectors[index] ? 1u : 0u;
    }

    const bool exhausted = !device_interrupt_allocate(device_interrupts_test_handler, NULL, &again);

    if (given != 0u)
    {
        device_interrupt_release(vectors[given - 1u]);
        kernel_test_check(test,
                          device_interrupt_allocate(device_interrupts_test_handler, NULL, &again) &&
                              again == vectors[given - 1u],
                          "a released vector is handed out again");
    }
    for (uint32_t index = 0u; index < given; ++index)
        device_interrupt_release(vectors[index]);
    kernel_test_check(test, given != 0u, "a vector was free");
    kernel_test_check(test, in_range == given, "every vector lies in 0x50-0x6F");
    kernel_test_check(test, repeated == 0u, "none is given twice");
    kernel_test_check(test, exhausted, "once every vector is taken, none is given");
    kernel_test_measure(test, "free_vectors", given);
}

/**
 * @brief An online slot other than @p here, or CPU_TOPOLOGY_NO_SLOT.
 */
static uint32_t device_interrupts_test_other_slot(uint32_t here)
{
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        if (slot != here && cpu_topology_is_logical_slot_online(slot))
            return slot;
    return CPU_TOPOLOGY_NO_SLOT;
}

/**
 * @brief A device vector this core sends itself runs its handler once, with its context, counted on this
 *        core's slot.
 */
KERNEL_TEST(a_vector_runs_its_handler_on_the_core_it_reaches)
{
    uint8_t vector = 0u;
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());

    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return;
    }
    if (!device_interrupt_allocate(device_interrupts_test_handler, &vector, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    device_interrupts_test_runs = 0u;
    device_interrupts_test_context = NULL;

    const uint32_t before = device_interrupt_count(vector, here);

    kernel_test_check(test, advanced_pic_ipi_send_fixed(0u, vector, DEVICE_INTERRUPTS_TEST_SELF) != 0u,
                      "the vector is sent to this core");
    kernel_test_check(test, device_interrupts_test_wait(vector, here, before) == before + 1u,
                      "it is counted once on this core's slot");
    kernel_test_check(test, device_interrupts_test_runs == 1u && device_interrupts_test_context == (void *) &vector,
                      "its handler ran once, with its context");
    device_interrupt_release(vector);
    kernel_test_measure(test, "vector", vector);
}

/**
 * @brief A device vector sent to another core is counted on that core's slot, and not on the sender's.
 */
KERNEL_TEST(a_vector_sent_to_another_core_is_counted_on_that_core)
{
    uint8_t vector = 0u;
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const uint32_t other = device_interrupts_test_other_slot(here);

    if (other == CPU_TOPOLOGY_NO_SLOT)
    {
        kernel_test_skip(test, "no other processor is online: boot with -smp 2");
        return;
    }
    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return;
    }
    if (!device_interrupt_allocate(device_interrupts_test_handler, &vector, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }

    const uint32_t there = device_interrupt_count(vector, other);
    const uint32_t here_before = device_interrupt_count(vector, here);

    kernel_test_check(test, advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(other), vector, 0u) != 0u,
                      "the vector is sent to another core");
    kernel_test_check(test, device_interrupts_test_wait(vector, other, there) == there + 1u,
                      "it is counted once on that core's slot");
    kernel_test_check(test, device_interrupt_count(vector, here) == here_before, "and not on this one's");
    device_interrupt_release(vector);
    kernel_test_measure(test, "other_slot", other);
}
