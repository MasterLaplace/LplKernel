#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/device_interrupt.h>
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

/** @name Windows slept on with the lead learned, one round a window of one cycle then a wide one
 *  @{ */
#define TICKLESS_TEST_LED_ROUNDS              16u   /**< How many rounds. */
#define TICKLESS_TEST_LED_OPENS_MICROSECONDS  1000u /**< How long after the arming the wide window opens. */
#define TICKLESS_TEST_LED_CLOSES_MICROSECONDS 2000u /**< How long after the arming each window closes. */
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
 * @brief A device vector's handler for the test's self-interrupt: running is all it has to do.
 * @param context Unused.
 */
static void tickless_test_interrupted(void *context) { (void) context; }

/**
 * @brief Arms the test's duty for a window and sleeps on the plan until it is served, or gives up.
 * @param duty     The duty.
 * @param earliest The window's earliest cycle.
 * @param latest   Its latest cycle.
 * @return true when the duty was served; its cycle is then in tickless_test_instant.
 */
static bool tickless_test_serve_window(uint32_t duty, uint64_t earliest, uint64_t latest)
{
    const uint32_t runs = tickless_test_runs;

    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, earliest, latest);
    for (uint32_t sleeps = 0u; tickless_test_runs == runs && sleeps < TICKLESS_TEST_SLEEPS; ++sleeps)
        (void) kernel_tickless_sleep_on(&tickless_test_plan);
    return tickless_test_runs != runs;
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
 * @brief The first stop of the tick in a boot probes the depth sleeps enter, before any duty sleeps: the lead is
 *        measured, from KERNEL_TICKLESS_PROBES sleeps at most, which, when none was cut short, last at least the
 *        probes' lengths; a later stop at the same depth probes it no more. Placed before every other test that stops
 * the tick, so that on a boot that runs this file first, its stop is the one that probes.
 */
