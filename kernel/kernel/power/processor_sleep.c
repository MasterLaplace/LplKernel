/**
 * @file processor_sleep.c
 * @brief Sleeping between buffers instead of spinning.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/power/processor_sleep.h>

#include <kernel/lib/asmutils.h>

/** CPUID leaf 1, ECX bit 3: the processor implements MONITOR and MWAIT. */
#define PROCESSOR_SLEEP_CPUID_MONITOR_BIT (1u << 3)

/** CPUID leaf 5, EBX low 16 bits: the largest line the monitor watches. */
#define PROCESSOR_SLEEP_MONITOR_LEAF 5u

/**
 * MWAIT extension bit 0: treat an unmasked interrupt as a break event.
 *
 * Set unconditionally. Without it a core that armed a monitor and then slept would
 * ignore its own timer, and the tickless deadline it just programmed would never
 * arrive — the sleep would end only when the device happened to write.
 */
#define PROCESSOR_SLEEP_BREAK_ON_INTERRUPT 1u

static bool processor_sleep_monitor_available = false;
static uint32_t processor_sleep_monitor_line = 0u;
static uint32_t processor_sleep_sleeps = 0u;
static uint32_t processor_sleep_skipped = 0u;
static uint32_t processor_sleep_halts = 0u;
static uint64_t processor_sleep_asleep = 0u;
static uint64_t processor_sleep_awake = 0u;
static uint64_t processor_sleep_last_wake = 0u;

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

    asmutils_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    if ((ecx & PROCESSOR_SLEEP_CPUID_MONITOR_BIT) == 0u)
        return;

    /* Leaf 5 reports the monitor's line size. A processor that advertises the pair
       but returns a zero line is not one to trust with a sleep that only a write can
       end, so it is treated as absent rather than guessed at. */
    asmutils_cpuid(PROCESSOR_SLEEP_MONITOR_LEAF, 0u, &eax, &ebx, &ecx, &edx);
    const uint32_t line = ebx & 0xFFFFu;
    if (line == 0u)
        return;

    processor_sleep_monitor_available = true;
    processor_sleep_monitor_line = line;
}

bool kernel_processor_sleep_has_monitor(void) { return processor_sleep_monitor_available; }

uint32_t kernel_processor_sleep_monitor_line_bytes(void) { return processor_sleep_monitor_line; }

/**
 * @brief Charges the time since the last wake-up to the awake total.
 * @param now Current timestamp.
 */
static void processor_sleep_charge_awake(uint64_t now)
{
    if (processor_sleep_last_wake != 0u && now > processor_sleep_last_wake)
        processor_sleep_awake += now - processor_sleep_last_wake;
}

ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected)
{
    if (watched == NULL)
    {
        processor_sleep_until_interrupt();
        return PROCESSOR_SLEEP_HALT;
    }

    const uint64_t before = asmutils_read_timestamp_counter();
    processor_sleep_charge_awake(before);

    if (!processor_sleep_monitor_available)
    {
        if (*watched != expected)
        {
            ++processor_sleep_skipped;
            processor_sleep_last_wake = before;
            return PROCESSOR_SLEEP_NONE;
        }
        ++processor_sleep_halts;
        ++processor_sleep_sleeps;
        asmutils_halt();
        const uint64_t after = asmutils_read_timestamp_counter();
        processor_sleep_asleep += after - before;
        processor_sleep_last_wake = after;
        return PROCESSOR_SLEEP_HALT;
    }

    asmutils_monitor((const void *) watched, 0u, 0u);

    /* Re-read AFTER arming. This is the whole point of the pair: if the device wrote
       between the caller's own check and the MONITOR, the monitor is already spent
       and MWAIT would sleep waiting for something that has already happened. */
    if (*watched != expected)
    {
        ++processor_sleep_skipped;
        processor_sleep_last_wake = before;
        return PROCESSOR_SLEEP_NONE;
    }

    ++processor_sleep_sleeps;
    asmutils_monitor_wait(0u, PROCESSOR_SLEEP_BREAK_ON_INTERRUPT);

    const uint64_t after = asmutils_read_timestamp_counter();
    processor_sleep_asleep += after - before;
    processor_sleep_last_wake = after;
    return PROCESSOR_SLEEP_MONITOR;
}

void processor_sleep_until_interrupt(void)
{
    const uint64_t before = asmutils_read_timestamp_counter();
    processor_sleep_charge_awake(before);

    ++processor_sleep_sleeps;
    ++processor_sleep_halts;
    asmutils_halt();

    const uint64_t after = asmutils_read_timestamp_counter();
    processor_sleep_asleep += after - before;
    processor_sleep_last_wake = after;
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
