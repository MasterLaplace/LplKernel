#include <kernel/power/tickless.h>

#include <kernel/acpi/tables.h>
#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/energy_counter.h>
#include <kernel/power/processor_park.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/sleep_cost.h>
#include <kernel/power/wake_latency.h>

#define TICKLESS_MICROSECONDS_PER_SECOND 1000000u
#define TICKLESS_NANOSECONDS_PER_SECOND  1000000000ull
#define TICKLESS_COUNTER_QUANTA          2u /**< Units of the energy counter a series cannot tell apart: one each end. */

static bool tickless_permitted = false;
static uint32_t tickless_nominal_hz = 0u;
static uint32_t tickless_ticks_avoided = 0u;
static uint32_t tickless_microseconds_short_of_a_tick = 0u;
static uint32_t tickless_early_wakes = 0u;
static uint64_t tickless_slept_microseconds = 0u;
static uint64_t tickless_longest_sleep_cycles = 0u;
static KernelWakeLatency_t tickless_wake_latency[PROCESSOR_SLEEP_DEPTHS] = {{0}};
static uint64_t tickless_probe_cycles = 0u;
static uint32_t tickless_probed_depths = 0u;
static uint32_t tickless_wakes_planned_on_a_published_lead = 0u;
static uint32_t tickless_wakes_planned_on_the_bound = 0u;
static uint32_t tickless_unstopped_probe_ranks[PROCESSOR_SLEEP_DEPTHS] = {0u};
static uint32_t tickless_stopped_probe_ranks[PROCESSOR_SLEEP_DEPTHS] = {0u};
static uint32_t tickless_probe_retakes = 0u;
static uint64_t tickless_residency_cycles[PROCESSOR_SLEEP_DEPTHS] = {0u};
static KernelSleepCost_t tickless_sleep_costs[PROCESSOR_SLEEP_DEPTHS] = {{0}};
static bool tickless_sleep_costs_settled = false;
static uint64_t tickless_calibration_cycles = 0u;
static uint32_t tickless_sleeps_chosen_at[PROCESSOR_SLEEP_DEPTHS] = {0u};
static uint32_t tickless_sleeps_chosen_by_cost = 0u;
static uint32_t tickless_sleeps_chosen_by_rule = 0u;
static uint32_t tickless_sleeps_without_a_choice = 0u;
static KernelSleepCostRecord_t tickless_sleep_records[KERNEL_SLEEP_COST_RECORDED] = {
    {0u, 0u, 0u}
};
static uint32_t tickless_sleeps_recorded = 0u;

_Static_assert((KERNEL_SLEEP_COST_RECORDED & (KERNEL_SLEEP_COST_RECORDED - 1u)) == 0u,
               "the record of the sleeps wraps on a mask");
_Static_assert(PROCESSOR_SLEEP_DEPTHS <= KERNEL_SLEEP_COST_DEPTHS, "a replay takes every depth of a ladder");

/** One series of a depth's calibration: how many sleeps, and how long each. */
typedef struct {
    uint32_t sleeps;       /**< Sleeps the series takes. */
    uint32_t microseconds; /**< How long each, on the timer. */
} TicklessCalibrationSeries_t;

/**
 * The series of a depth's calibration, 128 ms each so that the counter's ~1 ms update is under 1 % of them: many short
 * sleeps, few long ones, then the check between (kernel_sleep_cost_estimate).
 */
static const TicklessCalibrationSeries_t tickless_calibration_series[KERNEL_SLEEP_COST_SERIES] = {
    {64u, 2000u },
    {4u,  32000u},
    {16u, 8000u }
};

/** How a sleep armed on the local timer ended. */
typedef enum {
    TICKLESS_WAKE_UNARMED = 0, /**< The timer could not be armed: nothing slept. */
    TICKLESS_WAKE_TIMER = 1,   /**< The timer ended it. */
    TICKLESS_WAKE_EARLY = 2,   /**< Another interrupt did, the timer still armed. */
} TicklessWake_t;

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

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
static uint64_t tickless_probe_hold_before_the_read_cycles = 0u;
static uint64_t tickless_probe_hold_after_the_read_cycles = 0u;

/**
 * @brief Spins @p cycles of the timestamp counter.
 */
static void tickless_spin_for(uint64_t cycles)
{
    for (const uint64_t until = asmutils_read_timestamp_counter() + cycles; asmutils_read_timestamp_counter() < until;)
        asmutils_pause();
}

/**
 * @brief Spins the hold a debug kernel's test asked of every probe sleep before its read.
 */
static void tickless_hold_before_the_probe_read_when_asked(void)
{
    tickless_spin_for(tickless_probe_hold_before_the_read_cycles);
}

/**
 * @brief Spins the hold a debug kernel's test asked of every probe sleep once its read returned.
 */
static void tickless_hold_after_the_probe_read_when_asked(void)
{
    tickless_spin_for(tickless_probe_hold_after_the_read_cycles);
}
#else
/**
 * @brief Holds nothing: only a debug kernel's tests hold a probe sleep.
 */
static inline void tickless_hold_before_the_probe_read_when_asked(void) {}

/**
 * @brief Holds nothing: only a debug kernel's tests hold a probe sleep.
 */
static inline void tickless_hold_after_the_probe_read_when_asked(void) {}
#endif

/**
 * @brief Arms the local timer at @p armed and sleeps until an interrupt at @p depth, interrupts masked from the arming
 *        to the sleep: a deadline that passes while it is armed ends the sleep.
 * @param armed       The cycle the timer fires at.
 * @param out_after   Receives the cycle the core woke at, when it slept.
 * @param out_outcome Receives how the processor slept, when it did.
 * @param depth       The depth asked (processor_sleep_until_interrupt_at).
 * @param out_read    Receives when a port read began and returned, for a probe; NULL not to time it.
 * @return How the sleep ended.
 */
