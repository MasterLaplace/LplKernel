/**
 * @file tickless.c
 * @brief Stopping the periodic tick when nothing is due.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/power/tickless.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/processor_sleep.h>

/** Cadence assumed for the saving figure when nothing else says otherwise. */
#define KERNEL_TICKLESS_DEFAULT_FREQUENCY_HZ 1000u

static bool tickless_permitted = false;
static uint32_t tickless_nominal_hz = KERNEL_TICKLESS_DEFAULT_FREQUENCY_HZ;
static uint32_t tickless_ticks_avoided = 0u;
static uint32_t tickless_early_wakes = 0u;
static uint64_t tickless_slept_microseconds = 0u;

bool kernel_tickless_enable(bool no_world_instantiated, uint32_t nominal_frequency_hz)
{
    /* The one rule this file exists to enforce. A World's tick is the clock every
       parity gate is folded against; stopping it would make the simulation advance at
       a rate that depends on how idle the machine happened to be, which is not a
       performance problem but a determinism one. */
    if (!no_world_instantiated)
        return false;

    if (nominal_frequency_hz != 0u)
        tickless_nominal_hz = nominal_frequency_hz;

    advanced_pic_timer_backend_disable();
    tickless_permitted = true;
    return true;
}

void kernel_tickless_disable(uint32_t periodic_frequency_hz)
{
    tickless_permitted = false;
    if (periodic_frequency_hz == 0u)
        periodic_frequency_hz = tickless_nominal_hz;
    tickless_nominal_hz = periodic_frequency_hz;
    (void) advanced_pic_timer_backend_enable_periodic_mode(periodic_frequency_hz);
}

bool kernel_tickless_enabled(void) { return tickless_permitted; }

uint32_t kernel_tickless_sleep(uint32_t microseconds)
{
    if (microseconds > KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS)
        microseconds = KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS;

    if (!tickless_permitted)
    {
        /* Without permission the tick is still running, so waiting means taking the
           interrupts it delivers. Honest and unremarkable — and counted the same way,
           so a profile cannot claim a saving it did not make. */
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

    /* Step five of the sequence: ask the hardware how long was really spent rather
       than assuming the deadline was met. The count that remains says how much of the
       delay was left when something else woke the core. */
    uint32_t elapsed = microseconds;
    if (remaining != 0u && remaining <= armed && armed != 0u)
    {
        const uint64_t consumed = (uint64_t) (armed - remaining);
        elapsed = (uint32_t) ((consumed * 1000000u) / (uint64_t) timer_hz);
        ++tickless_early_wakes;
    }

    tickless_slept_microseconds += elapsed;
    tickless_ticks_avoided += (uint32_t) (((uint64_t) elapsed * (uint64_t) tickless_nominal_hz) / 1000000u);
    return elapsed;
}

uint32_t kernel_tickless_ticks_avoided(void) { return tickless_ticks_avoided; }

uint32_t kernel_tickless_early_wakes(void) { return tickless_early_wakes; }

uint64_t kernel_tickless_slept_microseconds(void) { return tickless_slept_microseconds; }

uint32_t kernel_tickless_nominal_frequency_hz(void) { return tickless_nominal_hz; }