KERNEL_TEST(the_first_stop_of_the_tick_probes_the_depth_sleeps_enter)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    (void) tickless_test_begin();
    const uint32_t unprobed = kernel_tickless_wake_samples();
    const uint64_t slept_before = kernel_tickless_slept_microseconds();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t slept = kernel_tickless_slept_microseconds() - slept_before;
    const uint32_t probed = kernel_tickless_wake_samples();
    const uint64_t lead = kernel_tickless_wake_lead_cycles();
    const uint64_t probe = kernel_tickless_probe_cycles();
    kernel_tickless_disable();
    const bool again = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t probed_again = kernel_tickless_wake_samples();
    kernel_tickless_disable();

    uint64_t lengths = 0u;
    for (uint32_t index = 0u, length = KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS; index < KERNEL_TICKLESS_PROBES;
         ++index, length *= KERNEL_TICKLESS_PROBE_GROWTH)
        lengths += kernel_tickless_cycles_from_microseconds(length);

    kernel_test_check(test, stopped && again, "the tick stops, twice");
    kernel_test_check(test,
                      unprobed != 0u || (probed >= 1u && probed <= KERNEL_TICKLESS_PROBES &&
                                         (probed < KERNEL_TICKLESS_PROBES || probe >= lengths) &&
                                         slept == kernel_tickless_microseconds_from_cycles(probe)),
                      "a depth no session probed is probed by the first stop, its sleeps as long as the probes and "
                      "counted as time with the tick stopped");
    kernel_test_check(test, probed != 0u && lead != 0u, "so the lead is measured before any duty sleeps");
    kernel_test_check(test, probed_again == probed, "and a later stop probes it no more");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "probe_samples", probed);
    kernel_test_measure(test, "probe_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(probe));
    kernel_test_measure(test, "wake_lead_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(lead));
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
                          kernel_wake_planner_next_wake(&tickless_test_plan, 0u) == KERNEL_WAKE_PLANNER_NEVER,
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
 *        core woke: a lead before its latest, and when the latency passed the lead, the plan's overshoot holds how
 *        late. The time counted asleep is at least the time from the first sleep to the window's earliest, each sleep
 *        rounding down a microsecond at most: the lead can bring the wake-up as early as that earliest, and the test's
 *        own arming, before the sleep, is not slept.
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
    const uint64_t asleep = asmutils_read_timestamp_counter();
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
    const uint32_t shortest = kernel_tickless_microseconds_from_cycles(earliest > asleep ? earliest - asleep : 0u);
    kernel_test_check(
        test,
        slept + sleeps >= shortest && slept <= TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS +
                                                   TICKLESS_TEST_WINDOW_MICROSECONDS +
                                                   TICKLESS_TEST_LATENESS_MICROSECONDS,
        "and the microseconds it counts asleep are the window's, from the sleep to its earliest at least");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "sleeps", sleeps);
    kernel_test_measure(test, "slept_us", slept);
    kernel_test_measure(test, "overshoot_ns",
                        (uint32_t) kernel_tickless_nanoseconds_from_cycles(tickless_test_plan.latest_overshoot));
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
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t early_before = kernel_tickless_early_wakes();
    const uint32_t halts_before = kernel_processor_sleep_count();
    const uint32_t timer_before = kernel_wakeup_accounting_get_vector_count(TICKLESS_TEST_TIMER_VECTOR);
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
    kernel_test_measure(test, "wake_error_mean_ns",
                        (uint32_t) kernel_tickless_nanoseconds_from_cycles(error_sum / TICKLESS_TEST_DEADLINES));
    kernel_test_measure(test, "wake_error_max_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(error_max));
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
 *          arming exits cost microseconds, some pass during the arming, and the rest just after it. The defect this
 *          guards hangs the boot rather than failing the test, against the rule of this file: with the tick stopped
 *          nothing else interrupts the core, and the one other timer, the RTC's periodic interrupt, is off on every
 *          profile and reaches the core through the 8259, whose masks no interface lets a test save and give back.
 *          The run's bound turns the hang into a failure.
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

/**
 * @brief The lead the wake-ups teach serves a window at least that wide by its latest, where a window of one cycle,
 *        which no lead can move, is served its latency late; the two alternate, so that a host that slows down
 *        slows both, and a wide window costs one wake-up of the timer, as without the lead.
 *
 * @details A quarter of the wide windows may still be late: a host that holds the virtual processor for
 *          milliseconds makes a wake-up no lead could have served, and the plan counts it late as it should. The lead
 *          stays shorter than the sleeps it is taught by, which a lead taught by their length would not.
 */
KERNEL_TEST(a_lead_learned_from_the_wake_ups_serves_wide_windows_by_their_latest)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const uint32_t duty = tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t opens = kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_LED_OPENS_MICROSECONDS);
    const uint64_t closes = kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_LED_CLOSES_MICROSECONDS);
    uint32_t served = 0u;
    uint32_t late_narrow = 0u;
    uint32_t late_wide = 0u;
    uint32_t timer_wide = 0u;
    uint64_t margin_sum = 0u;
    uint64_t lead_sum = 0u;
    uint64_t lead_least = UINT64_MAX;
    uint64_t lead_most = 0u;
    uint64_t overshoot_wide = 0u;

    for (uint32_t round = 0u; round < TICKLESS_TEST_LED_ROUNDS; ++round)
    {
        const uint64_t narrow = asmutils_read_timestamp_counter() + closes;
        served += tickless_test_serve_window(duty, narrow, narrow) ? 1u : 0u;
        late_narrow += tickless_test_instant > narrow ? 1u : 0u;

        const uint32_t timer_before = kernel_wakeup_accounting_get_vector_count(TICKLESS_TEST_TIMER_VECTOR);
        const uint64_t lead_armed = kernel_tickless_wake_lead_cycles();
        lead_sum += lead_armed;
        lead_least = lead_armed < lead_least ? lead_armed : lead_least;
        lead_most = lead_armed > lead_most ? lead_armed : lead_most;
        const uint64_t armed = asmutils_read_timestamp_counter();
        served += tickless_test_serve_window(duty, armed + opens, armed + closes) ? 1u : 0u;
        timer_wide += kernel_wakeup_accounting_get_vector_count(TICKLESS_TEST_TIMER_VECTOR) - timer_before;
        if (tickless_test_instant > armed + closes)
        {
            ++late_wide;
            if (tickless_test_instant - (armed + closes) > overshoot_wide)
                overshoot_wide = tickless_test_instant - (armed + closes);
        }
        else
            margin_sum += armed + closes - tickless_test_instant;
    }
    const uint64_t lead = kernel_tickless_wake_lead_cycles();
    kernel_tickless_disable();

    const uint32_t on_time = TICKLESS_TEST_LED_ROUNDS - late_wide;
    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, served == 2u * TICKLESS_TEST_LED_ROUNDS, "every window is served once");
    kernel_test_check(test, late_narrow == TICKLESS_TEST_LED_ROUNDS,
                      "the control: each window of one cycle keeps its arming and is served its latency late");
    kernel_test_check(test, late_wide <= TICKLESS_TEST_LED_ROUNDS / 4u,
                      "the wide windows are served by their latest, but for a held wake-up now and then");
    kernel_test_check(test, timer_wide <= TICKLESS_TEST_LED_ROUNDS,
                      "and each took at most one wake-up of the timer, as without the lead");
    kernel_test_check(test, lead_least != lead_most,
                      "the control: the timer's wake-ups move the lead from round to round");
    kernel_test_check(test, lead_sum / TICKLESS_TEST_LED_ROUNDS < closes,
                      "the lead is taught by the timer's lateness, not by the sleeps, and stays shorter than them");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "tsc_deadline", advanced_pic_timer_backend_arms_timestamp_counter_deadlines());
    kernel_test_measure(test, "late_wide", late_wide);
    kernel_test_measure(test, "late_narrow", late_narrow);
    kernel_test_measure(test, "timer_wakes_wide", timer_wide);
    kernel_test_measure(test, "wake_lead_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(lead));
    kernel_test_measure(test, "lead_mean_ns",
                        (uint32_t) kernel_tickless_nanoseconds_from_cycles(lead_sum / TICKLESS_TEST_LED_ROUNDS));
    kernel_test_measure(test, "overshoot_wide_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(overshoot_wide));
    kernel_test_measure(test, "margin_mean_ns",
                        on_time != 0u ? (uint32_t) kernel_tickless_nanoseconds_from_cycles(margin_sum / on_time) : 0u);
}

/**
 * @brief A sleep another interrupt ends before its deadline teaches the lead nothing: only the timer's own wake-ups
 *        tell how late it comes. The interrupt is sent to this processor with interrupts masked, so that it is
 *        pending when the sleep begins and ends it at once.
 */
KERNEL_TEST(a_sleep_another_interrupt_ends_teaches_the_lead_nothing)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;
    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return;
    }
    uint8_t vector = 0u;
    if (!kernel_test_check(test, device_interrupt_allocate(tickless_test_interrupted, NULL, &vector),
                           "a device vector is free"))
        return;

    const uint32_t duty = tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t opens = kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_LED_OPENS_MICROSECONDS);
    const uint64_t closes = kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_LED_CLOSES_MICROSECONDS);
    const uint64_t lead_cold = kernel_tickless_wake_lead_cycles();
    const uint64_t taught = asmutils_read_timestamp_counter();
    const bool warmed = tickless_test_serve_window(duty, taught + opens, taught + closes) &&
                        tickless_test_serve_window(duty, tickless_test_instant + opens, tickless_test_instant + closes);

    const uint64_t lead_before = kernel_tickless_wake_lead_cycles();
    const uint32_t early_before = kernel_tickless_early_wakes();
    const uint64_t armed = asmutils_read_timestamp_counter();
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, armed + opens, armed + closes);
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    const bool sent = advanced_pic_ipi_send_fixed(0u, vector, ADVANCED_PIC_IPI_SHORT_SELF) != 0u;
    (void) kernel_tickless_sleep_on(&tickless_test_plan);
    asmutils_restore_flags(flags);
    const uint64_t lead_after = kernel_tickless_wake_lead_cycles();
    const uint32_t early = kernel_tickless_early_wakes() - early_before;
    kernel_tickless_disable();
    device_interrupt_release(vector);

    kernel_test_check(test, stopped && warmed && lead_before != lead_cold,
                      "the control: the tick stops, and a window the timer serves moves the lead");
    kernel_test_check(test, sent && early == 1u, "the interrupt ends the next sleep before its deadline");
    kernel_test_check(test, lead_before != 0u && lead_after == lead_before, "and that sleep leaves the lead as it was");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "wake_lead_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(lead_after));
}

