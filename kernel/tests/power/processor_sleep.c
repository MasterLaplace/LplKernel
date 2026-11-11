#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/irq.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/wakeup_accounting.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(processor_sleep, KERNEL_TEST_STAGE_BOOTED);

/** Sleeps entered with a self-interrupt already pending, interrupts masked. */
#define PROCESSOR_SLEEP_TEST_TRIALS 16u

/** @name The encodings the sleeps rely on (Intel SDM vol. 2)
 *  @{ */
#define PROCESSOR_SLEEP_TEST_STI           0xFBu /**< STI. */
#define PROCESSOR_SLEEP_TEST_HLT           0xF4u /**< HLT. */
#define PROCESSOR_SLEEP_TEST_ROUTINE_BYTES 16u   /**< How far into a routine its STI is looked for. */
/** @} */

/** EFLAGS.IF, bit 9: maskable interrupts are taken. */
#define PROCESSOR_SLEEP_TEST_INTERRUPT_FLAG (1u << 9u)

static volatile uint32_t processor_sleep_test_runs = 0u;                /**< Runs of the test's interrupt handler. */
static volatile uint32_t *volatile processor_sleep_test_written = NULL; /**< The word it increments, when set. */

/**
 * @brief The test's device vector: counts its runs, and increments the word under watch, if any.
 * @param context Unused.
 */
static void processor_sleep_test_handler(void *context)
{
    (void) context;
    ++processor_sleep_test_runs;
    if (processor_sleep_test_written != NULL)
        ++*processor_sleep_test_written;
}

/**
 * @brief Whether interrupts are masked, read without changing them.
 * @return true when EFLAGS.IF is clear.
 */
static bool processor_sleep_test_interrupts_masked(void)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();

    asmutils_restore_flags(flags);
    return (flags & PROCESSOR_SLEEP_TEST_INTERRUPT_FLAG) == 0u;
}

/**
 * @brief The median of @p count values, which it sorts in place.
 * @param values The values; @p count is odd or even, the upper middle is taken.
 * @param count  How many.
 * @return The median.
 */
static uint32_t processor_sleep_test_median(uint32_t *values, uint32_t count)
{
    for (uint32_t index = 1u; index < count; ++index)
        for (uint32_t slot = index; slot > 0u && values[slot - 1u] > values[slot]; --slot)
        {
            const uint32_t swapped = values[slot];
            values[slot] = values[slot - 1u];
            values[slot - 1u] = swapped;
        }
    return values[count / 2u];
}

/**
 * @brief A quarter of the tick's period in cycles of the timestamp counter, or 0 when the counter's rate is unknown.
 * @return The cycles.
 */
static uint64_t processor_sleep_test_quarter_tick_cycles(void)
{
    const uint32_t tick_hz = interrupt_request_get_timer_frequency_hz();

    return tick_hz != 0u ? advanced_pic_timer_backend_get_calibrated_timestamp_counter_frequency_hz() / (4u * tick_hz) :
                           0u;
}

/**
 * @struct ProcessorSleepTestRaise_t
 * @brief What the test's condition sends, and whether it could.
 */
typedef struct {
    uint8_t vector; /**< The device vector it sends this processor. */
    bool sent;      /**< false once a send failed. */
} ProcessorSleepTestRaise_t;

/**
 * @brief The test's condition: sends this processor the vector while interrupts are masked, then says the wait goes
 *        on, so that the interrupt is pending when the sleep begins.
 * @param context The ProcessorSleepTestRaise_t.
 * @return false.
 */
static bool processor_sleep_test_raise_and_go_on(void *context)
{
    ProcessorSleepTestRaise_t *const raise = (ProcessorSleepTestRaise_t *) context;

    raise->sent = raise->sent && advanced_pic_ipi_send_fixed(0u, raise->vector, ADVANCED_PIC_IPI_SHORT_SELF) != 0u;
    return false;
}

/**
 * @brief The test's condition when the wait is already over.
 * @param context Unused.
 * @return true.
 */
