#include <kernel/power/sleep_cost.h>

#include <stdckdint.h>
#include <stddef.h>

#define SLEEP_COST_MICROWATTS_PER_WATT 1000000ll /**< P solves in microjoules per microsecond, watts. */
#define SLEEP_COST_NANO_PER_MICRO      1000ll    /**< Nanoseconds a microsecond, nanojoules a microjoule. */

/**
 * @brief How long @p series lasted, in whole microseconds.
 */
static int64_t sleep_cost_microseconds(const KernelSleepCostSeries_t *series)
{
    return (int64_t) (series->nanoseconds / (uint64_t) SLEEP_COST_NANO_PER_MICRO);
}

/**
 * @brief The energy of @p series, in whole microjoules.
 */
static int64_t sleep_cost_microjoules(const KernelSleepCostSeries_t *series)
{
    return (int64_t) (series->nanojoules / (uint64_t) SLEEP_COST_NANO_PER_MICRO);
}

/**
 * @brief (@p a * @p b - @p c * @p d) * @p scale into @p out.
 * @return false when a step leaves 64 bits.
 */
static bool sleep_cost_cross_difference(int64_t a, int64_t b, int64_t c, int64_t d, int64_t scale, int64_t *out)
{
    int64_t left = 0;
    int64_t right = 0;
    int64_t difference = 0;

    return !ckd_mul(&left, a, b) && !ckd_mul(&right, c, d) && !ckd_sub(&difference, left, right) &&
           !ckd_mul(out, difference, scale);
}

/**
 * @brief Whether the sleeps a second of @p first and @p second differ by KERNEL_SLEEP_COST_SEPARATION times at least:
 *        n1 * T2 against n2 * T1, in microseconds, the products of a determinant already checked to fit 64 bits.
 */
static bool sleep_cost_separated(const KernelSleepCostSeries_t *first, const KernelSleepCostSeries_t *second)
{
    const int64_t first_rate = (int64_t) first->sleeps * sleep_cost_microseconds(second);
    const int64_t second_rate = (int64_t) second->sleeps * sleep_cost_microseconds(first);
    const int64_t higher = first_rate > second_rate ? first_rate : second_rate;
    const int64_t lower = first_rate > second_rate ? second_rate : first_rate;

    return higher / (int64_t) KERNEL_SLEEP_COST_SEPARATION >= lower && higher != lower;
}

/**
 * @brief Solves @p first and @p second for P in microwatts and E in nanojoules, in microseconds and microjoules.
 * @return KERNEL_SLEEP_COST_MEASURED with both written; UNSEPARATED when their sleeps a second are too alike,
 *         IMPLAUSIBLE when a step of the solution leaves 64 bits.
 */
static uint32_t sleep_cost_solve(const KernelSleepCostSeries_t *first, const KernelSleepCostSeries_t *second,
                                 int64_t *microwatts, int64_t *nanojoules)
{
    const int64_t first_microseconds = sleep_cost_microseconds(first);
    const int64_t second_microseconds = sleep_cost_microseconds(second);
    int64_t determinant = 0;
    int64_t energy = 0;
    int64_t power = 0;

    if (!sleep_cost_cross_difference(first->sleeps, second_microseconds, second->sleeps, first_microseconds, 1,
                                     &determinant))
        return KERNEL_SLEEP_COST_IMPLAUSIBLE;
    if (!sleep_cost_separated(first, second))
        return KERNEL_SLEEP_COST_UNSEPARATED;
    if (!sleep_cost_cross_difference(sleep_cost_microjoules(first), second_microseconds, sleep_cost_microjoules(second),
                                     first_microseconds, SLEEP_COST_NANO_PER_MICRO, &energy) ||
        !sleep_cost_cross_difference(first->sleeps, sleep_cost_microjoules(second), second->sleeps,
                                     sleep_cost_microjoules(first), SLEEP_COST_MICROWATTS_PER_WATT, &power))
        return KERNEL_SLEEP_COST_IMPLAUSIBLE;
    *microwatts = power / determinant;
    *nanojoules = energy / determinant;
    return KERNEL_SLEEP_COST_MEASURED;
}

