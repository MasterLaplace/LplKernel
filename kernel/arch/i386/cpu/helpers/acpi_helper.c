#include <kernel/acpi/initialization.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/config.h>
#include <kernel/cpu/helpers/acpi_helper.h>
#include <kernel/diag/telemetry.h>

#include <string.h>

#define ACPI_HELPER_PATH_BYTES 64u /**< Bytes of the path the acpi.init record names: twelve levels deep. */

/**
 * @brief Writes the `[LPLTLM] acpi.regions` record: the field accesses of the boot's load, and what
 *        reaching their regions took.
 */
static void
acpi_helper_write_regions_record(Serial_t *serial,
                                 const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t *regions)
{
    uint32_t refused = 0u;

    for (uint32_t space = 0u; space < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES; ++space)
        refused += regions->space_unsupported[space];
    kernel_telemetry_begin_record(serial, "acpi.regions");
    kernel_telemetry_write_unsigned("field_reads", regions->field_reads);
    kernel_telemetry_write_unsigned("field_writes", regions->field_writes);
    kernel_telemetry_write_unsigned("map_calls", regions->map_calls);
    kernel_telemetry_write_unsigned("mapping_cache_hits", regions->mapping_cache_hits);
    kernel_telemetry_write_unsigned("split_qword_accesses", regions->split_qword_accesses);
    kernel_telemetry_write_unsigned("unaligned_accesses", regions->unaligned_accesses);
    kernel_telemetry_write_unsigned("protected_port_accesses", regions->protected_port_accesses);
    kernel_telemetry_write_unsigned("region_lengths_rounded", regions->region_lengths_rounded);
    kernel_telemetry_write_unsigned("field_accesses_peak", regions->field_accesses_peak);
    kernel_telemetry_write_unsigned("space_unsupported", refused);
    kernel_telemetry_write_unsigned("global_lock_acquisitions", regions->global_lock_acquisitions);
    kernel_telemetry_write_unsigned("global_lock_waits", regions->global_lock_waits);
    kernel_telemetry_write_unsigned("global_lock_timeouts", regions->global_lock_timeouts);
    kernel_telemetry_write_unsigned("global_lock_releases_signalled", regions->global_lock_releases_signalled);
    kernel_telemetry_write_unsigned("unlocked_lock_accesses", regions->unlocked_lock_accesses);
    kernel_telemetry_write_unsigned("facs_refused", regions->facs_refused ? 1u : 0u);
    kernel_telemetry_write_unsigned("pci_attachments", regions->pci_attachments);
    kernel_telemetry_write_unsigned("internal_evaluations", regions->internal_evaluations);
    kernel_telemetry_write_unsigned("internal_failures", regions->internal_failures);
    kernel_telemetry_write_unsigned("pci_protected_writes", regions->pci_protected_writes);
    kernel_telemetry_write_unsigned("pci_lost_accesses", regions->pci_lost_accesses);
    kernel_telemetry_write_unsigned("pci_ecam_accesses", regions->pci_ecam_accesses);
    kernel_telemetry_write_unsigned("pci_registrations", regions->pci_registrations);
    kernel_telemetry_write_unsigned("pci_registration_failures", regions->pci_registration_failures);
    kernel_telemetry_end_record();
}

/**
 * @brief Writes the `[LPLTLM] acpi.init` record: what the initialisation of the boot's namespace ran
 *        and counted (§3.8 steps 2 to 9), the fold of the C-states it published, and the first evaluation
 *        that failed, by its path and status.
 */
