#include "simulated_host.h"

#include <kernel/memory/vmm.h>

#include <stddef.h>

#define SIMULATED_HOST_SYSTEM_CONTROL_INTERRUPT_ENABLE 0x1u /**< SCI_EN, bit 0 of a PM1 control register. */

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

/**
 * @brief The index of the page of simulated memory taken at @p page_address, or page_count.
 */
static uint32_t simulated_host_page(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                    uint64_t page_address)
{
    uint32_t index = 0u;

    while (index < simulated->page_count && simulated->pages[index].physical_address != page_address)
        ++index;
    return index;
}

/**
 * @brief The byte at @p physical_address before any write: what a range holds there, or the fill.
 */
static uint8_t simulated_host_initial_byte(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                           uint64_t physical_address)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedRange_t *range =
        simulated_host_range_holding(simulated, physical_address, 1u);

    return (range != NULL) ? range->bytes[physical_address - range->physical_address] : simulated->memory_fill;
}

/**
 * @brief The byte simulated memory holds at @p physical_address.
 */
static uint8_t simulated_host_byte(const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                   uint64_t physical_address)
{
    const uint32_t page = simulated_host_page(simulated, physical_address & ~0xFFFull);

    return (page < simulated->page_count) ? simulated->pages[page].bytes[physical_address & 0xFFFu] :
                                            simulated_host_initial_byte(simulated, physical_address);
}

/**
 * @brief Writes a byte of simulated memory, taking its page at the first write; false when no page
 *        is left to take.
 */
static bool simulated_host_write_byte(AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                      uint64_t physical_address, uint8_t value)
{
    const uint64_t page_address = physical_address & ~0xFFFull;
    const uint32_t page = simulated_host_page(simulated, page_address);

    if (page == simulated->page_count)
    {
        uint8_t *bytes = (simulated->page_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_PAGES) ?
                             kernel_vmm_alloc_pages(1u) :
                             NULL;

        if (bytes == NULL)
            return false;
        for (uint32_t offset = 0u; offset < 4096u; ++offset)
            bytes[offset] = simulated_host_initial_byte(simulated, page_address + offset);
        simulated->pages[page].physical_address = page_address;
        simulated->pages[page].bytes = bytes;
        ++simulated->page_count;
    }
    simulated->pages[page].bytes[physical_address & 0xFFFu] = value;
    return true;
}

/**
 * @brief Logs an access; past the log's capacity it is only counted.
 */
static void simulated_host_log(AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint8_t kind,
                               uint8_t width, uint16_t port, uint64_t address, uint32_t value)
{
    if (simulated->access_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES)
    {
        AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &simulated->accesses[simulated->access_count];

        access->kind = kind;
        access->width = width;
        access->port = port;
        access->value = value;
        access->address = address;
    }
    ++simulated->access_count;
}

/**
 * @brief Records a map of @p kind, or returns NULL when the host keeps as many maps as it can.
 */
static const void *simulated_host_record_map(AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                             const void *address, uint64_t physical_address, uint64_t length,
                                             uint8_t kind)
{
    if (simulated->outstanding_maps == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MAPS)
        return NULL;
    simulated->maps[simulated->outstanding_maps].address = address;
    simulated->maps[simulated->outstanding_maps].length = length;
    simulated->maps[simulated->outstanding_maps].physical_address = physical_address;
    simulated->maps[simulated->outstanding_maps].kind = kind;
    ++simulated->outstanding_maps;
    return address;
}

/**
 * @brief Takes back the open map of @p kind at @p address with @p length, or counts a mismatch.
 */
static void simulated_host_forget_map(AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                      const void *address, uint64_t length, uint8_t kind)
{
    for (uint32_t index = 0u; index < simulated->outstanding_maps; ++index)
    {
        if (simulated->maps[index].address == address && simulated->maps[index].length == length &&
            simulated->maps[index].kind == kind)
        {
            simulated->maps[index] = simulated->maps[--simulated->outstanding_maps];
            return;
        }
    }
    ++simulated->mismatched_unmaps;
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
    return simulated_host_record_map(simulated, range->bytes + (physical_address - range->physical_address),
                                     physical_address, length,
                                     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE);
}

static void simulated_host_unmap_firmware(void *context, const void *address, uint64_t length)
{
    simulated_host_forget_map(context, address, length,
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE);
}

static uint8_t simulated_host_memory_type(void *context, uint64_t physical_address, uint64_t length)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    for (uint32_t index = 0u; index < simulated->memory_type_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedMemoryType_t *range = &simulated->memory_types[index];

        if (physical_address >= range->physical_address && physical_address - range->physical_address < range->length &&
            length <= range->length - (physical_address - range->physical_address))
            return range->type;
    }
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RESERVED;
}

