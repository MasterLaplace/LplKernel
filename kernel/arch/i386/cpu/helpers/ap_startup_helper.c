#include <kernel/config.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/helpers/ap_startup_helper.h>

void write_ap_bootstrap_init_info(Serial_t *serial, uint8_t ap_bootstrap_ok)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP bootstrap init=");
    serial_write_int(serial, (int32_t) ap_bootstrap_ok);
    serial_write_string(serial, ", ap_count=");
    serial_write_int(serial, (int32_t) application_processor_bootstrap_get_ap_count());
    serial_write_string(serial, ", kernel_cr3=");
    serial_write_hex32(serial, application_processor_startup_get_kernel_cr3());
    serial_write_string(serial, "\n");
}

void write_ap_startup_skipped_ipi_not_ready_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP startup skipped (IPI not ready)\n");
}

void write_ap_startup_skipped_identity_map_unavailable_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP startup skipped (identity map unavailable)\n");
}

void write_ap_startup_skipped_trampoline_install_failed_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP startup skipped (trampoline install failed)\n");
}

void write_ap_trampoline_installed_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP trampoline installed, vector=");
    serial_write_hex8(serial, application_processor_trampoline_get_startup_vector());
    serial_write_string(serial, "\n");
}

void write_ap_startup_dispatch_info(Serial_t *serial, ApplicationProcessorBootstrapEntry_t *entry, uint8_t sequence_ok,
                                    uint8_t ack_ok, uint8_t c_entry_ok, uint8_t attempts_used)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP startup dispatch apic_id=");
    serial_write_int(serial, (int32_t) entry->apic_id);
    serial_write_string(serial, ", slot=");
    serial_write_int(serial, (int32_t) entry->logical_slot);
    serial_write_string(serial, ", vector=");
    serial_write_hex8(serial, application_processor_trampoline_get_startup_vector());
    serial_write_string(serial, ", delivered=");
    serial_write_int(serial, (int32_t) (sequence_ok && ack_ok));
    serial_write_string(serial, ", seq_ok=");
    serial_write_int(serial, (int32_t) sequence_ok);
    serial_write_string(serial, ", ack_ok=");
    serial_write_int(serial, (int32_t) ack_ok);
    serial_write_string(serial, ", c_entry_ok=");
    serial_write_int(serial, (int32_t) c_entry_ok);
    serial_write_string(serial, ", ack_word=");
    serial_write_hex16(serial, application_processor_trampoline_get_acknowledgement_word());
    serial_write_string(serial, ", c_entry_word=");
    serial_write_hex16(serial, application_processor_trampoline_get_c_entry_word());
    serial_write_string(serial, ", attempts=");
    serial_write_int(serial, (int32_t) attempts_used);
    serial_write_string(serial, "\n");
}

void write_ap_startup_summary(Serial_t *serial, uint32_t attempted, uint32_t delivered, uint32_t retries_consumed,
                              uint32_t sequence_failures, uint32_t acknowledgement_timeouts, uint32_t c_entry_timeouts)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: AP startup summary attempted=");
    serial_write_int(serial, (int32_t) attempted);
    serial_write_string(serial, ", delivered=");
    serial_write_int(serial, (int32_t) delivered);
    serial_write_string(serial, ", seq_attempts=");
    serial_write_int(serial, (int32_t) advanced_pic_ipi_get_startup_sequence_attempt_count());
    serial_write_string(serial, ", seq_success=");
    serial_write_int(serial, (int32_t) advanced_pic_ipi_get_startup_sequence_success_count());
    serial_write_string(serial, ", init_ipi=");
    serial_write_int(serial, (int32_t) advanced_pic_ipi_get_init_attempt_count());
    serial_write_string(serial, ", sipi=");
    serial_write_int(serial, (int32_t) advanced_pic_ipi_get_sipi_attempt_count());
    serial_write_string(serial, ", ap_reported_online=");
    serial_write_int(serial, (int32_t) application_processor_startup_get_reported_online_count());
    serial_write_string(serial, ", ap_last_id=");
    serial_write_int(serial, (int32_t) application_processor_startup_get_last_reported_apic_id());
    serial_write_string(serial, ", retries=");
    serial_write_int(serial, (int32_t) retries_consumed);
    serial_write_string(serial, ", seq_fail=");
    serial_write_int(serial, (int32_t) sequence_failures);
    serial_write_string(serial, ", ack_to=");
    serial_write_int(serial, (int32_t) acknowledgement_timeouts);
    serial_write_string(serial, ", c_entry_to=");
    serial_write_int(serial, (int32_t) c_entry_timeouts);
    serial_write_string(serial, ", tramp_installs=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_install_count());
    serial_write_string(serial, ", tramp_waits=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_ack_wait_count());
    serial_write_string(serial, ", tramp_ack_ok=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_ack_success_count());
    serial_write_string(serial, ", tramp_ack_to=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_ack_timeout_count());
    serial_write_string(serial, ", tramp_c_waits=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_c_entry_wait_count());
    serial_write_string(serial, ", tramp_c_ok=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_c_entry_success_count());
    serial_write_string(serial, ", tramp_c_to=");
    serial_write_int(serial, (int32_t) application_processor_trampoline_get_c_entry_timeout_count());
    serial_write_string(serial, "\n");
}
