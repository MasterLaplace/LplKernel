#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_park.h>
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
 *        probes' lengths, the only time counted slept but the calibration's, which only the energy counter makes; a
 *        later stop at the same depth probes it no more. Placed before every other test that stops
 * the tick, so that on a boot that runs this file first, its stop is the one that probes.
 */
KERNEL_TEST(the_first_stop_of_the_tick_probes_the_depth_sleeps_enter)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    (void) tickless_test_begin();
    const uint32_t unprobed = kernel_tickless_wake_samples();
    const uint64_t slept_before = kernel_tickless_slept_microseconds();
    const uint64_t calibrated_before = kernel_tickless_calibration_cycles();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t slept = kernel_tickless_slept_microseconds() - slept_before;
    const uint64_t calibrated = kernel_tickless_calibration_cycles() - calibrated_before;
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
                                         slept == kernel_tickless_microseconds_from_cycles(probe) +
                                                      kernel_tickless_microseconds_from_cycles(calibrated)),
                      "a depth no session probed is probed by the first stop, its sleeps as long as the probes and "
                      "counted as time with the tick stopped, with the calibration's where the counter made one");
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

/** How long the window-due test holds a sleep's depth choice: past its window's close, whenever the sleep began. */
#define TICKLESS_TEST_HELD_CHOICE_MICROSECONDS                                                                         \
    (2u * (TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS + TICKLESS_TEST_WINDOW_MICROSECONDS))

/**
 * @struct TicklessTestHeldChoice_t
 * @brief What one sleep on the plan, its depth choice held, counted and served.
 */
typedef struct {
    bool chose;                /**< The depth it entered had learned a wake-up, so that the sleep chose a depth. */
    bool served;               /**< The sleep served the test's duty. */
    uint32_t slept;            /**< The microseconds it counted asleep. */
    uint32_t served_to_return; /**< The microseconds from the cycle it served the duty at to its return. */
} TicklessTestHeldChoice_t;

/**
 * @brief A copy of the estimate sleeps at @p depth arm with, taken out through the debug seam and put back at once.
 *
 * @details The seam also forgets that the depth was probed since the tick stopped, which only a depth that learned
 *          nothing consults: a copy of a learned estimate changes nothing else.
 *
 * @param depth The depth.
 * @return The copy.
 */
static KernelWakeLatency_t tickless_test_estimate_at(uint32_t depth)
{
    KernelWakeLatency_t taken = {0};

    kernel_tickless_debug_exchange_depth_estimate(depth, &taken);
    const KernelWakeLatency_t estimate = taken;
    kernel_tickless_debug_exchange_depth_estimate(depth, &taken);
    return estimate;
}

/**
 * @brief Arms the test's duty for a window as wide as the window-due test's, holds the next depth choice past its
 *        close, and sleeps on the plan once; then gives the depth back the estimate it had, since the timer, armed on
 *        a deadline the hold let pass, would otherwise leave the rest of the boot a lead taught by the hold.
 * @param duty The test's duty.
 * @return What the sleep counted and served.
 */
static TicklessTestHeldChoice_t tickless_test_sleep_with_the_choice_held(uint32_t duty)
{
    const uint32_t runs = tickless_test_runs;
    const uint32_t depth = kernel_processor_sleep_depth();
    KernelWakeLatency_t estimate = tickless_test_estimate_at(depth);
    const uint64_t earliest = asmutils_read_timestamp_counter() +
                              kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS);

    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, earliest,
                                   earliest +
                                       kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_MICROSECONDS));
    kernel_tickless_debug_hold_the_next_depth_choice(TICKLESS_TEST_HELD_CHOICE_MICROSECONDS);
    const uint32_t slept = kernel_tickless_sleep_on(&tickless_test_plan);
    const uint64_t returned = asmutils_read_timestamp_counter();
    kernel_tickless_debug_hold_the_next_depth_choice(0u);
    const bool chose = estimate.samples != 0u;
    kernel_tickless_debug_exchange_depth_estimate(depth, &estimate);

    const bool served = tickless_test_runs == runs + 1u;
    return (TicklessTestHeldChoice_t){
        chose, served, slept, served ? kernel_tickless_microseconds_from_cycles(returned - tickless_test_instant) : 0u};
}

/**
 * @brief A sleep on a plan, with the tick stopped, serves its duty once its window has opened, at the cycle the
 *        core woke: a lead before its latest, and when the latency passed the lead, the plan's overshoot holds how
 *        late; the time it counts asleep stays within the window and its lateness. A sleep counts from before it
 *        chooses its depth: one whose choice is held past its window's close, as TCG's first translation of the choice
 *        may take (#299), counts the hold asleep, or, when its window was due before it began, serves the duty at the
 *        cycle it began, once the held choice is made; either way the hold lies between the start of the count and the
 *        sleep's return.
 *
 * @details The start of the count is not read against the test's own reading of the counter before the sleep: on a
 *          loaded host the processor may be held for milliseconds between the two, and a lead learned on that host may
 *          plan the wake-up on the window's earliest, leaving no margin (#879). The held choice makes the start
 *          observable whatever the host does: the hold lies whole in one of two durations, the microseconds the sleep
 *          counted or those from its service to its return, whose conversion to microseconds rounds down by one at
 *          most. The control: the depth the held sleep enters learned a wake-up, without which no depth is chosen.
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
    const uint32_t runs = tickless_test_runs;
    const uint64_t instant = tickless_test_instant;
    const uint32_t late_services = tickless_test_plan.late_services;
    const uint64_t overshoot = tickless_test_plan.latest_overshoot;
    const TicklessTestHeldChoice_t held =
        stopped ? tickless_test_sleep_with_the_choice_held(duty) : (TicklessTestHeldChoice_t){false, false, 0u, 0u};
    kernel_tickless_disable();

    const uint64_t lateness = kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_LATENESS_MICROSECONDS);
    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, runs == 1u, "the duty is served once");
    kernel_test_check(test, instant >= earliest && instant <= latest + lateness,
                      "once its window has opened, and soon after it closed");
    kernel_test_check(test,
                      instant > latest ? late_services == 1u && overshoot == instant - latest :
                                         late_services == 0u && overshoot == 0u,
                      "late exactly by how far past its latest it ran");
    kernel_test_check(test,
                      slept <= TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS + TICKLESS_TEST_WINDOW_MICROSECONDS +
                                   TICKLESS_TEST_LATENESS_MICROSECONDS,
                      "and the microseconds it counts asleep stay within the window and its lateness");
    kernel_test_check(test, held.chose, "the control: the depth the held sleep enters learned a wake-up");
    kernel_test_check(test,
                      held.served && held.slept + held.served_to_return + 1u >= TICKLESS_TEST_HELD_CHOICE_MICROSECONDS,
                      "a sleep whose depth choice is held past its window's close counts the hold: it counts from "
                      "before its choice");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "sleeps", sleeps);
    kernel_test_measure(test, "slept_us", slept);
    kernel_test_measure(test, "overshoot_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(overshoot));
    kernel_test_measure(test, "held_slept_us", held.slept);
    kernel_test_measure(test, "held_served_to_return_us", held.served_to_return);
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
 * @struct TicklessTestTaught_t
 * @brief What the wake-ups of a run of windows taught the lead, as far as the test judged it.
 */
typedef struct {
    uint32_t judged; /**< Windows one sleep served that taught the depth one wake-up. */
    uint32_t taught; /**< Those whose wake-up taught the estimate its lateness past the planned cycle alone. */
} TicklessTestTaught_t;

