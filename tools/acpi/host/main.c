#include "needs_path_closure.h"
#include "platform.h"
#include "recorded_machine.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARNESS_PAGE_BYTES 4096u /**< What a bootloader aligns a module to, and the harness too. */
#define HARNESS_PATH_BYTES 256u  /**< Bytes of a node's path the harness prints: fifty-one levels deep. */

#define HARNESS_USAGE                                                                                                  \
    "Usage: test-acpi [--boot-module <image>] [--firmware-log <log>]\n"                                                \
    "                                    runs the kernel's ACPI tests, reported in KTAP; with <image>, the\n"          \
    "                                    Multiboot module of a debug kernel, or <log>, the serial log of a debug\n"    \
    "                                    kernel booted with lpl.acpi.tables=print, they run on the bytes that\n"       \
    "                                    kernel read, and tools/parity.sh compares the two runs\n"                     \
    "       test-acpi --machine <dump>   walks the tables of an acpidump text file and prints its directory, its\n"    \
    "                                    MADT, then what loading its definition blocks decoded\n"                      \
    "       test-acpi --namespace <dump> loads the definition blocks of an acpidump text file and prints its\n"        \
    "                                    namespace, one node per line, as acpiexec's namespace command does\n"         \
    "       test-acpi --namespace-tables <table>...  the same from raw definition blocks, the DSDT first\n"            \
    "       test-acpi --closure <dump>   prints the opcodes the needs path of an acpidump text file reaches\n"         \
    "       test-acpi --closure-tables <table>...    the same from raw definition blocks, the DSDT first\n"            \
    "       test-acpi --initialize <dump> [<presets>]  loads the definition blocks of an acpidump text file,\n"        \
    "                                    initialises their namespace as the boot does, and prints what it ran and\n"   \
    "                                    counted, each status a failure ended with, and the first object that\n"       \
    "                                    failed; the SSDTs the dump's PmRef triples place wait for Load, and\n"        \
    "                                    <presets>, a path and a hexadecimal integer per line, # for a comment,\n"     \
    "                                    gives NVS objects their values\n"                                             \
    "       test-acpi --loaded <dump> <presets> <directory>  the same, then prints the namespace and writes\n"         \
    "                                    each table it holds, Load's included, as a raw file of <directory>\n"         \
    "       test-acpi --initialize-tables <table>...  the same from raw definition blocks, the DSDT first\n"           \
    "       test-acpi --evaluate <path> <table>...    loads raw definition blocks, the DSDT first, initialises\n"      \
    "                                    their namespace, and prints what evaluating the absolute path gives: its\n"   \
    "                                    status, then the result's type and value, and each opcode run but not\n"      \
    "                                    executed\n"                                                                   \
    "       test-acpi --write-module <dump> <image>  writes the firmware an acpidump text file gives, as this\n"       \
    "                                    harness serves it, as an image: the Multiboot module of a debug kernel\n"

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
 * @brief Prints one register of the normalised FADT, named @p name.
 */
static void harness_print_register(const char *name, const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *reg)
{
    printf("fadt %s space=%u width=%u offset=%u access=%u address=0x%llX\n", name, reg->space, reg->bit_width,
           reg->bit_offset, reg->access_size, (unsigned long long) reg->address);
}

/**
 * @brief Prints the FADT the directory gives as the kernel normalises it: its fields, then one line
 *        per register, absent ones included.
 */
static void harness_print_fixed_description(const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt)
{
    if (!fadt->present)
    {
        printf("fadt present=0\n");
        return;
    }
    printf("fadt present=1 length=%u revision=%u hardware_reduced=%u flags=0x%08X sci=%u smi_command=0x%X "
           "enable=0x%02X pstate=0x%02X cst=0x%02X reset_value=0x%02X gpe0_length=%u gpe1_length=%u gpe1_base=%u\n",
           fadt->length, fadt->revision, fadt->hardware_reduced ? 1u : 0u, fadt->flags, fadt->system_control_interrupt,
           fadt->system_management_interrupt_command, fadt->enable_command, fadt->performance_state_command,
           fadt->idle_state_command, fadt->reset_value, fadt->general_purpose_event_block_lengths[0],
           fadt->general_purpose_event_block_lengths[1], fadt->general_purpose_event_1_base);
    harness_print_register("pm1a_event", &fadt->power_management_event_blocks[0]);
    harness_print_register("pm1b_event", &fadt->power_management_event_blocks[1]);
    harness_print_register("pm1a_control", &fadt->power_management_control_blocks[0]);
    harness_print_register("pm1b_control", &fadt->power_management_control_blocks[1]);
    harness_print_register("pm_timer", &fadt->power_management_timer_block);
    harness_print_register("gpe0", &fadt->general_purpose_event_blocks[0]);
    harness_print_register("gpe1", &fadt->general_purpose_event_blocks[1]);
    harness_print_register("reset", &fadt->reset_register);
    harness_print_register("sleep_control", &fadt->sleep_control_register);
    harness_print_register("sleep_status", &fadt->sleep_status_register);
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
           "disabled=%u invalid=%u nmis=%u nmis_dropped=%u skipped=%u too_short=%u io_apics=%u io_apics_dropped=%u "
           "overrides=%u overrides_dropped=%u\n",
           madt.checksum_valid ? "ok" : "bad", madt.local_apic_physical_base, madt.processor_count,
           madt.processors_dropped, madt.online_capable_count, madt.disabled_count, madt.invalid_count,
           madt.non_maskable_interrupt_count, madt.non_maskable_interrupts_dropped, madt.skipped_count,
           madt.too_short_count, madt.io_apic_count, madt.io_apics_dropped, madt.interrupt_source_override_count,
           madt.interrupt_source_overrides_dropped);
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
 * @brief Prints what running table-level code did: its runs and failures, its field accesses, the
 *        operating system services it called, the tables Load and LoadTable added, the failures per
 *        status, the region spaces refused, and the opcodes that failed as unimplemented.
 */
