#include <kernel/cpu/irq.h>

#include <kernel/cpu/exception.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/lib/asmutils.h>

#define IRQ_LINE_COUNT        16u
#define IRQ_CASCADE_LINE      2u
#define IRQ_TIMER_LINE        0u
#define IRQ_KEYBOARD_LINE     1u
#define IRQ_SPURIOUS7_LINE    7u
#define IRQ_SPURIOUS15_LINE   15u
#define IRQ_TIMER_VECTOR      (PIC_VECTOR_OFFSET_MASTER + IRQ_TIMER_LINE)
#define IRQ_SPURIOUS7_VECTOR  (PIC_VECTOR_OFFSET_MASTER + IRQ_SPURIOUS7_LINE)
#define IRQ_SPURIOUS15_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_SPURIOUS15_LINE)

static volatile uint32_t interrupt_request_tick_count = 0u;
static volatile uint32_t interrupt_request_spurious_irq7_count = 0u;
static volatile uint32_t interrupt_request_spurious_irq15_count = 0u;

static void interrupt_request_timer_handler(const InterruptFrame_t *frame)
{
    (void) frame;
    interrupt_request_tick_count++;
    programmable_interrupt_controller_send_end_of_interrupt(IRQ_TIMER_LINE);
}

static void interrupt_request_mask_all(void)
{
    for (uint8_t irq = 0u; irq < IRQ_LINE_COUNT; ++irq)
        programmable_interrupt_controller_set_mask(irq);
}

static void interrupt_request_spurious_irq7_handler(const InterruptFrame_t *frame)
{
    (void) frame;

    if (!programmable_interrupt_controller_is_in_service(IRQ_SPURIOUS7_LINE))
    {
        interrupt_request_spurious_irq7_count++;
        return;
    }

    programmable_interrupt_controller_send_end_of_interrupt(IRQ_SPURIOUS7_LINE);
}

static void interrupt_request_spurious_irq15_handler(const InterruptFrame_t *frame)
{
    (void) frame;

    if (!programmable_interrupt_controller_is_in_service(IRQ_SPURIOUS15_LINE))
    {
        interrupt_request_spurious_irq15_count++;
        programmable_interrupt_controller_send_end_of_interrupt(IRQ_CASCADE_LINE);
        return;
    }

    programmable_interrupt_controller_send_end_of_interrupt(IRQ_SPURIOUS15_LINE);
}

void interrupt_request_initialize(void)
{
    programmable_interrupt_controller_initialize();
    interrupt_request_mask_all();

    interrupt_exception_initialize();
    interrupt_service_routine_register_handler(IRQ_TIMER_VECTOR, interrupt_request_timer_handler);
    interrupt_service_routine_register_handler(IRQ_SPURIOUS7_VECTOR, interrupt_request_spurious_irq7_handler);
    interrupt_service_routine_register_handler(IRQ_SPURIOUS15_VECTOR, interrupt_request_spurious_irq15_handler);
    keyboard_interrupt_initialize();

    programmable_interrupt_controller_clear_mask(IRQ_TIMER_LINE);
    programmable_interrupt_controller_clear_mask(IRQ_KEYBOARD_LINE);
    cpu_enable_interrupts();
}

uint32_t interrupt_request_get_tick_count(void) { return interrupt_request_tick_count; }

uint32_t interrupt_request_get_spurious_irq7_count(void) { return interrupt_request_spurious_irq7_count; }

uint32_t interrupt_request_get_spurious_irq15_count(void) { return interrupt_request_spurious_irq15_count; }
