#include "field_cases.h"

#include "made_up_aml.h"

#include <stddef.h>

#define FIELD_CASES_FNV_PRIME 0x01000193u /**< The 32-bit FNV-1a prime. */

static const uint8_t field_cases_offsets[] = {0u, 1u, 7u, 15u, 31u, 63u};
static const uint8_t field_cases_lengths[] = {1u, 8u, 9u, 32u, 33u, 64u};

/**
 * @brief The byte of the fixed pattern the memory starts each case with.
 */
static uint8_t field_cases_pattern(uint32_t index) { return (uint8_t) (0x5Au ^ (index * 0x47u)); }

/**
 * @brief Bit @p bit of @p bytes, little-endian.
 */
static uint32_t field_cases_bit(const uint8_t *bytes, uint32_t bit) { return (bytes[bit / 8u] >> (bit % 8u)) & 1u; }

/**
 * @brief Sets bit @p bit of @p bytes to @p value.
 */
static void field_cases_set_bit(uint8_t *bytes, uint32_t bit, uint32_t value)
{
    bytes[bit / 8u] = (uint8_t) ((bytes[bit / 8u] & ~(1u << (bit % 8u))) | ((value & 1u) << (bit % 8u)));
}

/**
 * @brief The bytes of one datum of an AccessType, as the specification gives them: AnyAcc reads bytes.
 */
static uint32_t field_cases_datum_bytes(uint8_t access) { return (access <= 1u) ? 1u : (1u << (access - 1u)); }

/**
 * @brief Folds one byte into an FNV-1a fold.
 */
static uint32_t field_cases_fold_byte(uint32_t fold, uint8_t byte) { return (fold ^ byte) * FIELD_CASES_FNV_PRIME; }

/**
 * @brief Folds the eight bytes of @p value, little-endian.
 */
static uint32_t field_cases_fold_value(uint32_t fold, uint64_t value)
{
    for (uint32_t index = 0u; index < 8u; ++index)
        fold = field_cases_fold_byte(fold, (uint8_t) (value >> (8u * index)));
    return fold;
}

/**
 * @brief Writes what the model says the memory holds once @p value is written: the unit's bits from
 *        the value, the other bits of the datums it touches by the update rule, the rest untouched.
 */
static void field_cases_model_write(const FieldCase_t *field, uint64_t value, uint8_t *bytes)
{
    const uint32_t datum_bits = 8u * field_cases_datum_bytes(field->access);
    const uint32_t first = (field->bit_offset / datum_bits) * datum_bits;
    const uint32_t end = field->bit_offset + field->bit_length;
    const uint32_t last = ((end + datum_bits - 1u) / datum_bits) * datum_bits;

    for (uint32_t bit = first; bit < last; ++bit)
    {
        if (bit >= field->bit_offset && bit < end)
            field_cases_set_bit(bytes, bit, (uint32_t) (value >> (bit - field->bit_offset)));
        else if (field->update == 1u)
            field_cases_set_bit(bytes, bit, 1u);
        else if (field->update == 2u)
            field_cases_set_bit(bytes, bit, 0u);
    }
}

/**
 * @brief The unit's bits in @p bytes, as the model reads them.
 */
static uint64_t field_cases_model_read(const FieldCase_t *field, const uint8_t *bytes)
{
    uint64_t value = 0u;

    for (uint32_t bit = 0u; bit < field->bit_length; ++bit)
        value |= (uint64_t) field_cases_bit(bytes, field->bit_offset + bit) << bit;
    return value;
}

bool field_cases_open(KernelTest_t *test, AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                      const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint8_t *table,
                      uint64_t physical_address, uint32_t *out_unit)
{
    static const MadeUpFieldUnit_t unit[] = {
        {"FX__", 8u}
    };
    static MadeUpAml_t aml;

    made_up_aml_begin(&aml);
    made_up_aml_region(&aml, "FCSR", 0x00u, physical_address, FIELD_CASES_BYTES);
    made_up_aml_field(&aml, "FCSR", 0x01u, unit, 1u);
    if (!made_up_load_on(test, load, host, table, 2u, aml.bytes, aml.length))
        return false;
    *out_unit = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &load->space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "FX__", 0u));
    return kernel_test_check(test, made_up_root_node(load, "FX__") != NULL, "the cases' unit is built");
}

bool field_cases_run(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, uint32_t unit,
                     const FieldCasesMemory_t *memory, const FieldCase_t *field, uint32_t *fold)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, unit);
    AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *layout =
        (AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t
             *) (advanced_configuration_and_power_interface_machine_language_object(&load->heap, node->value.handle) +
                 1);
    const uint64_t mask = (field->bit_length >= 64u) ? ~0ull : ((1ull << field->bit_length) - 1u);
    const uint64_t written = 0xF0E1D2C3B4A59687ull ^ ((uint64_t) field->bit_offset << 40u) ^ field->bit_length;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value = {
        .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
        .payload = written};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t read = {0};
    uint8_t expected[FIELD_CASES_BYTES];
    bool agrees = true;

    for (uint32_t index = 0u; index < FIELD_CASES_BYTES; ++index)
        expected[index] = field_cases_pattern(index);
    memory->fill(memory->context, expected, FIELD_CASES_BYTES);
    layout->access = field->access;
    layout->update = field->update;
    layout->bit_offset = field->bit_offset;
    layout->bit_length = field->bit_length;
    agrees = advanced_configuration_and_power_interface_machine_language_field_read(load, unit, &read) ==
                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
             read.payload == field_cases_model_read(field, expected);
    *fold = field_cases_fold_value(*fold, read.payload);
    agrees = advanced_configuration_and_power_interface_machine_language_field_write(load, unit, &value) ==
                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
             agrees;
    field_cases_model_write(field, written, expected);
    for (uint32_t index = 0u; index < FIELD_CASES_BYTES; ++index)
    {
        const uint8_t byte = memory->byte(memory->context, index);

        agrees = agrees && byte == expected[index];
        *fold = field_cases_fold_byte(*fold, byte);
    }
    agrees = advanced_configuration_and_power_interface_machine_language_field_read(load, unit, &read) ==
                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
             read.payload == (written & mask) && agrees;
    *fold = field_cases_fold_value(*fold, read.payload);
    return agrees;
}

bool field_cases_sample(uint32_t index, FieldCase_t *out_case)
{
    const uint32_t offsets = sizeof(field_cases_offsets);
    const uint32_t lengths = sizeof(field_cases_lengths);

    if (index >= 4u * 3u * offsets * lengths)
        return false;
    out_case->bit_length = field_cases_lengths[index % lengths];
    out_case->bit_offset = field_cases_offsets[(index / lengths) % offsets];
    out_case->update = (uint8_t) ((index / (lengths * offsets)) % 3u);
    out_case->access = (uint8_t) (1u + index / (lengths * offsets * 3u));
    return true;
}