static bool processor_sleep_test_already_over(void *context)
{
    (void) context;
    return true;
}

/**
 * @brief Gives this processor a device vector for the test's handler, and the length a sleep must stay under, or says
 *        why it cannot.
 * @param test             The test, skipped or failed when it cannot.
 * @param out_vector       Receives the vector.
 * @param out_quarter_tick Receives a quarter of the tick's period, in cycles.
 * @return true when the vector can be sent to this processor and a sleep's length judged.
 */
static bool processor_sleep_test_take_a_vector(KernelTest_t *test, uint8_t *out_vector, uint64_t *out_quarter_tick)
{
    *out_quarter_tick = processor_sleep_test_quarter_tick_cycles();
    if (*out_quarter_tick == 0u)
    {
        kernel_test_skip(test, "the counter's rate is not calibrated, so the length of a sleep cannot be judged");
        return false;
    }
    if (!advanced_pic_ipi_is_ready())
    {
        kernel_test_skip(test, "the local APIC sends no interprocessor interrupt");
        return false;
    }
    return kernel_test_check(test, device_interrupt_allocate(processor_sleep_test_handler, NULL, out_vector),
                             "a device vector is free");
}

/**
 * @brief Whether @p routine holds STI immediately followed by @p after, within its first bytes.
 * @param routine     The routine's first byte.
 * @param after       The instruction expected right after STI.
 * @param after_bytes Its length.
 * @return true when an STI is found and @p after follows it with nothing between.
 */
static bool processor_sleep_test_sti_then(const uint8_t *routine, const uint8_t *after, uint32_t after_bytes)
{
    for (uint32_t offset = 0u; offset < PROCESSOR_SLEEP_TEST_ROUTINE_BYTES; ++offset)
    {
        if (routine[offset] != PROCESSOR_SLEEP_TEST_STI)
            continue;
        for (uint32_t index = 0u; index < after_bytes; ++index)
            if (routine[offset + 1u + index] != after[index])
                return false;
        return true;
    }
    return false;
}

/**
 * @brief Spins, boundedly, until the periodic tick advances.
 *
 * @details The bound is far above one tick at 100 Hz even on a fast processor, and is only reached
 *          when the tick has stopped.
 *
 * @return true when a tick was seen, which means an interrupt can end a sleep.
 */
static bool processor_sleep_test_wait_for_a_tick(void)
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
 * @brief MWAIT hints are read out of CPUID one state deep, and a depth nothing enumerates is lowered.
 *
 * @details CPUID.05H:EDX reports C1 in bits 7:4 while MWAIT's hint 0 targets C1, so hint h is
 *          enumerated when nibble h+1 is set. What this processor enumerates is measured, not
 *          checked: QEMU advertises no MWAIT.
 */
KERNEL_TEST(hints_are_read_one_state_deep)
{
    const uint32_t clamped_before = kernel_processor_sleep_clamped_count();
    const bool deepest_granted = kernel_processor_sleep_request_hint(PROCESSOR_SLEEP_HINT_MAX);
    const uint32_t available = kernel_processor_sleep_available_hints();

    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00000000u) == 0u, "nothing enumerated, no hint");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00000020u) == 0x1u,
                      "hint 0 targets C1, not the nibble it sits in");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00002220u) == 0x7u,
                      "three enumerated states give three hints");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00000002u) == 0u, "C0 is not a sleep state");
    kernel_test_check(test, kernel_processor_sleep_enumerated_hints(0x00200020u) == 0x11u,
                      "a gap in the enumeration stays a gap");
    kernel_test_check(test, deepest_granted || kernel_processor_sleep_clamped_count() == clamped_before + 1u,
                      "a depth nothing enumerates is lowered and counted, not passed");
    kernel_test_check(test,
                      available == 0u ? kernel_processor_sleep_active_hint() == PROCESSOR_SLEEP_HINT_C1 :
                                        ((available >> kernel_processor_sleep_active_hint()) & 1u) == 1u,
                      "the active hint is one the processor enumerates");
    kernel_test_check(test,
                      kernel_processor_sleep_request_hint(PROCESSOR_SLEEP_HINT_C1) &&
                          kernel_processor_sleep_active_hint() == PROCESSOR_SLEEP_HINT_C1,
                      "C1 is always askable");
    kernel_test_measure(test, "available", available);
    kernel_test_measure(test, "interrupt_break", kernel_processor_sleep_has_interrupt_break() ? 1u : 0u);
}