static void
acpi_helper_write_initialization_record(Serial_t *serial,
                                        const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                        const AdvancedConfigurationAndPowerInterfaceInitialization_t *initialization)
{
    char path[ACPI_HELPER_PATH_BYTES] = "none";

    if (initialization->first_failure_node != 0u &&
        advanced_configuration_and_power_interface_machine_language_namespace_path(
            &load->space, initialization->first_failure_node, path, sizeof(path)) == 0u)
        memcpy(path, "too_deep", sizeof("too_deep"));
    kernel_telemetry_begin_record(serial, "acpi.init");
    kernel_telemetry_write_unsigned("initializations", initialization->initializations);
    kernel_telemetry_write_unsigned("initialization_failed", initialization->initialization_failed);
    kernel_telemetry_write_unsigned("registrations", initialization->registrations);
    kernel_telemetry_write_unsigned("region_registration_failed", initialization->region_registration_failed);
    kernel_telemetry_write_unsigned("devices", initialization->devices);
    kernel_telemetry_write_unsigned("status_evaluations", initialization->status_evaluations);
    kernel_telemetry_write_unsigned("status_failed", initialization->status_failed);
    kernel_telemetry_write_unsigned("status_invalid", initialization->status_invalid);
    kernel_telemetry_write_unsigned("pic_mode_deferred", initialization->pic_mode_deferred);
    kernel_telemetry_write_text(
        "platform_capabilities",
        advanced_configuration_and_power_interface_platform_capabilities_name(initialization->platform_capabilities));
    kernel_telemetry_write_unsigned("processor_capabilities", initialization->processor_capabilities);
    kernel_telemetry_write_unsigned("processor_capabilities_legacy", initialization->processor_capabilities_legacy);
    kernel_telemetry_write_unsigned("processor_capabilities_refused", initialization->processor_capabilities_refused);
    kernel_telemetry_write_unsigned("processor_capabilities_failed", initialization->processor_capabilities_failed);
    kernel_telemetry_write_unsigned("processors_unbound", initialization->processors_unbound);
    kernel_telemetry_write_text(
        "idle_control", advanced_configuration_and_power_interface_idle_control_name(initialization->idle_control));
    kernel_telemetry_write_unsigned("processor_idle", initialization->processor_idle);
    kernel_telemetry_write_unsigned("processor_idle_failed", initialization->processor_idle_failed);
    kernel_telemetry_write_unsigned("processor_idle_unbound", initialization->processor_idle_unbound);
    kernel_telemetry_write_unsigned("idle_states_dropped", initialization->idle_states_dropped);
    kernel_telemetry_write_unsigned("processor_idle_changes", initialization->processor_idle_changes);
    kernel_telemetry_write_hexadecimal("idle_fold", advanced_configuration_and_power_interface_processor_idle_fold(
                                                        advanced_configuration_and_power_interface_processor_idle()));
    kernel_telemetry_write_text("first_failure", path);
    kernel_telemetry_write_text(
        "first_failure_status",
        advanced_configuration_and_power_interface_machine_language_status_name(initialization->first_failure));
    kernel_telemetry_end_record();
}

#define ACPI_HELPER_ROW_BYTES 16u /**< Bytes of a table on one printed line, as acpidump prints them. */

/**
 * @brief Writes @p value as @p digits upper-case hexadecimal digits, from @p text on.
 *
 * @return Where the next character goes.
 */
static char *acpi_helper_put_hexadecimal(char *text, uint32_t value, uint32_t digits)
{
    static const char alphabet[] = "0123456789ABCDEF";

    for (uint32_t digit = digits; digit != 0u; --digit)
        *text++ = alphabet[(value >> (4u * (digit - 1u))) & 0xFu];
    return text;
}

/**
 * @brief How many hexadecimal digits acpidump prints @p offset in: four, or as many as it needs.
 */
static uint32_t acpi_helper_offset_digits(uint32_t offset)
{
    uint32_t digits = 4u;

    while (digits < 8u && (offset >> (4u * digits)) != 0u)
        ++digits;
    return digits;
}

/**
 * @brief Writes one table: its header line, its bytes ACPI_HELPER_ROW_BYTES per line, each led by its
 *        offset as acpidump prints it, then an empty line.
 */
