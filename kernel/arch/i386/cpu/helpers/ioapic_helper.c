#include <kernel/config.h>
#include <kernel/cpu/helpers/ioapic_helper.h>
#include <kernel/cpu/ioapic.h>

void write_ioapic_scaffold_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: IOAPIC scaffold state=");
    serial_write_string(serial, input_output_advanced_programmable_interrupt_controller_get_state_name());
    serial_write_string(serial, ", mapped=");
    serial_write_int(serial, (int32_t) input_output_advanced_programmable_interrupt_controller_get_mapped_count());
    serial_write_string(serial, ", routes=");
    serial_write_int(serial,
                     (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_count());
    serial_write_string(serial, "\n");
}

void write_ioapic_routes_info(Serial_t *serial)
{
    for (uint8_t route_index = 0u;
         route_index < input_output_advanced_programmable_interrupt_controller_get_programmed_route_count();
         ++route_index)
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: IOAPIC route[");
        serial_write_int(serial, (int32_t) route_index);
        serial_write_string(serial, "] isa_irq=");
        serial_write_int(
            serial,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(route_index));
        serial_write_string(serial, ", gsi=");
        serial_write_int(
            serial,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_gsi(route_index));
        serial_write_string(serial, ", vector=");
        serial_write_int(
            serial,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_vector(route_index));
        serial_write_string(serial, ", ioapic_index=");
        serial_write_int(
            serial,
            (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_index(
                route_index));
        serial_write_string(serial, ", ioapic_id=");
        serial_write_int(
            serial, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_id(
                        route_index));
        serial_write_string(serial, ", masked=");
        serial_write_int(
            serial, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(
                        route_index));
        serial_write_string(serial, ", iso_flags=");
        serial_write_hex16(
            serial,
            input_output_advanced_programmable_interrupt_controller_get_programmed_route_iso_flags(route_index));
        serial_write_string(serial, ", dest_apic_id=");
        serial_write_int(
            serial, (int32_t) input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(
                        route_index));
        serial_write_string(serial, "\n");
    }
}
