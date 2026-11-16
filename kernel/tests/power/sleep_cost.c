#include <kernel/power/sleep_cost.h>
#include <kernel/testing/test.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(sleep_cost, KERNEL_TEST_STAGE_BOOTED);

#define SLEEP_COST_TEST_HERTZ            3000000000ull /**< A 3 GHz timestamp counter. */
#define SLEEP_COST_TEST_CYCLES_PER_MILLI 3000000ull    /**< Its cycles in a millisecond. */
#define SLEEP_COST_TEST_SERIES_NS        128000000ull  /**< Each made-up series lasts 128 ms. */
#define SLEEP_COST_TEST_UNIT_NJ          61035ull      /**< The counter's unit at ESU 14, rounded down. */
#define SLEEP_COST_TEST_C1               0u            /**< The made-up ladder's C1. */
#define SLEEP_COST_TEST_C6               1u            /**< Its C6. */
#define SLEEP_COST_TEST_C10              2u            /**< Its C10. */
#define SLEEP_COST_TEST_DEPTHS           3u            /**< Its depths. */
#define SLEEP_COST_TEST_C1_MICROWATTS    1200000u      /**< C1 at 1.2 W. */
#define SLEEP_COST_TEST_C6_MICROWATTS    400000u       /**< C6 at 0.4 W. */
#define SLEEP_COST_TEST_C6_NANOJOULES    150000ull     /**< C6 at 0.15 mJ a sleep. */
#define SLEEP_COST_TEST_C10_MICROWATTS   100000u       /**< C10 at 0.1 W. */
#define SLEEP_COST_TEST_C10_NANOJOULES   900000ull     /**< C10 at 0.9 mJ a sleep. */
#define SLEEP_COST_TEST_C6_RESIDENCY_US  200ull        /**< C6's residency, Skylake's in intel_idle. */
#define SLEEP_COST_TEST_C10_RESIDENCY_US 5000ull       /**< C10's. */
#define SLEEP_COST_TEST_REPLAYED_SLEEPS  12u           /**< Sleeps of the made-up session. */

/**
 * @brief The energy of @p sleeps sleeps over @p nanoseconds at @p microwatts and @p nanojoules a sleep: n * E + P * T,
 *        P * T being microwatts times nanoseconds over 10^6.
 */
static uint64_t sleep_cost_test_energy(uint32_t sleeps, uint64_t nanoseconds, uint32_t microwatts, uint64_t nanojoules)
{
    return (uint64_t) sleeps * nanojoules + ((uint64_t) microwatts * nanoseconds) / 1000000ull;
}

/**
 * @brief Three series at C6's costs: 64 sleeps, 4 sleeps and 16 sleeps, each over 128 ms.
 */
static void sleep_cost_test_c6_series(KernelSleepCostSeries_t *series)
{
    static const uint32_t sleeps[KERNEL_SLEEP_COST_SERIES] = {64u, 4u, 16u};

    for (uint32_t index = 0u; index < KERNEL_SLEEP_COST_SERIES; ++index)
    {
        series[index].sleeps = sleeps[index];
        series[index].nanoseconds = SLEEP_COST_TEST_SERIES_NS;
        series[index].nanojoules = sleep_cost_test_energy(sleeps[index], SLEEP_COST_TEST_SERIES_NS,
                                                          SLEEP_COST_TEST_C6_MICROWATTS, SLEEP_COST_TEST_C6_NANOJOULES);
    }
}

/**
 * @brief Costs measured at @p microwatts and @p nanojoules a sleep, on the 3 GHz clock.
 */
static KernelSleepCost_t sleep_cost_test_measured(uint32_t microwatts, uint64_t nanojoules)
{
    KernelSleepCost_t cost;

    cost.source = KERNEL_SLEEP_COST_MEASURED;
    cost.power_microwatts = microwatts;
    cost.sleep_nanojoules = nanojoules;
    cost.power_nanojoules_per_mebicycles =
        ((uint64_t) microwatts * 1000ull * (1ull << KERNEL_SLEEP_COST_CYCLE_SHIFT)) / SLEEP_COST_TEST_HERTZ;
    return cost;
}

