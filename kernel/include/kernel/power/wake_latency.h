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
 * @file wake_latency.h
 * @brief How long a core's timer wake-up comes after its deadline, learned from the wake-ups, and the lead
 *        that serves a window by its latest.
 *
 * A timer never ends a sleep on its deadline: in TSC-deadline mode it fires there at the earliest, and the
 * core then leaves its sleep and runs the handler, so the code after the sleep runs a latency later, the
 * longer the deeper the sleep or the busier the hypervisor. Arming a wake-up that much before the latest of
 * a window serves it by its latest, and since a window may be served from its earliest on, being early
 * there costs nothing but the windows that would have opened in the margin, while being late costs a late
 * service. The lead is therefore an envelope of the latency rather than its mean, which half the wake-ups
 * exceed: the estimator of RFC 6298, a smoothed latency plus a multiple of its smoothed mean deviation,
 * with its gains of an eighth and a quarter, in integers scaled as Jacobson and Karels wrote it.
 *
 * Two changes suit wake-ups. A latency has a long tail, so the lead takes six mean deviations rather than
 * RFC 6298's four. And a host that holds a virtual processor for milliseconds produces a wake-up no lead
 * could have served, which in the plain estimator would push every following wake-up milliseconds early
 * for a dozen wake-ups more: here a sample counts at most as twice the lead it is compared with, so that
 * one such wake-up moves the lead a bounded step, and a lasting change still multiplies the lead at every
 * wake-up until it is caught. KVM's own advance for its guests' timers, which follows the mean by an eighth
 * of each error, skips such spikes outright (`adjust_lapic_timer_advance` in `arch/x86/kvm/lapic.c`).
 *
 * The estimate is pure and counts in cycles of whatever clock its samples come from, so a test drives it
 * without waiting; a zeroed estimate has learned nothing, and its lead is zero.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-10
 **************************************************************************/

#ifndef KERNEL_POWER_WAKE_LATENCY_H
#define KERNEL_POWER_WAKE_LATENCY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mean deviations the lead adds to the smoothed latency: RFC 6298 takes four, a wake-up's tail wants more. */
#define KERNEL_WAKE_LATENCY_DEVIATIONS 6u

/** The most a sample counts for, in leads: a wake-up held far past every lead moves the lead a bounded step. */
#define KERNEL_WAKE_LATENCY_CLIP_IN_LEADS 2u

/**
 * @struct KernelWakeLatency_t
 * @brief What one core's wake-ups taught of their latency, in cycles; zeroed, nothing.
 */
typedef struct {
    uint64_t smoothed_eighths;   /**< The smoothed latency, in eighths of a cycle. */
    uint64_t deviation_quarters; /**< Its smoothed mean deviation, in quarters of a cycle. */
    uint32_t samples;            /**< Wake-ups learned from. */
} KernelWakeLatency_t;

/**
 * @brief Learns one wake-up's latency: how many cycles after its deadline the code after the sleep ran.
 *
 * @details The first sample sets the smoothed latency to itself and its deviation to half of it, as RFC 6298
 *          (2.2) does; each next one, counted at most as @ref KERNEL_WAKE_LATENCY_CLIP_IN_LEADS times the
 *          lead, moves the deviation a quarter of the way to its distance from the smoothed latency, then the
 *          smoothed latency an eighth of the way to it, in that order (2.3).
 *
 * @param estimate The core's estimate.
 * @param latency  The cycles from the deadline to the wake-up; 0 when the timer fired at or before it.
 */
void kernel_wake_latency_learn(KernelWakeLatency_t *estimate, uint64_t latency);

/**
 * @brief How many cycles before a window's latest to arm its wake-up: the smoothed latency plus
 *        @ref KERNEL_WAKE_LATENCY_DEVIATIONS mean deviations.
 *
 * @param estimate The core's estimate.
 * @return That lead; 0 before any wake-up was learned from.
 */
uint64_t kernel_wake_latency_lead(const KernelWakeLatency_t *estimate);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_POWER_WAKE_LATENCY_H */
