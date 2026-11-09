#include <kernel/acpi/tables.h>
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pmm.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/vmm.h>

#include <stddef.h>
#include <string.h>

/** Identity dword: vendor id | device id << 16. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_DWORD_IDENTITY 0x00u

/** Classification dword: revision | prog-if << 8 | subclass << 16 | class << 24. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_DWORD_CLASSIFICATION 0x08u

/** Header dword: the header type is byte 2. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_DWORD_HEADER 0x0Cu

/**
 * @brief Builds the CONFIG_ADDRESS word of configuration mechanism #1.
 *
 * @details The word is written to CONFIG_ADDRESS (0xCF8), then the targeted register is
 *          read or written through CONFIG_DATA (0xCFC).
 *
 * Address layout:
 *   bit 31     enable
 *   bits 23-16 bus
 *   bits 15-11 device (0-31)
 *   bits 10-8  function (0-7)
 *   bits 7-2   register offset (dword aligned)
 *   bits 1-0   always zero
 */
static uint32_t peripheral_component_interconnect_build_address(uint8_t bus, uint8_t device, uint8_t function,
                                                                uint8_t offset)
{
    return (uint32_t) ((1u << 31u) | ((uint32_t) bus << 16u) | (((uint32_t) device & 0x1Fu) << 11u) |
                       (((uint32_t) function & 0x07u) << 8u) | ((uint32_t) offset & 0xFCu));
}

/**
 * @brief Which bits of a base address register are writable, the way its size is read.
 *
 * @details Writes all-ones, reads back the bits the device let through, then restores the
 *          original value.
 *
 * @param bus      Bus number.
 * @param device   Device number.
 * @param function Function number.
 * @param offset   Configuration offset of the base address register.
 * @param original The register's value, written back afterwards.
 * @return The readback of all-ones.
 */
static uint32_t peripheral_component_interconnect_probe_writable_bits(uint8_t bus, uint8_t device, uint8_t function,
                                                                      uint8_t offset, uint32_t original)
{
    peripheral_component_interconnect_config_write_dword(bus, device, function, offset, 0xFFFFFFFFu);
    const uint32_t probe = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);
    peripheral_component_interconnect_config_write_dword(bus, device, function, offset, original);
    return probe;
}

#define PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_DEVICES   32u         /**< Devices on one bus. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_FUNCTIONS 8u          /**< Functions of one device. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALL_ONES  0xFFFFFFFFu /**< What a refused dword read gives. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_BUS_SHIFT 20u         /**< One bus spans 1 MiB of ECAM. */
#define PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALLOCATIONS                                                          \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_CONFIGURATION_SPACE_ALLOCATION_COUNT

_Static_assert(KERNEL_VMM_MAX_UNCACHED_RANGES >= PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALLOCATIONS,
               "the virtual memory manager holds a range for every window the MCFG keeps");

static uint32_t peripheral_component_interconnect_express_window = 0u;       /**< The mapped function's 4 KiB, or 0. */
static uint32_t peripheral_component_interconnect_express_window_frame = 0u; /**< Their physical address. */
static uint32_t peripheral_component_interconnect_express_moves = 0u;        /**< Window moves since boot. */
static PeripheralComponentInterconnectExpressWindowCounts_t peripheral_component_interconnect_express_counts = {0};
static bool peripheral_component_interconnect_express_declared[PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALLOCATIONS] =
    {false}; /**< Whether the MCFG's window of the same index was declared, so that accesses may use it. */

static PeripheralComponentInterconnectDevice_t
    peripheral_component_interconnect_devices[PERIPHERAL_COMPONENT_INTERCONNECT_MAX_DEVICES];

/**
 * @brief The bridge that leads to one bus, as a scan found it.
 */
typedef struct PeripheralComponentInterconnectBridgeLink {
    uint8_t bus;      /**< The bridge's bus. */
    uint8_t device;   /**< Its device. */
    uint8_t function; /**< Its function. */
    uint8_t found;    /**< 1 once a bridge names the bus as its secondary one. */
} PeripheralComponentInterconnectBridgeLink_t;

static PeripheralComponentInterconnectBridgeLink_t
    peripheral_component_interconnect_bridge_links[PERIPHERAL_COMPONENT_INTERCONNECT_BUSES]; /**< Indexed by the
                                                                                                secondary bus. */