static void *simulated_host_map_memory(AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                       uint64_t physical_address, uint64_t length, uint8_t kind)
{
    const uintptr_t token = (uintptr_t) 0x00100000u * ((simulated->map_serial++ % 1024u) + 1u);

    if (physical_address == 0u || length == 0u || physical_address + length < physical_address)
        return NULL;
    return (void *) simulated_host_record_map(simulated, (const void *) token, physical_address, length, kind);
}

/**
 * @brief Counts an access whose address is not where an open writable or uncached map puts its
 *        physical address.
 */
static void simulated_host_check_address(AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated,
                                         const void *address, uint64_t physical_address, uint8_t width)
{
    for (uint32_t index = 0u; index < simulated->outstanding_maps; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedMap_t *map = &simulated->maps[index];
        const uintptr_t base = (uintptr_t) map->address;

        if (map->kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE &&
            (uintptr_t) address >= base && (uintptr_t) address - base + width <= map->length &&
            (uintptr_t) address - base == physical_address - map->physical_address)
            return;
    }
    ++simulated->inconsistent_accesses;
}

static void *simulated_host_map_firmware_writable(void *context, uint64_t physical_address, uint64_t length)
{
    return simulated_host_map_memory(context, physical_address, length,
                                     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE_WRITABLE);
}

static void simulated_host_unmap_firmware_writable(void *context, void *address, uint64_t length)
{
    simulated_host_forget_map(context, address, length,
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE_WRITABLE);
}

static void *simulated_host_map_device(void *context, uint64_t physical_address, uint64_t length)
{
    return simulated_host_map_memory(context, physical_address, length,
                                     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_DEVICE);
}

static void simulated_host_unmap_device(void *context, void *address, uint64_t length)
{
    simulated_host_forget_map(context, address, length,
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_DEVICE);
}

static uint32_t simulated_host_read_memory(void *context, const void *address, uint64_t physical_address, uint8_t width)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;
    const uint32_t value =
        (uint32_t) advanced_configuration_and_power_interface_simulated_host_peek(simulated, physical_address, width);

    simulated_host_check_address(simulated, address, physical_address, width);
    simulated_host_log(simulated, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MEMORY_READ, width, 0u,
                       physical_address, value);
    if (physical_address == simulated->firmware_lock_address && simulated->firmware_holds_for_reads != 0u &&
        --simulated->firmware_holds_for_reads == 0u)
    {
        const uint32_t released = value & ~0x3u;

        advanced_configuration_and_power_interface_simulated_host_poke(simulated, physical_address, &released, 4u);
    }
    return value;
}

static uint32_t simulated_host_compare_exchange(void *context, void *address, uint64_t physical_address,
                                                uint32_t expected, uint32_t desired)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;
    const uint32_t held =
        (uint32_t) advanced_configuration_and_power_interface_simulated_host_peek(simulated, physical_address, 4u);
    uint32_t left = (held == expected) ? desired : held;

    simulated_host_check_address(simulated, address, physical_address, 4u);
    if (held == expected && physical_address == simulated->firmware_lock_address && simulated->firmware_wants_lock &&
        (expected & 0x2u) == 0u && (desired & 0x2u) != 0u)
    {
        left |= 0x1u;
        simulated->firmware_wants_lock = false;
    }
    advanced_configuration_and_power_interface_simulated_host_poke(simulated, physical_address, &left, 4u);
    simulated_host_log(simulated, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_EXCHANGE, 4u, 0u,
                       physical_address, left);
    return held;
}

static void simulated_host_write_memory(void *context, void *address, uint64_t physical_address, uint8_t width,
                                        uint32_t value)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    simulated_host_check_address(simulated, address, physical_address, width);
    simulated_host_log(simulated, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MEMORY_WRITE, width, 0u,
                       physical_address, value);
    for (uint8_t offset = 0u; offset < width; ++offset)
    {
        if (!simulated_host_write_byte(simulated, physical_address + offset, (uint8_t) (value >> (8u * offset))))
        {
            ++simulated->writes_dropped;
            return;
        }
    }
}

static uint32_t simulated_host_port_read(void *context, uint16_t port, uint8_t width)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;
    const uint32_t mask = (width >= 4u) ? 0xFFFFFFFFu : ((1u << (8u * width)) - 1u);
    uint32_t value = 0u;

    for (uint32_t index = 0u; index < simulated->port_count; ++index)
        if (simulated->ports[index].port == port)
            value = simulated->ports[index].value & mask;
    if (port == simulated->firmware_control_port && simulated->clock_milliseconds >= simulated->firmware_enabled_at)
        value |= SIMULATED_HOST_SYSTEM_CONTROL_INTERRUPT_ENABLE;
    simulated_host_log(simulated, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_READ, width, port, 0u,
                       value);
    return value;
}

