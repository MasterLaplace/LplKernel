#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/msi.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(message_signaled_interrupts, KERNEL_TEST_STAGE_PUBLISHED);

#define MESSAGE_SIGNALED_INTERRUPTS_TEST_WAIT_MICROSECONDS 100000u /**< How long a sent vector may take to arrive. */
#define MESSAGE_SIGNALED_INTERRUPTS_TEST_SELF              1u      /**< The ICR shorthand that names the sender. */

static volatile uint32_t message_signaled_interrupts_test_runs = 0u;   /**< Runs of the test handler. */
static void *volatile message_signaled_interrupts_test_context = NULL; /**< The context it last received. */

/**
 * @brief Counts its run and keeps the context it received.
 */
static void message_signaled_interrupts_test_handler(void *context)
{
    ++message_signaled_interrupts_test_runs;
    message_signaled_interrupts_test_context = context;
}

/**
 * @brief Waits until @p vector has been counted on @p slot more than @p before times, a bounded while.
 *
 * @return The count once it moved, or when the wait ran out.
 */
static uint32_t message_signaled_interrupts_test_wait(uint8_t vector, uint32_t slot, uint32_t before)
{
    InterruptRequestDeadline_t deadline;

    interrupt_request_deadline_start(&deadline, MESSAGE_SIGNALED_INTERRUPTS_TEST_WAIT_MICROSECONDS);
    while (message_signaled_interrupt_count(vector, slot) == before && !interrupt_request_deadline_passed(&deadline))
        ;
    return message_signaled_interrupt_count(vector, slot);
}

/**
 * @brief A message names its local APIC in bits 19:12 of 0xFEExxxxx and carries its vector as data, edge-triggered
 *        and fixed; the broadcast 0xFF, an identifier past it, which needs interrupt remapping, and the vectors
 *        outside 0x10-0xFE are refused.
 */
KERNEL_TEST(a_message_names_its_core_and_carries_its_vector)
{
    uint32_t address = 0u;
    uint32_t data = 0u;

    kernel_test_check(test, message_signaled_interrupt_compose(0x12u, 0x51u, &address, &data),
                      "APIC 0x12, vector 0x51 composes");
    kernel_test_check(test, address == 0xFEE12000u, "its address is 0xFEE12000");
    kernel_test_check(test, data == 0x51u, "its data is the vector alone: fixed, edge, asserted");
    kernel_test_check(test, message_signaled_interrupt_compose(0xFEu, 0x10u, &address, &data) && address == 0xFEEFE000u,
                      "APIC 0xFE and vector 0x10, the last destination and the first vector, compose");
    kernel_test_check(test, message_signaled_interrupt_compose(0u, 0xFEu, &address, &data) && data == 0xFEu,
                      "vector 0xFE, the last one, composes");
    address = 0u;
    kernel_test_check(test, !message_signaled_interrupt_compose(0xFFu, 0x51u, &address, &data) && address == 0u,
                      "APIC 0xFF, the broadcast, is refused, nothing written");
    kernel_test_check(test, !message_signaled_interrupt_compose(0x100u, 0x51u, &address, &data),
                      "APIC 0x100 is refused");
    kernel_test_check(test, !message_signaled_interrupt_compose(0u, 0x0Fu, &address, &data),
                      "vector 0x0F, a reserved one, is refused");
    kernel_test_check(test, !message_signaled_interrupt_compose(0u, 0xFFu, &address, &data), "vector 0xFF is refused");
}

/**
 * @brief The vectors handed out are the 32 from 0x50, each given once, until none is left; a released one is handed
 *        out again, and a NULL handler gets none.
 */
