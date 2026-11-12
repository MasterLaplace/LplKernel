#include <kernel/acpi/interrupt_routing.h>
#include <kernel/core/reconciler.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/msi.h>
#include <kernel/cpu/pci.h>
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

#define POWER_FLOOR_AUDIO_CLASS    0x04u /**< A multimedia controller. */
#define POWER_FLOOR_AUDIO_SUBCLASS 0x03u /**< Its Intel HDA subclass. */
#define POWER_FLOOR_INTERRUPT_PIN  0x3Du /**< A PCI function's Interrupt Pin register: 1 to 4 for INTA to INTD. */
#define POWER_FLOOR_ENTRY_VECTOR   0xFFu /**< A redirection entry's vector bits. */
#define POWER_FLOOR_ENTRY_LOW      13u   /**< Its polarity bit: active-low. */
#define POWER_FLOOR_ENTRY_LEVEL    15u   /**< Its trigger bit: level-triggered. */
#define POWER_FLOOR_ENTRY_MASKED   16u   /**< Its mask bit. */
#define POWER_FLOOR_ENTRY_TARGET   24u   /**< Where its destination starts in its high register. */
#define POWER_FLOOR_ISA_IRQS       16u   /**< ISA IRQs 0 to 15, the lines of the 8259 pair. */

/**
 * Services of the capture the satellite node runs for: twenty buffers, 800 ms of audio, long enough for the
 * reconciler's pass, 64 ticks of a 100 Hz tick, to come due during the session and ride one of its wake-ups.
 */
#define POWER_FLOOR_ITERATIONS 20u

/** The capture's rate: its stream format 0x0210 is 16 kHz, mono, 16 bits. */
#define POWER_FLOOR_SAMPLE_RATE_HZ 16000u

/** One capture buffer, in microseconds, from its samples and its rate. */
#define POWER_FLOOR_FRAME_MICROSECONDS ((KERNEL_HAL_AUDIO_FRAME_SAMPLES * 1000000u) / POWER_FLOOR_SAMPLE_RATE_HZ)

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
 * @brief Does nothing: the handler an ISA IRQ is given for as long as a check needs it to look served.
 */
static void power_floor_ignore_interrupt(const InterruptFrame_t *frame) { (void) frame; }

/**
 * @brief Checks that the capture interrupt comes on the controller's 8259 line, as @p claim says.
 */
static void power_floor_check_capture_on_the_8259(KernelTest_t *test, const char *claim)
{
    kernel_test_check(test,
                      hardware_abstraction_layer_audio_capture_interrupt_vector() == 0u &&
                          hardware_abstraction_layer_audio_capture_interrupt_line() != KERNEL_HDA_NO_INTERRUPT_LINE,
                      claim);
    kernel_test_measure(test, "line", hardware_abstraction_layer_audio_capture_interrupt_line());
}

/**
 * @brief Checks that the capture line is refused, even to the vector that holds it, while an ISA IRQ the MADT maps to
 *        it has an 8259 handler; noted where no ISA IRQ free of a handler reaches the line.
 */
static void power_floor_check_an_isa_handler_takes_the_line(KernelTest_t *test,
                                                            const PeripheralComponentInterconnectDevice_t *device,
                                                            uint32_t slot, uint8_t vector, uint32_t line)
{
    for (uint8_t isa_irq = 0u; isa_irq < POWER_FLOOR_ISA_IRQS; ++isa_irq)
    {
        const uint8_t isa_vector = (uint8_t) (PIC_VECTOR_OFFSET_MASTER + isa_irq);
        uint32_t global_system_interrupt = 0u;
        uint16_t flags = 0u;
        uint32_t refused = 0u;

        if (!advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &global_system_interrupt,
                                                                             &flags) ||
            global_system_interrupt != line || interrupt_service_routine_get_handler(isa_vector) != NULL)
            continue;
        interrupt_service_routine_register_handler(isa_vector, power_floor_ignore_interrupt);
        const bool taken =
            input_output_advanced_programmable_interrupt_controller_route_device(device, slot, vector, &refused);
        interrupt_service_routine_register_handler(isa_vector, NULL);
        kernel_test_check(test, !taken && refused == INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_NO_LINE,
                          "an ISA IRQ the 8259 serves on that line takes it from the controller");
        kernel_test_measure(test, "isa_irq", isa_irq);
        return;
    }
    kernel_test_note(test, "no ISA IRQ free of a handler reaches the line");
}