__attribute__((always_inline)) static inline TicklessWake_t tickless_sleep_until(uint64_t armed, uint64_t *out_after,
                                                                                 ProcessorSleepOutcome_t *out_outcome,
                                                                                 uint32_t depth,
                                                                                 ProcessorSleepRead_t *out_read)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    if (!advanced_pic_timer_backend_arm_at_cycle(armed))
    {
        asmutils_restore_flags(flags);
        return TICKLESS_WAKE_UNARMED;
    }

    if (out_read == NULL)
        *out_outcome = processor_sleep_until_interrupt_at(depth);
    else
    {
        *out_outcome = processor_sleep_until_interrupt_at_timing_its_read(depth, out_read);
        tickless_hold_after_the_probe_read_when_asked();
    }
    const uint64_t after = asmutils_read_timestamp_counter();
    const bool woken_early = advanced_pic_timer_backend_disarm(after) != 0u;
    asmutils_restore_flags(flags);
    *out_after = after;
    return woken_early ? TICKLESS_WAKE_EARLY : TICKLESS_WAKE_TIMER;
}

/**
 * @brief How long the longest probe sleeps: the first probe's length grown KERNEL_TICKLESS_PROBES - 1 times.
 * @return Cycles.
 */
static uint64_t tickless_longest_probe_cycles(void)
{
    uint64_t length = kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS);

    for (uint32_t probe = 1u; probe < KERNEL_TICKLESS_PROBES; ++probe)
        length *= KERNEL_TICKLESS_PROBE_GROWTH;
    return length;
}

/**
 * @brief The lead of a depth nothing else is known of: half the longest probe's length.
 * @return Cycles.
 */
static uint64_t tickless_bound_cycles(void) { return tickless_longest_probe_cycles() / 2u; }

/**
 * @brief The cycles a probe may take: its sleeps, each followed by a wake-up as late as the bound. A long sleep taken
 *        again, after the host held the processor before its read, and the sleep a probe left open asks for, run past
 *        it: a window then served late is counted so.
 * @return Cycles.
 */
static uint64_t tickless_probe_span_cycles(void)
{
    const uint64_t bound = tickless_bound_cycles();
    uint64_t length = kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS);
    uint64_t span = 0u;

    for (uint32_t probe = 0u; probe < KERNEL_TICKLESS_PROBES; ++probe, length *= KERNEL_TICKLESS_PROBE_GROWTH)
        span += length + bound;
    return span;
}

/**
 * @brief The entry and exit latency ACPI `_CST` publishes for @p depth, as the adopted ladder carries it.
 * @param depth A depth.
 * @return Cycles, or KERNEL_WAKE_LATENCY_UNPUBLISHED when the processor's CPU slot publishes no state for that depth.
 */
static uint64_t tickless_published_latency_cycles(uint32_t depth)
{
    ProcessorSleepDepth_t entered;

    return kernel_processor_sleep_depth_of(depth, &entered) && entered.latency_published ?
               kernel_tickless_cycles_from_microseconds(entered.latency) :
               KERNEL_WAKE_LATENCY_UNPUBLISHED;
}

/**
 * @brief Gives @p depth, while no wake-up taught it, the lead a shallower depth and `_CST` give it, or the bound.
 * @param depth A depth.
 * @return Where its lead comes from.
 */
static KernelWakeLeadSource_t tickless_assume_a_lead(uint32_t depth)
{
    return kernel_wake_latency_assume_unlearned(&tickless_wake_latency[depth],
                                                kernel_wake_latency_shallower_lead(tickless_wake_latency, depth),
                                                tickless_published_latency_cycles(depth), tickless_bound_cycles());
}

/**
 * @brief Whether @p depth is entered by reading a port.
 */
static bool tickless_reads_a_port(uint32_t depth)
{
    ProcessorSleepDepth_t entered;

    return kernel_processor_sleep_depth_of(depth, &entered) && entered.entry == PROCESSOR_SLEEP_ENTRY_PORT;
}

/**
 * @brief Sleeps once on the timer, @p length from now, at the depth sleeps enter, @p depth, its port read timed, and
 *        says what the sleep says of @p depth: a port read by when it began and returned against the deadline
 *        (@ref kernel_tickless_judge_a_probe_read); a halt or an MWAIT stopped the processor; a sleep the bus-master
 *        check demoted, or that the timer could not arm, says nothing of it.
 * @param depth           The depth probed.
 * @param length          How long, in cycles.
 * @param out_latency     Receives how late the wake-up came after the deadline.
 * @param out_timer_ended Receives whether the timer ended a sleep at @p depth.
 * @return What the sleep says.
 */
static KernelTicklessProbeRead_t tickless_probe_sleep_once(uint32_t depth, uint64_t length, uint64_t *out_latency,
                                                           bool *out_timer_ended)
{
    const uint64_t armed = asmutils_read_timestamp_counter() + length;
    uint64_t after = armed;
    ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_NONE, false, depth};
    ProcessorSleepRead_t read = {0u, 0u};
    tickless_hold_before_the_probe_read_when_asked();
    const TicklessWake_t wake = tickless_sleep_until(armed, &after, &outcome, depth, &read);
    const bool entered = wake != TICKLESS_WAKE_UNARMED && outcome.depth == depth;

    *out_timer_ended = entered && wake == TICKLESS_WAKE_TIMER;
    *out_latency = after > armed ? after - armed : 0u;
    if (!entered)
        return KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING;
    if (outcome.mode != PROCESSOR_SLEEP_PORT)
        return KERNEL_TICKLESS_PROBE_READ_STOPPED;
    return kernel_tickless_judge_a_probe_read(read.began, read.returned, armed);
}