/**
 * @brief The probe the ACPI initialisation reads arms nothing: the module answers after it as it did
 *        before, so every sleep keeps the instruction it had.
 *
 * @details Whether this processor implements the pair is measured, not checked: QEMU advertises no
 *          MWAIT.
 */
KERNEL_TEST(the_monitor_probe_arms_nothing)
{
    const bool armed = kernel_processor_sleep_has_monitor();
    const uint32_t line = kernel_processor_sleep_monitor_line_bytes();
    const bool implemented = kernel_processor_sleep_monitor_implemented();

    kernel_test_check(
        test, kernel_processor_sleep_has_monitor() == armed && kernel_processor_sleep_monitor_line_bytes() == line,
        "the module answers after the probe as before it");
    kernel_test_measure(test, "monitor_implemented", implemented ? 1u : 0u);
}

/**
 * @brief A wait on a word that already moved is skipped, one on a word that did not really sleeps,
 *        and whatever ended that sleep is named.
 *
 * @details The real sleep needs the tick to end it, so the tick is checked first: a test that can
 *          hang is worse than one that fails.
 */
KERNEL_TEST(sleep_until_a_write)
{
    static volatile uint32_t watched = 0u;
    const uint32_t sleeps_before = kernel_processor_sleep_count();
    const uint32_t skips_before = kernel_processor_sleep_skipped_count();

    kernel_test_check(test, keyboard_get_ring_head_address() != NULL,
                      "there is an index an interrupt handler advances");

    watched = 1u;
    kernel_test_check(test,
                      processor_sleep_until_write(&watched, 0u) == PROCESSOR_SLEEP_NONE &&
                          kernel_processor_sleep_skipped_count() == skips_before + 1u &&
                          kernel_processor_sleep_count() == sleeps_before,
                      "a wait already over is skipped, not entered");

    if (!kernel_test_check(test, processor_sleep_test_wait_for_a_tick(), "the periodic tick runs to end a sleep"))
        return;

    const uint32_t attributed_before = kernel_wakeup_accounting_get_attributed_count();
    const uint32_t entered_before = kernel_processor_sleep_count();
    const ProcessorSleepMode_t slept = processor_sleep_until_write(&watched, 1u);

    kernel_test_check(test, slept != PROCESSOR_SLEEP_NONE && kernel_processor_sleep_count() == entered_before + 1u,
                      "an unmoved word really sleeps");
    kernel_test_check(test, kernel_wakeup_accounting_get_attributed_count() == attributed_before + 1u,
                      "the sleep it entered was attributed to a source");
}

/**
 * @brief A sleep entered with interrupts masked and an interrupt already pending, raised between the caller's check and
 *        the sleep, ends on it and handles it before returning, every time, without waiting for the tick; it gives
 *        interrupts back masked, as it found them.
 *
 * @details The interrupt is the processor's own, sent with interrupts masked, so that it is pending, not handled, when
 *          the sleep begins: the window #300 names, made certain instead of rare. A halt the sleep did not enter in the
 *          same step as it enabled interrupts would handle it first, then wait for the next one, a tick away.
 */
