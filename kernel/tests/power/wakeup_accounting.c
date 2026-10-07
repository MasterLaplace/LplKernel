#include <kernel/lib/asmutils.h>
#include <kernel/power/wakeup_accounting.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(wakeup_accounting, KERNEL_TEST_STAGE_BOOTED);

/** Vectors no line delivers: crediting a real one would mix this test with the boot's wake-ups. */
#define WAKEUP_TEST_VECTOR_THAT_WOKE  0xFEu
#define WAKEUP_TEST_VECTOR_THAT_DIDNT 0xFDu

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
