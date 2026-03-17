/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** AP (Application Processor) bootstrap and stack management
*/

#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/paging.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Stack size for each AP: 8 KB */
#define AP_BOOTSTRAP_STACK_SIZE 8192u

/* Maximum APs we can bootstrap (limit to compacted slots) */
#define AP_BOOTSTRAP_MAX_APS 31u /* 0 is BSP, 1-31 are APs */

static ApplicationProcessorBootstrapTable_t ap_bootstrap_table = {0};
static bool application_processor_bootstrap_initialized = false;
static uint8_t ap_bootstrap_static_stacks[AP_BOOTSTRAP_MAX_APS][AP_BOOTSTRAP_STACK_SIZE];

/**
 * @brief Initialize AP bootstrap table and allocate per-AP stacks.
 *
 * This should be called after:
 * - CPU topology discovery (APIC IDs registered)
 * - Heap is operational
 * - Paging is set up
 *
 * Allocates 8 KB stack per non-BSP APIC entry, storing virtual addresses
 * for later use during AP startup (in protected mode entry point).
 */
uint8_t application_processor_bootstrap_initialize(void)
{
    if (application_processor_bootstrap_initialized)
        return 1u;

    uint32_t bsp_apic_id = cpu_topology_get_local_apic_id();
    uint32_t static_stack_index = 0u;

    /* Iterate through discovered APIC IDs and assign per-AP static stacks. */
    for (uint32_t slot = 0u; slot < CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC; ++slot)
    {
        uint8_t apic_id = cpu_topology_get_apic_id_at_slot(slot);

        if (apic_id == 0xFFu) /* Not yet discovered */
            continue;

        /* Skip BSP (it already has a stack) */
        if (apic_id == bsp_apic_id)
            continue;

        if (static_stack_index >= AP_BOOTSTRAP_MAX_APS)
            return 0u;

        /* Initialize entry */
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

/**
 * @brief Retrieve bootstrap entry for a given AP by APIC ID.
 */
ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_get_entry_by_apic_id(uint8_t apic_id)
{
    for (uint32_t slot = 0u; slot < 32u; ++slot)
    {
        if (ap_bootstrap_table.entries[slot].initialized && ap_bootstrap_table.entries[slot].apic_id == apic_id)
        {
            return &ap_bootstrap_table.entries[slot];
        }
    }
    return NULL;
}

/**
 * @brief Retrieve bootstrap entry for a given AP by logical slot.
 */
ApplicationProcessorBootstrapEntry_t *application_processor_bootstrap_get_entry_by_slot(uint32_t logical_slot)
{
    if (logical_slot >= 32u || !ap_bootstrap_table.entries[logical_slot].initialized)
        return NULL;

    return &ap_bootstrap_table.entries[logical_slot];
}

/**
 * @brief Get total count of non-BSP APs discovered.
 */
uint32_t application_processor_bootstrap_get_ap_count(void) { return ap_bootstrap_table.ap_count; }

/**
 * @brief Get kernel virtual address of AP stack (top of allocated region).
 */
void *application_processor_bootstrap_get_ap_stack_top(uint32_t logical_slot)
{
    ApplicationProcessorBootstrapEntry_t *entry = application_processor_bootstrap_get_entry_by_slot(logical_slot);
    if (!entry)
        return NULL;

    /* Stack grows downward; "top" is base + size */
    return (void *) ((uintptr_t) entry->stack_base + entry->stack_size);
}

/**
 * @brief Get physical address of AP stack (for use in real mode code).
 *
 * Note: This assumes identity mapping or explicit paging initialization
 * at the AP bootstrap stage.
 */
uint32_t application_processor_bootstrap_get_ap_stack_phys(uint32_t logical_slot)
{
    void *stack_virt = application_processor_bootstrap_get_ap_stack_top(logical_slot);
    if (!stack_virt)
        return 0u;

    /* Assume identity mapping during bootstrap phase */
    return (uint32_t) (uintptr_t) stack_virt - KERNEL_VIRTUAL_BASE;
}

/**
 * @brief Mark an AP as booted.
 */
void application_processor_bootstrap_mark_booted(uint32_t logical_slot)
{
    ApplicationProcessorBootstrapEntry_t *entry = application_processor_bootstrap_get_entry_by_slot(logical_slot);
    if (entry)
        entry->booted = 1u;
}

/**
 * @brief Query whether an AP has been started.
 */
uint8_t application_processor_bootstrap_is_booted(uint32_t logical_slot)
{
    ApplicationProcessorBootstrapEntry_t *entry = application_processor_bootstrap_get_entry_by_slot(logical_slot);
    return entry ? entry->booted : 0u;
}

/**
 * @brief Reset iteration cursor for iterating through APs.
 */
void application_processor_bootstrap_reset_iteration(void) { ap_bootstrap_table.next_entry_index = 0u; }

/**
 * @brief Get next unbooted AP entry; returns NULL when iteration exhausted.
 *
 * Use application_processor_bootstrap_reset_iteration() to start a new iteration.
 */
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