/**
 * @brief The made-up ladder of the PLAN's example, every depth allowed, measured, and a sleep of @p asleep_cycles at
 *        each.
 */
static void sleep_cost_test_ladder(KernelSleepCostCandidate_t *candidates, uint64_t asleep_cycles)
{
    memset(candidates, 0, sizeof(*candidates) * SLEEP_COST_TEST_DEPTHS);
    candidates[SLEEP_COST_TEST_C1].cost = sleep_cost_test_measured(SLEEP_COST_TEST_C1_MICROWATTS, 0u);
    candidates[SLEEP_COST_TEST_C6].cost =
        sleep_cost_test_measured(SLEEP_COST_TEST_C6_MICROWATTS, SLEEP_COST_TEST_C6_NANOJOULES);
    candidates[SLEEP_COST_TEST_C10].cost =
        sleep_cost_test_measured(SLEEP_COST_TEST_C10_MICROWATTS, SLEEP_COST_TEST_C10_NANOJOULES);
    candidates[SLEEP_COST_TEST_C6].residency_cycles = SLEEP_COST_TEST_C6_RESIDENCY_US * 3000ull;
    candidates[SLEEP_COST_TEST_C10].residency_cycles = SLEEP_COST_TEST_C10_RESIDENCY_US * 3000ull;
    for (uint32_t depth = 0u; depth < SLEEP_COST_TEST_DEPTHS; ++depth)
    {
        candidates[depth].allowed = true;
        candidates[depth].asleep_cycles = asleep_cycles;
    }
}

/**
 * @brief Whether @p value lies within @p bound of @p expected.
 */
static bool sleep_cost_test_within(uint64_t value, uint64_t expected, uint64_t bound)
{
    return value >= expected ? value - expected <= bound : expected - value <= bound;
}

/**
 * @brief Whether @p name is @p expected.
 */
static bool sleep_cost_test_named(const char *name, const char *expected)
{
    return strlen(name) == strlen(expected) && memcmp(name, expected, strlen(expected)) == 0;
}

/**
 * @brief Two series of different sleep counts over the same time solve a depth's power and sleep energy exactly when
 *        the counter gives exact energies, the third agreeing; the power per 2^20 cycles follows the clock.
 */
KERNEL_TEST(two_series_solve_a_depth_s_power_and_sleep_energy)
{
    KernelSleepCostSeries_t series[KERNEL_SLEEP_COST_SERIES];
    KernelSleepCost_t cost;

    sleep_cost_test_c6_series(series);
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_MEASURED, "the costs are measured");
    kernel_test_check(test, cost.power_microwatts == SLEEP_COST_TEST_C6_MICROWATTS, "P is 0.4 W");
    kernel_test_check(test, cost.sleep_nanojoules == SLEEP_COST_TEST_C6_NANOJOULES, "E is 0.15 mJ");
    kernel_test_check(test, cost.power_nanojoules_per_mebicycles == 139810u,
                      "0.4 W over 2^20 cycles at 3 GHz is 139810 nJ, rounded down");
}

/**
 * @brief Energies the counter rounds down to its unit move the solution by a bounded step: E by the unit over the 60
 *        sleeps the two series differ by, P by the units over their determinant.
 */
KERNEL_TEST(the_counter_s_quantisation_moves_the_solution_by_a_bounded_step)
{
    KernelSleepCostSeries_t series[KERNEL_SLEEP_COST_SERIES];
    KernelSleepCost_t cost;

    sleep_cost_test_c6_series(series);
    for (uint32_t index = 0u; index < KERNEL_SLEEP_COST_SERIES; ++index)
        series[index].nanojoules -= series[index].nanojoules % SLEEP_COST_TEST_UNIT_NJ;
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_measure(test, "power_microwatts", cost.power_microwatts);
    kernel_test_measure(test, "sleep_nanojoules", (uint32_t) cost.sleep_nanojoules);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_MEASURED, "the costs are measured");
    kernel_test_check(test, sleep_cost_test_within(cost.power_microwatts, SLEEP_COST_TEST_C6_MICROWATTS, 600u),
                      "P within 600 uW");
    kernel_test_check(test, sleep_cost_test_within(cost.sleep_nanojoules, SLEEP_COST_TEST_C6_NANOJOULES, 1100u),
                      "E within 1100 nJ");
}

