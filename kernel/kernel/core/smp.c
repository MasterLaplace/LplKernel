#include <kernel/core/smp.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/cpu/ap_startup.h>
#include <kernel/cpu/ap_trampoline.h>
#include <kernel/cpu/apic_ipi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/helpers/ap_startup_helper.h>
#include <kernel/diag/telemetry.h>
#include <kernel/memory/vmm.h>

/** Polls of the acknowledgement word before an attempt is given up. The AP writes it in real
    mode, first thing: measured at zero polls under QEMU, it is already there when the IPIs return. */
#define KERNEL_AP_TRAMPOLINE_ACK_SPIN_LIMIT 200000u

/**
 * Polls of the C-entry word before an attempt is given up.
 *
 * The AP reaches C after protected mode, paging and its stack, and under QEMU's TCG the first AP
 * to do it was measured at 722,496 polls, a warm one at 6,750: the former limit of 300,000 failed
 * the first AP of every multi-CPU boot, which then came up on a retry, or after the BSP had moved
 * on. A poll count is not a time bound, so the margin is wide: two orders of magnitude over the
 * slowest measured, and only spent in full by an AP that never arrives.
 */
#define KERNEL_AP_TRAMPOLINE_C_ENTRY_SPIN_LIMIT 100000000u

/** Start-up sequences sent to one AP before it is given up and parked. */
#define KERNEL_AP_STARTUP_MAX_ATTEMPTS 3u

/**
 * @brief Puts an AP that never confirmed its start-up back into wait-for-SIPI.
 *
 * @details An AP that misses the deadline is not necessarily dead: it may still be on its way,
 *          and the trampoline's mailbox is about to be rewritten for the next AP. Left running,
 *          it would read that AP's stack and slot. An INIT stops it where it is, and its online
 *          mark, if it set one late, is withdrawn so the topology does not count a stopped CPU.
 *
 * @param apic_id The AP to park.
 * @return 1 when the INIT was delivered.
 */
static uint8_t kernel_symmetric_multiprocessing_park(uint8_t apic_id)
{
    const uint8_t parked = advanced_pic_ipi_send_init(apic_id);
    cpu_topology_unmark_apic_id_online(apic_id);
    return parked;
}

static uint32_t smp_ap_attempted = 0u;
static uint32_t smp_ap_delivered = 0u;
static uint32_t smp_ap_parked = 0u;

void kernel_symmetric_multiprocessing_try_start_discovered_aps(Serial_t *com1)
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
    uint32_t parked = 0u;
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
            ++delivered;
        else
            parked += kernel_symmetric_multiprocessing_park(entry->apic_id);

        write_ap_startup_dispatch_info(com1, entry, sequence_ok, ack_ok, c_entry_ok, attempts_used);
    }

    write_ap_startup_summary(com1, attempted, delivered, retries_consumed, sequence_failures, acknowledgement_timeouts,
                             c_entry_timeouts, parked);

    smp_ap_attempted = attempted;
    smp_ap_delivered = delivered;
    smp_ap_parked = parked;
}

void kernel_symmetric_multiprocessing_report(Serial_t *serial)
{
    const bool madt = advanced_configuration_and_power_interface_madt_is_available() != 0u;
    const uint32_t madt_cpus =
        madt ? advanced_configuration_and_power_interface_madt_get_enabled_local_apic_count() : 1u;
    const uint32_t online = cpu_topology_get_online_cpu_count();
    const uint32_t timeouts = advanced_pic_ipi_get_tlb_shootdown_timeout_count();
    const bool pass = (online == madt_cpus) && (smp_ap_delivered == smp_ap_attempted) && (timeouts == 0u);

    kernel_telemetry_begin_record(serial, "smp");
    kernel_telemetry_write_boolean("madt", madt);
    kernel_telemetry_write_unsigned("cpus", madt_cpus);
    kernel_telemetry_write_unsigned("discovered", cpu_topology_get_discovered_cpu_count());
    kernel_telemetry_write_unsigned("online", online);
    kernel_telemetry_write_unsigned("ap_attempted", smp_ap_attempted);
    kernel_telemetry_write_unsigned("ap_delivered", smp_ap_delivered);
    kernel_telemetry_write_unsigned("ap_parked", smp_ap_parked);
    kernel_telemetry_write_unsigned("shootdowns", advanced_pic_ipi_get_tlb_shootdown_broadcast_count());
    kernel_telemetry_write_unsigned("ranges_unmapped", kernel_vmm_get_unmapped_range_count());
    kernel_telemetry_write_unsigned("shootdown_timeouts", timeouts);
    kernel_telemetry_write_text("result", pass ? "(pass)" : "(fail)");
    kernel_telemetry_end_record();
}
