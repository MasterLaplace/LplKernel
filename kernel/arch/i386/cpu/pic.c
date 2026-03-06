#include <kernel/cpu/pic.h>
#include <kernel/lib/asmutils.h>

#define PIC1_COMMAND 0x20u
#define PIC1_DATA    0x21u
#define PIC2_COMMAND 0xA0u
#define PIC2_DATA    0xA1u

#define PIC_EOI      0x20u
#define PIC_READ_ISR 0x0Bu

#define ICW1_ICW4 0x01u
#define ICW1_INIT 0x10u

#define ICW4_8086 0x01u

static inline void pic_outb(uint16_t port, uint8_t value) { outb((short) port, (unsigned char) value); }

static inline uint8_t pic_inb(uint16_t port) { return (uint8_t) inb((short) port); }

static inline void pic_io_wait(void) { pic_outb(0x80u, 0u); }

static uint8_t pic_read_in_service_register(uint16_t command_port)
{
    pic_outb(command_port, PIC_READ_ISR);
    return pic_inb(command_port);
}

void programmable_interrupt_controller_send_end_of_interrupt(uint8_t irq_line)
{
    if (irq_line >= 8u)
        pic_outb(PIC2_COMMAND, PIC_EOI);
    pic_outb(PIC1_COMMAND, PIC_EOI);
}

void programmable_interrupt_controller_set_mask(uint8_t irq_line)
{
    uint16_t port;
    uint8_t current;

    if (irq_line < 8u)
        port = PIC1_DATA;
    else
    {
        port = PIC2_DATA;
        irq_line -= 8u;
    }
    current = pic_inb(port);
    pic_outb(port, (uint8_t) (current | (1u << irq_line)));
}

void programmable_interrupt_controller_clear_mask(uint8_t irq_line)
{
    uint16_t port;
    uint8_t current;

    if (irq_line < 8u)
        port = PIC1_DATA;
    else
    {
        port = PIC2_DATA;
        irq_line -= 8u;
    }
    current = pic_inb(port);
    pic_outb(port, (uint8_t) (current & (uint8_t) ~(1u << irq_line)));
}

void programmable_interrupt_controller_initialize(void)
{
    uint8_t master_mask = pic_inb(PIC1_DATA);
    uint8_t slave_mask = pic_inb(PIC2_DATA);

    pic_outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    pic_io_wait();
    pic_outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    pic_io_wait();

    pic_outb(PIC1_DATA, PIC_VECTOR_OFFSET_MASTER);
    pic_io_wait();
    pic_outb(PIC2_DATA, PIC_VECTOR_OFFSET_SLAVE);
    pic_io_wait();

    pic_outb(PIC1_DATA, 0x04u);
    pic_io_wait();
    pic_outb(PIC2_DATA, 0x02u);
    pic_io_wait();

    pic_outb(PIC1_DATA, ICW4_8086);
    pic_io_wait();
    pic_outb(PIC2_DATA, ICW4_8086);
    pic_io_wait();

    pic_outb(PIC1_DATA, master_mask);
    pic_outb(PIC2_DATA, slave_mask);
}

uint8_t programmable_interrupt_controller_is_in_service(uint8_t irq_line)
{
    uint8_t isr;

    if (irq_line < 8u)
    {
        isr = pic_read_in_service_register(PIC1_COMMAND);
        return (uint8_t) ((isr & (uint8_t) (1u << irq_line)) != 0u);
    }

    if (irq_line < 16u)
    {
        isr = pic_read_in_service_register(PIC2_COMMAND);
        return (uint8_t) ((isr & (uint8_t) (1u << (irq_line - 8u))) != 0u);
    }

    return 0u;
}
