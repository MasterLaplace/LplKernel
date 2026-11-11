#include <kernel/diag/idle_states.h>

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/diag/telemetry.h>
#include <kernel/power/processor_sleep.h>

#include <stddef.h>

#define IDLE_STATES_HINT_BITS 32u /**< The hints a mask holds. */

typedef AdvancedConfigurationAndPowerInterfaceProcessorIdle_t IdleStatesSlot_t;
typedef AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t IdleStatesTable_t;

static const IdleStatesSlot_t *idle_states_of_this_processor(const IdleStatesTable_t *table)
{
    if (!apic_is_running())
        return NULL;
    return advanced_configuration_and_power_interface_processor_idle_of_processor(
        table, advanced_configuration_and_power_interface_madt(), apic_get_id());
}

static void idle_states_write_hint(Serial_t *serial, const IdleStatesSlot_t *idle, uint32_t hint)
{
    uint32_t latency = 0u;
    uint32_t residency = 0u;

    kernel_telemetry_begin_record(serial, "cstate");
    kernel_telemetry_write_unsigned("hint", hint);
    if (!advanced_configuration_and_power_interface_processor_idle_latency(idle, hint, &latency) ||
        !advanced_configuration_and_power_interface_processor_idle_residency(idle, hint, &residency))
    {
        kernel_telemetry_write_boolean("unread", true);
        kernel_telemetry_end_record();
        return;
    }
    kernel_telemetry_write_unsigned("latency_us", latency);
    kernel_telemetry_write_unsigned("residency_us", residency);
    kernel_telemetry_write_text("residency_source", "rule");
    kernel_telemetry_end_record();
}

void kernel_idle_states_report(Serial_t *serial)
{
    const IdleStatesTable_t *table = advanced_configuration_and_power_interface_processor_idle();
    const IdleStatesSlot_t *idle = idle_states_of_this_processor(table);
    const uint32_t enumerated = kernel_processor_sleep_hints_enumerated_by_this_processor();
    const uint32_t published =
        idle != NULL ? advanced_configuration_and_power_interface_processor_idle_hints(idle) : 0u;

    kernel_telemetry_begin_record(serial, "cstates");
    if (idle != NULL)
        kernel_telemetry_write_unsigned("slot", (uint32_t) (idle - table->slots));
    else
        kernel_telemetry_write_text("slot", "none");
    kernel_telemetry_write_hexadecimal("enumerated", enumerated);
    kernel_telemetry_write_hexadecimal(
        "usable", kernel_processor_sleep_usable_hints(enumerated, advanced_pic_timer_backend_always_runs() != 0u));
    kernel_telemetry_write_hexadecimal("published", published);
    kernel_telemetry_end_record();
    for (uint32_t hint = 0u; hint < IDLE_STATES_HINT_BITS; ++hint)
        if (((published >> hint) & 1u) != 0u)
            idle_states_write_hint(serial, idle, hint);
}
