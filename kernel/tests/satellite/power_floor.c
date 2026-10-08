#include <kernel/core/reconciler.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/pic.h>
#include <kernel/drivers/hda.h>
#include <kernel/hal/hal_audio.h>
#include <kernel/lib/asmutils.h>
#include <kernel/power/frequency_scaling.h>
#include <kernel/power/processor_sleep.h>
#include <kernel/power/tickless.h>
#include <kernel/power/wakeup_accounting.h>
#include <kernel/satellite/satellite_app.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(power_floor, KERNEL_TEST_STAGE_BOOTED);

/** Frames the satellite node runs for: 320 ms of audio, a sleep to each frame's deadline. */
#define POWER_FLOOR_ITERATIONS 8u

/** Ticks waited after the run: 100 ms at 100 Hz, two capture periods and a half. */
#define POWER_FLOOR_TICKS_AFTER_THE_RUN 10u

/** Cycles after which a wait for ticks gives up: seconds on any machine that boots this image. */
#define POWER_FLOOR_WAIT_CYCLE_BUDGET 8000000000ull

static SatelliteReport_t power_floor_report;
static bool power_floor_ran = false;
static bool power_floor_came_up = false;
static uint8_t power_floor_tick_owner_was_apic = 0u;

/**
 * @brief Runs the satellite node once for the whole suite, and returns what it measured.
 *
 * @details Every test reads the same run: a second one would be another session. The run resets
 *          the wake-up accounting, so the wake-ups it counts are its own. Its session is the last
 *          one closed, the one the reconciler judges at the end of the boot, because this suite runs
 *          after the reconciler's test, which opens one of its own.
 *
 * @return The measurements, or NULL when the power floor did not come up.
 */
static const SatelliteReport_t *power_floor_run_once(void)
{
    if (!power_floor_ran)
    {
        power_floor_ran = true;
        power_floor_tick_owner_was_apic = interrupt_request_is_timer_owner_apic();
        power_floor_came_up = kernel_satellite_app_run(POWER_FLOOR_ITERATIONS, &power_floor_report);
    }
    return power_floor_came_up ? &power_floor_report : NULL;
}

/**
 * @brief Waits until the periodic tick has fired @p ticks times, or gives up.
 *
 * @details Spins on the timestamp counter rather than halting: if the tick never came back, a halt
 *          could wait for an interrupt that never comes, and the boot would hang instead of failing.
 *
 * @param ticks Ticks to wait for.
 * @return The ticks that passed, fewer than @p ticks when the wait gave up.
 */
static uint32_t power_floor_wait_for_ticks(uint32_t ticks)
{
    const uint32_t first_tick = interrupt_request_get_tick_count();
    const uint64_t first_cycle = asmutils_read_timestamp_counter();

    while ((uint32_t) (interrupt_request_get_tick_count() - first_tick) < ticks &&
           asmutils_read_timestamp_counter() - first_cycle < POWER_FLOOR_WAIT_CYCLE_BUDGET)
        asmutils_pause();
    return interrupt_request_get_tick_count() - first_tick;
}

/**
 * @brief The power floor, measured rather than promised: the processor really slept, ticks were
 *        avoided and counted against the tick in force, the session spent most of its time asleep,
 *        and the tick came back to the timer that owned it, at the rate it was taken.
 *
 * @details A profile whose whole purpose is to spend nothing has to report a number, or "it idles
 *          cheaply" cannot be told from a spin loop. The ceiling is the reconciler's, and the duty
 *          cycle it judges must be this session's. The sleep depth the processor offers is measured,
 *          not checked: QEMU advertises no MONITOR/MWAIT, and pinning that would pin the emulator.
 */
