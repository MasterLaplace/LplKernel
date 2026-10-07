#include <kernel/power/frequency_scaling.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(frequency_scaling, KERNEL_TEST_STAGE_BOOTED);

/**
 * @brief The APERF/MPERF ratio reads right on deltas no emulator produces, and a missing counter
 *        pair is reported rather than invented.
 */
KERNEL_TEST(feedback_ratio_reads_right)
{
    kernel_test_check(test, kernel_frequency_scaling_ratio_permille(1000u, 1000u) == 1000u,
                      "equal counters read as nominal");
    kernel_test_check(test, kernel_frequency_scaling_ratio_permille(500u, 1000u) == 500u,
                      "half the counts read as half the clock");
    kernel_test_check(test, kernel_frequency_scaling_ratio_permille(1500u, 1000u) == 1500u,
                      "turbo reads above nominal");
    kernel_test_check(test, kernel_frequency_scaling_ratio_permille(1234u, 0u) == KERNEL_FREQUENCY_SCALING_NO_FEEDBACK,
                      "a reference that never moved is no answer");
    kernel_test_check(test, kernel_frequency_scaling_ratio_permille(UINT64_MAX / 2u, UINT64_MAX / 2u) == 1000u,
                      "a long window does not wrap the multiply");
    kernel_test_check(test,
                      kernel_frequency_scaling_feedback_available() ||
                          kernel_frequency_scaling_measured_permille() == KERNEL_FREQUENCY_SCALING_NO_FEEDBACK,
                      "a missing counter pair is reported, not invented");
    kernel_test_measure(test, "available", kernel_frequency_scaling_feedback_available() ? 1u : 0u);
}
