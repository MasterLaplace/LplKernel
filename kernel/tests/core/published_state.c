#include <kernel/core/reconciler.h>
#include <kernel/diag/telemetry.h>
#include <kernel/memory/backpressure.h>
#include <kernel/memory/section_protection.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(published_state, KERNEL_TEST_STAGE_PUBLISHED);

/**
 * @brief Every telemetry record written so far reads one way only: no key repeated, no value that
 *        needed a character replaced, no field dropped.
 *
 * @details A repeated key, or a value holding a space, makes a field mean two things to whoever
 *          reads the line. The suite runs after the live checks' records and the telemetry summary,
 *          so every record of the boot is covered.
 */
KERNEL_TEST(telemetry_records_read_one_way)
{
    kernel_test_check(test, kernel_telemetry_get_record_count() > 0u, "the boot wrote telemetry records");
    kernel_test_check(test, kernel_telemetry_get_duplicate_key_count() == 0u, "no record repeats a key");
    kernel_test_check(test, kernel_telemetry_get_sanitised_character_count() == 0u, "no value needed sanitising");
    kernel_test_check(test, kernel_telemetry_get_dropped_field_count() == 0u, "no field was dropped");
    kernel_test_measure(test, "records", kernel_telemetry_get_record_count());
}

/**
 * @brief Code and constants are read-only, and the processor enforces it in ring 0.
 *
 * @details Write protection is checked apart from the pass: with CR0.WP clear the page bits read the
 *          same and stop nothing.
 */
KERNEL_TEST(code_and_constants_are_read_only)
{
    kernel_test_check(test, kernel_section_protection_is_active(), "section protection was applied");
    kernel_test_check(test, kernel_section_protection_write_protect_is_enabled(),
                      "CR0.WP enforces read-only pages in ring 0");
    kernel_test_check(test, kernel_section_protection_get_read_only_page_count() > 0u, "some pages are read-only");
    kernel_test_measure(test, "read_only_pages", kernel_section_protection_get_read_only_page_count());
}

/**
 * @brief The declaration still holds after every test, the periodic tick kept checking it, and no
 *        bounded queue lost what it must not lose.
 *
 * @details Liveness is what gives "no drift" its meaning: a reconciler that never ran reports none
 *          either. The wait is the one the reconciler's report makes, so the claim does not depend on
 *          how fast the machine reached this point.
 */
KERNEL_TEST(the_reconciler_kept_the_declaration)
{
    kernel_test_check(test, kernel_reconciler_is_declared(), "the kernel declared its contract");
    kernel_test_check(test, kernel_reconciler_wait_for_periodic_pass(), "the periodic tick took a pass");
    kernel_test_check(test, kernel_reconciler_get_drift_count() == 0u && kernel_reconciler_get_drift_mask() == 0u,
                      "no invariant ever drifted");
    kernel_test_check(test,
                      kernel_processor_sleep_published_duty_cycle_permille() != KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED,
                      "the power floor's session was judged");
    kernel_test_check(test, kernel_backpressure_get_queue_count() > 0u, "bounded queues are declared");
    kernel_test_check(test, kernel_backpressure_get_intolerant_drop_count() == 0u,
                      "nothing was lost on a queue where loss corrupts");
    kernel_test_measure(test, "tick_passes", kernel_reconciler_get_periodic_pass_count());
    kernel_test_measure(test, "queues", kernel_backpressure_get_queue_count());
}
