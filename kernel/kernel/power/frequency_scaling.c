#include <kernel/power/frequency_scaling.h>

#include <kernel/lib/asmutils.h>

/** CPUID leaf 1, ECX bit 7: Enhanced SpeedStep. */
#define FREQUENCY_SCALING_CPUID_SPEEDSTEP_BIT (1u << 7)

/** IA32_PERF_CTL — the register a performance state is written to. */
#define FREQUENCY_SCALING_PERF_CTL_MSR 0x199u

/** IA32_PERF_STATUS — what the processor is actually running at. */
#define FREQUENCY_SCALING_PERF_STATUS_MSR 0x198u

/** IA32_MPERF — counts at the nominal clock while in C0. */
#define FREQUENCY_SCALING_MPERF_MSR 0xE7u

/** IA32_APERF — counts at the clock actually delivered while in C0. */
#define FREQUENCY_SCALING_APERF_MSR 0xE8u

/** CPUID leaf 6, ECX bit 0: the APERF/MPERF pair exists. */
#define FREQUENCY_SCALING_CPUID_FEEDBACK_BIT (1u << 0)

/** The thermal and power leaf; querying past the highest supported leaf returns junk. */
#define FREQUENCY_SCALING_CPUID_POWER_LEAF 6u

/** Busy share below which the lowest state is enough. */
#define FREQUENCY_SCALING_LOW_THRESHOLD_PERMILLE 100u

/** Busy share above which no scaling is applied at all. */
#define FREQUENCY_SCALING_HIGH_THRESHOLD_PERMILLE 600u

static bool frequency_scaling_available = false;
static PerformanceState_t frequency_scaling_state = PERFORMANCE_STATE_HIGH;
static uint32_t frequency_scaling_applied = 0u;
static uint32_t frequency_scaling_refused = 0u;
static uint16_t frequency_scaling_maximum_ratio = 0u;
static bool frequency_scaling_feedback = false;
static bool frequency_scaling_window_open = false;
static uint64_t frequency_scaling_window_actual = 0u;
static uint64_t frequency_scaling_window_reference = 0u;

/**
 * @brief Records whether the processor publishes the APERF/MPERF pair.
 */
static void frequency_scaling_probe_feedback(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(0u, 0u, &eax, &ebx, &ecx, &edx);
    if (eax < FREQUENCY_SCALING_CPUID_POWER_LEAF)
        return;

    asmutils_cpuid(FREQUENCY_SCALING_CPUID_POWER_LEAF, 0u, &eax, &ebx, &ecx, &edx);
    frequency_scaling_feedback = (ecx & FREQUENCY_SCALING_CPUID_FEEDBACK_BIT) != 0u;
}

/**
 * @brief Does the processor advertise Enhanced SpeedStep?
 * @return true when IA32_PERF_CTL may be written.
 */
static bool frequency_scaling_has_speedstep(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    return (ecx & FREQUENCY_SCALING_CPUID_SPEEDSTEP_BIT) != 0u;
}

/**
 * @brief The ratio the processor is running at right now, taken as the ceiling.
 *
 * @note There is no ACPI `_PSS` table in this kernel, so the alternative would be a list
 *       of frequencies invented from nothing — and a performance state the silicon does
 *       not have is a general protection fault rather than a slower clock.
 *
 * @return The ratio, 0 when the processor reports none.
 */
static uint16_t frequency_scaling_read_current_ratio(void)
{
    const uint64_t status = asmutils_read_model_specific_register(FREQUENCY_SCALING_PERF_STATUS_MSR);
    return (uint16_t) ((status >> 8) & 0xFFu);
}

/**
 * @brief The ratio a performance state asks the processor for.
 *
 * @details Halves and quarters of the ceiling rather than absolute ratios: the ceiling is
 *          whatever this processor was found at, so the same three states mean the same
 *          three fractions of its own capability on every machine.
 *
 * @note Floored at one, since a ratio of zero is not a slow clock but a stopped one.
 *
 * @param state The state requested.
 * @return The ratio to write to IA32_PERF_CTL.
 */
static uint16_t frequency_scaling_ratio_for(PerformanceState_t state)
{
    uint16_t ratio = frequency_scaling_maximum_ratio;
    if (state == PERFORMANCE_STATE_MEDIUM)
        ratio = (uint16_t) (frequency_scaling_maximum_ratio / 2u);
    else if (state == PERFORMANCE_STATE_LOW)
        ratio = (uint16_t) (frequency_scaling_maximum_ratio / 4u);
    return ratio == 0u ? 1u : ratio;
}

