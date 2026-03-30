#include <kernel/config.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/helpers/acpi_helper.h>

void write_acpi_madt_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: ACPI MADT state=");
    serial_write_string(serial, advanced_configuration_and_power_interface_madt_get_state_name());
    serial_write_string(serial, ", madt=");
    serial_write_int(serial, (int32_t) advanced_configuration_and_power_interface_madt_is_available());
    serial_write_string(serial, ", lapic_phys=");
    serial_write_hex32(serial, advanced_configuration_and_power_interface_madt_get_local_apic_physical_base());
    serial_write_string(serial, ", ioapic_count=");
    serial_write_int(serial, (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_count());
    serial_write_string(serial, ", lapic_count=");
    serial_write_int(serial, (int32_t) advanced_configuration_and_power_interface_madt_get_local_apic_count());
    serial_write_string(serial, ", iso_count=");
    serial_write_int(serial,
                     (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count());
    serial_write_string(serial, "\n");
}

void write_acpi_ioapics_info(Serial_t *serial)
{
    for (uint8_t ioapic_index = 0u; ioapic_index < advanced_configuration_and_power_interface_madt_get_io_apic_count();
         ++ioapic_index)
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: ACPI IOAPIC[");
        serial_write_int(serial, (int32_t) ioapic_index);
        serial_write_string(serial, "] id=");
        serial_write_int(serial,
                         (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_id(ioapic_index));
        serial_write_string(serial, ", phys=");
        serial_write_hex32(serial,
                           advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(ioapic_index));
        serial_write_string(serial, ", gsi_base=");
        serial_write_int(serial,
                         (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(ioapic_index));
        serial_write_string(serial, "\n");
    }
}

void write_acpi_isos_info(Serial_t *serial)
{
    for (uint8_t iso_index = 0u;
         iso_index < advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count(); ++iso_index)
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: ACPI ISO[");
        serial_write_int(serial, (int32_t) iso_index);
        serial_write_string(serial, "] bus=");
        serial_write_int(
            serial,
            (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_bus(iso_index));
        serial_write_string(serial, ", source_irq=");
        serial_write_int(
            serial, (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_source_irq(
                        iso_index));
        serial_write_string(serial, ", gsi=");
        serial_write_int(
            serial,
            (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_gsi(iso_index));
        serial_write_string(serial, ", flags=");
        serial_write_hex16(
            serial, advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(iso_index));
        serial_write_string(serial, "\n");
    }
}

void write_acpi_isa_routing_info(Serial_t *serial)
{
    for (uint8_t isa_irq = 0u; isa_irq <= 1u; ++isa_irq)
    {
        uint32_t mapped_gsi = 0u;
        uint16_t mapped_flags = 0u;
        uint8_t ioapic_index = 0u;

        if (!advanced_configuration_and_power_interface_madt_resolve_isa_irq(isa_irq, &mapped_gsi, &mapped_flags))
            continue;

        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: ACPI route ISA IRQ");
        serial_write_int(serial, (int32_t) isa_irq);
        serial_write_string(serial, " -> GSI ");
        serial_write_int(serial, (int32_t) mapped_gsi);
        serial_write_string(serial, ", flags=");
        serial_write_hex16(serial, mapped_flags);

        if (advanced_configuration_and_power_interface_madt_find_io_apic_for_gsi(mapped_gsi, &ioapic_index))
        {
            serial_write_string(serial, ", ioapic_index=");
            serial_write_int(serial, (int32_t) ioapic_index);
            serial_write_string(serial, ", ioapic_id=");
            serial_write_int(serial,
                             (int32_t) advanced_configuration_and_power_interface_madt_get_io_apic_id(ioapic_index));
        }
        else
            serial_write_string(serial, ", ioapic_index=none");

        serial_write_string(serial, "\n");
    }
}