static void harness_print_runs(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t *regions = &load->regions;

    printf("aml run statements=%u failures=%u implicit_returns=%u uninitialized_operands=%u frames_peak=%u "
           "values_peak=%u declared_not_run=%u mixed_type_comparisons=%u forbidden_conversions=%u objects=%u "
           "objects_freed=%u\n",
           evaluation->runs, evaluation->failures, evaluation->implicit_returns, evaluation->uninitialized_operands,
           evaluation->frames_peak, evaluation->values_peak, load->declared_not_run, evaluation->mixed_type_comparisons,
           evaluation->forbidden_conversions, load->heap.objects, load->heap.objects_freed);
    printf("aml run regions field_reads=%u field_writes=%u map_calls=%u mapping_cache_hits=%u "
           "split_qword_accesses=%u unaligned_accesses=%u protected_port_accesses=%u region_lengths_rounded=%u "
           "field_accesses_peak=%u\n",
           regions->field_reads, regions->field_writes, regions->map_calls, regions->mapping_cache_hits,
           regions->split_qword_accesses, regions->unaligned_accesses, regions->protected_port_accesses,
           regions->region_lengths_rounded, regions->field_accesses_peak);
    printf("aml run global_lock acquisitions=%u waits=%u releases_signalled=%u timeouts=%u unlocked_accesses=%u "
           "facs_refused=%u\n",
           regions->global_lock_acquisitions, regions->global_lock_waits, regions->global_lock_releases_signalled,
           regions->global_lock_timeouts, regions->unlocked_lock_accesses, regions->facs_refused ? 1u : 0u);
    printf("aml run services windows=%u osi_queries=%u osi_denied=%u sleeps=%u sleep_ms=%llu stalls=%u "
           "stall_us=%llu timer_reads=%u acquisitions=%u acquisitions_ignored=%u releases_ignored=%u "
           "released_at_end=%u held_peak=%u time_requested_peak_us=%llu signals=%u notifications=%u "
           "notifications_dropped=%u notification_peak=%u\n",
           load->services.windows_version, load->services.operating_system_queries,
           load->services.operating_system_queries_denied, load->services.sleeps,
           (unsigned long long) load->services.sleep_milliseconds, load->services.stalls,
           (unsigned long long) load->services.stall_microseconds, load->services.timer_reads,
           load->services.acquisitions, load->services.acquisitions_ignored, load->services.releases_ignored,
           load->services.released_at_end, load->services.held_peak,
           (unsigned long long) load->services.time_requested_peak, load->services.signals,
           load->services.notifications, load->services.notifications_dropped, load->services.notification_peak);
    printf("aml run tables loaded=%u duplicates=%u missing=%u bytes_copied=%u\n", load->tables_loaded,
           load->load_duplicates, load->load_tables_missing, load->table_bytes_copied);
    printf("aml run pci attachments=%u internal_evaluations=%u internal_failures=%u protected_writes=%u "
           "lost_accesses=%u ecam_accesses=%u registrations=%u registration_failures=%u\n",
           regions->pci_attachments, regions->internal_evaluations, regions->internal_failures,
           regions->pci_protected_writes, regions->pci_lost_accesses, regions->pci_ecam_accesses,
           regions->pci_registrations, regions->pci_registration_failures);
    for (uint32_t space = 0u; space < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES; ++space)
        if (regions->space_unsupported[space] != 0u)
            printf("aml run space 0x%02X=%u\n", space, regions->space_unsupported[space]);
    for (uint32_t status = 1u; status < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT;
         ++status)
        if (evaluation->statuses[status] != 0u)
            printf("aml run status %s=%u\n",
                   advanced_configuration_and_power_interface_machine_language_status_name(status),
                   evaluation->statuses[status]);
    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        if (evaluation->unimplemented[opcode] != 0u)
            printf("aml run unimplemented %s=%u\n",
                   advanced_configuration_and_power_interface_machine_language_opcode_name(opcode),
                   evaluation->unimplemented[opcode]);
}

/**
 * @brief Loads the DSDT and the SSDTs of @p directory into @p load, then, statically, the SSDTs
 *        @p machine serves for Load, as acpiexec and the census read every table of a dump; then decodes
 *        them all.
 */
static void harness_load_every_table(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                     const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory,
                                     const RecordedMachine_t *machine)
{
    advanced_configuration_and_power_interface_machine_language_declare_directory(load, directory);
    for (uint32_t index = 0u; index < machine->table_count; ++index)
        if (machine->tables[index].placed != 0u)
            (void) advanced_configuration_and_power_interface_machine_language_declare_table(
                load, (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) machine->tables[index].bytes);
    advanced_configuration_and_power_interface_machine_language_decode_tables(load);
}

/**
 * @brief Loads every table of @p machine, as harness_load_every_table does, with the server profile's
 *        caps, and prints what the load counted: its tables and faults, then each opcode decoded, in
 *        descriptor order.
 */
static void harness_print_machine_language(const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory,
                                           const RecordedMachine_t *machine)
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
    load.declares_every_branch = true;
    harness_load_every_table(&load, directory, machine);
    printf("aml tables=%u bytes=%u decode_errors=%u calls=%u\n", load.table_count, load.bytes, load.decode_errors,
           load.method_calls);
    printf("aml faults load_errors=%u unresolved_names=%u undeclared_faults=%u\n", load.load_errors,
           load.unresolved_names, load.undeclared_faults);
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
 *        the directory, its FADT, its MADT, then what loading its definition blocks decoded.
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
        harness_print_fixed_description(&directory.fixed_description);
        harness_print_madt(&directory);
        harness_print_machine_language(&directory, &machine);
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
 * @brief Prints a string as ACPICA's AcpiUtPrintString does, up to 80 characters, so that the two
 *        dumps compare line by line.
 */
