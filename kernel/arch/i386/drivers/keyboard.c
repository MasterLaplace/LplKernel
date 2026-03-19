#include <kernel/drivers/keyboard.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/drivers/ps2_keyboard.h>
#include <kernel/lib/asmutils.h>

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60u
#define COM1_PORT          0x3F8u
#define COM1_LSR_THRE      0x20u

#define IRQ_KEYBOARD_LINE   1u
#define IRQ_KEYBOARD_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_KEYBOARD_LINE)
#define KEYBOARD_CHAR_QUEUE_CAPACITY 128u

static uint32_t keyboard_irq_count = 0u;
static uint32_t keyboard_printable_count = 0u;
static char keyboard_last_printable_char = 0;
static char keyboard_char_queue[KEYBOARD_CHAR_QUEUE_CAPACITY];
static uint32_t keyboard_char_queue_head = 0u;
static uint32_t keyboard_char_queue_tail = 0u;
static uint32_t keyboard_char_queue_count = 0u;
static uint32_t keyboard_char_queue_drop_count = 0u;

static void keyboard_queue_push_char(char c)
{
    if (keyboard_char_queue_count >= KEYBOARD_CHAR_QUEUE_CAPACITY)
    {
        ++keyboard_char_queue_drop_count;
        return;
    }

    keyboard_char_queue[keyboard_char_queue_tail] = c;
    keyboard_char_queue_tail = (keyboard_char_queue_tail + 1u) % KEYBOARD_CHAR_QUEUE_CAPACITY;
    ++keyboard_char_queue_count;
}

static void keyboard_write_char(char c)
{
    while (!(asmutils_input_byte((short) (COM1_PORT + 5u)) & COM1_LSR_THRE))
    {
    }
    asmutils_output_byte((short) COM1_PORT, (unsigned char) c);
}

static void keyboard_write_string(const char *s)
{
    while (*s)
        keyboard_write_char(*s++);
}

static void keyboard_write_hex8(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    keyboard_write_string("0x");
    keyboard_write_char(hex[(value >> 4u) & 0xFu]);
    keyboard_write_char(hex[value & 0xFu]);
}

static void keyboard_interrupt_handler(const InterruptFrame_t *frame)
{
    const uint8_t scan_code = (uint8_t) asmutils_input_byte((short) KEYBOARD_DATA_PORT);
    const char decoded = ps2_keyboard_decode_scancode(scan_code);

    (void) frame;
    ++keyboard_irq_count;

    keyboard_write_string("[IRQ1] keyboard scan code = ");
    keyboard_write_hex8(scan_code);

    if (decoded)
    {
        ++keyboard_printable_count;
        keyboard_last_printable_char = decoded;
        keyboard_queue_push_char(decoded);
        keyboard_write_string(", ascii='");
        keyboard_write_char(decoded);
        keyboard_write_char('\'');
    }

    keyboard_write_string("\r\n");

    if (interrupt_request_is_keyboard_owner_apic())
        advanced_pic_timer_backend_signal_end_of_interrupt();
    else
        programmable_interrupt_controller_send_end_of_interrupt(IRQ_KEYBOARD_LINE);
}

void keyboard_interrupt_initialize(void)
{
    interrupt_service_routine_register_handler(IRQ_KEYBOARD_VECTOR, keyboard_interrupt_handler);
}

uint32_t keyboard_get_irq_count(void)
{
    return keyboard_irq_count;
}

uint32_t keyboard_get_printable_count(void)
{
    return keyboard_printable_count;
}

char keyboard_get_last_printable_char(void)
{
    return keyboard_last_printable_char;
}

uint32_t keyboard_get_pending_char_count(void)
{
    return keyboard_char_queue_count;
}

uint8_t keyboard_try_pop_char(char *out_char)
{
    if (!out_char || keyboard_char_queue_count == 0u)
        return 0u;

    *out_char = keyboard_char_queue[keyboard_char_queue_head];
    keyboard_char_queue_head = (keyboard_char_queue_head + 1u) % KEYBOARD_CHAR_QUEUE_CAPACITY;
    --keyboard_char_queue_count;
    return 1u;
}

uint32_t keyboard_get_dropped_char_count(void)
{
    return keyboard_char_queue_drop_count;
}
