/**
 * @file frequency_scaling.c
 * @brief Trading clock for battery, deliberately.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/power/frequency_scaling.h>

#include <kernel/lib/asmutils.h>

/** CPUID leaf 1, ECX bit 7: Enhanced SpeedStep. */
#define FREQUENCY_SCALING_CPUID_SPEEDSTEP_BIT (1u << 7)

/** IA32_PERF_CTL — the register a performance state is written to. */
#define FREQUENCY_SCALING_PERF_CTL_MSR 0x199u

/** IA32_PERF_STATUS — what the processor is actually running at. */
#define FREQUENCY_SCALING_PERF_STATUS_MSR 0x198u

/** Busy share below which the lowest state is enough. */
#define FREQUENCY_SCALING_LOW_THRESHOLD_PERMILLE 100u

/** Busy share above which no scaling is applied at all. */
#define FREQUENCY_SCALING_HIGH_THRESHOLD_PERMILLE 600u

static bool frequency_scaling_available = false;
static PerformanceState_t frequency_scaling_state = PERFORMANCE_STATE_HIGH;
static uint32_t frequency_scaling_applied = 0u;
static uint32_t frequency_scaling_refused = 0u;
static uint16_t frequency_scaling_maximum_ratio = 0u;

void kernel_frequency_scaling_initialize(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    frequency_scaling_available = false;
    frequency_scaling_state = PERFORMANCE_STATE_HIGH;
    frequency_scaling_applied = 0u;
    frequency_scaling_refused = 0u;
    frequency_scaling_maximum_ratio = 0u;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    if ((ecx & FREQUENCY_SCALING_CPUID_SPEEDSTEP_BIT) == 0u)
        return;

    /* The ratio the processor is running at right now is taken as the ceiling. There
       is no ACPI _PSS table in this kernel, so the alternative would be a list of
       frequencies invented from nothing — and a performance state the silicon does
       not have is a general protection fault rather than a slower clock. */
    const uint64_t status = asmutils_read_model_specific_register(FREQUENCY_SCALING_PERF_STATUS_MSR);
    frequency_scaling_maximum_ratio = (uint16_t) ((status >> 8) & 0xFFu);
    if (frequency_scaling_maximum_ratio == 0u)
        return;

    frequency_scaling_available = true;
}

bool kernel_frequency_scaling_available(void) { return frequency_scaling_available; }

PerformanceState_t kernel_frequency_scaling_govern(uint32_t busy_permille, bool deadline_bound)
{
    /* A cadence to meet outranks a low load, and that is the whole safety argument.
       A node streaming forty-millisecond buffers is idle between them by any measure
       of busy time — and dropping its clock on that evidence is how a buffer arrives
       late. */
    if (deadline_bound)
        return busy_permille >= FREQUENCY_SCALING_HIGH_THRESHOLD_PERMILLE ? PERFORMANCE_STATE_HIGH
                                                                          : PERFORMANCE_STATE_MEDIUM;

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

    /* Halves and quarters of the ceiling rather than absolute ratios: the ceiling is
       whatever this processor was found at, so the same three states mean the same
       three fractions of its own capability on every machine. Floored at one, since
       a ratio of zero is not a slow clock but a stopped one. */
    uint16_t ratio = frequency_scaling_maximum_ratio;
    if (state == PERFORMANCE_STATE_MEDIUM)
        ratio = (uint16_t) (frequency_scaling_maximum_ratio / 2u);
    else if (state == PERFORMANCE_STATE_LOW)
        ratio = (uint16_t) (frequency_scaling_maximum_ratio / 4u);
    if (ratio == 0u)
        ratio = 1u;

    asmutils_write_model_specific_register(FREQUENCY_SCALING_PERF_CTL_MSR, (uint64_t) ratio << 8);
    ++frequency_scaling_applied;
    return true;
}

PerformanceState_t kernel_frequency_scaling_current(void) { return frequency_scaling_state; }

uint32_t kernel_frequency_scaling_applied_count(void) { return frequency_scaling_applied; }

uint32_t kernel_frequency_scaling_refused_count(void) { return frequency_scaling_refused; }