static void harness_print_string(const char *text, uint32_t length)
{
    static const char escapes[] = {
        [0x07] = 'a', [0x08] = 'b', [0x09] = 't', [0x0A] = 'n', [0x0B] = 'v', [0x0C] = 'f', [0x0D] = 'r'};
    uint32_t index = 0u;

    putchar('"');
    for (; index < 80u && index < length && text[index] != '\0'; ++index)
    {
        const unsigned char character = (unsigned char) text[index];

        if (character < sizeof(escapes) && escapes[character] != '\0')
            printf("\\%c", escapes[character]);
        else if (character == '\'' || character == '"' || character == '\\')
            printf("\\%c", character);
        else if (character >= 0x20u && character < 0x7Fu)
            putchar(character);
        else
            printf("\\x%2.2X", (unsigned int) (int32_t) (int8_t) character);
    }
    putchar('"');
    if (index == 80u && index < length && text[index] != '\0')
        fputs("...", stdout);
}

/**
 * @brief The name ACPICA gives a region space.
 */
static const char *harness_region_space_name(uint8_t space)
{
    static const char *const names[] = {
        "SystemMemory", "SystemIO", "PCI_Config",       "EmbeddedControl",  "SMBus", "SystemCMOS",
        "PCIBARTarget", "IPMI",     "GeneralPurposeIo", "GenericSerialBus", "PCC",   "PlatformRtMechanism"};

    if (space >= 0x80u)
        return "UserDefinedRegion";
    if (space == 0x7Eu)
        return "DataTable";
    if (space == 0x7Fu)
        return "FunctionalFixedHW";
    return (space < sizeof(names) / sizeof(names[0])) ? names[space] : "InvalidSpaceId";
}

/**
 * @brief The access width in bytes ACPICA gives an AccessType: AnyAcc and BufferAcc are byte
 *        accesses.
 */
static uint32_t harness_access_bytes(uint8_t access)
{
    return (access == 2u) ? 2u : (access == 3u) ? 4u : (access == 4u) ? 8u : 1u;
}

/**
 * @brief The access width ACPICA records for a field unit: its AccessType's, except that a unit of an
 *        EmbeddedControl region wider than a byte is read whole (ACPICA's exprep.c), which this
 *        kernel does not do, since it has no EmbeddedControl handler.
 */
static uint32_t harness_unit_access_bytes(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                          const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *unit)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *region =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, unit->region);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        (region != NULL) ?
            advanced_configuration_and_power_interface_machine_language_object(&load->heap, region->value.handle) :
            NULL;

    if (unit->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_NORMAL && object != NULL &&
        region->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION &&
        ((const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *) (object + 1))->space == 0x03u &&
        unit->bit_length > 8u && (unit->bit_length + 7u) / 8u < 256u)
        return (unit->bit_length + 7u) / 8u;
    return harness_access_bytes(unit->access);
}

/**
 * @brief The name of the node that holds the Buffer object @p handle, the first in index order, as
 *        ACPICA names the Buffer of a buffer field; "????" when no node holds it.
 */
static const char *harness_buffer_name(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                       uint32_t handle)
{
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         index < load->space.node_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, index);

        if (node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER &&
            node->value.handle == handle)
            return (const char *) &node->name;
    }
    return "????";
}

/**
 * @brief Prints one node's type and value as ACPICA's namespace dump does, its addresses apart.
 */
static void harness_print_node_value(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                     const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &load->space;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, node->value.handle);

    switch (node->value.kind)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER:
        printf("Integer = %016llX", (unsigned long long) node->value.payload);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING:
        printf("String Len %.2X ", object->length);
        harness_print_string((const char *) (object + 1), object->length);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER:
        printf("Buffer Len %.2X", object->length);
        if (object->length != 0u)
            fputs(" =", stdout);
        for (uint32_t byte = 0u; byte < object->length && byte < 12u; ++byte)
            printf(" %2.2X", ((const uint8_t *) (object + 1))[byte]);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE:
        printf("Package Elements %.2X", object->length);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REGION: {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region =
            (const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *) (object + 1);

        printf("Region [%s]", harness_region_space_name(region->space));
        if (region->state != 0u)
            printf(" Addr %016llX Len %.4X", (unsigned long long) region->offset, (unsigned int) region->length);
        else
            fputs(" [Address/Length not yet evaluated]", stdout);
        return;
    }
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT: {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *unit =
            (const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *) (object + 1);
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *region =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, unit->region);
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *other =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, unit->data_or_bank);

        if (unit->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_INDEX)
            printf("IndexField Idx [%.4s] Dat [%.4s]", (const char *) &region->name,
                   other ? (const char *) &other->name : "????");
        else if (unit->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_BANK)
            printf("BankField Rgn [%.4s] Bnk [%.4s]", (const char *) &region->name,
                   other ? (const char *) &other->name : "????");
        else
            printf("RegionField Rgn [%.4s]", (const char *) &region->name);
        printf(" Off %.3X Len %.2X Acc %.2X", unit->bit_offset, unit->bit_length,
               harness_unit_access_bytes(load, unit));
        return;
    }
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD:
        printf("BufferField Buf [%.4s] Off %.3X Len %.2X Acc %.2X", harness_buffer_name(load, node->value.handle),
               (unsigned int) (node->value.payload & 0xFFFFFFFFu), (unsigned int) (node->value.payload >> 32u),
               node->value.auxiliary);
        return;
    default: break;
    }
    switch (node->type)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD:
        printf("Method Args %X Len %.4X", node->value.auxiliary & 0x7u, (unsigned int) (node->value.payload >> 32u));
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_PROCESSOR:
        printf("Processor ID %.2X Len %.2X Addr %016llX", (unsigned int) ((node->value.payload >> 40u) & 0xFFu),
               (unsigned int) ((node->value.payload >> 32u) & 0xFFu),
               (unsigned long long) (node->value.payload & 0xFFFFFFFFu));
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE: fputs("Device", stdout); return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_MUTEX: fputs("Mutex", stdout); return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_EVENT: fputs("Event", stdout); return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_POWER_RESOURCE:
        fputs("Power", stdout);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_THERMAL_ZONE:
        fputs("Thermal", stdout);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_SCOPE: fputs("Scope", stdout); return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_ALIAS:
        printf("Alias Target %.4s",
               (const char *) &advanced_configuration_and_power_interface_machine_language_namespace_node(
                   space, node->value.handle)
                   ->name);
        return;
    default: fputs("Untyped", stdout); return;
    }
}