/**
 * @brief Whether @p series measured the energy @p microwatts and @p nanojoules give it, n * E + P * T, within @p
 *        quantisation plus 1/2^KERNEL_SLEEP_COST_CHECK_SHIFT of what it measured; a prediction past 64 bits does not.
 */
static bool sleep_cost_agrees(const KernelSleepCostSeries_t *series, int64_t microwatts, int64_t nanojoules,
                              uint64_t quantisation)
{
    int64_t sleeps_energy = 0;
    int64_t power_energy = 0;
    int64_t predicted = 0;
    int64_t measured = 0;
    int64_t miss = 0;

    if (ckd_mul(&sleeps_energy, series->sleeps, nanojoules) ||
        ckd_mul(&power_energy, microwatts, series->nanoseconds) ||
        ckd_add(&predicted, sleeps_energy, power_energy / SLEEP_COST_MICROWATTS_PER_WATT) ||
        ckd_add(&measured, series->nanojoules, 0) || ckd_sub(&miss, measured, predicted))
        return false;
    return (uint64_t) (miss < 0 ? -miss : miss) <= quantisation + (series->nanojoules >> KERNEL_SLEEP_COST_CHECK_SHIFT);
}

/**
 * @brief Writes @p source into @p out with no costs.
 */
static void sleep_cost_refuse(KernelSleepCost_t *out, uint32_t source)
{
    out->source = source;
    out->power_microwatts = 0u;
    out->sleep_nanojoules = 0u;
    out->power_nanojoules_per_mebicycles = 0u;
}

/**
 * @brief Multiplies @p left by @p right, the largest value for a product past 64 bits.
 */
static uint64_t sleep_cost_saturated_product(uint64_t left, uint64_t right)
{
    uint64_t product = 0u;

    return ckd_mul(&product, left, right) ? UINT64_MAX : product;
}

/**
 * @brief Adds @p left and @p right, the largest value for a sum past 64 bits.
 */
static uint64_t sleep_cost_saturated_sum(uint64_t left, uint64_t right)
{
    uint64_t sum = 0u;

    return ckd_add(&sum, left, right) ? UINT64_MAX : sum;
}

/**
 * @brief Whether candidate @p candidate may be chosen: allowed, and its sleep lasts.
 */
static bool sleep_cost_eligible(const KernelSleepCostCandidate_t *candidate)
{
    return candidate->allowed && candidate->asleep_cycles != 0u;
}

/**
 * @brief Whether every eligible candidate among @p count has measured costs, and one at least is eligible.
 */
static bool sleep_cost_all_measured(const KernelSleepCostCandidate_t *candidates, uint32_t count)
{
    bool any = false;

    for (uint32_t depth = 0u; depth < count; ++depth)
    {
        if (!sleep_cost_eligible(&candidates[depth]))
            continue;
        if (candidates[depth].cost.source != KERNEL_SLEEP_COST_MEASURED)
            return false;
        any = true;
    }
    return any;
}

/**
 * @brief The eligible candidate with the least cost, the shallower on a tie.
 */
static uint32_t sleep_cost_cheapest(const KernelSleepCostCandidate_t *candidates, uint32_t count)
{
    uint32_t chosen = KERNEL_SLEEP_COST_NO_DEPTH;
    uint64_t least = UINT64_MAX;

    for (uint32_t depth = 0u; depth < count; ++depth)
    {
        if (!sleep_cost_eligible(&candidates[depth]))
            continue;
        const uint64_t cost = kernel_sleep_cost_of(&candidates[depth].cost, candidates[depth].asleep_cycles);
        if (chosen != KERNEL_SLEEP_COST_NO_DEPTH && cost >= least)
            continue;
        chosen = depth;
        least = cost;
    }
    return chosen;
}

/**
 * @brief The deepest eligible candidate whose residency its sleep outlasts, the first eligible one qualifying always.
 */