KERNEL_TEST(vectors_are_given_once_until_none_is_left)
{
    uint8_t vectors[MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT] = {0u};
    uint32_t given = 0u;
    uint32_t in_range = 0u;
    uint32_t repeated = 0u;
    uint8_t again = 0u;

    kernel_test_check(test, !message_signaled_interrupt_allocate(NULL, NULL, &again), "a NULL handler gets no vector");
    while (given < MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT &&
           message_signaled_interrupt_allocate(message_signaled_interrupts_test_handler, NULL, &vectors[given]))
        ++given;
    for (uint32_t index = 0u; index < given; ++index)
    {
        in_range +=
            (vectors[index] >= MESSAGE_SIGNALED_INTERRUPT_VECTOR_BASE &&
             vectors[index] < MESSAGE_SIGNALED_INTERRUPT_VECTOR_BASE + MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT) ?
                1u :
                0u;
        for (uint32_t other = 0u; other < index; ++other)
            repeated += vectors[other] == vectors[index] ? 1u : 0u;
    }

    const bool exhausted = !message_signaled_interrupt_allocate(message_signaled_interrupts_test_handler, NULL, &again);

    if (given != 0u)
    {
        message_signaled_interrupt_release(vectors[given - 1u]);
        kernel_test_check(test,
                          message_signaled_interrupt_allocate(message_signaled_interrupts_test_handler, NULL, &again) &&
                              again == vectors[given - 1u],
                          "a released vector is handed out again");
    }
    for (uint32_t index = 0u; index < given; ++index)
        message_signaled_interrupt_release(vectors[index]);
    kernel_test_check(test, given != 0u, "a vector was free");
    kernel_test_check(test, in_range == given, "every vector lies in 0x50-0x6F");
    kernel_test_check(test, repeated == 0u, "none is given twice");
    kernel_test_check(test, exhausted, "once every vector is taken, none is given");
    kernel_test_measure(test, "free_vectors", given);
}

/**
 * @brief An online slot other than @p here, or CPU_TOPOLOGY_NO_SLOT.
 */
static uint32_t message_signaled_interrupts_test_other_slot(uint32_t here)
{
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        if (slot != here && cpu_topology_is_logical_slot_online(slot))
            return slot;
    return CPU_TOPOLOGY_NO_SLOT;
}

/**
 * @brief A message-signaled vector this core sends itself runs its handler once, with its context, counted on this
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
    if (!message_signaled_interrupt_allocate(message_signaled_interrupts_test_handler, &vector, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    message_signaled_interrupts_test_runs = 0u;
    message_signaled_interrupts_test_context = NULL;

    const uint32_t before = message_signaled_interrupt_count(vector, here);

    kernel_test_check(test, advanced_pic_ipi_send_fixed(0u, vector, MESSAGE_SIGNALED_INTERRUPTS_TEST_SELF) != 0u,
                      "the vector is sent to this core");
    kernel_test_check(test, message_signaled_interrupts_test_wait(vector, here, before) == before + 1u,
                      "it is counted once on this core's slot");
    kernel_test_check(test,
                      message_signaled_interrupts_test_runs == 1u &&
                          message_signaled_interrupts_test_context == (void *) &vector,
                      "its handler ran once, with its context");
    message_signaled_interrupt_release(vector);
    kernel_test_measure(test, "vector", vector);
}

/**
 * @brief A message-signaled vector sent to another core is counted on that core's slot, and not on the sender's.
 */
KERNEL_TEST(a_vector_sent_to_another_core_is_counted_on_that_core)
{
    uint8_t vector = 0u;
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const uint32_t other = message_signaled_interrupts_test_other_slot(here);

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
    if (!message_signaled_interrupt_allocate(message_signaled_interrupts_test_handler, &vector, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }

    const uint32_t there = message_signaled_interrupt_count(vector, other);
    const uint32_t here_before = message_signaled_interrupt_count(vector, here);

    kernel_test_check(test, advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(other), vector, 0u) != 0u,
                      "the vector is sent to another core");
    kernel_test_check(test, message_signaled_interrupts_test_wait(vector, other, there) == there + 1u,
                      "it is counted once on that core's slot");
    kernel_test_check(test, message_signaled_interrupt_count(vector, here) == here_before, "and not on this one's");
    message_signaled_interrupt_release(vector);
    kernel_test_measure(test, "other_slot", other);
}
