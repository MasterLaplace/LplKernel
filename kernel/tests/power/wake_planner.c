#include <kernel/power/wake_planner.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(wake_planner, KERNEL_TEST_STAGE_BOOTED);

/** Runs a check records, at most, in one test. */
#define WAKE_PLANNER_TEST_LOG 16u

/** Window sets the comparison with every other schedule draws. */
#define WAKE_PLANNER_TEST_DRAWS 96u

/** Cycles over which a drawn window opens, and the longest one drawn. */
#define WAKE_PLANNER_TEST_SPAN   1000u
#define WAKE_PLANNER_TEST_LENGTH 300u

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
 * @brief Runs a plan the way a core sleeps on it: wakes at its next wake-up until none is needed, or until
 *        it has woken once more than it has duties, which no plan of drawn windows needs.
 * @return The wake-ups taken; more than KERNEL_WAKE_PLANNER_DUTIES when the plan never stopped asking.
 */
static uint32_t wake_planner_test_sleep_through(void)
{
    uint32_t wakes = 0u;

    for (uint64_t next = kernel_wake_planner_next_wake(&wake_planner_test_plan);
         next != KERNEL_WAKE_PLANNER_NEVER && wakes <= KERNEL_WAKE_PLANNER_DUTIES;
         next = kernel_wake_planner_next_wake(&wake_planner_test_plan))
    {
        (void) kernel_wake_planner_serve(&wake_planner_test_plan, next);
        ++wakes;
    }
    return wakes;
}

/**
 * @brief An empty plan asks for no wake-up, and a wake-up it is given finds nothing to serve.
 */
KERNEL_TEST(a_plan_with_no_window_never_wakes_the_core)
{
    (void) wake_planner_test_begin(0u);

    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan) == KERNEL_WAKE_PLANNER_NEVER,
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
    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan) == 60u,
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
    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan) == 300u,
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
                      kernel_wake_planner_next_wake(&wake_planner_test_plan) == 5u &&
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
    kernel_test_check(test, kernel_wake_planner_next_wake(&wake_planner_test_plan) == KERNEL_WAKE_PLANNER_NEVER,
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
                      kernel_wake_planner_next_wake(&wake_planner_test_plan) == KERNEL_WAKE_PLANNER_NEVER &&
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
        const uint32_t count = 1u + wake_planner_test_draw() % KERNEL_WAKE_PLANNER_DUTIES;

        (void) wake_planner_test_begin(count);
        for (uint32_t window = 0u; window < count; ++window)
        {
            earliest[window] = wake_planner_test_draw() % WAKE_PLANNER_TEST_SPAN;
            latest[window] = earliest[window] + wake_planner_test_draw() % (WAKE_PLANNER_TEST_LENGTH + 1u);
            (void) kernel_wake_planner_arm(&wake_planner_test_plan, wake_planner_test_duties[window].number,
                                           earliest[window], latest[window]);
        }

        const uint32_t wakes = wake_planner_test_sleep_through();
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