/**
 * @brief Whether two estimates hold the same lesson, field by field.
 * @param left  One estimate.
 * @param right The other.
 * @return true when every field is equal.
 */
static bool tickless_test_same_estimate(const KernelWakeLatency_t *left, const KernelWakeLatency_t *right)
{
    return left->smoothed_eighths == right->smoothed_eighths && left->deviation_quarters == right->deviation_quarters &&
           left->lead == right->lead && left->samples == right->samples;
}

/**
 * @brief Serves the test's duty for a window as tickless_test_serve_window does, and judges what its wake-up taught
 *        the depth sleeps enter: when one sleep served the window and taught the depth one wake-up, the estimate after
 *        it must be the one before taught that wake-up's lateness past the cycle the plan armed with the lead before.
 * @param duty     The duty.
 * @param earliest The window's earliest cycle.
 * @param latest   Its latest cycle.
 * @param seen     Counts the windows judged, and those taught their lateness.
 * @return true when the duty was served; its cycle is then in tickless_test_instant.
 */
static bool tickless_test_serve_window_and_judge_the_lesson(uint32_t duty, uint64_t earliest, uint64_t latest,
                                                            TicklessTestTaught_t *seen)
{
    const uint32_t depth = kernel_processor_sleep_depth();
    const KernelWakeLatency_t before = tickless_test_estimate_at(depth);
    const uint32_t halts = kernel_processor_sleep_count();
    const uint32_t runs = tickless_test_runs;

    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, earliest, latest);
    const uint64_t planned = kernel_wake_planner_next_wake(&tickless_test_plan, kernel_wake_latency_lead(&before));
    for (uint32_t sleeps = 0u; tickless_test_runs == runs && sleeps < TICKLESS_TEST_SLEEPS; ++sleeps)
        (void) kernel_tickless_sleep_on(&tickless_test_plan);

    const KernelWakeLatency_t after = tickless_test_estimate_at(depth);
    const bool served = tickless_test_runs != runs;
    if (!served || kernel_processor_sleep_count() - halts != 1u || after.samples != before.samples + 1u)
        return served;

    KernelWakeLatency_t expected = before;
    kernel_wake_latency_learn(&expected, tickless_test_instant > planned ? tickless_test_instant - planned : 0u);
    ++seen->judged;
    seen->taught += tickless_test_same_estimate(&expected, &after) ? 1u : 0u;
    return served;
}

/**
 * @brief The lead the wake-ups teach serves a window at least that wide by its latest, where a window of one cycle,
 *        which no lead can move, is served its latency late; the two alternate, so that a host that slows down
 *        slows both, and a wide window costs one wake-up of the timer, as without the lead.
 *
 * @details A quarter of the wide windows may still be late: a host that holds the virtual processor for
 *          milliseconds makes a wake-up no lead could have served, and the plan counts it late as it should. What
 *          teaches the lead is checked wake-up by wake-up rather than by its size, which on a loaded host follows the
 *          host's lateness past the sleeps' length as it should (#847): each window one sleep served, which taught the
 *          depth one wake-up, leaves the estimate taught exactly that wake-up's lateness past the cycle the plan armed,
 *          which a lead taught by the sleep's length is not, but on a window whose two values both pass the clip. At
 *          least half the windows are judged so, the control that the check is not empty: a host that held every
 *          sleep past its window would leave none judged and fail it, as a kernel that never learns does.
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
    TicklessTestTaught_t taught = {0u, 0u};

    for (uint32_t round = 0u; round < TICKLESS_TEST_LED_ROUNDS; ++round)
    {
        const uint64_t narrow = asmutils_read_timestamp_counter() + closes;
        served += tickless_test_serve_window_and_judge_the_lesson(duty, narrow, narrow, &taught) ? 1u : 0u;
        late_narrow += tickless_test_instant > narrow ? 1u : 0u;

        const uint32_t timer_before = kernel_wakeup_accounting_get_vector_count(TICKLESS_TEST_TIMER_VECTOR);
        const uint64_t lead_armed = kernel_tickless_wake_lead_cycles();
        lead_sum += lead_armed;
        lead_least = lead_armed < lead_least ? lead_armed : lead_least;
        lead_most = lead_armed > lead_most ? lead_armed : lead_most;
        const uint64_t armed = asmutils_read_timestamp_counter();
        served +=
            tickless_test_serve_window_and_judge_the_lesson(duty, armed + opens, armed + closes, &taught) ? 1u : 0u;
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
    kernel_test_check(
        test, taught.judged >= TICKLESS_TEST_LED_ROUNDS,
        "the control: half the windows at least are served by one sleep that teaches the depth one wake-up");
    kernel_test_check(test, taught.taught == taught.judged,
                      "each of those wake-ups teaches the lead its lateness past the cycle the plan armed, not the "
                      "sleep's length");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "tsc_deadline", advanced_pic_timer_backend_arms_timestamp_counter_deadlines());
    kernel_test_measure(test, "lessons_judged", taught.judged);
    kernel_test_measure(test, "lessons_taught", taught.taught);
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

/** Tries the other-interrupt test makes at most: one more each time a try could not be judged. */
#define TICKLESS_TEST_INTERRUPTED_TRIES 4u

/**
 * @struct TicklessTestInterruptedSleep_t
 * @brief What one sleep a pending interrupt should end did, and what it taught.
 */
typedef struct {
    bool sent;              /**< The interrupt was sent. */
    bool served_when_due;   /**< No sleep: the duty was served at a cycle at or past the window's planned wake-up. */
    bool returned_when_due; /**< The sleep returned at or past the window's planned wake-up. */
    uint32_t sleeps;        /**< Times the processor slept. */
    uint32_t ended;         /**< Wake-ups the dispatcher credited to the interrupt. */
    uint32_t early;         /**< Sleeps the kernel counted as ended before their deadline. */
    uint64_t lead_before;   /**< The lead before the sleep. */
    uint64_t lead_after;    /**< The lead after it. */
} TicklessTestInterruptedSleep_t;

/**
 * @brief Sends this processor an interrupt with interrupts masked, arms the test's duty for a window from @p opens to
 *        @p closes cycles after, and sleeps on the plan once, the interrupt pending.
 * @param duty   The test's duty.
 * @param vector The interrupt's vector.
 * @param opens  When the window opens after the arming.
 * @param closes When it closes.
 * @return What the sleep did and taught.
 */
static TicklessTestInterruptedSleep_t tickless_test_sleep_with_an_interrupt_pending(uint32_t duty, uint8_t vector,
                                                                                    uint64_t opens, uint64_t closes)
{
    TicklessTestInterruptedSleep_t seen = {.lead_before = kernel_tickless_wake_lead_cycles()};
    const uint32_t early_before = kernel_tickless_early_wakes();
    const uint32_t runs = tickless_test_runs;
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

    seen.sent = advanced_pic_ipi_send_fixed(0u, vector, ADVANCED_PIC_IPI_SHORT_SELF) != 0u;
    const uint64_t armed = asmutils_read_timestamp_counter();
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, armed + opens, armed + closes);
    const uint64_t planned = kernel_wake_planner_next_wake(&tickless_test_plan, seen.lead_before);
    const uint32_t halts = kernel_processor_sleep_count();
    const uint32_t ended = kernel_wakeup_accounting_get_vector_count(vector);
    (void) kernel_tickless_sleep_on(&tickless_test_plan);
    const uint64_t returned = asmutils_read_timestamp_counter();
    seen.sleeps = kernel_processor_sleep_count() - halts;
    seen.ended = kernel_wakeup_accounting_get_vector_count(vector) - ended;
    asmutils_restore_flags(flags);

    seen.early = kernel_tickless_early_wakes() - early_before;
    seen.lead_after = kernel_tickless_wake_lead_cycles();
    seen.served_when_due = seen.sleeps == 0u && tickless_test_runs != runs && tickless_test_instant >= planned;
    seen.returned_when_due = returned >= planned;
    return seen;
}

