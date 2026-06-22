/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** PCI (Peripheral Component Interconnect) configuration space access
*/

#include <kernel/cpu/pci.h>
#include <kernel/lib/asmutils.h>

/*
** Configuration mechanism #1: write a 32-bit address word to CONFIG_ADDRESS
** (0xCF8) then read/write the targeted register through CONFIG_DATA (0xCFC).
**
** Address layout:
**   bit 31     enable
**   bits 23-16 bus
**   bits 15-11 device (0-31)
**   bits 10-8  function (0-7)
**   bits 7-2   register offset (dword aligned)
**   bits 1-0   always zero
*/
static uint32_t peripheral_component_interconnect_build_address(uint8_t bus, uint8_t device, uint8_t function,
                                                                uint8_t offset)
{
    return (uint32_t) ((1u << 31u) | ((uint32_t) bus << 16u) | (((uint32_t) device & 0x1Fu) << 11u) |
                       (((uint32_t) function & 0x07u) << 8u) | ((uint32_t) offset & 0xFCu));
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

static PeripheralComponentInterconnectDevice_t
    peripheral_component_interconnect_devices[PERIPHERAL_COMPONENT_INTERCONNECT_MAX_DEVICES];
static uint32_t peripheral_component_interconnect_device_count = 0u;

static void peripheral_component_interconnect_record_function(uint8_t bus, uint8_t device, uint8_t function)
{
    if (peripheral_component_interconnect_device_count >= PERIPHERAL_COMPONENT_INTERCONNECT_MAX_DEVICES)
        return;

    /* Identity dword (0x00): vendor id | device id << 16. */
    uint32_t identity = peripheral_component_interconnect_config_read_dword(bus, device, function, 0x00u);
    /* Classification dword (0x08): revision | prog-if << 8 | subclass << 16 | class << 24. */
    uint32_t classification = peripheral_component_interconnect_config_read_dword(bus, device, function, 0x08u);
    /* Header dword (0x0C): header type is byte 2. */
    uint32_t header = peripheral_component_interconnect_config_read_dword(bus, device, function, 0x0Cu);

    PeripheralComponentInterconnectDevice_t *entry =
        &peripheral_component_interconnect_devices[peripheral_component_interconnect_device_count];

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

    ++peripheral_component_interconnect_device_count;
}

void peripheral_component_interconnect_scan(void)
{
    peripheral_component_interconnect_device_count = 0u;

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

                peripheral_component_interconnect_record_function(bus8, device, function);
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

    /* Probe the size: write all-ones, read back the writable bits, then restore. */
    peripheral_component_interconnect_config_write_dword(bus, device, function, offset, 0xFFFFFFFFu);
    uint32_t probe = peripheral_component_interconnect_config_read_dword(bus, device, function, offset);
    peripheral_component_interconnect_config_write_dword(bus, device, function, offset, original);

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
        base_high = peripheral_component_interconnect_config_read_dword(bus, device, function,
                                                                       (uint8_t) (offset + 4u));

    out_bar->base = ((uint64_t) base_high << 32u) | (uint64_t) base_low;

    uint32_t size_mask = probe & 0xFFFFFFF0u;
    out_bar->size = size_mask ? (uint64_t) (~size_mask + 1u) : 0u;

    return (uint8_t) (out_bar->size != 0u);
}
