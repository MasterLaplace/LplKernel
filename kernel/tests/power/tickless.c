#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/tickless.h>
#include <kernel/power/wakeup_accounting.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(tickless, KERNEL_TEST_STAGE_BOOTED);

/** @name The window a sleep must land in, in microseconds
 *  @{ */
#define TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS 2000u /**< How long after the arming it opens. */
#define TICKLESS_TEST_WINDOW_MICROSECONDS       2000u /**< How long it stays open. */
/** @} */

/**
 * How late, in microseconds, the interrupt of a sleep's own deadline may come: two ticks of 100 Hz, what a loaded
 * host is seen to add to a tick (#655).
 */
#define TICKLESS_TEST_LATENESS_MICROSECONDS 20000u

/** Sleeps a window may take: interrupts other than its deadline may end the first ones. */
#define TICKLESS_TEST_SLEEPS 64u

/** @name Deadlines slept on one after the other, each a window of one cycle
 *  @{ */
#define TICKLESS_TEST_DEADLINES             16u   /**< How many. */
#define TICKLESS_TEST_DEADLINE_MICROSECONDS 2000u /**< How far each is from the sleep before it. */
/** @} */

/** @name Deadlines closer than arming the timer takes, the window of #300
 *  @{ */
#define TICKLESS_TEST_CLOSE_DEADLINES        64u  /**< How many. */
#define TICKLESS_TEST_CLOSEST_NANOSECONDS    0u   /**< The first one's distance from the sleep before it. */
#define TICKLESS_TEST_CLOSE_STEP_NANOSECONDS 250u /**< How much further each next one is. */
/** @} */

/** The vector of the tick and of the one-shot, IRQ0 remapped: the dispatcher credits it with the wake-ups it ends. */
#define TICKLESS_TEST_TIMER_VECTOR 32u

/** Cycles a wait for the tick's return spins before it gives up, rather than hang the boot. */
#define TICKLESS_TEST_WAIT_CYCLE_BUDGET 8000000000ull

static KernelWakePlan_t tickless_test_plan;
static KernelWakePlan_t tickless_test_other_plan;
static uint32_t tickless_test_runs;
static uint64_t tickless_test_instant;

/**
 * @brief Records that the test's duty ran, and at which wake-up.
 * @param context Unused.
 * @param now     The cycle the core woke at.
 */
static void tickless_test_record(void *context, uint64_t now)
{
    (void) context;
    ++tickless_test_runs;
    tickless_test_instant = now;
}

/**
 * @brief The cycles of KERNEL_RECONCILER_TICK_SAMPLE_PERIOD ticks of the tick in force.
 * @return Those cycles.
 */
static uint64_t tickless_test_reconciler_period(void)
{
    return ((uint64_t) KERNEL_RECONCILER_TICK_SAMPLE_PERIOD *
            advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz()) /
           interrupt_request_get_timer_frequency_hz();
}

/**
 * @brief Waits until the periodic tick has fired twice, or gives up.
 * @return true when it did.
 */
static bool tickless_test_tick_runs(void)
{
    const uint32_t first_tick = interrupt_request_get_tick_count();
    const uint64_t first_cycle = asmutils_read_timestamp_counter();

    while ((uint32_t) (interrupt_request_get_tick_count() - first_tick) < 2u &&
           asmutils_read_timestamp_counter() - first_cycle < TICKLESS_TEST_WAIT_CYCLE_BUDGET)
        asmutils_pause();
    return interrupt_request_get_tick_count() - first_tick >= 2u;
}

/**
 * @brief Empties the test's plan and declares one duty that records its runs.
 * @return The duty's number.
 */
static uint32_t tickless_test_begin(void)
{
    kernel_wake_planner_initialize(&tickless_test_plan);
    tickless_test_runs = 0u;
    return kernel_wake_planner_declare(&tickless_test_plan, tickless_test_record, NULL);
}

/**
 * @brief Whether the tick can be stopped on this boot: it needs the local timer calibrated.
 * @param test The test, skipped when it cannot.
 * @return true when it can.
 */
static bool tickless_test_can_stop_the_tick(KernelTest_t *test)
{
    if (advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz() != 0u)
        return true;
    kernel_test_skip(test, "the local timer was not calibrated, so the tick cannot stop");
    return false;
}

/**
 * @brief The tick stays on when no plan, or a plan with no room left, is given to carry its duty.
 */
