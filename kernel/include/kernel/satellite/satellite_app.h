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
 * @file satellite_app.h
 * @brief The satellite profile's whole job.
 *
 * Capture, gate on the wake word, stream, play back, and sleep. No world, no engine
 * tick, no display, no inference beyond the gate — which is why this profile can run
 * on hardware the other two cannot.
 *
 * It is the middle rung of a ladder: the hosted development satellite above it, the
 * microcontroller firmware below, and the same protocol module on all three.
 *
 * The profile is the one place in this kernel where spending nothing is the goal
 * rather than a side effect, so everything it reports is a MEASUREMENT: how much of
 * its life the processor was awake, how many periodic interrupts it did not take,
 * how many sleeps it actually got. A profile that claimed to idle cheaply without
 * counting would be indistinguishable from one with a spin loop in it.
 *
 * @author @MasterLaplace
 * @version 0.1.0
 * @date 2026-08-05
 **************************************************************************/

#ifndef KERNEL_SATELLITE_SATELLITE_APP_H
#define KERNEL_SATELLITE_SATELLITE_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct SatelliteReport_t
 * @brief What a run of the node cost.
 */
typedef struct {
    uint32_t idle_iterations;        /**< Rounds of the loop on its plan: a sleep, or a wake-up already due. */
    uint32_t capture_services;       /**< Times the capture duty drained the ring. */
    uint32_t wasted_wakes;           /**< Wake-ups of the session that served no duty. */
    uint32_t late_services;          /**< Duties served after their window closed. */
    uint32_t slept_microseconds;     /**< Time the session spent asleep with the tick stopped. */
    uint32_t batched_wakes;          /**< Wake-ups that served two duties or more. */
    uint32_t overshoot_microseconds; /**< The most a duty ran past its latest. */
    uint32_t reconciler_passes;      /**< Reconciler passes the session's wake-ups carried with the tick stopped. */
    uint32_t sleeps;                 /**< Times the processor was actually put to sleep. */
    uint32_t sleeps_skipped;         /**< Times a sleep was unnecessary — the watch had fired. */
    uint32_t halts;                  /**< Sleeps that fell back to HLT for want of MONITOR. */
    uint32_t duty_permille;          /**< Share of accounted cycles spent awake. */
    uint32_t ticks_avoided;          /**< Periodic interrupts the profile did not take. */
    uint32_t monitor_available;      /**< 1 when the processor implements MONITOR/MWAIT. */
    uint32_t scaling_available;      /**< 1 when it can actually change its clock. */
    uint32_t scaling_refused;        /**< Performance-state requests the hardware could not honour. */
    uint32_t governed_state;         /**< The state the governor asked for. */
    uint32_t effective_permille; /**< Clock actually delivered over the run, or KERNEL_FREQUENCY_SCALING_NO_FEEDBACK. */
    uint32_t audio_present;      /**< 1 when a codec was found AND can be driven. */
    uint32_t output_ceiling;     /**< Loudest sample this kernel will ever emit. */
    uint32_t limiter_clipped;    /**< Samples the ceiling clamped during the self-check. */
    uint32_t limiter_peak;       /**< Loudest sample that survived it. */
    uint32_t frames_captured;    /**< Buffers the codec actually delivered. */
    /**
     * Loudest sample in the last buffer taken.
     *
     * Separate from @ref frames_captured because "buffers are arriving" and "buffers
     * contain signal" are different facts with the same shape. A stream that runs with
     * nothing plugged into it delivers silence perfectly on time, and this is the only
     * number that tells the two apart — which makes it the first thing to look at when
     * a microphone appears not to work.
     */
    uint32_t capture_peak;
} SatelliteReport_t;

/**
 * @brief Brings the power floor up and runs the node until its capture duty has drained the
 *        ring @p iterations times.
 *
 * Nothing here allocates and nothing here blocks on a device that may be absent: the
 * profile must be able to boot on a machine with no codec and say so, rather than
 * wait forever for a buffer that will never arrive.
 *
 * @details The loop sleeps on a wake plan whose capture duty drains every buffer waiting,
 *          which exercises the one-shot timer and so proves the periodic tick really is
 *          stopped. Where capture is interrupt-driven a completed half ends the sleep, and
 *          the wake accounting sees it as a second source. Frames are counted as what ARRIVED
 *          rather than what the loop pumped, which is the same number when the loop is the
 *          producer and the only honest one when the interrupt is.
 *
 * @note The deepest sleep hint is requested: a whole frame separates two buffers, so any
 *       C-state's exit latency is microseconds against 40 ms, and this caller is the one
 *       that can afford it.
 * @note Only this profile may declare the tick stoppable: a satellite instantiates no
 *       World, so there is no authoritative tick whose cadence a parity gate is folded
 *       against. The machine is left as the run found it all the same: the capture stream
 *       is stopped, and the tick goes back to the timer that owned it, at the rate it ran.
 *       The profile is exercised from inside an image that also runs a World, and leaving
 *       its clock stopped, faster than it was, or under capture interrupts would change
 *       what a simulation that needs it runs under, which is precisely the failure
 *       @ref kernel_tickless_enable exists to refuse.
 *
 * @param iterations Services of the capture duty to run.
 * @param out        Receives the measurements.
 * @return true when the power floor came up.
 */
bool kernel_satellite_app_run(uint32_t iterations, SatelliteReport_t *out);

/**
 * @brief A word for what the audio seam found.
 * @return The device's name, or "absent".
 */
const char *kernel_satellite_app_audio_name(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SATELLITE_SATELLITE_APP_H */
