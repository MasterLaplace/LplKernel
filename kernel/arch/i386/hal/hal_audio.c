#include <kernel/hal/hal_audio.h>

#include <kernel/cpu/irq.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/drivers/hda.h>

static int16_t hal_audio_ring[KERNEL_HAL_AUDIO_RING_FRAMES][KERNEL_HAL_AUDIO_FRAME_SAMPLES];
static volatile uint32_t hal_audio_write_index = 0u;
static volatile uint32_t hal_audio_read_index = 0u;
static volatile uint32_t hal_audio_overruns = 0u;

/** Where a half goes when the ring is full: drained from the controller, then refused. */
static int16_t hal_audio_discard[KERNEL_HAL_AUDIO_FRAME_SAMPLES];

static uint8_t hal_audio_interrupt_line = KERNEL_HDA_NO_INTERRUPT_LINE;
static bool hal_audio_interrupt_driven = false;
static volatile uint32_t hal_audio_interrupts = 0u;

static bool hal_audio_codec_present = false;
static const char *hal_audio_name = "absent";
static uint32_t hal_audio_gain_permille = 1000u;
static uint32_t hal_audio_clipped = 0u;

/**
 * @brief Moves a completed half into the ring, from interrupt context.
 *
 * @note A PCI line is level-triggered and may be shared, so the acknowledge decides
 *       whether this one was ours, and the end-of-interrupt goes out either way.
 *
 * @param frame Unused.
 */
static void hal_audio_capture_interrupt_handler(const InterruptFrame_t *frame)
{
    (void) frame;

    if (intel_high_definition_audio_acknowledge_capture_interrupt())
    {
        ++hal_audio_interrupts;
        (void) hardware_abstraction_layer_audio_capture_pump();
    }

    programmable_interrupt_controller_send_end_of_interrupt(hal_audio_interrupt_line);
}

/**
 * @brief Is @p line one whose end-of-interrupt this handler knows how to send?
 *
 * @details Only a PIC line, never the cascade. An IOAPIC-owned line needs its
 *          end-of-interrupt elsewhere, and sending the wrong one blocks the line after
 *          its first interrupt: the mouse paid for exactly that.
 *
 * @param line Legacy interrupt line the controller reports.
 * @return true when the line can be routed to the capture handler.
 */
static bool hal_audio_line_is_acknowledgeable(uint8_t line)
{
    return line < 16u && line != 2u && !interrupt_request_is_line_owner_apic(line);
}

/**
 * @brief Routes the controller's interrupt to the handler, or declines and says so.
 * @return true when capture is now interrupt-driven.
 */
static bool hal_audio_route_capture_interrupt(void)
{
    const uint8_t line = intel_high_definition_audio_capture_interrupt_line();
    if (!hal_audio_line_is_acknowledgeable(line))
        return false;

    const uint8_t vector = (uint8_t) (PIC_VECTOR_OFFSET_MASTER + line);
    const isr_handler_t present = interrupt_service_routine_get_handler(vector);
    if (present != NULL && present != hal_audio_capture_interrupt_handler)
        return false;

    hal_audio_interrupt_line = line;
    interrupt_service_routine_register_handler(vector, hal_audio_capture_interrupt_handler);
    if (!intel_high_definition_audio_enable_capture_interrupt())
        return false;

    programmable_interrupt_controller_clear_mask(line);
    return true;
}

bool hardware_abstraction_layer_audio_initialize(void)
{
    hal_audio_write_index = 0u;
    hal_audio_read_index = 0u;
    hal_audio_overruns = 0u;
    hal_audio_interrupt_driven = false;
    hal_audio_interrupts = 0u;
    hal_audio_codec_present = false;
    hal_audio_name = "absent";

    IntelHighDefinitionAudioState_t controller;
    const bool answered = intel_high_definition_audio_initialize(&controller);

    if (controller.capture_running)
    {
        hal_audio_codec_present = true;
        hal_audio_name = "intel-hda";
        hal_audio_interrupt_driven = hal_audio_route_capture_interrupt();
    }
    else if (answered)
        hal_audio_name = "intel-hda (codec answered, no capture stream)";
    else if (controller.controller_running)
        hal_audio_name = "intel-hda (no codec answered)";
    else if (controller.controller_present)
        hal_audio_name = "intel-hda (reset failed)";

    return hal_audio_codec_present;
}

