#include "simulated_host.h"

#include <kernel/memory/vmm.h>

#include <stddef.h>

/**
 * @brief The range of @p simulated that holds every byte of [@p physical_address,
 *        @p physical_address + @p length), or NULL.
 */
static const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *
simulated_host_range_holding(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                             uint64_t physical_address, uint64_t length)
{
    for (uint32_t index = 0u; index < simulated->range_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *range = &simulated->ranges[index];

        if (physical_address >= range->physical_address &&
            physical_address - range->physical_address <= range->length &&
            length <= range->length - (physical_address - range->physical_address))
            return range;
    }
    return NULL;
}

static uint64_t simulated_host_root_system_description_pointer(void *context)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    return simulated->root_system_description_pointer;
}

static const void *simulated_host_map_firmware(void *context, uint64_t physical_address, uint64_t length)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;
    const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *range =
        simulated_host_range_holding(simulated, physical_address, length);

    if (range == NULL || length == 0u ||
        simulated->outstanding_maps == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MAPS)
        return NULL;

    const void *address = range->bytes + (physical_address - range->physical_address);

    simulated->maps[simulated->outstanding_maps].address = address;
    simulated->maps[simulated->outstanding_maps].length = length;
    ++simulated->outstanding_maps;
    return address;
}

static void simulated_host_unmap_firmware(void *context, const void *address, uint64_t length)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    for (uint32_t index = 0u; index < simulated->outstanding_maps; ++index)
    {
        if (simulated->maps[index].address == address && simulated->maps[index].length == length)
        {
            simulated->maps[index] = simulated->maps[--simulated->outstanding_maps];
            return;
        }
    }
    ++simulated->mismatched_unmaps;
}

static void *simulated_host_allocate_pages(void *context, uint32_t count)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;
    void *pages = kernel_vmm_alloc_pages(count);

    if (pages != NULL)
        simulated->outstanding_pages += count;
    return pages;
}

static void simulated_host_release_pages(void *context, void *address, uint32_t count)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    kernel_vmm_free_pages(address, count);
    simulated->outstanding_pages -= count;
}

void advanced_configuration_and_power_interface_simulated_host_open(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated)
{
    simulated->host.context = simulated;
    simulated->host.root_system_description_pointer = simulated_host_root_system_description_pointer;
    simulated->host.map_firmware = simulated_host_map_firmware;
    simulated->host.unmap_firmware = simulated_host_unmap_firmware;
    simulated->host.allocate_pages = simulated_host_allocate_pages;
    simulated->host.release_pages = simulated_host_release_pages;
    simulated->root_system_description_pointer = 0u;
    simulated->outstanding_maps = 0u;
    simulated->mismatched_unmaps = 0u;
    simulated->range_count = 0u;
    simulated->outstanding_pages = 0u;
}

bool advanced_configuration_and_power_interface_simulated_host_place(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, const void *bytes,
    uint64_t length)
{
    if (length == 0u || physical_address + length < physical_address ||
        simulated->range_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES)
        return false;

    for (uint32_t index = 0u; index < simulated->range_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *range = &simulated->ranges[index];

        if (physical_address < range->physical_address + range->length &&
            range->physical_address < physical_address + length)
            return false;
    }

    AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *range = &simulated->ranges[simulated->range_count++];

    range->physical_address = physical_address;
    range->length = length;
    range->bytes = bytes;
    return true;
}
