#include <kernel/power/wakeup_accounting.h>

#include <kernel/diag/telemetry.h>

/** Changes underneath the halt that is waiting on it, so it cannot be cached. */
static volatile bool wakeup_accounting_armed = false;

static volatile uint32_t wakeup_accounting_vector[KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT];
static volatile uint32_t wakeup_accounting_sleeps = 0u;
static volatile uint32_t wakeup_accounting_monitor_wakes = 0u;
static volatile uint32_t wakeup_accounting_unattributed = 0u;
static volatile uint32_t wakeup_accounting_double_arms = 0u;

_Static_assert(KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT == 256u,
               "the vector table is indexed by a uint8_t with no bound check, so it must cover "
               "every value that type can take: narrowing it makes the attribution an "
               "out-of-bounds write in interrupt context");

_Static_assert(KERNEL_WAKEUP_ACCOUNTING_NO_VECTOR >= KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT,
               "the no-vector sentinel must lie outside the range of real vectors, or an empty "
               "report is indistinguishable from one naming a source");

/**
 * @brief Sleeps that already have an outcome, which excludes one still outstanding.
 *
 * @note Arming sets the flag and advances the count together, so being armed means at
 *       least one sleep and the subtraction cannot wrap.
 *
 * @return The count.
 */
static uint32_t wakeup_accounting_sleeps_with_an_outcome(void)
{
    if (!wakeup_accounting_armed)
        return wakeup_accounting_sleeps;

    return wakeup_accounting_sleeps - 1u;
}

void kernel_wakeup_accounting_reset(void)
{
    wakeup_accounting_armed = false;
    wakeup_accounting_sleeps = 0u;
    wakeup_accounting_monitor_wakes = 0u;
    wakeup_accounting_unattributed = 0u;
    wakeup_accounting_double_arms = 0u;

    for (uint32_t vector = 0u; vector < KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT; ++vector)
        wakeup_accounting_vector[vector] = 0u;
}

void kernel_wakeup_accounting_arm(void)
{
    if (wakeup_accounting_armed)
    {
        ++wakeup_accounting_double_arms;
        return;
    }

    wakeup_accounting_armed = true;
    ++wakeup_accounting_sleeps;
}

void kernel_wakeup_accounting_attribute(uint8_t vector)
{
    if (!wakeup_accounting_armed)
        return;

    wakeup_accounting_armed = false;
    ++wakeup_accounting_vector[vector];
}

void kernel_wakeup_accounting_close_monitor_write(void)
{
    if (!wakeup_accounting_armed)
        return;

    wakeup_accounting_armed = false;
    ++wakeup_accounting_monitor_wakes;
}

void kernel_wakeup_accounting_close_unattributed(void)
{
    if (!wakeup_accounting_armed)
        return;

    wakeup_accounting_armed = false;
    ++wakeup_accounting_unattributed;
}

bool kernel_wakeup_accounting_is_armed(void) { return wakeup_accounting_armed; }

uint32_t kernel_wakeup_accounting_get_vector_count(uint8_t vector) { return wakeup_accounting_vector[vector]; }

uint32_t kernel_wakeup_accounting_get_sleep_count(void) { return wakeup_accounting_sleeps; }

uint32_t kernel_wakeup_accounting_get_attributed_count(void)
{
    uint32_t total = 0u;
    for (uint32_t vector = 0u; vector < KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT; ++vector)
        total += wakeup_accounting_vector[vector];
    return total;
}

uint32_t kernel_wakeup_accounting_get_monitor_wake_count(void) { return wakeup_accounting_monitor_wakes; }

uint32_t kernel_wakeup_accounting_get_unattributed_count(void) { return wakeup_accounting_unattributed; }

uint32_t kernel_wakeup_accounting_get_double_arm_count(void) { return wakeup_accounting_double_arms; }

uint32_t kernel_wakeup_accounting_get_source_count(void)
{
    uint32_t distinct = 0u;
    for (uint32_t vector = 0u; vector < KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT; ++vector)
    {
        if (wakeup_accounting_vector[vector] != 0u)
            ++distinct;
    }
    return distinct;
}

uint16_t kernel_wakeup_accounting_get_busiest_vector(void)
{
    uint16_t busiest = KERNEL_WAKEUP_ACCOUNTING_NO_VECTOR;
    uint32_t best = 0u;

    for (uint32_t vector = 0u; vector < KERNEL_WAKEUP_ACCOUNTING_VECTOR_COUNT; ++vector)
    {
        if (wakeup_accounting_vector[vector] > best)
        {
            best = wakeup_accounting_vector[vector];
            busiest = (uint16_t) vector;
        }
    }

    return busiest;
}

bool kernel_wakeup_accounting_conserves(void)
{
    const uint32_t accounted = kernel_wakeup_accounting_get_attributed_count() + wakeup_accounting_monitor_wakes +
                               wakeup_accounting_unattributed;

    return accounted == wakeup_accounting_sleeps_with_an_outcome();
}

void kernel_wakeup_accounting_report(Serial_t *serial)
{
    const uint16_t busiest = kernel_wakeup_accounting_get_busiest_vector();

    kernel_telemetry_begin_record(serial, "wakeup");
    kernel_telemetry_write_unsigned("sleeps", wakeup_accounting_sleeps);
    kernel_telemetry_write_unsigned("attributed", kernel_wakeup_accounting_get_attributed_count());
    kernel_telemetry_write_unsigned("monitor_writes", wakeup_accounting_monitor_wakes);
    kernel_telemetry_write_unsigned("unattributed", wakeup_accounting_unattributed);
    kernel_telemetry_write_unsigned("double_arms", wakeup_accounting_double_arms);
    kernel_telemetry_write_unsigned("sources", kernel_wakeup_accounting_get_source_count());
    kernel_telemetry_write_unsigned("busiest_vector", (uint32_t) busiest);
    kernel_telemetry_write_unsigned(
        "busiest_count", busiest == KERNEL_WAKEUP_ACCOUNTING_NO_VECTOR ? 0u : wakeup_accounting_vector[busiest]);
    kernel_telemetry_write_boolean("conserves", kernel_wakeup_accounting_conserves());
    kernel_telemetry_end_record();
}