/**
 * @brief Prints every node of a load's namespace, depth first in declaration order, as
 *        `aml node <path> <type> <value>`; External nodes and the root are left out.
 */
static void harness_print_namespace(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &load->space;
    uint32_t path[256];
    uint32_t depth = 0u;
    uint32_t node = advanced_configuration_and_power_interface_machine_language_namespace_node(
                        space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT)
                        ->first_child;

    while (node != 0u)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *current =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, node);

        path[depth] = current->name;
        if (current->type != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_EXTERNAL)
        {
            fputs("aml node \\", stdout);
            for (uint32_t level = 0u; level <= depth; ++level)
                printf("%s%.4s", (level != 0u) ? "." : "", (const char *) &path[level]);
            putchar(' ');
            harness_print_node_value(load, current);
            putchar('\n');
        }
        if (current->first_child != 0u && depth + 1u < sizeof(path) / sizeof(path[0]))
        {
            ++depth;
            node = current->first_child;
            continue;
        }
        while (node != 0u &&
               advanced_configuration_and_power_interface_machine_language_namespace_node(space, node)->next_sibling ==
                   0u)
        {
            node = advanced_configuration_and_power_interface_machine_language_namespace_node(space, node)->parent;
            if (node == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT)
                node = 0u;
            else if (depth != 0u)
                --depth;
        }
        if (node != 0u)
            node =
                advanced_configuration_and_power_interface_machine_language_namespace_node(space, node)->next_sibling;
    }
}

/**
 * @brief What the harness prints of a load once its tables are decoded.
 */
typedef enum HarnessView {
    HARNESS_VIEW_NAMESPACE = 0,      /**< Its namespace, one node per line. */
    HARNESS_VIEW_CLOSURE = 1,        /**< The opcodes its needs path reaches. */
    HARNESS_VIEW_INITIALIZATION = 2, /**< What initialising its namespace ran and counted. */
} HarnessView_t;

static NeedsPathClosure_t harness_closure;
static AdvancedConfigurationAndPowerInterfaceInitialization_t harness_initialization;
static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t harness_idle;
static uint32_t harness_unimplemented[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT];

/**
 * @brief The machine a namespace is initialised for: the CPU slots of @p directory's MADT and its FADT,
 *        none without a directory, a processor that implements MONITOR/MWAIT, since a recorded machine's
 *        processor is not recorded, and a platform step 0 never switched to ACPI mode.
 */
static const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t *
harness_machine(const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    static AdvancedConfigurationAndPowerInterfaceMadtState_t madt;
    static AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine;

    machine.madt = (directory != NULL &&
                    advanced_configuration_and_power_interface_read_madt(
                        advanced_configuration_and_power_interface_find_in_directory(directory, "APIC"), &madt)) ?
                       &madt :
                       NULL;
    machine.monitor_wait = true;
    machine.fadt = (directory != NULL) ? &directory->fixed_description : NULL;
    return &machine;
}

/**
 * @brief Makes the closure the load's observer when @p view is the closure.
 *
 * @return false when the closure could not be allocated, which it says.
 */
static bool harness_observe(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, HarnessView_t view)
{
    if (view != HARNESS_VIEW_CLOSURE || needs_path_closure_attach(&harness_closure, load))
        return true;
    fputs("test-acpi: the closure's arrays could not be allocated\n", stderr);
    return false;
}

/**
 * @brief Walks the needs path of a decoded load and prints its counts, then each opcode it reaches,
 *        in descriptor order.
 */
static void harness_print_closure(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    uint32_t opcodes = 0u;

    needs_path_closure_walk(&harness_closure, load);
    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        opcodes += needs_path_closure_reaches_opcode(&harness_closure, opcode) ? 1u : 0u;
    printf("aml closure entries=%u methods=%u opcodes=%u dropped=%u\n", harness_closure.entries,
           harness_closure.methods, opcodes, harness_closure.dropped);
    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        if (needs_path_closure_reaches_opcode(&harness_closure, opcode))
            printf("aml closure opcode %s\n",
                   advanced_configuration_and_power_interface_machine_language_opcode_name(opcode));
}

/**
 * @brief Which method step 7 hands a bound processor object its capabilities through: its _OSC, else
 *        its _PDC, else none, so that acpiexec can be given the same handshake.
 */
static const char *harness_handshake(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                     uint32_t processor)
{
    if (advanced_configuration_and_power_interface_machine_language_namespace_find_object(
            &load->space, processor,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_OSC", 0u)) !=
        0u)
        return "osc";
    if (advanced_configuration_and_power_interface_machine_language_namespace_find_object(
            &load->space, processor,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "_PDC", 0u)) !=
        0u)
        return "pdc";
    return "none";
}

/**
 * @brief Prints what steps 8 and 9 counted, then each CPU slot whose _CST was evaluated, by its processor
 *        object's path, and each state it published.
 */