/** @name The decision to probe a depth before a sleep, in cycles
 *  @{ */
#define TICKLESS_TEST_PROBE_SPAN 5000u  /**< A probe's span. */
#define TICKLESS_TEST_FAR_WAKE   10000u /**< A wake-up the probe ends well before. */
/** @} */

/** How far the window a changed depth's first sleep serves opens after the arming, past any probe: microseconds. */
#define TICKLESS_TEST_CHANGED_DEPTH_OPENS_MICROSECONDS 20000u

/**
 * @brief A sleep at a depth no wake-up taught probes it first only when it was not probed since the tick stopped and
 *        its probe ends before the plan's next wake-up, one ending on that wake-up included.
 */
KERNEL_TEST(a_depth_is_probed_before_a_sleep_once_and_only_when_the_probe_fits)
{
    kernel_test_check(test,
                      kernel_tickless_probes_before_sleeping(false, TICKLESS_TEST_FAR_WAKE, TICKLESS_TEST_PROBE_SPAN),
                      "an unprobed depth whose probe ends before the wake-up is probed");
    kernel_test_check(test,
                      kernel_tickless_probes_before_sleeping(false, TICKLESS_TEST_PROBE_SPAN, TICKLESS_TEST_PROBE_SPAN),
                      "a probe that ends on the wake-up still fits");
    kernel_test_check(test,
                      !kernel_tickless_probes_before_sleeping(true, TICKLESS_TEST_FAR_WAKE, TICKLESS_TEST_PROBE_SPAN),
                      "a depth probed since the tick stopped is not probed again");
    kernel_test_check(
        test, !kernel_tickless_probes_before_sleeping(false, TICKLESS_TEST_PROBE_SPAN - 1u, TICKLESS_TEST_PROBE_SPAN),
        "a probe that would end a cycle past the wake-up is not taken");
    kernel_test_check(test, !kernel_tickless_probes_before_sleeping(false, 0u, TICKLESS_TEST_PROBE_SPAN),
                      "nor before a wake-up already due");
}

