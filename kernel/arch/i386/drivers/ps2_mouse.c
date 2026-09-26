#include <kernel/drivers/ps2_mouse.h>

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/lib/asmutils.h>

#include <stddef.h>

#define PERSONAL_SYSTEM_2_DATA_PORT    0x60u
#define PERSONAL_SYSTEM_2_STATUS_PORT  0x64u
#define PERSONAL_SYSTEM_2_COMMAND_PORT 0x64u

#define PERSONAL_SYSTEM_2_STATUS_OUTPUT_FULL 0x01u
#define PERSONAL_SYSTEM_2_STATUS_INPUT_FULL  0x02u
#define PERSONAL_SYSTEM_2_STATUS_FROM_AUX    0x20u

#define PERSONAL_SYSTEM_2_COMMAND_ENABLE_AUXILIARY    0xA8u
#define PERSONAL_SYSTEM_2_COMMAND_READ_CONFIGURATION  0x20u
#define PERSONAL_SYSTEM_2_COMMAND_WRITE_CONFIGURATION 0x60u
#define PERSONAL_SYSTEM_2_COMMAND_WRITE_TO_AUXILIARY  0xD4u

#define PERSONAL_SYSTEM_2_MOUSE_SET_DEFAULTS   0xF6u
#define PERSONAL_SYSTEM_2_MOUSE_ENABLE_REPORTS 0xF4u
#define PERSONAL_SYSTEM_2_MOUSE_ACKNOWLEDGE    0xFAu

#define PERSONAL_SYSTEM_2_CONFIGURATION_AUXILIARY_INTERRUPT 0x02u
#define PERSONAL_SYSTEM_2_CONFIGURATION_AUXILIARY_CLOCK_OFF 0x20u

#define IRQ_MOUSE_LINE   12u
#define IRQ_MOUSE_VECTOR (PIC_VECTOR_OFFSET_SLAVE + (IRQ_MOUSE_LINE - 8u))

/** Master line the slave 8259 cascades into. */
#define IRQ_CASCADE_LINE 2u

/** Packet header bits. Bit 3 is always set, which is how a header is told from a data byte. */
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_BUTTON_LEFT   0x01u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_BUTTON_RIGHT  0x02u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_BUTTON_MIDDLE 0x04u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_ALWAYS_SET    0x08u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_X_NEGATIVE    0x10u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_Y_NEGATIVE    0x20u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_X_OVERFLOW    0x40u
#define PERSONAL_SYSTEM_2_MOUSE_HEADER_Y_OVERFLOW    0x80u

/**
 * @brief Bounded wait for the controller.
 * @details
 * Every wait on the controller is bounded. A machine with no auxiliary port
 * never clears the status bit being waited on, and an unbounded loop there is a
 * boot that hangs on hardware the kernel was not asked to require.
 */
#define PERSONAL_SYSTEM_2_SPIN_BUDGET 100000u

/** Power-of-two capacity so head/tail wrap with a mask, as in the keyboard ring. */
#define PERSONAL_SYSTEM_2_MOUSE_RING_CAPACITY 256u
#define PERSONAL_SYSTEM_2_MOUSE_RING_MASK     (PERSONAL_SYSTEM_2_MOUSE_RING_CAPACITY - 1u)

static volatile uint8_t personal_system_2_mouse_ring[PERSONAL_SYSTEM_2_MOUSE_RING_CAPACITY];
static volatile uint32_t personal_system_2_mouse_ring_head = 0u;
static volatile uint32_t personal_system_2_mouse_ring_tail = 0u;

static uint32_t personal_system_2_mouse_irq_count = 0u;
static uint32_t personal_system_2_mouse_dropped_byte_count = 0u;
static uint32_t personal_system_2_mouse_resynchronization_count = 0u;
static uint8_t personal_system_2_mouse_present = 0u;

static uint8_t personal_system_2_wait_for_input_clear(void)
{
    uint32_t budget = PERSONAL_SYSTEM_2_SPIN_BUDGET;

    while (budget-- != 0u)
    {
        if ((asmutils_input_byte(PERSONAL_SYSTEM_2_STATUS_PORT) & PERSONAL_SYSTEM_2_STATUS_INPUT_FULL) == 0u)
            return 1u;
    }
    return 0u;
}

static uint8_t personal_system_2_wait_for_output_full(void)
{
    uint32_t budget = PERSONAL_SYSTEM_2_SPIN_BUDGET;

    while (budget-- != 0u)
    {
        if ((asmutils_input_byte(PERSONAL_SYSTEM_2_STATUS_PORT) & PERSONAL_SYSTEM_2_STATUS_OUTPUT_FULL) != 0u)
            return 1u;
    }
    return 0u;
}

static uint8_t personal_system_2_write_command(uint8_t command)
{
    if (!personal_system_2_wait_for_input_clear())
        return 0u;
    asmutils_output_byte(PERSONAL_SYSTEM_2_COMMAND_PORT, command);
    return 1u;
}

