#include <kernel/cpu/ap_bootstrap.h>

static ApplicationProcessorBootstrapTable_t ap_bootstrap_table = { 0 };
static bool application_processor_bootstrap_initialized = false;
static uint8_t ap_bootstrap_static_stacks[AP_BOOTSTRAP_MAX_APS][AP_BOOTSTRAP_STACK_SIZE];

uint8_t application_processor_bootstrap_initialize(void)
{
    if (application_processor_bootstrap_initialized)
        return 1u;

    uint32_t bsp_apic_id = cpu_topology_get_local_apic_id();
    uint32_t static_stack_index = 0u;

    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        uint8_t apic_id = cpu_topology_get_apic_id_at_slot(slot);

        if (apic_id == 0xFFu || apic_id == bsp_apic_id)
            continue;

        if (static_stack_index >= AP_BOOTSTRAP_MAX_APS)
            return 0u;

        ap_bootstrap_table.entries[slot].apic_id = apic_id;
        ap_bootstrap_table.entries[slot].logical_slot = slot;
        ap_bootstrap_table.entries[slot].stack_base = (void *) ap_bootstrap_static_stacks[static_stack_index];
        ap_bootstrap_table.entries[slot].stack_size = AP_BOOTSTRAP_STACK_SIZE;
        ap_bootstrap_table.entries[slot].initialized = 1u;
        ap_bootstrap_table.entries[slot].booted = 0u;

        ++static_stack_index;
        ++ap_bootstrap_table.ap_count;
    }

    application_processor_bootstrap_initialized = true;
    return 1u;
}

ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_get_entry_by_apic_id(uint8_t apic_id)
{
    for (uint32_t slot = 0u; slot < 32u; ++slot)
    {
        if (ap_bootstrap_table.entries[slot].initialized &&
            ap_bootstrap_table.entries[slot].apic_id == apic_id)
        {
            return &ap_bootstrap_table.entries[slot];
        }
    }
    return NULL;
}

ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_get_entry_by_slot(uint32_t logical_slot)
{
    if (logical_slot >= 32u || !ap_bootstrap_table.entries[logical_slot].initialized)
        return NULL;

    return &ap_bootstrap_table.entries[logical_slot];
}

uint32_t application_processor_bootstrap_get_ap_count(void)
{
    return ap_bootstrap_table.ap_count;
}

void *application_processor_bootstrap_get_ap_stack_top(uint32_t logical_slot)
{
    ApplicationProcessorBootstrapEntry_t *entry = application_processor_bootstrap_get_entry_by_slot(logical_slot);
    if (!entry)
        return NULL;

    return (void *) ((uintptr_t) entry->stack_base + entry->stack_size);
}

uint32_t application_processor_bootstrap_get_ap_stack_phys(uint32_t logical_slot)
{
    void *stack_virt = application_processor_bootstrap_get_ap_stack_top(logical_slot);
    if (!stack_virt)
        return 0u;

    return (uint32_t) (uintptr_t) stack_virt - KERNEL_VIRTUAL_BASE;
}

void application_processor_bootstrap_mark_booted(uint32_t logical_slot)
{
    ApplicationProcessorBootstrapEntry_t *entry = application_processor_bootstrap_get_entry_by_slot(logical_slot);
    if (entry)
        entry->booted = 1u;
}

uint8_t application_processor_bootstrap_is_booted(uint32_t logical_slot)
{
    ApplicationProcessorBootstrapEntry_t *entry = application_processor_bootstrap_get_entry_by_slot(logical_slot);
    return entry ? entry->booted : 0u;
}

void application_processor_bootstrap_reset_iteration(void)
{
    ap_bootstrap_table.next_entry_index = 0u;
}

ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_next_unbooted_ap(void)
{
    while (ap_bootstrap_table.next_entry_index < 32u)
    {
        uint32_t idx = ap_bootstrap_table.next_entry_index++;

        ApplicationProcessorBootstrapEntry_t *entry = &ap_bootstrap_table.entries[idx];
        if (entry->initialized && !entry->booted)
            return entry;
    }

    return NULL;
}
