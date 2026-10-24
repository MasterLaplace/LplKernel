#include "recorded_machine.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdio.h>
#include <string.h>

#define HARNESS_USAGE                                                                                                  \
    "Usage: test-acpi                    runs the kernel's ACPI tests, reported in KTAP\n"                             \
    "       test-acpi --machine <dump>   walks the tables of an acpidump text file and prints its directory, its\n"    \
    "                                    MADT, then what loading its definition blocks decoded\n"

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
 * @brief Prints the MADT the directory gives as the kernel reads it: its counters, then one line per
 *        CPU slot, I/O APIC, interrupt source override and NMI connection, in the table's order.
 */
static void harness_print_madt(const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    static AdvancedConfigurationAndPowerInterfaceMadtState_t madt;

    if (!advanced_configuration_and_power_interface_read_madt(
            advanced_configuration_and_power_interface_find_in_directory(directory, "APIC"), &madt))
    {
        printf("madt read=0 state=%s\n", madt.state_name);
        return;
    }
    printf("madt read=1 checksum=%s local_apic=0x%08X processors=%u processors_dropped=%u online_capable=%u "
           "disabled=%u invalid=%u nmis=%u nmis_dropped=%u skipped=%u io_apics=%u overrides=%u\n",
           madt.checksum_valid ? "ok" : "bad", madt.local_apic_physical_base, madt.processor_count,
           madt.processors_dropped, madt.online_capable_count, madt.disabled_count, madt.invalid_count,
           madt.non_maskable_interrupt_count, madt.non_maskable_interrupts_dropped, madt.skipped_count,
           madt.io_apic_count, madt.interrupt_source_override_count);
    for (uint32_t slot = 0u; slot < madt.processor_count; ++slot)
        printf("processor %u uid=0x%X apic_id=0x%X\n", slot, madt.processors[slot].processor_unique_identifier,
               madt.processors[slot].apic_id);
    for (uint32_t index = 0u; index < madt.io_apic_count; ++index)
        printf("io_apic id=%u address=0x%08X gsi_base=%u\n", madt.io_apics[index].id,
               madt.io_apics[index].physical_base, madt.io_apics[index].gsi_base);
    for (uint32_t index = 0u; index < madt.interrupt_source_override_count; ++index)
        printf("override bus=%u source=%u gsi=%u flags=0x%04X\n", madt.interrupt_source_overrides[index].bus,
               madt.interrupt_source_overrides[index].source_irq,
               madt.interrupt_source_overrides[index].global_system_interrupt,
               madt.interrupt_source_overrides[index].flags);
    for (uint32_t index = 0u; index < madt.non_maskable_interrupt_count; ++index)
        printf("nmi uid=0x%X flags=0x%04X lint=%u\n", madt.non_maskable_interrupts[index].processor_unique_identifier,
               madt.non_maskable_interrupts[index].flags, madt.non_maskable_interrupts[index].local_interrupt);
}

/**
 * @brief Loads the DSDT and the SSDTs of @p directory with the server profile's caps, and prints
 *        what the load counted: its tables and faults, then each opcode decoded, in descriptor order.
 */
static void harness_print_machine_language(const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;

    if (!advanced_configuration_and_power_interface_machine_language_open(
            &load, directory->host, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES,
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES))
    {
        printf("aml read=0 status=%s\n",
               advanced_configuration_and_power_interface_machine_language_status_name(load.status));
        advanced_configuration_and_power_interface_machine_language_close(&load);
        return;
    }
    advanced_configuration_and_power_interface_machine_language_load_directory(&load, directory);
    printf("aml tables=%u bytes=%u decode_errors=%u\n", load.table_count, load.bytes, load.decode_errors);
    printf("aml faults load_errors=%u unresolved_names=%u\n", load.load_errors, load.unresolved_names);
    for (uint32_t index = 0u; index < load.table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load.tables[index];

        if (table->decode_errors != 0u)
            printf("aml error %.4s/%.8s offset=0x%X status=%s opcode=%s faults=%u\n", table->header->signature,
                   table->header->oem_table_id, table->first_error_offset,
                   advanced_configuration_and_power_interface_machine_language_status_name(table->first_error),
                   advanced_configuration_and_power_interface_machine_language_opcode_name(table->first_error_opcode),
                   table->decode_errors);
    }
    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        if (load.opcodes[opcode] != 0u)
            printf("aml opcode %s %u\n",
                   advanced_configuration_and_power_interface_machine_language_opcode_name(opcode),
                   load.opcodes[opcode]);
    advanced_configuration_and_power_interface_machine_language_close(&load);
}

/**
 * @brief Walks the tables of the dump at @p path as the kernel walks a live machine's, and prints
 *        the directory, its MADT, then what loading its definition blocks decoded.
 *
 * @return 0 when the directory was read and every window it mapped was unmapped by its address and
 *         length, 1 otherwise.
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
        harness_print_madt(&directory);
        harness_print_machine_language(&directory);
        advanced_configuration_and_power_interface_release_directory(&directory);
        status = (machine.simulated.outstanding_maps == 0u && machine.simulated.mismatched_unmaps == 0u) ? 0 : 1;
        if (status != 0)
            fprintf(stderr, "test-acpi: %s: %u windows left mapped, %u unmaps that named no window\n", path,
                    machine.simulated.outstanding_maps, machine.simulated.mismatched_unmaps);
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
