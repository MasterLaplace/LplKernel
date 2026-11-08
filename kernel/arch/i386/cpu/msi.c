#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/device_interrupt.h>
#include <kernel/cpu/msi.h>
#include <kernel/memory/vmm.h>

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

#define MESSAGE_SIGNALED_INTERRUPT_CONTROL         0x02u      /**< Message Control, from the MSI capability. */
#define MESSAGE_SIGNALED_INTERRUPT_ADDRESS         0x04u      /**< Message Address, from it. */
#define MESSAGE_SIGNALED_INTERRUPT_UPPER_ADDRESS   0x08u      /**< Message Upper Address, in the 64-bit form. */
#define MESSAGE_SIGNALED_INTERRUPT_DATA_NARROW     0x08u      /**< Message Data, in the 32-bit form. */
#define MESSAGE_SIGNALED_INTERRUPT_DATA_WIDE       0x0Cu      /**< Message Data, in the 64-bit form. */
#define MESSAGE_SIGNALED_INTERRUPT_MASK_NARROW     0x0Cu      /**< Mask Bits, in the 32-bit form. */
#define MESSAGE_SIGNALED_INTERRUPT_MASK_WIDE       0x10u      /**< Mask Bits, in the 64-bit form. */
#define MESSAGE_SIGNALED_INTERRUPT_ENABLED         (1u << 0u) /**< Message Control: MSI on. */
#define MESSAGE_SIGNALED_INTERRUPT_MULTIPLE_ENABLE (7u << 4u) /**< Message Control: Multiple Message Enable. */
#define MESSAGE_SIGNALED_INTERRUPT_WIDE            (1u << 7u) /**< Message Control: 64-bit address capable. */
#define MESSAGE_SIGNALED_INTERRUPT_MASKABLE        (1u << 8u) /**< Message Control: per-vector masking capable. */

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
 * @brief Writes the Message Control register of the MSI-X capability of @p table's function.
 */
static void message_signaled_interrupt_extended_control(const MessageSignaledInterruptExtendedTable_t *table,
                                                        uint16_t control)
{
    peripheral_component_interconnect_config_write_word(
        table->bus, table->device, table->function,
        (uint8_t) (table->capability + MESSAGE_SIGNALED_INTERRUPT_EXTENDED_CONTROL), control);
}

/**
 * @brief Writes the dword at @p offset of the MSI capability at @p capability of @p device.
 */
static void message_signaled_interrupt_write(const PeripheralComponentInterconnectDevice_t *device, uint8_t capability,
                                             uint8_t offset, uint32_t value)
{
    peripheral_component_interconnect_config_write_dword(device->bus, device->device, device->function,
                                                         (uint8_t) (capability + offset), value);
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
    if (!device_interrupt_platform_delivers() ||
        !message_signaled_interrupt_compose(cpu_topology_get_apic_id_at_slot(slot), vector, &address, &data))
        return false;
    *message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_ADDRESS) = address;
    *message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_UPPER_ADDRESS) = 0u;
    *message_signaled_interrupt_extended_field(table, entry, MESSAGE_SIGNALED_INTERRUPT_EXTENDED_DATA) = data;
    message_signaled_interrupt_extended_mask(table, entry, false);
    return true;
}

bool message_signaled_interrupt_enable(const PeripheralComponentInterconnectDevice_t *device, uint32_t slot,
                                       uint8_t vector)
{
    const uint8_t capability = peripheral_component_interconnect_find_capability(
        device->bus, device->device, device->function, MESSAGE_SIGNALED_INTERRUPT_CAPABILITY);
    uint32_t address = 0u;
    uint32_t data = 0u;

    if (capability == 0u || !device_interrupt_platform_delivers() ||
        !message_signaled_interrupt_compose(cpu_topology_get_apic_id_at_slot(slot), vector, &address, &data))
        return false;

    const uint16_t control = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, (uint8_t) (capability + MESSAGE_SIGNALED_INTERRUPT_CONTROL));
    const bool wide = (control & MESSAGE_SIGNALED_INTERRUPT_WIDE) != 0u;
    const uint16_t single = (uint16_t) (control & ~MESSAGE_SIGNALED_INTERRUPT_MULTIPLE_ENABLE);

    peripheral_component_interconnect_config_write_word(device->bus, device->device, device->function,
                                                        (uint8_t) (capability + MESSAGE_SIGNALED_INTERRUPT_CONTROL),
                                                        (uint16_t) (single & ~MESSAGE_SIGNALED_INTERRUPT_ENABLED));
    message_signaled_interrupt_write(device, capability, MESSAGE_SIGNALED_INTERRUPT_ADDRESS, address);
    if (wide)
        message_signaled_interrupt_write(device, capability, MESSAGE_SIGNALED_INTERRUPT_UPPER_ADDRESS, 0u);
    peripheral_component_interconnect_config_write_word(
        device->bus, device->device, device->function,
        (uint8_t) (capability + (wide ? MESSAGE_SIGNALED_INTERRUPT_DATA_WIDE : MESSAGE_SIGNALED_INTERRUPT_DATA_NARROW)),
        (uint16_t) data);
    if ((control & MESSAGE_SIGNALED_INTERRUPT_MASKABLE) != 0u)
        message_signaled_interrupt_write(
            device, capability, wide ? MESSAGE_SIGNALED_INTERRUPT_MASK_WIDE : MESSAGE_SIGNALED_INTERRUPT_MASK_NARROW,
            0u);
    peripheral_component_interconnect_enable_memory_and_bus_mastering(device);
    peripheral_component_interconnect_disable_pin_interrupt(device);
    peripheral_component_interconnect_config_write_word(device->bus, device->device, device->function,
                                                        (uint8_t) (capability + MESSAGE_SIGNALED_INTERRUPT_CONTROL),
                                                        (uint16_t) (single | MESSAGE_SIGNALED_INTERRUPT_ENABLED));
    return true;
}
