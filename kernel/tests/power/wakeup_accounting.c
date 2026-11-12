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
 * @brief The first online slot other than @p here, or CPU_TOPOLOGY_NO_PROCESSOR.
 */
static uint32_t wakeup_test_other_online_slot(uint32_t here)
{
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        if (slot != here && cpu_topology_is_logical_slot_online(slot))
            return slot;
    return CPU_TOPOLOGY_NO_PROCESSOR;
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
 * @brief An interrupt another processor takes leaves this processor's sleep armed: the books are this processor's.
 *
 * @details Every processor dispatches its interrupts through the same routine, and the accounting it feeds is the
 *          bootstrap processor's. An application processor's interrupt taken while the bootstrap processor sleeps
 *          disarmed it, so that a port read that did not stop read as interrupted, and the probe that validates a
 *          port depth would have kept it. Interrupts stay masked here, so that only the other processor can take one.
 */
KERNEL_TEST(an_interrupt_another_processor_takes_leaves_this_processor_s_sleep_armed)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    const uint32_t other = wakeup_test_other_online_slot(here);
    InterruptRequestDeadline_t deadline;
    uint8_t vector = 0u;
    bool armed_after = false;

    if (other == CPU_TOPOLOGY_NO_PROCESSOR)
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
    wakeup_test_taken_on = CPU_TOPOLOGY_NO_PROCESSOR;
    asmutils_disable_interrupts();
    kernel_wakeup_accounting_reset();
    kernel_wakeup_accounting_arm();
    (void) advanced_pic_ipi_send_fixed(cpu_topology_get_apic_id_at_slot(other), vector, ADVANCED_PIC_IPI_SHORT_NONE);
    interrupt_request_deadline_start(&deadline, WAKEUP_TEST_ANSWER_MICROSECONDS);
    while (wakeup_test_taken_on == CPU_TOPOLOGY_NO_PROCESSOR && !interrupt_request_deadline_passed(&deadline))
        asmutils_pause();
    armed_after = kernel_wakeup_accounting_is_armed();
    const uint32_t credited = kernel_wakeup_accounting_get_vector_count(vector);
    kernel_wakeup_accounting_reset();
    asmutils_enable_interrupts();
    device_interrupt_release(vector);

    kernel_test_check(test, wakeup_test_taken_on == other, "the other processor took the interrupt");
    kernel_test_check(test, armed_after, "this processor's sleep is still armed");
    kernel_test_check(test, credited == 0u, "its vector woke nothing here");
    kernel_test_measure(test, "taken_on", wakeup_test_taken_on);
    kernel_test_measure(test, "credited", credited);
}
