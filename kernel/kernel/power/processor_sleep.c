#include <kernel/power/processor_sleep.h>

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
 * it costs nothing on these paths: MWAIT already exits on an UNMASKED external interrupt,
 * and they all run with EFLAGS.IF set.
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

void kernel_processor_sleep_initialize(void)
{
    uint32_t eax = 0u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;

    processor_sleep_monitor_available = false;
    processor_sleep_monitor_line = 0u;
    processor_sleep_sleeps = 0u;
    processor_sleep_skipped = 0u;
    processor_sleep_halts = 0u;
    processor_sleep_asleep = 0u;
    processor_sleep_awake = 0u;
    processor_sleep_last_wake = 0u;
    processor_sleep_available_hints = 0u;
    processor_sleep_hint = PROCESSOR_SLEEP_HINT_C1;
    processor_sleep_clamped = 0u;
    processor_sleep_interrupt_break = false;
    kernel_wakeup_accounting_reset();

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    if ((ecx & PROCESSOR_SLEEP_CPUID_MONITOR_BIT) == 0u)
        return;

    asmutils_cpuid(PROCESSOR_SLEEP_MONITOR_LEAF, 0u, &eax, &ebx, &ecx, &edx);
    const uint32_t line = ebx & 0xFFFFu;
    if (line == 0u)
        return;

    processor_sleep_monitor_available = true;
    processor_sleep_monitor_line = line;
    processor_sleep_interrupt_break = (ecx & PROCESSOR_SLEEP_CPUID_INTERRUPT_BREAK_BIT) != 0u;
    processor_sleep_available_hints = kernel_processor_sleep_enumerated_hints(edx);
}

bool kernel_processor_sleep_has_monitor(void) { return processor_sleep_monitor_available; }

uint32_t kernel_processor_sleep_monitor_line_bytes(void) { return processor_sleep_monitor_line; }

ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected)
{
    if (watched == NULL)
    {
        processor_sleep_until_interrupt();
        return PROCESSOR_SLEEP_HALT;
    }

    const uint64_t before = asmutils_read_timestamp_counter();
    processor_sleep_charge_awake(before);

    if (processor_sleep_monitor_available)
        asmutils_monitor((const void *) watched, 0u, 0u);

    if (*watched != expected)
    {
        ++processor_sleep_skipped;
        processor_sleep_last_wake = before;
        return PROCESSOR_SLEEP_NONE;
    }

    ++processor_sleep_sleeps;
    kernel_wakeup_accounting_arm();

    if (!processor_sleep_monitor_available)
    {
        ++processor_sleep_halts;
        asmutils_halt();
        kernel_wakeup_accounting_close_unattributed();
        processor_sleep_charge_asleep_since(before);
        return PROCESSOR_SLEEP_HALT;
    }

    asmutils_monitor_wait(processor_sleep_hint,
                          processor_sleep_interrupt_break ? PROCESSOR_SLEEP_BREAK_ON_INTERRUPT : 0u);
    kernel_wakeup_accounting_close_monitor_write();
    processor_sleep_charge_asleep_since(before);
    return PROCESSOR_SLEEP_MONITOR;
}

void processor_sleep_until_interrupt(void)
{
    const uint64_t before = asmutils_read_timestamp_counter();
    processor_sleep_charge_awake(before);

    ++processor_sleep_sleeps;
    kernel_wakeup_accounting_arm();

    if (processor_sleep_monitor_available)
    {
        asmutils_monitor((const void *) &processor_sleep_idle_line, 0u, 0u);
        asmutils_monitor_wait(processor_sleep_hint,
                              processor_sleep_interrupt_break ? PROCESSOR_SLEEP_BREAK_ON_INTERRUPT : 0u);
    }
    else
    {
        ++processor_sleep_halts;
        asmutils_halt();
    }

    kernel_wakeup_accounting_close_unattributed();
    processor_sleep_charge_asleep_since(before);
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

uint32_t kernel_processor_sleep_clamped_count(void) { return processor_sleep_clamped; }
