#include <kernel/satellite/satellite_app.h>

#include <kernel/hal/hal_audio.h>
#include <kernel/power/frequency_scaling.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/tickless.h>

/** Cadence the engine profiles run at, and the one this profile gives up. */
#define SATELLITE_NOMINAL_TICK_HZ 1000u

/** Microseconds of audio in one buffer, and therefore the longest useful sleep. */
#define SATELLITE_FRAME_MICROSECONDS 40000u

/**
 * @brief The magnitude of a sample, without the overflow `-(-32768)` would be.
 * @param sample Signed sample.
 * @return Its distance from zero.
 */
static uint32_t satellite_magnitude(int16_t sample)
{
    return (uint32_t) (sample < 0 ? -(int32_t) sample : (int32_t) sample);
}

/**
 * @brief The loudest sample of a buffer.
 * @param samples Buffer to scan.
 * @param count   Samples in it.
 * @return The largest magnitude found, 0 for an empty buffer.
 */
static uint32_t satellite_peak_magnitude(const int16_t *samples, uint32_t count)
{
    uint32_t peak = 0u;
    for (uint32_t i = 0u; i < count; ++i)
    {
        const uint32_t magnitude = satellite_magnitude(samples[i]);
        if (magnitude > peak)
            peak = magnitude;
    }
    return peak;
}

/**
 * @brief Checks the output limiter rather than trusting it.
 *
 * @details A full-scale square wave is pushed through it and the loudest thing that comes
 *          out is reported: if the ceiling were ever bypassed — by a gain that multiplied
 *          after the clamp, by a sign that clamped one way only — this number would say
 *          so, and it would say so before anybody has headphones on.
 *
 * @param report Receives the ceiling, the clipped count and the peak that came out.
 */
static void satellite_probe_output_limiter(SatelliteReport_t *report)
{
    static int16_t probe[64];
    static int16_t limited[64];
    for (uint32_t i = 0u; i < 64u; ++i)
        probe[i] = (i % 2u == 0u) ? 32767 : -32768;

    report->limiter_clipped = hardware_abstraction_layer_audio_limit(probe, 64u, limited);
    report->output_ceiling = (uint32_t) KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE;
    report->limiter_peak = satellite_peak_magnitude(limited, 64u);
}

/**
 * @brief Pumps the capture ring, unless an interrupt handler already does.
 *
 * @note Where the controller interrupts on each completed half, its handler is the
 *       producer and this loop must not pump as well: two producers on one ring is a race.
 */
static void satellite_pump_capture_when_polled(void)
{
    if (!hardware_abstraction_layer_audio_capture_is_interrupt_driven())
        (void) hardware_abstraction_layer_audio_capture_pump();
}

/**
 * @brief Drains one buffer and raises the capture peak to its loudest sample.
 *
 * @note The frame is static rather than automatic: 640 samples is 1280 bytes, and a
 *       kernel stack is not the place to put them.
 *
 * @param report Receives the running capture peak.
 */
static void satellite_drain_one_frame(SatelliteReport_t *report)
{
    static int16_t frame[KERNEL_HAL_AUDIO_FRAME_SAMPLES];
    const uint32_t samples = hardware_abstraction_layer_audio_capture_take(frame, KERNEL_HAL_AUDIO_FRAME_SAMPLES);
    const uint32_t peak = satellite_peak_magnitude(frame, samples);
    if (peak > report->capture_peak)
        report->capture_peak = peak;
}

/**
 * @brief Governs on what was measured rather than on what was intended.
 *
 * @details A node whose loop turned out to be busy must not be handed a low clock because
 *          its designer expected it to be idle.
 *
 * @param report Holds the measured duty cycle; receives the state and what came of it.
 */
static void satellite_govern_on_measured_duty(SatelliteReport_t *report)
{
    const PerformanceState_t state = kernel_frequency_scaling_govern(report->duty_permille, false);
    report->governed_state = (uint32_t) state;
    (void) kernel_frequency_scaling_request(state);
    report->scaling_refused = kernel_frequency_scaling_refused_count();
    report->effective_permille = kernel_frequency_scaling_measured_permille();
}

bool kernel_satellite_app_run(uint32_t iterations, SatelliteReport_t *out)
{
    if (out == NULL)
        return false;

    SatelliteReport_t report = {0};

    kernel_processor_sleep_initialize();
    (void) kernel_processor_sleep_request_hint(PROCESSOR_SLEEP_HINT_MAX);
    kernel_frequency_scaling_initialize();
    kernel_frequency_scaling_begin_measurement();

    report.monitor_available = kernel_processor_sleep_has_monitor() ? 1u : 0u;
    report.scaling_available = kernel_frequency_scaling_available() ? 1u : 0u;
    report.audio_present = hardware_abstraction_layer_audio_initialize() ? 1u : 0u;

    (void) kernel_tickless_enable(true, SATELLITE_NOMINAL_TICK_HZ);
    satellite_probe_output_limiter(&report);

    const volatile uint32_t *const written = hardware_abstraction_layer_audio_capture_write_index();
    const uint32_t written_before = (written != NULL) ? *written : 0u;

    for (uint32_t i = 0u; i < iterations; ++i)
    {
        ++report.idle_iterations;
        satellite_pump_capture_when_polled();
        satellite_drain_one_frame(&report);
        (void) kernel_tickless_sleep(SATELLITE_FRAME_MICROSECONDS);
    }

    report.frames_captured = (written != NULL) ? (*written - written_before) : 0u;
    report.sleeps = kernel_processor_sleep_count();
    report.sleeps_skipped = kernel_processor_sleep_skipped_count();
    report.halts = kernel_processor_sleep_halt_count();
    report.duty_permille = kernel_processor_sleep_duty_cycle_permille();
    report.ticks_avoided = kernel_tickless_ticks_avoided();

    satellite_govern_on_measured_duty(&report);
    kernel_tickless_disable(SATELLITE_NOMINAL_TICK_HZ);

    *out = report;
    return true;
}

const char *kernel_satellite_app_audio_name(void) { return hardware_abstraction_layer_audio_device_name(); }