/**
 * @brief A check series that the solution misses by more than its tolerance refuses the calibration, by name.
 */
KERNEL_TEST(a_check_series_that_disagrees_refuses_the_calibration_as_noisy)
{
    KernelSleepCostSeries_t series[KERNEL_SLEEP_COST_SERIES];
    KernelSleepCost_t cost;

    sleep_cost_test_c6_series(series);
    series[2].nanojoules += series[2].nanojoules / 5u;
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_NOISY, "20 % more energy is noise");

    sleep_cost_test_c6_series(series);
    series[2].nanojoules += series[2].nanojoules / 32u;
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_MEASURED, "3 % more is within 1/16");
}

/**
 * @brief Two series whose sleeps a second are equal, or closer than KERNEL_SLEEP_COST_SEPARATION times, cannot tell
 *        the sleep energy from the power.
 */
KERNEL_TEST(two_series_of_alike_rates_cannot_be_solved)
{
    KernelSleepCostSeries_t series[KERNEL_SLEEP_COST_SERIES];
    KernelSleepCost_t cost;

    sleep_cost_test_c6_series(series);
    series[1].sleeps = 32u;
    series[1].nanoseconds = SLEEP_COST_TEST_SERIES_NS / 2u;
    series[1].nanojoules = sleep_cost_test_energy(32u, series[1].nanoseconds, SLEEP_COST_TEST_C6_MICROWATTS,
                                                  SLEEP_COST_TEST_C6_NANOJOULES);
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_UNSEPARATED, "the same rate solves nothing");

    sleep_cost_test_c6_series(series);
    series[1].sleeps = 48u;
    series[1].nanojoules = sleep_cost_test_energy(48u, SLEEP_COST_TEST_SERIES_NS, SLEEP_COST_TEST_C6_MICROWATTS,
                                                  SLEEP_COST_TEST_C6_NANOJOULES);
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_UNSEPARATED, "64 against 48 sleeps is under twice");

    series[1].sleeps = 32u;
    series[1].nanojoules = sleep_cost_test_energy(32u, SLEEP_COST_TEST_SERIES_NS, SLEEP_COST_TEST_C6_MICROWATTS,
                                                  SLEEP_COST_TEST_C6_NANOJOULES);
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_MEASURED, "64 against 32 sleeps is twice: solved");
}

/**
 * @brief A power of 0 or less, or a sleep energy below 0 past the quantisation, is no depth's: implausible; a sleep
 *        energy below 0 within it is 0, as C1's measures.
 */
KERNEL_TEST(a_power_or_a_sleep_energy_below_zero_is_implausible)
{
    KernelSleepCostSeries_t series[KERNEL_SLEEP_COST_SERIES];
    KernelSleepCost_t cost;

    sleep_cost_test_c6_series(series);
    series[1].nanojoules = 0u;
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_IMPLAUSIBLE, "a power below 0 is implausible");

    sleep_cost_test_c6_series(series);
    series[0].nanojoules = 10000000u;
    series[1].nanojoules = 20000000u;
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_IMPLAUSIBLE, "E at -167 uJ is implausible");

    for (uint32_t index = 0u; index < KERNEL_SLEEP_COST_SERIES; ++index)
    {
        series[index].sleeps = index == 0u ? 64u : (index == 1u ? 4u : 16u);
        series[index].nanoseconds = SLEEP_COST_TEST_SERIES_NS;
        series[index].nanojoules =
            sleep_cost_test_energy(series[index].sleeps, SLEEP_COST_TEST_SERIES_NS, SLEEP_COST_TEST_C1_MICROWATTS, 0u);
    }
    series[0].nanojoules -= 30000u;
    kernel_sleep_cost_estimate(series, 2u * SLEEP_COST_TEST_UNIT_NJ, SLEEP_COST_TEST_HERTZ, &cost);
    kernel_test_check(test, cost.source == KERNEL_SLEEP_COST_MEASURED, "C1 is measured");
    kernel_test_check(test, cost.sleep_nanojoules == 0u, "E at -500 nJ, within the quantisation, is 0");
    kernel_test_check(test, sleep_cost_test_within(cost.power_microwatts, SLEEP_COST_TEST_C1_MICROWATTS, 300u),
                      "P stays 1.2 W");
}

