#include <kernel/power/wake_latency.h>

/**
 * @brief The sample a wake-up counts as: its latency, held to @ref KERNEL_WAKE_LATENCY_CLIP_IN_LEADS times the
 *        lead once there is one.
 * @param lead    The lead before this wake-up.
 * @param latency The wake-up's latency.
 * @return The sample.
 */
static uint64_t wake_latency_sample(uint64_t lead, uint64_t latency)
{
    const uint64_t ceiling = KERNEL_WAKE_LATENCY_CLIP_IN_LEADS * lead;

    return lead != 0u && latency > ceiling ? ceiling : latency;
}

/**
 * @brief The lead an estimate's smoothed latency and deviation give.
 * @param estimate The estimate.
 * @return The smoothed latency plus KERNEL_WAKE_LATENCY_DEVIATIONS mean deviations, in cycles.
 */
static uint64_t wake_latency_envelope(const KernelWakeLatency_t *estimate)
{
    return (estimate->smoothed_eighths >> 3u) + (estimate->deviation_quarters * KERNEL_WAKE_LATENCY_DEVIATIONS) / 4u;
}

void kernel_wake_latency_learn(KernelWakeLatency_t *estimate, uint64_t latency)
{
    if (estimate->samples == 0u)
    {
        estimate->smoothed_eighths = 8u * latency;
        estimate->deviation_quarters = 2u * latency;
        estimate->lead = wake_latency_envelope(estimate);
        estimate->samples = 1u;
        return;
    }

    const uint64_t sample = wake_latency_sample(estimate->lead, latency);
    const uint64_t smoothed = estimate->smoothed_eighths >> 3u;
    const uint64_t deviation = sample > smoothed ? sample - smoothed : smoothed - sample;

    estimate->deviation_quarters = estimate->deviation_quarters - (estimate->deviation_quarters >> 2u) + deviation;
    estimate->smoothed_eighths = estimate->smoothed_eighths - smoothed + sample;
    estimate->lead = wake_latency_envelope(estimate);
    if (estimate->samples != UINT32_MAX)
        ++estimate->samples;
}
