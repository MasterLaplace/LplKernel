#include <kernel/config.h>
#include <kernel/cpu/helpers/pci_helper.h>
#include <kernel/cpu/pci.h>

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
            ++index; /* the high half occupies the next slot */
    }
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