/**
 * @brief A sleep's cost is E * 2^20 plus the power per 2^20 cycles times its cycles, saturated past 64 bits.
 */
KERNEL_TEST(a_sleep_s_cost_is_its_energy_and_its_power_over_its_length)
{
    const KernelSleepCost_t c10 =
        sleep_cost_test_measured(SLEEP_COST_TEST_C10_MICROWATTS, SLEEP_COST_TEST_C10_NANOJOULES);
    const uint64_t thirty_four_ms = 34u * SLEEP_COST_TEST_CYCLES_PER_MILLI;
    const uint64_t cost = kernel_sleep_cost_of(&c10, thirty_four_ms);
    const uint64_t nanojoules = cost >> KERNEL_SLEEP_COST_CYCLE_SHIFT;

    kernel_test_measure(test, "c10_34ms_nanojoules", (uint32_t) nanojoules);
    kernel_test_check(test, sleep_cost_test_within(nanojoules, 4300000u, 100u),
                      "C10 for 34 ms is 4.3 mJ, within the 1 nJ per 2^20 cycles the power rounds down");
    kernel_test_check(test, kernel_sleep_cost_of(&c10, UINT64_MAX) == UINT64_MAX, "a product past 64 bits saturates");
}

/**
 * @brief The example's ladder: a sleep of 34 ms is cheapest at C10, one of 2 ms at C6, one of 50 us at C1; every
 *        allowed depth measured, the costs decide.
 */
KERNEL_TEST(the_cheapest_depth_for_the_sleep_s_length_is_chosen)
{
    KernelSleepCostCandidate_t candidates[SLEEP_COST_TEST_DEPTHS];
    bool by_cost = false;

    sleep_cost_test_ladder(candidates, 34u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    kernel_test_check(test,
                      kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, &by_cost) == SLEEP_COST_TEST_C10,
                      "34 ms: C10, 4.3 mJ against 13.75 and 40.8");
    kernel_test_check(test, by_cost, "the costs decided");
    sleep_cost_test_ladder(candidates, 2u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C6,
                      "2 ms: C6, 0.95 mJ against 1.1 and 2.4");
    sleep_cost_test_ladder(candidates, 150000u);
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C1,
                      "50 us: C1, 60 uJ against 170 and 905");
}

/**
 * @brief A depth whose lead does not fit, or that the caller does not allow, is never chosen, however cheap; with no
 *        candidate left, no depth.
 */
KERNEL_TEST(a_depth_that_does_not_fit_or_is_not_allowed_is_never_chosen)
{
    KernelSleepCostCandidate_t candidates[SLEEP_COST_TEST_DEPTHS];

    sleep_cost_test_ladder(candidates, 34u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    candidates[SLEEP_COST_TEST_C10].asleep_cycles = 0u;
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C6,
                      "C10's lead does not fit: C6");
    sleep_cost_test_ladder(candidates, 34u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    candidates[SLEEP_COST_TEST_C10].allowed = false;
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C6,
                      "C10 past the ceiling: C6");
    for (uint32_t depth = 0u; depth < SLEEP_COST_TEST_DEPTHS; ++depth)
        candidates[depth].asleep_cycles = 0u;
    kernel_test_check(test,
                      kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == KERNEL_SLEEP_COST_NO_DEPTH,
                      "no sleep lasts: no depth");
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, 0u, NULL) == KERNEL_SLEEP_COST_NO_DEPTH,
                      "no candidate: no depth");
}

/**
 * @brief While one allowed depth's costs are not measured, the rule decides: the deepest whose residency the sleep
 *        outlasts, the shallowest allowed always qualifying, and it says so.
 */
