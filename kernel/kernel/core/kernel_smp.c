#include <kernel/core/kernel_smp.h>
#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/helpers/ap_startup_helper.h>

#define KERNEL_AP_TRAMPOLINE_ACK_SPIN_LIMIT     200000u
#define KERNEL_AP_TRAMPOLINE_C_ENTRY_SPIN_LIMIT 300000u
#define KERNEL_AP_STARTUP_MAX_ATTEMPTS          3u

void kernel_smp_try_start_discovered_aps(Serial_t *com1)
{
    uint8_t ap_bootstrap_ok = application_processor_bootstrap_initialize();

    write_ap_bootstrap_init_info(com1, ap_bootstrap_ok);

    application_processor_startup_set_serial_port(com1);

    if (!ap_bootstrap_ok)
        return;

    if (!advanced_pic_ipi_is_ready())
    {
        write_ap_startup_skipped_ipi_not_ready_info(com1);
        return;
    }

    if (!application_processor_startup_ensure_low_identity_mapping())
    {
        write_ap_startup_skipped_identity_map_unavailable_info(com1);
        return;
    }

    if (!application_processor_trampoline_install())
    {
        write_ap_startup_skipped_trampoline_install_failed_info(com1);
        return;
    }

    write_ap_trampoline_installed_info(com1);

    application_processor_bootstrap_reset_iteration();

    uint32_t attempted = 0u;
    uint32_t delivered = 0u;
    uint32_t retries_consumed = 0u;
    uint32_t sequence_failures = 0u;
    uint32_t acknowledgement_timeouts = 0u;
    uint32_t c_entry_timeouts = 0u;
    ApplicationProcessorBootstrapEntry_t *entry = NULL;

    while ((entry = application_processor_bootstrap_next_unbooted_ap()) != NULL)
    {
        ++attempted;

        uint8_t sequence_ok = 0u;
        uint8_t ack_ok = 0u;
        uint8_t c_entry_ok = 0u;
        uint8_t attempts_used = 0u;

        for (uint32_t attempt = 0u; attempt < KERNEL_AP_STARTUP_MAX_ATTEMPTS; ++attempt)
        {
            void *ap_stack_top = application_processor_bootstrap_get_ap_stack_top(entry->logical_slot);

            if (!ap_stack_top)
            {
                ++sequence_failures;
                continue;
            }

            attempts_used = (uint8_t) (attempt + 1u);
            application_processor_trampoline_reset_acknowledgement();
            application_processor_trampoline_configure_handoff(
                entry->apic_id, entry->logical_slot, (uint32_t) (uintptr_t) ap_stack_top,
                (uint32_t) (uintptr_t) application_processor_startup_initialize_cpu,
                (uint32_t) (uintptr_t) application_processor_startup_main_loop,
                application_processor_startup_get_kernel_cr3());
            sequence_ok = advanced_pic_ipi_send_startup_sequence(entry->apic_id,
                                                                 application_processor_trampoline_get_startup_vector());
            if (!sequence_ok)
            {
                ++sequence_failures;
                continue;
            }

            ack_ok = application_processor_trampoline_wait_for_acknowledgement(KERNEL_AP_TRAMPOLINE_ACK_SPIN_LIMIT);
            if (!ack_ok)
            {
                ++acknowledgement_timeouts;
                continue;
            }

            c_entry_ok = application_processor_trampoline_wait_for_c_entry(KERNEL_AP_TRAMPOLINE_C_ENTRY_SPIN_LIMIT);
            if (!c_entry_ok)
            {
                ++c_entry_timeouts;
                continue;
            }

            break;
        }

        if (attempts_used > 1u)
            retries_consumed += (uint32_t) (attempts_used - 1u);

        if (sequence_ok && ack_ok && c_entry_ok)
        {
            ++delivered;
        }

        write_ap_startup_dispatch_info(com1, entry, sequence_ok, ack_ok, c_entry_ok, attempts_used);
    }

    write_ap_startup_summary(com1, attempted, delivered, retries_consumed, sequence_failures, acknowledgement_timeouts,
                             c_entry_timeouts);
}