static uint32_t peripheral_component_interconnect_device_count = 0u;

static void peripheral_component_interconnect_read_function(uint8_t bus, uint8_t device, uint8_t function,
                                                            PeripheralComponentInterconnectDevice_t *entry)
{
    uint32_t identity = peripheral_component_interconnect_config_read_dword(
        bus, device, function, PERIPHERAL_COMPONENT_INTERCONNECT_DWORD_IDENTITY);
    uint32_t classification = peripheral_component_interconnect_config_read_dword(
        bus, device, function, PERIPHERAL_COMPONENT_INTERCONNECT_DWORD_CLASSIFICATION);
    uint32_t header = peripheral_component_interconnect_config_read_dword(
        bus, device, function, PERIPHERAL_COMPONENT_INTERCONNECT_DWORD_HEADER);

    entry->bus = bus;
    entry->device = device;
    entry->function = function;
    entry->vendor_id = (uint16_t) (identity & 0xFFFFu);
    entry->device_id = (uint16_t) ((identity >> 16u) & 0xFFFFu);
    entry->revision_id = (uint8_t) (classification & 0xFFu);
    entry->prog_interface = (uint8_t) ((classification >> 8u) & 0xFFu);
    entry->subclass = (uint8_t) ((classification >> 16u) & 0xFFu);
    entry->class_code = (uint8_t) ((classification >> 24u) & 0xFFu);
    entry->header_type = (uint8_t) ((header >> 16u) & 0xFFu);
}

static void peripheral_component_interconnect_record_function(const PeripheralComponentInterconnectDevice_t *found)
{
    if (peripheral_component_interconnect_device_count >= PERIPHERAL_COMPONENT_INTERCONNECT_MAX_DEVICES)
        return;
    peripheral_component_interconnect_devices[peripheral_component_interconnect_device_count++] = *found;
}

static void peripheral_component_interconnect_record_bridge(const PeripheralComponentInterconnectDevice_t *found)
{
    const uint8_t layout = found->header_type & PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_TYPE_LAYOUT_MASK;

    if (layout != PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_LAYOUT_BRIDGE &&
        layout != PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_LAYOUT_CARDBUS)
        return;

    const uint8_t secondary = peripheral_component_interconnect_config_read_byte(
        found->bus, found->device, found->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_SECONDARY_BUS);
    PeripheralComponentInterconnectBridgeLink_t *link = &peripheral_component_interconnect_bridge_links[secondary];

    if (secondary == 0u || link->found != 0u)
        return;
    *link = (PeripheralComponentInterconnectBridgeLink_t){.bus = found->bus,
                                                          .device = found->device,
                                                          .function = found->function,
                                                          .found = 1u};
}

/**
 * @brief The declared ECAM window over @p bus of @p segment_group, or NULL: the first the boot's MCFG lists over it,
 *        when peripheral_component_interconnect_express_initialize declared it.
 */
static const AdvancedConfigurationAndPowerInterfaceConfigurationSpaceAllocation_t *
peripheral_component_interconnect_express_allocation(uint16_t segment_group, uint8_t bus)
{
    const AdvancedConfigurationAndPowerInterfaceMemoryMappedConfigurationState_t *mcfg =
        advanced_configuration_and_power_interface_memory_mapped_configuration();

    for (uint32_t index = 0u; index < mcfg->allocation_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceConfigurationSpaceAllocation_t *allocation =
            &mcfg->allocations[index];

        if (allocation->segment_group == segment_group && bus >= allocation->start_bus && bus <= allocation->end_bus)
            return peripheral_component_interconnect_express_declared[index] ? allocation : NULL;
    }
    return NULL;
}

/**
 * @brief Whether the configuration space of the buses up to @p end_bus, from @p base_address, ends below 4 GiB,
 *        which this kernel's 32-bit physical space reaches, without wrapping whatever the base.
 */
static bool peripheral_component_interconnect_express_is_reachable(uint64_t base_address, uint8_t end_bus)
{
    return base_address <= UINT32_MAX &&
           base_address + (((uint64_t) end_bus + 1u) << PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_BUS_SHIFT) - 1u <=
               UINT32_MAX;
}

/**
 * @brief The first byte of the configuration space of the buses from @p start_bus, from @p base_address.
 */
