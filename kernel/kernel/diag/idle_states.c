#include <kernel/diag/idle_states.h>

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/cpu/apic.h>
#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/diag/telemetry.h>
#include <kernel/power/processor_park.h>
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

static void idle_states_write_depth(Serial_t *serial, uint32_t depth, const ProcessorSleepDepth_t *entered)
{
    kernel_telemetry_begin_record(serial, "sleep.depth");
    kernel_telemetry_write_unsigned("depth", depth);
    kernel_telemetry_write_text("entry", kernel_processor_sleep_entry_name(entered->entry));
    kernel_telemetry_write_hexadecimal(
        "target", entered->entry == PROCESSOR_SLEEP_ENTRY_PORT ? (uint32_t) entered->port : entered->hint);
    if (entered->latency_published)
        kernel_telemetry_write_unsigned("latency_us", entered->latency);
    else
        kernel_telemetry_write_text("latency_us", "none");
    kernel_telemetry_write_unsigned("bus_master_check", entered->bus_master_check ? 1u : 0u);
    kernel_telemetry_end_record();
}

static void idle_states_write_ladder(Serial_t *serial)
{
    ProcessorSleepLadderFacts_t facts;
    ProcessorSleepLadder_t ladder;

    kernel_processor_sleep_read_ladder_facts(&facts);
    kernel_processor_sleep_build_ladder(&facts, &ladder);
    kernel_telemetry_begin_record(serial, "sleep.depths");
    kernel_telemetry_write_hexadecimal("available", ladder.available);
    kernel_telemetry_write_text("ports", kernel_processor_sleep_ports_name(ladder.ports));
    kernel_telemetry_write_unsigned("ports_refused", ladder.ports_refused);
    kernel_telemetry_write_unsigned("type_3", facts.bus_master_check ? 1u : 0u);
    kernel_telemetry_end_record();
    for (uint32_t depth = 0u; depth < PROCESSOR_SLEEP_DEPTHS; ++depth)
        if (((ladder.available >> depth) & 1u) != 0u)
            idle_states_write_depth(serial, depth, &ladder.depths[depth]);
}

static void idle_states_write_park(Serial_t *serial, uint32_t slot, const ProcessorParkRecord_t *record)
{
    kernel_telemetry_begin_record(serial, "park");
    kernel_telemetry_write_unsigned("slot", slot);
    kernel_telemetry_write_unsigned("depth", record->depth);
    kernel_telemetry_write_text("entry", kernel_processor_sleep_entry_name(record->entry));
    kernel_telemetry_write_hexadecimal("target", record->target);
    kernel_telemetry_write_hexadecimal("inherited", record->inherited);
    kernel_telemetry_write_hexadecimal("refused", record->refused);
    kernel_telemetry_write_unsigned("turns", record->turns);
    kernel_telemetry_write_unsigned("port_reads", record->port_reads);
    kernel_telemetry_write_unsigned("quick", record->quick);
    kernel_telemetry_write_unsigned("stuck", record->stuck);
    kernel_telemetry_write_unsigned("bus_mastered", record->bus_mastered);
    kernel_telemetry_write_unsigned("demotions", record->demotions);
    kernel_telemetry_end_record();
}

static void idle_states_write_parks(Serial_t *serial)
{
    const uint32_t here = apic_is_running() ? cpu_topology_get_slot_for_apic_id(apic_get_id()) : 0u;
    ProcessorParkRecord_t record;

    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
        if (slot != here && cpu_topology_is_logical_slot_online(slot) && kernel_processor_park_record(slot, &record))
            idle_states_write_park(serial, slot, &record);
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
    idle_states_write_ladder(serial);
    idle_states_write_parks(serial);
}
