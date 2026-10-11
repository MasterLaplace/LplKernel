#include <kernel/drivers/keyboard.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/spsc_ring.h>

#define KEYBOARD_DATA_PORT 0x60u

#define IRQ_KEYBOARD_LINE   1u
#define IRQ_KEYBOARD_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_KEYBOARD_LINE)

/**
 * @name Power-of-two capacities: the scan-code ring's slots, and the decoded characters
 * @{
 */
#define KEYBOARD_SCANCODE_RING_CAPACITY 256u
#define KEYBOARD_SCANCODE_RING_MASK     (KEYBOARD_SCANCODE_RING_CAPACITY - 1u)
/** @} */

static uint32_t keyboard_irq_count = 0u;
static uint32_t keyboard_printable_count = 0u;
static char keyboard_last_printable_char = 0;

/**
 * Raw scan codes from the IRQ1 handler (producer) to the decoder (consumer), on the SPSC ring
 * primitive. Decoding, which carries the modifier state, happens on the consumer side only,
 * keeping the handler short and bounded and the modifier state race-free.
 */
static KernelSpscRing_t keyboard_scancode_ring = KERNEL_SPSC_RING_INITIALIZER(KEYBOARD_SCANCODE_RING_CAPACITY);
static uint8_t keyboard_scancode_slots[KEYBOARD_SCANCODE_RING_CAPACITY];

/**
 * Characters decoded from the ring, waiting for the consumer.
 *
 * Consumer-side only, like the decoder: counting what is pending decodes the scan codes waiting,
 * in order, so the count is the number of characters the consumer will pop, never a key release or
 * a controller acknowledgement that decodes to nothing.
 */
static char keyboard_character_queue[KEYBOARD_SCANCODE_RING_CAPACITY];
static uint32_t keyboard_character_queue_head = 0u;
static uint32_t keyboard_character_queue_tail = 0u;

static void keyboard_interrupt_handler(const InterruptFrame_t *frame)
{
    const uint8_t scan_code = asmutils_input_byte(KEYBOARD_DATA_PORT);
    uint32_t slot = 0u;

    (void) frame;
    ++keyboard_irq_count;

    if (kernel_spsc_ring_reserve(&keyboard_scancode_ring, 1u, &slot) == 0u)
        kernel_spsc_ring_count_rejected(&keyboard_scancode_ring, 1u);
    else
    {
        keyboard_scancode_slots[slot] = scan_code;
        kernel_spsc_ring_publish(&keyboard_scancode_ring, 1u);
    }

    if (interrupt_request_is_keyboard_owner_apic())
        advanced_pic_timer_backend_signal_end_of_interrupt();
    else
        programmable_interrupt_controller_send_end_of_interrupt(IRQ_KEYBOARD_LINE);
}

/**
 * @brief Decodes the scan codes waiting, a run of slots at a time, into the character queue, as far
 *        as it has room: each scan code decodes to at most one character.
 */
static void keyboard_decode_pending_scan_codes(void)
{
    uint32_t room = KEYBOARD_SCANCODE_RING_CAPACITY - (keyboard_character_queue_head - keyboard_character_queue_tail);

    while (room != 0u)
    {
        uint32_t first_slot = 0u;
        const uint32_t run = kernel_spsc_ring_peek(&keyboard_scancode_ring, room, &first_slot);

        if (run == 0u)
            return;
        for (uint32_t offset = 0u; offset < run; ++offset)
        {
            const char decoded =
                personal_system_2_keyboard_decode_scancode(keyboard_scancode_slots[first_slot + offset]);

            if (decoded)
            {
                keyboard_character_queue[keyboard_character_queue_head & KEYBOARD_SCANCODE_RING_MASK] = decoded;
                ++keyboard_character_queue_head;
            }
        }
        kernel_spsc_ring_release(&keyboard_scancode_ring, run);
        room -= run;
    }
}

void keyboard_interrupt_initialize(void)
{
    interrupt_service_routine_register_handler(IRQ_KEYBOARD_VECTOR, keyboard_interrupt_handler);

#if defined(LPL_KERNEL_KEYBOARD_LAYOUT_AZERTY)
    personal_system_2_keyboard_set_layout(PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_FRENCH_AZERTY);
#else
    personal_system_2_keyboard_set_layout(PERSONAL_SYSTEM_2_KEYBOARD_LAYOUT_UNITED_STATES_QWERTY);
#endif
}

uint32_t keyboard_get_irq_count(void) { return keyboard_irq_count; }

uint32_t keyboard_get_printable_count(void) { return keyboard_printable_count; }

char keyboard_get_last_printable_char(void) { return keyboard_last_printable_char; }

uint32_t keyboard_get_pending_char_count(void)
{
    keyboard_decode_pending_scan_codes();
    return keyboard_character_queue_head - keyboard_character_queue_tail;
}

uint8_t keyboard_try_pop_char(char *out_char)
{
    char decoded = 0;

    if (!out_char)
        return 0u;

    keyboard_decode_pending_scan_codes();
    if (keyboard_character_queue_head == keyboard_character_queue_tail)
        return 0u;

    decoded = keyboard_character_queue[keyboard_character_queue_tail & KEYBOARD_SCANCODE_RING_MASK];
    ++keyboard_character_queue_tail;
    ++keyboard_printable_count;
    keyboard_last_printable_char = decoded;
    *out_char = decoded;
    return 1u;
}

const volatile uint32_t *keyboard_get_ring_head_address(void)
{
    return kernel_spsc_ring_write_index_address(&keyboard_scancode_ring);
}

uint32_t keyboard_get_ring_capacity(void) { return KEYBOARD_SCANCODE_RING_CAPACITY; }

uint32_t keyboard_get_dropped_char_count(void) { return kernel_spsc_ring_rejected_count(&keyboard_scancode_ring); }
