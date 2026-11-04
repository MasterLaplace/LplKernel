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

#define MACHINE_LANGUAGE_LEAD_NAME_CHARACTER                                                                           \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_LEAD |                                        \
     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER) /**< A LeadNameChar. */

#define MACHINE_LANGUAGE_EXTENDED_DESCRIPTOR(identifier, encoding_value, ...)                                          \
    MACHINE_LANGUAGE_DESCRIPTOR(identifier, 0x5B00u | (encoding_value), __VA_ARGS__)

#define MACHINE_LANGUAGE_NAME(identifier, encoding_value, name, ...)                                                   \
    [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##identifier] = name,

#define MACHINE_LANGUAGE_INDEX(identifier, encoding_value, ...)                                                        \
    [(encoding_value)] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_##identifier,

const uint8_t advanced_configuration_and_power_interface_machine_language_segment_characters[256] = {
    ['A'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['B'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['C'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['D'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['E'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['F'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['G'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['H'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['I'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['J'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['K'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['L'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['M'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['N'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['O'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['P'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['Q'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['R'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['S'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['T'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['U'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['V'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['W'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['X'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['Y'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['Z'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['_'] = MACHINE_LANGUAGE_LEAD_NAME_CHARACTER,
    ['0'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['1'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['2'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['3'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['4'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['5'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['6'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['7'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['8'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
    ['9'] = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SEGMENT_CHARACTER,
};

const AdvancedConfigurationAndPowerInterfaceMachineLanguageDescriptor_t
    advanced_configuration_and_power_interface_machine_language_descriptors
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT] = {
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_DESCRIPTOR)
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(
                    MACHINE_LANGUAGE_EXTENDED_DESCRIPTOR)};

static const char
    *const machine_language_names[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT] = {
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INVALID] = "Invalid",
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_NAME)
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_EXTENDED_OPCODES(MACHINE_LANGUAGE_NAME)};

const uint8_t advanced_configuration_and_power_interface_machine_language_one_byte_index[256] = {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ONE_BYTE_OPCODES(MACHINE_LANGUAGE_INDEX)};

const uint8_t advanced_configuration_and_power_interface_machine_language_extended_index[256] = {
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
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE] = "index_out_of_range",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_OBJECT_RUNS] = "limit_object_runs",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_VALUES] = "limit_values",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_LOOP_ITERATIONS] =
            "limit_loop_iterations",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_UNIMPLEMENTED_OPERATOR] =
            "unimplemented_operator",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_FIELD_ACCESS_DEFERRED] =
            "field_access_deferred",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH] = "type_mismatch",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_DIVIDE_BY_ZERO] = "divide_by_zero",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_EXISTS] = "name_exists",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_SPACE_UNSUPPORTED] = "space_unsupported",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_REGION_BOUNDS] = "region_bounds",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_ADDRESS_UNREACHABLE] =
            "address_unreachable",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_GLOBAL_LOCK_UNAVAILABLE] =
            "global_lock_unavailable",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_REGION_UNEVALUATED] = "region_unevaluated",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_ACCESS_WIDTH_INVALID] =
            "access_width_invalid",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FIELD_ACCESSES] =
            "limit_field_accesses",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_REFERENCE_CYCLE] = "reference_cycle",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_RESOLUTION_LOOP] =
            "name_resolution_loop",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TIME_REQUESTED] =
            "limit_time_requested",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_HELD_MUTEXES] = "limit_held_mutexes",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_MUTEX_ORDER] = "mutex_order",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LOAD_FORM_UNSUPPORTED] =
            "load_form_unsupported",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TABLES] = "limit_tables",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_TABLE_BYTES] = "limit_table_bytes",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_METHOD_WITHOUT_BODY] =
            "method_without_body",
        [ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OPERANDS_MISSING] = "operands_missing",
};

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

        if (!advanced_configuration_and_power_interface_machine_language_starts_segment(characters[0]) ||
            !advanced_configuration_and_power_interface_machine_language_continues_segment(characters[1]) ||
            !advanced_configuration_and_power_interface_machine_language_continues_segment(characters[2]) ||
            !advanced_configuration_and_power_interface_machine_language_continues_segment(characters[3]))
            return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    }
    name.segments = offset;
    name.segment_count = (uint8_t) segment_count;
    *out_name = name;
    *out_after = offset + segment_count * 4u;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
}
