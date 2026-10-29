#include <kernel/acpi/resources.h>

#define RESOURCES_LARGE                 0x80u /**< Tag bit 7: a large descriptor, its length in the next two bytes. */
#define RESOURCES_LARGE_HEADER          3u    /**< Bytes of a large descriptor's tag and length. */
#define RESOURCES_SMALL_LENGTH          0x07u /**< Tag bits 0 to 2: a small descriptor's length. */
#define RESOURCES_SMALL_NAME_SHIFT      3u    /**< Where a small descriptor's name starts in its tag. */
#define RESOURCES_SMALL_NAME            0x0Fu /**< A small descriptor's name, once shifted. */
#define RESOURCES_GENERIC_REGISTER_DATA 12u   /**< Bytes of a Generic Register after its header. */

/**
 * @brief The little-endian integer of @p count bytes at @p bytes.
 */
static uint64_t resources_little_endian(const uint8_t *bytes, uint32_t count)
{
    uint64_t value = 0u;

    for (uint32_t index = count; index != 0u; --index)
        value = (value << 8u) | bytes[index - 1u];
    return value;
}

void advanced_configuration_and_power_interface_resources_begin(
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t *iterator, const uint8_t *bytes, uint32_t length)
{
    iterator->bytes = bytes;
    iterator->length = length;
    iterator->offset = 0u;
}

AdvancedConfigurationAndPowerInterfaceResourceStep_t advanced_configuration_and_power_interface_resources_next(
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t *iterator,
    AdvancedConfigurationAndPowerInterfaceResource_t *out_resource)
{
    const uint32_t left = iterator->length - iterator->offset;
    const uint8_t *tag = iterator->bytes + iterator->offset;
    uint32_t header = 1u;
    uint32_t length = 0u;
    uint8_t name = 0u;

    if (left == 0u)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END;
    if ((*tag & RESOURCES_LARGE) != 0u)
    {
        if (left < RESOURCES_LARGE_HEADER)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_MALFORMED;
        header = RESOURCES_LARGE_HEADER;
        length = (uint32_t) resources_little_endian(tag + 1u, 2u);
        name = *tag;
    }
    else
    {
        length = *tag & RESOURCES_SMALL_LENGTH;
        name = (uint8_t) ((*tag >> RESOURCES_SMALL_NAME_SHIFT) & RESOURCES_SMALL_NAME);
        if (name == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END_TAG)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END;
    }
    if (length > left - header)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_MALFORMED;
    out_resource->data = tag + header;
    out_resource->length = (uint16_t) length;
    out_resource->name = name;
    iterator->offset += header + length;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR;
}

bool advanced_configuration_and_power_interface_resources_generic_register(
    const AdvancedConfigurationAndPowerInterfaceResource_t *resource,
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t *out_register)
{
    if (resource->name != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_GENERIC_REGISTER ||
        resource->length < RESOURCES_GENERIC_REGISTER_DATA)
        return false;
    out_register->space = resource->data[0];
    out_register->bit_width = resource->data[1];
    out_register->bit_offset = resource->data[2];
    out_register->access_size = resource->data[3];
    out_register->address = resources_little_endian(resource->data + 4u, 8u);
    return true;
}