KERNEL_TEST(without_every_cost_measured_the_residency_rule_decides)
{
    KernelSleepCostCandidate_t candidates[SLEEP_COST_TEST_DEPTHS];
    bool by_cost = true;

    sleep_cost_test_ladder(candidates, 2u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    candidates[SLEEP_COST_TEST_C10].cost.source = KERNEL_SLEEP_COST_NO_COUNTER;
    kernel_test_check(test,
                      kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, &by_cost) == SLEEP_COST_TEST_C6,
                      "2 ms outlasts C6's 200 us, not C10's 5 ms");
    kernel_test_check(test, !by_cost, "the rule decided");
    sleep_cost_test_ladder(candidates, 34u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    candidates[SLEEP_COST_TEST_C1].cost.source = KERNEL_SLEEP_COST_NOISY;
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C10,
                      "34 ms outlasts C10's");
    sleep_cost_test_ladder(candidates, 150000u);
    candidates[SLEEP_COST_TEST_C6].cost.source = KERNEL_SLEEP_COST_NOT_CALIBRATED;
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C1,
                      "50 us outlasts no residency: C1");
}

/**
 * @brief Two depths that cost the same for a sleep leave it at the shallower, which wakes sooner.
 */
KERNEL_TEST(a_tie_goes_to_the_shallower_depth)
{
    KernelSleepCostCandidate_t candidates[SLEEP_COST_TEST_DEPTHS];

    sleep_cost_test_ladder(candidates, 2u * SLEEP_COST_TEST_CYCLES_PER_MILLI);
    candidates[SLEEP_COST_TEST_C10].cost = candidates[SLEEP_COST_TEST_C6].cost;
    kernel_test_check(test, kernel_sleep_cost_choose(candidates, SLEEP_COST_TEST_DEPTHS, NULL) == SLEEP_COST_TEST_C6,
                      "C6 and C10 alike: C6");
}

/**
 * @brief The made-up ladder of the PLAN's example as a replay takes it: every depth allowed, measured, its residency,
 *        and leads of 2 us, 100 us and 1 ms.
 */
static void sleep_cost_test_replay_depths(KernelSleepCostReplayDepth_t *depths)
{
    static const uint64_t lead_microseconds[SLEEP_COST_TEST_DEPTHS] = {2u, 100u, 1000u};
    KernelSleepCostCandidate_t candidates[SLEEP_COST_TEST_DEPTHS];

    sleep_cost_test_ladder(candidates, 0u);
    for (uint32_t depth = 0u; depth < SLEEP_COST_TEST_DEPTHS; ++depth)
    {
        depths[depth].allowed = true;
        depths[depth].lead_cycles = lead_microseconds[depth] * 3000u;
        depths[depth].residency_cycles = candidates[depth].residency_cycles;
        depths[depth].cost = candidates[depth].cost;
    }
}

/**
 * @brief A made-up session of short and long sleeps: each window, and the depth the example's costs give it, worked
 *        out by hand (C10 past 2.5 ms of sleep, C6 below it, C10 out where its 1 ms lead does not fit).
 */
static void sleep_cost_test_session(KernelSleepCostRecord_t *records)
{
    static const uint32_t window_microseconds[SLEEP_COST_TEST_REPLAYED_SLEEPS] = {
        34000u, 34000u, 600u, 34000u, 2000u, 34000u, 34000u, 1500u, 34000u, 8000u, 34000u, 900u};
    static const uint32_t depth[SLEEP_COST_TEST_REPLAYED_SLEEPS] = {
        SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C6,  SLEEP_COST_TEST_C10,
        SLEEP_COST_TEST_C6,  SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C6,
        SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C10, SLEEP_COST_TEST_C6};

    for (uint32_t sleep = 0u; sleep < SLEEP_COST_TEST_REPLAYED_SLEEPS; ++sleep)
    {
        records[sleep].window_cycles = (uint64_t) window_microseconds[sleep] * 3000u;
        records[sleep].depth = depth[sleep];
        records[sleep].by_cost = 1u;
    }
}

