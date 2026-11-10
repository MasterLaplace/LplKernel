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
