#include "field_cases.h"
#include "made_up_aml.h"
#include "made_up_table.h"
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
static uint8_t field_engine_wide_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t field_engine_second_table[MADE_UP_TABLE_HEADER + 64u];

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
    kernel_test_check(test, field_engine_host.inconsistent_accesses == 0u,
                      "every access went where its mapping puts it");
    advanced_configuration_and_power_interface_machine_language_close(&field_engine_load);
    advanced_configuration_and_power_interface_simulated_host_close(&field_engine_host);
}

/**
 * @brief How many runs of table-level code failed with limit_field_accesses.
 */
static uint32_t field_engine_limited(void)
{
    return field_engine_load.evaluation
        .statuses[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_FIELD_ACCESSES];
}

/**
 * @brief A table's fields make at most 2^20 accesses to the host, the methods it calls included
 *        (§6.1): a method whose While stores One to a 64 KiB ByteAcc unit, 65,536 byte writes a Store,
 *        fails with limit_field_accesses in its 17th Store; the load goes on to the Name after the
 *        call, and an SSDT that stores to the same unit once starts a budget of its own.
 */
KERNEL_TEST(a_table_s_fields_make_at_most_two_to_the_twenty_accesses)
{
    static const MadeUpFieldUnit_t wide[] = {
        {"WIDE", 0x80000u}
    };
    static const uint8_t loop[] = {0x14u, 0x0Fu, 'L', 'O', 'O', 'P', 0x00u, 0xA2u, 0x08u, 0x01u,
                                   0x70u, 0x01u, 'W', 'I', 'D', 'E', 'L',   'O',   'O',   'P'};
    static const uint8_t second[] = {0x70u, 0x01u, 'W', 'I', 'D', 'E'};
    MadeUpAml_t aml;

    advanced_configuration_and_power_interface_simulated_host_open(&field_engine_host);
    made_up_aml_begin(&aml);
    made_up_aml_region(&aml, "WIDR", 0x00u, FIELD_ENGINE_MEMORY, 0x10000u);
    made_up_aml_field(&aml, "WIDR", 0x01u, wide, 1u);
    made_up_aml_raw(&aml, loop, sizeof(loop));
    made_up_aml_name_integer(&aml, "AFTR", 1u);
    if (!made_up_load_on(test, &field_engine_load, &field_engine_host.host, field_engine_wide_table, 2u, aml.bytes,
                         aml.length))
        return;
    kernel_test_measure(test, "accesses", field_engine_load.evaluation.field_accesses);
    kernel_test_check(test,
                      field_engine_limited() == 1u &&
                          field_engine_load.evaluation.field_accesses ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FIELD_ACCESSES &&
                          field_engine_load.regions.field_accesses_peak ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_FIELD_ACCESSES,
                      "the loop stops at 2^20 accesses, with limit_field_accesses");
    kernel_test_check(test, field_engine_load.regions.field_writes == 17u,
                      "the 17th Store is the one the limit stops: 16 make 2^20 accesses");
    kernel_test_check(test, made_up_root_node(&field_engine_load, "AFTR") != NULL,
                      "the Name after the loop is declared");
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &field_engine_load, made_up_definition_block(field_engine_second_table, "SSDT", 2u, second, sizeof(second)));
    kernel_test_check(test,
                      field_engine_limited() == 1u && field_engine_load.evaluation.field_accesses == 0x10000u &&
                          field_engine_load.regions.field_writes == 18u,
                      "the SSDT's Store makes its 65,536 accesses in a budget of its own");
    kernel_test_check(test, field_engine_host.inconsistent_accesses == 0u,
                      "every access went where its mapping puts it");
    advanced_configuration_and_power_interface_machine_language_close(&field_engine_load);
    advanced_configuration_and_power_interface_simulated_host_close(&field_engine_host);
}
