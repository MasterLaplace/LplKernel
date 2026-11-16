#include <kernel/power/sleep_cost.h>

#include <stddef.h>

#define SLEEP_COST_MICROWATTS_PER_WATT       1000000ll /**< P solves in nanojoules per nanosecond, watts. */
#define SLEEP_COST_NANOJOULES_PER_MICROJOULE 1000ull   /**< A microwatt-second is 1000 nanojoules. */

/**
 * @brief Whether the sleeps a second of @p first and @p second differ by KERNEL_SLEEP_COST_SEPARATION times at least:
 *        n1 * T2 against n2 * T1.
 */
static bool sleep_cost_separated(const KernelSleepCostSeries_t *first, const KernelSleepCostSeries_t *second)
{
    const uint64_t first_rate = (uint64_t) first->sleeps * second->nanoseconds;
    const uint64_t second_rate = (uint64_t) second->sleeps * first->nanoseconds;
    const uint64_t higher = first_rate > second_rate ? first_rate : second_rate;
    const uint64_t lower = first_rate > second_rate ? second_rate : first_rate;

    return higher / KERNEL_SLEEP_COST_SEPARATION >= lower && higher != lower;
}

/**
 * @brief The energy the costs @p microwatts and @p nanojoules give a series of @p sleeps over @p nanoseconds.
 */
static int64_t sleep_cost_predicted(int64_t microwatts, int64_t nanojoules, const KernelSleepCostSeries_t *series)
{
    return (int64_t) series->sleeps * nanojoules +
           (microwatts * (int64_t) series->nanoseconds) / SLEEP_COST_MICROWATTS_PER_WATT;
}

/**
 * @brief Whether @p series misses the energy @p predicted by at most @p quantisation plus
 * 1/2^KERNEL_SLEEP_COST_CHECK_SHIFT of what it measured.
 */
static bool sleep_cost_agrees(const KernelSleepCostSeries_t *series, int64_t predicted, uint64_t quantisation)
{
    const int64_t measured = (int64_t) series->nanojoules;
    const int64_t miss = measured > predicted ? measured - predicted : predicted - measured;

    return (uint64_t) miss <= quantisation + (series->nanojoules >> KERNEL_SLEEP_COST_CHECK_SHIFT);
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
    return left != 0u && right > UINT64_MAX / left ? UINT64_MAX : left * right;
}

/**
 * @brief Adds @p left and @p right, the largest value for a sum past 64 bits.
 */
static uint64_t sleep_cost_saturated_sum(uint64_t left, uint64_t right)
{
    return left > UINT64_MAX - right ? UINT64_MAX : left + right;
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
    const KernelSleepCostSeries_t *const first = &series[0];
    const KernelSleepCostSeries_t *const second = &series[1];

    if (!sleep_cost_separated(first, second))
    {
        sleep_cost_refuse(out, KERNEL_SLEEP_COST_UNSEPARATED);
        return;
    }
    const int64_t determinant = (int64_t) first->sleeps * (int64_t) second->nanoseconds -
                                (int64_t) second->sleeps * (int64_t) first->nanoseconds;
    const int64_t energy_numerator = (int64_t) first->nanojoules * (int64_t) second->nanoseconds -
                                     (int64_t) second->nanojoules * (int64_t) first->nanoseconds;
    const int64_t power_numerator =
        (int64_t) first->sleeps * (int64_t) second->nanojoules - (int64_t) second->sleeps * (int64_t) first->nanojoules;
    const int64_t microwatts = (power_numerator * SLEEP_COST_MICROWATTS_PER_WATT) / determinant;
    int64_t nanojoules = energy_numerator / determinant;

    if (nanojoules < 0 && (uint64_t) (-nanojoules) * first->sleeps <= quantisation_nanojoules)
        nanojoules = 0;
    if (microwatts <= 0 || microwatts > (int64_t) UINT32_MAX || nanojoules < 0)
    {
        sleep_cost_refuse(out, KERNEL_SLEEP_COST_IMPLAUSIBLE);
        return;
    }
    if (!sleep_cost_agrees(&series[2], sleep_cost_predicted(microwatts, nanojoules, &series[2]),
                           quantisation_nanojoules))
    {
        sleep_cost_refuse(out, KERNEL_SLEEP_COST_NOISY);
        return;
    }
    out->source = KERNEL_SLEEP_COST_MEASURED;
    out->power_microwatts = (uint32_t) microwatts;
    out->sleep_nanojoules = (uint64_t) nanojoules;
    out->power_nanojoules_per_mebicycles =
        timestamp_counter_hertz != 0u ?
            ((uint64_t) microwatts * SLEEP_COST_NANOJOULES_PER_MICROJOULE << KERNEL_SLEEP_COST_CYCLE_SHIFT) /
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