/**
 * @brief Sleeps as tickless_probe_sleep_once does, and takes the sleep again while it says nothing, @p retakes times at
 *        most, each retake counted (@ref kernel_tickless_probe_retakes).
 * @param depth           The depth probed.
 * @param length          How long, in cycles.
 * @param retakes         The most times the sleep is taken again.
 * @param out_latency     Receives how late the last wake-up came after the deadline.
 * @param out_timer_ended Receives whether the timer ended the last sleep at @p depth.
 * @return What the last sleep says.
 */
static KernelTicklessProbeRead_t tickless_probe_sleep(uint32_t depth, uint64_t length, uint32_t retakes,
                                                      uint64_t *out_latency, bool *out_timer_ended)
{
    KernelTicklessProbeRead_t said = tickless_probe_sleep_once(depth, length, out_latency, out_timer_ended);

    for (uint32_t retake = 0u; said == KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING && retake < retakes; ++retake)
    {
        ++tickless_probe_retakes;
        said = tickless_probe_sleep_once(depth, length, out_latency, out_timer_ended);
    }
    return said;
}

/**
 * @brief Takes the sleep a probe left open: one more of KERNEL_TICKLESS_CONFIRMATION_MICROSECONDS, taken again while it
 *        says nothing.
 * @param depth The depth probed.
 * @return What it says.
 */
static KernelTicklessProbeRead_t tickless_take_the_confirmation(uint32_t depth)
{
    uint64_t latency = 0u;
    bool timer_ended = false;

    return tickless_probe_sleep(depth,
                                kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_CONFIRMATION_MICROSECONDS),
                                KERNEL_TICKLESS_PROBE_RETAKES, &latency, &timer_ended);
}

/**
 * @brief Whether a probe of port depth @p depth refuses it, from its ranks that stopped the processor and returned at
 *        once (@ref kernel_tickless_judge_a_probe), the sleep it left open taken when the verdict asks
 *        (@ref kernel_tickless_settle_a_probe).
 */
static bool tickless_probe_refuses(uint32_t depth, uint32_t stopped_ranks, uint32_t returned_ranks, bool validating)
{
    const KernelTicklessProbeVerdict_t verdict = kernel_tickless_judge_a_probe(stopped_ranks, returned_ranks);
    const KernelTicklessProbeRead_t confirmation = verdict == KERNEL_TICKLESS_PROBE_CONFIRMS ?
                                                       tickless_take_the_confirmation(depth) :
                                                       KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING;

    return kernel_tickless_settle_a_probe(verdict, confirmation, validating);
}

/**
 * @brief Probes @p depth's wake latency: KERNEL_TICKLESS_PROBES sleeps on the timer, the first
 *        KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS long and each next one KERNEL_TICKLESS_PROBE_GROWTH times the one
 *        before, so that a hypervisor's halt-polling window and a package state that engages late are both crossed.
 *        Those the timer ended teach the depth's estimate from the longest sleep down, a latency counting at most as
 *        long as that longest sleep, and their time with the tick stopped is counted. A port depth awaiting a probe is
 *        entered for it alone (@ref kernel_processor_sleep_begin_probe), and its reads are judged by when each began
 *        and returned (@ref kernel_tickless_judge_a_probe_read), a long sleep whose read says nothing taken again
 *        KERNEL_TICKLESS_PROBE_RETAKES times at most; one whose two longest reads did not both stop the processor
 *        (@ref kernel_tickless_judge_a_probe) is refused and taught nothing, so that the next stop of the tick, once an
 *        initialisation adopts the depth again, probes it again; one whose reads stopped it is validated. A depth
 *        that reads no port is never refused.
 * @param depth The depth: the one sleeps enter, or the one awaiting a probe.
 * @return The microseconds the probe took.
 */
static uint32_t tickless_probe(uint32_t depth)
{
    uint64_t latencies[KERNEL_TICKLESS_PROBES];
    uint32_t taught = 0u;
    uint32_t unstopped_ranks = 0u;
    uint32_t stopped_ranks = 0u;
    uint64_t length = kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS);
    const bool validating = depth == kernel_processor_sleep_depth_awaiting_probe();
    const bool port = tickless_reads_a_port(depth);
    const uint64_t began = asmutils_read_timestamp_counter();

    if (validating)
        kernel_processor_sleep_begin_probe();
    for (uint32_t probe = 0u; probe < KERNEL_TICKLESS_PROBES; ++probe, length *= KERNEL_TICKLESS_PROBE_GROWTH)
    {
        uint64_t latency = 0u;
        bool timer_ended = false;
        const uint32_t retakes = port && probe + 2u >= KERNEL_TICKLESS_PROBES ? KERNEL_TICKLESS_PROBE_RETAKES : 0u;
        const KernelTicklessProbeRead_t said = tickless_probe_sleep(depth, length, retakes, &latency, &timer_ended);
        unstopped_ranks |= said == KERNEL_TICKLESS_PROBE_READ_RETURNED ? 1u << probe : 0u;
        stopped_ranks |= said == KERNEL_TICKLESS_PROBE_READ_STOPPED ? 1u << probe : 0u;
        if (said != KERNEL_TICKLESS_PROBE_READ_RETURNED && timer_ended)
            latencies[taught++] = latency;
    }
    const bool refused = port && tickless_probe_refuses(depth, stopped_ranks, unstopped_ranks, validating);
    if (validating)
        kernel_processor_sleep_end_probe(!refused);
    else if (refused)
        (void) kernel_processor_sleep_refuse_depth(depth);
    if (validating || refused)
        kernel_processor_park_reconsider();
    kernel_wake_latency_assume(&tickless_wake_latency[depth], tickless_bound_cycles());
    if (!refused)
        kernel_wake_latency_learn_from_the_longest(&tickless_wake_latency[depth], latencies, taught);
    tickless_probed_depths |= 1u << depth;
    tickless_unstopped_probe_ranks[depth] = unstopped_ranks;
    tickless_stopped_probe_ranks[depth] = stopped_ranks;
    tickless_probe_cycles = asmutils_read_timestamp_counter() - began;
    const uint32_t probed = tickless_microseconds_from_cycles(
        tickless_probe_cycles, advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
    tickless_slept_microseconds += probed;
    tickless_count_ticks_avoided(probed);
    return probed;
}

