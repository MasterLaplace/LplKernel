#include <kernel/cpu/irq.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/wakeup_accounting.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(processor_sleep, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief Spins, boundedly, until the periodic tick advances.
 *
 * @return true when a tick was seen, which means an interrupt can end a sleep.
 */
static bool processor_sleep_test_wait_for_a_tick(void)
{
    const uint32_t ticks_before = interrupt_request_get_tick_count();

    for (uint32_t spin = 0u; spin < 1000000u; ++spin)
    {
        if (interrupt_request_get_tick_count() != ticks_before)
            return true;
    }
    return false;
}

/**
 * @brief MWAIT hints are read out of CPUID one state deep, and a depth nothing enumerates is lowered.
 *
 * @details CPUID.05H:EDX reports C1 in bits 7:4 while MWAIT's hint 0 targets C1, so hint h is
 *          enumerated when nibble h+1 is set. What this processor enumerates is measured, not
 *          checked: QEMU advertises no MWAIT.
 */
KERNEL_TEST(hints_are_read_one_state_deep)
{
    const uint32_t clamped_before = kernel_processor_sleep_clamped_count();
    const bool deepest_granted = kernel_processor_sleep_request_hint(PROCESSOR_SLEEP_HINT_MAX);
    const uint32_t available = kernel_processor_sleep_available_hints();

    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00000000u) == 0u, "nothing enumerated, no hint");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00000020u) == 0x1u,
                      "hint 0 targets C1, not the nibble it sits in");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00002220u) == 0x7u,
                      "three enumerated states give three hints");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00000002u) == 0u, "C0 is not a sleep state");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00200020u) == 0x11u,
                      "a gap in the enumeration stays a gap");
    kernel_test_check(test, deepest_granted || kernel_processor_sleep_clamped_count() == clamped_before + 1u,
                      "a depth nothing enumerates is lowered and counted, not passed");
    kernel_test_check(test,
                      available == 0u ? kernel_processor_sleep_active_hint() == PROCESSOR_SLEEP_HINT_C1 :
                                        ((available >> kernel_processor_sleep_active_hint()) & 1u) == 1u,
                      "the active hint is one the processor enumerates");
    kernel_test_check(test,
                      kernel_processor_sleep_request_hint(PROCESSOR_SLEEP_HINT_C1) &&
                          kernel_processor_sleep_active_hint() == PROCESSOR_SLEEP_HINT_C1,
                      "C1 is always askable");
    kernel_test_measure(test, "available", available);
    kernel_test_measure(test, "interrupt_break", kernel_processor_sleep_has_interrupt_break() ? 1u : 0u);
}

/**
 * @brief A wait on a word that already moved is skipped, one on a word that did not really sleeps,
 *        and whatever ended that sleep is named.
 *
 * @details The real sleep needs the tick to end it, so the tick is checked first: a test that can
 *          hang is worse than one that fails.
 */
KERNEL_TEST(sleep_until_a_write)
{
    static volatile uint32_t watched = 0u;
    const uint32_t sleeps_before = kernel_processor_sleep_count();
    const uint32_t skips_before = kernel_processor_sleep_skipped_count();

    kernel_test_check(test, keyboard_get_ring_head_address() != NULL,
                      "there is an index an interrupt handler advances");

    watched = 1u;
    kernel_test_check(test,
                      processor_sleep_until_write(&watched, 0u) == PROCESSOR_SLEEP_NONE &&
                          kernel_processor_sleep_skipped_count() == skips_before + 1u &&
                          kernel_processor_sleep_count() == sleeps_before,
                      "a wait already over is skipped, not entered");

    if (!kernel_test_check(test, processor_sleep_test_wait_for_a_tick(), "the periodic tick runs to end a sleep"))
        return;

    const uint32_t attributed_before = kernel_wakeup_accounting_get_attributed_count();
    const uint32_t entered_before = kernel_processor_sleep_count();
    const ProcessorSleepMode_t slept = processor_sleep_until_write(&watched, 1u);

    kernel_test_check(test, slept != PROCESSOR_SLEEP_NONE && kernel_processor_sleep_count() == entered_before + 1u,
                      "an unmoved word really sleeps");
    kernel_test_check(test, kernel_wakeup_accounting_get_attributed_count() == attributed_before + 1u,
                      "the sleep it entered was attributed to a source");
}
