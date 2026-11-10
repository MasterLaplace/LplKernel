#include <kernel/power/wake_planner.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(wake_planner, KERNEL_TEST_STAGE_BOOTED);

/** Runs a check records, at most, in one test. */
#define WAKE_PLANNER_TEST_LOG 16u

/** Window sets the comparison with every other schedule draws. */
#define WAKE_PLANNER_TEST_DRAWS 96u

#define WAKE_PLANNER_TEST_SPAN   1000u /**< Cycles over which a drawn window opens. */
#define WAKE_PLANNER_TEST_LENGTH 300u  /**< The longest window drawn, in cycles. */
#define WAKE_PLANNER_TEST_LEAD   100u  /**< The lead the drawn windows are slept on with, a third of the longest. */

/** What a test duty leaves behind each time it runs. */
typedef struct {
    uint32_t count;                           /**< Runs recorded. */
    uint32_t duties[WAKE_PLANNER_TEST_LOG];   /**< Which duty ran, in order. */
    uint64_t instants[WAKE_PLANNER_TEST_LOG]; /**< At which wake-up. */
} WakePlannerTestLog_t;

/** A test duty: its number in the plan, and the log it writes to. */
typedef struct {
    uint32_t number;
    WakePlannerTestLog_t *log;
} WakePlannerTestDuty_t;

static KernelWakePlan_t wake_planner_test_plan;
static WakePlannerTestLog_t wake_planner_test_log;
static WakePlannerTestDuty_t wake_planner_test_duties[KERNEL_WAKE_PLANNER_DUTIES];
static uint32_t wake_planner_test_random_state;

/**
 * @brief Records that a test duty ran, and at which wake-up.
 * @param context The duty's WakePlannerTestDuty_t.
 * @param now     The wake-up's cycle.
 */
static void wake_planner_test_record(void *context, uint64_t now)
{
    WakePlannerTestDuty_t *const duty = (WakePlannerTestDuty_t *) context;

    if (duty->log->count < WAKE_PLANNER_TEST_LOG)
    {
        duty->log->duties[duty->log->count] = duty->number;
        duty->log->instants[duty->log->count] = now;
    }
    ++duty->log->count;
}

/**
 * @brief Records the run, then arms the duty again for a window that opens and closes at this wake-up.
 * @param context The duty's WakePlannerTestDuty_t.
 * @param now     The wake-up's cycle.
 */
static void wake_planner_test_record_and_rearm(void *context, uint64_t now)
{
    WakePlannerTestDuty_t *const duty = (WakePlannerTestDuty_t *) context;

    wake_planner_test_record(context, now);
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, duty->number, now, now);
}

/**
 * @brief Records the run, then withdraws the second duty of the plan.
 * @param context The duty's WakePlannerTestDuty_t.
 * @param now     The wake-up's cycle.
 */
static void wake_planner_test_record_and_withdraw_the_second(void *context, uint64_t now)
{
    wake_planner_test_record(context, now);
    kernel_wake_planner_withdraw(&wake_planner_test_plan, wake_planner_test_duties[1].number);
}

/**
 * @brief Empties the plan and the log, then declares @p count duties that record their runs.
 * @param count Duties to declare, at most KERNEL_WAKE_PLANNER_DUTIES.
 * @return true when every declaration was taken.
 */
static bool wake_planner_test_begin(uint32_t count)
{
    bool declared = true;

    kernel_wake_planner_initialize(&wake_planner_test_plan);
    wake_planner_test_log.count = 0u;
    for (uint32_t index = 0u; index < count; ++index)
    {
        WakePlannerTestDuty_t *const duty = &wake_planner_test_duties[index];

        duty->log = &wake_planner_test_log;
        duty->number = kernel_wake_planner_declare(&wake_planner_test_plan, wake_planner_test_record, duty);
        declared = declared && duty->number != KERNEL_WAKE_PLANNER_NO_DUTY;
    }
    return declared;
}

/**
 * @brief The next draw of a xorshift generator, so that every boot draws the same windows.
 * @return 32 bits.
 */
