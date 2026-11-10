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
 * @file wake_planner.h
 * @brief When a core must wake: the fewest wake-ups that serve every duty inside its window.
 *
 * A duty does not ask for an instant but for a window: it may run from its earliest cycle, and
 * must have run by its latest. Waking at the earliest latest among the duties waiting, and serving
 * there every duty whose window has opened, is the greedy cover of intervals by points, and no
 * schedule of wake-ups covers the same windows with fewer: the first wake-up cannot come later
 * without missing a window, and coming earlier covers no more. Any wake-up a device causes serves
 * the open windows the same way, so a duty whose window holds a device's interrupt never costs a
 * wake-up of its own. A timer's wake-up comes after the cycle it was armed for, so the plan arms it
 * a lead early, and a lead the wake-up takes exactly brings it to that same latest.
 *
 * A duty whose latest is @ref KERNEL_WAKE_PLANNER_NEVER never wakes the core: it runs at the first
 * wake-up after its earliest, whatever caused it, which is what a check that only reads what running
 * code changed needs.
 *
 * The plan is pure: the clock is a parameter, the cycles of the timestamp counter where the kernel
 * sleeps on one, so a test can drive every case without waiting, and the same plan decides the same
 * wake-ups for the same windows on every run. Sleeping on it is the tickless layer's job.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-10
 **************************************************************************/

#ifndef KERNEL_POWER_WAKE_PLANNER_H
#define KERNEL_POWER_WAKE_PLANNER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Duties one plan holds: enough for every source one core serves, and two cache lines of windows. */
#define KERNEL_WAKE_PLANNER_DUTIES 8u

/** The latest of a duty that never wakes the core by itself. */
#define KERNEL_WAKE_PLANNER_NEVER UINT64_MAX

/** The duty number that names no duty: duties are numbered from 1. */
#define KERNEL_WAKE_PLANNER_NO_DUTY 0u

/**
 * @brief Runs a duty whose window was open at a wake-up.
 *
 * @details The duty is disarmed before it runs, so it arms its next window itself, from @p now or from
 *          the data it found; a window armed here opens at a later wake-up, never at this one.
 *
 * @param context What the duty was declared with.
 * @param now     The cycle the core woke at.
 */
typedef void (*KernelWakePlannerDuty_t)(void *context, uint64_t now);

/**
 * @struct KernelWakePlan_t
 * @brief The duties of one core, their windows, and what serving them cost.
 *
 * @details The windows lead, each kind in a cache line of its own, because a wake-up reads them and
 *          nothing else of the duties it does not serve.
 */
typedef struct {
    uint64_t earliest[KERNEL_WAKE_PLANNER_DUTIES];              /**< Cycle from which each armed duty may run. */
    uint64_t latest[KERNEL_WAKE_PLANNER_DUTIES];                /**< Cycle by which it must have run, or NEVER. */
    KernelWakePlannerDuty_t duties[KERNEL_WAKE_PLANNER_DUTIES]; /**< What runs, per declared duty. */
    void *contexts[KERNEL_WAKE_PLANNER_DUTIES];                 /**< What it runs with. */
    uint32_t declared;                                          /**< One bit per declared duty, bit 0 for duty 1. */
    uint32_t armed;                                             /**< One bit per duty whose window is set. */
    uint32_t waking;       /**< One bit per duty whose last window has a latest, so wakes the core; read under armed. */
    uint32_t wakes;        /**< Wake-ups served. */
    uint32_t wasted_wakes; /**< Wake-ups that found no window open. */
    uint32_t batched_wakes;    /**< Wake-ups that served two duties or more. */
    uint32_t services;         /**< Duties run. */
    uint32_t late_services;    /**< Duties run after their latest. */
    uint64_t latest_overshoot; /**< The most cycles a duty ran past its latest: a wake-up's latency, at least. */
} __attribute__((aligned(64))) KernelWakePlan_t;

/**
 * @brief Empties a plan: no duty declared, nothing counted.
 *
 * @param plan The plan.
 */
void kernel_wake_planner_initialize(KernelWakePlan_t *plan);

/**
 * @brief Declares a duty, unarmed.
 *
 * @note Not from a duty while its wake-up is served: a duty withdrawn there leaves a number the wake-up may
 *       still serve, and a declaration would take it unarmed.
 *
 * @param plan    The plan.
 * @param duty    What runs when its window is open at a wake-up; NULL is refused.
 * @param context What it runs with.
 * @return The duty's number, from 1; @ref KERNEL_WAKE_PLANNER_NO_DUTY when refused or when the plan holds
 *         @ref KERNEL_WAKE_PLANNER_DUTIES already.
 */
uint32_t kernel_wake_planner_declare(KernelWakePlan_t *plan, KernelWakePlannerDuty_t duty, void *context);

/**
 * @brief Removes a declared duty, armed or not; its number may be given to the next declaration.
 *
 * @param plan The plan.
 * @param duty A number @ref kernel_wake_planner_declare returned; any other is ignored.
 */
void kernel_wake_planner_withdraw(KernelWakePlan_t *plan, uint32_t duty);

/**
 * @brief Sets a declared duty's window, replacing the one it had.
 *
 * @param plan     The plan.
 * @param duty     A declared duty.
 * @param earliest Cycle from which it may run.
 * @param latest   Cycle by which it must have run, at least @p earliest; @ref KERNEL_WAKE_PLANNER_NEVER for
 *                 a duty that rides other wake-ups.
 * @return true when armed; false for a duty not declared, or a window that closes before it opens.
 */
bool kernel_wake_planner_arm(KernelWakePlan_t *plan, uint32_t duty, uint64_t earliest, uint64_t latest);

/**
 * @brief When to arm the core's wake-up, so that one coming @p lead cycles after it serves the most urgent duty
 *        by its latest: for each armed duty, its latest less the lead, never before its earliest, and the earliest
 *        of those.
 *
 * @details With no lead, that is the earliest latest, the greedy cover. A lead the wake-up takes exactly brings each
 *          wake-up to the latest of the duty that sets it, so it takes no more wake-ups, and serves a window at least
 *          a lead wide by its latest. A window narrower than the lead is armed at its earliest rather than before it:
 *          a timer that never fires early then finds it open, however short the latency turns out, and it is late by
 *          less than without the lead. The duty that sets the wake-up is not always the one with the earliest
 *          latest: a wide window whose latest comes just after a narrow one's is armed earlier, where arming at the
 *          narrow one's earliest would serve both late.
 *
 * @param plan The plan.
 * @param lead Cycles the wake-up is expected to come after its arming; 0 for the earliest latest.
 * @return That cycle; @ref KERNEL_WAKE_PLANNER_NEVER when no armed duty needs a wake-up of its own.
 */
uint64_t kernel_wake_planner_next_wake(const KernelWakePlan_t *plan, uint64_t lead);

/**
 * @brief Serves a wake-up: runs, once each, every armed duty whose window has opened by @p now, the one
 *        with the earliest latest first.
 *
 * @details The duties are the ones open when the wake-up begins: one armed by a duty that runs here
 *          waits for the next wake-up, and one withdrawn by it is not run. Lateness is counted against
 *          @p now: a duty run after its latest is counted late, and how far past it raises the plan's
 *          worst overshoot. A wake-up on the plan's own deadline comes after it by the interrupt's latency,
 *          so its duties are late by that much, which the overshoot measures.
 *
 * @param plan The plan.
 * @param now  The cycle the core woke at.
 * @return The duties run.
 */
uint32_t kernel_wake_planner_serve(KernelWakePlan_t *plan, uint64_t now);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_WAKE_PLANNER_H */
