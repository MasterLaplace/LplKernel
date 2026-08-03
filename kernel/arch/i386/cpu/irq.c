#include <kernel/cpu/irq.h>

#include <kernel/drivers/ps2_mouse.h>

#define IRQ_LINE_COUNT        16u
#define IRQ_CASCADE_LINE      2u
#define IRQ_TIMER_LINE        0u
#define IRQ_KEYBOARD_LINE     1u
#define IRQ_RTC_LINE          8u
#define IRQ_SPURIOUS7_LINE    7u
#define IRQ_SPURIOUS15_LINE   15u
#define IRQ_TIMER_VECTOR      (PIC_VECTOR_OFFSET_MASTER + IRQ_TIMER_LINE)
#define IRQ_SPURIOUS7_VECTOR  (PIC_VECTOR_OFFSET_MASTER + IRQ_SPURIOUS7_LINE)
#define IRQ_SPURIOUS15_VECTOR (PIC_VECTOR_OFFSET_MASTER + IRQ_SPURIOUS15_LINE)

#define IRQ_TIMER_DEFAULT_FREQUENCY_HZ 100u

static volatile uint32_t interrupt_request_tick_count = 0u;
static volatile uint32_t interrupt_request_spurious_irq7_count = 0u;
static volatile uint32_t interrupt_request_spurious_irq15_count = 0u;
static uint32_t interrupt_request_timer_target_frequency_hz = IRQ_TIMER_DEFAULT_FREQUENCY_HZ;
static uint8_t interrupt_request_rtc_periodic_enabled = 0u;
static uint8_t interrupt_request_timer_owner_is_apic = 0u;
/*
** Which interrupt LINES are delivered through the IOAPIC rather than the 8259.
**
** One bit per ISA line, and not a single "the system is on the APIC now" flag,
** because the handoff is per line: kernel.c routes IRQ1 to the IOAPIC and masks it
** on the PIC, while every other line keeps arriving through the PIC. An interrupt
** handler has to acknowledge the controller that DELIVERED it, so the question a
** handler asks is about its own line.
**
** That distinction was learned the hard way. The mouse handler was written by
** copying the keyboard's, including its `is_keyboard_owner_apic()` test — a name
** that says "keyboard" and was read as "system". Once IRQ1 moved to the IOAPIC the
** mouse, still arriving on the PIC, started sending an APIC EOI: the 8259 was never
** acknowledged, so it blocked IRQ12 after the very first interrupt and the mouse
** appeared dead. It kept working in the browser emulator, which performs no handoff
** and left the flag at zero — the same code, right for the wrong reason.
*/
static uint16_t interrupt_request_apic_owned_lines = 0u;

static void interrupt_request_timer_handler(const InterruptFrame_t *frame)
{
    (void) frame;
    interrupt_request_tick_count++;
    if (interrupt_request_timer_owner_is_apic)
        advanced_pic_timer_backend_signal_end_of_interrupt();
    else
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
    programmable_interval_timer_initialize(interrupt_request_timer_target_frequency_hz);

    interrupt_exception_initialize();
    interrupt_service_routine_register_handler(IRQ_TIMER_VECTOR, interrupt_request_timer_handler);
    interrupt_service_routine_register_handler(IRQ_SPURIOUS7_VECTOR, interrupt_request_spurious_irq7_handler);
    interrupt_service_routine_register_handler(IRQ_SPURIOUS15_VECTOR, interrupt_request_spurious_irq15_handler);
    keyboard_interrupt_initialize();
    /* Probing costs a bounded number of controller reads and reports failure
       rather than hanging, so a machine with no pointing device pays nothing but
       the probe. */
    (void) personal_system_2_mouse_initialize();
    realtime_clock_initialize();

    if (interrupt_request_rtc_periodic_enabled)
    {
        realtime_clock_set_periodic_interrupt_enabled(1u);
        programmable_interrupt_controller_clear_mask(IRQ_CASCADE_LINE);
        programmable_interrupt_controller_clear_mask(IRQ_RTC_LINE);
    }
    else
        realtime_clock_set_periodic_interrupt_enabled(0u);

    programmable_interrupt_controller_clear_mask(IRQ_TIMER_LINE);
    programmable_interrupt_controller_clear_mask(IRQ_KEYBOARD_LINE);
    asmutils_enable_interrupts();
}

void interrupt_request_set_timer_frequency_hz(uint32_t frequency_hz)
{
    if (frequency_hz == 0u)
        frequency_hz = 1u;
    interrupt_request_timer_target_frequency_hz = frequency_hz;
}

void interrupt_request_set_realtime_clock_periodic_enabled(uint8_t enabled)
{
    interrupt_request_rtc_periodic_enabled = (uint8_t) (enabled != 0u);
}

void interrupt_request_set_timer_owner_is_apic(uint8_t enabled)
{
    interrupt_request_timer_owner_is_apic = (uint8_t) (enabled != 0u);
}

void interrupt_request_set_line_owner_is_apic(uint8_t irq_line, uint8_t enabled)
{
    if (irq_line >= IRQ_LINE_COUNT)
        return;
    if (enabled)
        interrupt_request_apic_owned_lines |= (uint16_t) (1u << irq_line);
    else
        interrupt_request_apic_owned_lines &= (uint16_t) ~(1u << irq_line);
}

uint8_t interrupt_request_is_line_owner_apic(uint8_t irq_line)
{
    if (irq_line >= IRQ_LINE_COUNT)
        return 0u;
    return (uint8_t) ((interrupt_request_apic_owned_lines & (1u << irq_line)) ? 1u : 0u);
}

void interrupt_request_set_keyboard_owner_is_apic(uint8_t enabled)
{
    interrupt_request_set_line_owner_is_apic(IRQ_KEYBOARD_LINE, enabled);
}

uint32_t interrupt_request_get_tick_count(void) { return interrupt_request_tick_count; }

uint32_t interrupt_request_get_spurious_irq7_count(void) { return interrupt_request_spurious_irq7_count; }

uint32_t interrupt_request_get_spurious_irq15_count(void) { return interrupt_request_spurious_irq15_count; }

uint32_t interrupt_request_get_timer_frequency_hz(void) { return programmable_interval_timer_get_frequency_hz(); }

uint32_t interrupt_request_get_realtime_clock_interrupt_count(void)
{
    return realtime_clock_get_periodic_interrupt_count();
}

uint8_t interrupt_request_is_realtime_clock_periodic_enabled(void)
{
    return realtime_clock_is_periodic_interrupt_enabled();
}

uint8_t interrupt_request_is_timer_owner_apic(void) { return interrupt_request_timer_owner_is_apic; }

uint8_t interrupt_request_is_keyboard_owner_apic(void)
{
    return interrupt_request_is_line_owner_apic(IRQ_KEYBOARD_LINE);
}