/**
 * @brief Picks a depth this processor offers, other than @p first, that no wake-up taught, and enters it.
 * @param first The depth the session entered first.
 * @return That depth, or @p first, entered again, when every other one learned a wake-up earlier in the boot.
 */
static uint32_t tickless_test_enter_an_unlearned_depth(uint32_t first)
{
    const uint32_t hints = kernel_processor_sleep_available_hints();

    for (uint32_t hint = PROCESSOR_SLEEP_HINT_C1; hint <= PROCESSOR_SLEEP_HINT_MAX; ++hint)
        if (hint != first && ((hints >> hint) & 1u) != 0u && kernel_processor_sleep_request_hint(hint) &&
            kernel_tickless_wake_samples() == 0u)
            return hint;
    (void) kernel_processor_sleep_request_hint(first);
    return first;
}

/**
 * @brief A depth entered with the tick stopped, which no wake-up taught, is probed at its first sleep, whose window
 *        opens past any probe, so that its lead is measured rather than assumed; skipped where the processor offers
 *        one depth.
 */
KERNEL_TEST(a_depth_entered_with_the_tick_stopped_is_probed_at_its_first_sleep)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;
    const uint32_t hints = kernel_processor_sleep_available_hints();
    if ((hints & (hints - 1u)) == 0u)
    {
        kernel_test_skip(test, "the processor offers one depth: no QEMU model lets the kernel use MONITOR (#818)");
        return;
    }

    const uint32_t duty = tickless_test_begin();
    const uint32_t first = kernel_processor_sleep_active_hint();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t entered = stopped ? tickless_test_enter_an_unlearned_depth(first) : first;
    if (stopped && entered == first)
    {
        kernel_tickless_disable();
        kernel_test_skip(test, "every other depth learned a wake-up earlier in this boot");
        return;
    }

    const uint64_t probe_before = kernel_tickless_probe_cycles();
    const uint64_t opens = asmutils_read_timestamp_counter() +
                           kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_CHANGED_DEPTH_OPENS_MICROSECONDS);
    const bool served =
        stopped &&
        tickless_test_serve_window(duty, opens,
                                   opens + kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_MICROSECONDS));
    const uint32_t samples = kernel_tickless_wake_samples();
    const uint64_t probe = kernel_tickless_probe_cycles();
    (void) kernel_processor_sleep_request_hint(first);
    kernel_tickless_disable();

    kernel_test_check(test, stopped && served, "the tick stops and the window is served");
    kernel_test_check(test, probe != probe_before, "the depth entered is probed at its first sleep");
    kernel_test_check(test, samples >= 1u, "so its lead is learned");
    kernel_test_measure(test, "entered_hint", entered);
    kernel_test_measure(test, "probe_samples", samples);
    kernel_test_measure(test, "probe_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(probe));
}

