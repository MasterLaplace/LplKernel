#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/msi.h>
#include <kernel/memory/vmm.h>

#include <stddef.h>

#define MESSAGE_SIGNALED_INTERRUPT_FIRST_VECTOR            0x10u   /**< Vectors 0 to 15 are reserved for messages. */
#define MESSAGE_SIGNALED_INTERRUPT_LAST_VECTOR             0xFEu   /**< The last vector a message may carry. */
#define MESSAGE_SIGNALED_INTERRUPT_LARGEST_DESTINATION     0xFEu   /**< 8 bits, 0xFF the broadcast. */
#define MESSAGE_SIGNALED_INTERRUPT_DESTINATION_SHIFT       12u     /**< Where the destination sits in the address. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_CONTROL        0x02u   /**< Message Control, from the capability. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_TABLE          0x04u   /**< Table Offset and Table BIR, from it. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_SIZE_MASK      0x07FFu /**< Message Control: the table size, minus 1. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_FUNCTION_MASK  (1u << 14u) /**< Message Control: every entry masked. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENABLE         (1u << 15u) /**< Message Control: MSI-X on. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_INDICATOR      0x7u        /**< Table BIR: the Base Address Register. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENTRY_BYTES    16u         /**< One table entry. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ADDRESS        0u          /**< Entry: Message Address. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_UPPER_ADDRESS  4u          /**< Entry: Message Upper Address. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_DATA           8u          /**< Entry: Message Data. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_VECTOR_CONTROL 12u         /**< Entry: Vector Control, bit 0 the mask. */
#define MESSAGE_SIGNALED_INTERRUPT_EXTENDED_MASKED         1u          /**< Vector Control: the entry is masked. */

static MessageSignaledInterruptHandler_t message_signaled_interrupt_handlers[MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT] =
    {NULL}; /**< Each vector's handler. */
static void *message_signaled_interrupt_contexts[MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT] = {NULL}; /**< Theirs. */
/** Interrupts per vector and per slot, each cell written by the processor of its slot alone. */
static volatile uint32_t message_signaled_interrupt_counts[MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT]
                                                          [CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC] = {{0u}};

/**
 * @brief Counts the interrupt on this processor, runs the vector's handler, then ends the interrupt.
 */
static void message_signaled_interrupt_dispatch(const InterruptFrame_t *frame)
{
    const uint32_t index = frame->int_no - MESSAGE_SIGNALED_INTERRUPT_VECTOR_BASE;
    const uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_get_id());

    if (index < MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT)
    {
        if (slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
            ++message_signaled_interrupt_counts[index][slot];
        if (message_signaled_interrupt_handlers[index] != NULL)
            message_signaled_interrupt_handlers[index](message_signaled_interrupt_contexts[index]);
    }
    apic_send_eoi();
}

/**
 * @brief The address of byte @p field of entry @p entry of an open table.
 */
static volatile uint32_t *
message_signaled_interrupt_extended_field(const MessageSignaledInterruptExtendedTable_t *table, uint16_t entry,
                                          uint32_t field)
{
    return (volatile uint32_t *) (uintptr_t) (table->table +
                                              (uint32_t) entry * MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENTRY_BYTES +
                                              field);
}

/**
 * @brief Masks entry @p entry of an open table, or unmasks it, its other Vector Control bits kept.
 */
static void message_signaled_interrupt_extended_mask(const MessageSignaledInterruptExtendedTable_t *table,
                                                     uint16_t entry, bool masked)
{
    volatile uint32_t *const control =
        message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_VECTOR_CONTROL);

    *control = (*control & ~MESSAGE_SIGNALED_INTERRUPT_EXTENDED_MASKED) |
               (masked ? MESSAGE_SIGNALED_INTERRUPT_EXTENDED_MASKED : 0u);
}

/**
 * @brief Whether a device's message can reach a local APIC: in xAPIC mode always, in x2APIC mode only under a
 *        hypervisor, since bare metal needs interrupt remapping there.
 */
static bool message_signaled_interrupt_platform_delivers(void)
{
    CpuIdentity_t identity;

    if (!apic_is_x2apic_active())
        return true;
    cpu_identity_read(&identity);
    return identity.hypervisor;
}

/**
 * @brief Writes the Message Control register of the MSI-X capability of @p table's function.
 */
static void message_signaled_interrupt_extended_control(const MessageSignaledInterruptExtendedTable_t *table,
                                                        uint16_t control)
{
    peripheral_component_interconnect_config_write_word(
        table->bus, table->device, table->function,
        (uint8_t) (table->capability + MESSAGE_SIGNALED_INTERRUPT_EXTENDED_CONTROL), control);
}

bool message_signaled_interrupt_compose(uint32_t apic_id, uint8_t vector, uint32_t *out_address, uint32_t *out_data)
{
    if (apic_id > MESSAGE_SIGNALED_INTERRUPT_LARGEST_DESTINATION || vector < MESSAGE_SIGNALED_INTERRUPT_FIRST_VECTOR ||
        vector > MESSAGE_SIGNALED_INTERRUPT_LAST_VECTOR)
        return false;
    *out_address = MESSAGE_SIGNALED_INTERRUPT_ADDRESS_BASE | apic_id << MESSAGE_SIGNALED_INTERRUPT_DESTINATION_SHIFT;
    *out_data = vector;
    return true;
}