/**
 * @brief What @p records cost at @p depth for every sleep, or at the depth each records when @p depth is
 *        KERNEL_SLEEP_COST_NO_DEPTH, in nanojoules, from the example's watts and millijoules rather than from cycles: a
 *        sleep lasts its window less the depth's lead, the whole window when the lead does not fit.
 */
static uint64_t sleep_cost_test_session_nanojoules(const KernelSleepCostRecord_t *records, uint32_t depth)
{
    static const uint32_t microwatts[SLEEP_COST_TEST_DEPTHS] = {
        SLEEP_COST_TEST_C1_MICROWATTS, SLEEP_COST_TEST_C6_MICROWATTS, SLEEP_COST_TEST_C10_MICROWATTS};
    static const uint64_t nanojoules[SLEEP_COST_TEST_DEPTHS] = {0u, SLEEP_COST_TEST_C6_NANOJOULES,
                                                                SLEEP_COST_TEST_C10_NANOJOULES};
    static const uint64_t lead_nanoseconds[SLEEP_COST_TEST_DEPTHS] = {2000u, 100000u, 1000000u};
    uint64_t total = 0u;

    for (uint32_t sleep = 0u; sleep < SLEEP_COST_TEST_REPLAYED_SLEEPS; ++sleep)
    {
        const uint32_t at = depth == KERNEL_SLEEP_COST_NO_DEPTH ? records[sleep].depth : depth;
        const uint64_t window = records[sleep].window_cycles / 3u;
        const uint64_t asleep = window > lead_nanoseconds[at] ? window - lead_nanoseconds[at] : window;
        total += sleep_cost_test_energy(1u, asleep, microwatts[at], nanojoules[at]);
    }
    return total;
}

/**
 * @brief A made-up session replayed: choosing each sleep's depth again spends no more than the cheaper of always C1
 *        and always C10 (33.44 mJ against 301.17 and 34.9), and misses no wake-up, where always C10 misses the two its
 *        lead does not fit; each choice made again is the one recorded, and a record altered is told apart.
 *
 * @details The costs are compared in nanojoules within 1 uJ, what rounding the power to nanojoules per 2^20 cycles
 *          moves over the session.
 */
KERNEL_TEST(a_replayed_session_spends_no_more_than_the_cheaper_fixed_depth_and_misses_nothing)
{
    KernelSleepCostRecord_t records[SLEEP_COST_TEST_REPLAYED_SLEEPS];
    KernelSleepCostReplayDepth_t depths[SLEEP_COST_TEST_DEPTHS];
    KernelSleepCostReplay_t replay;

    sleep_cost_test_session(records);
    sleep_cost_test_replay_depths(depths);
    kernel_sleep_cost_replay(records, SLEEP_COST_TEST_REPLAYED_SLEEPS, depths, SLEEP_COST_TEST_DEPTHS, &replay);
    const uint64_t chosen = replay.chosen_cost >> KERNEL_SLEEP_COST_CYCLE_SHIFT;
    const uint64_t shallowest = replay.shallowest_cost >> KERNEL_SLEEP_COST_CYCLE_SHIFT;
    const uint64_t deepest = replay.deepest_cost >> KERNEL_SLEEP_COST_CYCLE_SHIFT;
    kernel_test_measure(test, "chosen_microjoules", (uint32_t) (chosen / 1000u));
    kernel_test_measure(test, "always_c1_microjoules", (uint32_t) (shallowest / 1000u));
    kernel_test_measure(test, "always_deepest_microjoules", (uint32_t) (deepest / 1000u));
    kernel_test_measure(test, "always_deepest_misses", replay.deepest_misses);
    kernel_test_check(
        test,
        sleep_cost_test_within(chosen, sleep_cost_test_session_nanojoules(records, KERNEL_SLEEP_COST_NO_DEPTH), 1000u),
        "the depths chosen cost what the example's watts and millijoules give");
    kernel_test_check(
        test,
        sleep_cost_test_within(shallowest, sleep_cost_test_session_nanojoules(records, SLEEP_COST_TEST_C1), 1000u),
        "always C1 costs what they give");
    kernel_test_check(
        test, sleep_cost_test_within(deepest, sleep_cost_test_session_nanojoules(records, SLEEP_COST_TEST_C10), 1000u),
        "always C10 costs what they give, asleep the whole window where its lead does not fit");
    kernel_test_check(test, replay.chosen_cost <= replay.shallowest_cost && replay.chosen_cost <= replay.deepest_cost,
                      "no more than either fixed depth");
    kernel_test_check(test, replay.chosen_misses == 0u && replay.shallowest_misses == 0u, "no wake-up missed");
    kernel_test_check(test, replay.deepest_misses == 2u,
                      "always C10 misses the 600 and 900 us sleeps its 1 ms lead outlasts");
    kernel_test_check(test, replay.agreements == SLEEP_COST_TEST_REPLAYED_SLEEPS, "each choice is the one recorded");
    records[2].depth = SLEEP_COST_TEST_C1;
    kernel_sleep_cost_replay(records, SLEEP_COST_TEST_REPLAYED_SLEEPS, depths, SLEEP_COST_TEST_DEPTHS, &replay);
    kernel_test_check(test, replay.agreements == SLEEP_COST_TEST_REPLAYED_SLEEPS - 1u, "an altered record disagrees");
}

