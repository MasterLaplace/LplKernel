#include "recorded_machine.h"

#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdio.h>
#include <string.h>

#define HARNESS_USAGE                                                                                                  \
    "Usage: test-acpi                    runs the kernel's ACPI tests, reported in KTAP\n"                             \
    "       test-acpi --machine <dump>   walks the tables of an acpidump text file and prints its directory\n"

/**
 * @brief Prints the directory of @p directory: its counts and the FACS, then one line per table.
 */
static void harness_print_directory(const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    printf("tables=%u unreachable=%u facs=0x%08X\n", directory->table_count, directory->unreachable_count,
           directory->firmware_control_physical_address);
    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceTable_t *table = &directory->tables[index];

        printf("%.4s length=%u revision=%u checksum=%s\n", table->signature, table->length, table->revision,
               table->checksum_valid ? "ok" : "bad");
    }
}

/**
 * @brief Walks the tables of the dump at @p path as the kernel walks a live machine's, and prints
 *        the directory.
 *
 * @return 0 when the directory was read and every window it mapped was unmapped, 1 otherwise.
 */
static int harness_walk_machine(const char *path)
{
    static RecordedMachine_t machine;
    static AdvancedConfigurationAndPowerInterfaceDirectory_t directory;
    const char *error = NULL;
    int status = 1;

    if (!recorded_machine_open(&machine, path, &error))
        fprintf(stderr, "test-acpi: %s: %s\n", path, error);
    else if (!advanced_configuration_and_power_interface_read_directory(&machine.simulated.host, &directory))
        fprintf(stderr, "test-acpi: %s: no directory could be read\n", path);
    else
    {
        harness_print_directory(&directory);
        advanced_configuration_and_power_interface_release_directory(&directory);
        status = (machine.simulated.outstanding_maps == 0u) ? 0 : 1;
        if (status != 0)
            fprintf(stderr, "test-acpi: %s: %u windows left mapped\n", path, machine.simulated.outstanding_maps);
    }
    recorded_machine_close(&machine);
    return status;
}

/**
 * @brief Runs every stage of the kernel's tests linked in, which are the ACPI ones.
 *
 * @return 0 when none failed, 1 otherwise.
 */
static int harness_run_tests(void)
{
    Serial_t standard_output = {0};

    kernel_test_run_stage(KERNEL_TEST_STAGE_INITIALIZATION, &standard_output);
    kernel_test_run_stage(KERNEL_TEST_STAGE_BOOTED, &standard_output);
    kernel_test_run_stage(KERNEL_TEST_STAGE_PUBLISHED, &standard_output);
    return (kernel_test_failure_count() == 0u) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 1)
        return harness_run_tests();
    if (argc == 3 && strcmp(argv[1], "--machine") == 0)
        return harness_walk_machine(argv[2]);
    fputs(HARNESS_USAGE, (argc == 2 && strcmp(argv[1], "--help") == 0) ? stdout : stderr);
    return (argc == 2 && strcmp(argv[1], "--help") == 0) ? 0 : 2;
}
