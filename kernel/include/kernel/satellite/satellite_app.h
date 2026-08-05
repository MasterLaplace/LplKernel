/**
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
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

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
    uint32_t idle_iterations;   /**< Times the loop went round with nothing to do. */
    uint32_t sleeps;            /**< Times the processor was actually put to sleep. */
    uint32_t sleeps_skipped;    /**< Times a sleep was unnecessary — the watch had fired. */
    uint32_t halts;             /**< Sleeps that fell back to HLT for want of MONITOR. */
    uint32_t duty_permille;     /**< Share of accounted cycles spent awake. */
    uint32_t ticks_avoided;     /**< Periodic interrupts the profile did not take. */
    uint32_t monitor_available; /**< 1 when the processor implements MONITOR/MWAIT. */
    uint32_t scaling_available; /**< 1 when it can actually change its clock. */
    uint32_t scaling_refused;   /**< Performance-state requests the hardware could not honour. */
    uint32_t governed_state;    /**< The state the governor asked for. */
    uint32_t audio_present;     /**< 1 when a codec was found AND can be driven. */
    uint32_t output_ceiling;    /**< Loudest sample this kernel will ever emit. */
    uint32_t limiter_clipped;   /**< Samples the ceiling clamped during the self-check. */
    uint32_t limiter_peak;      /**< Loudest sample that survived it. */
    uint32_t frames_captured;   /**< Buffers the codec actually delivered. */
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
 * @brief Brings the power floor up and runs the node for @p iterations frames.
 *
 * Nothing here allocates and nothing here blocks on a device that may be absent: the
 * profile must be able to boot on a machine with no codec and say so, rather than
 * wait forever for a buffer that will never arrive.
 *
 * @param iterations Frames to run.
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