static void acpi_helper_write_table(Serial_t *serial, const AdvancedConfigurationAndPowerInterfaceTable_t *table)
{
    const uint8_t *bytes = (const uint8_t *) table->header;
    char row[4u + 8u + 1u + 3u * ACPI_HELPER_ROW_BYTES + 2u];

    for (uint32_t letter = 0u; letter < sizeof(table->signature); ++letter)
        serial_write_char(serial, table->signature[letter]);
    serial_write_string(serial, " @ ");
    serial_write_hex64(serial, table->physical_address);
    serial_write_char(serial, '\n');
    for (uint32_t offset = 0u; offset < table->length; offset += ACPI_HELPER_ROW_BYTES)
    {
        char *end = row;

        for (uint32_t space = 0u; space < 4u; ++space)
            *end++ = ' ';
        end = acpi_helper_put_hexadecimal(end, offset, acpi_helper_offset_digits(offset));
        *end++ = ':';
        for (uint32_t byte = offset; byte < offset + ACPI_HELPER_ROW_BYTES && byte < table->length; ++byte)
        {
            *end++ = ' ';
            end = acpi_helper_put_hexadecimal(end, bytes[byte], 2u);
        }
        *end++ = '\n';
        *end = '\0';
        serial_write_string(serial, row);
    }
    serial_write_char(serial, '\n');
}

/**
 * @brief Writes @p value in decimal from @p text on.
 *
 * @return Where the next character goes.
 */
