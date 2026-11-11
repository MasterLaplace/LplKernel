#include <kernel/power/processor_sleep.h>

#include <kernel/cpu/apic_timer.h>

#include <kernel/lib/asmutils.h>
#include <kernel/power/wakeup_accounting.h>

/** CPUID leaf 1, ECX bit 3: the processor implements MONITOR and MWAIT. */
#define PROCESSOR_SLEEP_CPUID_MONITOR_BIT (1u << 3)

/** CPUID leaf 5, EBX low 16 bits: the largest line the monitor watches. */
#define PROCESSOR_SLEEP_MONITOR_LEAF 5u

/**
 * MWAIT extension bit 0: treat interrupts as break events even when MASKED.
 *
 * Gated on CPUID.05H:ECX bit 1, which raises #GP when the bit is set without it. Dropping
 * it costs nothing on these paths: each MWAIT comes right after the STI that unmasks
 * interrupts, so a pending one ends it all the same. Linux passes the bit only to a
 * MWAIT entered masked (mwait_idle_with_hints, arch/x86/include/asm/mwait.h, v6.12).
 */
#define PROCESSOR_SLEEP_BREAK_ON_INTERRUPT 1u

/** CPUID leaf 5, ECX bit 1: MWAIT may be given the break-on-masked-interrupt extension. */
#define PROCESSOR_SLEEP_CPUID_INTERRUPT_BREAK_BIT (1u << 1)

static bool processor_sleep_monitor_available = false;
static uint32_t processor_sleep_monitor_line = 0u;
static uint32_t processor_sleep_sleeps = 0u;
static uint32_t processor_sleep_skipped = 0u;
static uint32_t processor_sleep_halts = 0u;
static uint64_t processor_sleep_asleep = 0u;
static uint64_t processor_sleep_awake = 0u;
static uint64_t processor_sleep_last_wake = 0u;
static uint32_t processor_sleep_available_hints = 0u;
static uint32_t processor_sleep_hint = PROCESSOR_SLEEP_HINT_C1;
static uint32_t processor_sleep_clamped = 0u;
static bool processor_sleep_interrupt_break = false;
static volatile uint32_t processor_sleep_published_duty = KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED;

/**
 * @brief Charges the time since the last wake-up to the awake total.
 * @param now Current timestamp.
 */
static void processor_sleep_charge_awake(uint64_t now)
{
    if (processor_sleep_last_wake != 0u && now > processor_sleep_last_wake)
        processor_sleep_awake += now - processor_sleep_last_wake;
}

/**
 * @brief Charges the time since @p before to the asleep total and marks the wake-up.
 * @param before Timestamp taken just before the processor went to sleep.
 */
static void processor_sleep_charge_asleep_since(uint64_t before)
{
    const uint64_t after = asmutils_read_timestamp_counter();
    processor_sleep_asleep += after - before;
    processor_sleep_last_wake = after;
}

/**
 * @brief Reads CPUID for the monitor/wait pair, and arms nothing.
 *
 * @param out_line Receives the line size CPUID leaf 5 gives.
 * @param out_ecx  Receives CPUID leaf 5's ECX.
 * @param out_edx  Receives CPUID leaf 5's EDX.
 * @return true when leaf 1 advertises the pair and leaf 5 gives a non-zero line size.
 */
static bool processor_sleep_read_monitor(uint32_t *out_line, uint32_t *out_ecx, uint32_t *out_edx)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    if ((ecx & PROCESSOR_SLEEP_CPUID_MONITOR_BIT) == 0u)
        return false;
    asmutils_cpuid(PROCESSOR_SLEEP_MONITOR_LEAF, 0u, &eax, &ebx, out_ecx, out_edx);
    *out_line = ebx & 0xFFFFu;
    return *out_line != 0u;
}

/**
 * A line nothing in the kernel writes. Armed as the monitor target, it turns MWAIT into
 * the same wait as HLT — only an interrupt ends it — except at the requested depth.
 */
static volatile uint32_t processor_sleep_idle_line = 0u;

/**
 * @brief The deepest enumerated hint no deeper than @p ceiling, or C1 when there is none.
 *
 * @details Walked down rather than taken from the highest set bit, because the
 *          enumeration has gaps and the caller asked for at most this much.
 *
 * @param ceiling Hint the caller asked for, which is the most it is willing to take.
 * @return The hint index.
 */
static uint32_t processor_sleep_deepest_enumerated_at_or_below(uint32_t ceiling)
{
    for (uint32_t candidate = ceiling; candidate > PROCESSOR_SLEEP_HINT_C1; --candidate)
    {
        if (((processor_sleep_available_hints >> candidate) & 1u) != 0u)
            return candidate;
    }

    return PROCESSOR_SLEEP_HINT_C1;
}