/**
 * @brief Probes @p depth when its probe ends before the plan's next wake-up and it was not probed since the tick
 *        stopped, its lead assumed first.
 * @param plan  The session's plan.
 * @param depth The depth.
 * @return The microseconds the probe took, or 0.
 */
static uint32_t tickless_probe_when_it_fits(KernelWakePlan_t *plan, uint32_t depth)
{
    (void) tickless_assume_a_lead(depth);
    const uint64_t now = asmutils_read_timestamp_counter();
    const uint64_t planned =
        kernel_wake_planner_next_wake(plan, kernel_wake_latency_lead(&tickless_wake_latency[depth]));
    const bool probed = ((tickless_probed_depths >> depth) & 1u) != 0u;

    if (!kernel_tickless_probes_before_sleeping(probed, planned > now ? planned - now : 0u,
                                                tickless_probe_span_cycles()))
        return 0u;
    return tickless_probe(depth);
}

/**
 * @brief Counts the wake-up a sleep at @p depth plans by where its lead comes from while it is still assumed.
 * @param depth The depth slept at.
 */
static void tickless_count_where_the_lead_comes_from(uint32_t depth)
{
    const KernelWakeLeadSource_t source = tickless_assume_a_lead(depth);

    if (source == KERNEL_WAKE_LEAD_PUBLISHED)
        ++tickless_wakes_planned_on_a_published_lead;
    else if (source == KERNEL_WAKE_LEAD_BOUND)
        ++tickless_wakes_planned_on_the_bound;
}

/**
 * @brief Sleeps until any interrupt, the tick's when it runs, then serves @p plan at the wake-up.
 * @param plan The plan.
 * @return 0: no time is counted slept without the one-shot timer.
 */
static uint32_t tickless_sleep_until_any_interrupt(KernelWakePlan_t *plan)
{
    (void) processor_sleep_until_interrupt();
    (void) kernel_wake_planner_serve(plan, asmutils_read_timestamp_counter());
    return 0u;
}

/**
 * @brief Sleeps once on @p plan with the tick stopped at @p depth, armed its lead before the plan's next wake-up, then
 *        serves the plan at the wake-up; a wake-up the timer ended teaches the estimate of the depth the sleep entered,
 *        @p depth's unless the bus-master check demoted it.
 * @param plan   The session's plan.
 * @param depth  The depth the sleep asks for.
 * @param before When the sleep began, its depth chosen since: the microseconds slept count from it.
 * @return The microseconds slept.
 */
__attribute__((always_inline)) static inline uint32_t tickless_sleep_at(KernelWakePlan_t *plan, uint32_t depth,
                                                                        uint64_t before)
{
    const uint64_t planned =
        kernel_wake_planner_next_wake(plan, kernel_wake_latency_lead(&tickless_wake_latency[depth]));
    if (planned <= before)
    {
        (void) kernel_wake_planner_serve(plan, before);
        return 0u;
    }

    const uint64_t longest = before + tickless_longest_sleep_cycles;
    const uint64_t armed = planned < longest ? planned : longest;
    uint64_t after = armed;
    ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_NONE, false, PROCESSOR_SLEEP_DEPTH_C1};
    const TicklessWake_t wake = tickless_sleep_until(armed, &after, &outcome, depth, NULL);
    if (wake == TICKLESS_WAKE_UNARMED)
        return tickless_sleep_until_any_interrupt(plan);
    if (wake == TICKLESS_WAKE_EARLY)
        ++tickless_early_wakes;
    else
        kernel_wake_latency_learn(&tickless_wake_latency[outcome.depth], after > armed ? after - armed : 0u);

    const uint32_t elapsed = tickless_microseconds_from_cycles(
        after - before, advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
    tickless_slept_microseconds += elapsed;
    tickless_count_ticks_avoided(elapsed);

    (void) kernel_wake_planner_serve(plan, after);
    return elapsed;
}

/**
 * @brief Sleeps once on @p plan at @p depth, which no wake-up taught: probes it first when the probe fits, and the
 * depth it falls to when the probe refuses it, then counts where the lead of the depth slept at comes from, and sleeps
 *        on that lead.
 * @param plan  The session's plan.
 * @param depth The depth the sleep enters.
 * @return The microseconds the probes and the sleep took.
 */