/**
 * @brief A replay where no depth is allowed misses every sleep at all three, costed nothing, and agrees with none.
 */
KERNEL_TEST(a_replay_with_no_depth_allowed_misses_every_sleep)
{
    KernelSleepCostRecord_t records[SLEEP_COST_TEST_REPLAYED_SLEEPS];
    KernelSleepCostReplayDepth_t depths[SLEEP_COST_TEST_DEPTHS];
    KernelSleepCostReplay_t replay = {1u, 1u, 1u, 0u, 0u, 0u, 1u};

    sleep_cost_test_session(records);
    sleep_cost_test_replay_depths(depths);
    for (uint32_t depth = 0u; depth < SLEEP_COST_TEST_DEPTHS; ++depth)
        depths[depth].allowed = false;
    kernel_sleep_cost_replay(records, SLEEP_COST_TEST_REPLAYED_SLEEPS, depths, SLEEP_COST_TEST_DEPTHS, &replay);
    kernel_test_check(test,
                      replay.chosen_misses == SLEEP_COST_TEST_REPLAYED_SLEEPS &&
                          replay.shallowest_misses == SLEEP_COST_TEST_REPLAYED_SLEEPS &&
                          replay.deepest_misses == SLEEP_COST_TEST_REPLAYED_SLEEPS,
                      "every sleep missed at all three");
    kernel_test_check(test, replay.chosen_cost == 0u && replay.shallowest_cost == 0u && replay.deepest_cost == 0u,
                      "costed nothing");
    kernel_test_check(test, replay.agreements == 0u, "agreeing with none");
}

/**
 * @brief A replay told of more sleeps than the record keeps replays the KERNEL_SLEEP_COST_RECORDED it kept, and one
 *        given more depths than KERNEL_SLEEP_COST_DEPTHS leaves the deeper ones out.
 */
