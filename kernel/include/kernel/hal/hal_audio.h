/**
 * @file hal_audio.h
 * @brief Audio capture and playback, behind the HAL.
 *
 * The fifth device group of the platform seam, and the first one a satellite needs at
 * all. Same shape as the display and input groups: the kernel exposes
 * `hardware_abstraction_layer_audio_*`, the engine side sees an interface, and neither
 * knows the other's language.
 *
 * Capture is a producer into a bounded ring, exactly like the keyboard and the mouse:
 * an interrupt handler pushes a buffer and returns, and every decision about that
 * buffer happens on the consumer side. A codec that is absent is a legitimate
 * configuration — a client image has no reason to have one.
 *
 * @ref hardware_abstraction_layer_audio_present means "capture works", and it is taken
 * from the stream descriptor actually running rather than from a codec having answered.
 * @ref hardware_abstraction_layer_audio_device_name says which situation a machine is
 * in — no controller, a controller whose codec stayed silent, a codec with no capture
 * stream, or a working one. Collapsing them would let a profile claim to be listening
 * to a quiet room.
 *
 * ⚠ Buffers arriving and buffers containing SIGNAL are different facts. A stream with
 * nothing plugged into it delivers silence perfectly on time, so the satellite profile
 * reports a level alongside its buffer count.
 *
 * The ring is exposed by ADDRESS as well as by value, because that is what the power
 * floor will need: `processor_sleep_until_write` arms a watch on the write index and
 * sleeps until the producer touches it, which is the difference between a node that
 * idles at a few watts and one that idles at twenty. That path waits on an interrupt
 * handler this driver does not have yet — today the producer is
 * @ref hardware_abstraction_layer_audio_capture_pump, called by the profile's loop,
 * which is why the loop sleeps to its frame deadline instead.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_HAL_HAL_AUDIO_H
#define KERNEL_HAL_HAL_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Samples in one capture or playback buffer: forty milliseconds at 16 kHz. */
#define KERNEL_HAL_AUDIO_FRAME_SAMPLES 640u

/** Buffers the capture ring holds before it overwrites the oldest. */
#define KERNEL_HAL_AUDIO_RING_FRAMES 8u

/**
 * @brief Hard ceiling on anything this kernel will ever play, in dBFS.
 *
 * Minus twelve decibels relative to full scale — a quarter of the amplitude the
 * format can express. Chosen for ears rather than for fidelity: this profile is
 * developed with headphones on, and the failure mode of an audio driver under
 * development is not a slightly wrong tone, it is full-scale noise at the exact
 * moment somebody is listening closely.
 *
 * It is a COMPILE-TIME ceiling and not a volume setting, which is the property that
 * matters. @ref hardware_abstraction_layer_audio_set_output_gain_permille can lower
 * the level and cannot raise it past this, so there is no sequence of calls — and no
 * bug in a caller — that reaches full scale. A limiter that could be turned off is a
 * limiter that will be off the one time it was needed.
 */
#define KERNEL_HAL_AUDIO_OUTPUT_CEILING_DECIBELS (-12)

/**
 * @brief The ceiling as a sample magnitude.
 *
 * 10^(-12/20) of full scale: 0.25119 * 32767, rounded down. Written out rather than
 * computed, because computing it needs a logarithm that nothing in ring 0 may call —
 * and a safety limit derived at run time is a safety limit that can fail to be
 * derived.
 */
#define KERNEL_HAL_AUDIO_OUTPUT_CEILING_AMPLITUDE 8230

/**
 * @brief Looks for a codec and prepares the ring.
 *
 * @return true when a codec was found and capture can start.
 */
bool hardware_abstraction_layer_audio_initialize(void);

/**
 * @brief Is there a codec?
 * @return true when one was found.
 */
bool hardware_abstraction_layer_audio_present(void);

/**
 * @brief A word for what was found, for a boot line.
 * @return The device's name, or "absent".
 */
const char *hardware_abstraction_layer_audio_device_name(void);

/**
 * @brief Sampling rate the codec is configured at.
 * @return Hertz, or 0 when absent.
 */
