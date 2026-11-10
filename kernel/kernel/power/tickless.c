#include <kernel/power/tickless.h>

#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_sleep.h>

#define TICKLESS_MICROSECONDS_PER_SECOND 1000000u

static bool tickless_permitted = false;
static uint32_t tickless_nominal_hz = 0u;
static uint32_t tickless_ticks_avoided = 0u;
static uint32_t tickless_microseconds_short_of_a_tick = 0u;
static uint32_t tickless_early_wakes = 0u;
static uint64_t tickless_slept_microseconds = 0u;
static uint64_t tickless_longest_sleep_cycles = 0u;
static uint64_t tickless_reconciler_period_cycles = 0u;
static KernelWakePlan_t *tickless_plan = NULL;
static uint32_t tickless_reconciler_duty = KERNEL_WAKE_PLANNER_NO_DUTY;

/**
 * @brief The reconciler's pass the stopped tick would have run: runs it, and lets the next one ride the
 *        first wake-up a tick period later.
 * @param context Unused.
 * @param now     The wake-up's cycle.
 */
static void tickless_run_the_reconciler_pass(void *context, uint64_t now)
{
    (void) context;
    kernel_reconciler_check_periodic();
    (void) kernel_wake_planner_arm(tickless_plan, tickless_reconciler_duty, now + tickless_reconciler_period_cycles,
                                   KERNEL_WAKE_PLANNER_NEVER);
}

/**
 * @brief The microseconds @p cycles of the timestamp counter last, rounded down.
 *
 * @details Whole seconds first, so that the product never overflows however long the sleep was.
 *
 * @param cycles A duration on the timestamp counter.
 * @param hertz  Its rate; never zero here.
 * @return The microseconds, saturated at the 32-bit maximum.
 */
static uint32_t tickless_microseconds_from_cycles(uint64_t cycles, uint64_t hertz)
{
    const uint64_t microseconds = (cycles / hertz) * TICKLESS_MICROSECONDS_PER_SECOND +
                                  ((cycles % hertz) * TICKLESS_MICROSECONDS_PER_SECOND) / hertz;

    return microseconds > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) microseconds;
}

/**
 * @brief Counts the ticks a stretch of sleep stood for, against the tick in force, and gives them
 *        to the tick count.
 *
 * @details The microseconds short of a whole tick are carried to the next sleep rather than lost,
 *          so the ticks counted over a session are its sleep divided by the period, whatever the
 *          length of each sleep.
 *
 * @param slept Microseconds just spent with the tick stopped.
 */
static void tickless_count_ticks_avoided(uint32_t slept)
{
    const uint32_t tick_microseconds = TICKLESS_MICROSECONDS_PER_SECOND / tickless_nominal_hz;
    const uint64_t carried = (uint64_t) tickless_microseconds_short_of_a_tick + (uint64_t) slept;
    const uint32_t ticks = (uint32_t) (carried / tick_microseconds);

    tickless_microseconds_short_of_a_tick = (uint32_t) (carried % tick_microseconds);
    tickless_ticks_avoided += ticks;
    interrupt_request_advance_tick_count(ticks);
}

/**
 * @brief Sleeps until any interrupt with the tick still running, then serves @p plan at the wake-up.
 * @param plan The plan.
 * @return 0: no time is counted slept while the tick runs.
 */
static uint32_t tickless_sleep_on_the_tick(KernelWakePlan_t *plan)
{
    processor_sleep_until_interrupt();
    (void) kernel_wake_planner_serve(plan, asmutils_read_timestamp_counter());
    return 0u;
}