static uint8_t personal_system_2_write_data(uint8_t value)
{
    if (!personal_system_2_wait_for_input_clear())
        return 0u;
    asmutils_output_byte(PERSONAL_SYSTEM_2_DATA_PORT, value);
    return 1u;
}

static uint8_t personal_system_2_read_data(uint8_t *out_value)
{
    if (!personal_system_2_wait_for_output_full())
        return 0u;
    *out_value = asmutils_input_byte(PERSONAL_SYSTEM_2_DATA_PORT);
    return 1u;
}

/**
 * @brief Sends one byte to the auxiliary device and consumes its acknowledgement.
 * @param value The byte.
 * @return 1 when the device acknowledged it.
 */
static uint8_t personal_system_2_mouse_send(uint8_t value)
{
    uint8_t response = 0u;

    if (!personal_system_2_write_command(PERSONAL_SYSTEM_2_COMMAND_WRITE_TO_AUXILIARY))
        return 0u;
    if (!personal_system_2_write_data(value))
        return 0u;
    if (!personal_system_2_read_data(&response))
        return 0u;
    return response == PERSONAL_SYSTEM_2_MOUSE_ACKNOWLEDGE ? 1u : 0u;
}

/**
 * @brief Does the controller hold a byte, and did it come from the auxiliary device?
 *
 * @note Both devices share port 0x60. Reading it on an interrupt that did not carry
 *       auxiliary data would steal a keystroke from the keyboard's ring, which is a bug you
 *       feel as characters that go missing while the mouse is moving.
 *
 * @param status The controller's status register.
 * @return 1 when the data port holds a mouse byte.
 */
static uint8_t personal_system_2_mouse_status_carries_auxiliary_byte(uint8_t status)
{
    return (status & PERSONAL_SYSTEM_2_STATUS_FROM_AUX) != 0u && (status & PERSONAL_SYSTEM_2_STATUS_OUTPUT_FULL) != 0u;
}

/**
 * @brief Pushes one byte into the ring, or counts it as dropped when the ring is full.
 * @param byte The byte read from the data port.
 */
static void personal_system_2_mouse_ring_push(uint8_t byte)
{
    const uint32_t head = personal_system_2_mouse_ring_head;
    const uint32_t tail = personal_system_2_mouse_ring_tail;

    if ((uint32_t) (head - tail) >= PERSONAL_SYSTEM_2_MOUSE_RING_CAPACITY)
    {
        ++personal_system_2_mouse_dropped_byte_count;
        return;
    }

    personal_system_2_mouse_ring[head & PERSONAL_SYSTEM_2_MOUSE_RING_MASK] = byte;
    personal_system_2_mouse_ring_head = head + 1u;
}

/**
 * @brief Acknowledges the controller that DELIVERED this interrupt, asked about THIS line.
 *
 * @note The first version copied the keyboard handler, including its
 *       `is_keyboard_owner_apic()` test — a name that says keyboard and reads like "the
 *       system". Once kernel.c handed IRQ1 to the IOAPIC that test turned true while IRQ12
 *       still arrived on the PIC, so the 8259 stopped being acknowledged and blocked the line
 *       after a single interrupt. The mouse went dead under QEMU and kept working in the
 *       browser emulator, which performs no handoff.
 */
static void personal_system_2_mouse_acknowledge_interrupt(void)
{
    if (interrupt_request_is_line_owner_apic(IRQ_MOUSE_LINE))
        advanced_pic_timer_backend_signal_end_of_interrupt();
    else
        programmable_interrupt_controller_send_end_of_interrupt(IRQ_MOUSE_LINE);
}

/**
 * @brief Moves a mouse byte into the ring, from interrupt context.
 * @param frame Unused.
 */
static void personal_system_2_mouse_interrupt_handler(const InterruptFrame_t *frame)
{
    const uint8_t status = asmutils_input_byte(PERSONAL_SYSTEM_2_STATUS_PORT);

    (void) frame;
    ++personal_system_2_mouse_irq_count;

    if (personal_system_2_mouse_status_carries_auxiliary_byte(status))
        personal_system_2_mouse_ring_push(asmutils_input_byte(PERSONAL_SYSTEM_2_DATA_PORT));

    personal_system_2_mouse_acknowledge_interrupt();
}

static uint8_t personal_system_2_mouse_ring_peek(uint32_t offset, uint8_t *out_byte)
{
    const uint32_t tail = personal_system_2_mouse_ring_tail;

    if ((uint32_t) (personal_system_2_mouse_ring_head - tail) <= offset)
        return 0u;
    *out_byte = personal_system_2_mouse_ring[(tail + offset) & PERSONAL_SYSTEM_2_MOUSE_RING_MASK];
    return 1u;
}

/**
 * @brief Unmasks the mouse line, and the cascade line it arrives through.
 *
 * @note IRQ12 arrives on the slave controller, which reaches the CPU through the master's
 *       cascade line. Unmasking 12 alone gets nothing if 2 is still masked — a symptom that
 *       reads exactly like "the mouse does not work".
 */