bool message_signaled_interrupt_allocate(MessageSignaledInterruptHandler_t handler, void *context, uint8_t *out_vector)
{
    if (handler == NULL)
        return false;
    for (uint32_t index = 0u; index < MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT; ++index)
    {
        if (message_signaled_interrupt_handlers[index] != NULL)
            continue;
        message_signaled_interrupt_contexts[index] = context;
        message_signaled_interrupt_handlers[index] = handler;
        *out_vector = (uint8_t) (MESSAGE_SIGNALED_INTERRUPT_VECTOR_BASE + index);
        interrupt_service_routine_register_handler(*out_vector, message_signaled_interrupt_dispatch);
        return true;
    }
    return false;
}

void message_signaled_interrupt_release(uint8_t vector)
{
    const uint32_t index = (uint32_t) vector - MESSAGE_SIGNALED_INTERRUPT_VECTOR_BASE;

    if (index >= MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT)
        return;
    message_signaled_interrupt_handlers[index] = NULL;
    message_signaled_interrupt_contexts[index] = NULL;
}

uint32_t message_signaled_interrupt_count(uint8_t vector, uint32_t slot)
{
    const uint32_t index = (uint32_t) vector - MESSAGE_SIGNALED_INTERRUPT_VECTOR_BASE;

    if (index >= MESSAGE_SIGNALED_INTERRUPT_VECTOR_COUNT || slot >= CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC)
        return 0u;
    return message_signaled_interrupt_counts[index][slot];
}

bool message_signaled_interrupt_extended_open(const PeripheralComponentInterconnectDevice_t *device,
                                              MessageSignaledInterruptExtendedTable_t *out)
{
    PeripheralComponentInterconnectBaseAddressRegister_t window;

    out->table = 0u;
    out->bus = device->bus;
    out->device = device->device;
    out->function = device->function;
    out->capability = peripheral_component_interconnect_find_capability(device->bus, device->device, device->function,
                                                                        MESSAGE_SIGNALED_INTERRUPT_CAPABILITY_EXTENDED);
    if (out->capability == 0u)
        return false;

    const uint16_t control = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function,
        (uint8_t) (out->capability + MESSAGE_SIGNALED_INTERRUPT_EXTENDED_CONTROL));
    const uint32_t placement = peripheral_component_interconnect_config_read_dword(
        device->bus, device->device, device->function,
        (uint8_t) (out->capability + MESSAGE_SIGNALED_INTERRUPT_EXTENDED_TABLE));
    const uint32_t offset = placement & ~MESSAGE_SIGNALED_INTERRUPT_EXTENDED_INDICATOR;

    out->entries = (uint16_t) ((control & MESSAGE_SIGNALED_INTERRUPT_EXTENDED_SIZE_MASK) + 1u);
    if (!peripheral_component_interconnect_read_base_address_register(
            device->bus, device->device, device->function,
            (uint8_t) (placement & MESSAGE_SIGNALED_INTERRUPT_EXTENDED_INDICATOR), &window) ||
        !peripheral_component_interconnect_memory_window_is_reachable(&window) ||
        (uint64_t) offset + (uint64_t) out->entries * MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENTRY_BYTES > window.size)
        return false;

    out->table = kernel_vmm_map_device_window(
        (uint32_t) window.base + offset, (uint32_t) out->entries * MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENTRY_BYTES);
    if (out->table == 0u)
        return false;

    peripheral_component_interconnect_enable_memory_and_bus_mastering(device);
    peripheral_component_interconnect_disable_pin_interrupt(device);
    message_signaled_interrupt_extended_control(out, (uint16_t) (control |
                                                                 MESSAGE_SIGNALED_INTERRUPT_EXTENDED_FUNCTION_MASK |
                                                                 MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENABLE));
    for (uint16_t entry = 0u; entry < out->entries; ++entry)
        message_signaled_interrupt_extended_mask(out, entry, true);
    message_signaled_interrupt_extended_control(out,
                                                (uint16_t) ((control | MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ENABLE) &
                                                            ~MESSAGE_SIGNALED_INTERRUPT_EXTENDED_FUNCTION_MASK));
    return true;
}

bool message_signaled_interrupt_extended_aim(const MessageSignaledInterruptExtendedTable_t *table, uint16_t entry,
                                             uint32_t slot, uint8_t vector)
{
    uint32_t address = 0u;
    uint32_t data = 0u;

    if (table->table == 0u || entry >= table->entries)
        return false;
    message_signaled_interrupt_extended_mask(table, entry, true);
    if (!message_signaled_interrupt_platform_delivers() ||
        !message_signaled_interrupt_compose(cpu_topology_get_apic_id_at_slot(slot), vector, &address, &data))
        return false;
    *message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ADDRESS) = address;
    *message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_UPPER_ADDRESS) = 0u;
    *message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_DATA) = data;
    message_signaled_interrupt_extended_mask(table, entry, false);
    return true;
}
