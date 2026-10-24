#include <kernel/acpi/machine_language_decoder.h>

#define MACHINE_LANGUAGE_KIND(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_##kind

#define MACHINE_LANGUAGE_DESCRIPTOR(identifier, encoding_value, name, package_length, statement, data_object,          \
                                    opens_scope, k0, k1, k2, k3, k4, k5)                                               \
    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##identifier] = {                              \
        .operands = (uint32_t) MACHINE_LANGUAGE_KIND(k0) | ((uint32_t) MACHINE_LANGUAGE_KIND(k1) << 4u) |              \
                    ((uint32_t) MACHINE_LANGUAGE_KIND(k2) << 8u) | ((uint32_t) MACHINE_LANGUAGE_KIND(k3) << 12u) |     \
                    ((uint32_t) MACHINE_LANGUAGE_KIND(k4) << 16u) | ((uint32_t) MACHINE_LANGUAGE_KIND(k5) << 20u),     \
        .encoding = (encoding_value),                                                                                  \
        .operand_count = (uint8_t) ((MACHINE_LANGUAGE_KIND(k0) != MACHINE_LANGUAGE_KIND(NONE)) +                       \
                                    (MACHINE_LANGUAGE_KIND(k1) != MACHINE_LANGUAGE_KIND(NONE)) +                       \
                                    (MACHINE_LANGUAGE_KIND(k2) != MACHINE_LANGUAGE_KIND(NONE)) +                       \
                                    (MACHINE_LANGUAGE_KIND(k3) != MACHINE_LANGUAGE_KIND(NONE)) +                       \
                                    (MACHINE_LANGUAGE_KIND(k4) != MACHINE_LANGUAGE_KIND(NONE)) +                       \
                                    (MACHINE_LANGUAGE_KIND(k5) != MACHINE_LANGUAGE_KIND(NONE))),                       \
        .flags =                                                                                                       \
            (uint8_t) (((package_length) ?                                                                             \
                            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_PACKAGE_LENGTH :          \
                            0u) |                                                                                      \
                       ((statement) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_STATEMENT :     \
                                      0u) |                                                                            \
                       ((data_object) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT : \
                                        0u) |                                                                          \
                       ((opens_scope) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_OPENS_SCOPE : \
                                        0u)),                                                                          \
    },

#define MACHINE_LANGUAGE_EXTENDED_DESCRIPTOR(identifier, encoding_value, ...)                                          \
    MACHINE_LANGUAGE_DESCRIPTOR(identifier, 0x5B00u | (encoding_value), __VA_ARGS__)

#define MACHINE_LANGUAGE_NAME(identifier, encoding_value, name, ...)                                                   \
    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##identifier] = name,

#define MACHINE_LANGUAGE_INDEX(identifier, encoding_value, ...)                                                        \
    [(encoding_value)] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##identifier,

static const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t
    machine_language_descriptors[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT] = {
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_DESCRIPTOR)
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(
                MACHINE_LANGUAGE_EXTENDED_DESCRIPTOR)};

static const char
    *const machine_language_names[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT] = {
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID] = "Invalid",
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_NAME)
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(MACHINE_LANGUAGE_NAME)};

static const uint8_t machine_language_one_byte_index[256] = {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_INDEX)};

static const uint8_t machine_language_extended_index[256] = {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(MACHINE_LANGUAGE_INDEX)};

static const char
    *const machine_language_status_names[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT] = {
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK] = "ok",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED] = "truncated",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_PACKAGE_LENGTH_OUT_OF_PARENT] =
            "pkglength_out_of_parent",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE] = "invalid_opcode",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND] = "name_not_found",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY] = "out_of_memory",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_NAMESPACE_NODES] =
            "limit_namespace_nodes",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BLOCKS] = "limit_blocks",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PENDING_OPERATORS] =
            "limit_pending_operators",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TEMPORARY_NODES] =
            "limit_temporary_nodes",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FRAMES] = "limit_frames",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_PACKAGE_ELEMENTS] =
            "limit_package_elements",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES] = "limit_buffer_bytes",
};

/**
 * @brief Whether @p byte may start a NameSeg: `A` to `Z` or `_`.
 */
