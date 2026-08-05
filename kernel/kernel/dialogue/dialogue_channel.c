/**
 * @file dialogue_channel.c
 * @brief The aperture between the sovereign and the demon.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <kernel/dialogue/dialogue_channel.h>

#define KERNEL_DIALOGUE_CHANNEL_MASK (KERNEL_DIALOGUE_CHANNEL_CAPACITY - 1u)

/**
 * @struct DialogueRing_t
 * @brief One direction.
 *
 * The producer owns `head`, the consumer owns `tail`, and neither writes the
 * other's index — which is what makes the pair safe without a lock. Both are
 * free-running counters rather than wrapped indices, so a full ring is
 * distinguishable from an empty one without spending a slot to say so.
 */
typedef struct {
    volatile uint8_t bytes[KERNEL_DIALOGUE_CHANNEL_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
} DialogueRing_t;

static DialogueRing_t dialogue_to_demon;
static DialogueRing_t dialogue_to_sovereign;
static volatile uint32_t dialogue_dropped;

/**
 * @brief Pushes one byte, or drops it.
 * @param ring The direction.
 * @param byte The byte.
 * @return false when the ring was full.
 */
static bool dialogue_ring_push(DialogueRing_t *ring, uint8_t byte)
{
    const uint32_t head = ring->head;
    const uint32_t tail = ring->tail;

    if ((uint32_t) (head - tail) >= KERNEL_DIALOGUE_CHANNEL_CAPACITY)
    {
        ++dialogue_dropped;
        return false;
    }

    ring->bytes[head & KERNEL_DIALOGUE_CHANNEL_MASK] = byte;
    ring->head = head + 1u;
    return true;
}

/**
 * @brief Pops one byte.
 * @param ring The direction.
 * @param out  Receives the byte.
 * @return false when the ring was empty.
 */
static bool dialogue_ring_pop(DialogueRing_t *ring, uint8_t *out)
{
    const uint32_t tail = ring->tail;

    if (ring->head == tail)
        return false;

    *out = ring->bytes[tail & KERNEL_DIALOGUE_CHANNEL_MASK];
    ring->tail = tail + 1u;
    return true;
}

void kernel_dialogue_channel_reset(void)
{
    dialogue_to_demon.head = 0u;
    dialogue_to_demon.tail = 0u;
    dialogue_to_sovereign.head = 0u;
    dialogue_to_sovereign.tail = 0u;
    dialogue_dropped = 0u;
}

bool kernel_dialogue_channel_offer_to_demon(uint8_t byte) { return dialogue_ring_push(&dialogue_to_demon, byte); }

bool kernel_dialogue_channel_take_for_demon(uint8_t *out)
{
    if (out == NULL)
        return false;
    return dialogue_ring_pop(&dialogue_to_demon, out);
}

bool kernel_dialogue_channel_offer_to_sovereign(uint8_t byte)
{
    return dialogue_ring_push(&dialogue_to_sovereign, byte);
}

bool kernel_dialogue_channel_take_for_sovereign(uint8_t *out)
{
    if (out == NULL)
        return false;
    return dialogue_ring_pop(&dialogue_to_sovereign, out);
}

uint32_t kernel_dialogue_channel_pending_for_demon(void)
{
    return (uint32_t) (dialogue_to_demon.head - dialogue_to_demon.tail);
}

uint32_t kernel_dialogue_channel_pending_for_sovereign(void)
{
    return (uint32_t) (dialogue_to_sovereign.head - dialogue_to_sovereign.tail);
}

uint32_t kernel_dialogue_channel_dropped(void) { return dialogue_dropped; }
