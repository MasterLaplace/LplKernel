#include <kernel/drivers/keyboard.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/lib/asmutils.h>

#define KEYBOARD_DATA_PORT 0x60u

#define IRQ_KEYBOARD_LINE   1u
#define IRQ_KEYBOARD_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_KEYBOARD_LINE)

/* Power-of-two capacity so head/tail wrap with a cheap mask. */
#define KEYBOARD_SCANCODE_RING_CAPACITY 256u
#define KEYBOARD_SCANCODE_RING_MASK     (KEYBOARD_SCANCODE_RING_CAPACITY - 1u)

static uint32_t keyboard_irq_count = 0u;
static uint32_t keyboard_printable_count = 0u;
static char keyboard_last_printable_char = 0;

/*
** Lock-free single-producer / single-consumer ring of raw scan codes.
**
** Producer: the IRQ1 handler (interrupt context).
** Consumer: keyboard_try_pop_char, run from the main loop (bottom half).
**
** The producer owns `head`, the consumer owns `tail`; neither side touches the
** other's index, and the indices are free-running uint32_t read through
** `volatile`, so no shared counter and no locking are needed. Decoding (which
** carries modifier state) happens entirely on the consumer side, keeping the
** IRQ handler short and bounded and the modifier state race-free.
*/
static volatile uint8_t keyboard_scancode_ring[KEYBOARD_SCANCODE_RING_CAPACITY];
static volatile uint32_t keyboard_scancode_ring_head = 0u;
static volatile uint32_t keyboard_scancode_ring_tail = 0u;
static uint32_t keyboard_scancode_drop_count = 0u; /* producer-only counter */

static void keyboard_interrupt_handler(const InterruptFrame_t *frame)
{
    const uint8_t scan_code = asmutils_input_byte(KEYBOARD_DATA_PORT);
    const uint32_t head = keyboard_scancode_ring_head;
    const uint32_t tail = keyboard_scancode_ring_tail;

    (void) frame;
    ++keyboard_irq_count;

    if ((uint32_t) (head - tail) >= KEYBOARD_SCANCODE_RING_CAPACITY)
    {
        ++keyboard_scancode_drop_count;
    }
    else
    {
        keyboard_scancode_ring[head & KEYBOARD_SCANCODE_RING_MASK] = scan_code;
        keyboard_scancode_ring_head = head + 1u;
    }

    if (interrupt_request_is_keyboard_owner_apic())
        advanced_pic_timer_backend_signal_end_of_interrupt();
    else
        programmable_interrupt_controller_send_end_of_interrupt(IRQ_KEYBOARD_LINE);
}

static uint8_t keyboard_scancode_ring_pop(uint8_t *out_scan_code)
{
    const uint32_t tail = keyboard_scancode_ring_tail;

    if (keyboard_scancode_ring_head == tail)
        return 0u;

    *out_scan_code = keyboard_scancode_ring[tail & KEYBOARD_SCANCODE_RING_MASK];
    keyboard_scancode_ring_tail = tail + 1u;
    return 1u;
}

void keyboard_interrupt_initialize(void)
{
    interrupt_service_routine_register_handler(IRQ_KEYBOARD_VECTOR, keyboard_interrupt_handler);

    /* Compile-time default layout (see make.config / build.sh --azerty|--qwerty).
       Can still be overridden at runtime via personal_system_2_keyboard_set_layout. */
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
    return (uint32_t) (keyboard_scancode_ring_head - keyboard_scancode_ring_tail);
}

uint8_t keyboard_try_pop_char(char *out_char)
{
    uint8_t scan_code = 0u;

    if (!out_char)
        return 0u;

    while (keyboard_scancode_ring_pop(&scan_code))
    {
        const char decoded = personal_system_2_keyboard_decode_scancode(scan_code);

        if (decoded)
        {
            ++keyboard_printable_count;
            keyboard_last_printable_char = decoded;
            *out_char = decoded;
            return 1u;
        }
    }

    return 0u;
}

uint32_t keyboard_get_dropped_char_count(void) { return keyboard_scancode_drop_count; }
