#include <kernel/cpu/numa_policy.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/drivers/serial.h>
#include <stddef.h>

#define NUMA_POLICY_MAX_SLOTS CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC
#define NUMA_POLICY_MAX_RANGES 32u

typedef struct {
    uint32_t base;
    uint32_t length;
    uint32_t node_id;
} NumaMemoryRange_t;

static uint32_t numa_slot_to_node_map[NUMA_POLICY_MAX_SLOTS] = {0};
static NumaNodeInfo_t numa_nodes[NUMA_POLICY_MAX_NODES] = {0};
static uint32_t numa_node_count = 1u;
static NumaMemoryRange_t numa_ranges[NUMA_POLICY_MAX_RANGES] = {0};
static uint32_t numa_range_count = 0u;

static void numa_policy_parse_srat(const AdvancedConfigurationAndPowerInterfaceSrat_t *srat)
{
    const uint8_t *entry_ptr = (const uint8_t *) srat + sizeof(AdvancedConfigurationAndPowerInterfaceSrat_t);
    const uint8_t *entry_end = (const uint8_t *) srat + srat->header.length;

    while (entry_ptr + sizeof(AdvancedConfigurationAndPowerInterfaceSratEntryHeader_t) <= entry_end)
    {
        const AdvancedConfigurationAndPowerInterfaceSratEntryHeader_t *entry = 
            (const AdvancedConfigurationAndPowerInterfaceSratEntryHeader_t *) entry_ptr;

        if (entry->length == 0u || entry_ptr + entry->length > entry_end)
            break;

        if (entry->type == 0u && entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceSratLocalApicEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceSratLocalApicEntry_t *lapic_entry =
                (const AdvancedConfigurationAndPowerInterfaceSratLocalApicEntry_t *) entry_ptr;
            
            if ((lapic_entry->flags & 1u) != 0u)
            {
                uint32_t domain = lapic_entry->proximity_domain_low |
                    ((uint32_t)lapic_entry->proximity_domain_high[0] << 8u) |
                    ((uint32_t)lapic_entry->proximity_domain_high[1] << 16u) |
                    ((uint32_t)lapic_entry->proximity_domain_high[2] << 24u);
                
                uint32_t apic_id = lapic_entry->apic_id;
                uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_id);
                
                if (slot < NUMA_POLICY_MAX_SLOTS)
                {
                    numa_slot_to_node_map[slot] = domain;
                    if (domain >= numa_node_count && domain < NUMA_POLICY_MAX_NODES)
                    {
                        numa_node_count = domain + 1u;
                        numa_nodes[domain].node_id = domain;
                    }
                    if (domain < NUMA_POLICY_MAX_NODES)
                        numa_nodes[domain].cpus_bound++;
                }
            }
        }
        else if (entry->type == 2u && entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceSratX2ApicEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceSratX2ApicEntry_t *x2apic_entry =
                (const AdvancedConfigurationAndPowerInterfaceSratX2ApicEntry_t *) entry_ptr;
            
            if ((x2apic_entry->flags & 1u) != 0u)
            {
                uint32_t domain = x2apic_entry->proximity_domain;
                uint32_t apic_id = x2apic_entry->x2apic_id;
                uint32_t slot = cpu_topology_get_slot_for_apic_id(apic_id);
                
                if (slot < NUMA_POLICY_MAX_SLOTS)
                {
                    numa_slot_to_node_map[slot] = domain;
                    if (domain >= numa_node_count && domain < NUMA_POLICY_MAX_NODES)
                    {
                        numa_node_count = domain + 1u;
                        numa_nodes[domain].node_id = domain;
                    }
                    if (domain < NUMA_POLICY_MAX_NODES)
                        numa_nodes[domain].cpus_bound++;
                }
            }
        }
        else if (entry->type == 1u && entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceSratMemoryEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceSratMemoryEntry_t *mem_entry =
                (const AdvancedConfigurationAndPowerInterfaceSratMemoryEntry_t *) entry_ptr;
            
            if ((mem_entry->flags & 1u) != 0u)
            {
                uint32_t domain = mem_entry->proximity_domain;
                if (domain >= numa_node_count && domain < NUMA_POLICY_MAX_NODES)
                {
                    numa_node_count = domain + 1u;
                    numa_nodes[domain].node_id = domain;
                }
                if (domain < NUMA_POLICY_MAX_NODES)
                {
                    numa_nodes[domain].memory_ranges++;
                    if (numa_range_count < NUMA_POLICY_MAX_RANGES)
                    {
                        numa_ranges[numa_range_count].base = (uint32_t)mem_entry->base_address;
                        numa_ranges[numa_range_count].length = (uint32_t)mem_entry->region_length;
                        numa_ranges[numa_range_count].node_id = domain;
                        numa_range_count++;
                    }
                }
            }
        }
        
        entry_ptr += entry->length;
    }
}

void numa_policy_initialize(void)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *srat_header;

    for (uint32_t i = 0; i < NUMA_POLICY_MAX_SLOTS; ++i)
        numa_slot_to_node_map[i] = 0u;

    numa_nodes[0].node_id = 0u;
    numa_node_count = 1u;
    numa_range_count = 0u;

    srat_header = advanced_configuration_and_power_interface_find_table("SRAT");
    if (srat_header)
    {
        numa_policy_parse_srat((const AdvancedConfigurationAndPowerInterfaceSrat_t *) srat_header);
        serial_write_string(&com1, "[Laplace Kernel]: NUMA discovery ready, ");
        serial_write_int(&com1, (int32_t) numa_node_count);
        serial_write_string(&com1, " nodes discovered via SRAT\n");
    }
    else
    {
        serial_write_string(&com1, "[Laplace Kernel]: NUMA discovery fallback, single node 0 assumed\n");
    }
    
    for (uint32_t i = 0; i < numa_node_count; ++i)
    {
        if (numa_nodes[i].cpus_bound > 0 || numa_nodes[i].memory_ranges > 0 || i == 0)
        {
            serial_write_string(&com1, "[Laplace Kernel]: NUMA node ");
            serial_write_int(&com1, (int32_t) i);
            serial_write_string(&com1, ": ");
            serial_write_int(&com1, (int32_t) numa_nodes[i].cpus_bound);
            serial_write_string(&com1, " CPUs bound, ");
            serial_write_int(&com1, (int32_t) numa_nodes[i].memory_ranges);
            serial_write_string(&com1, " memory ranges\n");
        }
    }
}

uint32_t numa_policy_get_node_for_slot(uint32_t slot)
{
    if (slot >= NUMA_POLICY_MAX_SLOTS)
        return 0u;
    return numa_slot_to_node_map[slot];
}

uint32_t numa_policy_get_local_node(void)
{
    return numa_policy_get_node_for_slot(cpu_topology_get_logical_slot());
}

uint32_t numa_policy_get_node_count(void)
{
    return numa_node_count;
}

uint32_t numa_policy_get_node_for_address(uint32_t phys_addr)
{
    for (uint32_t i = 0; i < numa_range_count; ++i)
    {
        if (phys_addr >= numa_ranges[i].base && phys_addr < numa_ranges[i].base + numa_ranges[i].length)
        {
            return numa_ranges[i].node_id;
        }
    }
    return 0u; // Fallback to Node 0
}