__attribute__((cold, noinline)) static uint32_t tickless_sleep_at_an_unlearned_depth(KernelWakePlan_t *plan,
                                                                                     uint32_t depth)
{
    uint32_t probed = tickless_probe_when_it_fits(plan, depth);

    for (uint32_t kept = kernel_processor_sleep_depth(); kept != depth; kept = kernel_processor_sleep_depth())
    {
        depth = kept;
        if (tickless_wake_latency[depth].samples != 0u)
            return probed + tickless_sleep_at(plan, depth, asmutils_read_timestamp_counter());
        probed += tickless_probe_when_it_fits(plan, depth);
    }
    tickless_count_where_the_lead_comes_from(depth);
    return probed + tickless_sleep_at(plan, depth, asmutils_read_timestamp_counter());
}

/**
 * @brief Probes the depth awaiting a probe, and each one the active depth falls to while it awaits one, until the
 *        active depth is validated.
 */
static void tickless_probe_until_the_active_depth_is_validated(void)
{
    for (uint32_t awaiting = kernel_processor_sleep_depth_awaiting_probe(); awaiting != PROCESSOR_SLEEP_NO_DEPTH;
         awaiting = kernel_processor_sleep_depth_awaiting_probe())
        (void) tickless_probe(awaiting);
}

/**
 * @brief Probes, as the active depth for a while, the depth the bus-master check demotes @p depth to, the deepest
 *        available below it without the check, so that a demotion lands there once it stops the processor rather
 *        than on C1, as ACPI demotes C3 to C2 (ACPI 6.6 §8.1.4); then makes @p depth active again.
 * @param depth The active depth, validated.
 */
static void tickless_probe_where_the_check_demotes(uint32_t depth)
{
    const uint32_t demoted = kernel_processor_sleep_depth_after_the_bus_master_check(
        kernel_processor_sleep_ladder(), kernel_processor_sleep_available_depths(), depth,
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_STATUS);

    if (demoted == depth)
        return;
    (void) kernel_processor_sleep_request_depth(demoted);
    tickless_probe_until_the_active_depth_is_validated();
    (void) kernel_processor_sleep_request_depth(depth);
}

/**
 * @brief The depths available and validated, a bit each.
 */
static uint32_t tickless_usable_depths(void)
{
    return kernel_processor_sleep_available_depths() & kernel_processor_sleep_validated_depths();
}

/**
 * @brief Whether a sleep under @p ceiling, the depth sleeps enter, may take @p depth: the ceiling itself, or a depth
 *        available and validated below it.
 */
static bool tickless_may_take(uint32_t depth, uint32_t ceiling)
{
    return depth == ceiling || (depth < ceiling && ((tickless_usable_depths() >> depth) & 1u) != 0u);
}

/**
 * @brief The depths a wake-up taught, a bit each.
 */
static uint32_t tickless_taught_depths(void)
{
    uint32_t taught = 0u;

    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth < PROCESSOR_SLEEP_DEPTHS; ++depth)
        taught |= tickless_wake_latency[depth].samples != 0u ? 1u << depth : 0u;
    return taught;
}

/**
 * @brief Probes each depth a sleep may take below @p ceiling that no wake-up taught, so that each has its own lead to
 *        be chosen on.
 */
static void tickless_probe_the_depths_below(uint32_t ceiling)
{
    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth < ceiling; ++depth)
    {
        const uint32_t unprobed =
            kernel_tickless_depths_to_probe_below(tickless_usable_depths(), tickless_taught_depths(), ceiling);
        if (((unprobed >> depth) & 1u) != 0u)
            (void) tickless_probe(depth);
    }
}

/**
 * @brief Probes the depth awaiting a probe, and each one the active depth falls to while it awaits one, until the
 *        active depth is validated; then the depth the bus-master check demotes it to; then the depth sleeps enter,
 *        while no wake-up taught it; then each depth a sleep may take below it, while no wake-up taught it.
 */
static void tickless_probe_the_depth_sleeps_enter(void)
{
    tickless_probe_until_the_active_depth_is_validated();
    tickless_probe_where_the_check_demotes(kernel_processor_sleep_depth());

    const uint32_t depth = kernel_processor_sleep_depth();
    if (tickless_wake_latency[depth].samples == 0u)
        (void) tickless_probe(depth);
    tickless_probe_the_depths_below(depth);
}

/**
 * @brief Converts each depth's `_CST` residency to cycles once a session, so that no sleep divides to choose.
 */
static void tickless_convert_the_residencies(void)
{
    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth < PROCESSOR_SLEEP_DEPTHS; ++depth)
        tickless_residency_cycles[depth] =
            kernel_tickless_cycles_from_microseconds(kernel_processor_sleep_residency_of(depth));
}

/**
 * @brief The cycles the calibration of every depth up to @p ceiling sleeps.
 */
static uint64_t tickless_calibration_span_cycles(uint32_t ceiling)
{
    uint32_t microseconds = 0u;

    for (uint32_t index = 0u; index < KERNEL_SLEEP_COST_SERIES; ++index)
        microseconds += tickless_calibration_series[index].sleeps * tickless_calibration_series[index].microseconds;
    return kernel_tickless_cycles_from_microseconds(microseconds) * (ceiling + 1u);
}

/**
 * @brief Measures one series of @p series at @p depth: its sleeps on the timer, the package energy counter read before
 *        the first and after the last.
 * @return KERNEL_SLEEP_COST_MEASURED with @p out written; UNREAD when a read failed, UNTIMED when the timer could not
 *         be armed, NOISY when a sleep entered another depth, the bus-master check having demoted it.
 */
