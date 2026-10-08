#include <kernel/dialogue/dialogue_channel.h>

#include <kernel/memory/spsc_ring.h>

_Static_assert((KERNEL_DIALOGUE_CHANNEL_CAPACITY & (KERNEL_DIALOGUE_CHANNEL_CAPACITY - 1u)) == 0u,
               "a dialogue ring has a power-of-two capacity");

/**
 * @struct DialogueRing_t
 * @brief One direction: the ring's indices and the bytes they point into.
 */
typedef struct {
    KernelSpscRing_t ring;                           /**< Written by the speaker, read by the listener. */
    uint8_t bytes[KERNEL_DIALOGUE_CHANNEL_CAPACITY]; /**< The slots. */
} DialogueRing_t;

static DialogueRing_t dialogue_to_demon = {.ring = KERNEL_SPSC_RING_INITIALIZER(KERNEL_DIALOGUE_CHANNEL_CAPACITY)};
static DialogueRing_t dialogue_to_sovereign = {.ring = KERNEL_SPSC_RING_INITIALIZER(KERNEL_DIALOGUE_CHANNEL_CAPACITY)};

/**
 * @brief Pushes one byte, or drops it and counts it.
 * @param direction The direction.
 * @param byte      The byte.
 * @return false when the ring was full.
 */
static bool dialogue_ring_push(DialogueRing_t *direction, uint8_t byte)
{
    uint32_t slot = 0u;

    if (kernel_spsc_ring_reserve(&direction->ring, 1u, &slot) == 0u)
    {
        kernel_spsc_ring_count_rejected(&direction->ring, 1u);
        return false;
    }

    direction->bytes[slot] = byte;
    kernel_spsc_ring_publish(&direction->ring, 1u);
    return true;
}

/**
 * @brief Pops one byte.
 * @param direction The direction.
 * @param out       Receives the byte.
 * @return false when the ring was empty.
 */
static bool dialogue_ring_pop(DialogueRing_t *direction, uint8_t *out)
{
    uint32_t slot = 0u;

    if (kernel_spsc_ring_peek(&direction->ring, 1u, &slot) == 0u)
        return false;

    *out = direction->bytes[slot];
    kernel_spsc_ring_release(&direction->ring, 1u);
    return true;
}

void kernel_dialogue_channel_reset(void)
{
    kernel_spsc_ring_initialize(&dialogue_to_demon.ring, KERNEL_DIALOGUE_CHANNEL_CAPACITY);
    kernel_spsc_ring_initialize(&dialogue_to_sovereign.ring, KERNEL_DIALOGUE_CHANNEL_CAPACITY);
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

uint32_t kernel_dialogue_channel_pending_for_demon(void) { return kernel_spsc_ring_size(&dialogue_to_demon.ring); }

uint32_t kernel_dialogue_channel_pending_for_sovereign(void)
{
    return kernel_spsc_ring_size(&dialogue_to_sovereign.ring);
}

uint32_t kernel_dialogue_channel_dropped(void)
{
    return kernel_spsc_ring_rejected_count(&dialogue_to_demon.ring) +
           kernel_spsc_ring_rejected_count(&dialogue_to_sovereign.ring);
}