static uint32_t sleep_cost_by_the_rule(const KernelSleepCostCandidate_t *candidates, uint32_t count)
{
    uint32_t chosen = KERNEL_SLEEP_COST_NO_DEPTH;

    for (uint32_t depth = 0u; depth < count; ++depth)
    {
        if (!sleep_cost_eligible(&candidates[depth]))
            continue;
        if (chosen == KERNEL_SLEEP_COST_NO_DEPTH ||
            candidates[depth].residency_cycles <= candidates[depth].asleep_cycles)
            chosen = depth;
    }
    return chosen;
}

/**
 * @brief How long a sleep of @p window lasts at a depth of @p lead: the window less the lead, 0 when the lead outlasts
 *        it.
 */
static uint64_t sleep_cost_asleep(uint64_t window, uint64_t lead) { return window > lead ? window - lead : 0u; }

/**
 * @brief The shallowest allowed depth of @p depths, or the deepest when @p deepest; KERNEL_SLEEP_COST_NO_DEPTH when
 * none is allowed.
 */
static uint32_t sleep_cost_fixed_depth(const KernelSleepCostReplayDepth_t *depths, uint32_t count, bool deepest)
{
    uint32_t found = KERNEL_SLEEP_COST_NO_DEPTH;

    for (uint32_t depth = 0u; depth < count; ++depth)
    {
        if (!depths[depth].allowed)
            continue;
        found = depth;
        if (!deepest)
            break;
    }
    return found;
}

/**
 * @brief Adds a sleep of @p window at the fixed depth @p depth to @p cost and @p misses: asleep the window less its
 *        lead, or the whole window and a miss where the lead outlasts it; a miss costing nothing where no depth is.
 */
static void sleep_cost_replay_at_a_fixed_depth(const KernelSleepCostReplayDepth_t *depths, uint32_t depth,
                                               uint64_t window, uint64_t *cost, uint32_t *misses)
{
    if (depth == KERNEL_SLEEP_COST_NO_DEPTH)
    {
        ++*misses;
        return;
    }
    const uint64_t asleep = sleep_cost_asleep(window, depths[depth].lead_cycles);
    if (asleep == 0u)
        ++*misses;
    *cost = sleep_cost_saturated_sum(*cost, kernel_sleep_cost_of(&depths[depth].cost, asleep != 0u ? asleep : window));
}

/**
 * @brief The depth a sleep of @p window would be given among @p depths, chosen again (kernel_sleep_cost_choose).
 */
static uint32_t sleep_cost_choose_again(const KernelSleepCostReplayDepth_t *depths, uint32_t count, uint64_t window)
{
    KernelSleepCostCandidate_t candidates[KERNEL_SLEEP_COST_DEPTHS];

    for (uint32_t depth = 0u; depth < count; ++depth)
    {
        candidates[depth].allowed = depths[depth].allowed;
        candidates[depth].asleep_cycles = sleep_cost_asleep(window, depths[depth].lead_cycles);
        candidates[depth].residency_cycles = depths[depth].residency_cycles;
        candidates[depth].cost = depths[depth].cost;
    }
    return kernel_sleep_cost_choose(candidates, count, NULL);
}

void kernel_sleep_cost_estimate(const KernelSleepCostSeries_t *series, uint64_t quantisation_nanojoules,
                                uint64_t timestamp_counter_hertz, KernelSleepCost_t *out)
{
    int64_t microwatts = 0;
    int64_t nanojoules = 0;
    const uint32_t solved = sleep_cost_solve(&series[0], &series[1], &microwatts, &nanojoules);

    if (solved != KERNEL_SLEEP_COST_MEASURED)
    {
        sleep_cost_refuse(out, solved);
        return;
    }
    if (nanojoules < 0 && (uint64_t) (-nanojoules) * series[0].sleeps <= quantisation_nanojoules)
        nanojoules = 0;
    if (microwatts <= 0 || microwatts > (int64_t) UINT32_MAX || nanojoules < 0)
    {
        sleep_cost_refuse(out, KERNEL_SLEEP_COST_IMPLAUSIBLE);
        return;
    }
    if (!sleep_cost_agrees(&series[2], microwatts, nanojoules, quantisation_nanojoules))
    {
        sleep_cost_refuse(out, KERNEL_SLEEP_COST_NOISY);
        return;
    }
    out->source = KERNEL_SLEEP_COST_MEASURED;
    out->power_microwatts = (uint32_t) microwatts;
    out->sleep_nanojoules = (uint64_t) nanojoules;
    out->power_nanojoules_per_mebicycles =
        timestamp_counter_hertz != 0u ?
            ((uint64_t) microwatts * (uint64_t) SLEEP_COST_NANO_PER_MICRO << KERNEL_SLEEP_COST_CYCLE_SHIFT) /
                timestamp_counter_hertz :
            0u;
}