/**
 * @struct ProcessorSleepWatch_t
 * @brief The word a watched sleep waits on, and the value that means nothing new.
 */
typedef struct {
    const volatile uint32_t *watched; /**< The word. */
    uint32_t expected;                /**< Its value until something new arrives. */
} ProcessorSleepWatch_t;

/**
 * @brief Whether the watched word has moved, for processor_sleep_masked.
 * @param context The ProcessorSleepWatch_t.
 * @return true once it differs from the value expected.
 */
__attribute__((always_inline)) static inline bool processor_sleep_watched_moved(void *context)
{
    const ProcessorSleepWatch_t *const watch = (const ProcessorSleepWatch_t *) context;

    return *watch->watched != watch->expected;
}

/**
 * @brief Sleeps unless @p done says the wait is over: masks interrupts, arms the monitor on @p line, asks @p done,
 *        then enters MWAIT or HLT in the step that enables interrupts, and gives the interrupt flag back as found.
 *
 * @details The one place every sleep of this module is entered, so that the masking around the last check is written,
 *          and tested, once; inlined, so that each caller pays only for the parts it uses. A wait on the idle line that
 * returns while still armed is counted unattributed, since nothing writes that line; one on another line, a write.
 *
 * @param line    The line MONITOR watches: the idle line when only an interrupt should end the wait.
 * @param done    Asked once, after the monitor is armed; NULL never says the wait is over.
 * @param context What @p done receives.
 * @param hint    The hint index, which kernel_processor_sleep_mwait_hint encodes for MWAIT.
 * @return How the processor was put to sleep, PROCESSOR_SLEEP_NONE when @p done said the wait was over.
 */
__attribute__((always_inline)) static inline ProcessorSleepMode_t
processor_sleep_masked(const volatile void *line, ProcessorSleepCondition_t done, void *context, uint32_t hint)
{
    const uint32_t flags = asmutils_save_flags_and_disable_interrupts();
    const uint64_t before = asmutils_read_timestamp_counter();
    ProcessorSleepMode_t mode = PROCESSOR_SLEEP_NONE;

    processor_sleep_charge_awake(before);
    if (processor_sleep_monitor_available)
        asmutils_monitor((const void *) line, 0u, 0u);

    if (done != NULL && done(context))
    {
        ++processor_sleep_skipped;
        processor_sleep_last_wake = before;
    }
    else if (!processor_sleep_monitor_available)
    {
        ++processor_sleep_sleeps;
        ++processor_sleep_halts;
        kernel_wakeup_accounting_arm();
        asmutils_enable_interrupts_and_halt();
        kernel_wakeup_accounting_close_unattributed();
        processor_sleep_charge_asleep_since(before);
        mode = PROCESSOR_SLEEP_HALT;
    }
    else
    {
        ++processor_sleep_sleeps;
        kernel_wakeup_accounting_arm();
        asmutils_enable_interrupts_and_monitor_wait(
            kernel_processor_sleep_mwait_hint(hint),
            processor_sleep_interrupt_break ? PROCESSOR_SLEEP_BREAK_ON_INTERRUPT : 0u);
        if (line == &processor_sleep_idle_line)
            kernel_wakeup_accounting_close_unattributed();
        else
            kernel_wakeup_accounting_close_monitor_write();
        processor_sleep_charge_asleep_since(before);
        mode = PROCESSOR_SLEEP_MONITOR;
    }
    asmutils_restore_flags(flags);
    return mode;
}

void kernel_processor_sleep_initialize(void)
{
    uint32_t line = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    processor_sleep_monitor_available = false;
    processor_sleep_monitor_line = 0u;
    processor_sleep_available_hints = 0u;
    processor_sleep_hint = PROCESSOR_SLEEP_HINT_C1;
    processor_sleep_clamped = 0u;
    processor_sleep_interrupt_break = false;
    processor_sleep_published_duty = KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED;
    kernel_processor_sleep_begin_accounting();

    if (!processor_sleep_read_monitor(&line, &ecx, &edx))
        return;

    processor_sleep_monitor_available = true;
    processor_sleep_monitor_line = line;
    processor_sleep_interrupt_break = (ecx & PROCESSOR_SLEEP_CPUID_INTERRUPT_BREAK_BIT) != 0u;
    processor_sleep_available_hints = kernel_processor_sleep_usable_hints(
        kernel_processor_sleep_enumerated_hints(edx), advanced_pic_timer_backend_always_runs() != 0u);
}

