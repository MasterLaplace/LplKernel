#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(local_timer, KERNEL_TEST_STAGE_BOOTED);

/** CPUID.01H:ECX[24], read here apart from the backend: the processor offers TSC-deadline mode. */
#define LOCAL_TIMER_TEST_CPUID_TSC_DEADLINE (1u << 24u)

/** IA32_TSC_DEADLINE (Intel SDM vol. 3A, §13.5.4.1); read only where CPUID offers it, since it faults elsewhere. */
#define LOCAL_TIMER_TEST_TSC_DEADLINE_MSR 0x000006E0u

/** @name The LVT timer entry's fields (Intel SDM vol. 3A, Figure 13-8)
 *  @{ */
#define LOCAL_TIMER_TEST_LVT_VECTOR_MASK   0xFFu       /**< Bits 7:0. */
#define LOCAL_TIMER_TEST_LVT_MASKED        (1u << 16u) /**< Bit 16. */
#define LOCAL_TIMER_TEST_LVT_MODE_SHIFT    17u         /**< Bits 18:17. */
#define LOCAL_TIMER_TEST_LVT_MODE_MASK     3u          /**< Their width. */
#define LOCAL_TIMER_TEST_MODE_ONE_SHOT     0u          /**< 00b. */
#define LOCAL_TIMER_TEST_MODE_TSC_DEADLINE 2u          /**< 10b. */
#define LOCAL_TIMER_TEST_VECTOR            32u         /**< The tick's vector. */
/** @} */

/** Sessions armed in a row, each with the tick run again between them: the entry must be written again each time. */
#define LOCAL_TIMER_TEST_SESSIONS 2u

/** Armings timed for their cost, an odd count so that the median is one of them. */
#define LOCAL_TIMER_TEST_ARMINGS 15u

/** Cycles a deadline already passed may take to fire before the test gives up, rather than hang the boot. */
#define LOCAL_TIMER_TEST_FIRE_BUDGET_CYCLES 100000000ull

/** Cycles a wait for the tick's return spins before it gives up. */
#define LOCAL_TIMER_TEST_TICK_BUDGET_CYCLES 8000000000ull

/**
 * @struct LocalTimerTestArming_t
 * @brief What the timer's registers held once a far deadline was armed, and once it was disarmed.
 */
typedef struct {
    bool armed;              /**< What arming returned. */
    uint32_t entry;          /**< The LVT timer entry once armed. */
    uint32_t count;          /**< The current count once armed. */
    uint64_t deadline;       /**< The deadline armed. */
    uint64_t deadline_held;  /**< IA32_TSC_DEADLINE once armed; 0 where CPUID does not offer it. */
    bool disarmed;           /**< What disarming returned. */
    uint32_t count_after;    /**< The current count once disarmed. */
    uint64_t deadline_after; /**< IA32_TSC_DEADLINE once disarmed; 0 where CPUID does not offer it. */
} LocalTimerTestArming_t;

/**
 * @brief Whether CPUID offers TSC-deadline mode.
 * @return true when it does.
 */
static bool local_timer_test_offers_deadlines(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    return (ecx & LOCAL_TIMER_TEST_CPUID_TSC_DEADLINE) != 0u;
}

/**
 * @brief Whether the tick can be stopped on this boot: it needs the local timer calibrated.
 * @param test The test, skipped when it cannot.
 * @return true when it can.
 */
static bool local_timer_test_can_stop_the_tick(KernelTest_t *test)
{
    if (advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz() != 0u &&
        interrupt_request_is_timer_owner_apic())
        return true;
    kernel_test_skip(test, "the local timer does not own a calibrated tick, so it cannot be taken from it");
    return false;
}

/**
 * @brief Waits until the periodic tick has fired twice, or gives up.
 * @return true when it did.
 */
static bool local_timer_test_tick_runs(void)
{
    const uint32_t first_tick = interrupt_request_get_tick_count();
    const uint64_t first_cycle = asmutils_read_timestamp_counter();

    while ((uint32_t) (interrupt_request_get_tick_count() - first_tick) < 2u &&
           asmutils_read_timestamp_counter() - first_cycle < LOCAL_TIMER_TEST_TICK_BUDGET_CYCLES)
        asmutils_pause();
    return interrupt_request_get_tick_count() - first_tick >= 2u;
}

/**
 * @brief Arms a deadline a second away with the tick stopped, reads the registers, disarms it, reads them again.
 * @param offered Whether CPUID offers TSC-deadline mode, so that its register may be read.
 * @param out     Receives what the registers held.
 */
static void local_timer_test_arm_far_and_disarm(bool offered, LocalTimerTestArming_t *out)
{
    out->deadline =
        asmutils_read_timestamp_counter() + advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();
    out->armed = advanced_pic_timer_backend_arm_at_cycle(out->deadline) != 0u;
    out->entry = apic_read(LAPIC_REG_LVT_TIMER);
    out->count = apic_read(LAPIC_REG_TIMER_CUR);
    out->deadline_held = offered ? asmutils_read_model_specific_register(LOCAL_TIMER_TEST_TSC_DEADLINE_MSR) : 0u;
    out->disarmed = advanced_pic_timer_backend_disarm(asmutils_read_timestamp_counter()) != 0u;
    out->count_after = apic_read(LAPIC_REG_TIMER_CUR);
    out->deadline_after = offered ? asmutils_read_model_specific_register(LOCAL_TIMER_TEST_TSC_DEADLINE_MSR) : 0u;
}

