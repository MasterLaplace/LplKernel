#include "needs_path_closure.h"
#include "recorded_machine.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HARNESS_USAGE                                                                                                  \
    "Usage: test-acpi                    runs the kernel's ACPI tests, reported in KTAP\n"                             \
    "       test-acpi --machine <dump>   walks the tables of an acpidump text file and prints its directory, its\n"    \
    "                                    MADT, then what loading its definition blocks decoded\n"                      \
    "       test-acpi --namespace <dump> loads the definition blocks of an acpidump text file and prints its\n"        \
    "                                    namespace, one node per line, as acpiexec's namespace command does\n"         \
    "       test-acpi --namespace-tables <table>...  the same from raw definition blocks, the DSDT first\n"            \
    "       test-acpi --closure <dump>   prints the opcodes the needs path of an acpidump text file reaches\n"         \
    "       test-acpi --closure-tables <table>...    the same from raw definition blocks, the DSDT first\n"

/**
 * @brief Prints the directory of @p directory: its counts, the FACS, the PM1 control ports and the
 *        hardware-reduced flag, then one line per table.
 */
static void harness_print_directory(const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    printf("tables=%u unreachable=%u facs=0x%08X pm1_control=0x%04X,0x%04X hardware_reduced=%u\n",
           directory->table_count, directory->unreachable_count, directory->firmware_control_physical_address,
           directory->power_management_control_ports[0], directory->power_management_control_ports[1],
           directory->hardware_reduced ? 1u : 0u);
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
 * @brief Prints what running table-level code did: its runs and failures, its field accesses, the
 *        failures per status, the region spaces refused, and the opcodes that failed as unimplemented.
 */
static void harness_print_runs(const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &load->evaluation;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t *regions = &load->regions;

    printf("aml run statements=%u failures=%u implicit_returns=%u uninitialized_operands=%u frames_peak=%u "
           "values_peak=%u declared_not_run=%u\n",
           evaluation->runs, evaluation->failures, evaluation->implicit_returns, evaluation->uninitialized_operands,
           evaluation->frames_peak, evaluation->values_peak, load->declared_not_run);
    printf("aml run regions field_reads=%u field_writes=%u map_calls=%u mapping_cache_hits=%u "
           "split_qword_accesses=%u unaligned_accesses=%u protected_port_accesses=%u region_lengths_rounded=%u\n",
           regions->field_reads, regions->field_writes, regions->map_calls, regions->mapping_cache_hits,
           regions->split_qword_accesses, regions->unaligned_accesses, regions->protected_port_accesses,
           regions->region_lengths_rounded);
    printf("aml run global_lock acquisitions=%u waits=%u releases_signalled=%u timeouts=%u unlocked_accesses=%u\n",
           regions->global_lock_acquisitions, regions->global_lock_waits, regions->global_lock_releases_signalled,
           regions->global_lock_timeouts, regions->unlocked_lock_accesses);
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
    load.declares_every_branch = true;
    advanced_configuration_and_power_interface_machine_language_load_directory(&load, directory);
    printf("aml tables=%u bytes=%u decode_errors=%u\n", load.table_count, load.bytes, load.decode_errors);
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
        printf("BufferField Buf [%.4s] Off %.3X Len %.2X Acc %.2X",
               (const char *) &advanced_configuration_and_power_interface_machine_language_namespace_node(
                   space, node->value.handle)
                   ->name,
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

static NeedsPathClosure_t harness_closure;

/**
 * @brief Makes the closure the load's observer when @p closure holds.
 *
 * @return false when the closure could not be allocated, which it says.
 */
static bool harness_observe(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, bool closure)
{
    if (!closure || needs_path_closure_attach(&harness_closure, load))
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
 * @brief Prints what a load could not build, then its namespace, or its closure when @p closure holds.
 */
static void harness_print_load(AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load, bool closure)
{
    printf("aml namespace table_level_failures=%u table_level_code=%u values_deferred=%u load_errors=%u "
           "decode_errors=%u\n",
           load->evaluation.failures, load->table_level_code, load->values_deferred, load->load_errors,
           load->decode_errors);
    harness_print_runs(load);
    if (closure)
        harness_print_closure(load);
    else
        harness_print_namespace(load);
}

/**
 * @brief Loads the definition blocks of the dump at @p path with the server profile's caps, and
 *        prints what the load could not build, then its namespace, or its closure when @p closure holds.
 *
 * @return 0 when the directory was read, 1 otherwise.
 */
static int harness_dump_namespace(const char *path, bool closure)
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
            harness_observe(&load, closure))
        {
            advanced_configuration_and_power_interface_machine_language_load_directory(&load, &directory);
            harness_print_load(&load, closure);
            status = 0;
        }
        advanced_configuration_and_power_interface_machine_language_close(&load);
        needs_path_closure_release(&harness_closure);
        advanced_configuration_and_power_interface_release_directory(&directory);
    }
    recorded_machine_close(&machine);
    return status;
}

/**
 * @brief Loads raw definition blocks, the DSDT first, with the server profile's caps, and prints
 *        what the load could not build, then its namespace, or its closure when @p closure holds.
 *
 * @return 0 when every file was read and the namespace opened, 1 otherwise.
 */
static int harness_dump_tables_namespace(int count, char **paths, bool closure)
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
        status = harness_observe(&load, closure) ? 0 : 1;
        for (int index = 0; index < count && status == 0; ++index)
        {
            FILE *file = fopen(paths[index], "rb");
            long length = -1;

            if (file != NULL && fseek(file, 0, SEEK_END) == 0)
                length = ftell(file);
            if (length >= (long) sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) &&
                fseek(file, 0, SEEK_SET) == 0 && (tables[index] = malloc((size_t) length)) != NULL &&
                fread(tables[index], 1u, (size_t) length, file) == (size_t) length &&
                ((const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) tables[index])->length <=
                    (uint32_t) length)
                advanced_configuration_and_power_interface_machine_language_declare_table(
                    &load, (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) tables[index]);
            else
            {
                fprintf(stderr, "test-acpi: %s: not a whole table\n", paths[index]);
                status = 1;
            }
            if (file != NULL)
                fclose(file);
        }
        if (status == 0)
        {
            advanced_configuration_and_power_interface_machine_language_decode_tables(&load);
            harness_print_load(&load, closure);
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
    if (argc == 3 && (strcmp(argv[1], "--namespace") == 0 || strcmp(argv[1], "--closure") == 0))
        return harness_dump_namespace(argv[2], strcmp(argv[1], "--closure") == 0);
    if (argc >= 3 && (strcmp(argv[1], "--namespace-tables") == 0 || strcmp(argv[1], "--closure-tables") == 0))
        return harness_dump_tables_namespace(argc - 2, argv + 2, strcmp(argv[1], "--closure-tables") == 0);
    fputs(HARNESS_USAGE, (argc == 2 && strcmp(argv[1], "--help") == 0) ? stdout : stderr);
    return (argc == 2 && strcmp(argv[1], "--help") == 0) ? 0 : 2;
}
