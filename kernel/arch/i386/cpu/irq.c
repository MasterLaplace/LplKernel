#include <kernel/cpu/irq.h>

#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/lib/asmutils.h>

#define IRQ_LINE_COUNT 16u
#define IRQ_TIMER_LINE 0u
#define IRQ_TIMER_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_TIMER_LINE)

static volatile uint32_t interrupt_request_tick_count = 0u;

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

void interrupt_request_initialize(void)
{
    programmable_interrupt_controller_initialize();
    interrupt_request_mask_all();
    interrupt_service_routine_register_handler(IRQ_TIMER_VECTOR, interrupt_request_timer_handler);
    programmable_interrupt_controller_clear_mask(IRQ_TIMER_LINE);
    cpu_enable_interrupts();
}

uint32_t interrupt_request_get_tick_count(void)
{
    return interrupt_request_tick_count;
}