static uint64_t peripheral_component_interconnect_express_window_first(uint64_t base_address, uint8_t start_bus)
{
    return base_address + ((uint64_t) start_bus << PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_BUS_SHIFT);
}

/**
 * @brief The bytes of the configuration space of the buses from @p start_bus to @p end_bus.
 */
static uint64_t peripheral_component_interconnect_express_window_size(uint8_t start_bus, uint8_t end_bus)
{
    return ((uint64_t) end_bus - start_bus + 1u) << PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_BUS_SHIFT;
}

/**
 * @brief The value a refused read of @p width bytes gives: that many bytes of ones, as an absent function reads.
 */
static uint32_t peripheral_component_interconnect_express_all_ones(uint8_t width)
{
    if (width == 1u)
        return 0xFFu;
    if (width == 2u)
        return 0xFFFFu;
    return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALL_ONES;
}

/**
 * @brief Whether a device, a function, an offset and a width name one register of a function's 4 KiB.
 */
static bool peripheral_component_interconnect_express_names_a_register(uint8_t device, uint8_t function,
                                                                       uint16_t offset, uint8_t width)
{
    return (width == 1u || width == 2u || width == 4u) && offset % width == 0u &&
           offset <= PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE - width &&
           device < PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_DEVICES &&
           function < PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_FUNCTIONS;
}

/**
 * @brief The register at @p offset of a function through its ECAM window, the window moved onto the function when
 *        it held another, or NULL when no window below 4 GiB covers the bus or the function cannot be mapped.
 */
static volatile uint8_t *peripheral_component_interconnect_express_register(uint16_t segment_group, uint8_t bus,
                                                                            uint8_t device, uint8_t function,
                                                                            uint16_t offset)
{
    const AdvancedConfigurationAndPowerInterfaceConfigurationSpaceAllocation_t *allocation =
        peripheral_component_interconnect_express_allocation(segment_group, bus);

    if (allocation == NULL)
        return NULL;

    const uint32_t frame =
        (uint32_t) allocation->base_address + ((uint32_t) bus << PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_BUS_SHIFT |
                                               (uint32_t) device << 15u | (uint32_t) function << 12u);

    if (peripheral_component_interconnect_express_window == 0u ||
        peripheral_component_interconnect_express_window_frame != frame)
    {
        const uint32_t window =
            kernel_vmm_map_device_window(frame, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE);

        if (window == 0u)
            return NULL;
        if (peripheral_component_interconnect_express_window != 0u)
            kernel_vmm_unmap_device_window(peripheral_component_interconnect_express_window,
                                           PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE);
        peripheral_component_interconnect_express_window = window;
        peripheral_component_interconnect_express_window_frame = frame;
        ++peripheral_component_interconnect_express_moves;
    }
    return (volatile uint8_t *) (uintptr_t) (peripheral_component_interconnect_express_window + offset);
}

/**
 * @brief Whether configuration mechanism #1 reaches @p width bytes at @p offset of segment group @p segment_group.
 */
static bool peripheral_component_interconnect_express_ports_reach(uint16_t segment_group, uint16_t offset,
                                                                  uint8_t width)
{
    return segment_group == 0u && offset + width <= PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE;
}

/**
 * @brief A function the walk of its extended capabilities reads through ECAM.
 */
typedef struct PeripheralComponentInterconnectExpressFunction {
    uint16_t segment_group; /**< Its segment group. */
    uint8_t bus;            /**< Its bus. */
    uint8_t device;         /**< Its device. */
    uint8_t function;       /**< Its function. */
} PeripheralComponentInterconnectExpressFunction_t;

/**
 * @brief Reads one dword of the function @p context names through peripheral_component_interconnect_express_read.
 */
static bool peripheral_component_interconnect_express_read_function(void *context, uint16_t offset, uint32_t *out_value)
{
    const PeripheralComponentInterconnectExpressFunction_t *function = context;

    return peripheral_component_interconnect_express_read(function->segment_group, function->bus, function->device,
                                                          function->function, offset, 4u, out_value);
}

/**
 * @brief How a walk of @p count capabilities that reached a next offset of 0 ended, with room for @p capacity.
 */
static PeripheralComponentInterconnectExpressWalk_t
peripheral_component_interconnect_express_walk_end(uint32_t count, uint32_t capacity)
{
    return (count > capacity) ? PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_TRUNCATED :
                                PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_LISTED;
}