KERNEL_TEST(stopping_the_tick_needs_a_plan_with_room_for_its_duty)
{
    kernel_test_check(test, !kernel_tickless_enable(true, NULL) && !kernel_tickless_enabled(),
                      "without a plan the tick is not stopped");

    kernel_wake_planner_initialize(&tickless_test_plan);
    for (uint32_t index = 0u; index < KERNEL_WAKE_PLANNER_DUTIES; ++index)
        (void) kernel_wake_planner_declare(&tickless_test_plan, tickless_test_record, NULL);
    kernel_test_check(test, !kernel_tickless_enable(true, &tickless_test_plan) && !kernel_tickless_enabled(),
                      "nor with a plan that has no duty left for the reconciler's pass");
}

/**
 * @brief With the tick stopped, the reconciler's pass is a duty of the session's plan that never wakes
 *        the core and rides the first wake-up a tick period after the last pass; the tick takes it back
 *        when it starts again.
 */
KERNEL_TEST(the_reconciler_pass_goes_on_with_the_tick_stopped)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    kernel_wake_planner_initialize(&tickless_test_plan);
    const uint64_t period = tickless_test_reconciler_period();
    const uint64_t before = asmutils_read_timestamp_counter();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t after = asmutils_read_timestamp_counter();
    const uint32_t passes = kernel_reconciler_get_periodic_pass_count();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test,
                      tickless_test_plan.declared != 0u &&
                          kernel_wake_planner_next_wake(&tickless_test_plan) == KERNEL_WAKE_PLANNER_NEVER,
                      "the pass is a duty of the plan that never wakes the core");
    (void) kernel_wake_planner_serve(&tickless_test_plan, before + period / 2u);
    kernel_test_check(test, kernel_reconciler_get_periodic_pass_count() == passes,
                      "a wake-up within a tick period of the stop runs no pass");
    (void) kernel_wake_planner_serve(&tickless_test_plan, after + period);
    kernel_test_check(test, kernel_reconciler_get_periodic_pass_count() == passes + 1u,
                      "the first wake-up a tick period later runs it");
    (void) kernel_wake_planner_serve(&tickless_test_plan, after + period + period / 2u);
    kernel_test_check(test, kernel_reconciler_get_periodic_pass_count() == passes + 1u,
                      "and the next one waits a period after that pass");

    kernel_tickless_disable();
    kernel_test_check(test, tickless_test_plan.declared == 0u, "the tick takes the pass back from the plan");
    kernel_test_check(test, tickless_test_tick_runs(), "and runs again");
    kernel_test_measure(test, "period_cycles_low", (uint32_t) period);
}

/**
 * @brief A sleep on a plan, with the tick stopped, serves its duty once its window has opened, at the cycle the
 *        core woke: on its deadline, by the interrupt's latency after it, which the plan's overshoot holds.
 */
KERNEL_TEST(a_sleep_on_the_plan_serves_its_duty_when_its_window_is_due)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const uint32_t duty = tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t earliest = asmutils_read_timestamp_counter() +
                              kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS);
    const uint64_t latest = earliest + kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_MICROSECONDS);
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, earliest, latest);

    uint32_t sleeps = 0u;
    uint32_t slept = 0u;
    while (tickless_test_runs == 0u && sleeps < TICKLESS_TEST_SLEEPS)
    {
        slept += kernel_tickless_sleep_on(&tickless_test_plan);
        ++sleeps;
    }
    kernel_tickless_disable();

    const uint64_t lateness = kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_LATENESS_MICROSECONDS);
    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, tickless_test_runs == 1u, "the duty is served once");
    kernel_test_check(test, tickless_test_instant >= earliest && tickless_test_instant <= latest + lateness,
                      "once its window has opened, and soon after it closed");
    kernel_test_check(test,
                      tickless_test_instant > latest ?
                          tickless_test_plan.late_services == 1u &&
                              tickless_test_plan.latest_overshoot == tickless_test_instant - latest :
                          tickless_test_plan.late_services == 0u && tickless_test_plan.latest_overshoot == 0u,
                      "late exactly by how far past its latest it ran");
    kernel_test_check(test,
                      slept >= TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS &&
                          slept <= TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS + TICKLESS_TEST_WINDOW_MICROSECONDS +
                                       TICKLESS_TEST_LATENESS_MICROSECONDS,
                      "and the microseconds it counts asleep are the window's");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "sleeps", sleeps);
    kernel_test_measure(test, "slept_us", slept);
    kernel_test_measure(test, "overshoot_us",
                        kernel_tickless_microseconds_from_cycles(tickless_test_plan.latest_overshoot));
}

