#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/tickless.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(tickless, KERNEL_TEST_STAGE_BOOTED);

/** Where the window a sleep must land in opens, and how long it lasts, in microseconds. */
#define TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS 2000u
#define TICKLESS_TEST_WINDOW_MICROSECONDS       2000u

/** Sleeps a window may take: interrupts other than its deadline may end the first ones. */
#define TICKLESS_TEST_SLEEPS 64u

/** Cycles a wait for the tick's return spins before it gives up, rather than hang the boot. */
#define TICKLESS_TEST_WAIT_CYCLE_BUDGET 8000000000ull

static KernelWakePlan_t tickless_test_plan;
static uint32_t tickless_test_runs;
static uint64_t tickless_test_instant;

/**
 * @brief Records that the test's duty ran, and at which wake-up.
 * @param context Unused.
 * @param now     The wake-up's cycle.
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
 * @brief The tick stays on when no plan is given to carry its duty.
 */
KERNEL_TEST(stopping_the_tick_needs_a_plan_to_carry_its_duty)
{
    kernel_test_check(test, !kernel_tickless_enable(true, NULL) && !kernel_tickless_enabled(),
                      "without a plan the tick is not stopped");
}

/**
 * @brief With the tick stopped, the reconciler's pass is a duty of the session's plan that never wakes
 *        the core and rides the first wake-up a tick period after the last pass; the tick takes it back
 *        when it starts again.
 */
KERNEL_TEST(the_reconciler_pass_goes_on_with_the_tick_stopped)
{
    if (advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz() == 0u)
    {
        kernel_test_skip(test, "the local timer was not calibrated, so the tick cannot stop");
        return;
    }

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
 * @brief A sleep on a plan, with the tick stopped, serves its duty inside its window, on time.
 */
KERNEL_TEST(a_sleep_on_the_plan_serves_its_duty_inside_its_window)
{
    if (advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz() == 0u)
    {
        kernel_test_skip(test, "the local timer was not calibrated, so the tick cannot stop");
        return;
    }

    kernel_wake_planner_initialize(&tickless_test_plan);
    tickless_test_runs = 0u;
    const uint32_t duty = kernel_wake_planner_declare(&tickless_test_plan, tickless_test_record, NULL);
    const bool stopped = kernel_tickless_enable(true, &tickless_test_plan);
    const uint64_t earliest = asmutils_read_timestamp_counter() +
                              kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_OPENS_MICROSECONDS);
    const uint64_t latest = earliest + kernel_tickless_cycles_from_microseconds(TICKLESS_TEST_WINDOW_MICROSECONDS);
    (void) kernel_wake_planner_arm(&tickless_test_plan, duty, earliest, latest);

    uint32_t sleeps = 0u;
    while (tickless_test_runs == 0u && sleeps < TICKLESS_TEST_SLEEPS)
    {
        (void) kernel_tickless_sleep_on(&tickless_test_plan);
        ++sleeps;
    }
    kernel_tickless_disable();

    kernel_test_check(test, stopped, "the tick stops");
    kernel_test_check(test, tickless_test_runs == 1u, "the duty is served once");
    kernel_test_check(test, tickless_test_instant >= earliest && tickless_test_instant <= latest, "inside its window");
    kernel_test_check(test, tickless_test_plan.late_services == 0u, "on time");
    kernel_test_check(test, tickless_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "sleeps", sleeps);
    kernel_test_measure(test, "served_after_opening_us",
                        tickless_test_instant >= earliest ?
                            (uint32_t) (((tickless_test_instant - earliest) * 1000000u) /
                                        advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz()) :
                            0u);
}