static uint32_t wake_planner_test_draw(void)
{
    uint32_t state = wake_planner_test_random_state;

    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    wake_planner_test_random_state = state;
    return state;
}

/**
 * @brief The fewest wake-ups any schedule needs to serve every window inside it, by trying every subset of
 *        the windows' latest cycles: a schedule can always move a wake-up later to the nearest latest it
 *        still serves, so those cycles are enough.
 * @param earliest Each window's earliest cycle.
 * @param latest   Each window's latest cycle.
 * @param count    Windows, at most KERNEL_WAKE_PLANNER_DUTIES.
 * @return The size of the smallest subset that serves them all.
 */
static uint32_t wake_planner_test_fewest_wakes(const uint64_t *earliest, const uint64_t *latest, uint32_t count)
{
    uint32_t fewest = count;

    for (uint32_t subset = 1u; subset < (1u << count); ++subset)
    {
        uint32_t covered = 0u;
        uint32_t size = 0u;

        for (uint32_t point = 0u; point < count; ++point)
        {
            if ((subset & (1u << point)) == 0u)
                continue;
            ++size;
            for (uint32_t window = 0u; window < count; ++window)
                if (earliest[window] <= latest[point] && latest[point] <= latest[window])
                    covered |= 1u << window;
        }
        if (covered == (1u << count) - 1u && size < fewest)
            fewest = size;
    }
    return fewest;
}

/**
 * @brief Runs a plan the way a core sleeps on it: arms its next wake-up for @p lead, wakes that lead after the
 *        arming, until none is needed, or until it has woken once more than it has duties, which no plan of drawn
 *        windows needs.
 * @param lead Cycles each wake-up comes after its arming, the lead the plan arms for.
 * @return The wake-ups taken; more than KERNEL_WAKE_PLANNER_DUTIES when the plan never stopped asking.
 */
static uint32_t wake_planner_test_sleep_through(uint64_t lead)
{
    uint32_t wakes = 0u;

    for (uint64_t next = kernel_wake_planner_next_wake(&wake_planner_test_plan, lead);
         next != KERNEL_WAKE_PLANNER_NEVER && wakes <= KERNEL_WAKE_PLANNER_DUTIES;
         next = kernel_wake_planner_next_wake(&wake_planner_test_plan, lead))
    {
        (void) kernel_wake_planner_serve(&wake_planner_test_plan, next + lead);
        ++wakes;
    }
    return wakes;
}

/**
 * @brief Draws a set of windows and arms them, each in the duty of its index.
 * @param earliest Receives each window's earliest cycle.
 * @param latest   Receives each window's latest cycle.
 * @return The windows drawn, from 1 to KERNEL_WAKE_PLANNER_DUTIES.
 */
static uint32_t wake_planner_test_draw_windows(uint64_t *earliest, uint64_t *latest)
{
    const uint32_t count = 1u + wake_planner_test_draw() % KERNEL_WAKE_PLANNER_DUTIES;

    (void) wake_planner_test_begin(count);
    for (uint32_t window = 0u; window < count; ++window)
    {
        earliest[window] = wake_planner_test_draw() % WAKE_PLANNER_TEST_SPAN;
        latest[window] = earliest[window] + wake_planner_test_draw() % (WAKE_PLANNER_TEST_LENGTH + 1u);
        (void) kernel_wake_planner_arm(&wake_planner_test_plan, wake_planner_test_duties[window].number,
                                       earliest[window], latest[window]);
    }
    return count;
}

/**
 * @brief An empty plan asks for no wake-up, and a wake-up it is given finds nothing to serve.
 */
KERNEL_TEST(a_plan_with_no_window_never_wakes_the_core)
{
    (void) wake_planner_test_begin(0u);

    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == KERNEL_WAKE_PLANNER_NEVER,
                      "an empty plan never wakes the core");
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 0u) == 0u &&
                          wake_planner_test_plan.wakes == 1u && wake_planner_test_plan.wasted_wakes == 1u,
                      "a wake-up with no window open serves nothing and is counted wasted");
}

