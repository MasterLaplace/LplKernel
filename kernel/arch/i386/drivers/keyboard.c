#include <kernel/drivers/keyboard.h>

#include <kernel/cpu/isr.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/pic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/lib/asmutils.h>

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60u
#define COM1_PORT          0x3F8u
#define COM1_LSR_THRE      0x20u

#define IRQ_KEYBOARD_LINE   1u
#define IRQ_KEYBOARD_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_KEYBOARD_LINE)

static void keyboard_write_char(char c)
{
    while (!(inb((short) (COM1_PORT + 5u)) & COM1_LSR_THRE))
    {
    }
    outb((short) COM1_PORT, (unsigned char) c);
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
    const uint8_t scan_code = (uint8_t) inb((short) KEYBOARD_DATA_PORT);

    (void) frame;
    keyboard_write_string("[IRQ1] keyboard scan code = ");
    keyboard_write_hex8(scan_code);
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