static void harness_print_idle(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                               const AdvancedConfigurationAndPowerInterfaceInitialization_t *counts)
{
    char path[HARNESS_PATH_BYTES];
    uint32_t published = 0u;
    uint32_t states = 0u;
    uint32_t skipped = 0u;

    for (uint32_t slot = 0u; slot < harness_idle.slot_count; ++slot)
    {
        published += (harness_idle.slots[slot].processor != 0u) ? 1u : 0u;
        states += harness_idle.slots[slot].count;
        skipped += harness_idle.slots[slot].skipped;
    }
    printf("aml idle control=%s processor_idle=%u processor_idle_failed=%u processor_idle_absent=%u "
           "processor_idle_unbound=%u idle_states_dropped=%u processor_idle_changes=%u slots=%u published=%u states=%u "
           "skipped=%u fold=0x%08X\n",
           advanced_configuration_and_power_interface_idle_control_name(counts->idle_control), counts->processor_idle,
           counts->processor_idle_failed, counts->processor_idle_absent, counts->processor_idle_unbound,
           counts->idle_states_dropped, counts->processor_idle_changes, harness_idle.slot_count, published, states,
           skipped, advanced_configuration_and_power_interface_processor_idle_fold(&harness_idle));
    for (uint32_t slot = 0u; slot < harness_idle.slot_count; ++slot)
    {
        const AdvancedConfigurationAndPowerInterfaceProcessorIdle_t *idle = &harness_idle.slots[slot];

        if (idle->processor == 0u)
            continue;
        if (advanced_configuration_and_power_interface_machine_language_namespace_path(&load->space, idle->processor,
                                                                                       path, sizeof(path)) == 0u)
            memcpy(path, "too_deep", sizeof("too_deep"));
        printf("aml idle slot %u %s states=%u skipped=%u handshake=%s\n", slot, path, idle->count, idle->skipped,
               harness_handshake(load, idle->processor));
        for (uint32_t state = 0u; state < idle->count; ++state)
            printf("aml idle state %u %u type=%u latency=%u power=%u space=0x%02X width=%u offset=%u access=%u "
                   "address=0x%llX\n",
                   slot, state + 1u, idle->states[state].type, idle->states[state].latency, idle->states[state].power,
                   idle->states[state].entry.space, idle->states[state].entry.bit_width,
                   idle->states[state].entry.bit_offset, idle->states[state].entry.access_size,
                   (unsigned long long) idle->states[state].entry.address);
    }
}

/**
 * @brief Initialises a decoded load's namespace as the boot does, for the machine @p directory describes,
 *        and prints what it ran and counted, each status a failure ended with, each unimplemented opcode
 *        it reached, the first object whose evaluation failed, and the C-states it published.
 */
static void harness_print_initialization(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                         const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    const AdvancedConfigurationAndPowerInterfaceInitialization_t *counts = &harness_initialization;
    char path[HARNESS_PATH_BYTES] = "none";

    memcpy(harness_unimplemented, load->evaluation.unimplemented, sizeof(harness_unimplemented));
    advanced_configuration_and_power_interface_initialize_namespace(load, harness_machine(directory),
                                                                    &harness_initialization, &harness_idle);
    printf("aml init initializations=%u initialization_failed=%u registrations=%u region_registration_failed=%u "
           "devices=%u status_evaluations=%u status_failed=%u status_invalid=%u pic_mode_deferred=%u "
           "platform_capabilities=%s processor_capabilities=%u processor_capabilities_legacy=%u "
           "processor_capabilities_refused=%u processor_capabilities_failed=%u processors_unbound=%u tables=%u "
           "late_initializations=%u\n",
           counts->initializations, counts->initialization_failed, counts->registrations,
           counts->region_registration_failed, counts->devices, counts->status_evaluations, counts->status_failed,
           counts->status_invalid, counts->pic_mode_deferred,
           advanced_configuration_and_power_interface_platform_capabilities_name(counts->platform_capabilities),
           counts->processor_capabilities, counts->processor_capabilities_legacy,
           counts->processor_capabilities_refused, counts->processor_capabilities_failed, counts->processors_unbound,
           counts->tables, counts->late_initializations);
    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        if (load->evaluation.unimplemented[opcode] != harness_unimplemented[opcode])
            printf("aml init unimplemented %s=%u\n",
                   advanced_configuration_and_power_interface_machine_language_opcode_name(opcode),
                   load->evaluation.unimplemented[opcode] - harness_unimplemented[opcode]);
    for (uint32_t status = 1u; status < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT;
         ++status)
        if (counts->statuses[status] != 0u)
            printf("aml init failed %s=%u\n",
                   advanced_configuration_and_power_interface_machine_language_status_name(status),
                   counts->statuses[status]);
    if (counts->first_failure_node != 0u)
    {
        (void) advanced_configuration_and_power_interface_machine_language_namespace_path(
            &load->space, counts->first_failure_node, path, sizeof(path));
        printf("aml init first_failure %s %s\n", path,
               advanced_configuration_and_power_interface_machine_language_status_name(counts->first_failure));
    }
    harness_print_idle(load, counts);
}

/**
 * @brief Prints what a load could not build, then the view of it asked for: its namespace, its closure,
 *        or what initialising its namespace for the machine @p directory describes, or none, ran, which
 *        the runs printed after it include.
 */
static void harness_print_load(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                               const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory, HarnessView_t view)
{
    printf("aml namespace table_level_failures=%u table_level_code=%u values_deferred=%u load_errors=%u "
           "decode_errors=%u\n",
           load->evaluation.failures, load->table_level_code, load->values_deferred, load->load_errors,
           load->decode_errors);
    if (view == HARNESS_VIEW_INITIALIZATION)
        harness_print_initialization(load, directory);
    harness_print_runs(load);
    if (view == HARNESS_VIEW_CLOSURE)
        harness_print_closure(load);
    else if (view == HARNESS_VIEW_NAMESPACE)
        harness_print_namespace(load);
}

/**
 * @brief The node the absolute path @p text names, as acpiexec writes one: a backslash, then
 *        NameSegs of up to four characters, padded with underscores, separated by dots; 0 when a
 *        segment is missing.
 */