/**
 * @brief Whether a try can judge the claim: unless the window was due before its sleep began, or the interrupt
 *        ended the sleep and the kernel took it for the timer's wake-up only once the planned wake-up had passed.
 * @param seen The try.
 * @return true when it can.
 */
static bool tickless_test_interrupted_sleep_judges(const TicklessTestInterruptedSleep_t *seen)
{
    return !seen->served_when_due && !(seen->ended == 1u && seen->early == 0u && seen->returned_when_due);
}

/**
 * @brief A sleep another interrupt ends before its deadline teaches the lead nothing: only the timer's own wake-ups
 *        tell how late it comes. The interrupt is sent to this processor with interrupts masked, so that it is
 *        pending when the sleep begins and ends it at once; the dispatcher says which interrupt ended the sleep.
 *
 * @details The window is armed once the interrupt is sent, so that sending it, slow under TCG, takes nothing from the
 *          margin before the window's planned wake-up. A try cannot judge the claim when the processor runs past that
 *          wake-up before the sleep begins, which leaves the window due and no sleep for the interrupt to end, or
 *          past the deadline once the interrupt ended the sleep, which the kernel, reading the timer then, takes for
 *          the timer's wake-up. A loaded host does either now and then (#847), a kernel that is slow there every
 *          time: the test tries again, TICKLESS_TEST_INTERRUPTED_TRIES times at most, and fails when no try could
 *          judge. A sleep the timer ended rather than the interrupt fails.
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
    const uint64_t lead_warmed = kernel_tickless_wake_lead_cycles();

    TicklessTestInterruptedSleep_t seen = {0};
    uint32_t tries = 0u;
    bool judged = false;
    while (stopped && !judged && tries < TICKLESS_TEST_INTERRUPTED_TRIES)
    {
        seen = tickless_test_sleep_with_an_interrupt_pending(duty, vector, opens, closes);
        judged = tickless_test_interrupted_sleep_judges(&seen);
        ++tries;
    }
    kernel_tickless_disable();
    device_interrupt_release(vector);

    kernel_test_check(test, stopped && warmed && lead_warmed != lead_cold,
                      "the control: the tick stops, and a window the timer serves moves the lead");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "wake_lead_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(seen.lead_after));
    kernel_test_measure(test, "tries", tries);
    kernel_test_measure(test, "sleeps", seen.sleeps);
    kernel_test_measure(test, "interrupt_wakes", seen.ended);
    kernel_test_measure(test, "early_wakes", seen.early);
    if (!kernel_test_check(test, judged,
                           "a try judges: the window is not due before its sleep, nor the deadline past when the "
                           "kernel reads the clock after the interrupt ended it"))
        return;
    kernel_test_check(test, seen.sent && seen.sleeps == 1u && seen.ended == 1u, "the interrupt ends the next sleep");
    kernel_test_check(test, seen.early == 1u, "before its deadline, the timer still armed");
    kernel_test_check(test, seen.lead_before != 0u && seen.lead_after == seen.lead_before,
                      "and that sleep leaves the lead as it was");
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
    const uint32_t depths = kernel_processor_sleep_available_depths();

    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth <= PROCESSOR_SLEEP_DEPTH_DEEPEST; ++depth)
        if (depth != first && ((depths >> depth) & 1u) != 0u && kernel_processor_sleep_request_depth(depth) &&
            kernel_tickless_wake_samples_at(depth) == 0u)
            return depth;
    (void) kernel_processor_sleep_request_depth(first);
    return first;
}

/**
 * @brief A depth entered with the tick stopped, which no wake-up taught, is probed at its first sleep, whose window
 *        opens past any probe, so that its lead is measured rather than assumed, or, a port depth whose reads did not
 *        stop the processor, refused; skipped where the processor offers one depth.
 */
KERNEL_TEST(a_depth_entered_with_the_tick_stopped_is_probed_at_its_first_sleep)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;
    const uint32_t depths = kernel_processor_sleep_available_depths();
    if ((depths & (depths - 1u)) == 0u)
    {
        kernel_test_skip(test, "the processor offers one depth: no QEMU model lets the kernel use MONITOR (#818)");
        return;
    }

    const uint32_t duty = tickless_test_begin();
    const uint32_t first = kernel_processor_sleep_depth();
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
    const uint32_t samples = kernel_tickless_wake_samples_at(entered);
    const uint32_t kept = kernel_processor_sleep_depth();
    const uint64_t probe = kernel_tickless_probe_cycles();
    (void) kernel_processor_sleep_request_depth(first);
    kernel_tickless_disable();

    const bool refused = kept != entered;

    kernel_test_check(test, stopped && served, "the tick stops and the window is served");
    kernel_test_check(test, probe != probe_before, "the depth entered is probed at its first sleep");
    kernel_test_check(test,
                      refused ? kernel_tickless_judge_a_probe(kernel_tickless_stopped_probe_ranks_at(entered),
                                                              kernel_tickless_unstopped_probe_ranks_at(entered)) !=
                                    KERNEL_TICKLESS_PROBE_KEEPS :
                                samples >= 1u,
                      "so its lead is learned, or it is refused for reads that did not stop");
    kernel_test_measure(test, "entered_depth", entered);
    kernel_test_measure(test, "refused", refused ? 1u : 0u);
    kernel_test_measure(test, "probe_samples", samples);
    kernel_test_measure(test, "probe_ns", (uint32_t) kernel_tickless_nanoseconds_from_cycles(probe));
}

/** @name Windows armed at a depth made to have learned nothing, from the arming, in microseconds
 *  @{ */
#define TICKLESS_TEST_NEAR_OPENS_MICROSECONDS  100u   /**< A near one opens. */
#define TICKLESS_TEST_NEAR_CLOSES_MICROSECONDS 3000u  /**< It closes, the bound's wake-up nearer than a probe ends. */
#define TICKLESS_TEST_FAR_OPENS_MICROSECONDS   20000u /**< A far one opens, past any probe. */
#define TICKLESS_TEST_FAR_CLOSES_MICROSECONDS  22000u /**< It closes. */
#define TICKLESS_TEST_DUE_CLOSES_MICROSECONDS  100u   /**< One open from the arming closes nearer than the bound. */
/** @} */

_Static_assert(KERNEL_TICKLESS_PROBES == 4u, "the near window's wake-up grows the first probe three times");

/**
 * When the near window's wake-up comes after its arming: its close, less the bound a depth that learned nothing is led
 * by, half the longest probe sleep.
 */
#define TICKLESS_TEST_NEAR_WAKE_MICROSECONDS                                                                           \
    (TICKLESS_TEST_NEAR_CLOSES_MICROSECONDS - KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS *                               \
                                                  KERNEL_TICKLESS_PROBE_GROWTH * KERNEL_TICKLESS_PROBE_GROWTH *        \
                                                  KERNEL_TICKLESS_PROBE_GROWTH / 2u)

/**
 * How many near windows a test arms at most: one more each time the one before was planned once its wake-up was due.
 */
#define TICKLESS_TEST_NEAR_ATTEMPTS 4u

/**
 * @struct TicklessTestUnlearned_t
 * @brief What serving windows taught, each at a depth made to have learned nothing.
 */