uint32_t peripheral_component_interconnect_config_read_dword(uint8_t bus, uint8_t device, uint8_t function,
                                                             uint8_t offset)
{
    asmutils_output_dword(PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_ADDRESS_PORT,
                          peripheral_component_interconnect_build_address(bus, device, function, offset));
    return asmutils_input_dword(PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_DATA_PORT);
}

uint16_t peripheral_component_interconnect_config_read_word(uint8_t bus, uint8_t device, uint8_t function,
                                                            uint8_t offset)
{
    uint32_t dword = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);

    return (uint16_t) ((dword >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t peripheral_component_interconnect_config_read_byte(uint8_t bus, uint8_t device, uint8_t function,
                                                           uint8_t offset)
{
    uint32_t dword = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);

    return (uint8_t) ((dword >> ((offset & 3u) * 8u)) & 0xFFu);
}

void peripheral_component_interconnect_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                                                          uint32_t value)
{
    asmutils_output_dword(PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_ADDRESS_PORT,
                          peripheral_component_interconnect_build_address(bus, device, function, offset));
    asmutils_output_dword(PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_DATA_PORT, value);
}

void peripheral_component_interconnect_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                                                         uint16_t value)
{
    uint32_t dword = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);
    uint32_t shift = (offset & 2u) * 8u;

    dword = (dword & ~(0xFFFFu << shift)) | ((uint32_t) value << shift);
    peripheral_component_interconnect_config_write_dword(bus, device, function, offset, dword);
}

void peripheral_component_interconnect_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                                                         uint8_t value)
{
    uint32_t dword = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);
    uint32_t shift = (offset & 3u) * 8u;

    dword = (dword & ~(0xFFu << shift)) | ((uint32_t) value << shift);
    peripheral_component_interconnect_config_write_dword(bus, device, function, offset, dword);
}

uint32_t peripheral_component_interconnect_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                                                       uint8_t width)
{
    const uint16_t port = (uint16_t) (PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_DATA_PORT + (offset & 3u));

    asmutils_output_dword(PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_ADDRESS_PORT,
                          peripheral_component_interconnect_build_address(bus, device, function, offset));
    switch (width)
    {
    case 1u: return asmutils_input_byte(port);
    case 2u: return asmutils_input_word(port);
    default: return asmutils_input_dword(port);
    }
}

void peripheral_component_interconnect_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                                                    uint8_t width, uint32_t value)
{
    const uint16_t port = (uint16_t) (PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_DATA_PORT + (offset & 3u));

    asmutils_output_dword(PERIPHERAL_COMPONENT_INTERCONNECT_CONFIG_ADDRESS_PORT,
                          peripheral_component_interconnect_build_address(bus, device, function, offset));
    switch (width)
    {
    case 1u: asmutils_output_byte(port, (uint8_t) value); break;
    case 2u: asmutils_output_word(port, (uint16_t) value); break;
    default: asmutils_output_dword(port, value); break;
    }
}

void peripheral_component_interconnect_scan(void)
{
    peripheral_component_interconnect_device_count = 0u;
    memset(peripheral_component_interconnect_bridge_links, 0, sizeof(peripheral_component_interconnect_bridge_links));

    for (uint16_t bus = 0u; bus < 256u; ++bus)
    {
        for (uint8_t device = 0u; device < 32u; ++device)
        {
            uint8_t bus8 = (uint8_t) bus;
            uint16_t vendor_id = peripheral_component_interconnect_config_read_word(bus8, device, 0u, 0x00u);

            if (vendor_id == PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID)
                continue;

            uint8_t header_type = peripheral_component_interconnect_config_read_byte(bus8, device, 0u, 0x0Eu);
            uint8_t function_count =
                (header_type & PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_TYPE_MULTIFUNCTION_MASK) ? 8u : 1u;

            for (uint8_t function = 0u; function < function_count; ++function)
            {
                uint16_t function_vendor =
                    peripheral_component_interconnect_config_read_word(bus8, device, function, 0x00u);

                if (function_vendor == PERIPHERAL_COMPONENT_INTERCONNECT_INVALID_VENDOR_ID)
                    continue;

                PeripheralComponentInterconnectDevice_t found;

                peripheral_component_interconnect_read_function(bus8, device, function, &found);
                peripheral_component_interconnect_record_function(&found);
                peripheral_component_interconnect_record_bridge(&found);
            }
        }
    }
}

