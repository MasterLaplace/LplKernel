/**
 * @file hal_audio.c
 * @brief Audio capture and playback, behind the HAL.
 *
 * Every entry point below reports honestly that nothing was found rather than
 * returning a plausible silence — a capture that handed back a zeroed buffer would let
 * the satellite profile claim to be listening to a quiet room, and there is no way to
 * tell those two apart from the outside.
 *
 * The ring is the same single-producer structure the keyboard uses, sized for
 * forty-millisecond buffers, and its write index is exposed so the power floor can
 * sleep on it. The producer today is @ref hardware_abstraction_layer_audio_capture_pump
 * rather than an interrupt handler, because `drivers/hda.c` polls the stream's link
 * position; when it grows a handler, the handler fills this ring instead and nothing
 * above changes.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/hal/hal_audio.h>

#include <kernel/drivers/hda.h>

static int16_t hal_audio_ring[KERNEL_HAL_AUDIO_RING_FRAMES][KERNEL_HAL_AUDIO_FRAME_SAMPLES];
static volatile uint32_t hal_audio_write_index = 0u;
static volatile uint32_t hal_audio_read_index = 0u;
static volatile uint32_t hal_audio_overruns = 0u;
static bool hal_audio_codec_present = false;
static const char *hal_audio_name = "absent";
static uint32_t hal_audio_gain_permille = 1000u;
static uint32_t hal_audio_clipped = 0u;

bool hardware_abstraction_layer_audio_initialize(void)
{
    hal_audio_write_index = 0u;
    hal_audio_read_index = 0u;
    hal_audio_overruns = 0u;
    hal_audio_codec_present = false;
    hal_audio_name = "absent";

    /* `present` means "capture works", and it is taken from the stream descriptor
       rather than from the codec having answered. The distinction is the whole point of
       reporting these separately: "there is no sound card", "there is one and no codec
       answered", and "a codec answered but no input stream is running" are three
       different facts, and a seam that collapsed them would let the profile claim to be
       listening to a quiet room. */
    IntelHighDefinitionAudioState_t controller;
    const bool answered = intel_high_definition_audio_initialize(&controller);

    if (controller.capture_running)
    {
        hal_audio_codec_present = true;
        hal_audio_name = "intel-hda";
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
        const uint32_t slot = hal_audio_write_index % KERNEL_HAL_AUDIO_RING_FRAMES;
        const uint32_t samples =
            intel_high_definition_audio_poll_capture(hal_audio_ring[slot], KERNEL_HAL_AUDIO_FRAME_SAMPLES);
        if (samples == 0u)
            break;

        /* Overrun is counted, not hidden. A consumer that has fallen a whole ring
           behind is losing audio, and that only shows up as a number — the samples
           themselves look exactly like the ones that arrived on time. */
        if (hal_audio_write_index - hal_audio_read_index >= KERNEL_HAL_AUDIO_RING_FRAMES)
        {
            ++hal_audio_overruns;
            ++hal_audio_read_index;
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
        /* Gain first, ceiling second, and the order is the guarantee. Clamping before
           scaling would let a gain of one thousand multiply an already-clamped sample
           back past the ceiling; this way the last thing that touches a sample is the
           limit, so nothing downstream of it can be louder. */
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
    /* The limiter runs even though nothing plays yet, and that is deliberate: it means
       there is no future commit in which the stream path exists and the ceiling has
       not been wired to it. The submission still fails, because there is no stream. */
    static int16_t limited[KERNEL_HAL_AUDIO_FRAME_SAMPLES];
    const uint32_t take = count < KERNEL_HAL_AUDIO_FRAME_SAMPLES ? count : KERNEL_HAL_AUDIO_FRAME_SAMPLES;
    (void) hardware_abstraction_layer_audio_limit(samples, take, limited);
    return false;
}

bool hardware_abstraction_layer_audio_playback_active(void) { return false; }

void hardware_abstraction_layer_audio_playback_flush(void) {}

uint32_t hardware_abstraction_layer_audio_capture_overruns(void) { return hal_audio_overruns; }
