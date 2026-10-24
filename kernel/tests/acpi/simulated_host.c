#include "simulated_host.h"

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

    if (range == NULL || length == 0u)
        return NULL;
    ++simulated->outstanding_maps;
    return range->bytes + (physical_address - range->physical_address);
}

static void simulated_host_unmap_firmware(void *context, const void *address, uint64_t length)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    (void) address;
    (void) length;
    if (simulated->outstanding_maps != 0u)
        --simulated->outstanding_maps;
}

void advanced_configuration_and_power_interface_simulated_host_open(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated)
{
    simulated->host.context = simulated;
    simulated->host.root_system_description_pointer = simulated_host_root_system_description_pointer;
    simulated->host.map_firmware = simulated_host_map_firmware;
    simulated->host.unmap_firmware = simulated_host_unmap_firmware;
    simulated->root_system_description_pointer = 0u;
    simulated->outstanding_maps = 0u;
    simulated->range_count = 0u;
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