uint32_t peripheral_component_interconnect_get_device_count(void)
{
    return peripheral_component_interconnect_device_count;
}

const PeripheralComponentInterconnectDevice_t *peripheral_component_interconnect_get_device(uint32_t index)
{
    if (index >= peripheral_component_interconnect_device_count)
        return (const PeripheralComponentInterconnectDevice_t *) 0;

    return &peripheral_component_interconnect_devices[index];
}

uint8_t peripheral_component_interconnect_read_base_address_register(
    uint8_t bus, uint8_t device, uint8_t function, uint8_t index,
    PeripheralComponentInterconnectBaseAddressRegister_t *out_bar)
{
    if (!out_bar || index >= PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT)
        return 0u;

    uint8_t offset = (uint8_t) (PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_BASE_ADDRESS_0 + (index * 4u));
    uint32_t original = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);

    out_bar->index = index;
    out_bar->is_io = (uint8_t) (original & 0x1u);
    out_bar->is_64bit = 0u;
    out_bar->prefetchable = 0u;
    out_bar->base = 0u;
    out_bar->size = 0u;

    uint32_t probe = peripheral_component_interconnect_probe_writable_bits(bus, device, function, offset, original);

    if (out_bar->is_io)
    {
        uint32_t mask = probe & 0xFFFFFFFCu;

        out_bar->base = (uint64_t) (original & 0xFFFFFFFCu);
        out_bar->size = mask ? (uint64_t) ((~mask + 1u) & 0xFFFFu) : 0u;
        return (uint8_t) (out_bar->size != 0u);
    }

    out_bar->prefetchable = (uint8_t) ((original & 0x8u) != 0u);
    out_bar->is_64bit = (uint8_t) (((original >> 1u) & 0x3u) == 0x2u);

    uint32_t base_low = original & 0xFFFFFFF0u;
    uint32_t base_high = 0u;
    if (out_bar->is_64bit && index < (PERIPHERAL_COMPONENT_INTERCONNECT_BASE_ADDRESS_REGISTER_COUNT - 1u))
        base_high = peripheral_component_interconnect_config_read_dword(bus, device, function, (uint8_t) (offset + 4u));

    out_bar->base = ((uint64_t) base_high << 32u) | (uint64_t) base_low;

    uint32_t size_mask = probe & 0xFFFFFFF0u;
    out_bar->size = size_mask ? (uint64_t) (~size_mask + 1u) : 0u;

    return (uint8_t) (out_bar->size != 0u);
}

bool peripheral_component_interconnect_memory_window_is_reachable(
    const PeripheralComponentInterconnectBaseAddressRegister_t *bar)
{
    return !bar->is_io && (bar->base >> 32u) == 0u;
}

bool peripheral_component_interconnect_find_bridge_to(uint8_t bus, PeripheralComponentInterconnectDevice_t *out_bridge)
{
    const PeripheralComponentInterconnectBridgeLink_t *link = &peripheral_component_interconnect_bridge_links[bus];

    if (link->found == 0u)
        return false;
    peripheral_component_interconnect_read_function(link->bus, link->device, link->function, out_bridge);
    return true;
}

const PeripheralComponentInterconnectDevice_t *peripheral_component_interconnect_find_by_class(uint8_t class_code,
                                                                                               uint8_t subclass)
{
    const uint32_t count = peripheral_component_interconnect_get_device_count();

    for (uint32_t index = 0u; index < count; ++index)
    {
        const PeripheralComponentInterconnectDevice_t *candidate = peripheral_component_interconnect_get_device(index);
        if (candidate == NULL)
            continue;
        if (candidate->class_code == class_code && candidate->subclass == subclass)
            return candidate;
    }
    return NULL;
}

void peripheral_component_interconnect_enable_memory_and_bus_mastering(
    const PeripheralComponentInterconnectDevice_t *device)
{
    const uint16_t command = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);

    peripheral_component_interconnect_config_write_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND,
        (uint16_t) (command | PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_MEMORY_SPACE |
                    PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_BUS_MASTER));
}

void peripheral_component_interconnect_disable_pin_interrupt(const PeripheralComponentInterconnectDevice_t *device)
{
    const uint16_t command = peripheral_component_interconnect_config_read_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND);

    peripheral_component_interconnect_config_write_word(
        device->bus, device->device, device->function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_COMMAND,
        (uint16_t) (command | PERIPHERAL_COMPONENT_INTERCONNECT_COMMAND_INTERRUPT_DISABLE));
}

