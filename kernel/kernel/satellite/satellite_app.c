#include <kernel/satellite/satellite_app.h>

#include <kernel/core/reconciler.h>
#include <kernel/hal/hal_audio.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/energy_counter.h>
#include <kernel/power/frequency_scaling.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/tickless.h>
#include <kernel/power/wake_planner.h>

/** Microseconds of audio in one buffer. */
#define SATELLITE_FRAME_MICROSECONDS 40000u

#define SATELLITE_NANOJOULES_PER_MICROJOULE 1000u /**< Nanojoules in a microjoule. */

/** The capture as a duty of the session's plan: what it reports into, and its window's measures. */
typedef struct {
    SatelliteReport_t *report; /**< Receives the capture peak. */
    KernelWakePlan_t *plan;    /**< The plan it is a duty of. */
    uint32_t duty;             /**< Its number there. */
    uint64_t frame_cycles;     /**< One buffer, in cycles of the timestamp counter. */
    uint32_t services;         /**< Times it ran. */
} SatelliteCapture_t;

static KernelWakePlan_t satellite_plan;

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
 * @brief Drains every buffer waiting in the ring and raises the capture peak to its loudest sample.
 *
 * @note The frame is static rather than automatic: 640 samples is 1280 bytes, and a
 *       kernel stack is not the place to put them.
 *
 * @param report Receives the running capture peak.
 */
static void satellite_drain_waiting_frames(SatelliteReport_t *report)
{
    static int16_t frame[KERNEL_HAL_AUDIO_FRAME_SAMPLES];

    for (uint32_t taken = 0u; taken < KERNEL_HAL_AUDIO_RING_FRAMES; ++taken)
    {
        const uint32_t samples = hardware_abstraction_layer_audio_capture_take(frame, KERNEL_HAL_AUDIO_FRAME_SAMPLES);
        if (samples == 0u)
            return;
        const uint32_t peak = satellite_peak_magnitude(frame, samples);
        if (peak > report->capture_peak)
            report->capture_peak = peak;
    }
}

/**
 * @brief Arms the capture's next window, from the service that just ran at @p from.
 *
 * @details Where the controller's interrupt is the producer, a wake-up means a buffer arrived, so the
 *          window opens at once and holds the interrupt for two buffers: the next one is due within one,
 *          and the timer wakes the core only when it fails to come, long before the ring's eight fill.
 *          Where this loop is the producer, it must pump each completed half before the controller
 *          writes over it one buffer later, so the window closes an eighth of a buffer short of one
 *          buffer on, a margin for the wake-up's latency and for a counter rate measured on a late tick,
 *          and opens half a buffer on, after which a wake-up for another duty finds one worth taking.
 *          Without a calibrated counter there is no buffer to count in cycles, and the capture rides
 *          whatever wakes the core.
 *
 * @param capture The capture duty.
 * @param from    The cycle of that service.
 */
static void satellite_arm_capture(SatelliteCapture_t *capture, uint64_t from)
{
    const uint64_t frame = capture->frame_cycles;

    if (frame == 0u)
    {
        (void) kernel_wake_planner_arm(capture->plan, capture->duty, from, KERNEL_WAKE_PLANNER_NEVER);
        return;
    }
    if (hardware_abstraction_layer_audio_capture_is_interrupt_driven())
    {
        (void) kernel_wake_planner_arm(capture->plan, capture->duty, from, from + 2u * frame);
        return;
    }
    (void) kernel_wake_planner_arm(capture->plan, capture->duty, from + frame / 2u, from + frame - frame / 8u);
}

/**
 * @brief The capture duty: pumps the ring where nothing else does, drains what waits in it, and arms
 *        the next window.
 *
 * @param context The SatelliteCapture_t.
 * @param now     The cycle the core woke at.
 */
