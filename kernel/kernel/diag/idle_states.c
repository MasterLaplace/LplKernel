#include <kernel/diag/idle_states.h>

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/diag/telemetry.h>
#include <kernel/power/processor_sleep.h>

#include <stddef.h>

#define IDLE_STATES_HINTS ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_HINTS /**< The hints looked up. */

typedef AdvancedConfigurationAndPowerInterfaceProcessorIdle_t IdleStatesSlot_t;
typedef AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t IdleStatesTable_t;
typedef AdvancedConfigurationAndPowerInterfaceIdleCosts_t IdleStatesCosts_t;

static const IdleStatesSlot_t *idle_states_of_this_processor(const IdleStatesTable_t *table)
{
    if (!apic_is_running())
        return NULL;
    return advanced_configuration_and_power_interface_processor_idle_of_processor(
        table, advanced_configuration_and_power_interface_madt(), apic_get_id());
}

static uint32_t idle_states_published_hints(const IdleStatesSlot_t *idle)
{
    uint32_t published = 0u;
    IdleStatesCosts_t costs;

    for (uint32_t hint = 0u; idle != NULL && hint < IDLE_STATES_HINTS; ++hint)
        if (advanced_configuration_and_power_interface_processor_idle_costs(idle, hint, &costs))
            published |= 1u << hint;
    return published;
}

static void idle_states_write_hint(Serial_t *serial, uint32_t hint, const IdleStatesCosts_t *costs)
{
    kernel_telemetry_begin_record(serial, "cstate");
    kernel_telemetry_write_unsigned("hint", hint);
    kernel_telemetry_write_unsigned("latency_us", costs->latency);
    kernel_telemetry_write_unsigned("residency_us", costs->residency);
    kernel_telemetry_write_text("residency_source", "rule");
    kernel_telemetry_end_record();
}

void kernel_idle_states_report(Serial_t *serial)
{
    const IdleStatesTable_t *table = advanced_configuration_and_power_interface_processor_idle();
    const IdleStatesSlot_t *idle = idle_states_of_this_processor(table);
    const uint32_t enumerated = kernel_processor_sleep_hints_enumerated_by_this_processor();
    IdleStatesCosts_t costs;

    kernel_telemetry_begin_record(serial, "cstates");
    if (idle != NULL)
        kernel_telemetry_write_unsigned("slot", (uint32_t) (idle - table->slots));
    else
        kernel_telemetry_write_text("slot", "none");
    kernel_telemetry_write_boolean("monitor", kernel_processor_sleep_monitor_implemented());
    kernel_telemetry_write_hexadecimal("enumerated", enumerated);
    kernel_telemetry_write_hexadecimal(
        "usable", kernel_processor_sleep_usable_hints(enumerated, advanced_pic_timer_backend_always_runs() != 0u));
    kernel_telemetry_write_hexadecimal("published", idle_states_published_hints(idle));
    kernel_telemetry_end_record();
    for (uint32_t hint = 0u; idle != NULL && hint < IDLE_STATES_HINTS; ++hint)
        if (advanced_configuration_and_power_interface_processor_idle_costs(idle, hint, &costs))
            idle_states_write_hint(serial, hint, &costs);
}