KERNEL_TEST(the_idle_loop_sleeps_under_its_ceiling)
{
    const SatelliteReport_t *const report = power_floor_run_once();

    kernel_test_check(test, report != NULL, "the power floor comes up");
    if (report == NULL)
        return;
    kernel_test_check(test, report->sleeps > 0u, "the processor actually slept");
    kernel_test_check(test, report->ticks_avoided > 0u, "periodic interrupts were avoided");
    kernel_test_check(test, report->duty_permille <= KERNEL_RECONCILER_DUTY_CYCLE_CEILING_PERMILLE,
                      "the idle loop is mostly asleep");
    kernel_test_check(test, kernel_processor_sleep_published_duty_cycle_permille() == report->duty_permille,
                      "the duty cycle the reconciler judges is this session's");
    kernel_test_check(test,
                      (uint64_t) report->ticks_avoided * (1000000u / interrupt_request_get_timer_frequency_hz()) <=
                          kernel_tickless_slept_microseconds(),
                      "the ticks avoided are counted against the tick in force");
    kernel_test_check(test,
                      !kernel_tickless_enabled() &&
                          kernel_tickless_nominal_frequency_hz() == interrupt_request_get_timer_frequency_hz(),
                      "the tick comes back at the rate it was taken");
    kernel_test_check(test, interrupt_request_is_timer_owner_apic() == power_floor_tick_owner_was_apic,
                      "the timer that owned the tick still owns it");
    kernel_test_check(test, power_floor_wait_for_ticks(2u) >= 2u, "and the tick runs again");

    kernel_test_measure(test, "iterations", report->idle_iterations);
    kernel_test_measure(test, "sleeps", report->sleeps);
    kernel_test_measure(test, "sleeps_skipped", report->sleeps_skipped);
    kernel_test_measure(test, "halts", report->halts);
    kernel_test_measure(test, "duty_permille", report->duty_permille);
    kernel_test_measure(test, "ticks_avoided", report->ticks_avoided);
    kernel_test_measure(test, "slept_microseconds", (uint32_t) kernel_tickless_slept_microseconds());
    kernel_test_measure(test, "early_wakes", kernel_tickless_early_wakes());
    kernel_test_measure(test, "tick_owner_apic", interrupt_request_is_timer_owner_apic());
    kernel_test_measure(test, "monitor_available", report->monitor_available);
    kernel_test_measure(test, "sleep_hints_available", kernel_processor_sleep_available_hints());
    kernel_test_measure(test, "sleep_hint_active", kernel_processor_sleep_active_hint());
    kernel_test_measure(test, "sleep_hint_clamped", kernel_processor_sleep_clamped_count());
    kernel_test_measure(test, "interrupt_break", kernel_processor_sleep_has_interrupt_break() ? 1u : 0u);
    kernel_test_measure(test, "scaling_available", report->scaling_available);
    kernel_test_measure(test, "scaling_refused", report->scaling_refused);
    kernel_test_measure(test, "governed_state", report->governed_state);
    kernel_test_measure(test, "frequency_feedback", kernel_frequency_scaling_feedback_available() ? 1u : 0u);
    kernel_test_measure(test, "effective_permille", report->effective_permille);
}

/**
 * @brief Every wake-up of the session is accounted for, and the books balance.
 *
 * @details The sleep count is checked with the law, because 0 == 0 holds for a session that never
 *          slept. Which vector woke the core is measured, not checked: a new wake source is a
 *          driver that started waking the core, which is progress rather than a defect.
 */
KERNEL_TEST(every_wake_up_of_the_session_is_accounted_for)
{
    const SatelliteReport_t *const report = power_floor_run_once();

    kernel_test_check(test, report != NULL, "the power floor comes up");
    kernel_test_check(test, kernel_wakeup_accounting_get_sleep_count() > 0u, "the session really slept");
    kernel_test_check(test, kernel_wakeup_accounting_get_attributed_count() > 0u,
                      "the kernel can name a real wake source");
    kernel_test_check(test, kernel_wakeup_accounting_get_unattributed_count() == 0u,
                      "every real wake-up was accounted for");
    kernel_test_check(test, kernel_wakeup_accounting_get_double_arm_count() == 0u, "no sleep armed itself twice");
    kernel_test_check(test, kernel_wakeup_accounting_conserves(), "the session's books balance");

    kernel_test_measure(test, "sleeps", kernel_wakeup_accounting_get_sleep_count());
    kernel_test_measure(test, "attributed", kernel_wakeup_accounting_get_attributed_count());
    kernel_test_measure(test, "monitor_wakes", kernel_wakeup_accounting_get_monitor_wake_count());
    kernel_test_measure(test, "sources", kernel_wakeup_accounting_get_source_count());
}

/**
 * @brief Nothing this kernel can emit is louder than its compile-time ceiling, checked by pushing a
 *        full-scale square wave through the limiter before there is anything to play.
 */
KERNEL_TEST(nothing_plays_louder_than_the_ceiling)
{
    const SatelliteReport_t *const report = power_floor_run_once();

    kernel_test_check(test, report != NULL, "the power floor comes up");
    if (report == NULL)
        return;
    kernel_test_check(test, report->output_ceiling > 0u && report->limiter_peak <= report->output_ceiling,
                      "nothing can play louder than the ceiling");
    kernel_test_check(test, report->limiter_clipped > 0u, "the limiter actually clamps");

    kernel_test_measure(test, "output_ceiling", report->output_ceiling);
    kernel_test_measure(test, "limiter_peak", report->limiter_peak);
    kernel_test_measure(test, "limiter_clipped", report->limiter_clipped);
}

/**
 * @brief The audio controller answers every verb, its capture stream delivers buffers by
 *        interrupt during the session, and stops with it.
 *
 * @details The capture claims are checked together because each alone can pass for the wrong
 *          reason: a converter can be found on a stream that never runs, and a stream can run while
 *          delivering nothing. The level is measured, not checked: QEMU's codec has no microphone
 *          behind it; the note names the codec. The command rings either side of the first
 *          unanswered verb are measured only when one went unanswered, because the registers settle
 *          what reading the driver cannot.
 */
