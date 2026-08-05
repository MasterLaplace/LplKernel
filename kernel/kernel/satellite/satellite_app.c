/**
 * @file satellite_app.c
 * @brief The satellite profile's whole job.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/satellite/satellite_app.h>

#include <kernel/hal/hal_audio.h>
#include <kernel/power/frequency_scaling.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/tickless.h>

/** Cadence the engine profiles run at, and the one this profile gives up. */
#define SATELLITE_NOMINAL_TICK_HZ 1000u

/** Microseconds of audio in one buffer, and therefore the longest useful sleep. */
#define SATELLITE_FRAME_MICROSECONDS 40000u

bool kernel_satellite_app_run(uint32_t iterations, SatelliteReport_t *out)
{
    if (out == NULL)
        return false;

    SatelliteReport_t report;
    report.idle_iterations = 0u;
    report.sleeps = 0u;
    report.sleeps_skipped = 0u;
    report.halts = 0u;
    report.duty_permille = 0u;
    report.ticks_avoided = 0u;
    report.monitor_available = 0u;
    report.scaling_available = 0u;
    report.scaling_refused = 0u;
    report.governed_state = 0u;
    report.audio_present = 0u;
    report.output_ceiling = 0u;
    report.limiter_clipped = 0u;
    report.limiter_peak = 0u;
    report.frames_captured = 0u;
    report.capture_peak = 0u;

    kernel_processor_sleep_initialize();
    kernel_frequency_scaling_initialize();

    report.monitor_available = kernel_processor_sleep_has_monitor() ? 1u : 0u;
    report.scaling_available = kernel_frequency_scaling_available() ? 1u : 0u;
    report.audio_present = hardware_abstraction_layer_audio_initialize() ? 1u : 0u;

    /* The declaration the tickless floor demands, and the reason this profile is the
       only one allowed to make it: a satellite instantiates no World, so there is no
       authoritative tick whose cadence a parity gate is folded against. */
    (void) kernel_tickless_enable(true, SATELLITE_NOMINAL_TICK_HZ);

    /* The output limiter, checked rather than trusted. A full-scale square wave is
       pushed through it and the loudest thing that comes out is reported: if the
       ceiling were ever bypassed — by a gain that multiplied after the clamp, by a
       sign that clamped one way only — this number would say so, and it would say so
       before anybody has headphones on. */
    {
        static int16_t probe[64];
        static int16_t limited[64];
        for (uint32_t i = 0u; i < 64u; ++i)
            probe[i] = (i % 2u == 0u) ? 32767 : -32768;

        report.limiter_clipped = hardware_abstraction_layer_audio_limit(probe, 64u, limited);
        report.output_ceiling = (uint32_t) KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE;
        for (uint32_t i = 0u; i < 64u; ++i)
        {
            const int32_t magnitude = limited[i] < 0 ? -(int32_t) limited[i] : (int32_t) limited[i];
            if ((uint32_t) magnitude > report.limiter_peak)
                report.limiter_peak = (uint32_t) magnitude;
        }
    }

    for (uint32_t i = 0u; i < iterations; ++i)
    {
        ++report.idle_iterations;

        /* Take whatever the controller has finished. This is the producer: the driver
           polls its stream's link position, so nothing advances the ring unless it is
           asked to. Sleeping on the write index — the shape this profile will use once
           an interrupt handler pushes buffers — would here be a wait on an address only
           this loop ever writes, which is a wait that cannot end. */
        report.frames_captured += hardware_abstraction_layer_audio_capture_pump();

        /* Drain one buffer and measure it. Static rather than automatic: 640 samples is
           1280 bytes, and a kernel stack is not the place to put them. */
        {
            static int16_t frame[KERNEL_HAL_AUDIO_FRAME_SAMPLES];
            const uint32_t samples = hardware_abstraction_layer_audio_capture_take(frame, KERNEL_HAL_AUDIO_FRAME_SAMPLES);
            for (uint32_t s = 0u; s < samples; ++s)
            {
                const int32_t magnitude = frame[s] < 0 ? -(int32_t) frame[s] : (int32_t) frame[s];
                if ((uint32_t) magnitude > report.capture_peak)
                    report.capture_peak = (uint32_t) magnitude;
            }
        }

        /* Sleep to the frame deadline. It is the honest wait for a polled producer, and
           it still exercises the one-shot timer, which is what proves the periodic tick
           really is stopped rather than merely quiet. */
        (void) kernel_tickless_sleep(SATELLITE_FRAME_MICROSECONDS);
    }

    report.sleeps = kernel_processor_sleep_count();
    report.sleeps_skipped = kernel_processor_sleep_skipped_count();
    report.halts = kernel_processor_sleep_halt_count();
    report.duty_permille = kernel_processor_sleep_duty_cycle_permille();
    report.ticks_avoided = kernel_tickless_ticks_avoided();

    /* Govern on what was measured rather than on what was intended. A node whose
       loop turned out to be busy must not be handed a low clock because its designer
       expected it to be idle. */
    const PerformanceState_t state = kernel_frequency_scaling_govern(report.duty_permille, false);
    report.governed_state = (uint32_t) state;
    (void) kernel_frequency_scaling_request(state);
    report.scaling_refused = kernel_frequency_scaling_refused_count();

    /* The tick goes back on before returning. This profile is being exercised from
       inside an image that also runs a World, and leaving its clock stopped would
       take the cadence away from a simulation that needs it — which is precisely the
       failure kernel_tickless_enable exists to refuse. */
    kernel_tickless_disable(SATELLITE_NOMINAL_TICK_HZ);

    *out = report;
    return true;
}

const char *kernel_satellite_app_audio_name(void) { return hardware_abstraction_layer_audio_device_name(); }
