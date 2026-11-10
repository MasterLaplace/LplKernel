#include <kernel/power/wake_latency.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(wake_latency, KERNEL_TEST_STAGE_BOOTED);

/** @name How far a lead may stand from RFC 6298's exact value, the integers flooring at each step
 *  @{ */
#define WAKE_LATENCY_TEST_ROUNDING_CYCLES 2u    /**< At least this many cycles. */
#define WAKE_LATENCY_TEST_ROUNDING_SHARE  1024u /**< Or this fraction of the value, the steps' floors adding up. */
/** @} */

#define WAKE_LATENCY_TEST_STEADY       800u /**< The latency of the steady wake-ups the tests start from, in cycles. */
#define WAKE_LATENCY_TEST_STEADY_WAKES 4u   /**< Steady wake-ups learned before a test changes the latency. */

/**
 * @brief Whether @p lead stands within the integers' rounding of @p exact, RFC 6298's value in exact fractions.
 * @param lead  The lead the estimate gives.
 * @param exact The exact lead, rounded down, computed apart from the kernel.
 * @return true when they agree.
 */
static bool wake_latency_test_near(uint64_t lead, uint64_t exact)
{
    const uint64_t distance = lead > exact ? lead - exact : exact - lead;
    const uint64_t share = exact / WAKE_LATENCY_TEST_ROUNDING_SHARE;

    return distance <= (share > WAKE_LATENCY_TEST_ROUNDING_CYCLES ? share : WAKE_LATENCY_TEST_ROUNDING_CYCLES);
}

/**
 * @brief Teaches @p estimate @p count wake-ups of @p latency cycles each.
 * @param estimate The estimate.
 * @param latency  Each wake-up's latency.
 * @param count    How many.
 */
static void wake_latency_test_learn(KernelWakeLatency_t *estimate, uint64_t latency, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
        kernel_wake_latency_learn(estimate, latency);
}

/**
 * @brief An estimate that learned nothing leads by nothing, so that a sleep arms its deadline where it did before.
 */
KERNEL_TEST(an_estimate_that_learned_nothing_leads_by_nothing)
{
    const KernelWakeLatency_t estimate = {0};

    kernel_test_check(test, kernel_wake_latency_lead(&estimate) == 0u, "no wake-up learned, no lead");
}

/**
 * @brief The first wake-up sets the lead to four times its latency, RFC 6298's start with six deviations of half of
 *        it, and steady wake-ups bring the lead down toward the latency by a quarter of the deviation each.
 *
 * @details 3200, 2600, 2150 and 1812.5 cycles: 800 plus six times 400, then times three quarters at each wake-up.
 */
KERNEL_TEST(steady_wake_ups_bring_the_lead_down_toward_their_latency)
{
    static const uint64_t exact[WAKE_LATENCY_TEST_STEADY_WAKES] = {3200u, 2600u, 2150u, 1812u};
    KernelWakeLatency_t estimate = {0};
    uint32_t matched = 0u;

    for (uint32_t index = 0u; index < WAKE_LATENCY_TEST_STEADY_WAKES; ++index)
    {
        kernel_wake_latency_learn(&estimate, WAKE_LATENCY_TEST_STEADY);
        if (wake_latency_test_near(kernel_wake_latency_lead(&estimate), exact[index]))
            ++matched;
    }

    kernel_test_check(test, matched == WAKE_LATENCY_TEST_STEADY_WAKES, "each lead is RFC 6298's, six deviations wide");
    kernel_test_check(test, estimate.samples == WAKE_LATENCY_TEST_STEADY_WAKES, "and each wake-up was learned");
    kernel_test_measure(test, "lead_cycles", (uint32_t) kernel_wake_latency_lead(&estimate));
}

/**
 * @brief One wake-up held 125 times as long as the steady ones counts as twice the lead, so it raises the lead to
 *        about 6148 cycles instead of about 162760, and the deviation is measured from the smoothed latency before
 *        that latency moves.
 */
KERNEL_TEST(a_wake_up_held_far_past_the_lead_moves_it_a_bounded_step)
{
    KernelWakeLatency_t estimate = {0};

    wake_latency_test_learn(&estimate, WAKE_LATENCY_TEST_STEADY, WAKE_LATENCY_TEST_STEADY_WAKES);
    kernel_wake_latency_learn(&estimate, 125u * WAKE_LATENCY_TEST_STEADY);
    const uint64_t lead = kernel_wake_latency_lead(&estimate);

    kernel_test_check(test, wake_latency_test_near(lead, 6148u),
                      "the held wake-up counts as twice the lead, the deviation taken before the mean moves");
    kernel_test_measure(test, "lead_cycles", (uint32_t) lead);
}

/**
 * @brief A latency that grows 25 times and stays there is covered by the lead from its second wake-up on: 23006
 *        cycles against 20000.
 */
KERNEL_TEST(a_lasting_change_of_latency_is_caught_within_two_wake_ups)
{
    KernelWakeLatency_t estimate = {0};

    wake_latency_test_learn(&estimate, WAKE_LATENCY_TEST_STEADY, WAKE_LATENCY_TEST_STEADY_WAKES);
    kernel_wake_latency_learn(&estimate, 25u * WAKE_LATENCY_TEST_STEADY);
    const uint64_t first = kernel_wake_latency_lead(&estimate);
    kernel_wake_latency_learn(&estimate, 25u * WAKE_LATENCY_TEST_STEADY);
    const uint64_t second = kernel_wake_latency_lead(&estimate);

    kernel_test_check(test, first < 25u * WAKE_LATENCY_TEST_STEADY && wake_latency_test_near(second, 23006u),
                      "the first wake-up of the new latency is late, the second already covered");
    kernel_test_measure(test, "second_lead_cycles", (uint32_t) second);
}

