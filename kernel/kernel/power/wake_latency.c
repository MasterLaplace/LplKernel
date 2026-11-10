#include <kernel/power/wake_latency.h>

_Static_assert(8ull * KERNEL_WAKE_LATENCY_LONGEST_CYCLES <= UINT32_MAX, "the smoothed eighths fit in 32 bits");
_Static_assert(4ull * KERNEL_WAKE_LATENCY_DEVIATIONS * KERNEL_WAKE_LATENCY_LONGEST_CYCLES <= UINT32_MAX,
               "the deviation's quarters, times the deviations, fit in 32 bits");
_Static_assert((1ull + KERNEL_WAKE_LATENCY_DEVIATIONS) * KERNEL_WAKE_LATENCY_CLIP_IN_LEADS *
                       KERNEL_WAKE_LATENCY_LONGEST_CYCLES <=
                   UINT32_MAX,
               "the clip of the largest lead, the longest latency plus as many deviations, fits in 32 bits");

/**
 * @brief The sample a wake-up counts as: its latency, held to KERNEL_WAKE_LATENCY_LONGEST_CYCLES, and to
 *        KERNEL_WAKE_LATENCY_CLIP_IN_LEADS times the lead once there is one.
 * @param lead    The lead before this wake-up, assumed or learned.
 * @param latency The wake-up's latency.
 * @return The sample.
 */
static uint32_t wake_latency_sample(uint32_t lead, uint64_t latency)
{
    const uint32_t bounded =
        latency > KERNEL_WAKE_LATENCY_LONGEST_CYCLES ? KERNEL_WAKE_LATENCY_LONGEST_CYCLES : (uint32_t) latency;
    const uint32_t ceiling = KERNEL_WAKE_LATENCY_CLIP_IN_LEADS * lead;

    return lead != 0u && bounded > ceiling ? ceiling : bounded;
}

/**
 * @brief The lead an estimate's smoothed latency and deviation give.
 * @param estimate The estimate.
 * @return The smoothed latency plus KERNEL_WAKE_LATENCY_DEVIATIONS mean deviations, in cycles.
 */
static uint32_t wake_latency_envelope(const KernelWakeLatency_t *estimate)
{
    return (estimate->smoothed_eighths >> 3u) + (estimate->deviation_quarters * KERNEL_WAKE_LATENCY_DEVIATIONS) / 4u;
}

/**
 * @brief Sorts @p values in place, the smallest first: an insertion sort, for the handful of probes a depth takes.
 * @param values The values.
 * @param count  How many.
 */
static void wake_latency_sort(uint64_t *values, uint32_t count)
{
    for (uint32_t next = 1u; next < count; ++next)
    {
        const uint64_t value = values[next];
        uint32_t slot = next;
        for (; slot > 0u && values[slot - 1u] > value; --slot)
            values[slot] = values[slot - 1u];
        values[slot] = value;
    }
}

void kernel_wake_latency_learn(KernelWakeLatency_t *estimate, uint64_t latency)
{
    const uint32_t sample = wake_latency_sample(estimate->lead, latency);

    if (estimate->samples == 0u)
    {
        estimate->smoothed_eighths = 8u * sample;
        estimate->deviation_quarters = 2u * sample;
        estimate->lead = wake_latency_envelope(estimate);
        estimate->samples = 1u;
        return;
    }

    const uint32_t smoothed = estimate->smoothed_eighths >> 3u;
    const uint32_t deviation = sample > smoothed ? sample - smoothed : smoothed - sample;

    estimate->deviation_quarters = estimate->deviation_quarters - (estimate->deviation_quarters >> 2u) + deviation;
    estimate->smoothed_eighths = estimate->smoothed_eighths - smoothed + sample;
    estimate->lead = wake_latency_envelope(estimate);
    if (estimate->samples != UINT32_MAX)
        ++estimate->samples;
}

void kernel_wake_latency_assume(KernelWakeLatency_t *estimate, uint64_t lead)
{
    if (estimate->samples == 0u)
        estimate->lead =
            lead > KERNEL_WAKE_LATENCY_LONGEST_CYCLES ? KERNEL_WAKE_LATENCY_LONGEST_CYCLES : (uint32_t) lead;
}

void kernel_wake_latency_learn_ascending(KernelWakeLatency_t *estimate, uint64_t *latencies, uint32_t count)
{
    wake_latency_sort(latencies, count);
    for (uint32_t index = 0u; index < count; ++index)
        kernel_wake_latency_learn(estimate, latencies[index]);
}
