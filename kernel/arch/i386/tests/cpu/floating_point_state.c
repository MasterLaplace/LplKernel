#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/exception_trigger.h>
#include <kernel/cpu/isr.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(floating_point_state, KERNEL_TEST_STAGE_PUBLISHED);

#define FLOATING_POINT_STATE_TEST_BREAKPOINT 3u /**< The vector INT3 raises, which the tests take over a while. */
#define FLOATING_POINT_STATE_TEST_INTERRUPTED                                                                          \
    0x7F80u /**< The MXCSR the interrupted code holds: every exception masked, rounding toward zero. */
#define FLOATING_POINT_STATE_TEST_HANDLER                                                                              \
    0x3F80u /**< The MXCSR a handler sets: every exception masked, rounding down. */

static volatile uint32_t floating_point_state_test_entries = 0u; /**< Entries into the test's breakpoint handler. */

/**
 * @brief The MXCSR of this processor, which FXSAVE and FXRSTOR carry with the SSE registers.
 */
__attribute__((target("sse"))) static uint32_t floating_point_state_test_read(void) { return __builtin_ia32_stmxcsr(); }

/**
 * @brief Loads @p value into the MXCSR of this processor.
 */
__attribute__((target("sse"))) static void floating_point_state_test_write(uint32_t value)
{
    __builtin_ia32_ldmxcsr(value);
}

/**
 * @brief The first entry changes the MXCSR, then takes a breakpoint inside the handler; the second returns at once.
 */
static void floating_point_state_test_fault_inside(const InterruptFrame_t *frame)
{
    (void) frame;
    if (floating_point_state_test_entries++ != 0u)
        return;
    floating_point_state_test_write(FLOATING_POINT_STATE_TEST_HANDLER);
    exception_trigger_breakpoint();
}

/**
 * @brief Sends a TLB flush to every other processor and waits until each has taken it, inside the handler.
 */
static void floating_point_state_test_others_interrupted(const InterruptFrame_t *frame)
{
    (void) frame;
    ++floating_point_state_test_entries;
    advanced_pic_ipi_broadcast_tlb_flush();
}

/**
 * @brief Takes a breakpoint, handled by @p handler, while the MXCSR holds FLOATING_POINT_STATE_TEST_INTERRUPTED.
 *
 * @return The MXCSR once the breakpoint returned; the MXCSR and the breakpoint's handler are restored after.
 */
static uint32_t floating_point_state_test_interrupt(isr_handler_t handler)
{
    const isr_handler_t kept = interrupt_service_routine_get_handler(FLOATING_POINT_STATE_TEST_BREAKPOINT);
    const uint32_t before = floating_point_state_test_read();
    uint32_t after = 0u;

    floating_point_state_test_entries = 0u;
    interrupt_service_routine_register_handler(FLOATING_POINT_STATE_TEST_BREAKPOINT, handler);
    floating_point_state_test_write(FLOATING_POINT_STATE_TEST_INTERRUPTED);
    exception_trigger_breakpoint();
    after = floating_point_state_test_read();
    floating_point_state_test_write(before);
    interrupt_service_routine_register_handler(FLOATING_POINT_STATE_TEST_BREAKPOINT, kept);
    return after;
}

/**
 * @brief A fault taken inside a handler, which saves the state that handler changed, leaves the state of the code the
 *        handler interrupted intact once both return.
 */
KERNEL_TEST(a_fault_inside_a_handler_returns_the_interrupted_state_intact)
{
    const uint32_t after = floating_point_state_test_interrupt(floating_point_state_test_fault_inside);

    kernel_test_check(test, floating_point_state_test_entries == 2u, "the handler ran, then the fault inside it");
    kernel_test_check(test, after == FLOATING_POINT_STATE_TEST_INTERRUPTED,
                      "the interrupted code gets its MXCSR back, not the handler's");
    kernel_test_measure_hexadecimal(test, "mxcsr_after", after);
}

/**
 * @brief Other processors taking an interrupt while this one is inside a handler leave this one's interrupted state
 *        intact: each saved its own.
 */
KERNEL_TEST(other_processors_interrupted_meanwhile_return_the_interrupted_state_intact)
{
    const uint32_t online = cpu_topology_get_online_cpu_count();
    const uint32_t broadcasts = advanced_pic_ipi_get_tlb_shootdown_broadcast_count();
    const uint32_t timeouts = advanced_pic_ipi_get_tlb_shootdown_timeout_count();

    if (online < 2u || !advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "no other processor online: boot with -smp 2");
        return;
    }

    const uint32_t after = floating_point_state_test_interrupt(floating_point_state_test_others_interrupted);

    kernel_test_check(test, floating_point_state_test_entries == 1u, "the handler ran once");
    kernel_test_check(test,
                      advanced_pic_ipi_get_tlb_shootdown_broadcast_count() == broadcasts + 1u &&
                          advanced_pic_ipi_get_tlb_shootdown_timeout_count() == timeouts,
                      "every other processor took the flush and answered it");
    kernel_test_check(test, after == FLOATING_POINT_STATE_TEST_INTERRUPTED,
                      "the interrupted code gets its MXCSR back, not another processor's");
    kernel_test_measure(test, "online", online);
    kernel_test_measure_hexadecimal(test, "mxcsr_after", after);
}
