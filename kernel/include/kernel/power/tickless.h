/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
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
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef KERNEL_POWER_TICKLESS_H
#define KERNEL_POWER_TICKLESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/power/wake_planner.h>

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

#define KERNEL_TICKLESS_PROBES                   4u  /**< Sleeps the first stop of the tick probes a depth with. */
#define KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS 16u /**< The first probe's length; each next one is four times it. */

/**
 * @brief Stops the periodic tick, whichever timer owns it, permits sleeping without it, and hands
 *        the tick's own duty to @p plan.
 *
 * @details The saving is measured against the tick in force when the session starts, read from
 *          the timer rather than assumed: a profile that reported ticks avoided against a rate it
 *          was never running would be reporting fiction.
 *
 *          The tick runs a reconciler pass every KERNEL_RECONCILER_TICK_SAMPLE_PERIOD ticks, and a
 *          stopped tick would stop it in silence. The pass becomes a duty of @p plan that never wakes
 *          the core: it only reads what running code changed, so it rides the first wake-up after
 *          each period of the tick in force.
 *
 *          The first session of a boot at a depth probes it before returning: @ref KERNEL_TICKLESS_PROBES
 *          sleeps on the timer, from @ref KERNEL_TICKLESS_FIRST_PROBE_MICROSECONDS each four times longer,
 *          teach the depth its lead, so that no duty's first wake-up is armed on a guess. The probe's sleeps
 *          are the processor's, counted as any; a session that counts only its loop starts its accounting
 *          after this call (@ref kernel_processor_sleep_begin_accounting).
 *
 * @param no_world_instantiated Caller's declaration that no authoritative simulation
 *                              is running. False refuses: stopping the tick under a
 *                              World would make its cadence depend on how busy the
 *                              machine was, and every parity gate rests on it not.
 * @param plan                  The plan the session sleeps on; NULL refuses, and so does a plan
 *                              with no duty left for the reconciler's pass.
 * @return true when the tick is stopped; false when refused, already stopped, or when no
 *         calibrated one-shot timer could wake a core with no tick.
 */
bool kernel_tickless_enable(bool no_world_instantiated, KernelWakePlan_t *plan);

/**
 * @brief Gives the tick back to the timer that owned it, at the rate it ran, takes the reconciler's
 *        pass back from the session's plan, and forbids stopping the tick again until the next
 *        @ref kernel_tickless_enable.
 *
 * @note Does nothing when the tick was not stopped.
 */
void kernel_tickless_disable(void);

/**
 * @brief Is tickless operation permitted?
 * @return true after a successful @ref kernel_tickless_enable.
 */
bool kernel_tickless_enabled(void);

/**
 * @brief The cycles of the timestamp counter that last @p microseconds, at the rate the local timer's
 *        calibration measured.
 *
 * @param microseconds A duration.
 * @return Its cycles; 0 before a calibration succeeded.
 */
uint64_t kernel_tickless_cycles_from_microseconds(uint32_t microseconds);

/**
 * @brief The microseconds @p cycles of the timestamp counter last, at the rate the local timer's calibration
 *        measured, rounded down.
 *
 * @param cycles A duration.
 * @return Its microseconds, saturated at the 32-bit maximum; 0 before a calibration succeeded.
 */
uint32_t kernel_tickless_microseconds_from_cycles(uint64_t cycles);

/**
 * @brief Sleeps once, with no tick, until @p plan's next wake-up or any interrupt, then serves the
 *        plan at that wake-up.
 *
 * Returns early on any interrupt, which is the point rather than a caveat: the plan's
 * wake-up is the LATEST its duties allow, and a device that has something to say should
 * not have to wait for it; the duties whose windows are open by then are served at its
 * wake-up instead of costing one of their own.
 *
 * @details The plan is served at the cycle the core woke, so a window that opened while the interrupt
 *          was on its way is served too. The timer comes after its deadline by a latency, so the wake-up is
 *          armed early by the lead the timer's own wake-ups taught (wake_latency.h), never before the earliest
 *          of the window it serves: a window at least that wide is served by its latest, a narrower one late by
 *          less than without the lead, and a window of one cycle late by the whole latency, which the plan's
 *          worst overshoot measures. Only a sleep the timer ended teaches the lead; one another interrupt cut
 *          short does not. Each depth has its own lead: the first stop of the tick in a boot probes the depth
 *          sleeps enter, before any duty sleeps (@ref kernel_tickless_enable), and a depth the session did not
 *          probe starts from the lead of the one it did.
 *          A wake-up already due is served without sleeping, and one further than
 *          @ref KERNEL_TICKLESS_MAX_SLEEP_MICROSECONDS is cut to it. In TSC-deadline mode the armed cycle is
 *          the deadline, so the interrupt never comes before it; in count mode a count stands for it, through
 *          the calibration's ratio. Interrupts are masked from the arming to the sleep, which enables them
 *          in the same step: a deadline that passes while it is armed, closer than the arming takes, ends the
 *          sleep instead of being handled just before it, which with the tick stopped would leave nothing to
 *          wake the core.
 *
 * @note Without permission the tick is still running, so the sleep simply takes the
 *       interrupts it delivers and reports no time slept. Honest and unremarkable — and
 *       counted the same way, so a profile cannot claim a saving it did not make.
 *
 * @param plan The plan the session sleeps on: with the tick stopped, the one
 *             @ref kernel_tickless_enable was given, which carries the reconciler's pass; any other is
 *             refused, neither slept on nor served.
 * @return Microseconds actually spent, measured on the timestamp counter; 0 for a refused plan.
 */
uint32_t kernel_tickless_sleep_on(KernelWakePlan_t *plan);

/**
 * @brief Periodic interrupts this profile did not take.
 *
 * The saving, as a number. Computed from the time actually slept and the cadence
 * that was running when the tick was stopped, so it is what was avoided rather than
 * what was hoped for. The same ticks are added to the tick count, which stays
 * continuous across a session.
 *
 * @return The count.
 */
uint32_t kernel_tickless_ticks_avoided(void);

/**
 * @brief Times the sleep was cut short by something other than its deadline: the timer was still armed
 *        when the core woke.
 * @return The count.
 */
uint32_t kernel_tickless_early_wakes(void);

/**
 * @brief How many cycles before a window's latest a sleep at the current depth arms its wake-up: what the timer's
 *        wake-ups at that depth taught, the probe's first, over every session of this boot.
 * @return The lead; 0 before a session probed the depth.
 */
uint64_t kernel_tickless_wake_lead_cycles(void);

/**
 * @brief How many of the timer's wake-ups taught the current depth's lead, the probe's included.
 * @return The count; 0 before a session probed the depth.
 */
uint32_t kernel_tickless_wake_samples(void);

/**
 * @brief How long the last probe took, from its first sleep to the end of its last.
 * @return Cycles of the timestamp counter; 0 before any probe.
 */
uint64_t kernel_tickless_probe_cycles(void);

/**
 * @brief The nanoseconds @p cycles of the timestamp counter last, rounded down, whole seconds first so that the
 *        product never overflows.
 * @param cycles A duration on the timestamp counter.
 * @return The nanoseconds; 0 without a calibrated counter.
 */
uint64_t kernel_tickless_nanoseconds_from_cycles(uint64_t cycles);

/**
 * @brief Total time spent with the tick stopped, in microseconds.
 * @return The total.
 */
uint64_t kernel_tickless_slept_microseconds(void);

/**
 * @brief The cadence the last session stopped, against which its saving was counted.
 * @return Hertz, or 0 before any session.
 */
uint32_t kernel_tickless_nominal_frequency_hz(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_TICKLESS_H */