/**
 * @brief A window that opens after the plan's deadline, while that deadline's interrupt is on its way, is served at
 *        the same wake-up: the plan is served at the cycle the core woke, not at the one it planned.
 */
KERNEL_TEST(a_window_that_opens_while_the_interrupt_is_on_its_way_is_served_at_that_wake_up)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const uint32_t due = tickless_test_begin();
    const uint32_t opening = kernel_wake_planner_declare(&tickless_test_plan, tickless_test_record, NULL);
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t deadline = asmutils_read_timestamp_counter() +
                              kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS);
    (void) kernel_wake_planner_arm(&tickless_test_plan, due, deadline, deadline);
    (void) kernel_wake_planner_arm(&tickless_test_plan, opening, deadline + 1u, KERNEL_WAKE_PLANNER_NEVER);

    uint32_t sleeps = 0u;
    while (tickless_test_runs == 0u && sleeps < TICKLESS_TEST_SLEEPS)
    {
        (void) kernel_tickless_sleep_on(&tickless_test_plan);
        ++sleeps;
    }
    kernel_tickless_disable();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, tickless_test_runs == 2u && tickless_test_plan.batched_wakes == 1u,
                      "the wake-up for the deadline also serves the window that opened a cycle after it");
}

/**
 * @brief A window already due when the sleep begins is served at once, without sleeping.
 */
KERNEL_TEST(a_window_already_due_is_served_without_sleeping)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const uint32_t duty = tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, 0u, 1u);
    const uint32_t sleeps = kernel_processor_sleep_count();
    const uint32_t slept = kernel_tickless_sleep_on(&tickless_test_plan);
    const uint32_t sleeps_after = kernel_processor_sleep_count();
    kernel_tickless_disable();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, tickless_test_runs == 1u && slept == 0u && sleeps_after == sleeps,
                      "the duty is served and the core never slept");
}

/**
 * @brief With the tick stopped, a plan other than the session's is refused: neither slept on nor served, since
 *        the reconciler's pass rides only the session's.
 */
KERNEL_TEST(a_plan_other_than_the_sessions_is_refused)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    (void) tickless_test_begin();
    kernel_wake_planner_initialize(&tickless_test_other_plan);
    const uint32_t other = kernel_wake_planner_declare(&tickless_test_other_plan, tickless_test_record, NULL);
    (void) kernel_wake_planner_arm(&tickless_test_other_plan, other, 0u, 1u);
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t sleeps = kernel_processor_sleep_count();
    const uint32_t slept = kernel_tickless_sleep_on(&tickless_test_other_plan);
    const uint32_t sleeps_after = kernel_processor_sleep_count();
    kernel_tickless_disable();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(
        test, tickless_test_runs == 0u && slept == 0u && sleeps_after == sleeps && tickless_test_other_plan.wakes == 0u,
        "the other plan is neither slept on nor served");
}

/**
 * @brief Slept on one after the other, deadlines that are windows of one cycle are each served once, by the
 *        interrupt's latency after it: the wake error, measured. The dispatcher, not the backend, says which
 *        interrupt ended each sleep: in TSC-deadline mode the timer ends at most one sleep per deadline, since it
 *        never fires before it, while in count mode the calibration's ratio may fire it a little before, which costs
 *        at most one more sleep per deadline over the run.
 */