/**
 * @brief The core wakes at the earliest latest, and that wake-up serves every window open there, the most
 *        urgent first, while a window that has not opened waits for its own.
 */
KERNEL_TEST(the_core_wakes_at_the_earliest_latest_and_serves_every_open_window_there)
{
    kernel_test_check(test, wake_planner_test_begin(4u), "four duties are declared");
    const uint32_t wide = wake_planner_test_duties[0].number;
    const uint32_t narrow = wake_planner_test_duties[1].number;
    const uint32_t later = wake_planner_test_duties[2].number;
    const uint32_t rider = wake_planner_test_duties[3].number;

    kernel_test_check(test,
                      kernel_wake_planner_arm(&wake_planner_test_plan, wide, 10u, 100u) &&
                          kernel_wake_planner_arm(&wake_planner_test_plan, narrow, 50u, 60u) &&
                          kernel_wake_planner_arm(&wake_planner_test_plan, later, 200u, 300u) &&
                          kernel_wake_planner_arm(&wake_planner_test_plan, rider, 0u, KERNEL_WAKE_PLANNER_NEVER),
                      "the four windows are armed");
    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == 60u,
                      "the core wakes at the earliest latest");

    const uint32_t served = kernel_wake_planner_serve(&wake_planner_test_plan, 60u);
    kernel_test_check(test,
                      served == 3u && wake_planner_test_log.count == 3u && wake_planner_test_log.duties[0] == narrow &&
                          wake_planner_test_log.duties[1] == wide && wake_planner_test_log.duties[2] == rider,
                      "that wake-up serves every open window, the earliest latest first");
    kernel_test_check(test,
                      wake_planner_test_plan.batched_wakes == 1u && wake_planner_test_plan.services == 3u &&
                          wake_planner_test_plan.late_services == 0u && wake_planner_test_plan.wasted_wakes == 0u,
                      "one batched wake-up, three services, none late");
    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == 300u,
                      "the window that had not opened keeps its own wake-up");
    kernel_test_measure(test, "served", served);
}

/**
 * @brief A duty that arms its next window while it runs is not run again at the same wake-up, even when
 *        that window is already open.
 */
KERNEL_TEST(a_window_armed_while_serving_opens_at_the_next_wake)
{
    kernel_wake_planner_initialize(&wake_planner_test_plan);
    wake_planner_test_log.count = 0u;
    WakePlannerTestDuty_t *const duty = &wake_planner_test_duties[0];
    duty->log = &wake_planner_test_log;
    duty->number = kernel_wake_planner_declare(&wake_planner_test_plan, wake_planner_test_record_and_rearm, duty);

    kernel_test_check(test, kernel_wake_planner_arm(&wake_planner_test_plan, duty->number, 5u, 5u),
                      "a window of one cycle is armed");
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 5u) == 1u && wake_planner_test_log.count == 1u,
                      "the duty runs once at the wake-up although it rearmed an open window");
    kernel_test_check(test,
                      kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == 5u &&
                          kernel_wake_planner_serve(&wake_planner_test_plan, 5u) == 1u &&
                          wake_planner_test_log.count == 2u,
                      "the rearmed window is served at the next wake-up");
}

/**
 * @brief A duty withdrawn by one that runs before it at the same wake-up is not run, though its window was
 *        open when the wake-up began.
 */
KERNEL_TEST(a_duty_withdrawn_during_a_wake_is_not_run)
{
    kernel_wake_planner_initialize(&wake_planner_test_plan);
    wake_planner_test_log.count = 0u;
    for (uint32_t index = 0u; index < 2u; ++index)
        wake_planner_test_duties[index].log = &wake_planner_test_log;
    wake_planner_test_duties[0].number = kernel_wake_planner_declare(
        &wake_planner_test_plan, wake_planner_test_record_and_withdraw_the_second, &wake_planner_test_duties[0]);
    wake_planner_test_duties[1].number =
        kernel_wake_planner_declare(&wake_planner_test_plan, wake_planner_test_record, &wake_planner_test_duties[1]);

    (void) kernel_wake_planner_arm(&wake_planner_test_plan, wake_planner_test_duties[0].number, 0u, 10u);
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, wake_planner_test_duties[1].number, 0u, 20u);
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 10u) == 1u &&
                          wake_planner_test_log.count == 1u &&
                          wake_planner_test_log.duties[0] == wake_planner_test_duties[0].number,
                      "only the duty that withdrew the other ran");
}