typedef struct {
    bool stopped;            /**< The tick stopped. */
    bool served;             /**< Every window was served. */
    uint32_t on_the_bound;   /**< Wake-ups planned on the bound meanwhile. */
    uint32_t on_a_published; /**< Wake-ups planned on a published lead meanwhile. */
    uint32_t sleeps;         /**< Times the processor slept meanwhile. */
    uint32_t samples;        /**< Wake-ups the depth learned by the last window's end. */
    uint32_t probes;         /**< Windows before which the depth was probed. */
    uint64_t returned;       /**< Microseconds the sleeps on the plan returned. */
    uint64_t slept;          /**< Microseconds counted with the tick stopped meanwhile. */
    uint64_t served_after;   /**< Cycles from the last window's arming to its service, once it is served. */
} TicklessTestUnlearned_t;

/**
 * @brief Serves one window of the test's duty, from @p opens to @p closes microseconds after the arming, at a depth
 *        that learned nothing, and adds to @p seen what it taught.
 * @param duty   The test's duty.
 * @param opens  When the window opens.
 * @param closes When it closes.
 * @param seen   What the windows taught so far.
 */
static void tickless_test_serve_round(uint32_t duty, uint32_t opens, uint32_t closes, TicklessTestUnlearned_t *seen)
{
    const uint32_t bound_before = kernel_tickless_wakes_planned_on_the_bound();
    const uint32_t published_before = kernel_tickless_wakes_planned_on_a_published_lead();
    const uint32_t sleeps_before = kernel_processor_sleep_count();
    const uint64_t probe_before = kernel_tickless_probe_cycles();
    const uint64_t slept_before = kernel_tickless_slept_microseconds();
    const uint32_t runs = tickless_test_runs;
    const uint64_t armed = asmutils_read_timestamp_counter();

    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, armed + kernel_tickless_cycles_from_microseconds(opens),
                                   armed + kernel_tickless_cycles_from_microseconds(closes));
    for (uint32_t sleeps = 0u; tickless_test_runs == runs && sleeps < TICKLESS_TEST_SLEEPS; ++sleeps)
        seen->returned += kernel_tickless_sleep_on(&tickless_test_plan);
    seen->served = seen->served && tickless_test_runs != runs;
    if (tickless_test_runs != runs)
        seen->served_after = tickless_test_instant - armed;
    seen->samples = kernel_tickless_wake_samples();
    seen->on_the_bound += kernel_tickless_wakes_planned_on_the_bound() - bound_before;
    seen->on_a_published += kernel_tickless_wakes_planned_on_a_published_lead() - published_before;
    seen->sleeps += kernel_processor_sleep_count() - sleeps_before;
    seen->probes += kernel_tickless_probe_cycles() != probe_before ? 1u : 0u;
    seen->slept += kernel_tickless_slept_microseconds() - slept_before;
}

/**
 * @brief Stops the tick and serves @p rounds windows from @p opens to @p closes microseconds after their arming, the
 *        depth sleeps enter made to have learned nothing before each, then gives the depth back its estimate and
 *        starts the tick again.
 * @param opens  When each window opens.
 * @param closes When it closes.
 * @param rounds How many windows.
 * @return What serving them taught.
 */
static TicklessTestUnlearned_t tickless_test_serve_at_an_unlearned_depth(uint32_t opens, uint32_t closes,
                                                                         uint32_t rounds)
{
    TicklessTestUnlearned_t seen = {.served = true};
    const uint32_t duty = tickless_test_begin();

    seen.stopped = kernel_tickless_enable(true, &tickless_test_plan);
    if (!seen.stopped)
        return seen;

    const uint32_t depth = kernel_processor_sleep_depth();
    KernelWakeLatency_t held = {0};
    kernel_tickless_debug_exchange_depth_estimate(depth, &held);
    for (uint32_t round = 0u; round < rounds; ++round)
    {
        KernelWakeLatency_t forgotten = {0};
        if (round > 0u)
            kernel_tickless_debug_exchange_depth_estimate(depth, &forgotten);
        tickless_test_serve_round(duty, opens, closes, &seen);
    }
    kernel_tickless_debug_exchange_depth_estimate(depth, &held);
    kernel_tickless_disable();
    return seen;
}

/**
 * @struct TicklessTestNear_t
 * @brief The near window served at a depth that learned nothing, and the ones armed before it whose plan came once
 *        their wake-up was due.
 */
typedef struct {
    TicklessTestUnlearned_t seen; /**< What the last window taught. */
    uint32_t late_plans;          /**< Windows planned once their wake-up was due, each followed by another. */
    uint32_t latest_late_plan;    /**< Microseconds from the arming of the last of those to its service. */
} TicklessTestNear_t;

/**
 * @brief Whether the window @p seen tells of was served without sleeping because it was planned once its wake-up was
 *        due: the time from its arming to its service is then its plan's, and only a slow emulator takes that long.
 */
static bool tickless_test_planned_once_due(const TicklessTestUnlearned_t *seen)
{
    return seen->stopped && seen->served && seen->sleeps == 0u &&
           seen->served_after >= kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_NEAR_WAKE_MICROSECONDS);
}

/**
 * @brief Serves the near window at a depth that learned nothing, and arms another while the one before was planned
 *        once its wake-up was due, TICKLESS_TEST_NEAR_ATTEMPTS windows at most.
 * @return The last window, and how many before it were planned too late.
 */
static TicklessTestNear_t tickless_test_serve_the_near_window(void)
{
    TicklessTestNear_t near = {0};

    for (uint32_t attempt = 0u; attempt < TICKLESS_TEST_NEAR_ATTEMPTS; ++attempt)
    {
        near.seen = tickless_test_serve_at_an_unlearned_depth(TICKLESS_TEST_NEAR_OPENS_MICROSECONDS,
                                                              TICKLESS_TEST_NEAR_CLOSES_MICROSECONDS, 1u);
        if (!tickless_test_planned_once_due(&near.seen))
            break;
        ++near.late_plans;
        near.latest_late_plan = kernel_tickless_microseconds_from_cycles(near.seen.served_after);
    }
    return near;
}

/**
 * @brief A window whose wake-up, a bound before its latest, comes before a probe would end is served at a depth that
 *        learned nothing without a probe, its wake-up planned on the bound, the shallowest depth having no shallower
 *        lead, and the depth learns from that sleep; what the sleeps return is the time counted slept.
 *
 * @details The window closes 3000 us after its arming: its wake-up comes 2488 us after it, short of the 3408 us a
 *          probe may take. The first plan of a boot runs the cold path for the first time: under KVM it is made 1 us
 *          after the arming, under TCG 0.4 to 1.3 ms after it, and 18 ms once on a host loaded three times over, while
 *          the next plans take less than 30 us (#874). A window planned once its wake-up was due is served without
 *          sleeping, as it should be: it is counted, and another is armed. One served without sleeping before its
 *          wake-up was due is never armed again, and fails.
 */