static uint32_t harness_find_path(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, const char *text)
{
    uint32_t node = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;

    if (*text++ != '\\')
        return 0u;
    while (*text != '\0' && node != 0u)
    {
        uint8_t segment[4] = {'_', '_', '_', '_'};
        uint32_t length = 0u;

        while (*text != '\0' && *text != '.')
        {
            if (length == sizeof(segment))
                return 0u;
            segment[length++] = (uint8_t) *text++;
        }
        if (*text == '.')
            ++text;
        node = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            &load->space, node, advanced_configuration_and_power_interface_machine_language_name_segment(segment, 0u));
    }
    return node;
}

/**
 * @brief Gives the objects a preset file names their value before the initialisation, as an acpiexec -fi
 *        file of the same lines would, so that firmware reads what it wrote to NVS at boot, which a dump lacks
 *        (§8.4): each line is a path and a hexadecimal integer; a field unit is written, an Integer
 *        replaced; a line that starts with #, or names nothing else, is skipped.
 *
 * @return The presets given.
 */
static uint32_t harness_apply_presets(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                      const char *path)
{
    FILE *file = fopen(path, "r");
    char line[HARNESS_PATH_BYTES];
    uint32_t applied = 0u;

    if (file == NULL)
    {
        fprintf(stderr, "test-acpi: %s: cannot be opened\n", path);
        return 0u;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char name[HARNESS_PATH_BYTES];
        unsigned long long integer = 0u;
        uint32_t node = 0u;

        if (line[0] == '#' || sscanf(line, "%255s %llx", name, &integer) != 2 ||
            (node = harness_find_path(load, name)) == 0u)
            continue;

        AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *object =
            advanced_configuration_and_power_interface_machine_language_namespace_node(&load->space, node);
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value = {
            .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
            .payload = integer};

        if (object->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FIELD_UNIT)
            applied += (advanced_configuration_and_power_interface_machine_language_field_write(load, node, &value) ==
                        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK) ?
                           1u :
                           0u;
        else if (object->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        {
            object->value.payload = integer;
            ++applied;
        }
    }
    fclose(file);
    return applied;
}

/**
 * @brief Writes each table of @p load, in its order, as a raw file of @p directory, so that acpiexec
 *        loads statically the tables the load holds, the ones Load added included.
 *
 * @return false, said on stderr, when one cannot be written.
 */
static bool harness_write_tables(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                 const char *directory)
{
    for (uint32_t index = 0u; index < load->table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageTable_t *table = &load->tables[index];
        char path[HARNESS_PATH_BYTES * 2u];
        FILE *file = NULL;

        snprintf(path, sizeof(path), "%s/%03u-%.4s.dat", directory, index, table->header->signature);
        file = fopen(path, "wb");
        if (file == NULL || fwrite(table->header, 1u, table->length, file) != table->length || fclose(file) != 0)
        {
            fprintf(stderr, "test-acpi: %s: cannot be written\n", path);
            return false;
        }
    }
    return true;
}

/**
 * @brief Loads the definition blocks of the dump at @p path with the server profile's caps, and
 *        prints what the load could not build, then the view of it asked for: initialised, the tables
 *        served for Load wait for it, and the objects of the preset file @p presets, when it is not
 *        NULL, get their value first; any other view loads them statically. With @p tables, the
 *        initialised namespace is printed too, and each table it holds written there.
 *
 * @return 0 when the directory was read, 1 otherwise.
 */
static int harness_dump_namespace(const char *path, HarnessView_t view, const char *presets, const char *tables)
{
    static RecordedMachine_t machine;
    static AdvancedConfigurationAndPowerInterfaceDirectory_t directory;
    static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;
    const char *error = NULL;
    int status = 1;

    if (!recorded_machine_open(&machine, path, &error))
        fprintf(stderr, "test-acpi: %s: %s\n", path, error);
    else if (!advanced_configuration_and_power_interface_read_directory(&machine.simulated.host, &directory))
        fprintf(stderr, "test-acpi: %s: no directory could be read\n", path);
    else
    {
        if (advanced_configuration_and_power_interface_machine_language_open(
                &load, &machine.simulated.host,
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES,
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES) &&
            harness_observe(&load, view))
        {
            if (view == HARNESS_VIEW_INITIALIZATION)
            {
                advanced_configuration_and_power_interface_machine_language_load_directory(&load, &directory);
                printf("aml load placed=%u presets=%u\n", machine.placed_count,
                       (presets != NULL) ? harness_apply_presets(&load, presets) : 0u);
            }
            else
                harness_load_every_table(&load, &directory, &machine);
            harness_print_load(&load, &directory, view);
            status = 0;
            if (tables != NULL)
            {
                harness_print_namespace(&load);
                status = harness_write_tables(&load, tables) ? 0 : 1;
            }
        }
        advanced_configuration_and_power_interface_machine_language_close(&load);
        needs_path_closure_release(&harness_closure);
        advanced_configuration_and_power_interface_release_directory(&directory);
    }
    recorded_machine_close(&machine);
    return status;
}

/**
 * @brief Reads a whole definition block from @p path into memory the caller frees.
 *
 * @return false, said on stderr, when the file cannot be opened or is no whole table.
 */
static bool harness_read_table(const char *path, uint8_t **out_table)
{
    FILE *file = fopen(path, "rb");
    long length = -1;
    bool read = false;

    if (file == NULL)
    {
        fprintf(stderr, "test-acpi: %s: cannot be opened\n", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) == 0)
        length = ftell(file);
    if (length >= (long) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) && fseek(file, 0, SEEK_SET) == 0 &&
        (*out_table = malloc((size_t) length)) != NULL &&
        fread(*out_table, 1u, (size_t) length, file) == (size_t) length)
        read = ((const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) *out_table)->length <= (uint32_t) length;
    if (!read)
        fprintf(stderr, "test-acpi: %s: not a whole table\n", path);
    fclose(file);
    return read;
}

/**
 * @brief Loads raw definition blocks, the DSDT first, with the server profile's caps, and prints
 *        what the load could not build, then the view of it asked for.
 *
 * @return 0 when every file was read and the namespace opened, 1 otherwise.
 */