/** @name Windows armed at a depth made to have learned nothing, from the arming, in microseconds
 *  @{ */
#define TICKLESS_TEST_NEAR_OPENS_MICROSECONDS  100u   /**< A near one opens. */
#define TICKLESS_TEST_NEAR_CLOSES_MICROSECONDS 1000u  /**< It closes, the bound's wake-up nearer than a probe ends. */
#define TICKLESS_TEST_FAR_OPENS_MICROSECONDS   20000u /**< A far one opens, past any probe. */
#define TICKLESS_TEST_FAR_CLOSES_MICROSECONDS  22000u /**< It closes. */
#define TICKLESS_TEST_DUE_CLOSES_MICROSECONDS  100u   /**< One open from the arming closes nearer than the bound. */
/** @} */

/**
 * @struct TicklessTestUnlearned_t
 * @brief What serving one window taught, at a depth made to have learned nothing.
 */
typedef struct {
    bool stopped;            /**< The tick stopped. */
    bool served;             /**< The window was served. */
    uint32_t on_the_bound;   /**< Wake-ups planned on the bound meanwhile. */
    uint32_t on_a_published; /**< Wake-ups planned on a published lead meanwhile. */
    uint32_t sleeps;         /**< Times the processor slept meanwhile. */
    uint32_t samples;        /**< Wake-ups the depth learned by the window's end. */
    bool probed;             /**< The depth was probed meanwhile. */
} TicklessTestUnlearned_t;

/**
 * @brief Stops the tick, makes the depth sleeps enter learn nothing, serves a window from @p opens to @p closes
 *        microseconds after the arming, then gives the depth back its estimate and starts the tick again.
 * @param opens  When the window opens.
 * @param closes When it closes.
 * @return What serving it taught.
 */
static TicklessTestUnlearned_t tickless_test_serve_at_an_unlearned_depth(uint32_t opens, uint32_t closes)
{
    TicklessTestUnlearned_t seen = {0};
    const uint32_t duty = tickless_test_begin();

    seen.stopped = kernel_tickless_enable(true, &tickless_test_plan);
    if (!seen.stopped)
        return seen;

    const uint32_t depth = kernel_processor_sleep_depth();
    KernelWakeLatency_t held = {0};
    kernel_tickless_debug_exchange_depth_estimate(depth, &held);
    const uint32_t bound_before = kernel_tickless_wakes_planned_on_the_bound();
    const uint32_t published_before = kernel_tickless_wakes_planned_on_a_published_lead();
    const uint32_t sleeps_before = kernel_processor_sleep_count();
    const uint64_t probe_before = kernel_tickless_probe_cycles();
    const uint64_t armed = asmutils_read_timestamp_counter();

    seen.served = tickless_test_serve_window(duty, armed + kernel_tickless_cycles_from_microseconds(opens),
                                             armed + kernel_tickless_cycles_from_microseconds(closes));
    seen.samples = kernel_tickless_wake_samples();
    seen.on_the_bound = kernel_tickless_wakes_planned_on_the_bound() - bound_before;
    seen.on_a_published = kernel_tickless_wakes_planned_on_a_published_lead() - published_before;
    seen.sleeps = kernel_processor_sleep_count() - sleeps_before;
    seen.probed = kernel_tickless_probe_cycles() != probe_before;
    kernel_tickless_debug_exchange_depth_estimate(depth, &held);
    kernel_tickless_disable();
    return seen;
}