KERNEL_TEST(a_near_window_at_a_depth_that_learned_nothing_is_planned_on_the_bound_without_a_probe)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const TicklessTestNear_t near = tickless_test_serve_the_near_window();
    const TicklessTestUnlearned_t *const seen = &near.seen;

    kernel_test_check(test, near.late_plans < TICKLESS_TEST_NEAR_ATTEMPTS,
                      "one of the windows is planned before its wake-up is due");
    kernel_test_check(test, seen->stopped && seen->served, "the tick stops and the window is served");
    kernel_test_check(test, seen->probes == 0u, "no probe, which would end after the wake-up");
    kernel_test_check(test, seen->sleeps >= 1u, "the window is slept on, not found due once its wake-up is planned");
    kernel_test_check(test, seen->on_the_bound >= 1u && seen->on_a_published == 0u,
                      "the wake-up is planned on the bound, never on a published latency alone");
    kernel_test_check(test, seen->samples >= 1u || seen->on_the_bound > 1u,
                      "the depth learns from its sleep, or plans the next on the bound again");
    kernel_test_check(test, seen->returned == seen->slept, "what the sleeps return is the time counted slept");
    kernel_test_measure(test, "near_wakes_on_bound", seen->on_the_bound);
    kernel_test_measure(test, "near_samples", seen->samples);
    kernel_test_measure(test, "near_sleeps", seen->sleeps);
    kernel_test_measure(test, "near_slept_us", (uint32_t) seen->slept);
    kernel_test_measure(test, "near_late_plans", near.late_plans);
    if (near.late_plans != 0u)
        kernel_test_measure(test, "near_late_plan_us", near.latest_late_plan);
}

/**
 * @brief A window whose wake-up comes after a probe would end is served at a depth that learned nothing once the depth
 *        is probed, its wake-up then planned on the lead the probe taught, and a depth made to learn nothing again in
 *        the same session is probed again; what the sleeps return, the probes included, is the time counted slept.
 */
KERNEL_TEST(a_far_window_at_a_depth_that_learned_nothing_is_planned_after_probing_it)
{
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const TicklessTestUnlearned_t seen = tickless_test_serve_at_an_unlearned_depth(
        TICKLESS_TEST_FAR_OPENS_MICROSECONDS, TICKLESS_TEST_FAR_CLOSES_MICROSECONDS, 2u);

    kernel_test_check(test, seen.stopped && seen.served, "the tick stops and both windows are served");
    kernel_test_check(test, seen.probes == 2u, "the depth is probed before each, forgotten in between");
    kernel_test_check(test, seen.samples >= 2u && seen.on_the_bound == 0u && seen.on_a_published == 0u,
                      "so each wake-up is planned on the lead the probe taught, and the sleep teaches it more");
    kernel_test_check(test, seen.returned == seen.slept,
                      "what the sleeps return, the probes included, is the time counted slept");
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
        tickless_test_serve_at_an_unlearned_depth(0u, TICKLESS_TEST_DUE_CLOSES_MICROSECONDS, 1u);

    kernel_test_check(test, seen.stopped && seen.served, "the tick stops and the window is served");
    kernel_test_check(test, seen.sleeps == 0u && seen.probes == 0u && seen.samples == 0u,
                      "without sleeping, probing or learning");
    kernel_test_check(test, seen.on_the_bound == 1u, "its one wake-up planned on the bound is counted");
    kernel_test_measure(test, "due_wakes_on_bound", seen.on_the_bound);
}

/**
 * @brief The first stop of the tick probes each depth below the ceiling that is usable and that no wake-up
 *        taught, and no other: neither the ceiling, nor one past it, nor one unusable or already taught.
 */
KERNEL_TEST(the_first_stop_probes_each_usable_untaught_depth_below_the_ceiling)
{
    kernel_test_check(test, kernel_tickless_depths_to_probe_below(0x0Fu, 0x01u, 3u) == 0x06u,
                      "C1 taught, depths 1 and 2 usable below the ceiling 3: those two");
    kernel_test_check(test, kernel_tickless_depths_to_probe_below(0x0Bu, 0x01u, 3u) == 0x02u,
                      "depth 2 unusable: depth 1 alone");
    kernel_test_check(test, kernel_tickless_depths_to_probe_below(0x7Fu, 0x00u, 2u) == 0x03u,
                      "nothing taught under the ceiling 2: C1 and depth 1, neither the ceiling nor past it");
    kernel_test_check(test, kernel_tickless_depths_to_probe_below(0x7Fu, 0x0Fu, 4u) == 0u,
                      "every depth below the ceiling taught: none");
    kernel_test_check(test, kernel_tickless_depths_to_probe_below(0x7Fu, 0x00u, 0u) == 0u,
                      "a ceiling at C1: none below it");
}

/**
 * @brief A probe of a port depth keeps it only when its two longest sleeps stopped the processor, whatever the shorter
 *        ones did; asks one more sleep when one of the two did; refuses it when neither did and one returned at once;
 *        says nothing of it when neither told.
 */
KERNEL_TEST(a_probe_keeps_a_port_depth_only_when_its_two_longest_sleeps_stopped)
{
    static const struct {
        uint32_t stopped;
        uint32_t returned;
        KernelTicklessProbeVerdict_t verdict;
    } cases[] = {
        {0xCu, 0x0u, KERNEL_TICKLESS_PROBE_KEEPS       },
        {0xDu, 0x2u, KERNEL_TICKLESS_PROBE_KEEPS       },
        {0xFu, 0x0u, KERNEL_TICKLESS_PROBE_KEEPS       },
        {0x8u, 0x4u, KERNEL_TICKLESS_PROBE_CONFIRMS    },
        {0xBu, 0x4u, KERNEL_TICKLESS_PROBE_CONFIRMS    },
        {0x4u, 0x8u, KERNEL_TICKLESS_PROBE_CONFIRMS    },
        {0x8u, 0x0u, KERNEL_TICKLESS_PROBE_CONFIRMS    },
        {0x0u, 0xCu, KERNEL_TICKLESS_PROBE_REFUSES     },
        {0x3u, 0xCu, KERNEL_TICKLESS_PROBE_REFUSES     },
        {0x1u, 0x8u, KERNEL_TICKLESS_PROBE_REFUSES     },
        {0x0u, 0x0u, KERNEL_TICKLESS_PROBE_SAYS_NOTHING},
        {0x3u, 0x0u, KERNEL_TICKLESS_PROBE_SAYS_NOTHING},
    };
    uint32_t held[4] = {0u};
    uint32_t seen[4] = {0u};

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        ++seen[cases[index].verdict];
        held[cases[index].verdict] +=
            kernel_tickless_judge_a_probe(cases[index].stopped, cases[index].returned) == cases[index].verdict ? 1u :
                                                                                                                 0u;
    }
    kernel_test_check(test, held[KERNEL_TICKLESS_PROBE_KEEPS] == seen[KERNEL_TICKLESS_PROBE_KEEPS],
                      "the two longest that stopped keep, whatever the shorter did");
    kernel_test_check(test, held[KERNEL_TICKLESS_PROBE_CONFIRMS] == seen[KERNEL_TICKLESS_PROBE_CONFIRMS],
                      "one of the two longest that stopped, the longest or the other, asks one more");
    kernel_test_check(test, held[KERNEL_TICKLESS_PROBE_REFUSES] == seen[KERNEL_TICKLESS_PROBE_REFUSES],
                      "neither of them stopping, one returning at once, refuses, even when the shorter stopped");
    kernel_test_check(test, held[KERNEL_TICKLESS_PROBE_SAYS_NOTHING] == seen[KERNEL_TICKLESS_PROBE_SAYS_NOTHING],
                      "neither of them telling says nothing");
}

/**
 * @brief A probe settles on its verdict and on the sleep it asked for: a refusal refuses and a keep keeps; one more
 *        sleep that stopped keeps and one that returned at once refuses; when nothing told, a depth that awaited the
 *        probe to be validated is refused and one validated before stays.
 */