/**
 * @brief A wake-up before a window opens does not run its duty, one at its earliest does, and one past its
 *        latest runs it late.
 */
KERNEL_TEST(a_duty_runs_only_inside_its_window_and_is_counted_late_after_it)
{
    kernel_test_check(test, wake_planner_test_begin(2u), "two duties are declared");
    const uint32_t first = wake_planner_test_duties[0].number;
    const uint32_t second = wake_planner_test_duties[1].number;

    (void) kernel_wake_planner_arm(&wake_planner_test_plan, first, 100u, 200u);
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, second, 100u, 150u);
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 99u) == 0u &&
                          wake_planner_test_plan.wasted_wakes == 1u,
                      "a wake-up before the windows open serves nothing");
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 150u) == 2u &&
                          wake_planner_test_plan.late_services == 0u,
                      "a wake-up at a latest serves its duty on time");

    (void) kernel_wake_planner_arm(&wake_planner_test_plan, first, 300u, 400u);
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 401u) == 1u &&
                          wake_planner_test_plan.late_services == 1u,
                      "a wake-up past a latest serves its duty late");
}

/**
 * @brief A duty whose latest is NEVER asks for no wake-up, and runs at the first one after its earliest.
 */
KERNEL_TEST(a_duty_that_never_wakes_the_core_rides_the_next_wake)
{
    kernel_test_check(test, wake_planner_test_begin(1u), "a duty is declared");
    const uint32_t rider = wake_planner_test_duties[0].number;

    (void) kernel_wake_planner_arm(&wake_planner_test_plan, rider, 5u, KERNEL_WAKE_PLANNER_NEVER);
    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == KERNEL_WAKE_PLANNER_NEVER,
                      "it asks for no wake-up");
    kernel_test_check(test, kernel_wake_planner_serve(&wake_planner_test_plan, 4u) == 0u,
                      "a wake-up before its earliest leaves it");
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 5u) == 1u &&
                          wake_planner_test_plan.late_services == 0u,
                      "the first wake-up after its earliest serves it, never late");
}

/**
 * @brief What cannot be planned is refused: a duty without a function, a duty past the plan's size, a window
 *        that closes before it opens, a duty never declared or withdrawn; a withdrawn duty no longer wakes the
 *        core, and its number goes to the next declaration.
 */
KERNEL_TEST(what_cannot_be_planned_is_refused)
{
    kernel_test_check(test, wake_planner_test_begin(KERNEL_WAKE_PLANNER_DUTIES), "a full plan is declared");

    uint32_t distinct = 0u;
    for (uint32_t index = 0u; index < KERNEL_WAKE_PLANNER_DUTIES; ++index)
        distinct |= 1u << (wake_planner_test_duties[index].number - 1u);
    kernel_test_check(test, distinct == (1u << KERNEL_WAKE_PLANNER_DUTIES) - 1u, "each duty gets its own number");
    kernel_test_check(test,
                      kernel_wake_planner_declare(&wake_planner_test_plan, wake_planner_test_record,
                                                  &wake_planner_test_duties[0]) == KERNEL_WAKE_PLANNER_NO_DUTY,
                      "a duty past the plan's size is refused");

    const uint32_t first = wake_planner_test_duties[0].number;
    kernel_test_check(test, !kernel_wake_planner_arm(&wake_planner_test_plan, first, 20u, 19u),
                      "a window that closes before it opens is refused");
    kernel_test_check(test,
                      !kernel_wake_planner_arm(&wake_planner_test_plan, KERNEL_WAKE_PLANNER_NO_DUTY, 0u, 1u) &&
                          !kernel_wake_planner_arm(&wake_planner_test_plan, KERNEL_WAKE_PLANNER_DUTIES + 1u, 0u, 1u),
                      "a number that names no duty is refused");

    (void) kernel_wake_planner_arm(&wake_planner_test_plan, first, 0u, 10u);
    kernel_wake_planner_withdraw(&wake_planner_test_plan, first);
    kernel_test_check(test,
                      kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == KERNEL_WAKE_PLANNER_NEVER &&
                          !kernel_wake_planner_arm(&wake_planner_test_plan, first, 0u, 10u),
                      "a withdrawn duty no longer wakes the core and can no longer be armed");
    kernel_test_check(test,
                      kernel_wake_planner_declare(&wake_planner_test_plan, NULL, NULL) == KERNEL_WAKE_PLANNER_NO_DUTY &&
                          kernel_wake_planner_declare(&wake_planner_test_plan, wake_planner_test_record,
                                                      &wake_planner_test_duties[0]) == first,
                      "a duty without a function is refused, and the withdrawn number goes to the next one");
}

