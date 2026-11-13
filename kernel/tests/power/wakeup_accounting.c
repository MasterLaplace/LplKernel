#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/wakeup_accounting.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(wakeup_accounting, KERNEL_TEST_STAGE_BOOTED);

/** Vectors no line delivers: crediting a real one would mix this test with the boot's wake-ups. */
#define WAKEUP_TEST_VECTOR_THAT_WOKE  0xFEu
#define WAKEUP_TEST_VECTOR_THAT_DIDNT 0xFDu

#define WAKEUP_TEST_ANSWER_MICROSECONDS 100000u /**< How long another processor may take to take the interrupt. */

/** The slot of the processor that took the test's interrupt, CPU_TOPOLOGY_NO_PROCESSOR until one does. */
static volatile uint32_t wakeup_test_taken_on = CPU_TOPOLOGY_NO_PROCESSOR;

/**
 * @brief Run on the processor the test's vector reached: records its slot.
 */
static void wakeup_test_record_taker(void *context)
{
    (void) context;
    wakeup_test_taken_on = cpu_topology_get_slot_for_apic_id(apic_get_id());
}

/**
 * @brief Every outcome of a sleep is accounted for, and the books balance.
 *
 * @details The first claim is the one that matters: an interrupt with nothing armed is not a
 *          wake-up, or the module would merely count interrupts. The sleep count is checked with the
 *          law, because 0 == 0 holds for a module that does nothing.
 *
 * @note Interrupts are off throughout, so the tick cannot land between an arm and its attribution.
 *       The counters are reset on the way out.
 */
KERNEL_TEST(every_outcome_is_accounted_for)
{
    asmutils_disable_interrupts();
    kernel_wakeup_accounting_reset();

    kernel_wakeup_accounting_attribute(WAKEUP_TEST_VECTOR_THAT_WOKE);
    kernel_test_check(test,
                      kernel_wakeup_accounting_get_sleep_count() == 0u &&
                          kernel_wakeup_accounting_get_attributed_count() == 0u &&
                          kernel_wakeup_accounting_get_vector_count(WAKEUP_TEST_VECTOR_THAT_WOKE) == 0u,
                      "an interrupt while nothing sleeps wakes nobody");

    kernel_wakeup_accounting_arm();
    kernel_test_check(test, kernel_wakeup_accounting_is_armed(), "arming shows");
    kernel_wakeup_accounting_attribute(WAKEUP_TEST_VECTOR_THAT_WOKE);
    kernel_test_check(test,
                      kernel_wakeup_accounting_get_vector_count(WAKEUP_TEST_VECTOR_THAT_WOKE) == 1u &&
                          kernel_wakeup_accounting_get_vector_count(WAKEUP_TEST_VECTOR_THAT_DIDNT) == 0u &&
                          kernel_wakeup_accounting_get_attributed_count() == 1u && !kernel_wakeup_accounting_is_armed(),
                      "a wake-up credits its vector and no other, and disarms");

    kernel_wakeup_accounting_attribute(WAKEUP_TEST_VECTOR_THAT_DIDNT);
    kernel_test_check(test,
                      kernel_wakeup_accounting_get_vector_count(WAKEUP_TEST_VECTOR_THAT_DIDNT) == 0u &&
                          kernel_wakeup_accounting_get_attributed_count() == 1u,
                      "the interrupts after a wake-up did not cause it");

    kernel_wakeup_accounting_arm();
    kernel_wakeup_accounting_close_unattributed();
    kernel_test_check(test,
                      kernel_wakeup_accounting_get_unattributed_count() == 1u &&
                          kernel_wakeup_accounting_get_attributed_count() == 1u,
                      "an unnamed wake-up is kept apart");

    kernel_wakeup_accounting_arm();
    kernel_wakeup_accounting_close_monitor_write();
    kernel_test_check(test,
                      kernel_wakeup_accounting_get_monitor_wake_count() == 1u &&
                          kernel_wakeup_accounting_get_attributed_count() == 1u,
                      "a monitored write is not an interrupt");

    kernel_wakeup_accounting_arm();
    kernel_wakeup_accounting_attribute(WAKEUP_TEST_VECTOR_THAT_DIDNT);
    kernel_wakeup_accounting_arm();
    kernel_wakeup_accounting_attribute(WAKEUP_TEST_VECTOR_THAT_DIDNT);
    kernel_wakeup_accounting_arm();
    kernel_wakeup_accounting_arm();
    kernel_test_check(test, kernel_wakeup_accounting_get_double_arm_count() == 1u,
                      "arming twice is refused rather than counted twice");
    kernel_wakeup_accounting_attribute(WAKEUP_TEST_VECTOR_THAT_DIDNT);

    kernel_test_check(test, kernel_wakeup_accounting_get_busiest_vector() == (uint16_t) WAKEUP_TEST_VECTOR_THAT_DIDNT,
                      "the busiest vector is the one that woke most");
    kernel_test_check(test, kernel_wakeup_accounting_conserves() && kernel_wakeup_accounting_get_sleep_count() == 6u,
                      "six sleeps, and the books balance");

    kernel_wakeup_accounting_reset();
    asmutils_enable_interrupts();
}