uint32_t hardware_abstraction_layer_audio_sample_rate(void);

/**
 * @brief The capture ring's write index.
 *
 * Returned as an address so a caller can arm a monitor on it. Volatile because the
 * producer is an interrupt handler — or, on real hardware, a DMA engine that does not
 * even take an interrupt to advance it.
 *
 * @return The address, or NULL when there is no codec.
 */
const volatile uint32_t *hardware_abstraction_layer_audio_capture_write_index(void);

/**
 * @brief Moves whatever the controller has finished into the ring.
 *
 * The producer side, and it has to be called: the driver underneath is POLLED, not
 * interrupt-driven, so nothing advances the write index on its own. That is why the
 * satellite loop pumps and then sleeps to its frame deadline rather than sleeping on
 * the write index — arming a monitor on an address only this thread ever writes is a
 * wait for something that cannot happen. When an interrupt handler eventually pushes
 * buffers, the monitor path becomes the right one and this becomes a no-op.
 *
 * @return Buffers moved into the ring.
 */
uint32_t hardware_abstraction_layer_audio_capture_pump(void);

/**
 * @brief Takes the oldest unread capture buffer.
 *
 * @param out      Receives @ref KERNEL_HAL_AUDIO_FRAME_SAMPLES signed samples.
 * @param capacity Room in @p out, in samples.
 * @return Samples written; 0 when nothing is waiting or no codec is present.
 */
uint32_t hardware_abstraction_layer_audio_capture_take(int16_t *out, uint32_t capacity);

/**
 * @brief Sets the output gain, in thousandths of the ceiling.
 *
 * Clamped to 1000. There is deliberately no way to ask for more: the argument names a
 * fraction OF the ceiling, so the loudest thing this function can request is the
 * ceiling itself.
 *
 * @param permille Gain, 0 to 1000.
 * @return The gain actually in force.
 */
uint32_t hardware_abstraction_layer_audio_set_output_gain_permille(uint32_t permille);

/**
 * @brief The gain in force.
 * @return Thousandths of the ceiling.
 */
uint32_t hardware_abstraction_layer_audio_output_gain_permille(void);

/**
 * @brief Applies the gain and the ceiling to a buffer.
 *
 * Exposed rather than kept inside the submit path so it can be exercised without a
 * codec — which, while there is no stream, is the only way to know the limiter works
 * at all. Every sample that leaves this kernel goes through it.
 *
 * @param samples  Input.
 * @param count    How many.
 * @param out      Receives the limited samples; may alias @p samples.
 * @return Samples that hit the ceiling and were clamped.
 */
uint32_t hardware_abstraction_layer_audio_limit(const int16_t *samples, uint32_t count, int16_t *out);

/**
 * @brief Samples clamped since initialisation.
 *
 * Counted because a limiter that is constantly working is telling you something: the
 * signal upstream of it is too hot, and the ceiling is papering over it.
 *
 * @return The count.
 */
uint32_t hardware_abstraction_layer_audio_clipped_samples(void);

/**
 * @brief Queues a buffer for playback.
 *
 * @param samples Signed samples.
 * @param count   How many.
 * @return true when the codec accepted it.
 */
bool hardware_abstraction_layer_audio_playback_submit(const int16_t *samples, uint32_t count);

/**
 * @brief Is the codec still emitting?
 * @return true while a queued buffer is playing.
 */
bool hardware_abstraction_layer_audio_playback_active(void);

/**
 * @brief Drops everything queued for playback.
 *
 * The node was interrupted. Dropping rather than draining is the point: a reply the
 * sovereign has cut off must stop being audible now, not at the end of the buffer.
 */
void hardware_abstraction_layer_audio_playback_flush(void);

/**
 * @brief Capture buffers lost because the consumer fell behind.
 *
 * Counted rather than silently overwritten. A node dropping buffers is a node whose
 * frame budget is too small, and that is only visible as a number.
 *
 * @return The count.
 */
uint32_t hardware_abstraction_layer_audio_capture_overruns(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_HAL_HAL_AUDIO_H */
