#include <kernel/acpi/tables.h>
#include <kernel/config.h>
#include <kernel/cpu/helpers/acpi_helper.h>
#include <kernel/diag/telemetry.h>

void write_acpi_tables_info(Serial_t *serial)
{
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory =
        advanced_configuration_and_power_interface_directory();
    char signatures[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_TABLE_COUNT * 5u + 1u];
    uint32_t length = 0u;
    uint32_t bad_checksums = 0u;

    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceTable_t *table = &directory->tables[index];

        if (index != 0u)
            signatures[length++] = ',';
        for (uint32_t letter = 0u; letter < sizeof(table->signature); ++letter)
        {
            const char character = table->signature[letter];
            signatures[length++] = (character > ' ' && character <= '~' && character != ',') ? character : '_';
        }
        if (!table->checksum_valid)
            ++bad_checksums;
    }
    signatures[length] = '\0';

    kernel_telemetry_begin_record(serial, "acpi");
    kernel_telemetry_write_boolean("walked", directory->walked);
    kernel_telemetry_write_unsigned("rsdp_revision", directory->rsdp_revision);
    kernel_telemetry_write_text("root", directory->extended_root ? "XSDT" : "RSDT");
    kernel_telemetry_write_unsigned("tables", directory->table_count);
    kernel_telemetry_write_unsigned("unreachable", directory->unreachable_count);
    kernel_telemetry_write_unsigned("bad_checksums", bad_checksums);
    kernel_telemetry_write_text("signatures", signatures);
    kernel_telemetry_end_record();
}

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
    serial_write_unsigned(serial, advanced_configuration_and_power_interface_madt()->processor_count);
    serial_write_string(serial, ", iso_count=");
    serial_write_int(serial,
                     (int32_t) advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count());
    serial_write_string(serial, "\n");
}

void write_acpi_madt_record(Serial_t *serial)
{
    const AdvancedConfigurationAndPowerInterfaceMadtState_t *madt = advanced_configuration_and_power_interface_madt();

    kernel_telemetry_begin_record(serial, "acpi.madt");
    kernel_telemetry_write_boolean("read", madt->available);
    kernel_telemetry_write_boolean("checksum_valid", madt->checksum_valid);
    kernel_telemetry_write_unsigned("processors", madt->processor_count);
    kernel_telemetry_write_unsigned("processors_dropped", madt->processors_dropped);
    kernel_telemetry_write_unsigned("online_capable", madt->online_capable_count);
    kernel_telemetry_write_unsigned("disabled", madt->disabled_count);
    kernel_telemetry_write_unsigned("invalid", madt->invalid_count);
    kernel_telemetry_write_unsigned("nmis", madt->non_maskable_interrupt_count);
    kernel_telemetry_write_unsigned("nmis_dropped", madt->non_maskable_interrupts_dropped);
    kernel_telemetry_write_unsigned("skipped", madt->skipped_count);
    kernel_telemetry_write_unsigned("io_apics", madt->io_apic_count);
    kernel_telemetry_write_unsigned("io_apics_dropped", madt->io_apics_dropped);
    kernel_telemetry_write_unsigned("overrides", madt->interrupt_source_override_count);
    kernel_telemetry_write_unsigned("overrides_dropped", madt->interrupt_source_overrides_dropped);
    kernel_telemetry_end_record();
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