/**
 * @brief Wake-ups the timer fired at or before their deadline teach a latency of zero, and a lead of zero does not
 *        hold back the next wake-up that shows a real latency: 0 then 1300 cycles.
 */
KERNEL_TEST(a_lead_of_zero_takes_the_next_latency_in_full)
{
    KernelWakeLatency_t estimate = {0};

    kernel_wake_latency_learn(&estimate, 0u);
    const uint64_t after_zero = kernel_wake_latency_lead(&estimate);
    const uint32_t learned = estimate.samples;
    kernel_wake_latency_learn(&estimate, WAKE_LATENCY_TEST_STEADY);

    kernel_test_check(test, after_zero == 0u && learned == 1u, "a wake-up at its deadline teaches a zero lead");
    kernel_test_check(test, wake_latency_test_near(kernel_wake_latency_lead(&estimate), 1300u),
                      "and the next real latency counts in full");
}

/**
 * @brief A lead assumed before any wake-up is the one given, and the first wake-up learned replaces it, counted at most
 *        as twice it: 800 cycles after 5000 assumed set the lead to 3200, four times them, but after 300 assumed they
 *        count as 600, and set it to 2400. A lead assumed after a wake-up changes nothing.
 */
KERNEL_TEST(an_assumed_lead_holds_until_the_first_wake_up_which_it_bounds)
{
    KernelWakeLatency_t roomy = {0};
    KernelWakeLatency_t tight = {0};

    kernel_wake_latency_assume(&roomy, 5000u);
    kernel_wake_latency_assume(&tight, 300u);
    const uint64_t assumed = kernel_wake_latency_lead(&roomy);
    kernel_wake_latency_learn(&roomy, WAKE_LATENCY_TEST_STEADY);
    kernel_wake_latency_learn(&tight, WAKE_LATENCY_TEST_STEADY);
    const uint64_t learned = kernel_wake_latency_lead(&roomy);
    kernel_wake_latency_assume(&roomy, 5000u);

    kernel_test_check(test, assumed == 5000u, "before any wake-up, the lead is the one assumed");
    kernel_test_check(
        test, wake_latency_test_near(learned, 3200u) && wake_latency_test_near(kernel_wake_latency_lead(&tight), 2400u),
        "the first wake-up replaces it, counted at most as twice it");
    kernel_test_check(test, kernel_wake_latency_lead(&roomy) == learned, "and an assumption after it is ignored");
}

/**
 * @brief A latency past KERNEL_WAKE_LATENCY_LONGEST_CYCLES counts as that many cycles, so that no term wraps: a first
 *        one sets the lead to four times it, and a second to 3.25 times it, its deviation falling by a quarter.
 */
KERNEL_TEST(a_latency_past_the_longest_counts_as_the_longest)
{
    KernelWakeLatency_t estimate = {0};

    kernel_wake_latency_learn(&estimate, UINT64_MAX);
    const uint64_t first = kernel_wake_latency_lead(&estimate);
    kernel_wake_latency_learn(&estimate, UINT64_MAX);

    kernel_test_check(test, first == 4ull * KERNEL_WAKE_LATENCY_LONGEST_CYCLES, "the first counts as the longest");
    kernel_test_check(test, kernel_wake_latency_lead(&estimate) == 13ull * (KERNEL_WAKE_LATENCY_LONGEST_CYCLES / 4u),
                      "and so does the second, with nothing wrapped");
}

/**
 * @brief An estimate that has counted as many wake-ups as its counter holds keeps learning as before rather than
 *        start over: from the state of one 800-cycle wake-up, two more give 2150 cycles, not the 3200 of a first.
 */
KERNEL_TEST(a_full_count_of_wake_ups_keeps_learning)
{
    KernelWakeLatency_t estimate = {0};

    kernel_wake_latency_learn(&estimate, WAKE_LATENCY_TEST_STEADY);
    estimate.samples = UINT32_MAX;
    wake_latency_test_learn(&estimate, WAKE_LATENCY_TEST_STEADY, 2u);

    kernel_test_check(
        test, wake_latency_test_near(kernel_wake_latency_lead(&estimate), 2150u) && estimate.samples == UINT32_MAX,
        "the count stays full and the lead goes on falling");
}

/**
 * @brief Latencies of sleeps of increasing length, the short ones polled back at once, are learned from the longest
 *        sleep: 1, 1, 100 and 1000 cycles give a lead of about 4865, where learned from the shortest, each held to
 *        twice the lead the ones before taught, they give about 38.
 */
KERNEL_TEST(sleeps_of_increasing_length_are_learned_from_the_longest)
{
    static const uint64_t by_length[4] = {1u, 1u, 100u, 1000u};
    KernelWakeLatency_t longest_first = {0};
    KernelWakeLatency_t shortest_first = {0};

    kernel_wake_latency_learn_from_the_longest(&longest_first, by_length, 4u);
    for (uint32_t index = 0u; index < 4u; ++index)
        kernel_wake_latency_learn(&shortest_first, by_length[index]);

    kernel_test_check(test, wake_latency_test_near(kernel_wake_latency_lead(&longest_first), 4865u),
                      "the longest sleep's latency sets the lead");
    kernel_test_check(test, kernel_wake_latency_lead(&shortest_first) < 100u,
                      "the control: from the shortest, the lead never reaches the long sleeps' latency");
    kernel_test_check(test, longest_first.samples == 4u, "each is learned once");
    kernel_test_measure(test, "lead_cycles", (uint32_t) kernel_wake_latency_lead(&longest_first));
}