PeripheralComponentInterconnectExpressWindow_t
peripheral_component_interconnect_express_judge_window(uint64_t base_address, uint8_t start_bus, uint8_t end_bus)
{
    if (!peripheral_component_interconnect_express_is_reachable(base_address, end_bus))
        return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH;

    if (physical_memory_manager_reaches_ram(
            peripheral_component_interconnect_express_window_first(base_address, start_bus),
            peripheral_component_interconnect_express_window_size(start_bus, end_bus)))
        return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_OVER_RAM;
    return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_USABLE;
}

void peripheral_component_interconnect_express_initialize(void)
{
    const AdvancedConfigurationAndPowerInterfaceMemoryMappedConfigurationState_t *mcfg =
        advanced_configuration_and_power_interface_memory_mapped_configuration();
    PeripheralComponentInterconnectExpressWindowCounts_t *const counts =
        &peripheral_component_interconnect_express_counts;

    for (uint32_t index = 0u; index < mcfg->allocation_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceConfigurationSpaceAllocation_t *allocation =
            &mcfg->allocations[index];
        const PeripheralComponentInterconnectExpressWindow_t verdict =
            peripheral_component_interconnect_express_judge_window(allocation->base_address, allocation->start_bus,
                                                                   allocation->end_bus);
        const uint64_t first =
            peripheral_component_interconnect_express_window_first(allocation->base_address, allocation->start_bus);
        const uint64_t size =
            peripheral_component_interconnect_express_window_size(allocation->start_bus, allocation->end_bus);

        if (verdict == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH)
            ++counts->past_reach;
        else if (verdict == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_OVER_RAM)
            ++counts->over_ram;
        else if (!kernel_vmm_declare_uncached_range(first, size))
            ++counts->no_room;
        else
        {
            peripheral_component_interconnect_express_declared[index] = true;
            ++counts->declared;
        }
    }
}

bool peripheral_component_interconnect_express_read(uint16_t segment_group, uint8_t bus, uint8_t device,
                                                    uint8_t function, uint16_t offset, uint8_t width,
                                                    uint32_t *out_value)
{
    *out_value = peripheral_component_interconnect_express_all_ones(width);
    if (!peripheral_component_interconnect_express_names_a_register(device, function, offset, width))
        return false;

    volatile uint8_t *const register_address =
        peripheral_component_interconnect_express_register(segment_group, bus, device, function, offset);

    if (register_address != NULL)
    {
        switch (width)
        {
        case 1u: *out_value = *register_address; break;
        case 2u: *out_value = *(volatile uint16_t *) register_address; break;
        default: *out_value = *(volatile uint32_t *) register_address; break;
        }
        return true;
    }
    if (!peripheral_component_interconnect_express_ports_reach(segment_group, offset, width))
        return false;
    *out_value = peripheral_component_interconnect_config_read(bus, device, function, (uint8_t) offset, width);
    return true;
}

bool peripheral_component_interconnect_express_write(uint16_t segment_group, uint8_t bus, uint8_t device,
                                                     uint8_t function, uint16_t offset, uint8_t width, uint32_t value)
{
    if (!peripheral_component_interconnect_express_names_a_register(device, function, offset, width))
        return false;

    volatile uint8_t *const register_address =
        peripheral_component_interconnect_express_register(segment_group, bus, device, function, offset);

    if (register_address != NULL)
    {
        switch (width)
        {
        case 1u: *register_address = (uint8_t) value; break;
        case 2u: *(volatile uint16_t *) register_address = (uint16_t) value; break;
        default: *(volatile uint32_t *) register_address = value; break;
        }
        return true;
    }
    if (!peripheral_component_interconnect_express_ports_reach(segment_group, offset, width))
        return false;
    peripheral_component_interconnect_config_write(bus, device, function, (uint8_t) offset, width, value);
    return true;
}

bool peripheral_component_interconnect_express_reaches(uint16_t segment_group, uint8_t bus)
{
    return peripheral_component_interconnect_express_allocation(segment_group, bus) != NULL;
}

uint32_t peripheral_component_interconnect_express_window_moves(void)
{
    return peripheral_component_interconnect_express_moves;
}

