/**
 * @file tickless.h
 * @brief Stopping the periodic tick when nothing is due.
 *
 * The deterministic tick is the heart of the engine profiles and pure waste on a
 * satellite: waking a thousand times a second to discover that nobody spoke. When the
 * next deadline is far, the timer is programmed for that deadline instead of for a
 * fixed period.
 *
 * The trap is that the tick is also what the determinism contract is built on, so this
 * may only ever be enabled on a profile that runs no world.
 *
 * That last sentence is enforced and not merely written: @ref kernel_tickless_enable
 * refuses unless the caller declares that no World is instantiated. A satellite is a
 * sense organ, not an organ of the demon — it carries no parity gate precisely because
 * it runs no authoritative simulation, and that is what earns it the right to have no
 * cadence at all.
 *
 * The sequence is the classic one (LplKernel_Book §9.6.2): work out the exact delay
 * before the next deadline, stop the periodic timer, arm a one-shot for that delay,
 * sleep, and on waking ask the absolute clock how long was actually spent so the
 * global tick count can be advanced by the right amount. The illusion is perfect for
 * anything above: time appears continuous.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_POWER_TICKLESS_H
#define KERNEL_POWER_TICKLESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Longest single sleep, in microseconds.
 *
 * One second. Not a hardware limit — the one-shot count reaches further — but a
 * bound on how wrong the reconstructed tick count can be if the absolute clock is
 * ever misread. A node that woke up believing an hour had passed would be worse
 * than one that woke up too often.
 */
#define KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS 1000000u

/**
 * @brief Permits the tick to be stopped.
 *
 * @param no_world_instantiated Caller's declaration that no authoritative simulation
 *                              is running. False refuses: stopping the tick under a
 *                              World would make its cadence depend on how busy the
 *                              machine was, and every parity gate rests on it not.
 * @param nominal_frequency_hz  The cadence being given up. Taken as a parameter and
 *                              not assumed, because the saving is measured against
 *                              it: a profile that reported ticks avoided against a
 *                              rate it was never running would be reporting fiction.
 * @return true when tickless operation is now permitted.
 */
bool kernel_tickless_enable(bool no_world_instantiated, uint32_t nominal_frequency_hz);

/**
 * @brief Forbids stopping the tick and restores the periodic timer.
 *
 * @param periodic_frequency_hz Cadence to restore.
 */
void kernel_tickless_disable(uint32_t periodic_frequency_hz);

/**
 * @brief Is tickless operation permitted?
 * @return true after a successful @ref kernel_tickless_enable.
 */
bool kernel_tickless_enabled(void);

/**
 * @brief Sleeps until @p microseconds have passed, with no tick in between.
 *
 * Returns early on any interrupt, which is the point rather than a caveat: a
 * deadline is the LATEST the caller wants to wake, and a device that has something
 * to say should not have to wait for it.
 *
 * @param microseconds Delay; clamped to @ref KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS.
 * @return Microseconds actually spent, measured from the absolute clock.
 */
uint32_t kernel_tickless_sleep(uint32_t microseconds);

/**
 * @brief Periodic interrupts this profile did not take.
 *
 * The saving, as a number. Computed from the time actually slept and the cadence
 * that would otherwise have been running, so it is what was avoided rather than
 * what was hoped for.
 *
 * @return The count.
 */
uint32_t kernel_tickless_ticks_avoided(void);

/**
 * @brief Times the sleep was cut short by something other than its deadline.
 * @return The count.
 */
uint32_t kernel_tickless_early_wakes(void);

/**
 * @brief Total time spent with the tick stopped, in microseconds.
 * @return The total.
 */
uint64_t kernel_tickless_slept_microseconds(void);

/**
 * @brief The cadence that would be running were the tick not stopped.
 * @return Hertz.
 */
uint32_t kernel_tickless_nominal_frequency_hz(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_TICKLESS_H */