bool kernel_tickless_enable(bool no_world_instantiated, KernelWakePlan_t *plan)
{
    if (!no_world_instantiated || tickless_permitted || plan == NULL)
        return false;

    const uint32_t tick_hz = interrupt_request_get_timer_frequency_hz();
    const uint64_t cycle_hz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();
    if (tick_hz == 0u || tick_hz > TICKLESS_MICROSECONDS_PER_SECOND ||
        advanced_pic_timer_backend_get_calibrated_timer_frequency_hz() == 0u || cycle_hz == 0u)
        return false;

    const uint32_t duty = kernel_wake_planner_declare(plan, tickless_run_the_reconciler_pass, NULL);
    if (duty == KERNEL_WAKE_PLANNER_NO_DUTY)
        return false;

    tickless_plan = plan;
    tickless_reconciler_duty = duty;
    tickless_reconciler_period_cycles = ((uint64_t) KERNEL_RECONCILER_TICK_SAMPLE_PERIOD * cycle_hz) / tick_hz;
    (void) kernel_wake_planner_arm(plan, duty, asmutils_read_timestamp_counter() + tickless_reconciler_period_cycles,
                                   KERNEL_WAKE_PLANNER_NEVER);

    tickless_nominal_hz = tick_hz;
    tickless_microseconds_short_of_a_tick = 0u;
    tickless_longest_sleep_cycles = kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS);
    interrupt_request_suspend_periodic_tick();
    tickless_permitted = true;
    return true;
}

void kernel_tickless_disable(void)
{
    if (!tickless_permitted)
        return;

    tickless_permitted = false;
    kernel_wake_planner_withdraw(tickless_plan, tickless_reconciler_duty);
    tickless_plan = NULL;
    tickless_reconciler_duty = KERNEL_WAKE_PLANNER_NO_DUTY;
    interrupt_request_resume_periodic_tick();
}

bool kernel_tickless_enabled(void) { return tickless_permitted; }

uint64_t kernel_tickless_cycles_from_microseconds(uint32_t microseconds)
{
    const uint64_t hertz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    return (uint64_t) (microseconds / TICKLESS_MICROSECONDS_PER_SECOND) * hertz +
           ((uint64_t) (microseconds % TICKLESS_MICROSECONDS_PER_SECOND) * hertz) / TICKLESS_MICROSECONDS_PER_SECOND;
}

uint32_t kernel_tickless_sleep_on(KernelWakePlan_t *plan)
{
    if (!tickless_permitted)
        return tickless_sleep_on_the_tick(plan);

    const uint64_t before = asmutils_read_timestamp_counter();
    const uint64_t planned = kernel_wake_planner_next_wake(plan);
    if (planned <= before)
    {
        (void) kernel_wake_planner_serve(plan, before);
        return 0u;
    }

    const uint64_t longest = before + tickless_longest_sleep_cycles;
    const uint64_t deadline = planned < longest ? planned : longest;
    if (!advanced_pic_timer_backend_arm_one_shot_in_cycles(deadline - before))
        return tickless_sleep_on_the_tick(plan);

    const uint32_t armed = advanced_pic_timer_backend_read_current_count();
    processor_sleep_until_interrupt();
    const uint32_t remaining = advanced_pic_timer_backend_read_current_count();
    advanced_pic_timer_backend_disable();
    const uint64_t after = asmutils_read_timestamp_counter();

    const bool on_the_deadline = remaining == 0u || remaining > armed || armed == 0u;
    if (!on_the_deadline)
        ++tickless_early_wakes;

    const uint32_t elapsed = tickless_microseconds_from_cycles(
        after - before, advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
    tickless_slept_microseconds += elapsed;
    tickless_count_ticks_avoided(elapsed);

    const bool planned_instant = on_the_deadline && deadline == planned && after > deadline;
    (void) kernel_wake_planner_serve(plan, planned_instant ? deadline : after);
    return elapsed;
}

uint32_t kernel_tickless_ticks_avoided(void) { return tickless_ticks_avoided; }

uint32_t kernel_tickless_early_wakes(void) { return tickless_early_wakes; }

uint64_t kernel_tickless_slept_microseconds(void) { return tickless_slept_microseconds; }

uint32_t kernel_tickless_nominal_frequency_hz(void) { return tickless_nominal_hz; }