/**
 * @brief Whether an arming went as the mode CPUID offers has it: in TSC-deadline mode the register holds the deadline
 *        and the count reads 0, as only that mode has it (§13.5.4.1); in count mode the count runs.
 * @param offered Whether CPUID offers TSC-deadline mode.
 * @param arming  What the registers held.
 * @return true when it went so.
 */
static bool local_timer_test_armed_in_its_mode(bool offered, const LocalTimerTestArming_t *arming)
{
    const uint32_t mode = (arming->entry >> LOCAL_TIMER_TEST_LVT_MODE_SHIFT) & LOCAL_TIMER_TEST_LVT_MODE_MASK;
    const bool delivered = (arming->entry & LOCAL_TIMER_TEST_LVT_MASKED) == 0u &&
                           (arming->entry & LOCAL_TIMER_TEST_LVT_VECTOR_MASK) == LOCAL_TIMER_TEST_VECTOR;

    if (offered)
        return arming->armed && delivered && mode == LOCAL_TIMER_TEST_MODE_TSC_DEADLINE &&
               arming->deadline_held == arming->deadline && arming->count == 0u;
    return arming->armed && delivered && mode == LOCAL_TIMER_TEST_MODE_ONE_SHOT && arming->count != 0u;
}

/**
 * @brief Whether a disarming said the timer had not fired and left nothing armed.
 * @param offered Whether CPUID offers TSC-deadline mode.
 * @param arming  What the registers held.
 * @return true when it did.
 */
static bool local_timer_test_disarmed_unfired(bool offered, const LocalTimerTestArming_t *arming)
{
    return arming->disarmed && (offered ? arming->deadline_after == 0u : arming->count_after == 0u);
}

/**
 * @brief The cycles one arming and one disarming of a far deadline cost, the median of LOCAL_TIMER_TEST_ARMINGS.
 * @return The median.
 */
static uint32_t local_timer_test_median_arming_cycles(void)
{
    uint32_t cycles[LOCAL_TIMER_TEST_ARMINGS];

    for (uint32_t index = 0u; index < LOCAL_TIMER_TEST_ARMINGS; ++index)
    {
        const uint64_t before = asmutils_read_timestamp_counter();
        (void) advanced_pic_timer_backend_arm_at_cycle(
            before + advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz());
        (void) advanced_pic_timer_backend_disarm(asmutils_read_timestamp_counter());
        const uint64_t spent = asmutils_read_timestamp_counter() - before;
        cycles[index] = spent > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) spent;
    }
    for (uint32_t index = 1u; index < LOCAL_TIMER_TEST_ARMINGS; ++index)
        for (uint32_t slot = index; slot > 0u && cycles[slot - 1u] > cycles[slot]; --slot)
        {
            const uint32_t swapped = cycles[slot];
            cycles[slot] = cycles[slot - 1u];
            cycles[slot - 1u] = swapped;
        }
    return cycles[LOCAL_TIMER_TEST_ARMINGS / 2u];
}

/**
 * @brief The one-shot arms a deadline on the timestamp counter where CPUID offers TSC-deadline mode, and counts down
 *        elsewhere; disarmed before it fires, it says so and leaves nothing armed, session after session.
 */
KERNEL_TEST(the_one_shot_arms_a_tsc_deadline_where_the_processor_offers_one)
{
    if (!local_timer_test_can_stop_the_tick(test))
        return;

    const bool offered = local_timer_test_offers_deadlines();
    bool armed_in_its_mode = true;
    bool disarmed_unfired = true;
    bool tick_runs_again = true;
    uint32_t median = 0u;

    for (uint32_t session = 0u; session < LOCAL_TIMER_TEST_SESSIONS; ++session)
    {
        LocalTimerTestArming_t arming;

        interrupt_request_suspend_periodic_tick();
        local_timer_test_arm_far_and_disarm(offered, &arming);
        if (session == 0u)
            median = local_timer_test_median_arming_cycles();
        interrupt_request_resume_periodic_tick();

        armed_in_its_mode = armed_in_its_mode && local_timer_test_armed_in_its_mode(offered, &arming);
        disarmed_unfired = disarmed_unfired && local_timer_test_disarmed_unfired(offered, &arming);
        tick_runs_again = tick_runs_again && local_timer_test_tick_runs();
    }

    kernel_test_check(test, (advanced_pic_timer_backend_arms_timestamp_counter_deadlines() != 0u) == offered,
                      "the backend arms deadlines on the counter exactly when CPUID offers them");
    kernel_test_check(test, armed_in_its_mode,
                      offered ? "each session arms IA32_TSC_DEADLINE with the cycle, its count reading 0" :
                                "each session arms a count, in one-shot mode");
    kernel_test_check(test, disarmed_unfired, "disarmed before it fired, it says so and nothing stays armed");
    kernel_test_check(test, tick_runs_again, "and the tick runs again after each session");
    kernel_test_measure(test, "tsc_deadline", offered ? 1u : 0u);
    kernel_test_measure(test, "tsc_invariant", advanced_pic_timer_backend_has_invariant_timestamp_counter());
    kernel_test_measure(test, "arm_and_disarm_cycles", median);
}