/**
 * @brief Halves both deltas together until the actual one survives a multiply by 1000.
 *
 * @details Halving both keeps their ratio, and keeps `delta_actual * 1000` inside 64 bits
 *          over any window this kernel will open.
 *
 * @param delta_actual    Increase of IA32_APERF, halved in place.
 * @param delta_reference Increase of IA32_MPERF, halved in place; may reach zero.
 */
static void frequency_scaling_halve_until_permille_fits(uint64_t *delta_actual, uint64_t *delta_reference)
{
    while (*delta_actual > UINT64_MAX / 1000u)
    {
        *delta_actual >>= 1;
        *delta_reference >>= 1;
    }
}

void kernel_frequency_scaling_initialize(void)
{
    frequency_scaling_available = false;
    frequency_scaling_state = PERFORMANCE_STATE_HIGH;
    frequency_scaling_applied = 0u;
    frequency_scaling_refused = 0u;
    frequency_scaling_maximum_ratio = 0u;
    frequency_scaling_feedback = false;
    frequency_scaling_window_open = false;

    frequency_scaling_probe_feedback();
    if (!frequency_scaling_has_speedstep())
        return;

    frequency_scaling_maximum_ratio = frequency_scaling_read_current_ratio();
    frequency_scaling_available = frequency_scaling_maximum_ratio != 0u;
}

bool kernel_frequency_scaling_available(void) { return frequency_scaling_available; }

PerformanceState_t kernel_frequency_scaling_govern(uint32_t busy_permille, bool deadline_bound)
{
    if (deadline_bound)
        return busy_permille >= FREQUENCY_SCALING_HIGH_THRESHOLD_PERMILLE ? PERFORMANCE_STATE_HIGH :
                                                                            PERFORMANCE_STATE_MEDIUM;

    if (busy_permille <= FREQUENCY_SCALING_LOW_THRESHOLD_PERMILLE)
        return PERFORMANCE_STATE_LOW;
    if (busy_permille >= FREQUENCY_SCALING_HIGH_THRESHOLD_PERMILLE)
        return PERFORMANCE_STATE_HIGH;
    return PERFORMANCE_STATE_MEDIUM;
}

bool kernel_frequency_scaling_request(PerformanceState_t state)
{
    frequency_scaling_state = state;

    if (!frequency_scaling_available)
    {
        ++frequency_scaling_refused;
        return false;
    }

    const uint16_t ratio = frequency_scaling_ratio_for(state);
    asmutils_write_model_specific_register(FREQUENCY_SCALING_PERF_CTL_MSR, (uint64_t) ratio << 8);
    ++frequency_scaling_applied;
    return true;
}

PerformanceState_t kernel_frequency_scaling_current(void) { return frequency_scaling_state; }

uint32_t kernel_frequency_scaling_applied_count(void) { return frequency_scaling_applied; }

uint32_t kernel_frequency_scaling_refused_count(void) { return frequency_scaling_refused; }

bool kernel_frequency_scaling_feedback_available(void) { return frequency_scaling_feedback; }

uint32_t kernel_frequency_scaling_ratio_permille(uint64_t delta_actual, uint64_t delta_reference)
{
    if (delta_reference == 0u)
        return KERNEL_FREQUENCY_SCALING_NO_FEEDBACK;

    frequency_scaling_halve_until_permille_fits(&delta_actual, &delta_reference);
    if (delta_reference == 0u)
        return KERNEL_FREQUENCY_SCALING_NO_FEEDBACK;

    const uint64_t permille = (delta_actual * 1000u) / delta_reference;
    return permille >= KERNEL_FREQUENCY_SCALING_NO_FEEDBACK ? KERNEL_FREQUENCY_SCALING_NO_FEEDBACK - 1u
                                                            : (uint32_t) permille;
}

void kernel_frequency_scaling_begin_measurement(void)
{
    if (!frequency_scaling_feedback)
        return;

    frequency_scaling_window_reference = asmutils_read_model_specific_register(FREQUENCY_SCALING_MPERF_MSR);
    frequency_scaling_window_actual = asmutils_read_model_specific_register(FREQUENCY_SCALING_APERF_MSR);
    frequency_scaling_window_open = true;
}

uint32_t kernel_frequency_scaling_measured_permille(void)
{
    if (!frequency_scaling_feedback || !frequency_scaling_window_open)
        return KERNEL_FREQUENCY_SCALING_NO_FEEDBACK;

    const uint64_t reference = asmutils_read_model_specific_register(FREQUENCY_SCALING_MPERF_MSR);
    const uint64_t actual = asmutils_read_model_specific_register(FREQUENCY_SCALING_APERF_MSR);
    return kernel_frequency_scaling_ratio_permille(actual - frequency_scaling_window_actual,
                                                   reference - frequency_scaling_window_reference);
}