KERNEL_TEST(a_probe_settles_on_its_verdict_and_the_sleep_it_asked_for)
{
    const KernelTicklessProbeRead_t nothing = KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING;

    kernel_test_check(test,
                      kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_REFUSES, nothing, false) &&
                          !kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_KEEPS, nothing, true),
                      "a refusal refuses, and a keep keeps, validated before or not");
    kernel_test_check(
        test,
        !kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_CONFIRMS, KERNEL_TICKLESS_PROBE_READ_STOPPED, true) &&
            kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_CONFIRMS, KERNEL_TICKLESS_PROBE_READ_RETURNED, false),
        "one more sleep that stopped keeps the depth, one that returned at once refuses it");
    kernel_test_check(test,
                      kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_SAYS_NOTHING, nothing, true) &&
                          kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_CONFIRMS, nothing, true),
                      "when nothing told, a depth awaiting its validation is refused");
    kernel_test_check(test,
                      !kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_SAYS_NOTHING, nothing, false) &&
                          !kernel_tickless_settle_a_probe(KERNEL_TICKLESS_PROBE_CONFIRMS, nothing, false),
                      "and one validated before stays");
}

/** @name A probe sleep's deadline and reads around it, in made-up cycles of the timestamp counter
 *  @{ */
#define TICKLESS_TEST_READ_DEADLINE 100000u /**< The sleep's deadline. */
#define TICKLESS_TEST_READ_BEGAN    20000u  /**< When a read that began in time began. */
#define TICKLESS_TEST_READ_HELD     900000u /**< When a read the host held before it began. */
/** @} */

/**
 * @brief A probe read is judged by when it began and returned against its deadline: one that began and returned before
 *        it did not stop the processor; one that began before it and returned at or past it stopped it; one that
 *        began at or past it says nothing.
 */
KERNEL_TEST(a_probe_read_is_judged_by_when_it_began_and_returned)
{
    const uint64_t deadline = TICKLESS_TEST_READ_DEADLINE;
    const uint64_t began = TICKLESS_TEST_READ_BEGAN;
    const uint64_t held = TICKLESS_TEST_READ_HELD;

    kernel_test_check(
        test,
        kernel_tickless_judge_a_probe_read(began, began + 1u, deadline) == KERNEL_TICKLESS_PROBE_READ_RETURNED &&
            kernel_tickless_judge_a_probe_read(began, deadline - 1u, deadline) == KERNEL_TICKLESS_PROBE_READ_RETURNED,
        "a read that began and returned before its deadline did not stop, a cycle short of it included");
    kernel_test_check(
        test,
        kernel_tickless_judge_a_probe_read(began, deadline, deadline) == KERNEL_TICKLESS_PROBE_READ_STOPPED &&
            kernel_tickless_judge_a_probe_read(deadline - 1u, held, deadline) == KERNEL_TICKLESS_PROBE_READ_STOPPED,
        "one that began before its deadline and returned at or past it stopped");
    kernel_test_check(test,
                      kernel_tickless_judge_a_probe_read(deadline, deadline + 1u, deadline) ==
                              KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING &&
                          kernel_tickless_judge_a_probe_read(held, held + 1u, deadline) ==
                              KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING,
                      "one that began at or past its deadline says nothing, however fast it returned");
}

/** The port the made-up depth reads: nothing decodes it on QEMU's q35, whose ICH9 power management lies at 0x600. */
#define TICKLESS_TEST_UNDECODED_PORT 0x414u

/**
 * @brief Adopts the ladder in force with a made-up depth 1 that reads the port nothing decodes.
 * @param ladder Receives the ladder adopted; it must outlive the adoption.
 */
static void tickless_test_adopt_a_made_up_port_depth(ProcessorSleepLadder_t *ladder)
{
    *ladder = *kernel_processor_sleep_ladder();
    ladder->available |= 1u << 1u;
    ladder->depths[1] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_PORT,
                                                .port = TICKLESS_TEST_UNDECODED_PORT,
                                                .latency_published = true,
                                                .latency = 18u};
    kernel_processor_sleep_debug_adopt_ladder(ladder);
}

#define TICKLESS_TEST_RECONSIDER_MICROSECONDS 100000u /**< How long a parked processor may take to be reached. */

/**
 * @brief How many times the parks' reconsider reached the processor of @p slot so far.
 */
static uint32_t tickless_test_reconsiders_of(uint32_t slot)
{
    const uint8_t vector = kernel_processor_park_reconsider_vector();

    return vector == 0u ? 0u : device_interrupt_count(vector, slot);
}

/**
 * @brief Waits, boundedly, until every other online processor was reached by the parks' reconsider more often than
 *        @p before records.
 * @return How many were; @p out_others receives how many there are.
 */
static uint32_t tickless_test_reconsiders_since(const uint32_t *before, uint32_t *out_others)
{
    const uint32_t here = cpu_topology_get_slot_for_apic_id(apic_get_id());
    InterruptRequestDeadline_t deadline;
    uint32_t reached = 0u;

    *out_others = 0u;
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        if (slot == here || !cpu_topology_is_logical_slot_online(slot))
            continue;
        ++*out_others;
        interrupt_request_deadline_start(&deadline, TICKLESS_TEST_RECONSIDER_MICROSECONDS);
        while (tickless_test_reconsiders_of(slot) <= before[slot] && !interrupt_request_deadline_passed(&deadline))
            asmutils_pause();
        reached += tickless_test_reconsiders_of(slot) > before[slot] ? 1u : 0u;
    }
    return reached;
}

/**
 * @brief Under a hypervisor, a port depth that reads a port nothing decodes is refused at the first stop of the tick,
 *        before any session counts its sleeps: its probe's reads return at once with no interrupt; the tick stops at
 *        the depth below instead, the refusal is counted, and the depth is taught nothing. The refusal is published,
 *        and every other processor told to look at its target again. Skipped on a real processor, whose port may stop
 *        it.
 */
KERNEL_TEST(a_port_depth_whose_read_does_not_stop_is_refused_at_the_first_stop_of_the_tick)
{
    uint32_t reconsiders[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];
    uint32_t verdicts[PROCESSOR_SLEEP_PORT_VERDICTS];
    uint32_t others = 0u;
    static ProcessorSleepLadder_t ladder;
    CpuIdentity_t identity;
    KernelWakeLatency_t held = {0};

    cpu_identity_read(&identity);
    if (!identity.hypervisor)
    {
        kernel_test_skip(test, "a real processor: the made-up port may well stop it");
        return;
    }
    if (!tickless_test_can_stop_the_tick(test))
        return;

    tickless_test_adopt_a_made_up_port_depth(&ladder);
    const bool granted = kernel_processor_sleep_request_depth(1u);
    kernel_tickless_debug_exchange_depth_estimate(1u, &held);
    const uint32_t refused_before = kernel_processor_sleep_refused_depth_count();
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        reconsiders[slot] = tickless_test_reconsiders_of(slot);

    (void) tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t depth = kernel_processor_sleep_depth();
    const uint32_t available = kernel_processor_sleep_available_depths();
    const uint32_t refused = kernel_processor_sleep_refused_depth_count() - refused_before;
    const uint32_t unstopped = kernel_tickless_unstopped_probe_ranks_at(1u);
    const uint32_t stopped_ranks = kernel_tickless_stopped_probe_ranks_at(1u);
    const uint32_t taught = kernel_tickless_wake_samples_at(1u);
    kernel_tickless_disable();
    kernel_processor_sleep_port_verdicts(verdicts);
    const uint32_t reached = tickless_test_reconsiders_since(reconsiders, &others);
    kernel_tickless_debug_exchange_depth_estimate(1u, &held);
    kernel_processor_sleep_initialize();

    kernel_test_check(test, granted && stopped, "depth 1 is granted, and the tick stops");
    kernel_test_check(test, refused == 1u && ((available >> 1u) & 1u) == 0u, "the port depth is refused");
    kernel_test_check(test, depth == PROCESSOR_SLEEP_DEPTH_C1, "and the sleeps fall to C1");
    kernel_test_check(test, kernel_tickless_judge_a_probe(stopped_ranks, unstopped) != KERNEL_TICKLESS_PROBE_KEEPS,
                      "for reads that did not stop");
    kernel_test_check(test, taught == 0u,
                      "and the refused depth is taught nothing, so that its next adoption is probed again");
    kernel_test_check(test, verdicts[0] == (TICKLESS_TEST_UNDECODED_PORT | PROCESSOR_SLEEP_PORT_VERDICT_REFUSED),
                      "the refusal is published for the parked processors");
    kernel_test_check(test, reached == others, "and each other processor is told to look at its target again");
    kernel_test_measure(test, "unstopped_ranks", unstopped);
    kernel_test_measure(test, "stopped_ranks", stopped_ranks);
    kernel_test_measure(test, "others_reached", reached);
}

