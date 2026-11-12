#include <kernel/power/tickless.h>

#include <kernel/core/reconciler.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_park.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/wake_latency.h>

#define TICKLESS_MICROSECONDS_PER_SECOND 1000000u
#define TICKLESS_NANOSECONDS_PER_SECOND  1000000000ull

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

/**
 * @brief Arms the local timer at @p armed and sleeps until an interrupt, interrupts masked from the arming to the
 *        sleep: a deadline that passes while it is armed ends the sleep.
 * @param armed       The cycle the timer fires at.
 * @param out_after   Receives the cycle the core woke at, when it slept.
 * @param out_outcome Receives how the processor slept, when it did.
 * @return How the sleep ended.
 */
__attribute__((always_inline)) static inline TicklessWake_t tickless_sleep_until(uint64_t armed, uint64_t *out_after,
                                                                                 ProcessorSleepOutcome_t *out_outcome)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    if (!advanced_pic_timer_backend_arm_at_cycle(armed))
    {
        asmutils_restore_flags(flags);
        return TICKLESS_WAKE_UNARMED;
    }

    *out_outcome = processor_sleep_until_interrupt();
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
 * @brief The cycles a probe may take: its sleeps, each followed by a wake-up as late as the bound.
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
 * @brief Sleeps once on the timer, @p length from now, at the depth sleeps enter, @p depth, and judges the sleep only
 *        when it entered @p depth: one the bus-master check demoted says nothing of it.
 * @param depth           The depth probed.
 * @param length          How long, in cycles.
 * @param out_latency     Receives how late the wake-up came after the deadline.
 * @param out_timer_ended Receives whether the timer ended a sleep at @p depth.
 * @return Whether the sleep was a port read at @p depth that did not stop the processor
 *         (@ref kernel_tickless_probe_sleep_did_not_stop).
 */
static bool tickless_probe_sleep(uint32_t depth, uint64_t length, uint64_t *out_latency, bool *out_timer_ended)
{
    const uint64_t armed = asmutils_read_timestamp_counter() + length;
    uint64_t after = armed;
    ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_NONE, false, depth};
    const TicklessWake_t wake = tickless_sleep_until(armed, &after, &outcome);
    const bool entered = outcome.depth == depth;

    *out_timer_ended = entered && wake == TICKLESS_WAKE_TIMER;
    *out_latency = after > armed ? after - armed : 0u;
    return entered && kernel_tickless_probe_sleep_did_not_stop(wake == TICKLESS_WAKE_EARLY, &outcome);
}

/**
 * @brief Whether a probe of port depth @p depth whose ranks @p unstopped_ranks did not stop the processor refuses it,
 *        one more sleep of KERNEL_TICKLESS_CONFIRMATION_MICROSECONDS deciding when @ref kernel_tickless_judge_a_probe
 *        asks.
 */
static bool tickless_probe_refuses(uint32_t depth, uint32_t unstopped_ranks)
{
    const KernelTicklessProbeVerdict_t verdict = kernel_tickless_judge_a_probe(unstopped_ranks);
    uint64_t latency = 0u;
    bool timer_ended = false;

    if (verdict != KERNEL_TICKLESS_PROBE_CONFIRMS)
        return verdict == KERNEL_TICKLESS_PROBE_REFUSES;
    return tickless_probe_sleep(depth,
                                kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_CONFIRMATION_MICROSECONDS),
                                &latency, &timer_ended);
}

/**
 * @brief Probes @p depth's wake latency: KERNEL_TICKLESS_PROBES sleeps on the timer, the first
 *        KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS long and each next one KERNEL_TICKLESS_PROBE_GROWTH times the one
 *        before, so that a hypervisor's halt-polling window and a package state that engages late are both crossed.
 *        Those the timer ended teach the depth's estimate from the longest sleep down, a latency counting at most as
 *        long as that longest sleep, and their time with the tick stopped is counted. A port depth awaiting a probe is
 *        entered for it alone (@ref kernel_processor_sleep_begin_probe); one whose reads did not stop the processor
 *        (@ref kernel_tickless_judge_a_probe) is refused and taught nothing, so that the next stop of the tick, once an
 *        initialisation adopts the depth again, probes it again; one whose reads stopped it is validated.
 * @param depth The depth: the one sleeps enter, or the one awaiting a probe.
 * @return The microseconds the probe took.
 */