static void simulated_host_port_write(void *context, uint16_t port, uint8_t width, uint32_t value)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;

    simulated_host_log(simulated, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE, width, port, 0u,
                       value);
    if (port == simulated->firmware_command_port && port != 0u && width == 1u &&
        value == simulated->firmware_enable_command)
        simulated->firmware_enabled_at = (simulated->firmware_enable_milliseconds == UINT32_MAX) ?
                                             UINT64_MAX :
                                             simulated->clock_milliseconds + simulated->firmware_enable_milliseconds;
}

static uint32_t simulated_host_sleep_milliseconds(void *context, uint32_t milliseconds)
{
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated = context;
    const uint32_t tick = simulated->tick_milliseconds;
    const uint32_t ticks = (tick == 0u) ? 0u : ((milliseconds + tick - 1u) / tick);
    const uint32_t waited = (tick == 0u) ? milliseconds : ((ticks == 0u) ? 1u : ticks) * tick;

    simulated->clock_milliseconds += waited;
    return waited;
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
    simulated->host.memory_type = simulated_host_memory_type;
    simulated->host.map_firmware_writable = simulated_host_map_firmware_writable;
    simulated->host.unmap_firmware_writable = simulated_host_unmap_firmware_writable;
    simulated->host.map_device = simulated_host_map_device;
    simulated->host.unmap_device = simulated_host_unmap_device;
    simulated->host.read_memory = simulated_host_read_memory;
    simulated->host.write_memory = simulated_host_write_memory;
    simulated->host.compare_exchange = simulated_host_compare_exchange;
    simulated->host.port_read = simulated_host_port_read;
    simulated->host.port_write = simulated_host_port_write;
    simulated->host.sleep_milliseconds = simulated_host_sleep_milliseconds;
    simulated->host.allocate_pages = simulated_host_allocate_pages;
    simulated->host.release_pages = simulated_host_release_pages;
    simulated->root_system_description_pointer = 0u;
    simulated->outstanding_maps = 0u;
    simulated->mismatched_unmaps = 0u;
    simulated->range_count = 0u;
    simulated->outstanding_pages = 0u;
    simulated->memory_fill = 0u;
    simulated->page_count = 0u;
    simulated->writes_dropped = 0u;
    simulated->memory_type_count = 0u;
    simulated->port_count = 0u;
    simulated->access_count = 0u;
    simulated->map_serial = 0u;
    simulated->inconsistent_accesses = 0u;
    simulated->firmware_lock_address = 0u;
    simulated->firmware_holds_for_reads = 0u;
    simulated->firmware_wants_lock = false;
    simulated->clock_milliseconds = 0u;
    simulated->tick_milliseconds = 0u;
    simulated->firmware_command_port = 0u;
    simulated->firmware_enable_command = 0u;
    simulated->firmware_control_port = 0u;
    simulated->firmware_enable_milliseconds = 0u;
    simulated->firmware_enabled_at = UINT64_MAX;
}

void advanced_configuration_and_power_interface_simulated_host_close(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated)
{
    while (simulated->page_count > 0u)
        kernel_vmm_free_pages(simulated->pages[--simulated->page_count].bytes, 1u);
}

bool advanced_configuration_and_power_interface_simulated_host_describe(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, uint64_t length,
    uint8_t type)
{
    if (simulated->memory_type_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MEMORY_TYPES)
        return false;

    AdvancedConfigurationAndPowerInterfaceSimulatedMemoryType_t *range =
        &simulated->memory_types[simulated->memory_type_count++];

    range->physical_address = physical_address;
    range->length = length;
    range->type = type;
    return true;
}

bool advanced_configuration_and_power_interface_simulated_host_set_port(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint16_t port, uint32_t value)
{
    if (simulated->port_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_PORTS)
        return false;
    simulated->ports[simulated->port_count].port = port;
    simulated->ports[simulated->port_count].value = value;
    ++simulated->port_count;
    return true;
}

uint64_t advanced_configuration_and_power_interface_simulated_host_peek(
    const AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, uint8_t width)
{
    uint64_t value = 0u;

    for (uint8_t offset = 0u; offset < width; ++offset)
        value |= (uint64_t) simulated_host_byte(simulated, physical_address + offset) << (8u * offset);
    return value;
}

bool advanced_configuration_and_power_interface_simulated_host_poke(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, const void *bytes,
    uint32_t length)
{
    const uint8_t *source = bytes;

    for (uint32_t offset = 0u; offset < length; ++offset)
        if (!simulated_host_write_byte(simulated, physical_address + offset, source[offset]))
            return false;
    return true;
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