static void satellite_serve_capture(void *context, uint64_t now)
{
    SatelliteCapture_t *const capture = (SatelliteCapture_t *) context;

    satellite_pump_capture_when_polled();
    satellite_drain_waiting_frames(capture->report);
    ++capture->services;
    satellite_arm_capture(capture, now);
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

/**
 * @brief The microjoules of @p nanojoules, the largest value past 32 bits, 4294 J: a loop longer than the counter's
 *        one wrap is measured short in any case (kernel/power/energy_counter.h).
 */
static uint32_t satellite_microjoules_of(uint64_t nanojoules)
{
    const uint64_t microjoules = nanojoules / SATELLITE_NANOJOULES_PER_MICROJOULE;

    return microjoules > UINT32_MAX ? UINT32_MAX : (uint32_t) microjoules;
}

bool kernel_satellite_app_run(uint32_t iterations, SatelliteReport_t *out)
{
    if (out == NULL)
        return false;

    SatelliteReport_t report = {0};
    const uint32_t published_wakes_before = kernel_tickless_wakes_planned_on_a_published_lead();
    const uint32_t bound_wakes_before = kernel_tickless_wakes_planned_on_the_bound();

    kernel_processor_sleep_initialize();
    (void) kernel_processor_sleep_request_depth(PROCESSOR_SLEEP_DEPTH_DEEPEST);
    kernel_frequency_scaling_initialize();
    kernel_frequency_scaling_begin_measurement();

    report.monitor_available = kernel_processor_sleep_has_monitor() ? 1u : 0u;
    report.scaling_available = kernel_frequency_scaling_available() ? 1u : 0u;
    report.audio_present = hardware_abstraction_layer_audio_initialize() ? 1u : 0u;

    SatelliteCapture_t capture = {.report = &report, .plan = &satellite_plan};
    kernel_wake_planner_initialize(&satellite_plan);
    capture.duty = kernel_wake_planner_declare(&satellite_plan, satellite_serve_capture, &capture);
    (void) kernel_tickless_enable(true, &satellite_plan);
    kernel_processor_sleep_begin_accounting();
    const uint32_t passes_before = kernel_reconciler_get_periodic_pass_count();
    capture.frame_cycles = kernel_tickless_cycles_from_microseconds(SATELLITE_FRAME_MICROSECONDS);
    satellite_probe_output_limiter(&report);

    const volatile uint32_t *const written = hardware_abstraction_layer_audio_capture_write_index();
    const uint32_t written_before = (written != NULL) ? *written : 0u;

    uint32_t energy_before = 0u;
    uint32_t energy_after = 0u;
    const bool energy_began = kernel_energy_counter_sample(&energy_before);
    const uint64_t loop_began = asmutils_read_timestamp_counter();
    satellite_arm_capture(&capture, loop_began);
    while (capture.services < iterations)
    {
        ++report.idle_iterations;
        report.slept_microseconds += kernel_tickless_sleep_on(&satellite_plan);
    }
    report.loop_microseconds = kernel_tickless_microseconds_from_cycles(asmutils_read_timestamp_counter() - loop_began);
    report.package_energy_known = energy_began && kernel_energy_counter_sample(&energy_after) ? 1u : 0u;
    report.package_microjoules =
        report.package_energy_known != 0u ?
            satellite_microjoules_of(kernel_energy_counter_nanojoules_between(energy_before, energy_after)) :
            0u;

    kernel_processor_sleep_close_session();
    report.frames_captured = (written != NULL) ? (*written - written_before) : 0u;
    report.capture_services = capture.services;
    report.wasted_wakes = satellite_plan.wasted_wakes;
    report.late_services = satellite_plan.late_services;
    report.batched_wakes = satellite_plan.batched_wakes;
    report.overshoot_microseconds = kernel_tickless_microseconds_from_cycles(satellite_plan.latest_overshoot);
    report.wake_lead_nanoseconds =
        (uint32_t) kernel_tickless_nanoseconds_from_cycles(kernel_tickless_wake_lead_cycles());
    report.wakes_planned_on_a_published_lead =
        kernel_tickless_wakes_planned_on_a_published_lead() - published_wakes_before;
    report.wakes_planned_on_the_bound = kernel_tickless_wakes_planned_on_the_bound() - bound_wakes_before;
    report.reconciler_passes = kernel_reconciler_get_periodic_pass_count() - passes_before;
    report.sleeps = kernel_processor_sleep_count();
    report.sleeps_skipped = kernel_processor_sleep_skipped_count();
    report.halts = kernel_processor_sleep_halt_count();
    report.duty_permille = kernel_processor_sleep_duty_cycle_permille();
    report.ticks_avoided = kernel_tickless_ticks_avoided();

    satellite_govern_on_measured_duty(&report);
    (void) hardware_abstraction_layer_audio_capture_stop();
    kernel_tickless_disable();
    kernel_wake_planner_withdraw(&satellite_plan, capture.duty);

    *out = report;
    return true;
}

const char *kernel_satellite_app_audio_name(void) { return hardware_abstraction_layer_audio_device_name(); }