/**
 * @brief Checks that the vector that holds the capture line takes it again aimed at another core's slot, its entry then
 *        naming that core, and gives it back to @p slot; noted on a machine of one core.
 */
static void power_floor_check_the_line_follows_its_slot(KernelTest_t *test,
                                                        const PeripheralComponentInterconnectDevice_t *device,
                                                        uint32_t slot, uint8_t vector, uint32_t line)
{
    const uint32_t other = slot == 0u ? 1u : 0u;
    const uint32_t other_apic_id = cpu_topology_get_apic_id_at_slot(other);
    uint32_t taken = 0u;
    uint32_t back = 0u;
    uint32_t low = 0u;
    uint32_t high = 0u;

    if (other_apic_id == CPU_TOPOLOGY_NO_PROCESSOR)
    {
        kernel_test_note(test, "one core: the line is not aimed elsewhere");
        return;
    }
    kernel_test_check(
        test,
        input_output_advanced_programmable_interrupt_controller_route_device(device, other, vector, &taken) &&
            taken == line &&
            input_output_advanced_programmable_interrupt_controller_debug_read_line(line, &low, &high) &&
            (high >> POWER_FLOOR_ENTRY_TARGET) == other_apic_id,
        "the vector that holds the line takes it again, aimed at the slot it names");
    kernel_test_check(
        test,
        input_output_advanced_programmable_interrupt_controller_route_device(device, slot, vector, &back) &&
            back == line &&
            input_output_advanced_programmable_interrupt_controller_debug_read_line(line, &low, &high) &&
            (high >> POWER_FLOOR_ENTRY_TARGET) == cpu_topology_get_apic_id_at_slot(slot),
        "and gives it back to the core that took the ring");
    kernel_test_measure(test, "other_slot", other);
}

/**
 * @brief Checks the I/O APIC line of a controller without MSI: the one the boot's published routes give its pin, its
 *        entry carrying the capture vector with the route's trigger and polarity, unmasked, to the core that took the
 *        ring, which alone it interrupted, more than once; refused to another vector and while an ISA IRQ the 8259
 *        serves reaches it; aimed at the slot the vector that holds it names.
 */
