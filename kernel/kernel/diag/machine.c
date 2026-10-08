#include <kernel/diag/machine.h>

#include <kernel/cpu/acpi.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/cpu_identity.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/cpu/pci.h>
#include <kernel/diag/telemetry.h>

#include <stddef.h>

/** PCI class and subclass of a high-definition audio controller. */
#define MACHINE_AUDIO_CLASS    0x04u
#define MACHINE_AUDIO_SUBCLASS 0x03u

/** Longest value the record carries, terminator included. */
#define MACHINE_VALUE_SIZE 48u

static bool machine_is_value_character(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '.' || character == '-' || character == '_';
}

static void machine_write_value(const char *key, const char *value)
{
    char clean[MACHINE_VALUE_SIZE];
    uint32_t length = 0u;

    for (; value != NULL && value[length] != '\0' && length + 1u < MACHINE_VALUE_SIZE; ++length)
        clean[length] = machine_is_value_character(value[length]) ? value[length] : '-';
    clean[length] = '\0';
    kernel_telemetry_write_text(key, length > 0u ? clean : "none");
}

static const char *machine_hypervisor_name(const CpuIdentity_t *identity)
{
    if (!identity->hypervisor)
        return "none";
    return identity->hypervisor_signature[0] != '\0' ? identity->hypervisor_signature : "unnamed";
}

static const char *machine_local_apic_mode(void)
{
    if (apic_is_x2apic_active())
        return "x2apic";
    return advanced_pic_timer_backend_is_local_apic_mmio_mapped() ? "xapic" : "absent";
}

void kernel_machine_report(Serial_t *serial)
{
    CpuIdentity_t identity;

    cpu_identity_read(&identity);
    kernel_telemetry_begin_record(serial, "machine");
    machine_write_value("hypervisor", machine_hypervisor_name(&identity));
    machine_write_value("vendor", identity.vendor);
    kernel_telemetry_write_unsigned("family", identity.family);
    kernel_telemetry_write_unsigned("model", identity.model);
    kernel_telemetry_write_unsigned("stepping", identity.stepping);
    kernel_telemetry_write_unsigned("cpus", cpu_topology_get_online_cpu_count());
    machine_write_value("acpi", advanced_configuration_and_power_interface_madt_get_state_name());
    machine_write_value("apic", machine_local_apic_mode());
    machine_write_value("ioapic", input_output_advanced_programmable_interrupt_controller_get_state_name());
    machine_write_value("topology", cpu_topology_get_source_name());
    kernel_telemetry_write_boolean("audio_controller", peripheral_component_interconnect_find_by_class(
                                                           MACHINE_AUDIO_CLASS, MACHINE_AUDIO_SUBCLASS) != NULL);
    kernel_telemetry_end_record();
}