static uint32_t tickless_measure_a_series(uint32_t depth, const TicklessCalibrationSeries_t *series,
                                          KernelSleepCostSeries_t *out)
{
    const uint64_t length = kernel_tickless_cycles_from_microseconds(series->microseconds);
    uint32_t before = 0u;
    uint32_t after = 0u;
    bool demoted = false;

    if (!kernel_energy_counter_sample(&before))
        return KERNEL_SLEEP_COST_UNREAD;
    const uint64_t began = asmutils_read_timestamp_counter();
    for (uint32_t sleep = 0u; sleep < series->sleeps; ++sleep)
    {
        uint64_t woke = 0u;
        ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_NONE, false, depth};
        if (tickless_sleep_until(asmutils_read_timestamp_counter() + length, &woke, &outcome, depth, NULL) ==
            TICKLESS_WAKE_UNARMED)
            return KERNEL_SLEEP_COST_UNTIMED;
        demoted = demoted || outcome.depth != depth;
    }
    const uint64_t ended = asmutils_read_timestamp_counter();
    if (!kernel_energy_counter_sample(&after))
        return KERNEL_SLEEP_COST_UNREAD;
    if (demoted)
        return KERNEL_SLEEP_COST_NOISY;
    out->sleeps = series->sleeps;
    out->nanoseconds = kernel_tickless_nanoseconds_from_cycles(ended - began);
    out->nanojoules = kernel_energy_counter_nanojoules_between(before, after);
    return KERNEL_SLEEP_COST_MEASURED;
}

/**
 * @brief Calibrates @p depth: its series, then the costs they solve, or why they solve none.
 */
static void tickless_calibrate_a_depth(uint32_t depth)
{
    KernelSleepCostSeries_t series[KERNEL_SLEEP_COST_SERIES];
    const KernelSleepCost_t unmeasured = {KERNEL_SLEEP_COST_NOT_CALIBRATED, 0u, 0u, 0u};

    for (uint32_t index = 0u; index < KERNEL_SLEEP_COST_SERIES; ++index)
    {
        const uint32_t source = tickless_measure_a_series(depth, &tickless_calibration_series[index], &series[index]);
        if (source == KERNEL_SLEEP_COST_MEASURED)
            continue;
        tickless_sleep_costs[depth] = unmeasured;
        tickless_sleep_costs[depth].source = source;
        return;
    }
    kernel_sleep_cost_estimate(series, kernel_energy_counter_nanojoules_between(0u, TICKLESS_COUNTER_QUANTA),
                               advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz(),
                               &tickless_sleep_costs[depth]);
}

/**
 * @brief Calibrates each depth a sleep may take up to @p ceiling, from @p began: the time is counted slept, as a
 *        probe's.
 */
static void tickless_calibrate_the_depths(uint32_t ceiling, uint64_t began)
{
    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth <= ceiling; ++depth)
        if (tickless_may_take(depth, ceiling))
            tickless_calibrate_a_depth(depth);
    tickless_calibration_cycles = asmutils_read_timestamp_counter() - began;
    const uint32_t calibrated = tickless_microseconds_from_cycles(
        tickless_calibration_cycles, advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
    tickless_slept_microseconds += calibrated;
    tickless_count_ticks_avoided(calibrated);
}

/**
 * @brief Marks each depth a sleep may take up to @p ceiling as costed by no counter, which sleeps nothing.
 */
static void tickless_mark_the_depths_uncounted(uint32_t ceiling)
{
    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth <= ceiling; ++depth)
        if (tickless_may_take(depth, ceiling))
            tickless_sleep_costs[depth].source = KERNEL_SLEEP_COST_NO_COUNTER;
}

/**
 * @brief Settles once a boot what a sleep costs at each depth a sleep may take up to @p ceiling: measured where the
 *        package energy counter is present and @p plan's next wake-up leaves the calibration its time, no counter
 *        otherwise; a plan that leaves too little leaves them unsettled, for the next stop of the tick.
 */
static void tickless_settle_the_sleep_costs(const KernelWakePlan_t *plan, uint32_t ceiling)
{
    const bool counter = kernel_energy_counter()->state == KERNEL_ENERGY_COUNTER_PRESENT;
    const uint64_t began = asmutils_read_timestamp_counter();
    const uint64_t next = kernel_wake_planner_next_wake(plan, 0u);

    if (tickless_sleep_costs_settled ||
        (counter && (next <= began || next - began < tickless_calibration_span_cycles(ceiling))))
        return;
    if (counter)
        tickless_calibrate_the_depths(ceiling, began);
    else
        tickless_mark_the_depths_uncounted(ceiling);
    tickless_sleep_costs_settled = true;
}

/**
 * @brief What a sleep on @p plan from @p now would be at @p depth under @p ceiling, for kernel_sleep_cost_choose.
 */
static void tickless_describe_a_candidate(const KernelWakePlan_t *plan, uint32_t depth, uint32_t ceiling, uint64_t now,
                                          KernelSleepCostCandidate_t *out)
{
    const KernelWakeLatency_t *const latency = &tickless_wake_latency[depth];
    const uint64_t planned = kernel_wake_planner_next_wake(plan, kernel_wake_latency_lead(latency));

    out->allowed = tickless_may_take(depth, ceiling) && latency->samples != 0u;
    out->asleep_cycles = kernel_sleep_cost_cycles_until(now, planned, tickless_longest_sleep_cycles);
    out->residency_cycles = tickless_residency_cycles[depth];
    out->cost = tickless_sleep_costs[depth];
}

/**
 * @brief Records a sleep on @p plan from @p now at @p depth: its window to the plan's next wake-up, capped by the
 *        longest sleep, the depth, and whether measured costs chose it.
 */
