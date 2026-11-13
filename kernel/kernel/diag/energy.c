#include <kernel/diag/energy.h>

#include <kernel/cpu/exception.h>
#include <kernel/diag/telemetry.h>
#include <kernel/power/energy_counter.h>

/**
 * @brief Writes @p key as @p value when @p known, as `none` otherwise.
 */
static void energy_write_known(const char *key, bool known, uint32_t value)
{
    if (known)
        kernel_telemetry_write_unsigned(key, value);
    else
        kernel_telemetry_write_text(key, "none");
}

void kernel_energy_report(Serial_t *serial)
{
    const KernelEnergyCounter_t *found = kernel_energy_counter();
    const bool units_read =
        found->state == KERNEL_ENERGY_COUNTER_PRESENT || found->state == KERNEL_ENERGY_COUNTER_NO_UNIT ||
        found->state == KERNEL_ENERGY_COUNTER_FROZEN || found->state == KERNEL_ENERGY_COUNTER_UNTIMED ||
        found->state == KERNEL_ENERGY_COUNTER_COUNTER_FAULTED;

    kernel_telemetry_begin_record(serial, "energy");
    kernel_telemetry_write_text("backend", kernel_energy_counter_backend_name(found->backend));
    kernel_telemetry_write_text("state", kernel_energy_counter_state_name(found->state));
    energy_write_known("esu", units_read, found->energy_unit_exponent);
    energy_write_known("pu", units_read, found->power_unit_exponent);
    energy_write_known("tdp_mw", found->thermal_design_power_known, found->thermal_design_power_milliwatts);
    energy_write_known("pl1_mw", found->power_limit_known, found->power_limit_milliwatts);
    energy_write_known("pl1_enabled", found->power_limit_known, found->power_limit_enabled ? 1u : 0u);
    kernel_telemetry_write_unsigned("refused_reads", interrupt_exception_refused_model_specific_register_reads());
    kernel_telemetry_end_record();
}