/**
 * @brief Over windows drawn at random, sleeping on the plan serves every window inside it, and takes exactly
 *        as many wake-ups as the fewest any schedule needs, found by trying every one.
 */
KERNEL_TEST(no_schedule_serves_the_same_windows_with_fewer_wakes)
{
    static uint64_t earliest[KERNEL_WAKE_PLANNER_DUTIES];
    static uint64_t latest[KERNEL_WAKE_PLANNER_DUTIES];
    uint32_t outside = 0u;
    uint32_t more = 0u;
    uint32_t wakes_taken = 0u;

    wake_planner_test_random_state = 0x2971D0C5u;
    for (uint32_t draw = 0u; draw < WAKE_PLANNER_TEST_DRAWS; ++draw)
    {
        const uint32_t count = wake_planner_test_draw_windows(earliest, latest);
        const uint32_t wakes = wake_planner_test_sleep_through(0u);
        wakes_taken += wakes;
        if (wakes != wake_planner_test_fewest_wakes(earliest, latest, count))
            ++more;
        if (wake_planner_test_log.count != count)
            ++outside;
        for (uint32_t run = 0u; run < wake_planner_test_log.count && run < WAKE_PLANNER_TEST_LOG; ++run)
        {
            const uint32_t window = wake_planner_test_log.duties[run] - 1u;
            const uint64_t instant = wake_planner_test_log.instants[run];
            if (instant < earliest[window] || instant > latest[window])
                ++outside;
        }
    }

    kernel_test_check(test, outside == 0u, "every window is served once, inside it");
    kernel_test_check(test, more == 0u, "no draw takes more wake-ups than the fewest any schedule needs");
    kernel_test_measure(test, "draws", WAKE_PLANNER_TEST_DRAWS);
    kernel_test_measure(test, "wakes", wakes_taken);
}

/**
 * @brief A lead arms the wake-up that much before the latest, but never before the earliest, and never below cycle 0.
 */
KERNEL_TEST(a_lead_arms_the_wake_up_before_the_latest_but_never_before_the_earliest)
{
    kernel_test_check(test, wake_planner_test_begin(1u), "a duty is declared");
    const uint32_t duty = wake_planner_test_duties[0].number;
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, duty, 100u, 1000u);

    kernel_test_check(test,
                      kernel_wake_planner_next_wake(&wake_planner_test_plan, 0u) == 1000u &&
                          kernel_wake_planner_next_wake(&wake_planner_test_plan, 300u) == 700u &&
                          kernel_wake_planner_next_wake(&wake_planner_test_plan, 900u) == 100u,
                      "a lead the window holds arms that much before its latest");
    kernel_test_check(test,
                      kernel_wake_planner_next_wake(&wake_planner_test_plan, 950u) == 100u &&
                          kernel_wake_planner_next_wake(&wake_planner_test_plan, 5000u) == 100u,
                      "a lead wider than the window, or than the latest itself, arms at the earliest");
}