static void power_floor_check_capture_line(KernelTest_t *test, const PeripheralComponentInterconnectDevice_t *device)
{
    const uint8_t pin = peripheral_component_interconnect_config_read_byte(device->bus, device->device,
                                                                           device->function, POWER_FLOOR_INTERRUPT_PIN);
    const uint8_t vector = hardware_abstraction_layer_audio_capture_interrupt_vector();
    const uint32_t slot = hardware_abstraction_layer_audio_capture_interrupt_slot();
    const uint32_t line = hardware_abstraction_layer_audio_capture_global_system_interrupt();
    AdvancedConfigurationAndPowerInterfaceInterruptRoute_t route = {0};
    uint32_t low = 0u;
    uint32_t high = 0u;
    uint32_t low_after = 0u;
    uint32_t high_after = 0u;
    uint32_t refused = 0u;
    uint32_t elsewhere = 0u;

    kernel_test_check(
        test, pin != 0u && input_output_advanced_programmable_interrupt_controller_find_device_route(device, &route),
        "the published routes name the controller's pin, behind its bridges or on its root bus");
    kernel_test_check(test, vector != 0u && line == route.interrupt,
                      "without MSI, it comes on the I/O APIC line its pin's route gives");
    kernel_test_check(
        test,
        input_output_advanced_programmable_interrupt_controller_debug_read_line(line, &low, &high) &&
            (low & POWER_FLOOR_ENTRY_VECTOR) == vector && ((low >> POWER_FLOOR_ENTRY_LEVEL) & 1u) == route.level &&
            ((low >> POWER_FLOOR_ENTRY_LOW) & 1u) == route.active_low &&
            ((low >> POWER_FLOOR_ENTRY_MASKED) & 1u) == 0u &&
            (high >> POWER_FLOOR_ENTRY_TARGET) == cpu_topology_get_apic_id_at_slot(slot),
        "its entry carries the vector, the route's trigger and polarity, unmasked, to the core that took the ring");
    for (uint32_t other = 0u; other < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++other)
        elsewhere += other != slot ? device_interrupt_count(vector, other) : 0u;
    kernel_test_check(test, device_interrupt_count(vector, slot) >= 2u,
                      "the line interrupted that core more than once: a level-triggered line whose end of interrupt "
                      "never reached the I/O APIC would have fired once");
    kernel_test_check(test, elsewhere == 0u, "and no other core");
    kernel_test_check(
        test,
        !input_output_advanced_programmable_interrupt_controller_route_device(device, slot, (uint8_t) (vector + 1u),
                                                                              &refused) &&
            refused == INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_NO_LINE &&
            input_output_advanced_programmable_interrupt_controller_debug_read_line(line, &low_after, &high_after) &&
            low_after == low && high_after == high,
        "another vector is refused the line, its entry left as it was");
    kernel_test_measure(test, "global_system_interrupt", line);
    kernel_test_measure(test, "level", route.level);
    kernel_test_measure(test, "active_low", route.active_low);
    kernel_test_measure(test, "vector", vector);
    kernel_test_measure(test, "slot", slot);
    kernel_test_measure(test, "interrupts", device_interrupt_count(vector, slot));
    power_floor_check_an_isa_handler_takes_the_line(test, device, slot, vector, line);
    power_floor_check_the_line_follows_its_slot(test, device, slot, vector, line);
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
    kernel_test_measure(test, "sleep_depths_available", kernel_processor_sleep_available_depths());
    kernel_test_measure(test, "sleep_depth_active", kernel_processor_sleep_depth());
    kernel_test_measure(test, "sleep_depth_clamped", kernel_processor_sleep_clamped_count());
    kernel_test_measure(test, "sleep_depths_refused", kernel_processor_sleep_refused_depths());
    kernel_test_measure(test, "port_sleeps", kernel_processor_sleep_port_sleep_count());
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
 * @brief Where no interrupt brings the buffers, the capture wakes the core on its own deadline once a buffer, before
 *        the controller writes over a half: no wake-up of the session serves nothing, each pump runs past its
 *        latest by less than the margin its window keeps, and none is more than a buffer after the last.
 */
KERNEL_TEST(the_capture_keeps_its_own_deadline_where_nothing_interrupts)
{
    const SatelliteReport_t *const report = power_floor_run_once();

    if (report == NULL ||
        (report->audio_present == 1u && hardware_abstraction_layer_audio_capture_is_interrupt_driven()))
    {
        kernel_test_skip(test, "the controller's interrupt brought the buffers");
        return;
    }

    kernel_test_check(test, report->wasted_wakes == 0u, "no wake-up served nothing");
    kernel_test_check(test, report->overshoot_microseconds < POWER_FLOOR_FRAME_MICROSECONDS / 8u,
                      "each pump ran past its latest by less than an eighth of a buffer, the window's margin");
    kernel_test_check(test, report->sleeps <= report->capture_services + 1u, "one wake-up per service");
    kernel_test_check(test,
                      report->slept_microseconds <= report->capture_services * (POWER_FLOOR_FRAME_MICROSECONDS -
                                                                                POWER_FLOOR_FRAME_MICROSECONDS / 8u +
                                                                                report->overshoot_microseconds),
                      "each at most seven eighths of a buffer and a wake-up's latency after the last");
    kernel_test_measure(test, "sleeps", report->sleeps);
    kernel_test_measure(test, "capture_services", report->capture_services);
    kernel_test_measure(test, "slept_microseconds", report->slept_microseconds);
    kernel_test_measure(test, "late_services", report->late_services);
    kernel_test_measure(test, "overshoot_microseconds", report->overshoot_microseconds);
    kernel_test_measure(test, "wake_lead_ns", report->wake_lead_nanoseconds);
    kernel_test_measure(test, "wakes_planned_on_published_lead", report->wakes_planned_on_a_published_lead);
    kernel_test_measure(test, "wakes_planned_on_bound", report->wakes_planned_on_the_bound);
}

/**
 * @brief The reconciler's pass, which the stopped tick no longer runs, comes due once the session has lasted a
 *        tick period, and rides a wake-up of the capture: the wake-up serves both, and the pass costs none of
 *        its own.
 */
KERNEL_TEST(the_reconciler_pass_rides_a_capture_wake_up)
{
    const SatelliteReport_t *const report = power_floor_run_once();
    const uint32_t tick_hz = kernel_tickless_nominal_frequency_hz();

    if (report == NULL || report->ticks_avoided == 0u || tick_hz == 0u)
    {
        kernel_test_skip(test, "the session did not stop the tick");
        return;
    }

    const uint32_t period_microseconds = (KERNEL_RECONCILER_TICK_SAMPLE_PERIOD * 1000000u) / tick_hz;
    kernel_test_check(test, report->slept_microseconds >= period_microseconds, "the session lasted a tick period");
    kernel_test_check(test, report->reconciler_passes >= 1u, "the pass ran with the tick stopped");
    kernel_test_check(test, report->batched_wakes >= report->reconciler_passes,
                      "each time on a wake-up that also served the capture");
    kernel_test_check(test, report->wasted_wakes == 0u && report->sleeps <= report->capture_services + 1u,
                      "so it cost no wake-up of its own");
    kernel_test_measure(test, "reconciler_passes", report->reconciler_passes);
    kernel_test_measure(test, "batched_wakes", report->batched_wakes);
    kernel_test_measure(test, "period_microseconds", period_microseconds);
}

/**
 * @brief Where the controller's interrupt ends the sleeps, the capture's window takes its wake-ups: none of
 *        the session's serves nothing, none serves a duty late, no buffer is lost, and the core wakes once
 *        per buffer that arrived, give or take the one in flight.
 *
 * @details Waking on the capture's own deadline and again on the interrupt that brings the buffer is two
 *          wake-ups for one, and the one on the deadline finds the ring empty: it is the waste a window
 *          that holds the interrupt removes.
 */
KERNEL_TEST(the_capture_rides_the_controllers_wake_ups)
{
    const SatelliteReport_t *const report = power_floor_run_once();

    if (report == NULL || report->audio_present != 1u ||
        !hardware_abstraction_layer_audio_capture_is_interrupt_driven())
    {
        kernel_test_skip(test, "no interrupt-driven capture ran on this machine");
        return;
    }

    kernel_test_check(test, report->wasted_wakes == 0u, "no wake-up of the session served nothing");
    kernel_test_check(test, report->late_services == 0u, "no duty was served late");
    kernel_test_check(test, hardware_abstraction_layer_audio_capture_overruns() == 0u, "no buffer was lost");
    kernel_test_check(test, report->sleeps <= report->frames_captured + 1u,
                      "the core wakes once per buffer that arrived, give or take the one in flight");

    kernel_test_measure(test, "sleeps", report->sleeps);
    kernel_test_measure(test, "frames_captured", report->frames_captured);
    kernel_test_measure(test, "capture_services", report->capture_services);
    kernel_test_measure(test, "wasted_wakes", report->wasted_wakes);
    kernel_test_measure(test, "late_services", report->late_services);
    kernel_test_measure(test, "wakes_per_frame_permille",
                        report->frames_captured != 0u ? (report->sleeps * 1000u) / report->frames_captured : 0u);
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

/**
 * @brief The capture interrupt reaches the core that took the ring, and no other: as an MSI message when the controller
 *        has the MSI capability, on its I/O APIC line otherwise, and on its 8259 line where the boot keeps the PIC
 *        model.
 *
 * @details QEMU's intel-hda offers MSI unless told `msi=off`: `qemu-system-i386 -machine q35 -smp 2 -audiodev
 *          none,id=lplaudio -device intel-hda -device hda-duplex,audiodev=lplaudio -kernel kernel/lpl.kernel` routes a
 *          message and `-device intel-hda,msi=off` the I/O APIC line, on a satellite kernel (`./build.sh
 *          --satellite`); behind a bridge, `-device pcie-root-port,id=lplport,chassis=1 -device
 *          intel-hda,msi=off,bus=lplport` the line its pin swizzles to; `-cpu max,-hypervisor` keeps xAPIC mode and
 *          changes none of them. The 8259 line is left to
 *          x2APIC mode handed over by firmware on bare metal, which no device interrupt reaches and QEMU's firmware
 *          never hands over.
 */
KERNEL_TEST(the_capture_interrupt_reaches_the_core_that_took_the_ring)
{
    const SatelliteReport_t *const report = power_floor_run_once();
    const PeripheralComponentInterconnectDevice_t *const device =
        peripheral_component_interconnect_find_by_class(POWER_FLOOR_AUDIO_CLASS, POWER_FLOOR_AUDIO_SUBCLASS);
    const uint8_t vector = hardware_abstraction_layer_audio_capture_interrupt_vector();
    const uint32_t slot = hardware_abstraction_layer_audio_capture_interrupt_slot();
    uint32_t elsewhere = 0u;

    if (device == NULL || report == NULL || report->audio_present != 1u)
    {
        kernel_test_skip(test, "no Intel HDA capture ran on this machine");
        return;
    }

    const bool has_msi =
        peripheral_component_interconnect_find_capability(device->bus, device->device, device->function,
                                                          MESSAGE_SIGNALED_INTERRUPT_CAPABILITY) != 0u;
    const bool delivers = device_interrupt_platform_delivers();
    const bool input_output_apic = advanced_configuration_and_power_interface_madt_is_available() &&
                                   advanced_configuration_and_power_interface_madt_get_io_apic_count() != 0u;

    kernel_test_check(test, hardware_abstraction_layer_audio_capture_is_interrupt_driven(),
                      "capture is driven by the controller's interrupt");
    kernel_test_check(test,
                      (advanced_configuration_and_power_interface_interrupt_routing()->model ==
                       ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC) ==
                          (input_output_apic && delivers),
                      "the boot took the APIC model exactly when the MADT declares an I/O APIC and a device interrupt "
                      "reaches a core");
    if (!delivers)
    {
        power_floor_check_capture_on_the_8259(test,
                                              "where no device interrupt reaches a core, it comes on the 8259 line");
        return;
    }
    if (!has_msi && !input_output_apic)
    {
        power_floor_check_capture_on_the_8259(test, "without MSI nor an I/O APIC, it comes on the 8259 line");
        return;
    }
    if (!has_msi)
    {
        power_floor_check_capture_line(test, device);
        return;
    }
    for (uint32_t other = 0u; other < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++other)
        elsewhere += other != slot ? device_interrupt_count(vector, other) : 0u;
    kernel_test_check(test, vector != 0u, "with MSI, it comes as a message");
    kernel_test_check(test, device_interrupt_count(vector, slot) > 0u,
                      "the message interrupted the core that took the ring");
    kernel_test_check(test, elsewhere == 0u, "and no other core");
    kernel_test_measure(test, "vector", vector);
    kernel_test_measure(test, "slot", slot);
    kernel_test_measure(test, "messages", device_interrupt_count(vector, slot));
}
