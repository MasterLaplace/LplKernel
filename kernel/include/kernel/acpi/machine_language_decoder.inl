#include <stdbool.h>
#include <stdint.h>

static inline const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *
advanced_configuration_and_power_interface_machine_language_descriptor(uint32_t opcode)
{
    if (opcode >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT)
        opcode = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID;
    return &advanced_configuration_and_power_interface_machine_language_descriptors[opcode];
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t
advanced_configuration_and_power_interface_machine_language_operand_kind(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *descriptor, uint32_t index)
{
    if (index >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OPERANDS)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_NONE;
    return (
        AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t) ((descriptor->operands >> (index * 4u)) &
                                                                             0xFu);
}

static inline bool advanced_configuration_and_power_interface_machine_language_starts_segment(uint8_t byte)
{
    return (byte >= 'A' && byte <= 'Z') || byte == '_';
}

static inline bool advanced_configuration_and_power_interface_machine_language_continues_segment(uint8_t byte)
{
    return advanced_configuration_and_power_interface_machine_language_starts_segment(byte) ||
           (byte >= '0' && byte <= '9');
}

static inline bool advanced_configuration_and_power_interface_machine_language_starts_name(uint8_t byte)
{
    return byte == '\\' || byte == '^' || byte == 0x2Eu || byte == 0x2Fu ||
           advanced_configuration_and_power_interface_machine_language_starts_segment(byte);
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_opcode(const uint8_t *bytes, uint32_t offset,
                                                                        uint32_t limit, uint32_t *out_opcode,
                                                                        uint32_t *out_after)
{
    if (offset >= limit)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;

    uint32_t opcode = advanced_configuration_and_power_interface_machine_language_one_byte_index[bytes[offset]];
    uint32_t after = offset + 1u;

    if (bytes[offset] == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_PREFIX)
    {
        if (after >= limit)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
        opcode = advanced_configuration_and_power_interface_machine_language_extended_index[bytes[after]];
        ++after;
    }
    if (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE;
    *out_opcode = opcode;
    *out_after = after;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_package_length(const uint8_t *bytes, uint32_t offset,
                                                                                uint32_t limit, uint32_t *out_value,
                                                                                uint32_t *out_after)
{
    if (offset >= limit)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;

    const uint32_t follow_bytes = (uint32_t) bytes[offset] >> 6u;

    if (follow_bytes >= limit - offset)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    if (follow_bytes == 0u)
    {
        *out_value = bytes[offset] & 0x3Fu;
        *out_after = offset + 1u;
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    }

    uint32_t value = bytes[offset] & 0x0Fu;

    for (uint32_t index = 0u; index < follow_bytes; ++index)
        value |= (uint32_t) bytes[offset + 1u + index] << (4u + 8u * index);
    *out_value = value;
    *out_after = offset + 1u + follow_bytes;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

static inline uint32_t advanced_configuration_and_power_interface_machine_language_package_length_bytes(uint8_t lead)
{
    return 1u + ((uint32_t) lead >> 6u);
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_package_end(const uint8_t *bytes, uint32_t offset,
                                                                             uint32_t limit, uint32_t *out_end,
                                                                             uint32_t *out_after)
{
    uint32_t value = 0u;
    uint32_t after = 0u;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_read_package_length(bytes, offset, limit, &value,
                                                                                        &after);

    if (status != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return status;
    if (value < after - offset)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    if (value > limit - offset)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_PACKAGE_LENGTH_OUT_OF_PARENT;
    *out_end = offset + value;
    *out_after = after;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

static inline AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_constant(const uint8_t *bytes, uint32_t opcode,
                                                                          uint32_t after, uint32_t limit,
                                                                          uint64_t *out_integer, uint32_t *out_after)
{
    const uint32_t width =
        (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BYTE_PREFIX)        ? 1u :
        (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_WORD_PREFIX)        ? 2u :
        (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_DOUBLE_WORD_PREFIX) ? 4u :
        (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_QUAD_WORD_PREFIX)   ? 8u :
                                                                                                            0u;
    uint64_t integer = (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONE) ? 1u : 0u;

    if (limit - after < width)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    for (uint32_t byte = 0u; byte < width; ++byte)
        integer |= (uint64_t) bytes[after + byte] << (8u * byte);
    *out_integer =
        (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONES) ? ~0ull : integer;
    *out_after = after + width;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}