/** How long the port-depth tests hold each probe sleep: twice the longest, past every deadline of the probe's ranks. */
#define TICKLESS_TEST_PROBE_HOLD_MICROSECONDS                                                                          \
    (2u * KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS * KERNEL_TICKLESS_PROBE_GROWTH * KERNEL_TICKLESS_PROBE_GROWTH *     \
     KERNEL_TICKLESS_PROBE_GROWTH)

_Static_assert(KERNEL_TICKLESS_PROBES == 4u, "the probe hold doubles the fourth and longest probe sleep");

/** The ranks of a probe's two longest sleeps, the only ones its verdict reads. */
#define TICKLESS_TEST_LONGEST_RANKS ((1u << (KERNEL_TICKLESS_PROBES - 1u)) | (1u << (KERNEL_TICKLESS_PROBES - 2u)))

/**
 * @struct TicklessTestPortProbe_t
 * @brief What the first stop of the tick made of the made-up port depth.
 */
typedef struct {
    bool granted;      /**< Depth 1 was granted. */
    bool stopped;      /**< The tick stopped. */
    uint32_t refused;  /**< Depths refused meanwhile. */
    uint32_t returned; /**< The ranks whose reads returned before their deadline. */
    uint32_t halted;   /**< The ranks whose reads stopped the processor. */
    uint32_t taught;   /**< Wake-ups depth 1 learned. */
    uint32_t retakes;  /**< Probe sleeps taken again meanwhile. */
} TicklessTestPortProbe_t;

/**
 * @brief Adopts the made-up port depth, stops the tick with every probe sleep held @p before_the_read microseconds
 *        before its read and @p after_the_read once it returned, and gives back what the probe made of it, the ladder
 *        and depth 1's estimate as they were.
 * @param before_the_read How long each probe sleep is held before its read.
 * @param after_the_read  How long each is held once its read returned.
 * @return What the probe made of the depth.
 */
static TicklessTestPortProbe_t tickless_test_probe_the_made_up_port(uint32_t before_the_read, uint32_t after_the_read)
{
    static ProcessorSleepLadder_t ladder;
    KernelWakeLatency_t held = {0};
    TicklessTestPortProbe_t seen = {0};

    tickless_test_adopt_a_made_up_port_depth(&ladder);
    seen.granted = kernel_processor_sleep_request_depth(1u);
    kernel_tickless_debug_exchange_depth_estimate(1u, &held);
    const uint32_t refused_before = kernel_processor_sleep_refused_depth_count();
    const uint32_t retakes_before = kernel_tickless_probe_retakes();

    (void) tickless_test_begin();
    kernel_tickless_debug_hold_the_probe_sleeps(before_the_read, after_the_read);
    seen.stopped = kernel_tickless_enable(true, &tickless_test_plan);
    kernel_tickless_debug_hold_the_probe_sleeps(0u, 0u);
    seen.refused = kernel_processor_sleep_refused_depth_count() - refused_before;
    seen.returned = kernel_tickless_unstopped_probe_ranks_at(1u);
    seen.halted = kernel_tickless_stopped_probe_ranks_at(1u);
    seen.taught = kernel_tickless_wake_samples_at(1u);
    seen.retakes = kernel_tickless_probe_retakes() - retakes_before;
    kernel_tickless_disable();
    kernel_tickless_debug_exchange_depth_estimate(1u, &held);
    kernel_processor_sleep_initialize();
    return seen;
}

/**
 * @brief Under a hypervisor, the made-up port depth is judged by its reads whatever the host does around them: held
 *        once each read returned, past every deadline, its reads still returned at once and it is refused (#880);
 *        held before each read, past every deadline, its long reads say nothing, are each taken again
 *        KERNEL_TICKLESS_PROBE_RETAKES times, and it is refused, no long sleep having stopped. Skipped on a real
 *        processor, whose port may stop it.
 */
KERNEL_TEST(a_port_depth_is_judged_by_its_reads_whatever_the_host_does_around_them)
{
    CpuIdentity_t identity;

    cpu_identity_read(&identity);
    if (!identity.hypervisor)
    {
        kernel_test_skip(test, "a real processor: the made-up port may well stop it");
        return;
    }
    if (!tickless_test_can_stop_the_tick(test))
        return;

    const TicklessTestPortProbe_t after =
        tickless_test_probe_the_made_up_port(0u, TICKLESS_TEST_PROBE_HOLD_MICROSECONDS);
    const TicklessTestPortProbe_t before =
        tickless_test_probe_the_made_up_port(TICKLESS_TEST_PROBE_HOLD_MICROSECONDS, 0u);

    kernel_test_check(test, after.granted && after.stopped && before.granted && before.stopped,
                      "depth 1 is granted, and the tick stops, twice");
    kernel_test_check(test,
                      after.refused == 1u &&
                          (after.returned & TICKLESS_TEST_LONGEST_RANKS) == TICKLESS_TEST_LONGEST_RANKS &&
                          (after.halted & TICKLESS_TEST_LONGEST_RANKS) == 0u && after.taught == 0u,
                      "held once each read returned, its long reads returned at once, and it is refused");
    kernel_test_check(test,
                      before.refused == 1u && ((before.returned | before.halted) & TICKLESS_TEST_LONGEST_RANKS) == 0u &&
                          before.taught == 0u,
                      "held before each read, its long reads say nothing, and it is refused");
    kernel_test_check(test, before.retakes == 2u * KERNEL_TICKLESS_PROBE_RETAKES && after.retakes == 0u,
                      "and each of the two long sleeps was taken again, the most a probe takes it");
    kernel_test_measure(test, "held_after_returned_ranks", after.returned);
    kernel_test_measure(test, "held_after_stopped_ranks", after.halted);
    kernel_test_measure(test, "held_before_returned_ranks", before.returned);
    kernel_test_measure(test, "held_before_stopped_ranks", before.halted);
    kernel_test_measure(test, "held_before_retakes", before.retakes);
}

/** The latency the made-up deeper depth publishes, in microseconds. */
#define TICKLESS_TEST_PUBLISHED_LATENCY_MICROSECONDS 100u

/**
 * @brief A near window at a deeper depth that learned nothing is planned on the lead of the depth below it plus the
 *        latency the ladder publishes for it, as `_CST` gives a port state its own. The deeper depth is a made-up
 *        halt, since no QEMU model has a deeper depth that stops; skipped where C1 learned no lead, which the
 *        published one adds to.
 */