uint64_t kernel_sleep_cost_of(const KernelSleepCost_t *cost, uint64_t asleep_cycles)
{
    const uint64_t entering =
        sleep_cost_saturated_product(cost->sleep_nanojoules, 1ull << KERNEL_SLEEP_COST_CYCLE_SHIFT);
    const uint64_t asleep = sleep_cost_saturated_product(cost->power_nanojoules_per_mebicycles, asleep_cycles);

    return sleep_cost_saturated_sum(entering, asleep);
}

uint32_t kernel_sleep_cost_choose(const KernelSleepCostCandidate_t *candidates, uint32_t count, bool *out_by_cost)
{
    const bool by_cost = sleep_cost_all_measured(candidates, count);

    if (out_by_cost != NULL)
        *out_by_cost = by_cost;
    return by_cost ? sleep_cost_cheapest(candidates, count) : sleep_cost_by_the_rule(candidates, count);
}

uint64_t kernel_sleep_cost_cycles_until(uint64_t now, uint64_t wake, uint64_t longest)
{
    if (wake <= now)
        return 0u;
    return wake - now < longest ? wake - now : longest;
}

void kernel_sleep_cost_replay(const KernelSleepCostRecord_t *records, uint32_t count,
                              const KernelSleepCostReplayDepth_t *depths, uint32_t depth_count,
                              KernelSleepCostReplay_t *out)
{
    const uint32_t kept = count < KERNEL_SLEEP_COST_RECORDED ? count : KERNEL_SLEEP_COST_RECORDED;
    const uint32_t taken = depth_count < KERNEL_SLEEP_COST_DEPTHS ? depth_count : KERNEL_SLEEP_COST_DEPTHS;
    const uint32_t shallowest = sleep_cost_fixed_depth(depths, taken, false);
    const uint32_t deepest = sleep_cost_fixed_depth(depths, taken, true);
    KernelSleepCostReplay_t replay = {0u, 0u, 0u, 0u, 0u, 0u, 0u};

    for (uint32_t sleep = 0u; sleep < kept; ++sleep)
    {
        const uint64_t window = records[sleep].window_cycles;
        const uint32_t chosen = sleep_cost_choose_again(depths, taken, window);
        sleep_cost_replay_at_a_fixed_depth(depths, shallowest, window, &replay.shallowest_cost,
                                           &replay.shallowest_misses);
        sleep_cost_replay_at_a_fixed_depth(depths, deepest, window, &replay.deepest_cost, &replay.deepest_misses);
        if (chosen == KERNEL_SLEEP_COST_NO_DEPTH)
        {
            ++replay.chosen_misses;
            continue;
        }
        replay.chosen_cost = sleep_cost_saturated_sum(
            replay.chosen_cost,
            kernel_sleep_cost_of(&depths[chosen].cost, sleep_cost_asleep(window, depths[chosen].lead_cycles)));
        replay.agreements += chosen == records[sleep].depth ? 1u : 0u;
    }
    *out = replay;
}

const char *kernel_sleep_cost_source_name(uint32_t source)
{
    static const char *const names[] = {"not_calibrated", "no_counter",  "measured", "noisy",
                                        "unseparated",    "implausible", "unread",   "untimed"};

    return source < sizeof(names) / sizeof(names[0]) ? names[source] : "unknown";
}
