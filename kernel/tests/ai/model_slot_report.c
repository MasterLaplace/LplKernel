#include <kernel/ai/model_slot.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(model_slot_report, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The fallbacks a boot reports are what the index sort's counter gained across it, a counter that wrapped
 *        included; a boot that brought no mind up reports none when the counter gained nothing, since it may have
 *        indexed no vocabulary, and what the counter gained otherwise.
 *
 * @details No boot takes a fallback while the sort's bound holds, so the counts are made up: the record must still
 *          tell a fallback apart from a boot that counted nothing.
 */
KERNEL_TEST(a_boot_reports_what_the_index_counter_gained_and_none_when_nothing_was_surely_indexed)
{
    kernel_test_check(test, kernel_model_slot_index_fallbacks(5u, 5u, true) == 0u, "a mind up, nothing gained: 0");
    kernel_test_check(test, kernel_model_slot_index_fallbacks(5u, 7u, true) == 2u, "a mind up, two gained: 2");
    kernel_test_check(test, kernel_model_slot_index_fallbacks(UINT32_MAX - 1u, 1u, true) == 3u,
                      "a mind up, the counter wrapped after gaining three: 3");
    kernel_test_check(test, kernel_model_slot_index_fallbacks(5u, 5u, false) == KERNEL_MODEL_SLOT_NOT_COUNTED,
                      "no mind, nothing gained: none");
    kernel_test_check(test, kernel_model_slot_index_fallbacks(5u, 7u, false) == 2u, "no mind, two gained: 2");
}