/**
 * @brief A deadline already passed fires at once, in either mode, and disarming it afterwards says it fired.
 */
KERNEL_TEST(a_deadline_already_passed_fires_at_once)
{
    if (!local_timer_test_can_stop_the_tick(test))
        return;

    const bool offered = local_timer_test_offers_deadlines();

    interrupt_request_suspend_periodic_tick();
    const uint64_t armed_at = asmutils_read_timestamp_counter();
    const bool armed = advanced_pic_timer_backend_arm_at_cycle(armed_at - 1u) != 0u;
    bool fired = false;
    uint64_t now = armed_at;
    while (!fired && now - armed_at < LOCAL_TIMER_TEST_FIRE_BUDGET_CYCLES)
    {
        fired = offered ? asmutils_read_model_specific_register(LOCAL_TIMER_TEST_TSC_DEADLINE_MSR) == 0u :
                          apic_read(LAPIC_REG_TIMER_CUR) == 0u;
        now = asmutils_read_timestamp_counter();
    }
    const bool disarmed = advanced_pic_timer_backend_disarm(now) != 0u;
    interrupt_request_resume_periodic_tick();

    kernel_test_check(test, armed && fired,
                      offered ? "the deadline register clears itself at once" : "the count reaches zero at once");
    kernel_test_check(test, !disarmed, "and disarming afterwards says it fired");
    kernel_test_check(test, local_timer_test_tick_runs(), "and the tick runs again");
    kernel_test_measure(test, "fire_cycles", (uint32_t) (now - armed_at));
}

/**
 * @brief A second arming in a session writes the deadline alone, not the timer's entry again: an entry masked behind
 *        the backend's back stays masked.
 */
KERNEL_TEST(a_second_arming_in_a_session_writes_only_its_deadline)
{
    if (!local_timer_test_can_stop_the_tick(test))
        return;

    const uint64_t far = advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz();

    interrupt_request_suspend_periodic_tick();
    const bool first = advanced_pic_timer_backend_arm_at_cycle(asmutils_read_timestamp_counter() + far) != 0u;
    const uint32_t entry = apic_read(LAPIC_REG_LVT_TIMER);
    apic_write(LAPIC_REG_LVT_TIMER, entry | LOCAL_TIMER_TEST_LVT_MASKED);
    const bool second = advanced_pic_timer_backend_arm_at_cycle(asmutils_read_timestamp_counter() + far) != 0u;
    const uint32_t entry_after = apic_read(LAPIC_REG_LVT_TIMER);
    (void) advanced_pic_timer_backend_disarm(asmutils_read_timestamp_counter());
    interrupt_request_resume_periodic_tick();

    kernel_test_check(test, first && second, "both armings take");
    kernel_test_check(test, entry_after == (entry | LOCAL_TIMER_TEST_LVT_MASKED),
                      "the second writes no entry: the one masked behind its back is still there");
    kernel_test_check(test, local_timer_test_tick_runs(), "and the tick, given back, runs");
}

/**
 * @brief Disarming with nothing armed says the timer had fired, and leaves the periodic tick running.
 */
KERNEL_TEST(disarming_with_nothing_armed_leaves_the_tick_running)
{
    if (!local_timer_test_can_stop_the_tick(test))
        return;

    const bool disarmed = advanced_pic_timer_backend_disarm(asmutils_read_timestamp_counter()) != 0u;

    kernel_test_check(test, !disarmed, "nothing was armed, so nothing is disarmed");
    kernel_test_check(test, local_timer_test_tick_runs(), "and the tick runs on");
}

/**
 * @brief An arming after the timer was stopped writes the timer's entry again: the stop left it masked, and the
 *        deadline arms in its mode all the same.
 */
KERNEL_TEST(an_arming_after_a_stop_writes_the_entry_again)
{
    if (!local_timer_test_can_stop_the_tick(test))
        return;

    const bool offered = local_timer_test_offers_deadlines();
    LocalTimerTestArming_t first;
    LocalTimerTestArming_t again;

    interrupt_request_suspend_periodic_tick();
    local_timer_test_arm_far_and_disarm(offered, &first);
    advanced_pic_timer_backend_disable();
    local_timer_test_arm_far_and_disarm(offered, &again);
    interrupt_request_resume_periodic_tick();

    kernel_test_check(test,
                      local_timer_test_armed_in_its_mode(offered, &first) &&
                          local_timer_test_armed_in_its_mode(offered, &again),
                      "the arming after the stop is in its mode, as the one before it");
    kernel_test_check(test, local_timer_test_tick_runs(), "and the tick, given back, runs");
}