static int harness_dump_tables_namespace(int count, char **paths, HarnessView_t view)
{
    static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;
    static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t simulated;
    uint8_t *tables[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES] = {0};
    int status = 1;

    advanced_configuration_and_power_interface_simulated_host_open(&simulated);
    if (count <= (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES &&
        advanced_configuration_and_power_interface_machine_language_open(
            &load, &simulated.host, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES,
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES))
    {
        status = harness_observe(&load, view) ? 0 : 1;
        for (int index = 0; index < count && status == 0; ++index)
        {
            if (harness_read_table(paths[index], &tables[index]))
                advanced_configuration_and_power_interface_machine_language_declare_table(
                    &load, (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) tables[index]);
            else
                status = 1;
        }
        if (status == 0)
        {
            advanced_configuration_and_power_interface_machine_language_decode_tables(&load);
            harness_print_load(&load, NULL, view);
        }
    }
    advanced_configuration_and_power_interface_machine_language_close(&load);
    needs_path_closure_release(&harness_closure);
    for (int index = 0;
         index < count && index < (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES; ++index)
        free(tables[index]);
    return status;
}

/**
 * @brief Prints the result of an evaluation: its type, then an Integer in decimal, the characters of a
 *        String, the bytes of a Buffer in hexadecimal, or a Package's count.
 */
static void harness_print_result(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
                                 const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *result)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&load->heap, result->handle);

    switch (result->kind)
    {
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER:
        printf("aml evaluate type=int value=%llu\n", (unsigned long long) result->payload);
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING:
        printf("aml evaluate type=str value=%.*s\n", (int) object->length, (const char *) (object + 1));
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER:
        fputs("aml evaluate type=buffer value=", stdout);
        for (uint32_t byte = 0u; byte < object->length; ++byte)
            printf("%s%02X", (byte != 0u) ? " " : "", ((const uint8_t *) (object + 1))[byte]);
        putchar('\n');
        return;
    case ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE:
        printf("aml evaluate type=package value=%u\n", object->length);
        return;
    default: printf("aml evaluate type=other value=%u\n", result->kind); return;
    }
}

/**
 * @brief Loads raw definition blocks, the DSDT first, running their table-level code, initialises their
 *        namespace as uACPI's test runner does before it evaluates \MAIN, then evaluates @p path and
 *        prints its status, its result, and each opcode the load, the initialisation and the evaluation
 *        ran but do not execute.
 *
 * @return 0 when every file was read and the namespace opened, 1 otherwise; the evaluation's own
 *         failure is printed, not returned.
 */