/**
 * @brief The wake-up a lead arms is the earliest over every duty, not the one of the duty with the earliest latest:
 *        a wide window closing at 200 and a narrow one at [190, 195], with a lead of 50, are armed at 150 and both
 *        served at 200, the narrow one late by 5, where arming at the narrow one's earliest would serve both late.
 */
KERNEL_TEST(with_a_lead_the_most_urgent_arming_wins_over_the_earliest_latest)
{
    kernel_test_check(test, wake_planner_test_begin(2u), "two duties are declared");
    const uint32_t wide = wake_planner_test_duties[0].number;
    const uint32_t narrow = wake_planner_test_duties[1].number;
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, wide, 100u, 200u);
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, narrow, 190u, 195u);

    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan, 50u) == 150u,
                      "the wide window's arming comes first");
    kernel_test_check(test,
                      kernel_wake_planner_serve(&wake_planner_test_plan, 200u) == 2u &&
                          wake_planner_test_plan.late_services == 1u && wake_planner_test_plan.latest_overshoot == 5u,
                      "a lead after it, both are served, only the window narrower than the lead late");
}

/**
 * @brief A duty that never wakes the core is not brought forward by a lead: its latest is not a cycle.
 */
KERNEL_TEST(a_lead_never_brings_forward_a_duty_that_never_wakes_the_core)
{
    kernel_test_check(test, wake_planner_test_begin(1u), "a duty is declared");
    (void) kernel_wake_planner_arm(&wake_planner_test_plan, wake_planner_test_duties[0].number, 0u,
                                   KERNEL_WAKE_PLANNER_NEVER);

    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan, 1000u) == KERNEL_WAKE_PLANNER_NEVER,
                      "with a lead, it still rides the other wake-ups");
}

/**
 * @brief Over the same drawn windows, sleeping with wake-ups that come exactly a lead after their arming takes no
 *        more wake-ups than the fewest any schedule needs without one, serves every window once and never before it
 *        opens, by its latest when it is at least a lead wide, and else by its earliest plus the lead.
 */
KERNEL_TEST(a_lead_the_wake_ups_take_exactly_costs_no_wake_up)
{
    static uint64_t earliest[KERNEL_WAKE_PLANNER_DUTIES];
    static uint64_t latest[KERNEL_WAKE_PLANNER_DUTIES];
    uint32_t outside = 0u;
    uint32_t more = 0u;
    uint32_t late_narrow = 0u;
    uint32_t wakes_taken = 0u;

    wake_planner_test_random_state = 0x2971D0C5u;
    for (uint32_t draw = 0u; draw < WAKE_PLANNER_TEST_DRAWS; ++draw)
    {
        const uint32_t count = wake_planner_test_draw_windows(earliest, latest);
        const uint32_t wakes = wake_planner_test_sleep_through(WAKE_PLANNER_TEST_LEAD);
        wakes_taken += wakes;
        if (wakes > wake_planner_test_fewest_wakes(earliest, latest, count))
            ++more;
        if (wake_planner_test_log.count != count)
            ++outside;
        for (uint32_t run = 0u; run < wake_planner_test_log.count && run < WAKE_PLANNER_TEST_LOG; ++run)
        {
            const uint32_t window = wake_planner_test_log.duties[run] - 1u;
            const uint64_t instant = wake_planner_test_log.instants[run];
            const bool wide = latest[window] - earliest[window] >= WAKE_PLANNER_TEST_LEAD;
            const uint64_t by = wide ? latest[window] : earliest[window] + WAKE_PLANNER_TEST_LEAD;
            if (instant < earliest[window] || instant > by)
                ++outside;
            if (!wide && instant > latest[window])
                ++late_narrow;
        }
    }

    kernel_test_check(test, outside == 0u,
                      "every window is served once, from its earliest, by its latest when a lead wide");
    kernel_test_check(test, more == 0u, "no draw takes more wake-ups than the fewest without a lead");
    kernel_test_check(test, late_narrow > 0u,
                      "the control: windows narrower than the lead are among them, and some are served late");
    kernel_test_measure(test, "wakes", wakes_taken);
    kernel_test_measure(test, "late_narrow", late_narrow);
}