KERNEL_TEST(a_replay_takes_no_more_sleeps_than_kept_nor_more_depths_than_it_holds)
{
    KernelSleepCostRecord_t records[KERNEL_SLEEP_COST_RECORDED];
    KernelSleepCostReplayDepth_t ladder[SLEEP_COST_TEST_DEPTHS];
    KernelSleepCostReplayDepth_t depths[KERNEL_SLEEP_COST_DEPTHS + 1u];
    KernelSleepCostReplay_t past_the_record = {0u, 0u, 0u, 0u, 0u, 0u, 0u};
    KernelSleepCostReplay_t past_the_depths = {0u, 0u, 0u, 0u, 0u, 0u, 0u};

    for (uint32_t sleep = 0u; sleep < KERNEL_SLEEP_COST_RECORDED; ++sleep)
    {
        records[sleep].window_cycles = 34000u * 3000u;
        records[sleep].depth = SLEEP_COST_TEST_C1;
        records[sleep].by_cost = 1u;
    }
    sleep_cost_test_replay_depths(ladder);
    for (uint32_t depth = 0u; depth < SLEEP_COST_TEST_DEPTHS; ++depth)
        ladder[depth].allowed = false;
    kernel_sleep_cost_replay(records, KERNEL_SLEEP_COST_RECORDED + 6u, ladder, SLEEP_COST_TEST_DEPTHS,
                             &past_the_record);
    for (uint32_t depth = 0u; depth <= KERNEL_SLEEP_COST_DEPTHS; ++depth)
    {
        depths[depth] = ladder[SLEEP_COST_TEST_C1];
        depths[depth].allowed = depth == KERNEL_SLEEP_COST_DEPTHS;
    }
    kernel_sleep_cost_replay(records, KERNEL_SLEEP_COST_RECORDED, depths, KERNEL_SLEEP_COST_DEPTHS + 1u,
                             &past_the_depths);
    kernel_test_check(test,
                      past_the_record.chosen_misses == KERNEL_SLEEP_COST_RECORDED &&
                          past_the_record.shallowest_misses == KERNEL_SLEEP_COST_RECORDED &&
                          past_the_record.deepest_misses == KERNEL_SLEEP_COST_RECORDED,
                      "six sleeps told past the record, only those it kept replayed");
    kernel_test_check(test,
                      past_the_depths.chosen_misses == KERNEL_SLEEP_COST_RECORDED &&
                          past_the_depths.shallowest_misses == KERNEL_SLEEP_COST_RECORDED &&
                          past_the_depths.deepest_misses == KERNEL_SLEEP_COST_RECORDED,
                      "the one depth allowed, past those a replay takes, left out");
}

/**
 * @brief A sleep from now lasts to its wake-up, capped by the longest sleep, none for a wake-up not after now, and
 *        without wrapping where now plus the longest sleep would pass 64 bits.
 */
KERNEL_TEST(a_sleep_lasts_to_its_wake_up_capped_by_the_longest_sleep)
{
    kernel_test_check(test, kernel_sleep_cost_cycles_until(1000u, 1200u, 500u) == 200u,
                      "a wake-up within the longest sleep: 200 cycles");
    kernel_test_check(test, kernel_sleep_cost_cycles_until(1000u, 1500u, 500u) == 500u,
                      "a wake-up as far as the longest sleep: all 500");
    kernel_test_check(test, kernel_sleep_cost_cycles_until(1000u, UINT64_MAX, 500u) == 500u,
                      "no wake-up planned: the longest sleep");
    kernel_test_check(test,
                      kernel_sleep_cost_cycles_until(1000u, 1000u, 500u) == 0u &&
                          kernel_sleep_cost_cycles_until(1000u, 900u, 500u) == 0u,
                      "a wake-up now or past: none");
    kernel_test_check(test, kernel_sleep_cost_cycles_until(UINT64_MAX - 10u, UINT64_MAX, 1000u) == 10u,
                      "near the top of 64 bits, 10 cycles, no wrap");
}

/**
 * @brief Each source has its own name, and a value past them has none.
 */
KERNEL_TEST(every_source_has_its_name)
{
    kernel_test_check(
        test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_NOT_CALIBRATED), "not_calibrated"),
        "not_calibrated");
    kernel_test_check(test,
                      sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_NO_COUNTER), "no_counter"),
                      "no_counter");
    kernel_test_check(
        test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_MEASURED), "measured"), "measured");
    kernel_test_check(test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_NOISY), "noisy"),
                      "noisy");
    kernel_test_check(
        test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_UNSEPARATED), "unseparated"),
        "unseparated");
    kernel_test_check(
        test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_IMPLAUSIBLE), "implausible"),
        "implausible");
    kernel_test_check(test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_UNREAD), "unread"),
                      "unread");
    kernel_test_check(test, sleep_cost_test_named(kernel_sleep_cost_source_name(KERNEL_SLEEP_COST_UNTIMED), "untimed"),
                      "untimed");
    kernel_test_check(test, sleep_cost_test_named(kernel_sleep_cost_source_name(8u), "unknown"), "a value past them");
}