static uint32_t tickless_probe(uint32_t depth)
{
    uint64_t latencies[KERNEL_TICKLESS_PROBES];
    uint32_t taught = 0u;
    uint32_t unstopped_ranks = 0u;
    uint64_t length = kernel_tickless_cycles_from_microseconds(KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS);
    const bool validating = depth == kernel_processor_sleep_depth_awaiting_probe();
    const uint64_t began = asmutils_read_timestamp_counter();

    if (validating)
        kernel_processor_sleep_begin_probe();
    for (uint32_t probe = 0u; probe < KERNEL_TICKLESS_PROBES; ++probe, length *= KERNEL_TICKLESS_PROBE_GROWTH)
    {
        uint64_t latency = 0u;
        bool timer_ended = false;
        if (tickless_probe_sleep(depth, length, &latency, &timer_ended))
            unstopped_ranks |= 1u << probe;
        else if (timer_ended)
            latencies[taught++] = latency;
    }
    const bool refused = tickless_probe_refuses(depth, unstopped_ranks);
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
 * @brief Sleeps once on @p plan with the tick stopped, armed @p latency's lead before the plan's next wake-up, then
 *        serves the plan at the wake-up; a wake-up the timer ended teaches the estimate of the depth the sleep entered,
 *        @p latency's unless the bus-master check demoted it.
 * @param plan    The session's plan.
 * @param latency The estimate of the depth the sleep asks for.
 * @return The microseconds slept.
 */
__attribute__((always_inline)) static inline uint32_t tickless_sleep_at(KernelWakePlan_t *plan,
                                                                        KernelWakeLatency_t *latency)
{
    const uint64_t before = asmutils_read_timestamp_counter();
    const uint64_t planned = kernel_wake_planner_next_wake(plan, kernel_wake_latency_lead(latency));
    if (planned <= before)
    {
        (void) kernel_wake_planner_serve(plan, before);
        return 0u;
    }

    const uint64_t longest = before + tickless_longest_sleep_cycles;
    const uint64_t armed = planned < longest ? planned : longest;
    uint64_t after = armed;
    ProcessorSleepOutcome_t outcome = {PROCESSOR_SLEEP_NONE, false, PROCESSOR_SLEEP_DEPTH_C1};
    const TicklessWake_t wake = tickless_sleep_until(armed, &after, &outcome);
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
            return probed + tickless_sleep_at(plan, &tickless_wake_latency[depth]);
        probed += tickless_probe_when_it_fits(plan, depth);
    }
    tickless_count_where_the_lead_comes_from(depth);
    return probed + tickless_sleep_at(plan, &tickless_wake_latency[depth]);
}

/**
 * @brief Probes the depth awaiting a probe, and each one the active depth falls to while it awaits one, until the
 *        active depth is validated; then the depth sleeps enter, while no wake-up taught it.
 */
static void tickless_probe_the_depth_sleeps_enter(void)
{
    for (uint32_t awaiting = kernel_processor_sleep_depth_awaiting_probe(); awaiting != PROCESSOR_SLEEP_NO_DEPTH;
         awaiting = kernel_processor_sleep_depth_awaiting_probe())
        (void) tickless_probe(awaiting);

    const uint32_t depth = kernel_processor_sleep_depth();
    if (tickless_wake_latency[depth].samples == 0u)
        (void) tickless_probe(depth);
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
    return probed + tickless_sleep_at(plan, &tickless_wake_latency[depth]);
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

KernelTicklessProbeVerdict_t kernel_tickless_judge_a_probe(uint32_t unstopped_ranks)
{
    const uint32_t longest = 1u << (KERNEL_TICKLESS_PROBES - 1u);
    const uint32_t second_longest = 1u << (KERNEL_TICKLESS_PROBES - 2u);

    if ((unstopped_ranks & longest) == 0u)
        return KERNEL_TICKLESS_PROBE_KEEPS;
    return (unstopped_ranks & second_longest) != 0u ? KERNEL_TICKLESS_PROBE_REFUSES : KERNEL_TICKLESS_PROBE_CONFIRMS;
}

bool kernel_tickless_probe_sleep_did_not_stop(bool before_its_deadline, const ProcessorSleepOutcome_t *outcome)
{
    return before_its_deadline && outcome->mode == PROCESSOR_SLEEP_PORT && !outcome->interrupted;
}

uint32_t kernel_tickless_unstopped_probe_ranks_at(uint32_t depth)
{
    return depth < PROCESSOR_SLEEP_DEPTHS ? tickless_unstopped_probe_ranks[depth] : 0u;
}

uint64_t kernel_tickless_probe_cycles(void) { return tickless_probe_cycles; }

bool kernel_tickless_probes_before_sleeping(bool probed, uint64_t until_the_wake, uint64_t probe_span)
{
    return !probed && until_the_wake >= probe_span;
}

uint32_t kernel_tickless_wakes_planned_on_a_published_lead(void) { return tickless_wakes_planned_on_a_published_lead; }

uint32_t kernel_tickless_wakes_planned_on_the_bound(void) { return tickless_wakes_planned_on_the_bound; }

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
#endif
