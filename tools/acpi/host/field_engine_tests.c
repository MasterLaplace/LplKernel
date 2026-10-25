#include "field_cases.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(host_field_engine, KERNEL_TEST_STAGE_INITIALIZATION);

#define FIELD_ENGINE_MEMORY     0x40000u    /**< Where the cases' region lies in simulated memory. */
#define FIELD_ENGINE_FNV_OFFSET 0x811C9DC5u /**< The 32-bit FNV-1a offset basis. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t field_engine_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t field_engine_load;
static uint8_t field_engine_table[MADE_UP_TABLE_HEADER + 64u];

/**
 * @brief Writes the first bytes of the cases' region in simulated memory.
 */
static void field_engine_fill(void *context, const uint8_t *bytes, uint32_t length)
{
    advanced_configuration_and_power_interface_simulated_host_poke(context, FIELD_ENGINE_MEMORY, bytes, length);
}

/**
 * @brief Reads a byte of the cases' region in simulated memory.
 */
static uint8_t field_engine_byte(void *context, uint32_t offset)
{
    return (uint8_t) advanced_configuration_and_power_interface_simulated_host_peek(context,
                                                                                    FIELD_ENGINE_MEMORY + offset, 1u);
}

/**
 * @brief Every layout of one SystemMemory unit agrees with the bit model: each AccessType from AnyAcc
 *        to QWordAcc, each update rule, every bit offset from 0 to 63 and length from 1 to 64,
 *        61,440 cases read, written and read back (§8.2).
 */
KERNEL_TEST(every_field_layout_agrees_with_the_bit_model)
{
    const FieldCasesMemory_t memory = {&field_engine_host, field_engine_fill, field_engine_byte};
    uint32_t unit = 0u;
    uint32_t fold = FIELD_ENGINE_FNV_OFFSET;
    uint32_t cases = 0u;
    uint32_t disagreements = 0u;

    advanced_configuration_and_power_interface_simulated_host_open(&field_engine_host);
    if (!field_cases_open(test, &field_engine_load, &field_engine_host.host, field_engine_table, FIELD_ENGINE_MEMORY,
                          &unit))
        return;
    for (uint8_t access = 0u; access <= 4u; ++access)
        for (uint8_t update = 0u; update <= 2u; ++update)
            for (uint8_t offset = 0u; offset <= 63u; ++offset)
                for (uint8_t length = 1u; length <= 64u; ++length)
                {
                    const FieldCase_t field = {access, update, offset, length};

                    disagreements += field_cases_run(&field_engine_load, unit, &memory, &field, &fold) ? 0u : 1u;
                    ++cases;
                }
    kernel_test_measure(test, "cases", cases);
    kernel_test_measure(test, "disagreements", disagreements);
    kernel_test_measure(test, "fold", fold);
    kernel_test_check(test, cases == 61440u && disagreements == 0u, "the 61,440 cases agree with the bit model");
    advanced_configuration_and_power_interface_machine_language_close(&field_engine_load);
    advanced_configuration_and_power_interface_simulated_host_close(&field_engine_host);
}