static void tickless_record_a_sleep(const KernelWakePlan_t *plan, uint64_t now, uint32_t depth, bool by_cost)
{
    KernelSleepCostRecord_t *const record =
        &tickless_sleep_records[tickless_sleeps_recorded & (KERNEL_SLEEP_COST_RECORDED - 1u)];

    record->window_cycles =
        kernel_sleep_cost_cycles_until(now, kernel_wake_planner_next_wake(plan, 0u), tickless_longest_sleep_cycles);
    record->depth = depth;
    record->by_cost = by_cost ? 1u : 0u;
    ++tickless_sleeps_recorded;
}

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
static uint64_t tickless_choice_hold_cycles = 0u;

/**
 * @brief Spins the hold a debug kernel's test asked of the next depth choice, then forgets it.
 */
static void tickless_hold_the_choice_when_asked(void)
{
    const uint64_t hold = tickless_choice_hold_cycles;

    tickless_choice_hold_cycles = 0u;
    tickless_spin_for(hold);
}
#else
/**
 * @brief Holds nothing: only a debug kernel's tests hold a depth choice.
 */
static inline void tickless_hold_the_choice_when_asked(void) {}
#endif

/**
 * @brief The depth a sleep on @p plan from @p now enters under @p ceiling, the depth sleeps enter: the cheapest by
 *        measured costs, or the rule's (kernel_sleep_cost_choose), counted by which decided; @p ceiling when no depth
 *        would sleep, whose sleep then serves the plan at once.
 */
static uint32_t tickless_choose_a_depth(const KernelWakePlan_t *plan, uint32_t ceiling, uint64_t now)
{
    KernelSleepCostCandidate_t candidates[PROCESSOR_SLEEP_DEPTHS];
    bool by_cost = false;

    tickless_hold_the_choice_when_asked();
    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth <= ceiling; ++depth)
        tickless_describe_a_candidate(plan, depth, ceiling, now, &candidates[depth]);
    const uint32_t chosen = kernel_sleep_cost_choose(candidates, ceiling + 1u, &by_cost);
    if (chosen == KERNEL_SLEEP_COST_NO_DEPTH)
    {
        ++tickless_sleeps_without_a_choice;
        return ceiling;
    }
    ++tickless_sleeps_chosen_at[chosen];
    if (by_cost)
        ++tickless_sleeps_chosen_by_cost;
    else
        ++tickless_sleeps_chosen_by_rule;
    tickless_record_a_sleep(plan, now, chosen, by_cost);
    return chosen;
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
    tickless_probed_depths = 0u;
    tickless_probe_the_depth_sleeps_enter();
    tickless_convert_the_residencies();
    tickless_settle_the_sleep_costs(plan, kernel_processor_sleep_depth());
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

    const uint32_t awaiting = kernel_processor_sleep_depth_awaiting_probe();
    const uint32_t probed = awaiting == PROCESSOR_SLEEP_NO_DEPTH ? 0u : tickless_probe_when_it_fits(plan, awaiting);
    const uint32_t depth = kernel_processor_sleep_depth();
    if (tickless_wake_latency[depth].samples == 0u)
        return probed + tickless_sleep_at_an_unlearned_depth(plan, depth);
    const uint64_t now = asmutils_read_timestamp_counter();
    return probed + tickless_sleep_at(plan, tickless_choose_a_depth(plan, depth, now), now);
}

uint32_t kernel_tickless_depths_to_probe_below(uint32_t usable, uint32_t taught, uint32_t ceiling)
{
    return usable & ~taught & ((1u << ceiling) - 1u);
}

uint32_t kernel_tickless_ticks_avoided(void) { return tickless_ticks_avoided; }

uint32_t kernel_tickless_early_wakes(void) { return tickless_early_wakes; }

uint64_t kernel_tickless_wake_lead_cycles(void)
{
    return kernel_wake_latency_lead(&tickless_wake_latency[kernel_processor_sleep_depth()]);
}

uint32_t kernel_tickless_wake_samples(void) { return tickless_wake_latency[kernel_processor_sleep_depth()].samples; }

uint32_t kernel_tickless_wake_samples_at(uint32_t depth)
{
    return depth < PROCESSOR_SLEEP_DEPTHS ? tickless_wake_latency[depth].samples : 0u;
}

KernelTicklessProbeVerdict_t kernel_tickless_judge_a_probe(uint32_t stopped_ranks, uint32_t returned_ranks)
{
    const uint32_t longest = 1u << (KERNEL_TICKLESS_PROBES - 1u);
    const uint32_t second_longest = 1u << (KERNEL_TICKLESS_PROBES - 2u);
    const uint32_t long_ones_stopped =
        ((stopped_ranks & longest) != 0u ? 1u : 0u) + ((stopped_ranks & second_longest) != 0u ? 1u : 0u);

    if (long_ones_stopped == 2u)
        return KERNEL_TICKLESS_PROBE_KEEPS;
    if (long_ones_stopped == 1u)
        return KERNEL_TICKLESS_PROBE_CONFIRMS;
    return (returned_ranks & (longest | second_longest)) != 0u ? KERNEL_TICKLESS_PROBE_REFUSES :
                                                                 KERNEL_TICKLESS_PROBE_SAYS_NOTHING;
}