KERNEL_TEST(a_near_window_at_a_deeper_unlearned_depth_is_planned_on_its_published_latency)
{
    static ProcessorSleepLadder_t ladder;
    KernelWakeLatency_t before = {0};

    if (!tickless_test_can_stop_the_tick(test))
        return;
    if (kernel_tickless_wake_samples_at(PROCESSOR_SLEEP_DEPTH_C1) == 0u)
    {
        kernel_test_skip(test, "C1 learned no lead, which the published latency adds to");
        return;
    }

    ladder = *kernel_processor_sleep_ladder();
    ladder.available |= 1u << 1u;
    ladder.depths[1] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_HALT,
                                               .latency_published = true,
                                               .latency = TICKLESS_TEST_PUBLISHED_LATENCY_MICROSECONDS};
    kernel_processor_sleep_debug_adopt_ladder(&ladder);
    const bool granted = kernel_processor_sleep_request_depth(1u);
    kernel_tickless_debug_exchange_depth_estimate(1u, &before);
    const TicklessTestUnlearned_t seen = tickless_test_serve_at_an_unlearned_depth(
        TICKLESS_TEST_NEAR_OPENS_MICROSECONDS, TICKLESS_TEST_NEAR_CLOSES_MICROSECONDS, 1u);
    kernel_tickless_debug_exchange_depth_estimate(1u, &before);
    kernel_processor_sleep_initialize();

    kernel_test_check(test, granted && seen.stopped && seen.served, "depth 1 is granted, and the window is served");
    kernel_test_check(test, seen.probes == 0u, "no probe, which would end after the wake-up");
    kernel_test_check(test, seen.on_a_published >= 1u && seen.on_the_bound == 0u,
                      "the wake-up is planned on the published latency, never on the bound");
    kernel_test_measure(test, "near_wakes_on_published", seen.on_a_published);
}

/**
 * @brief Under a hypervisor, a sleep the bus-master check demoted teaches the depth it entered, never the one asked:
 *        with a validated C3 whose status register reads BM_STS set, a port nothing decodes, and HLT below it, every
 *        sleep of a session enters HLT, which the timer ends, and only its estimate learns; the C3's probe, every one
 *        of its sleeps demoted, teaches nothing and refuses nothing. Skipped on a real processor, whose ports may stop
 *        it.
 */
KERNEL_TEST(a_sleep_the_bus_master_check_demoted_is_learned_at_the_depth_it_entered)
{
    static ProcessorSleepLadder_t ladder;
    CpuIdentity_t identity;
    KernelWakeLatency_t held_halt = {0};
    KernelWakeLatency_t held_c3 = {0};

    cpu_identity_read(&identity);
    if (!identity.hypervisor)
    {
        kernel_test_skip(test, "a real processor: the made-up ports may well stop it");
        return;
    }
    if (!tickless_test_can_stop_the_tick(test))
        return;

    ladder = *kernel_processor_sleep_ladder();
    ladder.available |= 0x6u;
    ladder.depths[1] =
        (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_HALT, .latency_published = true, .latency = 18u};
    ladder.depths[2] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_PORT,
                                               .port = TICKLESS_TEST_UNDECODED_PORT,
                                               .latency_published = true,
                                               .latency = 350u,
                                               .bus_master_check = true};
    ladder.bus_master_status_ports[0] = TICKLESS_TEST_UNDECODED_PORT;
    kernel_processor_sleep_debug_adopt_ladder(&ladder);
    (void) kernel_processor_sleep_request_depth(2u);
    kernel_processor_sleep_begin_probe();
    kernel_processor_sleep_end_probe(true);
    kernel_tickless_debug_exchange_depth_estimate(1u, &held_halt);
    kernel_tickless_debug_exchange_depth_estimate(2u, &held_c3);
    const uint32_t refused_before = kernel_processor_sleep_refused_depth_count();
    const uint32_t demotions_before = kernel_processor_sleep_bus_master_demotion_count();

    const uint32_t duty = tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t probe_taught = kernel_tickless_wake_samples_at(2u);
    const uint64_t earliest = asmutils_read_timestamp_counter() +
                              kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS);
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, earliest,
                                   earliest +
                                       kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_MICROSECONDS));
    for (uint32_t sleeps = 0u; tickless_test_runs == 0u && sleeps < TICKLESS_TEST_SLEEPS; ++sleeps)
        (void) kernel_tickless_sleep_on(&tickless_test_plan);
    const uint32_t halt_taught = kernel_tickless_wake_samples_at(1u);
    const uint32_t c3_taught = kernel_tickless_wake_samples_at(2u);
    const uint32_t demotions = kernel_processor_sleep_bus_master_demotion_count() - demotions_before;
    const uint32_t refused = kernel_processor_sleep_refused_depth_count() - refused_before;
    kernel_tickless_disable();
    kernel_tickless_debug_exchange_depth_estimate(1u, &held_halt);
    kernel_tickless_debug_exchange_depth_estimate(2u, &held_c3);
    kernel_processor_sleep_initialize();

    kernel_test_check(test, stopped && tickless_test_runs == 1u, "the tick stops, and the duty is served");
    kernel_test_check(test, probe_taught == 0u && refused == 0u, "the C3's probe teaches nothing, and refuses nothing");
    kernel_test_check(test, demotions > 0u, "the check demotes the sleeps");
    kernel_test_check(test, halt_taught > 0u, "HLT, the depth they entered, learns");
    kernel_test_check(test, c3_taught == 0u, "the C3, never entered, learns nothing");
    kernel_test_measure(test, "demotions", demotions);
    kernel_test_measure(test, "halt_taught", halt_taught);
}

/**
 * @brief Under a hypervisor, the first stop of the tick probes the depth the bus-master check demotes a validated
 *        depth to, so that a demotion lands on C2 rather than C1 once C2 stops the processor, as ACPI demotes C3
 *        (ACPI 6.6 §8.1.4): with HLT behind the check at depth 2 and a port nothing decodes at depth 1, depth 1 is
 *        probed, and refused, while sleeps stay at depth 2. Skipped on a real processor, whose port may stop it.
 */
KERNEL_TEST(the_first_stop_of_the_tick_probes_where_the_check_demotes)
{
    static ProcessorSleepLadder_t ladder;
    CpuIdentity_t identity;

    cpu_identity_read(&identity);
    if (!identity.hypervisor)
    {
        kernel_test_skip(test, "a real processor: the made-up port may well stop it");
        return;
    }
    if (!tickless_test_can_stop_the_tick(test))
        return;

    ladder = *kernel_processor_sleep_ladder();
    ladder.available |= 0x6u;
    ladder.depths[1] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_PORT,
                                               .port = TICKLESS_TEST_UNDECODED_PORT,
                                               .latency_published = true,
                                               .latency = 18u};
    ladder.depths[2] = (ProcessorSleepDepth_t){.entry = PROCESSOR_SLEEP_ENTRY_HALT,
                                               .latency_published = true,
                                               .latency = 350u,
                                               .bus_master_check = true};
    kernel_processor_sleep_debug_adopt_ladder(&ladder);
    (void) kernel_processor_sleep_request_depth(2u);
    const uint32_t refused_before = kernel_processor_sleep_refused_depth_count();

    (void) tickless_test_begin();
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint32_t depth = kernel_processor_sleep_depth();
    const uint32_t unstopped = kernel_tickless_unstopped_probe_ranks_at(1u);
    const uint32_t refused = kernel_processor_sleep_refused_depth_count() - refused_before;
    kernel_tickless_disable();
    kernel_processor_sleep_initialize();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, refused == 1u && unstopped != 0u, "depth 1, where the check would demote, is probed");
    kernel_test_check(test, depth == 2u, "and the sleeps stay at depth 2");
    kernel_test_measure(test, "unstopped_ranks", unstopped);
}