uint32_t hardware_abstraction_layer_audio_capture_pump(void)
{
    if (!hal_audio_codec_present)
        return 0u;

    uint32_t moved = 0u;
    for (;;)
    {
        const bool full = (hal_audio_write_index - hal_audio_read_index) >= KERNEL_HAL_AUDIO_RING_FRAMES;
        int16_t *const destination =
            full ? hal_audio_discard : hal_audio_ring[hal_audio_write_index % KERNEL_HAL_AUDIO_RING_FRAMES];

        const uint32_t samples = intel_high_definition_audio_poll_capture(destination, KERNEL_HAL_AUDIO_FRAME_SAMPLES);
        if (samples == 0u)
            break;

        if (full)
        {
            ++hal_audio_overruns;
            continue;
        }

        ++hal_audio_write_index;
        ++moved;
    }
    return moved;
}

bool hardware_abstraction_layer_audio_present(void) { return hal_audio_codec_present; }

const char *hardware_abstraction_layer_audio_device_name(void) { return hal_audio_name; }

uint32_t hardware_abstraction_layer_audio_sample_rate(void) { return hal_audio_codec_present ? 16000u : 0u; }

const volatile uint32_t *hardware_abstraction_layer_audio_capture_write_index(void)
{
    return hal_audio_codec_present ? &hal_audio_write_index : NULL;
}

uint32_t hardware_abstraction_layer_audio_capture_take(int16_t *out, uint32_t capacity)
{
    if (out == NULL || capacity < KERNEL_HAL_AUDIO_FRAME_SAMPLES)
        return 0u;

    const uint32_t write = hal_audio_write_index;
    const uint32_t read = hal_audio_read_index;
    if (write == read)
        return 0u;

    const uint32_t slot = read % KERNEL_HAL_AUDIO_RING_FRAMES;
    for (uint32_t i = 0u; i < KERNEL_HAL_AUDIO_FRAME_SAMPLES; ++i)
        out[i] = hal_audio_ring[slot][i];

    hal_audio_read_index = read + 1u;
    return KERNEL_HAL_AUDIO_FRAME_SAMPLES;
}

uint32_t hardware_abstraction_layer_audio_set_output_gain_permille(uint32_t permille)
{
    hal_audio_gain_permille = permille > 1000u ? 1000u : permille;
    return hal_audio_gain_permille;
}

uint32_t hardware_abstraction_layer_audio_output_gain_permille(void) { return hal_audio_gain_permille; }

uint32_t hardware_abstraction_layer_audio_limit(const int16_t *samples, uint32_t count, int16_t *out)
{
    if (samples == NULL || out == NULL)
        return 0u;

    uint32_t clipped = 0u;
    for (uint32_t i = 0u; i < count; ++i)
    {
        int32_t scaled = ((int32_t) samples[i] * (int32_t) hal_audio_gain_permille) / 1000;

        if (scaled > KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE)
        {
            scaled = KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE;
            ++clipped;
        }
        else if (scaled < -KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE)
        {
            scaled = -KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE;
            ++clipped;
        }
        out[i] = (int16_t) scaled;
    }

    hal_audio_clipped += clipped;
    return clipped;
}

uint32_t hardware_abstraction_layer_audio_clipped_samples(void) { return hal_audio_clipped; }

bool hardware_abstraction_layer_audio_playback_submit(const int16_t *samples, uint32_t count)
{
    static int16_t limited[KERNEL_HAL_AUDIO_FRAME_SAMPLES];
    const uint32_t take = count < KERNEL_HAL_AUDIO_FRAME_SAMPLES ? count : KERNEL_HAL_AUDIO_FRAME_SAMPLES;
    (void) hardware_abstraction_layer_audio_limit(samples, take, limited);
    return false;
}

bool hardware_abstraction_layer_audio_playback_active(void) { return false; }

void hardware_abstraction_layer_audio_playback_flush(void) {}

uint32_t hardware_abstraction_layer_audio_capture_overruns(void) { return hal_audio_overruns; }

bool hardware_abstraction_layer_audio_capture_is_interrupt_driven(void) { return hal_audio_interrupt_driven; }

uint32_t hardware_abstraction_layer_audio_capture_interrupt_count(void) { return hal_audio_interrupts; }

uint8_t hardware_abstraction_layer_audio_capture_interrupt_line(void) { return hal_audio_interrupt_line; }