static char *acpi_helper_put_decimal(char *text, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;

    do
    {
        digits[count++] = (char) ('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u)
        *text++ = digits[--count];
    return text;
}

/**
 * @brief Writes one published C-state as `type:latency:power:space:width:offset:access:address`, the
 *        space and the address in hexadecimal, from @p text on, and ends the text.
 */
static void acpi_helper_put_idle_state(char *text, const AdvancedConfigurationAndPowerInterfaceIdleState_t *state)
{
    text = acpi_helper_put_decimal(text, state->type);
    *text++ = ':';
    text = acpi_helper_put_decimal(text, state->latency);
    *text++ = ':';
    text = acpi_helper_put_decimal(text, state->power);
    *text++ = ':';
    text = acpi_helper_put_hexadecimal(text, state->entry.space, 2u);
    *text++ = ':';
    text = acpi_helper_put_decimal(text, state->entry.bit_width);
    *text++ = ':';
    text = acpi_helper_put_decimal(text, state->entry.bit_offset);
    *text++ = ':';
    text = acpi_helper_put_decimal(text, state->entry.access_size);
    *text++ = ':';
    text = acpi_helper_put_hexadecimal(text, (uint32_t) (state->entry.address >> 32u), 8u);
    text = acpi_helper_put_hexadecimal(text, (uint32_t) state->entry.address, 8u);
    *text = '\0';
}

/**
 * @brief A count of 64 bits as the record writes it: saturated to 32 bits.
 */
static uint32_t acpi_helper_saturate(uint64_t count) { return (count > UINT32_MAX) ? UINT32_MAX : (uint32_t) count; }

/**
 * @brief Writes the `[LPLTLM] acpi.services` record: the Windows identity AML asked for, the time it
 *        requested and the time the host waited, its sleeps and its busy-waits apart (§3.3, §5.5), its
 *        mutexes and its Notifies.
 */
static void
acpi_helper_write_services_record(Serial_t *serial,
                                  const AdvancedConfigurationAndPowerInterfaceMachineLanguageServices_t *services)
{
    kernel_telemetry_begin_record(serial, "acpi.services");
    kernel_telemetry_write_unsigned("windows", services->windows_version);
    kernel_telemetry_write_unsigned("osi_queries", services->operating_system_queries);
    kernel_telemetry_write_unsigned("osi_denied", services->operating_system_queries_denied);
    kernel_telemetry_write_unsigned("sleeps", services->sleeps);
    kernel_telemetry_write_unsigned("sleep_ms", acpi_helper_saturate(services->sleep_milliseconds));
    kernel_telemetry_write_unsigned("stalls", services->stalls);
    kernel_telemetry_write_unsigned("stall_us", acpi_helper_saturate(services->stall_microseconds));
    kernel_telemetry_write_unsigned("long_stalls", services->long_stalls);
    kernel_telemetry_write_unsigned("stalls_slept", services->stalls_slept);
    kernel_telemetry_write_unsigned("waited_ms", acpi_helper_saturate(services->waited_milliseconds));
    kernel_telemetry_write_unsigned("stall_waited_us", acpi_helper_saturate(services->stall_waited_microseconds));
    kernel_telemetry_write_unsigned("time_requested_peak_us", acpi_helper_saturate(services->time_requested_peak));
    kernel_telemetry_write_unsigned("timer_reads", services->timer_reads);
    kernel_telemetry_write_unsigned("acquisitions", services->acquisitions);
    kernel_telemetry_write_unsigned("acquisitions_ignored", services->acquisitions_ignored);
    kernel_telemetry_write_unsigned("releases_ignored", services->releases_ignored);
    kernel_telemetry_write_unsigned("released_at_end", services->released_at_end);
    kernel_telemetry_write_unsigned("held_peak", services->held_peak);
    kernel_telemetry_write_unsigned("signals", services->signals);
    kernel_telemetry_write_unsigned("notifications", services->notifications);
    kernel_telemetry_write_unsigned("notifications_dropped", services->notifications_dropped);
    kernel_telemetry_write_unsigned("notifications_taken", services->notifications_taken);
    kernel_telemetry_write_unsigned("notification_peak", services->notification_peak);
    kernel_telemetry_end_record();
}

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

void write_acpi_mode_record(Serial_t *serial)
{
    const AdvancedConfigurationAndPowerInterfaceModeSwitch_t *mode = advanced_configuration_and_power_interface_mode();
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt =
        &advanced_configuration_and_power_interface_directory()->fixed_description;

    kernel_telemetry_begin_record(serial, "acpi.mode");
    kernel_telemetry_write_text("outcome", advanced_configuration_and_power_interface_mode_outcome_name(mode->outcome));
    kernel_telemetry_write_unsigned("enables_cleared", mode->enable_registers_cleared);
    kernel_telemetry_write_unsigned("polls", mode->polls);
    kernel_telemetry_write_unsigned("waited_ms", mode->waited_milliseconds);
    kernel_telemetry_write_unsigned("fadt_revision", fadt->revision);
    kernel_telemetry_write_unsigned("sci_int", fadt->system_control_interrupt);
    kernel_telemetry_write_hexadecimal("smi_command", fadt->system_management_interrupt_command);
    kernel_telemetry_write_hexadecimal("enable_command", fadt->enable_command);
    kernel_telemetry_end_record();
}

void write_acpi_machine_language_record(Serial_t *serial)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load =
        advanced_configuration_and_power_interface_machine_language();
    uint32_t terms = 0u;

    for (uint32_t index = 0u; index < load->table_count; ++index)
        terms += load->tables[index].terms;
    kernel_telemetry_begin_record(serial, "acpi.aml");
    kernel_telemetry_write_text("status",
                                advanced_configuration_and_power_interface_machine_language_status_name(load->status));
    kernel_telemetry_write_unsigned("tables", load->table_count);
    kernel_telemetry_write_unsigned("bytes", load->bytes);
    kernel_telemetry_write_unsigned("integer_bits", load->integer_bits);
    kernel_telemetry_write_unsigned("terms", terms);
    kernel_telemetry_write_unsigned("declarations", load->declarations);
    kernel_telemetry_write_unsigned("duplicates", load->duplicates);
    kernel_telemetry_write_unsigned("method_calls", load->method_calls);
    kernel_telemetry_write_unsigned("name_references", load->name_references);
    kernel_telemetry_write_unsigned("unresolved_names", load->unresolved_names);
    kernel_telemetry_write_unsigned("load_errors", load->load_errors);
    kernel_telemetry_write_unsigned("decode_errors", load->decode_errors);
    kernel_telemetry_write_unsigned("checksum_bad_loaded", load->checksum_bad_loaded);
    kernel_telemetry_write_unsigned("nodes", load->space.live_nodes);
    kernel_telemetry_write_unsigned("arena_bytes", load->space.arena_bytes);
    kernel_telemetry_write_unsigned("lookups", load->space.lookups);
    kernel_telemetry_write_unsigned("probes", load->space.probes);
    kernel_telemetry_write_unsigned("pending_peak", load->pending_peak);
    kernel_telemetry_write_unsigned("blocks_peak", load->blocks_peak);
    kernel_telemetry_write_unsigned("objects", load->heap.objects);
    kernel_telemetry_write_unsigned("values_deferred", load->values_deferred);
    kernel_telemetry_write_unsigned("table_level_code", load->table_level_code);
    kernel_telemetry_write_unsigned("table_level_runs", load->evaluation.runs);
    kernel_telemetry_write_unsigned("table_level_failures", load->evaluation.failures);
    kernel_telemetry_write_unsigned("undeclared_faults", load->undeclared_faults);
    kernel_telemetry_write_unsigned("declared_not_run", load->declared_not_run);
    kernel_telemetry_write_unsigned("frames_peak", load->evaluation.frames_peak);
    kernel_telemetry_write_unsigned("values_peak", load->evaluation.values_peak);
    kernel_telemetry_write_hexadecimal(
        "namespace_fold", advanced_configuration_and_power_interface_machine_language_namespace_fold(load));
    kernel_telemetry_end_record();
    acpi_helper_write_initialization_record(serial, load, advanced_configuration_and_power_interface_initialization());
    acpi_helper_write_regions_record(serial, &load->regions);
    acpi_helper_write_services_record(serial, &load->services);
    kernel_telemetry_begin_record(serial, "acpi.values");
    kernel_telemetry_write_unsigned("objects", load->heap.objects);
    kernel_telemetry_write_unsigned("objects_freed", load->heap.objects_freed);
    kernel_telemetry_write_unsigned("stale_handles", load->heap.stale_handles);
    kernel_telemetry_write_unsigned("object_bytes_peak", load->heap.object_bytes_peak);
    kernel_telemetry_write_unsigned("mixed_type_comparisons", load->evaluation.mixed_type_comparisons);
    kernel_telemetry_write_unsigned("forbidden_conversions", load->evaluation.forbidden_conversions);
    kernel_telemetry_end_record();
}

void write_advanced_configuration_and_power_interface_tables(Serial_t *serial)
{
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory =
        advanced_configuration_and_power_interface_directory();

    serial_write_string(serial, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_BEGIN "\n");
    for (uint32_t index = 0u; index < directory->table_count; ++index)
        acpi_helper_write_table(serial, &directory->tables[index]);
    serial_write_string(serial, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PRINTED_TABLES_END "\n");
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

void write_acpi_processor_idle_record(Serial_t *serial)
{
    static const char *const keys[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_MAX_STATES] = {
        "state1", "state2", "state3", "state4", "state5", "state6", "state7", "state8"};
    const AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t *table =
        advanced_configuration_and_power_interface_processor_idle();
    const AdvancedConfigurationAndPowerInterfaceMadtState_t *madt = advanced_configuration_and_power_interface_madt();
    char text[72];

    for (uint32_t slot = 0u; slot < table->slot_count; ++slot)
    {
        const AdvancedConfigurationAndPowerInterfaceProcessorIdle_t *idle = &table->slots[slot];

        kernel_telemetry_begin_record(serial, "acpi.cst");
        kernel_telemetry_write_unsigned("slot", slot);
        kernel_telemetry_write_unsigned("apic_id", madt->processors[slot].apic_id);
        kernel_telemetry_write_unsigned("uid", madt->processors[slot].processor_unique_identifier);
        kernel_telemetry_write_boolean("evaluated", idle->processor != 0u);
        kernel_telemetry_write_unsigned("states", idle->count);
        for (uint32_t state = 0u; state < idle->count; ++state)
        {
            acpi_helper_put_idle_state(text, &idle->states[state]);
            kernel_telemetry_write_text(keys[state], text);
        }
        kernel_telemetry_end_record();
    }
}