KERNEL_TEST(the_capture_stream_delivers_buffers)
{
    const SatelliteReport_t *const report = power_floor_run_once();
    const IntelHighDefinitionAudioState_t *const controller = intel_high_definition_audio_state();

    if (!controller->controller_present)
    {
        kernel_test_skip(test, "no Intel HDA controller on this machine");
        return;
    }
    kernel_test_check(test, report != NULL && report->audio_present == 1u, "the audio seam reports capture working");
    kernel_test_check(test, controller->rings_running, "the controller's rings run");
    kernel_test_check(test, controller->codec_mask != 0u, "a codec announced itself");
    kernel_test_check(test, controller->codec_vendor[0] != 0u, "the codec answered a verb");
    kernel_test_check(test, controller->verb_timeouts == 0u, "every codec verb was answered");
    kernel_test_check(test, controller->capture_converter != 0u && report != NULL && report->frames_captured > 0u,
                      "the capture stream delivers buffers");
    kernel_test_check(test, hardware_abstraction_layer_audio_capture_is_interrupt_driven(),
                      "capture is driven by the controller's interrupt");
    kernel_test_check(test, hardware_abstraction_layer_audio_capture_interrupt_count() > 0u,
                      "the capture handler actually ran");
    kernel_test_check(test, hardware_abstraction_layer_audio_capture_overruns() == 0u,
                      "a full capture ring refused rather than overwrote");

    const uint32_t interrupts_after_the_run = hardware_abstraction_layer_audio_capture_interrupt_count();
    const uint32_t ticks_waited = power_floor_wait_for_ticks(POWER_FLOOR_TICKS_AFTER_THE_RUN);
    kernel_test_check(test,
                      !controller->capture_running && ticks_waited >= POWER_FLOOR_TICKS_AFTER_THE_RUN &&
                          hardware_abstraction_layer_audio_capture_interrupt_count() == interrupts_after_the_run,
                      "the capture stops with the session");

    uint8_t stream_status = 0u;
    uint32_t interrupt_status = 0u;
    uint32_t position = 0u;
    const uint8_t line = hardware_abstraction_layer_audio_capture_interrupt_line();

    intel_high_definition_audio_capture_registers(&stream_status, &interrupt_status, &position);
    kernel_test_measure(test, "version", controller->major_version);
    kernel_test_measure(test, "input_streams", controller->input_streams);
    kernel_test_measure(test, "output_streams", controller->output_streams);
    kernel_test_measure_hexadecimal(test, "codec_mask", controller->codec_mask);
    kernel_test_measure_hexadecimal(test, "codec_vendor", controller->codec_vendor[0]);
    kernel_test_measure(test, "verbs_sent", controller->verbs_sent);
    kernel_test_measure(test, "responses_read", controller->responses_read);
    kernel_test_measure(test, "widgets_walked", controller->widgets_walked);
    kernel_test_measure(test, "capture_converter", controller->capture_converter);
    kernel_test_measure(test, "capture_pin", controller->capture_pin);
    kernel_test_measure(test, "playback_pin", controller->playback_pin);
    kernel_test_measure(test, "outputs_muted", controller->outputs_muted);
    kernel_test_measure(test, "capture_wraps", controller->capture_wraps);
    kernel_test_measure(test, "frames_captured", report != NULL ? report->frames_captured : 0u);
    kernel_test_measure(test, "capture_level", report != NULL ? report->capture_peak : 0u);
    kernel_test_measure(test, "capture_line", line);
    kernel_test_measure(test, "capture_interrupts", hardware_abstraction_layer_audio_capture_interrupt_count());
    kernel_test_measure_hexadecimal(test, "stream_status", stream_status);
    kernel_test_measure_hexadecimal(test, "interrupt_status", interrupt_status);
    kernel_test_measure(test, "position", position);
    kernel_test_measure(test, "pic_in_service", programmable_interrupt_controller_is_in_service(line));
    kernel_test_note(test, kernel_satellite_app_audio_name());
    if (!controller->probe_captured)
        return;
    kernel_test_measure_hexadecimal(test, "probe_command", controller->probe_command);
    kernel_test_measure(test, "probe_command_write_pointer_before", controller->probe_before.command_write_pointer);
    kernel_test_measure(test, "probe_command_read_pointer_before", controller->probe_before.command_read_pointer);
    kernel_test_measure(test, "probe_response_write_pointer_before", controller->probe_before.response_write_pointer);
    kernel_test_measure(test, "probe_response_read_pointer_before",
                        controller->probe_before.response_read_pointer_shadow);
    kernel_test_measure(test, "probe_command_write_pointer", controller->probe_after.command_write_pointer);
    kernel_test_measure(test, "probe_command_read_pointer", controller->probe_after.command_read_pointer);
    kernel_test_measure(test, "probe_response_write_pointer", controller->probe_after.response_write_pointer);
    kernel_test_measure_hexadecimal(test, "probe_command_ring_status", controller->probe_after.command_ring_status);
    kernel_test_measure_hexadecimal(test, "probe_response_ring_status", controller->probe_after.response_ring_status);
}