void kernel_processor_sleep_begin_accounting(void)
{
    processor_sleep_sleeps = 0u;
    processor_sleep_skipped = 0u;
    processor_sleep_halts = 0u;
    processor_sleep_asleep = 0u;
    processor_sleep_awake = 0u;
    processor_sleep_last_wake = 0u;
    kernel_wakeup_accounting_reset();
}

bool kernel_processor_sleep_has_monitor(void) { return processor_sleep_monitor_available; }

bool kernel_processor_sleep_monitor_implemented(void)
{
    uint32_t line = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    return processor_sleep_read_monitor(&line, &ecx, &edx);
}

uint32_t kernel_processor_sleep_hints_enumerated_by_this_processor(void)
{
    uint32_t line = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    return processor_sleep_read_monitor(&line, &ecx, &edx) ? kernel_processor_sleep_enumerated_hints(edx) : 0u;
}

uint32_t kernel_processor_sleep_monitor_line_bytes(void) { return processor_sleep_monitor_line; }

ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected)
{
    if (watched == NULL)
    {
        processor_sleep_until_interrupt();
        return PROCESSOR_SLEEP_HALT;
    }

    ProcessorSleepWatch_t watch = {watched, expected};
    return processor_sleep_masked(watched, processor_sleep_watched_moved, &watch, processor_sleep_hint);
}

void processor_sleep_until_interrupt(void)
{
    (void) processor_sleep_masked(&processor_sleep_idle_line, NULL, NULL, processor_sleep_hint);
}

bool processor_sleep_unless(ProcessorSleepCondition_t done, void *context)
{
    return processor_sleep_masked(&processor_sleep_idle_line, done, context, PROCESSOR_SLEEP_HINT_C1) ==
           PROCESSOR_SLEEP_NONE;
}

uint32_t kernel_processor_sleep_count(void) { return processor_sleep_sleeps; }

uint32_t kernel_processor_sleep_skipped_count(void) { return processor_sleep_skipped; }

uint32_t kernel_processor_sleep_halt_count(void) { return processor_sleep_halts; }

uint64_t kernel_processor_sleep_asleep_cycles(void) { return processor_sleep_asleep; }

uint64_t kernel_processor_sleep_awake_cycles(void) { return processor_sleep_awake; }

uint32_t kernel_processor_sleep_duty_cycle_permille(void)
{
    const uint64_t accounted = processor_sleep_awake + processor_sleep_asleep;
    if (accounted == 0u)
        return 1000u;
    return (uint32_t) ((processor_sleep_awake * 1000u) / accounted);
}

void kernel_processor_sleep_close_session(void)
{
    const uint64_t accounted = processor_sleep_awake + processor_sleep_asleep;
    processor_sleep_published_duty =
        (accounted == 0u) ? KERNEL_PROCESSOR_SLEEP_DUTY_UNMEASURED : kernel_processor_sleep_duty_cycle_permille();
}

uint32_t kernel_processor_sleep_published_duty_cycle_permille(void) { return processor_sleep_published_duty; }

uint32_t kernel_processor_sleep_enumerated_hints(uint32_t leaf5_edx)
{
    uint32_t mask = 0u;

    for (uint32_t hint = 0u; hint <= PROCESSOR_SLEEP_HINT_MAX; ++hint)
    {
        const uint32_t nibble = (leaf5_edx >> (4u * (hint + 1u))) & 0xFu;
        if (nibble != 0u)
            mask |= (1u << hint);
    }

    return mask;
}

uint32_t kernel_processor_sleep_usable_hints(uint32_t enumerated, bool timer_always_runs)
{
    return timer_always_runs ? enumerated : enumerated & (1u << PROCESSOR_SLEEP_HINT_C1);
}

uint32_t kernel_processor_sleep_available_hints(void) { return processor_sleep_available_hints; }

bool kernel_processor_sleep_has_interrupt_break(void) { return processor_sleep_interrupt_break; }

bool kernel_processor_sleep_request_hint(uint32_t hint)
{
    if (hint > PROCESSOR_SLEEP_HINT_MAX)
        hint = PROCESSOR_SLEEP_HINT_MAX;

    if (hint == PROCESSOR_SLEEP_HINT_C1 || ((processor_sleep_available_hints >> hint) & 1u) != 0u)
    {
        processor_sleep_hint = hint;
        return true;
    }

    processor_sleep_hint = processor_sleep_deepest_enumerated_at_or_below(hint);
    ++processor_sleep_clamped;
    return false;
}

uint32_t kernel_processor_sleep_active_hint(void) { return processor_sleep_hint; }

uint32_t kernel_processor_sleep_depth(void)
{
    return processor_sleep_monitor_available ? processor_sleep_hint : PROCESSOR_SLEEP_HINT_C1;
}

uint32_t kernel_processor_sleep_clamped_count(void) { return processor_sleep_clamped; }
