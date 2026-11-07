#include <kernel/config.h>
#include <kernel/cpu/helpers/pci_helper.h>
#include <kernel/cpu/pci.h>
#include <kernel/diag/telemetry.h>

static const char *peripheral_component_interconnect_class_name(uint8_t class_code)
{
    switch (class_code)
    {
    case 0x00u: return "unclassified";
    case 0x01u: return "mass-storage";
    case 0x02u: return "network";
    case 0x03u: return "display";
    case 0x04u: return "multimedia";
    case 0x06u: return "bridge";
    case 0x07u: return "communication";
    case 0x0Cu: return "serial-bus";
    default: return "other";
    }
}

/**
 * @brief Writes every implemented base address register of a device to serial.
 *
 * @note A 64-bit register's high half occupies the next slot, which is skipped.
 *
 * @param serial Serial port the lines are written to.
 * @param device The device.
 */
static void
peripheral_component_interconnect_write_base_address_registers(Serial_t *serial,
                                                               const PeripheralComponentInterconnectDevice_t *device)
{
    for (uint8_t index = 0u; index < PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT; ++index)
    {
        PeripheralComponentInterconnectBaseAddressRegister_t bar;

        if (!peripheral_component_interconnect_read_base_address_register(device->bus, device->device, device->function,
                                                                          index, &bar))
            continue;

        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]:   BAR");
        serial_write_int(serial, (int32_t) index);
        serial_write_string(serial, bar.is_io ? " io  base=" : " mem base=");
        serial_write_hex32(serial, (uint32_t) bar.base);
        serial_write_string(serial, " size=");
        serial_write_hex32(serial, (uint32_t) bar.size);
        if (bar.is_64bit)
            serial_write_string(serial, " 64-bit");
        if (bar.prefetchable)
            serial_write_string(serial, " prefetch");
        serial_write_string(serial, "\n");

        if (bar.is_64bit)
            ++index;
    }
}

#define PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_LISTED_CAPABILITIES                                                   \
    16u /**< Extended capabilities a function's record names; its count says how many there are. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_CAPABILITY_TEXT                                                       \
    11u /**< Characters of one capability in a record, as `0001v2@100,`. */

static PeripheralComponentInterconnectExpressExtendedCapability_t
    peripheral_component_interconnect_helper_capabilities[PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_LISTED_CAPABILITIES];

/**
 * @brief Writes the low @p digits hexadecimal digits of @p value at @p text, the most significant first.
 *
 * @return Past the last digit written.
 */
static char *peripheral_component_interconnect_helper_put_hexadecimal(char *text, uint32_t value, uint32_t digits)
{
    static const char hexadecimal[] = "0123456789ABCDEF";

    for (uint32_t index = 0u; index < digits; ++index)
        text[index] = hexadecimal[(value >> (4u * (digits - 1u - index))) & 0xFu];
    return text + digits;
}

/**
 * @brief Writes @p count capabilities as one record value, `<identifier>v<version>@<offset>` apiece, comma-separated.
 */
static void peripheral_component_interconnect_helper_put_capabilities(
    char *text, const PeripheralComponentInterconnectExpressExtendedCapability_t *capabilities, uint32_t count)
{
    for (uint32_t index = 0u; index < count; ++index)
    {
        text = peripheral_component_interconnect_helper_put_hexadecimal(text, capabilities[index].identifier, 4u);
        *text++ = 'v';
        text = peripheral_component_interconnect_helper_put_hexadecimal(text, capabilities[index].version, 1u);
        *text++ = '@';
        text = peripheral_component_interconnect_helper_put_hexadecimal(text, capabilities[index].offset, 3u);
        if (index + 1u < count)
            *text++ = ',';
    }
    *text = '\0';
}

/**
 * @brief Writes the `[LPLTLM] pci.function` record of one function: whether it is a PCI Express function and, for one,
 *        how the walk of its extended capability list through ECAM ended and what it listed.
 */
static void
peripheral_component_interconnect_helper_write_function_record(Serial_t *serial,
                                                               const PeripheralComponentInterconnectDevice_t *device)
{
    char text[PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_LISTED_CAPABILITIES *
                  PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_CAPABILITY_TEXT +
              1u];
    uint32_t count = 0u;
    const bool express =
        peripheral_component_interconnect_find_capability(device->bus, device->device, device->function,
                                                          PERIPHERAL_COMPONENT_INTERCONNECT_CAPABILITY_EXPRESS) != 0u;

    kernel_telemetry_begin_record(serial, "pci.function");
    kernel_telemetry_write_unsigned("bus", device->bus);
    kernel_telemetry_write_unsigned("device", device->device);
    kernel_telemetry_write_unsigned("function", device->function);
    kernel_telemetry_write_hexadecimal("vendor", device->vendor_id);
    kernel_telemetry_write_hexadecimal("device_id", device->device_id);
    kernel_telemetry_write_boolean("express", express);
    if (express)
    {
        const PeripheralComponentInterconnectExpressWalk_t walk =
            peripheral_component_interconnect_express_list_extended_capabilities(
                0u, device->bus, device->device, device->function,
                peripheral_component_interconnect_helper_capabilities,
                PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_LISTED_CAPABILITIES, &count);
        const uint32_t listed = (count < PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_LISTED_CAPABILITIES) ?
                                    count :
                                    PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_LISTED_CAPABILITIES;

        peripheral_component_interconnect_helper_put_capabilities(
            text, peripheral_component_interconnect_helper_capabilities, listed);
        kernel_telemetry_write_text("walk", peripheral_component_interconnect_express_walk_name(walk));
        kernel_telemetry_write_unsigned("extended", count);
        if (listed != 0u)
            kernel_telemetry_write_text("capabilities", text);
    }
    kernel_telemetry_end_record();
}

void write_peripheral_component_interconnect_info(Serial_t *serial)
{
    uint32_t count = peripheral_component_interconnect_get_device_count();

    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PCI enumeration: ");
    serial_write_int(serial, (int32_t) count);
    serial_write_string(serial, " device(s)\n");

    for (uint32_t index = 0u; index < count; ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (!device)
            continue;

        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PCI ");
        serial_write_hex8(serial, device->bus);
        serial_write_string(serial, ":");
        serial_write_hex8(serial, device->device);
        serial_write_string(serial, ".");
        serial_write_hex8(serial, device->function);
        serial_write_string(serial, " id=");
        serial_write_hex16(serial, device->vendor_id);
        serial_write_string(serial, ":");
        serial_write_hex16(serial, device->device_id);
        serial_write_string(serial, " class=");
        serial_write_hex8(serial, device->class_code);
        serial_write_string(serial, "/");
        serial_write_hex8(serial, device->subclass);
        serial_write_string(serial, " (");
        serial_write_string(serial, peripheral_component_interconnect_class_name(device->class_code));
        serial_write_string(serial, ")\n");

        peripheral_component_interconnect_write_base_address_registers(serial, device);
    }
}

void write_peripheral_component_interconnect_function_records(Serial_t *serial)
{
    for (uint32_t index = 0u; index < peripheral_component_interconnect_get_device_count(); ++index)
        peripheral_component_interconnect_helper_write_function_record(
            serial, peripheral_component_interconnect_get_device(index));
}

void write_peripheral_component_interconnect_express_record(Serial_t *serial)
{
    uint32_t refused = 0u;
    const uint32_t declared = peripheral_component_interconnect_express_declared_windows(&refused);

    kernel_telemetry_begin_record(serial, "pci.express");
    kernel_telemetry_write_unsigned("windows_declared", declared);
    kernel_telemetry_write_unsigned("windows_refused", refused);
    kernel_telemetry_end_record();
}
