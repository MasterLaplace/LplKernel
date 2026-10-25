#include "field_cases.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_fields, KERNEL_TEST_STAGE_INITIALIZATION);

#define FIELDS_MEMORY     0x40000u    /**< Where the cases' region lies in simulated memory. */
#define FIELDS_FNV_OFFSET 0x811C9DC5u /**< The 32-bit FNV-1a offset basis. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t fields_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t fields_load;
static uint8_t fields_table[MADE_UP_TABLE_HEADER + 64u];

/**
 * @brief Writes the first bytes of the cases' region in simulated memory.
 */
static void fields_fill(void *context, const uint8_t *bytes, uint32_t length)
{
    advanced_configuration_and_power_interface_simulated_host_poke(context, FIELDS_MEMORY, bytes, length);
}

/**
 * @brief Reads a byte of the cases' region in simulated memory.
 */
static uint8_t fields_byte(void *context, uint32_t offset)
{
    return (uint8_t) advanced_configuration_and_power_interface_simulated_host_peek(context, FIELDS_MEMORY + offset,
                                                                                    1u);
}

/**
 * @brief Ring 0 and the host run the same stratified sample of 432 field layouts through the
 *        simulated host, each against the bit model, and fold what they saw (§8.2).
 */
KERNEL_TEST(a_stratified_sample_of_field_layouts_agrees_with_the_bit_model)
{
    const FieldCasesMemory_t memory = {&fields_host, fields_fill, fields_byte};
    uint32_t unit = 0u;
    uint32_t fold = FIELDS_FNV_OFFSET;
    uint32_t cases = 0u;
    uint32_t disagreements = 0u;
    FieldCase_t field;

    advanced_configuration_and_power_interface_simulated_host_open(&fields_host);
    if (!field_cases_open(test, &fields_load, &fields_host.host, fields_table, FIELDS_MEMORY, &unit))
        return;
    for (; field_cases_sample(cases, &field); ++cases)
        disagreements += field_cases_run(&fields_load, unit, &memory, &field, &fold) ? 0u : 1u;
    kernel_test_measure(test, "cases", cases);
    kernel_test_measure(test, "field_cases_fold", fold);
    kernel_test_check(test, cases == 432u && disagreements == 0u, "the 432 cases agree with the bit model");
    advanced_configuration_and_power_interface_machine_language_close(&fields_load);
    kernel_test_check(test, fields_host.outstanding_maps == 0u, "closing the load unmaps every mapping");
    advanced_configuration_and_power_interface_simulated_host_close(&fields_host);
}