static void personal_system_2_mouse_unmask_line(void)
{
    programmable_interrupt_controller_clear_mask(IRQ_CASCADE_LINE);
    programmable_interrupt_controller_clear_mask(IRQ_MOUSE_LINE);
}

/**
 * @brief One axis of a packet, as a signed delta.
 *
 * @details The sign is the header's negative bit, applied as nine-bit two's complement.
 *
 * @note An overflowed axis carries no usable magnitude; reporting the truncated byte would
 *       send the view lurching. Zero is the honest reading.
 *
 * @param raw      The axis byte.
 * @param overflow Non-zero when the header flags the axis as overflowed.
 * @param negative Non-zero when the header flags the axis as negative.
 * @return The delta.
 */
static int32_t personal_system_2_mouse_axis_delta(uint8_t raw, uint8_t overflow, uint8_t negative)
{
    if (overflow != 0u)
        return 0;
    return negative != 0u ? (int32_t) raw - 256 : (int32_t) raw;
}

uint8_t personal_system_2_mouse_initialize(void)
{
    uint8_t configuration = 0u;

    personal_system_2_mouse_present = 0u;

    if (!personal_system_2_write_command(PERSONAL_SYSTEM_2_COMMAND_ENABLE_AUXILIARY))
        return 0u;

    if (!personal_system_2_write_command(PERSONAL_SYSTEM_2_COMMAND_READ_CONFIGURATION))
        return 0u;
    if (!personal_system_2_read_data(&configuration))
        return 0u;

    configuration |= PERSONAL_SYSTEM_2_CONFIGURATION_AUXILIARY_INTERRUPT;
    configuration &= (uint8_t) ~PERSONAL_SYSTEM_2_CONFIGURATION_AUXILIARY_CLOCK_OFF;

    if (!personal_system_2_write_command(PERSONAL_SYSTEM_2_COMMAND_WRITE_CONFIGURATION))
        return 0u;
    if (!personal_system_2_write_data(configuration))
        return 0u;

    if (!personal_system_2_mouse_send(PERSONAL_SYSTEM_2_MOUSE_SET_DEFAULTS))
        return 0u;
    if (!personal_system_2_mouse_send(PERSONAL_SYSTEM_2_MOUSE_ENABLE_REPORTS))
        return 0u;

    interrupt_service_routine_register_handler(IRQ_MOUSE_VECTOR, personal_system_2_mouse_interrupt_handler);

    personal_system_2_mouse_unmask_line();

    personal_system_2_mouse_present = 1u;
    return 1u;
}

uint8_t personal_system_2_mouse_is_present(void) { return personal_system_2_mouse_present; }

uint8_t personal_system_2_mouse_try_pop_packet(PersonalSystem2MousePacket_t *out_packet)
{
    uint8_t flags = 0u;
    uint8_t raw_x = 0u;
    uint8_t raw_y = 0u;

    if (out_packet == NULL)
        return 0u;

    for (;;)
    {
        if (!personal_system_2_mouse_ring_peek(0u, &flags))
            return 0u;

        if ((flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_ALWAYS_SET) == 0u)
        {
            personal_system_2_mouse_ring_tail = personal_system_2_mouse_ring_tail + 1u;
            ++personal_system_2_mouse_resynchronization_count;
            continue;
        }

        if (!personal_system_2_mouse_ring_peek(1u, &raw_x) || !personal_system_2_mouse_ring_peek(2u, &raw_y))
            return 0u;
        break;
    }

    personal_system_2_mouse_ring_tail = personal_system_2_mouse_ring_tail + 3u;

    out_packet->delta_x = personal_system_2_mouse_axis_delta(raw_x, flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_X_OVERFLOW,
                                                             flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_X_NEGATIVE);
    out_packet->delta_y = personal_system_2_mouse_axis_delta(raw_y, flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_Y_OVERFLOW,
                                                             flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_Y_NEGATIVE);
    out_packet->button_left = (flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_BUTTON_LEFT) != 0u ? 1u : 0u;
    out_packet->button_right = (flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_BUTTON_RIGHT) != 0u ? 1u : 0u;
    out_packet->button_middle = (flags & PERSONAL_SYSTEM_2_MOUSE_HEADER_BUTTON_MIDDLE) != 0u ? 1u : 0u;
    return 1u;
}

uint32_t personal_system_2_mouse_get_pending_byte_count(void)
{
    return (uint32_t) (personal_system_2_mouse_ring_head - personal_system_2_mouse_ring_tail);
}

uint32_t personal_system_2_mouse_get_irq_count(void) { return personal_system_2_mouse_irq_count; }

uint32_t personal_system_2_mouse_get_ring_capacity(void) { return PERSONAL_SYSTEM_2_MOUSE_RING_CAPACITY; }

uint32_t personal_system_2_mouse_get_dropped_byte_count(void) { return personal_system_2_mouse_dropped_byte_count; }

uint32_t personal_system_2_mouse_get_resynchronization_count(void)
{
    return personal_system_2_mouse_resynchronization_count;
}