/**
 * @brief A window whose wake-up, a bound before its latest, comes before a probe would end is served at a depth that
 *        learned nothing without a probe, its wake-up planned on the bound, the shallowest depth having no shallower
 *        lead, and the depth learns from that sleep.
 */
KERNEL_TEST(a_near_window_at_a_depth_that_learned_nothing_is_planned_on_the_bound_without_a_probe)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const TicklessTestUnlearned_t seen = tickless_test_serve_at_an_unlearned_depth(
        TICKLESS_TEST_NEAR_OPENS_MICROSECONDS, TICKLESS_TEST_NEAR_CLOSES_MICROSECONDS);

    kernel_test_check(test, seen.stopped && seen.served, "the tick stops and the window is served");
    kernel_test_check(test, !seen.probed, "no probe, which would end after the wake-up");
    kernel_test_check(test, seen.on_the_bound >= 1u && seen.on_a_published == 0u,
                      "the wake-up is planned on the bound, never on a published latency alone");
    kernel_test_check(test, seen.samples >= 1u || seen.on_the_bound > 1u,
                      "the depth learns from its sleep, or plans the next on the bound again");
    kernel_test_measure(test, "near_wakes_on_bound", seen.on_the_bound);
    kernel_test_measure(test, "near_samples", seen.samples);
}

/**
 * @brief A window whose wake-up comes after a probe would end is served at a depth that learned nothing once the depth
 *        is probed, its wake-up then planned on the lead the probe taught.
 */
KERNEL_TEST(a_far_window_at_a_depth_that_learned_nothing_is_planned_after_probing_it)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const TicklessTestUnlearned_t seen = tickless_test_serve_at_an_unlearned_depth(
        TICKLESS_TEST_FAR_OPENS_MICROSECONDS, TICKLESS_TEST_FAR_CLOSES_MICROSECONDS);

    kernel_test_check(test, seen.stopped && seen.served, "the tick stops and the window is served");
    kernel_test_check(test, seen.probed, "the depth is probed first");
    kernel_test_check(test, seen.samples >= 2u && seen.on_the_bound == 0u && seen.on_a_published == 0u,
                      "so the wake-up is planned on the lead the probe taught, and the sleep teaches it more");
    kernel_test_measure(test, "far_samples", seen.samples);
}

/**
 * @brief A window already due at a depth that learned nothing is served without sleeping or probing, and the wake-up
 *        the plan chose for it, on the bound, is counted all the same.
 */
KERNEL_TEST(a_window_already_due_at_a_depth_that_learned_nothing_is_served_without_sleeping_and_counted)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const TicklessTestUnlearned_t seen =
        tickless_test_serve_at_an_unlearned_depth(0u, TICKLESS_TEST_DUE_CLOSES_MICROSECONDS);

    kernel_test_check(test, seen.stopped && seen.served, "the tick stops and the window is served");
    kernel_test_check(test, seen.sleeps == 0u && !seen.probed && seen.samples == 0u,
                      "without sleeping, probing or learning");
    kernel_test_check(test, seen.on_the_bound == 1u, "its one wake-up planned on the bound is counted");
    kernel_test_measure(test, "due_wakes_on_bound", seen.on_the_bound);
}
