/**
 * @file dialogue_channel.h
 * @brief The aperture between the sovereign and the demon.
 *
 * Not plumbing. The world is deterministic and closed; this is the one path by
 * which something the world cannot predict enters it, and by which the demon
 * answers. Bounded, lock-free, and readable from a context that must not block —
 * the same single-producer ring the input drivers already use.
 *
 * Two rings and not one, because the two directions have different producers and a
 * shared ring would need a lock to keep them apart — which is the one thing an
 * interrupt-context writer cannot take. Inbound is written by whatever carries the
 * sovereign's words (a console, a serial line, a capture buffer) and drained by the
 * demon; outbound is the reverse.
 *
 * A full ring DROPS and counts. Blocking is not available to the producer, and
 * overwriting the oldest byte would corrupt a sentence already half-read by the
 * consumer — so the loss is made visible instead of being made silent.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_DIALOGUE_DIALOGUE_CHANNEL_H
#define KERNEL_DIALOGUE_DIALOGUE_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes each direction holds. A power of two, so the indices wrap with a mask. */
#define KERNEL_DIALOGUE_CHANNEL_CAPACITY 512u

/**
 * @brief Empties both rings and clears the drop counts.
 */
void kernel_dialogue_channel_reset(void);

/**
 * @brief Offers one byte from the sovereign.
 *
 * Safe from an interrupt handler: it advances one index and returns.
 *
 * @param byte The byte.
 * @return false when the ring is full and the byte was dropped.
 */
bool kernel_dialogue_channel_offer_to_demon(uint8_t byte);

/**
 * @brief Takes one byte for the demon.
 * @param out Receives the byte.
 * @return false when nothing is waiting.
 */
bool kernel_dialogue_channel_take_for_demon(uint8_t *out);

/**
 * @brief Offers one byte from the demon.
 * @param byte The byte.
 * @return false when the ring is full and the byte was dropped.
 */
bool kernel_dialogue_channel_offer_to_sovereign(uint8_t byte);

/**
 * @brief Takes one byte for the sovereign.
 * @param out Receives the byte.
 * @return false when nothing is waiting.
 */
bool kernel_dialogue_channel_take_for_sovereign(uint8_t *out);

/**
 * @brief Bytes waiting for the demon.
 * @return The occupancy of the inbound ring.
 */
uint32_t kernel_dialogue_channel_pending_for_demon(void);

/**
 * @brief Bytes waiting for the sovereign.
 * @return The occupancy of the outbound ring.
 */
uint32_t kernel_dialogue_channel_pending_for_sovereign(void);

/**
 * @brief Bytes lost because a ring was full.
 * @return The total drop count, both directions.
 */
uint32_t kernel_dialogue_channel_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_DIALOGUE_DIALOGUE_CHANNEL_H */