static int harness_evaluate(const char *path, int count, char **paths)
{
    static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load;
    static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t simulated;
    uint8_t *tables[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES] = {0};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    int status = 1;

    advanced_configuration_and_power_interface_simulated_host_open(&simulated);
    if (count <= (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES &&
        advanced_configuration_and_power_interface_machine_language_open(
            &load, &simulated.host, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES,
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES))
    {
        status = 0;
        for (int index = 0; index < count && status == 0; ++index)
            status = harness_read_table(paths[index], &tables[index]) ? 0 : 1;
        for (int index = 0; index < count && status == 0; ++index)
            advanced_configuration_and_power_interface_machine_language_declare_table(
                &load, (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) tables[index]);
        if (status == 0)
        {
            advanced_configuration_and_power_interface_machine_language_decode_tables(&load);
            advanced_configuration_and_power_interface_initialize_namespace(&load, harness_machine(NULL),
                                                                            &harness_initialization, &harness_idle);
            printf("aml evaluate status=%s\n", advanced_configuration_and_power_interface_machine_language_status_name(
                                                   advanced_configuration_and_power_interface_machine_language_evaluate(
                                                       &load, path, NULL, 0u, &result)));
            harness_print_result(&load, &result);
            advanced_configuration_and_power_interface_machine_language_value_release(&load.heap, &load.space, &result);
            for (uint32_t opcode = 1u;
                 opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT; ++opcode)
                if (load.evaluation.unimplemented[opcode] != 0u)
                    printf("aml evaluate unimplemented %s\n",
                           advanced_configuration_and_power_interface_machine_language_opcode_name(opcode));
            for (uint32_t failure = 1u;
                 failure < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT; ++failure)
                if (load.evaluation.statuses[failure] != 0u)
                    printf("aml evaluate failed %s %u\n",
                           advanced_configuration_and_power_interface_machine_language_status_name(failure),
                           load.evaluation.statuses[failure]);
        }
    }
    advanced_configuration_and_power_interface_machine_language_close(&load);
    for (int index = 0;
         index < count && index < (int) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES; ++index)
        free(tables[index]);
    return status;
}

/**
 * @brief Writes the firmware the dump at @p dump gives, as the harness serves it, as an image at @p path.
 *
 * @return 0 when the image is written, 1 otherwise.
 */
static int harness_write_module(const char *dump, const char *path)
{
    static RecordedMachine_t machine;
    const char *error = NULL;
    int status = 1;

    if (!recorded_machine_open(&machine, dump, &error))
        fprintf(stderr, "test-acpi: %s: %s\n", dump, error);
    else if (!recorded_machine_write_image(&machine, path, &error))
        fprintf(stderr, "test-acpi: %s: %s\n", path, error);
    else
        status = 0;
    recorded_machine_close(&machine);
    return status;
}

/**
 * @brief Reads the file at @p path into pages of its own, as a bootloader loads a module.
 *
 * @return The bytes, freed by the caller, or NULL when the file is empty, longer than 4 GiB or cannot
 *         be read.
 */
static uint8_t *harness_read_module(const char *path, uint32_t *out_length)
{
    FILE *file = fopen(path, "rb");
    long length = -1;
    uint8_t *bytes = NULL;

    if (file != NULL && fseek(file, 0, SEEK_END) == 0)
        length = ftell(file);
    if (length > 0 && (unsigned long) length <= UINT32_MAX && fseek(file, 0, SEEK_SET) == 0)
        bytes = aligned_alloc(HARNESS_PAGE_BYTES,
                              ((size_t) length + HARNESS_PAGE_BYTES - 1u) / HARNESS_PAGE_BYTES * HARNESS_PAGE_BYTES);
    if (bytes != NULL && fread(bytes, 1u, (size_t) length, file) != (size_t) length)
    {
        free(bytes);
        bytes = NULL;
    }
    if (file != NULL)
        fclose(file);
    *out_length = (bytes != NULL) ? (uint32_t) length : 0u;
    return bytes;
}

/**
 * @brief Runs every stage of the kernel's tests linked in, which are the ACPI ones.
 *
 * @return 0 when none failed, 1 otherwise.
 */
static int harness_run_stages(void)
{
    Serial_t standard_output = {0};

    kernel_test_run_stage(KERNEL_TEST_STAGE_INITIALIZATION, &standard_output);
    kernel_test_run_stage(KERNEL_TEST_STAGE_BOOTED, &standard_output);
    kernel_test_run_stage(KERNEL_TEST_STAGE_PUBLISHED, &standard_output);
    return (kernel_test_failure_count() == 0u) ? 0 : 1;
}

/**
 * @brief Runs the kernel's ACPI tests, given the module at @p module_path and the firmware the log at
 *        @p log_path printed, each when it is not NULL.
 *
 * @return 0 when every test passed and every window the firmware mapped was unmapped, 1 when one did
 *         not or an input cannot be read.
 */
static int harness_run_tests(const char *module_path, const char *log_path)
{
    static RecordedMachine_t firmware;
    uint8_t *module = NULL;
    uint32_t module_length = 0u;
    const char *error = NULL;
    int status = 1;

    if (module_path != NULL && (module = harness_read_module(module_path, &module_length)) == NULL)
        fprintf(stderr, "test-acpi: %s: the module cannot be read, or is empty\n", module_path);
    else if (log_path != NULL && !recorded_machine_open_printed(&firmware, log_path, &error))
        fprintf(stderr, "test-acpi: %s: %s\n", log_path, error);
    else
    {
        if (module != NULL)
            platform_serve_boot_module(module_path, module, module_length);
        if (log_path != NULL)
            platform_serve_firmware(&firmware.simulated.host);
        status = harness_run_stages();
        if (firmware.simulated.outstanding_maps != 0u || firmware.simulated.mismatched_unmaps != 0u)
        {
            fprintf(stderr, "test-acpi: %s: %u windows left mapped, %u unmaps that named no window\n", log_path,
                    firmware.simulated.outstanding_maps, firmware.simulated.mismatched_unmaps);
            status = 1;
        }
        platform_serve_firmware(NULL);
        platform_serve_boot_module(NULL, NULL, 0u);
    }
    recorded_machine_close(&firmware);
    free(module);
    return status;
}

/**
 * @brief Reads the options of a test run: each of `--boot-module <image>` and `--firmware-log <log>`
 *        at most once.
 *
 * @return false when an option is unknown, repeated or has no value.
 */
static bool harness_read_test_options(int count, char **options, const char **module_path, const char **log_path)
{
    for (int index = 0; index < count; index += 2)
    {
        const char **value = (strcmp(options[index], "--boot-module") == 0)  ? module_path :
                             (strcmp(options[index], "--firmware-log") == 0) ? log_path :
                                                                               NULL;

        if (value == NULL || *value != NULL || index + 1 >= count)
            return false;
        *value = options[index + 1];
    }
    return true;
}

/**
 * @brief Reads which view an option of a dump asks for: --namespace, --closure or --initialize, each
 *        followed by @p suffix.
 *
 * @return false when @p option is none of them.
 */
static bool harness_view(const char *option, const char *suffix, HarnessView_t *out_view)
{
    static const char *const views[] = {"--namespace", "--closure", "--initialize"};
    const size_t suffix_length = strlen(suffix);

    for (uint32_t view = 0u; view < sizeof(views) / sizeof(views[0]); ++view)
    {
        const size_t length = strlen(views[view]);

        if (strncmp(option, views[view], length) == 0 && strncmp(option + length, suffix, suffix_length + 1u) == 0)
        {
            *out_view = (HarnessView_t) view;
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    const char *module_path = NULL;
    const char *log_path = NULL;
    HarnessView_t view = HARNESS_VIEW_NAMESPACE;

    if (argc == 3 && strcmp(argv[1], "--machine") == 0)
        return harness_walk_machine(argv[2]);
    if (argc == 3 && harness_view(argv[1], "", &view))
        return harness_dump_namespace(argv[2], view, NULL, NULL);
    if (argc == 4 && strcmp(argv[1], "--initialize") == 0)
        return harness_dump_namespace(argv[2], HARNESS_VIEW_INITIALIZATION, argv[3], NULL);
    if (argc == 5 && strcmp(argv[1], "--loaded") == 0)
        return harness_dump_namespace(argv[2], HARNESS_VIEW_INITIALIZATION, argv[3], argv[4]);
    if (argc >= 3 && harness_view(argv[1], "-tables", &view))
        return harness_dump_tables_namespace(argc - 2, argv + 2, view);
    if (argc >= 4 && strcmp(argv[1], "--evaluate") == 0)
        return harness_evaluate(argv[2], argc - 3, argv + 3);
    if (argc == 4 && strcmp(argv[1], "--write-module") == 0)
        return harness_write_module(argv[2], argv[3]);
    if (harness_read_test_options(argc - 1, argv + 1, &module_path, &log_path))
        return harness_run_tests(module_path, log_path);
    fputs(HARNESS_USAGE, (argc == 2 && strcmp(argv[1], "--help") == 0) ? stdout : stderr);
    return (argc == 2 && strcmp(argv[1], "--help") == 0) ? 0 : 2;
}
