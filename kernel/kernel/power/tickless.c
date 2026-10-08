#include <kernel/power/tickless.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_sleep.h>

static bool tickless_permitted = false;
static uint32_t tickless_nominal_hz = 0u;
static uint32_t tickless_ticks_avoided = 0u;
static uint32_t tickless_microseconds_short_of_a_tick = 0u;
static uint32_t tickless_early_wakes = 0u;
static uint64_t tickless_slept_microseconds = 0u;

/**
 * @brief How long a one-shot sleep really lasted, asked of the hardware.
 *
 * @details Step five of the sequence: the deadline is not assumed to have been met. The
 *          count that remains says how much of the delay was left when something else
 *          woke the core, and such an early wake is counted.
 *
 * @param requested Microseconds the timer was armed for.
 * @param armed     Count read right after arming.
 * @param remaining Count read on waking.
 * @param timer_hz  Calibrated timer frequency; never zero here.
 * @return Microseconds actually spent.
 */
static uint32_t tickless_elapsed_microseconds(uint32_t requested, uint32_t armed, uint32_t remaining, uint32_t timer_hz)
{
    if (remaining == 0u || remaining > armed || armed == 0u)
        return requested;

    ++tickless_early_wakes;
    const uint64_t consumed = (uint64_t) (armed - remaining);
    return (uint32_t) ((consumed * 1000000u) / (uint64_t) timer_hz);
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
    const uint32_t tick_microseconds = 1000000u / tickless_nominal_hz;
    const uint64_t carried = (uint64_t) tickless_microseconds_short_of_a_tick + (uint64_t) slept;
    const uint32_t ticks = (uint32_t) (carried / tick_microseconds);

    tickless_microseconds_short_of_a_tick = (uint32_t) (carried % tick_microseconds);
    tickless_ticks_avoided += ticks;
    interrupt_request_advance_tick_count(ticks);
}

bool kernel_tickless_enable(bool no_world_instantiated)
{
    if (!no_world_instantiated || tickless_permitted)
        return false;

    const uint32_t tick_hz = interrupt_request_get_timer_frequency_hz();
    if (tick_hz == 0u || tick_hz > 1000000u || advanced_pic_timer_backend_get_calibrated_timer_frequency_hz() == 0u)
        return false;

    tickless_nominal_hz = tick_hz;
    tickless_microseconds_short_of_a_tick = 0u;
    interrupt_request_suspend_periodic_tick();
    tickless_permitted = true;
    return true;
}

void kernel_tickless_disable(void)
{
    if (!tickless_permitted)
        return;

    tickless_permitted = false;
    interrupt_request_resume_periodic_tick();
}

bool kernel_tickless_enabled(void) { return tickless_permitted; }

uint32_t kernel_tickless_sleep(uint32_t microseconds)
{
    if (microseconds > KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS)
        microseconds = KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS;

    if (!tickless_permitted)
    {
        processor_sleep_until_interrupt();
        return 0u;
    }

    const uint32_t timer_hz = advanced_pic_timer_backend_get_calibrated_timer_frequency_hz();
    if (timer_hz == 0u || !advanced_pic_timer_backend_arm_one_shot(microseconds))
    {
        processor_sleep_until_interrupt();
        return 0u;
    }

    const uint32_t armed = advanced_pic_timer_backend_read_current_count();
    processor_sleep_until_interrupt();
    const uint32_t remaining = advanced_pic_timer_backend_read_current_count();

    advanced_pic_timer_backend_disable();

    const uint32_t elapsed = tickless_elapsed_microseconds(microseconds, armed, remaining, timer_hz);
    tickless_slept_microseconds += elapsed;
    tickless_count_ticks_avoided(elapsed);
    return elapsed;
}

uint32_t kernel_tickless_ticks_avoided(void) { return tickless_ticks_avoided; }

uint32_t kernel_tickless_early_wakes(void) { return tickless_early_wakes; }

uint64_t kernel_tickless_slept_microseconds(void) { return tickless_slept_microseconds; }

uint32_t kernel_tickless_nominal_frequency_hz(void) { return tickless_nominal_hz; }
