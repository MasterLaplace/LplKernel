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

/** The reconciler's pass while the tick is stopped: the plan it is a duty of, its number, its period. */
typedef struct {
    KernelWakePlan_t *plan; /**< The session's plan; NULL outside a session. */
    uint32_t duty;          /**< The pass's number in it. */
    uint64_t period;        /**< KERNEL_RECONCILER_TICK_SAMPLE_PERIOD ticks of the stopped tick, in cycles. */
} TicklessReconcilerPass_t;

static TicklessReconcilerPass_t tickless_reconciler_pass = {NULL, KERNEL_WAKE_PLANNER_NO_DUTY, 0u};

/**
 * @brief The reconciler's pass the stopped tick would have run: runs it, and lets the next one ride the
 *        first wake-up a tick period later.
 * @param context The TicklessReconcilerPass_t.
 * @param now     The cycle the core woke at.
 */
static void tickless_run_the_reconciler_pass(void *context, uint64_t now)
{
    const TicklessReconcilerPass_t *const pass = (const TicklessReconcilerPass_t *) context;

    kernel_reconciler_check_periodic();
    (void) kernel_wake_planner_arm(pass->plan, pass->duty, now + pass->period, KERNEL_WAKE_PLANNER_NEVER);
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
 * @brief Sleeps until any interrupt, the tick's when it runs, then serves @p plan at the wake-up.
 * @param plan The plan.
 * @return 0: no time is counted slept without the one-shot timer.
 */
static uint32_t tickless_sleep_until_any_interrupt(KernelWakePlan_t *plan)
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

    const uint32_t duty =
        kernel_wake_planner_declare(plan, tickless_run_the_reconciler_pass, &tickless_reconciler_pass);
    if (duty == KERNEL_WAKE_PLANNER_NO_DUTY)
        return false;

    tickless_reconciler_pass.plan = plan;
    tickless_reconciler_pass.duty = duty;
    tickless_reconciler_pass.period = ((uint64_t) KERNEL_RECONCILER_TICK_SAMPLE_PERIOD * cycle_hz) / tick_hz;
    (void) kernel_wake_planner_arm(plan, duty, asmutils_read_timestamp_counter() + tickless_reconciler_pass.period,
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
    kernel_wake_planner_withdraw(tickless_reconciler_pass.plan, tickless_reconciler_pass.duty);
    tickless_reconciler_pass.plan = NULL;
    tickless_reconciler_pass.duty = KERNEL_WAKE_PLANNER_NO_DUTY;
    interrupt_request_resume_periodic_tick();
}

bool kernel_tickless_enabled(void) { return tickless_permitted; }

uint64_t kernel_tickless_cycles_from_microseconds(uint32_t microseconds)
{
    const uint64_t hertz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    return (uint64_t) (microseconds / TICKLESS_MICROSECONDS_PER_SECOND) * hertz +
           ((uint64_t) (microseconds % TICKLESS_MICROSECONDS_PER_SECOND) * hertz) / TICKLESS_MICROSECONDS_PER_SECOND;
}

uint32_t kernel_tickless_microseconds_from_cycles(uint64_t cycles)
{
    const uint64_t hertz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    return hertz != 0u ? tickless_microseconds_from_cycles(cycles, hertz) : 0u;
}

uint32_t kernel_tickless_sleep_on(KernelWakePlan_t *plan)
{
    if (!tickless_permitted)
        return tickless_sleep_until_any_interrupt(plan);
    if (plan != tickless_reconciler_pass.plan)
        return 0u;

    const uint64_t before = asmutils_read_timestamp_counter();
    const uint64_t planned = kernel_wake_planner_next_wake(plan);
    if (planned <= before)
    {
        (void) kernel_wake_planner_serve(plan, before);
        return 0u;
    }

    const uint64_t longest = before + tickless_longest_sleep_cycles;
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    if (!advanced_pic_timer_backend_arm_at_cycle(planned < longest ? planned : longest))
    {
        asmutils_restore_flags(flags);
        return tickless_sleep_until_any_interrupt(plan);
    }

    processor_sleep_until_interrupt();
    const uint64_t after = asmutils_read_timestamp_counter();
    const bool woken_early = advanced_pic_timer_backend_disarm(after) != 0u;
    asmutils_restore_flags(flags);
    if (woken_early)
        ++tickless_early_wakes;

    const uint32_t elapsed = tickless_microseconds_from_cycles(
        after - before, advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
    tickless_slept_microseconds += elapsed;
    tickless_count_ticks_avoided(elapsed);

    (void) kernel_wake_planner_serve(plan, after);
    return elapsed;
}

uint32_t kernel_tickless_ticks_avoided(void) { return tickless_ticks_avoided; }

uint32_t kernel_tickless_early_wakes(void) { return tickless_early_wakes; }

uint64_t kernel_tickless_slept_microseconds(void) { return tickless_slept_microseconds; }

uint32_t kernel_tickless_nominal_frequency_hz(void) { return tickless_nominal_hz; }
