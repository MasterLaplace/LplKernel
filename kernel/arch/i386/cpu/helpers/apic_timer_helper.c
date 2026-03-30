#include <kernel/config.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/helpers/apic_timer_helper.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/irq.h>

void write_apic_late_init_state_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC late init state=");
    serial_write_string(serial, advanced_pic_timer_backend_name());
    serial_write_string(serial, ", lapic_phys=");
    serial_write_hex32(serial, advanced_pic_timer_backend_get_local_apic_physical_base());
    serial_write_string(serial, ", mmio_mapped=");
    serial_write_int(serial, (int32_t) advanced_pic_timer_backend_is_local_apic_mmio_mapped());
    serial_write_string(serial, ", lapic_ver=");
    serial_write_hex32(serial, advanced_pic_timer_backend_get_local_apic_version_register());
    serial_write_string(serial, "\n");
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC IPI framework initialized\n");
}

void write_apic_late_init_skipped_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC late init skipped state=");
    serial_write_string(serial, advanced_pic_timer_backend_name());
    serial_write_string(serial, "\n");
}

void write_ioapic_keyboard_handoff_info(Serial_t *serial, uint8_t success)
{
    if (success)
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: IOAPIC keyboard handoff state=");
        serial_write_string(serial, input_output_advanced_programmable_interrupt_controller_get_state_name());
        serial_write_string(serial, ", irq1_owner_apic=");
        serial_write_int(serial, (int32_t) interrupt_request_is_keyboard_owner_apic());
        serial_write_string(serial, "\n");
    }
    else
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: IOAPIC keyboard handoff fallback state=");
        serial_write_string(serial, input_output_advanced_programmable_interrupt_controller_get_state_name());
        serial_write_string(serial, ", irq1_owner_apic=");
        serial_write_int(serial, (int32_t) interrupt_request_is_keyboard_owner_apic());
        serial_write_string(serial, "\n");
    }
}

void write_ioapic_keyboard_policy_fallback_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: IOAPIC keyboard ownership policy=pic-fallback\n");
}

void write_apic_calibration_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC calibration state=");
    serial_write_string(serial, advanced_pic_timer_backend_name());
    serial_write_string(serial, ", lapic_timer_hz_estimate=");
    serial_write_int(serial, (int32_t) advanced_pic_timer_backend_get_calibrated_timer_frequency_hz());
    serial_write_string(serial, "\n");
}

void write_apic_calibration_skipped_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC calibration skipped state=");
    serial_write_string(serial, advanced_pic_timer_backend_name());
    serial_write_string(serial, "\n");
}

void write_apic_owner_handoff_info(Serial_t *serial, uint8_t success)
{
    if (success)
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC owner handoff state=");
        serial_write_string(serial, advanced_pic_timer_backend_name());
        serial_write_string(serial, ", apic_owner=");
        serial_write_int(serial, (int32_t) interrupt_request_is_timer_owner_apic());
        serial_write_string(serial, ", apic_periodic=");
        serial_write_int(serial, (int32_t) advanced_pic_timer_backend_is_periodic_mode_enabled());
        serial_write_string(serial, "\n");
    }
    else
    {
        serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC owner handoff fallback state=");
        serial_write_string(serial, advanced_pic_timer_backend_name());
        serial_write_string(serial, ", apic_owner=");
        serial_write_int(serial, (int32_t) interrupt_request_is_timer_owner_apic());
        serial_write_string(serial, ", apic_periodic=");
        serial_write_int(serial, (int32_t) advanced_pic_timer_backend_is_periodic_mode_enabled());
        serial_write_string(serial, "\n");
    }
}

void write_apic_owner_policy_fallback_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: APIC ownership policy=pit-fallback\n");
}
