/**
 * @file processor_sleep.h
 * @brief Sleeping between buffers instead of spinning.
 *
 * A satellite is silent most of its life. `HLT` alone wakes late; the monitor/wait pair
 * lets the processor sleep and be woken by a write to a watched address — which is
 * precisely what a device pushing an audio buffer does. This is the difference between
 * a node that idles at a few watts and one that idles at twenty.
 *
 * Everything here is accounted, and that is not decoration. "This profile costs
 * nothing when idle" is a claim about energy, and a claim about energy that is not
 * measured is a wish. The counters below are what let the satellite profile print a
 * duty cycle instead of a promise.
 *
 * The pair also has a discipline that a wrapper cannot enforce and a caller must
 * not skip: between arming the monitor and sleeping, re-check the condition. If the
 * device wrote in that window the monitor is already spent, and the sleep would be
 * waiting for a wake-up that has already happened. @ref processor_sleep_until_write
 * takes the condition as an argument for exactly this reason.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_POWER_PROCESSOR_SLEEP_H
#define KERNEL_POWER_PROCESSOR_SLEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum ProcessorSleepMode_t
 * @brief How the processor was actually put to sleep.
 */
typedef enum {
    PROCESSOR_SLEEP_NONE = 0, /**< It was not: the condition was already true. */
    PROCESSOR_SLEEP_HALT,     /**< `HLT`, woken by an interrupt. */
    PROCESSOR_SLEEP_MONITOR,  /**< `MONITOR`/`MWAIT`, woken by a write or an interrupt. */
} ProcessorSleepMode_t;

/**
 * @brief Probes what the processor offers and zeroes the accounting.
 *
 * MONITOR/MWAIT is advertised by CPUID leaf 1, ECX bit 3. Probed rather than
 * assumed: it is absent on early processors and, more relevantly here, absent from
 * QEMU's default model — so a kernel that executed MWAIT unconditionally would take
 * an invalid-opcode fault on the machine it is developed on.
 */
void kernel_processor_sleep_initialize(void);

/**
 * @brief Does this processor implement the monitor/wait pair?
 * @return true when CPUID advertises it.
 */
bool kernel_processor_sleep_has_monitor(void);

/**
 * @brief Smallest cache line the monitor can watch, in bytes.
 * @return The size, or 0 when the pair is unavailable.
 */
uint32_t kernel_processor_sleep_monitor_line_bytes(void);

/**
 * @brief Sleeps until @p watched changes, or an interrupt arrives.
 *
 * The value is re-read after arming the monitor and before sleeping, which closes
 * the race the pair is famous for. Falls back to `HLT` where the pair is absent —
 * correct, just less precise: a halted core still wakes on the timer interrupt, it
 * simply cannot be woken by a device's DMA alone.
 *
 * @param watched  Address to watch; typically a driver's write index.
 * @param expected Value that means "nothing new yet".
 * @return How the processor was put to sleep, if at all.
 */
ProcessorSleepMode_t processor_sleep_until_write(const volatile uint32_t *watched, uint32_t expected);

/**
 * @brief Halts until the next interrupt.
 *
 * The blunt instrument, for an idle loop with nothing to watch. Counted like the
 * other path so a profile cannot quietly fall back to it and still claim to sleep
 * deeply.
 */
void processor_sleep_until_interrupt(void);

/**
 * @brief Times the processor was put to sleep.
 * @return The count, both modes together.
 */
uint32_t kernel_processor_sleep_count(void);

/**
 * @brief Times a sleep was skipped because the condition was already true.
 *
 * The number that says whether the watch is set on the right address. A node that
 * never actually sleeps is a node whose condition is always already true — which
 * looks identical, from the outside, to one that sleeps perfectly.
 *
 * @return The count.
 */
uint32_t kernel_processor_sleep_skipped_count(void);

/**
 * @brief Times the fallback was taken because the pair is unavailable.
 * @return The count.
 */
uint32_t kernel_processor_sleep_halt_count(void);

/**
 * @brief Cycles spent asleep.
 * @return The total, from the timestamp counter.
 */
uint64_t kernel_processor_sleep_asleep_cycles(void);

/**
 * @brief Cycles spent awake between sleeps.
 *
 * Measured from the end of one sleep to the start of the next, so it counts the work
 * and not the accounting. Before the first sleep there is nothing to measure and this
 * stays zero.
 *
 * @return The total.
 */
uint64_t kernel_processor_sleep_awake_cycles(void);

/**
 * @brief Share of accounted time the processor was awake, in thousandths.
 *
 * The satellite profile's headline number. Nothing accounted yet reads as fully
 * awake rather than fully asleep: a counter that claimed perfect efficiency before
 * measuring anything is the one answer a power figure must never give by default.
 *
 * @return Awake over awake plus asleep, 0 to 1000.
 */
uint32_t kernel_processor_sleep_duty_cycle_permille(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_PROCESSOR_SLEEP_H */