static bool machine_language_is_segment_lead(uint8_t byte) { return (byte >= 'A' && byte <= 'Z') || byte == '_'; }

/**
 * @brief Whether @p byte may follow the first character of a NameSeg: `A` to `Z`, `0` to `9` or `_`.
 */
static bool machine_language_is_segment_character(uint8_t byte)
{
    return machine_language_is_segment_lead(byte) || (byte >= '0' && byte <= '9');
}

const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *
advanced_configuration_and_power_interface_machine_language_descriptor(uint32_t opcode)
{
    if (opcode >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT)
        opcode = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID;
    return &machine_language_descriptors[opcode];
}

const char *advanced_configuration_and_power_interface_machine_language_opcode_name(uint32_t opcode)
{
    if (opcode >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT)
        opcode = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID;
    return machine_language_names[opcode];
}

const char *advanced_configuration_and_power_interface_machine_language_status_name(uint32_t status)
{
    if (status >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT)
        return "unknown";
    return machine_language_status_names[status];
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t
advanced_configuration_and_power_interface_machine_language_operand_kind(
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t *descriptor, uint32_t index)
{
    if (index >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_OPERANDS)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPERAND_NONE;
    return (
        AdvancedConfigurationAndPowerInterfaceMachineLanguageOperandKind_t) ((descriptor->operands >> (index * 4u)) &
                                                                             0xFu);
}

bool advanced_configuration_and_power_interface_machine_language_starts_name(uint8_t byte)
{
    return byte == '\\' || byte == '^' || byte == 0x2Eu || byte == 0x2Fu || machine_language_is_segment_lead(byte);
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_opcode(const uint8_t *bytes, uint32_t offset,
                                                                        uint32_t limit, uint32_t *out_opcode,
                                                                        uint32_t *out_after)
{
    if (offset >= limit)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;

    uint32_t opcode = machine_language_one_byte_index[bytes[offset]];
    uint32_t after = offset + 1u;

    if (bytes[offset] == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_PREFIX)
    {
        if (after >= limit)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
        opcode = machine_language_extended_index[bytes[after]];
        ++after;
    }
    if (opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE;
    *out_opcode = opcode;
    *out_after = after;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
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

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
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

AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
advanced_configuration_and_power_interface_machine_language_read_name_string(
    const uint8_t *bytes, uint32_t offset, uint32_t limit,
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t *out_name, uint32_t *out_after)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name = {0};

    if (offset < limit && bytes[offset] == '\\')
    {
        name.root = true;
        ++offset;
    }
    else
    {
        while (offset < limit && bytes[offset] == '^' && name.parent_prefixes < UINT8_MAX)
        {
            ++name.parent_prefixes;
            ++offset;
        }
    }
    if (offset >= limit)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;

    uint32_t segment_count = 1u;

    if (bytes[offset] == 0x00u)
        segment_count = 0u;
    else if (bytes[offset] == 0x2Eu)
        segment_count = 2u;
    else if (bytes[offset] == 0x2Fu)
    {
        if (limit - offset < 2u || bytes[offset + 1u] == 0u)
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
        segment_count = bytes[offset + 1u];
        ++offset;
    }
    if (segment_count != 1u)
        ++offset;
    if ((limit - offset) / 4u < segment_count)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    for (uint32_t segment = 0u; segment < segment_count; ++segment)
    {
        const uint8_t *characters = &bytes[offset + segment * 4u];

        if (!machine_language_is_segment_lead(characters[0]) || !machine_language_is_segment_character(characters[1]) ||
            !machine_language_is_segment_character(characters[2]) ||
            !machine_language_is_segment_character(characters[3]))
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    }
    name.segments = offset;
    name.segment_count = (uint8_t) segment_count;
    *out_name = name;
    *out_after = offset + segment_count * 4u;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}

uint32_t advanced_configuration_and_power_interface_machine_language_name_segment(const uint8_t *bytes, uint32_t offset)
{
    return (uint32_t) bytes[offset] | ((uint32_t) bytes[offset + 1u] << 8u) | ((uint32_t) bytes[offset + 2u] << 16u) |
           ((uint32_t) bytes[offset + 3u] << 24u);
}