KERNEL_TEST(an_interrupt_pending_when_the_sleep_begins_ends_it)
{
    uint8_t vector = 0u;
    uint64_t quarter_tick = 0u;
    if (!processor_sleep_test_take_a_vector(test, &vector, &quarter_tick))
        return;

    uint32_t cycles[PROCESSOR_SLEEP_TEST_TRIALS];
    bool sent = true;
    bool handled_inside = true;
    bool masked_after = true;
    const uint32_t credited_before = kernel_wakeup_accounting_get_vector_count(vector);

    processor_sleep_test_runs = 0u;
    for (uint32_t trial = 0u; trial < PROCESSOR_SLEEP_TEST_TRIALS; ++trial)
    {
        const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
        sent = sent && advanced_pic_ipi_send_fixed(0u, vector, ADVANCED_PIC_IPI_SHORT_SELF) != 0u;
        const uint32_t runs_before = processor_sleep_test_runs;
        const uint64_t before = asmutils_read_timestamp_counter();
        processor_sleep_until_interrupt();
        const uint64_t spent = asmutils_read_timestamp_counter() - before;
        handled_inside = handled_inside && processor_sleep_test_runs == runs_before + 1u;
        masked_after = masked_after && processor_sleep_test_interrupts_masked();
        asmutils_restore_flags(flags);
        cycles[trial] = spent > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) spent;
    }
    const uint32_t credited = kernel_wakeup_accounting_get_vector_count(vector) - credited_before;
    device_interrupt_release(vector);

    const uint32_t median = processor_sleep_test_median(cycles, PROCESSOR_SLEEP_TEST_TRIALS);
    kernel_test_check(test, sent, "each trial sends this processor the vector, interrupts masked");
    kernel_test_check(test, handled_inside, "each sleep ends on it and handles it before returning");
    kernel_test_check(test, credited == PROCESSOR_SLEEP_TEST_TRIALS, "each wake-up is credited to it");
    kernel_test_check(test, masked_after, "and gives interrupts back masked, as it found them");
    kernel_test_check(test, median < quarter_tick,
                      "a sleep lasts less than a quarter of a tick: none waits for the next one");
    kernel_test_measure(test, "median_cycles", median);
    kernel_test_measure(test, "monitor", kernel_processor_sleep_has_monitor() ? 1u : 0u);
}

/**
 * @brief A watched sleep entered with interrupts masked and an interrupt pending whose handler writes the word sleeps,
 *        since the word had not moved when it was read, and wakes with the write done, without waiting for the tick;
 *        it gives interrupts back masked.
 */
KERNEL_TEST(a_write_pending_when_the_watched_sleep_begins_ends_it)
{
    static volatile uint32_t word = 0u;
    uint8_t vector = 0u;
    uint64_t quarter_tick = 0u;
    if (!processor_sleep_test_take_a_vector(test, &vector, &quarter_tick))
        return;

    uint32_t cycles[PROCESSOR_SLEEP_TEST_TRIALS];
    bool sent = true;
    bool slept = true;
    bool written = true;
    bool masked_after = true;
    ProcessorSleepMode_t mode = PROCESSOR_SLEEP_NONE;

    processor_sleep_test_written = &word;
    for (uint32_t trial = 0u; trial < PROCESSOR_SLEEP_TEST_TRIALS; ++trial)
    {
        const uint32_t expected = word;
        const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
        sent = sent && advanced_pic_ipi_send_fixed(0u, vector, ADVANCED_PIC_IPI_SHORT_SELF) != 0u;
        const uint64_t before = asmutils_read_timestamp_counter();
        mode = processor_sleep_until_write(&word, expected);
        const uint64_t spent = asmutils_read_timestamp_counter() - before;
        slept = slept && mode != PROCESSOR_SLEEP_NONE;
        written = written && word == expected + 1u;
        masked_after = masked_after && processor_sleep_test_interrupts_masked();
        asmutils_restore_flags(flags);
        cycles[trial] = spent > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) spent;
    }
    processor_sleep_test_written = NULL;
    device_interrupt_release(vector);

    const uint32_t median = processor_sleep_test_median(cycles, PROCESSOR_SLEEP_TEST_TRIALS);
    kernel_test_check(test, sent, "each trial sends this processor the vector, interrupts masked");
    kernel_test_check(test, slept, "the word had not moved when it was read, so each trial sleeps");
    kernel_test_check(test, written, "and wakes with the handler's write done");
    kernel_test_check(test, masked_after, "and gives interrupts back masked, as it found them");
    kernel_test_check(test, median < quarter_tick,
                      "a sleep lasts less than a quarter of a tick: none waits for the next one");
    kernel_test_measure(test, "median_cycles", median);
    kernel_test_measure(test, "mode", (uint32_t) mode);
}

