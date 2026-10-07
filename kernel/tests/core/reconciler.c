#include <kernel/core/reconciler.h>
#include <kernel/cpu/irq.h>
#include <kernel/memory/backpressure.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(reconciler, KERNEL_TEST_STAGE_BOOTED);

/** Passes the test drives by hand, so the count is a fact about the test, not about the boot. */
#define RECONCILER_TEST_PASS_COUNT 16u

/** Ticks a session forced to spin stays awake, against the one or two it sleeps. */
#define RECONCILER_TEST_SPIN_TICKS 16u

/**
 * @brief Spins, boundedly, until the periodic tick advances.
 *
 * @details The bound is far above one tick at 100 Hz even on a fast processor, and is only reached
 *          when the tick has stopped.
 *
 * @return true when a tick was seen, which means an interrupt can end a sleep.
 */
static bool reconciler_test_wait_for_a_tick(void)
{
    const uint32_t ticks_before = interrupt_request_get_tick_count();

    for (uint32_t spin = 0u; spin < 100000000u; ++spin)
    {
        if (interrupt_request_get_tick_count() != ticks_before)
            return true;
    }
    return false;
}

/**
 * @brief Declares one more read-only page than the kernel has, and reports whether a pass notices.
 *
 * @return true when exactly the read-only page invariant drifted.
 */
static bool reconciler_test_detects_an_impossible_contract(const KernelReconcilerDeclaration_t *truthful)
{
    KernelReconcilerDeclaration_t impossible = *truthful;

    impossible.read_only_page_count += 1u;
    kernel_reconciler_declare(&impossible);
    return kernel_reconciler_check() == 1u &&
           kernel_reconciler_get_drift_mask() == (1u << (uint32_t) KERNEL_RECONCILER_INVARIANT_READ_ONLY_PAGE_COUNT);
}

/**
 * @brief Runs a power-floor session that spins instead of sleeping, and reports whether a pass notices.
 *
 * @details The session sleeps, stays awake for RECONCILER_TEST_SPIN_TICKS ticks, then sleeps again:
 *          a loop that stopped sleeping halfway. A new session is opened afterwards, so the rest of
 *          the boot is judged on its own.
 *
 * @param truthful The contract in force, which bounds the duty cycle.
 * @param duty     Receives the duty cycle the session reached, in per mille.
 * @return true when exactly the duty-cycle invariant drifted.
 */
static bool reconciler_test_detects_a_power_floor_that_spins(const KernelReconcilerDeclaration_t *truthful,
                                                             uint32_t *duty)
{
    bool spun = true;

    kernel_reconciler_declare(truthful);
    kernel_processor_sleep_initialize();
    processor_sleep_until_interrupt();
    for (uint32_t tick = 0u; spun && tick < RECONCILER_TEST_SPIN_TICKS; ++tick)
        spun = reconciler_test_wait_for_a_tick();
    processor_sleep_until_interrupt();
    kernel_processor_sleep_close_session();
    *duty = kernel_processor_sleep_published_duty_cycle_permille();

    const uint32_t detected = kernel_reconciler_check();

    kernel_processor_sleep_initialize();
    return spun && detected == 1u &&
           kernel_reconciler_get_drift_mask() == (1u << (uint32_t) KERNEL_RECONCILER_INVARIANT_DUTY_CYCLE);
}

/**
 * @brief The declared contract holds, a pass notices a contract the kernel cannot meet and a power
 *        floor that spins, and the real contract is back in force afterwards.
 *
 * @details A reconciler that checks nothing reports no drift too: the two broken contracts are what
 *          tell them apart.
 */
KERNEL_TEST(contract_holds_and_drift_is_noticed)
{
    const KernelReconcilerDeclaration_t truthful = *kernel_reconciler_get_declaration();
    const uint32_t passes_before = kernel_reconciler_get_pass_count();
    uint32_t drifts = 0u;
    uint32_t spin_duty = KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED;

    kernel_test_check(test, kernel_reconciler_is_declared(), "the kernel declared its contract");
    for (uint32_t pass = 0u; pass < RECONCILER_TEST_PASS_COUNT; ++pass)
        drifts += kernel_reconciler_check();
    kernel_test_check(test, kernel_reconciler_get_pass_count() == passes_before + RECONCILER_TEST_PASS_COUNT,
                      "every pass is counted");
    kernel_test_check(
        test, drifts == 0u && kernel_reconciler_get_drift_count() == 0u && kernel_reconciler_get_drift_mask() == 0u,
        "the declared contract holds on every pass");
    kernel_test_check(test, reconciler_test_detects_an_impossible_contract(&truthful),
                      "a pass notices a contract the kernel cannot meet");
    kernel_test_check(test, reconciler_test_detects_a_power_floor_that_spins(&truthful, &spin_duty),
                      "a pass notices a power floor that spins");
    kernel_reconciler_declare(&truthful);
    kernel_test_check(test, kernel_reconciler_check() == 0u, "the real contract holds again once it is restored");
    kernel_test_measure(test, "spin_duty", spin_duty);
    kernel_test_measure(test, "queues", kernel_backpressure_get_queue_count());
    kernel_test_measure(test, "corrupting_drops", kernel_backpressure_get_intolerant_drop_count());
}