bool kernel_tickless_settle_a_probe(KernelTicklessProbeVerdict_t verdict, KernelTicklessProbeRead_t confirmation,
                                    bool validating)
{
    if (verdict == KERNEL_TICKLESS_PROBE_CONFIRMS && confirmation == KERNEL_TICKLESS_PROBE_READ_STOPPED)
        return false;
    if (verdict == KERNEL_TICKLESS_PROBE_CONFIRMS && confirmation == KERNEL_TICKLESS_PROBE_READ_RETURNED)
        return true;
    if (verdict == KERNEL_TICKLESS_PROBE_CONFIRMS || verdict == KERNEL_TICKLESS_PROBE_SAYS_NOTHING)
        return validating;
    return verdict == KERNEL_TICKLESS_PROBE_REFUSES;
}

KernelTicklessProbeRead_t kernel_tickless_judge_a_probe_read(uint64_t began, uint64_t returned, uint64_t deadline)
{
    if (began >= deadline)
        return KERNEL_TICKLESS_PROBE_READ_SAYS_NOTHING;
    return returned < deadline ? KERNEL_TICKLESS_PROBE_READ_RETURNED : KERNEL_TICKLESS_PROBE_READ_STOPPED;
}

uint32_t kernel_tickless_unstopped_probe_ranks_at(uint32_t depth)
{
    return depth < PROCESSOR_SLEEP_DEPTHS ? tickless_unstopped_probe_ranks[depth] : 0u;
}

uint32_t kernel_tickless_stopped_probe_ranks_at(uint32_t depth)
{
    return depth < PROCESSOR_SLEEP_DEPTHS ? tickless_stopped_probe_ranks[depth] : 0u;
}

uint32_t kernel_tickless_probe_retakes(void) { return tickless_probe_retakes; }

uint64_t kernel_tickless_probe_cycles(void) { return tickless_probe_cycles; }

bool kernel_tickless_probes_before_sleeping(bool probed, uint64_t until_the_wake, uint64_t probe_span)
{
    return !probed && until_the_wake >= probe_span;
}

uint32_t kernel_tickless_wakes_planned_on_a_published_lead(void) { return tickless_wakes_planned_on_a_published_lead; }

uint32_t kernel_tickless_wakes_planned_on_the_bound(void) { return tickless_wakes_planned_on_the_bound; }

const KernelSleepCost_t *kernel_tickless_sleep_cost_at(uint32_t depth)
{
    return depth < PROCESSOR_SLEEP_DEPTHS ? &tickless_sleep_costs[depth] : NULL;
}

uint32_t kernel_tickless_sleeps_chosen_at(uint32_t depth)
{
    return depth < PROCESSOR_SLEEP_DEPTHS ? tickless_sleeps_chosen_at[depth] : 0u;
}

uint32_t kernel_tickless_sleeps_chosen_by_cost(void) { return tickless_sleeps_chosen_by_cost; }

uint32_t kernel_tickless_sleeps_chosen_by_rule(void) { return tickless_sleeps_chosen_by_rule; }

uint32_t kernel_tickless_sleeps_without_a_choice(void) { return tickless_sleeps_without_a_choice; }

uint64_t kernel_tickless_calibration_cycles(void) { return tickless_calibration_cycles; }

uint32_t kernel_tickless_sleeps_recorded(void) { return tickless_sleeps_recorded; }

void kernel_tickless_replay_the_session(KernelSleepCostReplay_t *out)
{
    KernelSleepCostReplayDepth_t depths[PROCESSOR_SLEEP_DEPTHS];
    const uint32_t ceiling = kernel_processor_sleep_depth();

    for (uint32_t depth = PROCESSOR_SLEEP_DEPTH_C1; depth <= ceiling; ++depth)
    {
        depths[depth].allowed = tickless_may_take(depth, ceiling) && tickless_wake_latency[depth].samples != 0u;
        depths[depth].lead_cycles = kernel_wake_latency_lead(&tickless_wake_latency[depth]);
        depths[depth].residency_cycles = tickless_residency_cycles[depth];
        depths[depth].cost = tickless_sleep_costs[depth];
    }
    kernel_sleep_cost_replay(tickless_sleep_records, tickless_sleeps_recorded, depths, ceiling + 1u, out);
}

uint64_t kernel_tickless_nanoseconds_from_cycles(uint64_t cycles)
{
    const uint64_t hertz = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    return hertz != 0u ? (cycles / hertz) * TICKLESS_NANOSECONDS_PER_SECOND +
                             ((cycles % hertz) * TICKLESS_NANOSECONDS_PER_SECOND) / hertz :
                         0u;
}

uint64_t kernel_tickless_slept_microseconds(void) { return tickless_slept_microseconds; }

uint32_t kernel_tickless_nominal_frequency_hz(void) { return tickless_nominal_hz; }

#if defined(LPL_KERNEL_ENABLE_SMOKE_TESTS)
void kernel_tickless_debug_exchange_depth_estimate(uint32_t depth, KernelWakeLatency_t *estimate)
{
    if (depth > PROCESSOR_SLEEP_DEPTH_DEEPEST)
        return;

    const KernelWakeLatency_t taken = tickless_wake_latency[depth];
    tickless_wake_latency[depth] = *estimate;
    *estimate = taken;
    tickless_probed_depths &= ~(1u << depth);
}

void kernel_tickless_debug_hold_the_next_depth_choice(uint32_t microseconds)
{
    tickless_choice_hold_cycles = kernel_tickless_cycles_from_microseconds(microseconds);
}

void kernel_tickless_debug_hold_the_probe_sleeps(uint32_t before_the_read, uint32_t after_the_read)
{
    tickless_probe_hold_before_the_read_cycles = kernel_tickless_cycles_from_microseconds(before_the_read);
    tickless_probe_hold_after_the_read_cycles = kernel_tickless_cycles_from_microseconds(after_the_read);
}
#endif