/**
 * @brief A conditional sleep entered with interrupts enabled ends on an interrupt raised while its condition was read,
 *        every time, without waiting for the tick, and gives interrupts back enabled; a condition already true enters
 * no sleep.
 *
 * @details The condition itself sends the interrupt, interrupts masked, so that it is pending when the sleep begins:
 * the change a handler makes after the condition read it, made certain.
 */
KERNEL_TEST(an_interrupt_raised_while_the_condition_is_read_ends_the_sleep)
{
    ProcessorSleepTestRaise_t raise = {0u, true};
    uint64_t quarter_tick = 0u;
    if (!processor_sleep_test_take_a_vector(test, &raise.vector, &quarter_tick))
        return;

    uint32_t cycles[PROCESSOR_SLEEP_TEST_TRIALS];
    bool went_on = true;
    bool handled_inside = true;
    bool enabled_after = true;
    const uint32_t credited_before = kernel_wakeup_accounting_get_vector_count(raise.vector);

    processor_sleep_test_runs = 0u;
    for (uint32_t trial = 0u; trial < PROCESSOR_SLEEP_TEST_TRIALS; ++trial)
    {
        const uint32_t runs_before = processor_sleep_test_runs;
        const uint64_t before = asmutils_read_timestamp_counter();
        went_on = went_on && !processor_sleep_unless(processor_sleep_test_raise_and_go_on, &raise);
        const uint64_t spent = asmutils_read_timestamp_counter() - before;
        handled_inside = handled_inside && processor_sleep_test_runs == runs_before + 1u;
        enabled_after = enabled_after && !processor_sleep_test_interrupts_masked();
        cycles[trial] = spent > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) spent;
    }
    const uint32_t credited = kernel_wakeup_accounting_get_vector_count(raise.vector) - credited_before;
    device_interrupt_release(raise.vector);

    const uint32_t sleeps_before = kernel_processor_sleep_count();
    const bool over = processor_sleep_unless(processor_sleep_test_already_over, NULL);
    const uint32_t sleeps_after = kernel_processor_sleep_count();
    const bool enabled_after_over = !processor_sleep_test_interrupts_masked();

    const uint32_t median = processor_sleep_test_median(cycles, PROCESSOR_SLEEP_TEST_TRIALS);
    kernel_test_check(test, raise.sent && went_on, "each condition sends the vector, and says the wait goes on");
    kernel_test_check(test, handled_inside && credited == PROCESSOR_SLEEP_TEST_TRIALS,
                      "each sleep ends on it, handles it before returning, and is credited to it");
    kernel_test_check(test, enabled_after, "and gives interrupts back enabled, as it found them");
    kernel_test_check(test, median < quarter_tick,
                      "a sleep lasts less than a quarter of a tick: none waits for the next one");
    kernel_test_check(test, over && sleeps_after == sleeps_before && enabled_after_over,
                      "a wait already over enters no sleep, and gives interrupts back enabled");
    kernel_test_measure(test, "median_cycles", median);
}

/**
 * @brief Each sleep routine holds its STI immediately before its HLT or MWAIT: the STI's inhibition covers only the
 *        next instruction (Intel SDM vol. 3A, §7.8.1), so anything between the two would reopen the window.
 *
 * @details The MWAIT path cannot be woken here: under QEMU the kernel never takes it, since KVM hides MONITOR and TCG
 *          gives CPUID.05H no line size. The encoding is what this test can still check of it.
 */