KERNEL_TEST(a_deadline_wakes_the_core_by_the_interrupts_latency_after_it)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const uint32_t duty = tickless_test_begin();
    const uint32_t early_before = kernel_tickless_early_wakes();
    const uint32_t halts_before = kernel_processor_sleep_count();
    const uint32_t timer_before = kernel_wakeup_accounting_get_vector_count(TICKLESS_TEST_TIMER_VECTOR);
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    uint64_t error_sum = 0u;
    uint64_t error_max = 0u;

    for (uint32_t deadline_index = 0u; deadline_index < TICKLESS_TEST_DEADLINES; ++deadline_index)
    {
        const uint32_t runs = tickless_test_runs;
        const uint64_t deadline = asmutils_read_timestamp_counter() +
                                  kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_DEADLINE_MICROSECONDS);
        (void) kernel_wake_planner_arm(&tickless_test_plan, duty, deadline, deadline);
        for (uint32_t sleeps = 0u; tickless_test_runs == runs && sleeps < TICKLESS_TEST_SLEEPS; ++sleeps)
            (void) kernel_tickless_sleep_on(&tickless_test_plan);

        const uint64_t error = tickless_test_runs != runs ? tickless_test_instant - deadline : 0u;
        error_sum += error;
        error_max = error > error_max ? error : error_max;
    }
    const uint32_t early = kernel_tickless_early_wakes() - early_before;
    const uint32_t halts = kernel_processor_sleep_count() - halts_before;
    const uint32_t timer_wakes = kernel_wakeup_accounting_get_vector_count(TICKLESS_TEST_TIMER_VECTOR) - timer_before;
    kernel_tickless_disable();

    const bool deadlines = advanced_pic_timer_backend_arms_timestamp_counter_deadlines() != 0u;
    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, tickless_test_runs == TICKLESS_TEST_DEADLINES, "each deadline is served once");
    kernel_test_check(test,
                      deadlines ? halts == TICKLESS_TEST_DEADLINES + early : halts >= TICKLESS_TEST_DEADLINES + early,
                      "every sleep ended on a deadline served, early with the timer still armed, or in count mode on "
                      "the timer just before its deadline");
    kernel_test_check(test,
                      timer_wakes <= (deadlines ? TICKLESS_TEST_DEADLINES : 2u * TICKLESS_TEST_DEADLINES) &&
                          timer_wakes + early <= halts,
                      deadlines ? "the timer ended at most one sleep per deadline, so it never fired before one" :
                                  "the timer ended at most two sleeps per deadline, over the run");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "tsc_deadline", deadlines ? 1u : 0u);
    kernel_test_measure(test, "wake_error_mean_us",
                        kernel_tickless_microseconds_from_cycles(error_sum / TICKLESS_TEST_DEADLINES));
    kernel_test_measure(test, "wake_error_max_us", kernel_tickless_microseconds_from_cycles(error_max));
    kernel_test_measure(test, "wasted_wakes", tickless_test_plan.wasted_wakes);
    kernel_test_measure(test, "early_wakes", early);
    kernel_test_measure(test, "sleeps", halts);
    kernel_test_measure(test, "timer_wakes", timer_wakes);
}

/**
 * @brief With the tick running, a sleep on a plan lasts until the next interrupt, the tick's at the latest, and
 *        serves the plan there; no time is counted slept.
 */
KERNEL_TEST(with_the_tick_running_a_sleep_serves_the_plan_at_the_next_interrupt)
{
    const uint32_t duty = tickless_test_begin();
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, 0u, KERNEL_WAKE_PLANNER_NEVER);
    const uint32_t tick = interrupt_request_get_tick_count();
    const uint32_t slept = kernel_tickless_sleep_on(&tickless_test_plan);

    kernel_test_check(test, !kernel_tickless_enabled(), "the tick runs");
    kernel_test_check(test, tickless_test_runs == 1u && tickless_test_plan.wakes == 1u,
                      "the wake-up serves the plan once");
    kernel_test_check(test, slept == 0u, "and counts no time slept");
    kernel_test_measure(test, "ticks", interrupt_request_get_tick_count() - tick);
}

/**
 * @brief Deadlines closer than arming the timer takes, which pass while it is armed, end their sleep: none is handled
 *        just before the halt, which with the tick stopped would leave nothing to wake the core.
 *
 * @details From 0 to 16 µs away, a quarter of a microsecond further each time, so that under a hypervisor, whose
 *          arming exits cost microseconds, some pass during the arming, and the rest just after it.
 */
KERNEL_TEST(a_deadline_closer_than_its_arming_ends_the_sleep)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const uint32_t duty = tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t hertz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    for (uint32_t index = 0u; index < TICKLESS_TEST_CLOSE_DEADLINES; ++index)
    {
        const uint32_t runs = tickless_test_runs;
        const uint64_t nanoseconds =
            TICKLESS_TEST_CLOSEST_NANOSECONDS + (uint64_t) index * TICKLESS_TEST_CLOSE_STEP_NANOSECONDS;
        const uint64_t deadline = asmutils_read_timestamp_counter() + (nanoseconds * hertz) / 1000000000ull;
        (void) kernel_wake_planner_arm(&tickless_test_plan, duty, deadline, deadline);
        for (uint32_t sleeps = 0u; tickless_test_runs == runs && sleeps < TICKLESS_TEST_SLEEPS; ++sleeps)
            (void) kernel_tickless_sleep_on(&tickless_test_plan);
    }
    kernel_tickless_disable();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, tickless_test_runs == TICKLESS_TEST_CLOSE_DEADLINES,
                      "every deadline is served, however close it was");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
}