/**
 * @brief Sends the test's vector to the processor of @p slot, this processor's accounting armed and its interrupts
 *        masked, and waits, boundedly, for that processor to take it.
 * @return Whether the accounting is still armed once it did; @p out_taken says whether it did.
 */
static bool wakeup_test_armed_after_one_on(uint32_t slot, uint8_t vector, bool *out_taken)
{
    InterruptRequestDeadline_t deadline;

    wakeup_test_taken_on = CPU_TOPOLOGY_NO_PROCESSOR;
    kernel_wakeup_accounting_arm();
    (void) advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(slot), vector, ADVANCED_PIC_IPI_SHORT_NONE);
    interrupt_request_deadline_start(&deadline, WAKEUP_TEST_ANSWER_MICROSECONDS);
    while (wakeup_test_taken_on == CPU_TOPOLOGY_NO_PROCESSOR && !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();
    *out_taken = wakeup_test_taken_on == slot;
    const bool armed = kernel_wakeup_accounting_is_armed();
    kernel_wakeup_accounting_close_unattributed();
    return armed;
}

/**
 * @brief An interrupt another processor takes leaves this processor's sleep armed: the books are this processor's.
 *
 * @details Every processor dispatches its interrupts through the same routine, and the accounting it feeds is the
 *          bootstrap processor's. An application processor's interrupt taken while the bootstrap processor sleeps
 *          disarmed it, so that a port read that did not stop read as interrupted, and the probe that validates a
 *          port depth would have kept it. Each other processor is tried, each on its own stack. Interrupts stay
 *          masked here, so that only the other processor can take one.
 */
KERNEL_TEST(an_interrupt_another_processor_takes_leaves_this_processor_s_sleep_armed)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    uint32_t asked = 0u;
    uint32_t taken = 0u;
    uint32_t armed = 0u;
    uint8_t vector = 0u;

    if (cpu_topology_get_online_cpu_count() < 2u)
    {
        kernel_test_skip(test, "no other processor is online: boot with -smp 2");
        return;
    }
    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return;
    }
    if (!device_interrupt_allocate(wakeup_test_record_taker, NULL, &vector))
    {
        kernel_test_check(test, false, "a vector is free");
        return;
    }
    asmutils_disable_interrupts();
    kernel_wakeup_accounting_reset();
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        bool took = false;

        if (slot == here || !cpu_topology_is_logical_slot_online(slot))
            continue;
        ++asked;
        armed += wakeup_test_armed_after_one_on(slot, vector, &took) ? 1u : 0u;
        taken += took ? 1u : 0u;
    }
    const uint32_t credited = kernel_wakeup_accounting_get_vector_count(vector);
    kernel_wakeup_accounting_reset();
    asmutils_enable_interrupts();
    device_interrupt_release(vector);

    kernel_test_check(test, taken == asked, "each other processor took the interrupt sent to it");
    kernel_test_check(test, armed == asked, "and this processor's sleep was still armed after each");
    kernel_test_check(test, credited == 0u, "their vector woke nothing here");
    kernel_test_measure(test, "asked", asked);
    kernel_test_measure(test, "credited", credited);
}
