#include "../../../../tests/acpi/field_cases.h"
#include "../../../../tests/acpi/simulated_host.h"

#include <kernel/acpi/host.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_machine_regions, KERNEL_TEST_STAGE_INITIALIZATION);

#define MACHINE_REGIONS_SIMULATED 0x40000u    /**< Where the simulated run places the cases' region. */
#define MACHINE_REGIONS_FNV       0x811C9DC5u /**< The 32-bit FNV-1a offset basis. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t machine_regions_simulated;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t machine_regions_load;
static uint8_t machine_regions_table[MADE_UP_TABLE_HEADER + 64u];

/**
 * @brief Writes the first bytes of the owned frame, through the test's own window on it.
 */
static void machine_regions_fill(void *context, const uint8_t *bytes, uint32_t length)
{
    memcpy(context, bytes, length);
}

/**
 * @brief Reads a byte of the owned frame, through the test's own window on it.
 */
static uint8_t machine_regions_byte(void *context, uint32_t offset)
{
    return ((const volatile uint8_t *) context)[offset];
}

/**
 * @brief Writes the first bytes of the cases' region in simulated memory.
 */
static void machine_regions_simulated_fill(void *context, const uint8_t *bytes, uint32_t length)
{
    advanced_configuration_and_power_interface_simulated_host_poke(context, MACHINE_REGIONS_SIMULATED, bytes, length);
}

/**
 * @brief Reads a byte of the cases' region in simulated memory.
 */
static uint8_t machine_regions_simulated_byte(void *context, uint32_t offset)
{
    return (uint8_t) advanced_configuration_and_power_interface_simulated_host_peek(
        context, MACHINE_REGIONS_SIMULATED + offset, 1u);
}

/**
 * @brief Runs the stratified sample on a load of the cases' region, counting the disagreements.
 */
static uint32_t machine_regions_run(uint32_t unit, const FieldCasesMemory_t *memory, uint32_t *fold)
{
    uint32_t disagreements = 0u;
    FieldCase_t field;

    for (uint32_t index = 0u; field_cases_sample(index, &field); ++index)
        disagreements += field_cases_run(&machine_regions_load, unit, memory, &field, fold) ? 0u : 1u;
    return disagreements;
}

/**
 * @brief The stratified sample of field layouts reaches a frame the test owns through the machine's
 *        own host, its windows and its memory map, and folds as it does through the simulated host
 *        (§8.2).
 */
KERNEL_TEST(the_field_sample_reaches_machine_memory_as_it_reaches_simulated_memory)
{
    const uint32_t frame = physical_memory_manager_page_frame_allocate();
    uint8_t *window =
        (frame != 0u) ? (uint8_t *) (uintptr_t) kernel_vmm_map_firmware_writable_window(frame, 4096u) : NULL;
    const FieldCasesMemory_t machine = {window, machine_regions_fill, machine_regions_byte};
    const FieldCasesMemory_t simulated = {&machine_regions_simulated, machine_regions_simulated_fill,
                                          machine_regions_simulated_byte};
    uint32_t machine_fold = MACHINE_REGIONS_FNV;
    uint32_t simulated_fold = MACHINE_REGIONS_FNV;
    uint32_t unit = 0u;

    if (!kernel_test_check(test, window != NULL, "a frame is taken and mapped for the test"))
    {
        if (frame != 0u)
            physical_memory_manager_page_frame_free(frame);
        return;
    }
    if (field_cases_open(test, &machine_regions_load, &advanced_configuration_and_power_interface_machine_host,
                         machine_regions_table, frame, &unit))
    {
        kernel_test_check(test, machine_regions_run(unit, &machine, &machine_fold) == 0u,
                          "the 432 cases agree with the bit model in machine memory");
        kernel_test_check(test,
                          machine_regions_load.regions.mappings[0].kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_CACHED,
                          "the frame, usable memory, is mapped cached");
    }
    advanced_configuration_and_power_interface_machine_language_close(&machine_regions_load);
    advanced_configuration_and_power_interface_simulated_host_open(&machine_regions_simulated);
    if (field_cases_open(test, &machine_regions_load, &machine_regions_simulated.host, machine_regions_table,
                         MACHINE_REGIONS_SIMULATED, &unit))
        machine_regions_run(unit, &simulated, &simulated_fold);
    advanced_configuration_and_power_interface_machine_language_close(&machine_regions_load);
    advanced_configuration_and_power_interface_simulated_host_close(&machine_regions_simulated);
    kernel_test_measure(test, "field_cases_fold", machine_fold);
    kernel_test_check(test, machine_fold == simulated_fold, "machine memory folds as simulated memory does");
    kernel_vmm_unmap_firmware_window((uint32_t) (uintptr_t) window, 4096u);
    physical_memory_manager_page_frame_free(frame);
}