KERNEL_TEST(each_sleep_enables_interrupts_right_before_it_halts_or_waits)
{
    static const uint8_t halt[] = {PROCESSOR_SLEEP_TEST_HLT};
    static const uint8_t monitor_wait[] = {0x0Fu, 0x01u, 0xC9u};

    kernel_test_check(test,
                      processor_sleep_test_sti_then((const uint8_t *) (uintptr_t) asmutils_enable_interrupts_and_halt,
                                                    halt, sizeof(halt)),
                      "STI is right before HLT");
    kernel_test_check(
        test,
        processor_sleep_test_sti_then((const uint8_t *) (uintptr_t) asmutils_enable_interrupts_and_monitor_wait,
                                      monitor_wait, sizeof(monitor_wait)),
        "STI is right before MWAIT");
}

/**
 * @brief Without a local timer that always runs, a sleep takes no hint past C1's, whatever CPUID enumerates; with one,
 *        it takes every enumerated hint, gaps included.
 */
KERNEL_TEST(without_an_always_running_timer_no_hint_goes_past_c1)
{
    kernel_test_check(test,
                      kernel_processor_sleep_usable_hints(0x7Fu, true) == 0x7Fu &&
                          kernel_processor_sleep_usable_hints(0x11u, true) == 0x11u,
                      "with it, every enumerated hint, gaps staying gaps");
    kernel_test_check(test, kernel_processor_sleep_usable_hints(0x7Fu, false) == 0x01u,
                      "without it, C1's alone of seven");
    kernel_test_check(test, kernel_processor_sleep_usable_hints(0x7Eu, false) == 0u,
                      "and none when C1 itself is not enumerated");
    kernel_test_check(test,
                      kernel_processor_sleep_usable_hints(0u, false) == 0u &&
                          kernel_processor_sleep_usable_hints(0u, true) == 0u,
                      "nothing enumerated, nothing usable");
}

/**
 * @brief The hints an initialisation adopts are the usable ones of those CPUID enumerates, for the local timer this
 *        processor has. Skipped where the kernel cannot use MONITOR, every QEMU model (#818), where both sides would be
 *        zero and prove nothing.
 */
KERNEL_TEST(an_initialisation_adopts_the_usable_hints)
{
    kernel_processor_sleep_initialize();
    if (!kernel_processor_sleep_has_monitor())
    {
        kernel_test_skip(test, "the kernel cannot use MONITOR on this processor, so it adopts no hint (#818)");
        return;
    }

    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;
    asmutils_cpuid(5u, 0u, &eax, &ebx, &ecx, &edx);
    const uint32_t expected = kernel_processor_sleep_usable_hints(kernel_processor_sleep_enumerated_hints(edx),
                                                                  advanced_pic_timer_backend_always_runs() != 0u);

    kernel_test_check(test, kernel_processor_sleep_available_hints() == expected,
                      "the hints adopted are the enumerated ones the timer allows");
    kernel_test_measure(test, "available_hints", kernel_processor_sleep_available_hints());
}

/**
 * @brief A hint reaches MWAIT as its target C-state less one in bits 7:4: hint 0 targets C1 as 0x00, hint 1 C2 as
 *        0x10, and the deepest, hint 6, C7 as 0x60, never a sub-state of C1.
 */
KERNEL_TEST(a_hint_reaches_mwait_as_its_c_state_in_bits_7_to_4)
{
    kernel_test_check(test, kernel_processor_sleep_mwait_hint(PROCESSOR_SLEEP_HINT_C1) == 0x00u, "C1 is 0x00");
    kernel_test_check(test, kernel_processor_sleep_mwait_hint(1u) == 0x10u, "C2 is 0x10, not C1's sub-state 1");
    kernel_test_check(test, kernel_processor_sleep_mwait_hint(PROCESSOR_SLEEP_HINT_MAX) == 0x60u, "C7 is 0x60");
}