const PeripheralComponentInterconnectExpressWindowCounts_t *
peripheral_component_interconnect_express_window_counts(void)
{
    return &peripheral_component_interconnect_express_counts;
}

uint8_t peripheral_component_interconnect_find_capability(uint8_t bus, uint8_t device, uint8_t function,
                                                          uint8_t identifier)
{
    const uint16_t status = peripheral_component_interconnect_config_read_word(
        bus, device, function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_STATUS);
    const uint8_t layout = peripheral_component_interconnect_config_read_byte(
                               bus, device, function, PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_HEADER_TYPE) &
                           PERIPHERAL_COMPONENT_INTERCONNECT_HEADER_TYPE_LAYOUT_MASK;
    uint8_t offset = 0u;

    if ((status & PERIPHERAL_COMPONENT_INTERCONNECT_STATUS_CAPABILITIES_LIST) == 0u || layout > 2u)
        return 0u;
    offset = (uint8_t) (peripheral_component_interconnect_config_read_byte(
                            bus, device, function,
                            (layout == 2u) ? PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_CARDBUS_CAPABILITIES_POINTER :
                                             PERIPHERAL_COMPONENT_INTERCONNECT_REGISTER_CAPABILITIES_POINTER) &
                        0xFCu);
    for (uint32_t step = 0u; offset >= 0x40u && step < PERIPHERAL_COMPONENT_INTERCONNECT_MAX_CAPABILITIES; ++step)
    {
        const uint16_t header = peripheral_component_interconnect_config_read_word(bus, device, function, offset);

        if ((header & 0xFFu) == 0xFFu)
            return 0u;
        if ((header & 0xFFu) == identifier)
            return offset;
        offset = (uint8_t) ((header >> 8u) & 0xFCu);
    }
    return 0u;
}

PeripheralComponentInterconnectExpressWalk_t peripheral_component_interconnect_express_walk_extended_capabilities(
    PeripheralComponentInterconnectExpressDwordReader_t read, void *context,
    PeripheralComponentInterconnectExpressExtendedCapability_t *out_capabilities, uint32_t capacity,
    uint32_t *out_count)
{
    uint16_t offset = PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE;
    uint32_t header = 0u;

    *out_count = 0u;
    if (!read(context, offset, &header) || header == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALL_ONES)
        return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NO_SPACE;
    if (header == 0u)
        return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NONE;
    for (;;)
    {
        const uint16_t next = (uint16_t) ((header >> 20u) & 0xFFCu);

        if (*out_count < capacity)
            out_capabilities[*out_count] = (PeripheralComponentInterconnectExpressExtendedCapability_t){
                .offset = offset,
                .identifier = (uint16_t) (header & 0xFFFFu),
                .version = (uint8_t) ((header >> 16u) & 0xFu)};
        ++*out_count;
        if (next == 0u)
            return peripheral_component_interconnect_express_walk_end(*out_count, capacity);
        if (next < PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE)
            return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_MALFORMED;
        if (*out_count == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_MAX_EXTENDED_CAPABILITIES)
            return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_LOOPED;
        offset = next;
        if (!read(context, offset, &header) || header == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_ALL_ONES)
            return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_MALFORMED;
    }
}

PeripheralComponentInterconnectExpressWalk_t peripheral_component_interconnect_express_list_extended_capabilities(
    uint16_t segment_group, uint8_t bus, uint8_t device, uint8_t function,
    PeripheralComponentInterconnectExpressExtendedCapability_t *out_capabilities, uint32_t capacity,
    uint32_t *out_count)
{
    PeripheralComponentInterconnectExpressFunction_t named = {segment_group, bus, device, function};

    if (!peripheral_component_interconnect_express_reaches(segment_group, bus))
    {
        *out_count = 0u;
        return PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NO_SPACE;
    }
    return peripheral_component_interconnect_express_walk_extended_capabilities(
        peripheral_component_interconnect_express_read_function, &named, out_capabilities, capacity, out_count);
}

const char *peripheral_component_interconnect_express_walk_name(PeripheralComponentInterconnectExpressWalk_t walk)
{
    static const char *const names[] = {"listed", "none", "no-space", "malformed", "looped", "truncated"};

    return ((uint32_t) walk < sizeof(names) / sizeof(names[0])) ? names[walk] : "unknown";
}
