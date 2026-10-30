#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/machine_language.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_load, KERNEL_TEST_STAGE_INITIALIZATION);

#define LOAD_STATUS(name)         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define LOAD_ORIGIN(name)         ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_TABLE_##name
#define LOAD_SYSTEM_MEMORY        0x00u                 /**< The RegionSpace byte of SystemMemory. */
#define LOAD_SYSTEM_IO            0x01u                 /**< The RegionSpace byte of SystemIO. */
#define LOAD_TABLE_ADDRESS        0x00300000u           /**< Where the table Load reads is placed. */
#define LOAD_OTHER_ADDRESS        0x00310000u           /**< Where a second table is placed. */
#define LOAD_ONES                 0xFFFFFFFFFFFFFFFFull /**< True under a DSDT of revision 2. */
#define LOAD_UNTOUCHED            0x55u                 /**< What a Name a Load must not store to holds. */
#define LOAD_REVISION_BITS        (24u * 8u)            /**< The bits of a header before its OEM revision. */
#define LOAD_PROCESSOR_IDENTIFIER 1u                    /**< The ACPI Processor UID of the made-up CPU slot. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t load_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t load_load;
static AdvancedConfigurationAndPowerInterfaceInitialization_t load_counts;
static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t load_idle;
static AdvancedConfigurationAndPowerInterfaceMadtState_t load_madt;
static AdvancedConfigurationAndPowerInterfaceDirectory_t load_directory;
static uint8_t load_dsdt[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t load_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static uint8_t load_other_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static MadeUpAml_t load_aml;
static MadeUpAml_t load_loaded;
static MadeUpAml_t load_body;
static MadeUpAml_t load_inner;

/**
 * @brief Opens the simulated host and empties both builders: load_aml, the DSDT, starts with Name (TRAC, 0)
 *        and Name (HND1, 0x55); load_loaded, the table Load reads, with Name (LDED, 0x2A).
 */
static void load_begin(void)
{
    advanced_configuration_and_power_interface_simulated_host_open(&load_host);
    memset(&load_madt, 0, sizeof(load_madt));
    made_up_aml_begin(&load_aml);
    made_up_aml_name_integer(&load_aml, "TRAC", 0u);
    made_up_aml_name_integer(&load_aml, "HND1", LOAD_UNTOUCHED);
    made_up_aml_begin(&load_loaded);
    made_up_aml_name_integer(&load_loaded, "LDED", 0x2Au);
}

/**
 * @brief Writes the AML of @p aml as an SSDT in @p table and pokes it at @p address of the simulated
 *        memory.
 *
 * @return Its length.
 */
static uint32_t load_place(const MadeUpAml_t *aml, uint8_t *table, uint64_t address)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        made_up_definition_block(table, "SSDT", 2u, aml->bytes, aml->length);

    (void) advanced_configuration_and_power_interface_simulated_host_poke(&load_host, address, table, header->length);
    return header->length;
}

/**
 * @brief Appends OperationRegion (@p name, SystemMemory, @p address, @p length) to the DSDT.
 */
static void load_region(const char *name, uint64_t address, uint64_t length)
{
    made_up_aml_region(&load_aml, name, LOAD_SYSTEM_MEMORY, address, length);
}

/**
 * @brief Appends Method (@p name) { the @p length bytes at @p body } to @p aml.
 */
static void load_method(MadeUpAml_t *aml, const char *name, const uint8_t *body, uint32_t length)
{
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, body, length);
    made_up_aml_method(aml, name, 0x00u, &load_body);
}

/**
 * @brief Loads the DSDT written, its table-level code run.
 */
static bool load_run(KernelTest_t *test)
{
    return kernel_test_check(test, !load_aml.overflowed && !load_loaded.overflowed, "the made-up AML fits") &&
           made_up_load_on(test, &load_load, &load_host.host, load_dsdt, 2u, load_aml.bytes, load_aml.length);
}

/**
 * @brief Evaluates the method at @p path, which takes no argument, and gives back its status.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t load_call(const char *path)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&load_load, path, NULL, 0u, &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&load_load.heap, &load_load.space,
                                                                              &result);
    return status;
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t load_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&load_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Checks that the evaluations left nothing behind, then closes the load and the host.
 */
static void load_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &load_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          load_load.temporary_count == 0u && load_load.declaring == 0u,
                      "every evaluation leaves no frame, value, operator, block, temporary node or declaration behind");
    kernel_test_check(test, load_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    advanced_configuration_and_power_interface_machine_language_close(&load_load);
    kernel_test_check(test, load_load.space.arena_bytes == 0u, "closing gives every page back, the copies included");
    advanced_configuration_and_power_interface_simulated_host_close(&load_host);
}

/**
 * @brief Load from a SystemMemory region copies the table, runs it relative to the root from the copy,
 *        and stores True; loading the same table again stores False and adds nothing (§5.1).
 */
KERNEL_TEST(a_load_runs_a_copy_of_its_table_and_the_same_table_again_returns_false)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t loads_again[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '2'};
    static const uint8_t returns[] = {0xA4u, 0x0Au, 0x77u};
    static const uint8_t zero[MADE_UP_TABLE_HEADER + 64u] = {0};
    static MadeUpAml_t device;

    load_begin();
    load_method(&load_loaded, "LMTH", returns, sizeof(returns));

    const uint32_t length = load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS);

    load_region("TBLR", LOAD_TABLE_ADDRESS, length);
    made_up_aml_name_integer(&load_aml, "HND2", LOAD_UNTOUCHED);
    made_up_aml_begin(&device);
    load_method(&device, "LDIT", loads, sizeof(loads));
    load_method(&device, "LDAG", loads_again, sizeof(loads_again));
    made_up_aml_device(&load_aml, "XDEV", &device);
    if (!load_run(test))
        return;
    kernel_test_check(test, load_integer("LDED") == 0xDEADu, "the table's objects do not exist before the Load");
    kernel_test_check(test, load_call("\\XDEV.LDIT") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "Load from the region succeeds, and stores True");
    kernel_test_check(test, load_integer("LDED") == 0x2Au,
                      "the table's objects are created relative to the root, not the method's scope");
    kernel_test_check(test,
                      load_load.table_count == 2u && load_load.tables_loaded == 1u &&
                          load_load.tables[1].origin == LOAD_ORIGIN(LOAD) &&
                          load_load.tables[1].physical_address == LOAD_TABLE_ADDRESS &&
                          load_load.tables[1].length == length && load_load.tables[1].pages == 1u &&
                          load_load.tables[1].checksum_valid && load_load.table_bytes_copied == length,
                      "the registry holds the table Load copied, its address, length, checksum and pages");
    kernel_test_check(test,
                      load_call("\\XDEV.LDAG") == LOAD_STATUS(OK) && load_integer("HND2") == 0u &&
                          load_load.table_count == 2u && load_load.load_duplicates == 1u &&
                          load_load.table_bytes_copied == length,
                      "the same table loaded again stores False, adds nothing, and is counted");
    (void) advanced_configuration_and_power_interface_simulated_host_poke(&load_host, LOAD_TABLE_ADDRESS, zero, length);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;

    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &load_load, "\\LMTH", NULL, 0u, &result) == LOAD_STATUS(OK) &&
                          result.payload == 0x77u,
                      "a method of the table runs from the copy once the firmware's bytes are gone");
    load_close(test);
}

/**
 * @brief Load from a field unit reads the unit as a Buffer and loads the table it holds (§5.1).
 */
KERNEL_TEST(a_load_from_a_field_reads_the_unit_as_a_buffer)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'A', 'B', 'L', '\\', 'H', 'N', 'D', '1'};

    load_begin();

    const uint32_t length = load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS);
    const MadeUpFieldUnit_t units[] = {
        {"TABL", 8u * length},
    };

    load_region("TBLR", LOAD_TABLE_ADDRESS, length);
    made_up_aml_field(&load_aml, "TBLR", 0x01u, units, 1u);
    load_method(&load_aml, "LDFD", loads, sizeof(loads));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDFD") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "Load from the field succeeds, and stores True");
    kernel_test_check(test,
                      load_integer("LDED") == 0x2Au && load_load.tables[1].origin == LOAD_ORIGIN(LOAD) &&
                          load_load.tables[1].physical_address == 0u && load_load.tables[1].length == length,
                      "the table the unit holds is copied and run");
    load_close(test);
}

/**
 * @brief LoadTable with an empty RootPath, ParameterPath and ParameterData finds a table of the
 *        directory by signature, OEM ID and OEM table ID, and runs it in place; a table the directory
 *        lacks, or one loaded already, gives False (§5.1).
 */
KERNEL_TEST(load_table_runs_a_table_of_the_directory_in_place)
{
    static const uint8_t finds[] = {0x70u, 0x5Bu, 0x1Fu, 0x0Du, 'O',   'E',   'M',  '1', 0x00u, 0x0Du, 'L', 'P', 'L',
                                    'T',   'S',   'T',   0x00u, 0x0Du, 'M',   'A',  'D', 'E',   'U',   'P', 'M', 'L',
                                    0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u, 0x00u, '\\', 'R', 'E',   'S',   '1'};
    static const uint8_t finds_any[] = {0x70u, 0x5Bu, 0x1Fu, 0x0Du, 'O',   'E',   'M',  '1', 0x00u, 0x0Du, 0x00u, 0x0Du,
                                        0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u, 0x00u, '\\', 'R', 'E',   'S',   '2'};
    static const uint8_t misses[] = {0x70u, 0x5Bu, 0x1Fu, 0x0Du, 'O',   'E',   'M',   '1',   0x00u,
                                     0x0Du, 'L',   'P',   'L',   0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u,
                                     0x0Du, 0x00u, 0x00u, '\\',  'R',   'E',   'S',   '3'};
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = NULL;

    load_begin();
    made_up_aml_name_integer(&load_aml, "RES1", LOAD_UNTOUCHED);
    made_up_aml_name_integer(&load_aml, "RES2", LOAD_UNTOUCHED);
    made_up_aml_name_integer(&load_aml, "RES3", LOAD_UNTOUCHED);
    load_method(&load_aml, "LTB1", finds, sizeof(finds));
    load_method(&load_aml, "LTB2", finds_any, sizeof(finds_any));
    load_method(&load_aml, "LTB3", misses, sizeof(misses));
    header = made_up_definition_block(load_table, "OEM1", 2u, load_loaded.bytes, load_loaded.length);
    if (!load_run(test))
        return;
    memset(&load_directory, 0, sizeof(load_directory));
    load_directory.table_count = 1u;
    memcpy(load_directory.tables[0].signature, "OEM1", 4u);
    load_directory.tables[0].length = header->length;
    load_directory.tables[0].header = header;
    load_load.directory = &load_directory;
    kernel_test_check(test, load_call("\\LTB3") == LOAD_STATUS(OK) && load_integer("RES3") == 0u,
                      "an OEM ID that only starts the table's gives False: identifiers are padded with zeros");
    kernel_test_check(test, load_load.load_tables_missing == 1u && load_load.table_count == 1u,
                      "the table found nowhere is counted, and nothing is added");
    kernel_test_check(test, load_call("\\LTB1") == LOAD_STATUS(OK) && load_integer("RES1") == LOAD_ONES,
                      "LoadTable finds OEM1 by its signature, OEM ID and OEM table ID, and gives True");
    kernel_test_check(test,
                      load_integer("LDED") == 0x2Au && load_load.tables[1].origin == LOAD_ORIGIN(LOAD_TABLE) &&
                          load_load.tables[1].header == header && load_load.tables[1].pages == 0u &&
                          load_load.table_bytes_copied == 0u,
                      "the table runs in place, from the directory's window, and is not copied");
    kernel_test_check(test,
                      load_call("\\LTB2") == LOAD_STATUS(OK) && load_integer("RES2") == 0u &&
                          load_load.load_duplicates == 1u && load_load.table_count == 2u,
                      "empty OEM identifiers match any table: OEM1 again, which the load holds, gives False");
    load_close(test);
}

/**
 * @brief Load from a Buffer, from a local, or while a table is declared, and LoadTable with a RootPath,
 *        a ParameterPath or ParameterData, fail with load_form_unsupported and store nothing (§4.5).
 */
KERNEL_TEST(the_forms_the_kernel_does_not_load_fail_with_load_form_unsupported)
{
    static const uint8_t loads_buffer[] = {0x5Bu, 0x20u, '\\', 'B', 'U', 'F', 'F', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t loads_local[] = {0x70u, '\\',  'B',  'U', 'F', 'F', 0x60u, 0x5Bu,
                                          0x20u, 0x60u, '\\', 'H', 'N', 'D', '1'};
    static const uint8_t with_root[] = {0x70u, 0x5Bu, 0x1Fu, 0x0Du, 'O',   'E',   'M',  '1',
                                        0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u, 0x0Du, '\\', 0x00u,
                                        0x0Du, 0x00u, 0x00u, '\\',  'H',   'N',   'D',  '1'};
    static const uint8_t with_parameter[] = {0x70u, 0x5Bu, 0x1Fu, 0x0Du, 'O',   'E',   'M',   '1', 0x00u,
                                             0x0Du, 0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u, 0x0Du, 'X', 'X',
                                             0x00u, 0x00u, '\\',  'H',   'N',   'D',   '1'};
    static const uint8_t with_data[] = {0x70u, 0x5Bu, 0x1Fu, 0x0Du, 'O',   'E',   'M',  '1', 0x00u, 0x0Du, 0x00u, 0x0Du,
                                        0x00u, 0x0Du, 0x00u, 0x0Du, 0x00u, 0x01u, '\\', 'H', 'N',   'D',   '1'};
    static const uint8_t at_table_level[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t buffer[] = {0x08u, 'B', 'U', 'F', 'F', 0x11u, 0x05u, 0x0Au, 0x24u, 0x53u, 0x53u};
    static const char *const methods[] = {"\\LDBF", "\\LDLC", "\\LTRT", "\\LTPP", "\\LTPD"};

    load_begin();
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_raw(&load_aml, buffer, sizeof(buffer));
    load_method(&load_aml, "LDBF", loads_buffer, sizeof(loads_buffer));
    load_method(&load_aml, "LDLC", loads_local, sizeof(loads_local));
    load_method(&load_aml, "LTRT", with_root, sizeof(with_root));
    load_method(&load_aml, "LTPP", with_parameter, sizeof(with_parameter));
    load_method(&load_aml, "LTPD", with_data, sizeof(with_data));
    made_up_aml_raw(&load_aml, at_table_level, sizeof(at_table_level));
    if (!load_run(test))
        return;
    kernel_test_check(test,
                      load_load.evaluation.statuses[LOAD_STATUS(LOAD_FORM_UNSUPPORTED)] == 1u &&
                          load_integer("LDED") == 0xDEADu,
                      "a Load while a table is declared fails, counted, and loads nothing");
    for (uint32_t index = 0u; index < sizeof(methods) / sizeof(methods[0]); ++index)
        kernel_test_check(test, load_call(methods[index]) == LOAD_STATUS(LOAD_FORM_UNSUPPORTED),
                          "a Load from a Buffer or a local, or a LoadTable with a path or data, fails with "
                          "load_form_unsupported");
    kernel_test_check(
        test, load_integer("HND1") == LOAD_UNTOUCHED && load_load.table_count == 1u && load_load.tables_loaded == 0u,
        "none stores a result or adds a table");
    load_close(test);
}

/**
 * @brief Load refuses a region of another space than SystemMemory, a table longer than its region, a
 *        region shorter than a header and a header shorter than itself, and leaves no page taken.
 */
KERNEL_TEST(a_load_refuses_what_is_not_a_whole_table_of_system_memory)
{
    static const uint8_t from_input_output[] = {0x5Bu, 0x20u, '\\', 'I', 'O', 'R', 'G', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t from_short[] = {0x5Bu, 0x20u, '\\', 'S', 'H', 'R', 'T', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t from_tiny[] = {0x5Bu, 0x20u, '\\', 'T', 'I', 'N', 'Y', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t from_cut[] = {0x5Bu, 0x20u, '\\', 'C', 'U', 'T', 'H', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t from_integer[] = {0x5Bu, 0x20u, '\\', 'H', 'N', 'D', '1', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t cut_header[MADE_UP_TABLE_HEADER] = {'S', 'S', 'D', 'T', 0x10u};
    static const struct {
        const char *path;
        AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status;
        const char *claim;
    } cases[] = {
        {"\\LDIO", LOAD_STATUS(TYPE_MISMATCH), "a SystemIO region is not loaded"                           },
        {"\\LDSH", LOAD_STATUS(REGION_BOUNDS), "a table longer than its region is not loaded"              },
        {"\\LDTN", LOAD_STATUS(REGION_BOUNDS), "a region shorter than a header is not loaded"              },
        {"\\LDCT", LOAD_STATUS(REGION_BOUNDS), "a header whose length is shorter than itself is not loaded"},
        {"\\LDIN", LOAD_STATUS(TYPE_MISMATCH), "an Integer is not loaded"                                  },
    };

    load_begin();

    const uint32_t length = load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS);

    (void) advanced_configuration_and_power_interface_simulated_host_poke(&load_host, LOAD_OTHER_ADDRESS, cut_header,
                                                                          sizeof(cut_header));
    made_up_aml_region(&load_aml, "IORG", LOAD_SYSTEM_IO, 0x80u, 0x40u);
    load_region("SHRT", LOAD_TABLE_ADDRESS, length - 1u);
    load_region("TINY", LOAD_TABLE_ADDRESS, MADE_UP_TABLE_HEADER - 1u);
    load_region("CUTH", LOAD_OTHER_ADDRESS, MADE_UP_TABLE_HEADER);
    load_method(&load_aml, "LDIO", from_input_output, sizeof(from_input_output));
    load_method(&load_aml, "LDSH", from_short, sizeof(from_short));
    load_method(&load_aml, "LDTN", from_tiny, sizeof(from_tiny));
    load_method(&load_aml, "LDCT", from_cut, sizeof(from_cut));
    load_method(&load_aml, "LDIN", from_integer, sizeof(from_integer));
    if (!load_run(test))
        return;

    const uint32_t arena_bytes = load_load.space.arena_bytes;

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        kernel_test_check(test, load_call(cases[index].path) == cases[index].status, cases[index].claim);
    kernel_test_check(test,
                      load_load.table_count == 1u && load_integer("HND1") == LOAD_UNTOUCHED &&
                          load_load.space.arena_bytes == arena_bytes,
                      "none adds a table, stores a result or keeps a page");
    load_close(test);
}

/**
 * @brief Loads stop at the caps of §6.1: the 128th table fails with limit_tables, and a table past the
 *        256 KiB Load may copy with limit_table_bytes, neither keeping a page.
 */
KERNEL_TEST(table_loads_stop_at_their_limits)
{
    static const uint8_t loads_each[] = {0x70u, 0x00u, 0x60u, 0xA2u, 0x17u, 0x95u, 0x60u, 0x0Bu, 0x00u,
                                         0x01u, 0x70u, 0x60u, '\\',  'O',   'R',   'E',   'V',   0x5Bu,
                                         0x20u, '\\',  'T',   'B',   'L',   'R',   0x61u, 0x75u, 0x60u};
    static const uint8_t loads_once[] = {0x5Bu, 0x20u, '\\', 'O', 'T', 'H', 'R', '\\', 'H', 'N', 'D', '1'};
    const MadeUpFieldUnit_t units[] = {
        {NULL,   LOAD_REVISION_BITS},
        {"OREV", 32u               },
    };

    load_begin();
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_field(&load_aml, "TBLR", 0x03u, units, 2u);
    made_up_aml_begin(&load_inner);
    made_up_aml_name_integer(&load_inner, "OTHR", 1u);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    load_method(&load_aml, "LDEA", loads_each, sizeof(loads_each));
    load_method(&load_aml, "LDON", loads_once, sizeof(loads_once));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDEA") == LOAD_STATUS(LIMIT_TABLES),
                      "loading a new table each turn fails with limit_tables");
    kernel_test_check(test,
                      load_load.table_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES &&
                          load_load.tables_loaded ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES - 1u,
                      "the load holds 128 tables: the DSDT and the 127 Load added");

    const uint32_t arena_bytes = load_load.space.arena_bytes;

    --load_load.table_count;
    advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
        &load_load.space, (void *) (uintptr_t) load_load.tables[load_load.table_count].header,
        load_load.tables[load_load.table_count].pages);
    load_load.table_bytes_copied =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLE_BYTES - MADE_UP_TABLE_HEADER;
    kernel_test_check(test,
                      load_call("\\LDON") == LOAD_STATUS(LIMIT_TABLE_BYTES) && load_integer("HND1") == LOAD_UNTOUCHED,
                      "a table longer than the bytes Load may still copy fails with limit_table_bytes");
    kernel_test_check(test,
                      load_load.space.arena_bytes +
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES ==
                          arena_bytes,
                      "and keeps no page");
    load_close(test);
}

/**
 * @brief A Load a method runs belongs to that method's evaluation: its table's declaration neither
 *        resets the While back-edges counted before it nor releases the mutexes held (§6.1).
 */
KERNEL_TEST(a_load_in_a_method_belongs_to_its_evaluation)
{
    static const uint8_t loops_around[] = {
        0x70u, 0x00u, 0x60u, 0xA2u, 0x07u, 0x95u, 0x60u, 0x0Au, 0x05u, 0x75u, 0x60u, 0x5Bu, 0x23u, '\\', 'M', 'U',
        'T',   'X',   0xFFu, 0xFFu, 0x5Bu, 0x20u, '\\',  'T',   'B',   'L',   'R',   '\\',  'H',   'N',  'D', '1',
        0xA2u, 0x07u, 0x95u, 0x60u, 0x0Au, 0x08u, 0x75u, 0x60u, 0x5Bu, 0x27u, '\\',  'M',   'U',   'T',  'X'};
    static const uint8_t mutex[] = {0x5Bu, 0x01u, 'M', 'U', 'T', 'X', 0x00u};

    load_begin();
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_raw(&load_aml, mutex, sizeof(mutex));
    load_method(&load_aml, "LPLD", loops_around, sizeof(loops_around));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LPLD") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "the method loads the table between two loops");
    kernel_test_check(test, load_load.evaluation.iterations == 8u,
                      "the evaluation counts the 8 back-edges of both loops: the Load reset none");
    kernel_test_check(test, load_load.services.released_at_end == 0u && load_load.services.releases_ignored == 0u,
                      "the mutex the method took before the Load is still held after it, and released by the method");
    load_close(test);
}

/**
 * @brief Appends Method (_REG, 2), which traces its two arguments, Arg0 then Arg1, to \TRAC.
 */
static void load_registration(MadeUpAml_t *aml)
{
    static const uint8_t body[] = {0x70u, 0x72u, 0x77u, 0x72u, 0x77u, '\\',  'T',   'R',  'A', 'C', 0x0Au, 0x10u, 0x00u,
                                   0x68u, 0x00u, 0x0Au, 0x10u, 0x00u, 0x69u, 0x00u, '\\', 'T', 'R', 'A',   'C'};

    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, body, sizeof(body));
    made_up_aml_method(aml, "_REG", 0x02u, &load_body);
}

/**
 * @brief Appends Method (_INI), which appends @p digit to \TRAC, and Method (_STA) { Return (0x0F) }.
 */
static void load_present_initialization(MadeUpAml_t *aml, uint8_t digit)
{
    const uint8_t traces[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                              0x00u, 0x0Au, digit, 0x00u, '\\', 'T', 'R', 'A', 'C'};
    static const uint8_t present[] = {0xA4u, 0x0Au, 0x0Fu};

    load_method(aml, "_STA", present, sizeof(present));
    load_method(aml, "_INI", traces, sizeof(traces));
}

/**
 * @brief Intel's pattern (§8.2): a processor's _PDC Loads its C-state table, and the HWP table only when
 *        the platform _OSC granted bit 6, which the kernel never claims. The table loaded at step 7
 *        declares a device, a child device and PCI_Config regions with a _REG in their scope: its late
 *        initialisation runs that _REG once, then the _STA and _INI of the device and of its child (§3.8).
 */
KERNEL_TEST(a_table_loaded_at_run_time_is_initialised_late)
{
    static const uint8_t platform[] = {0x8Au, 0x6Bu, 0x0Au, 0x04u, 'C', 'D', 'W', '2', 0x70u, 'C',
                                       'D',   'W',   '2',   '\\',  'O', 'S', 'C', 'P', 0xA4u, 0x6Bu};
    static const uint8_t driver[] = {0x5Bu, 0x20u, '\\',  'C',  'S', 'T', 'R',  '\\', 'H',   'C',   '0',   '_',
                                     0xA0u, 0x16u, 0x7Bu, '\\', 'O', 'S', 'C',  'P',  0x0Au, 0x40u, 0x00u, 0x5Bu,
                                     0x20u, '\\',  'H',   'W',  'P', 'R', '\\', 'H',  'W',   '0',   '_'};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "OSCP", 0u);
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "HW0_", 0u);
    made_up_aml_begin(&load_inner);
    load_present_initialization(&load_inner, 6u);
    made_up_aml_begin(&device);
    load_registration(&device);
    made_up_aml_region(&device, "PCFG", 0x02u, 0u, 0x100u);
    made_up_aml_region(&device, "PCF2", 0x02u, 0x100u, 0x100u);
    load_present_initialization(&device, 3u);
    made_up_aml_device(&device, "LCHD", &load_inner);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&load_inner);
    made_up_aml_name_integer(&load_inner, "HWPT", 1u);
    load_region("HWPR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, platform, sizeof(platform));
    made_up_aml_method(&device, "_OSC", 0x04u, &load_body);
    made_up_aml_scope(&load_aml, "_SB_", &device);
    made_up_aml_begin(&device);
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, driver, sizeof(driver));
    made_up_aml_method(&device, "_PDC", 0x01u, &load_body);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test,
                      load_integer("HC0_") == LOAD_ONES && load_integer("HW0_") == 0u &&
                          load_integer("HWPT") == 0xDEADu && load_load.tables_loaded == 1u,
                      "the _PDC loads the C-state table, never the HWP one: the platform granted nothing");
    kernel_test_check(test, load_integer("TRAC") == 0x2136u,
                      "the loaded table's scope gets one _REG (2, 1) for its two regions, then its device's _INI "
                      "runs, then its child's");
    kernel_test_check(test,
                      load_counts.late_initializations == 1u && load_counts.tables == 2u &&
                          load_counts.registrations == 1u && load_counts.processor_capabilities == 1u &&
                          load_counts.processor_capabilities_failed == 0u,
                      "one late initialisation covers the table, counted with the registration it ran");
    load_close(test);
}

/**
 * @brief A table the _CST of step 8 loads gets its late initialisation once that evaluation returns
 *        (§3.8): its device's _STA and _INI run once, and the processor publishes the state its _CST returned.
 */
KERNEL_TEST(a_table_a_cst_loads_is_initialised_late)
{
    static const uint8_t body[] = {0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',
                                   0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                   0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", body, sizeof(body));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
                      "the _CST loads its table");
    kernel_test_check(test, load_integer("TRAC") == 0x7u, "the loaded device's _INI runs once");
    kernel_test_check(test, load_counts.late_initializations == 1u && load_counts.tables == 2u,
                      "one late initialisation covers the table");
    kernel_test_check(test, load_counts.processor_idle == 1u && load_idle.slots[0].count == 1u,
                      "the processor publishes the state its _CST returned");
    load_close(test);
}

/**
 * @brief A table that a _STA step 8 reads loads gets its late initialisation once that _STA returns (§3.8),
 *        whatever the _STA says: CPU0's loads its table, then keeps CPU0 out.
 */
KERNEL_TEST(a_table_a_status_of_step_eight_loads_is_initialised_late)
{
    static const uint8_t status[] = {0x5Bu, 0x20u, '\\', 'C', 'S', 'T', 'R', '\\', 'H', 'C', '0', '_', 0xA4u, 0x00u};
    static const uint8_t idle[] = {0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                   0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", status, sizeof(status));
    load_method(&device, "_CST", idle, sizeof(idle));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test,
                      load_integer("HC0_") == LOAD_ONES && load_idle.slots[0].processor == 0u &&
                          load_counts.processor_idle_absent == 1u,
                      "CPU0's _STA loads its table and keeps CPU0 out");
    kernel_test_check(
        test, load_integer("TRAC") == 0x7u && load_counts.late_initializations == 1u && load_counts.tables == 2u,
        "one late initialisation runs the loaded device's _INI");
    load_close(test);
}

/**
 * @brief A table the _CST loads when step 9 evaluates it once more gets its late initialisation too (§3.8):
 *        the _CST notifies its own processor 0x81, and loads its table on its second run.
 */
KERNEL_TEST(a_table_a_step_nine_evaluation_loads_is_initialised_late)
{
    static const uint8_t body[] = {0x86u, '\\',  'C',   'P',   'U',   '0',   0x0Au, 0x81u, 0xA0u, 0x14u, 0x93u, '\\',
                                   'C',   'N',   'T',   '_',   0x01u, 0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',
                                   '\\',  'H',   'C',   '0',   '_',   0x75u, '\\',  'C',   'N',   'T',   '_',   0xA4u,
                                   0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u, 0x82u,
                                   0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "CNT_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", body, sizeof(body));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test,
                      load_integer("CNT_") == 2u && load_counts.processor_idle_changes == 1u &&
                          load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
                      "the _CST runs twice, and its second run, step 9's, loads the table");
    kernel_test_check(test, load_integer("TRAC") == 0x7u && load_counts.late_initializations == 1u,
                      "one late initialisation runs the loaded device's _INI");
    kernel_test_check(test, load_idle.slots[0].count == 1u, "the processor publishes the state step 9 read");
    load_close(test);
}

/**
 * @brief Writes a DSDT whose CPU0 has a _CST that loads a table declaring CPU1, with a _PDC and a _CST that
 *        append 1 and 2 to \TRAC: at the root, after CPU0, or in the scope of DEVZ, a device declared before
 *        CPU0, when @p in_device holds, whose _STA is 0 when @p absent holds; then loads it and initialises it
 *        with the CPU slots of both.
 */
static bool load_late_processor(KernelTest_t *test, bool in_device, bool absent)
{
    static const uint8_t driver[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                                     0x00u, 0x0Au, 0x01u, 0x00u, '\\', 'T', 'R', 'A', 'C'};
    static const uint8_t idle[] = {0x70u, 0x72u, 0x77u, '\\',  'T',   'R',   'A',   'C',   0x0Au, 0x10u, 0x00u,
                                   0x0Au, 0x02u, 0x00u, '\\',  'T',   'R',   'A',   'C',   0xA4u, 0x12u, 0x20u,
                                   0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u, 0x82u, 0x0Cu,
                                   0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',
                                    0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                    0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, driver, sizeof(driver));
    made_up_aml_method(&device, "_PDC", 0x01u, &load_body);
    load_method(&device, "_CST", idle, sizeof(idle));
    made_up_aml_begin(&scope);
    made_up_aml_processor(in_device ? &scope : &load_loaded, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    if (in_device)
    {
        made_up_aml_scope(&load_loaded, "DEVZ", &scope);
        made_up_aml_begin(&scope);
        made_up_aml_name_integer(&scope, absent ? "_STA" : "ZNAM", 0u);
        made_up_aml_device(&load_aml, "DEVZ", &scope);
    }
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", loads, sizeof(loads));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return false;
    load_madt.processor_count = 2u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;
    load_madt.processors[1].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER + 1u;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    return true;
}

/**
 * @brief Checks what the pass after step 8 gave CPU1, which the table CPU0's _CST loads declares: its _PDC,
 *        then its _CST, each once (§3.8).
 */
static void load_check_late_processor(KernelTest_t *test)
{
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
                      "CPU0's _CST loads the table that declares CPU1");
    kernel_test_check(test, load_integer("TRAC") == 0x12u, "CPU1's _PDC runs, then its _CST, each once");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 1u && load_counts.processor_capabilities_legacy == 1u &&
                          load_counts.processor_capabilities_failed == 0u,
                      "CPU1 is given step 7's capabilities through its _PDC");
    kernel_test_check(test,
                      load_counts.processor_idle == 2u && load_idle.slots[0].count == 1u &&
                          load_idle.slots[1].count == 1u && load_counts.processor_idle_unbound == 0u,
                      "CPU0 and CPU1 publish their states");
}

/**
 * @brief A processor that a table step 8 loads declares after the processor whose _CST loads it is left to
 *        the pass after step 8, which gives it step 7's capabilities before its _CST (§3.8).
 */
KERNEL_TEST(a_processor_a_cst_loads_gets_its_capabilities_then_its_cst)
{
    if (!load_late_processor(test, false, false))
        return;
    load_check_late_processor(test);
    load_close(test);
}

/**
 * @brief A processor that a table step 8 loads declares below a device step 8 has gone past publishes its
 *        _CST in the pass after step 8, as Linux attaches it when it scans the namespace again (§3.8).
 */
KERNEL_TEST(a_processor_a_cst_loads_below_a_device_step_eight_passed_publishes)
{
    if (!load_late_processor(test, true, false))
        return;
    load_check_late_processor(test);
    load_close(test);
}

/**
 * @brief The pass after step 8 decides the presence of the processors it handles as step 8 does: CPU1, below
 *        DEVZ, whose _STA is 0, is kept out, though step 7's capabilities reach it (§3.8, §3.10).
 */
KERNEL_TEST(a_processor_a_cst_loads_below_an_absent_device_is_kept_out)
{
    if (!load_late_processor(test, true, true))
        return;
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_idle.slots[0].count == 1u,
                      "CPU0's _CST loads the table that declares CPU1, and CPU0 publishes");
    kernel_test_check(test, load_integer("TRAC") == 0x1u && load_counts.processor_capabilities == 1u,
                      "CPU1's _PDC runs, as step 7 runs it whatever _STA says, and its _CST does not");
    kernel_test_check(test, load_idle.slots[1].processor == 0u && load_counts.processor_idle_absent == 1u,
                      "CPU1 is kept out, counted absent");
    load_close(test);
}

/**
 * @brief A round of the pass after step 8 reads again the _STA of the devices above its processors, as
 *        Linux's scan after a table loads reads them again (§3.10): step 8 reads DEVB present, then the _CST
 *        of CPU0, below it, sets the flag that makes DEVB absent and loads the table that declares CPU1 below
 *        DEVB, which is kept out.
 */
KERNEL_TEST(a_round_of_the_pass_reads_again_the_devices_above_its_processors)
{
    static const uint8_t status[] = {0xA0u, 0x08u, '\\', 'F', 'L', 'A', 'G', 0xA4u, 0x00u, 0xA4u, 0x0Au, 0x0Fu};
    static const uint8_t hides[] = {0x70u, 0x01u, '\\',  'F',   'L',   'A',   'G',   0x5Bu, 0x20u, '\\',  'C',
                                    'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',   0xA4u, 0x12u, 0x20u,
                                    0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u, 0x82u, 0x0Cu,
                                    0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static const uint8_t traced[] = {0x70u, 0x72u, 0x77u, '\\',  'T',   'R',   'A',   'C',   0x0Au, 0x10u, 0x00u,
                                     0x0Au, 0x02u, 0x00u, '\\',  'T',   'R',   'A',   'C',   0xA4u, 0x12u, 0x20u,
                                     0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u, 0x82u, 0x0Cu,
                                     0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                     0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "FLAG", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", traced, sizeof(traced));
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_scope(&load_loaded, "DEVB", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", hides, sizeof(hides));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", status, sizeof(status));
    made_up_aml_processor(&scope, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    made_up_aml_device(&load_aml, "DEVB", &scope);
    if (!load_run(test))
        return;
    load_madt.processor_count = 2u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;
    load_madt.processors[1].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER + 1u;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test,
                      load_integer("FLAG") == 1u && load_integer("HC0_") == LOAD_ONES && load_idle.slots[0].count == 1u,
                      "CPU0 publishes, its _CST having set FLAG and loaded the table of CPU1");
    kernel_test_check(test,
                      load_idle.slots[1].processor == 0u && load_counts.processor_idle_absent == 1u &&
                          load_integer("TRAC") == 0u,
                      "CPU1 is kept out, its _CST never read: DEVB is read again, absent");
    load_close(test);
}

/**
 * @brief The pass after step 8 goes on while the tables it loads declare processor objects (§3.8): CPU0's
 *        _CST loads the table of CPU1, whose _CST loads the table of CPU2; each publishes once.
 */
KERNEL_TEST(the_pass_after_step_eight_goes_on_while_its_tables_declare_processors)
{
    static const uint8_t first[] = {0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',
                                    0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                    0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                    0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static const uint8_t second[] = {0x5Bu, 0x20u, '\\',  'O',   'T',   'H',   'R',   '\\',  'H',   'C',   '1',   '_',
                                     0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                     0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                     0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static const uint8_t last[] = {0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                   0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "HC1_", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", second, sizeof(second));
    made_up_aml_processor(&load_loaded, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&load_inner);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", last, sizeof(last));
    made_up_aml_processor(&load_inner, "CPU2", LOAD_PROCESSOR_IDENTIFIER + 2u, 0u, 0u, &device);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", first, sizeof(first));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 3u;
    for (uint32_t slot = 0u; slot < 3u; ++slot)
        load_madt.processors[slot].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER + slot;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(
        test, load_integer("HC0_") == LOAD_ONES && load_integer("HC1_") == LOAD_ONES && load_load.tables_loaded == 2u,
        "CPU0's _CST loads CPU1's table, and CPU1's loads CPU2's");
    kernel_test_check(test,
                      load_counts.processor_idle == 3u && load_counts.processor_idle_unbound == 0u &&
                          load_idle.slots[0].count == 1u && load_idle.slots[1].count == 1u &&
                          load_idle.slots[2].count == 1u,
                      "the three processors publish their states, each _CST read once");
    load_close(test);
}

/**
 * @brief A _CST that loads a table, then fails, is the first failure, before the _INI of the loaded device
 *        that fails in the late initialisation its return starts (§3.8): the _CST loads, then divides by
 *        zero; the loaded _INI reads a name that does not exist.
 */
KERNEL_TEST(a_cst_that_loads_a_table_then_fails_is_the_first_failure)
{
    static const uint8_t body[] = {0x5Bu, 0x20u, '\\', 'C',   'S',   'T',   'R',   '\\',  'H',
                                   'C',   '0',   '_',  0xA4u, 0x78u, 0x01u, 0x00u, 0x00u, 0x00u};
    static const uint8_t present[] = {0xA4u, 0x0Au, 0x0Fu};
    static const uint8_t missing[] = {0x70u, '\\', 'Z', 'Z', 'Z', 'Z', 0x60u};
    static MadeUpAml_t device;
    char path[12];

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_STA", present, sizeof(present));
    load_method(&device, "_INI", missing, sizeof(missing));
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", body, sizeof(body));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test,
                      load_integer("HC0_") == LOAD_ONES && load_counts.late_initializations == 1u &&
                          load_counts.initialization_failed == 1u && load_counts.processor_idle_failed == 1u,
                      "the _CST loads its table and fails, then the loaded _INI fails");
    kernel_test_check(test,
                      load_counts.first_failure == LOAD_STATUS(DIVIDE_BY_ZERO) &&
                          advanced_configuration_and_power_interface_machine_language_namespace_path(
                              &load_load.space, load_counts.first_failure_node, path, sizeof(path)) == 10u &&
                          memcmp(path, "\\CPU0._CST", 11u) == 0,
                      "the first failure is the _CST's, divide_by_zero");
    load_close(test);
}

/**
 * @brief Step 8 and the late initialisation decide the presence of a device each for itself (§3.8, §3.10):
 *        step 8 reads the _STA of DEVB, above its processor, which step 4 left unread, since nothing below it
 *        holds an _INI; the table that processor's _CST loads declares a device under DEVB, and its late
 *        initialisation reads DEVB's _STA as step 4 would, then runs the device's _INI.
 */
KERNEL_TEST(step_eight_and_the_late_initialisation_each_decide_a_device)
{
    static const uint8_t counted[] = {0x75u, '\\', 'B', 'S', 'T', 'A', 0xA4u, 0x0Au, 0x0Fu};
    static const uint8_t body[] = {0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',
                                   0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                   0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "BSTA", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&scope);
    made_up_aml_device(&scope, "LDEV", &device);
    made_up_aml_scope(&load_loaded, "DEVB", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", body, sizeof(body));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", counted, sizeof(counted));
    made_up_aml_processor(&scope, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    made_up_aml_device(&load_aml, "DEVB", &scope);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES, "the _CST of DEVB's processor loads its table");
    kernel_test_check(test, load_integer("BSTA") == 2u,
                      "DEVB's _STA is read twice: once by step 8, once by the late initialisation");
    kernel_test_check(test, load_integer("TRAC") == 0x7u && load_counts.late_initializations == 1u,
                      "the late initialisation runs the loaded device's _INI");
    kernel_test_check(test, load_idle.slots[0].count == 1u, "the processor publishes its state");
    load_close(test);
}

/**
 * @brief The late initialisation decides the presence of a processor step 8 decided, for itself (§3.8,
 *        §3.10): CPU1, declared first, is found absent by step 8, and CPU0 present; the table CPU0's _CST
 *        loads declares a device under each. The late initialisation reads CPU0's _STA and CPU1's again,
 *        initialises the device under CPU0 and leaves the one under CPU1 alone.
 */
KERNEL_TEST(the_late_initialisation_decides_a_processor_step_eight_decided)
{
    static const uint8_t present[] = {0x75u, '\\', 'B', 'S', 'T', 'A', 0xA4u, 0x0Au, 0x0Fu};
    static const uint8_t absent[] = {0x75u, '\\', 'A', 'S', 'T', 'A', 0xA4u, 0x00u};
    static const uint8_t integer[] = {0xA4u, 0x01u};
    static const uint8_t body[] = {0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',
                                   0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                   0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "BSTA", 0u);
    made_up_aml_name_integer(&load_aml, "ASTA", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&scope);
    made_up_aml_device(&scope, "LDEV", &device);
    made_up_aml_scope(&load_loaded, "CPU0", &scope);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 8u);
    made_up_aml_begin(&scope);
    made_up_aml_device(&scope, "LDV2", &device);
    made_up_aml_scope(&load_loaded, "CPU1", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", absent, sizeof(absent));
    load_method(&device, "_CST", integer, sizeof(integer));
    made_up_aml_processor(&load_aml, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_begin(&device);
    load_method(&device, "_STA", present, sizeof(present));
    load_method(&device, "_CST", body, sizeof(body));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 2u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;
    load_madt.processors[1].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER + 1u;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES, "CPU0's _CST loads its table");
    kernel_test_check(test, load_integer("BSTA") == 2u && load_integer("ASTA") == 2u,
                      "CPU0's _STA and CPU1's are read twice: once by step 8, once by the late initialisation");
    kernel_test_check(test, load_integer("TRAC") == 0x7u,
                      "the device under CPU0 is initialised, the one under CPU1 left alone");
    kernel_test_check(test, load_counts.processor_idle_absent == 1u && load_idle.slots[0].count == 1u,
                      "CPU1 is kept out, CPU0 publishes its state");
    load_close(test);
}

/**
 * @brief The absence step 4 found still keeps the late initialisation out where step 8, reading the _STA
 *        again once step 4 is over, finds the device present (§3.8, §3.10): DEVB's _STA reads FLAG, which
 *        LATE's _INI sets after step 4 read DEVB absent. CPU0, below DEVB, publishes, and the device under
 *        DEVB that its _CST loads is left alone.
 */
KERNEL_TEST(the_late_initialisation_keeps_the_absence_step_four_found)
{
    static const uint8_t status[] = {0x75u, '\\', 'B', 'S', 'T',   'A',   0xA0u, 0x09u, '\\',
                                     'F',   'L',  'A', 'G', 0xA4u, 0x0Au, 0x0Fu, 0xA4u, 0x00u};
    static const uint8_t nothing[] = {0xA3u};
    static const uint8_t sets[] = {0x70u, 0x01u, '\\', 'F', 'L', 'A', 'G'};
    static const uint8_t body[] = {0x5Bu, 0x20u, '\\',  'C',   'S',   'T',   'R',   '\\',  'H',   'C',   '0',   '_',
                                   0xA4u, 0x12u, 0x20u, 0x02u, 0x01u, 0x12u, 0x1Cu, 0x04u, 0x11u, 0x14u, 0x0Au, 0x11u,
                                   0x82u, 0x0Cu, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                   0x00u, 0x00u, 0x00u, 0x79u, 0x00u, 0x01u, 0x01u, 0x0Bu, 0xE8u, 0x03u};
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "FLAG", 0u);
    made_up_aml_name_integer(&load_aml, "BSTA", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&scope);
    made_up_aml_device(&scope, "LDEV", &device);
    made_up_aml_scope(&load_loaded, "DEVB", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", body, sizeof(body));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", status, sizeof(status));
    load_method(&scope, "_INI", nothing, sizeof(nothing));
    made_up_aml_processor(&scope, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    made_up_aml_device(&load_aml, "DEVB", &scope);
    made_up_aml_begin(&scope);
    load_method(&scope, "_INI", sets, sizeof(sets));
    made_up_aml_device(&load_aml, "LATE", &scope);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("FLAG") == 1u && load_integer("BSTA") == 2u,
                      "DEVB's _STA is read by step 4, absent, then by step 8, present");
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_idle.slots[0].count == 1u,
                      "CPU0's _CST loads its table, and CPU0 publishes its state");
    kernel_test_check(test, load_integer("TRAC") == 0u && load_counts.late_initializations == 0u,
                      "the device loaded under DEVB is left alone: step 4 found DEVB absent");
    load_close(test);
}

/**
 * @brief A table \_SB._INI loads, before step 3, is initialised with the static ones: step 4 runs the
 *        _INI of its device, and step 7 gives its processor the capabilities, no late initialisation
 *        needed (§3.8).
 */
KERNEL_TEST(a_table_loaded_before_step_three_is_initialised_with_the_static_ones)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'E', 'A', 'R', 'L', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t keeps[] = {0x9Du, 0x68u, '\\', 'E', 'P', 'D', 'C'};
    static const uint8_t identifies[] = {0x08u, '_', 'H', 'I',   'D',   0x0Du, 'A', 'C', 'P', 'I',  '0',
                                         '0',   '0', '7', 0x00u, 0x08u, '_',   'U', 'I', 'D', 0x01u};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "EPDC", 0u);
    made_up_aml_begin(&device);
    made_up_aml_raw(&device, identifies, sizeof(identifies));
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, keeps, sizeof(keeps));
    made_up_aml_method(&device, "_PDC", 0x01u, &load_body);
    load_present_initialization(&device, 4u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "EPRC", &device);
    made_up_aml_scope(&load_loaded, "_SB_", &load_inner);
    load_region("EARL", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_INI", loads, sizeof(loads));
    made_up_aml_scope(&load_aml, "_SB_", &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 1u;
    load_madt.processors[0].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HND1") == LOAD_ONES && load_integer("TRAC") == 4u,
                      "\\_SB._INI loads the table, and step 4 runs its device's _INI once");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 1u && load_counts.processor_capabilities_legacy == 1u &&
                          load_counts.processor_capabilities_failed == 0u &&
                          made_up_root_node(&load_load, "EPDC")->value.kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                      "step 7 finds its processor, and hands it the capabilities through its _PDC");
    kernel_test_check(test, load_counts.late_initializations == 0u && load_counts.tables == 2u,
                      "no late initialisation runs: the table came before step 3");
    load_close(test);
}

/**
 * @brief A table a _REG of step 3 loads gets the _REG of its scope once step 3 is over, and step 3
 *        does not reach its region again; its devices, and those of a table an _INI of step 4 loads, lie
 *        where step 4 has not reached yet, and step 4 initialises each once when it reaches it (§3.8).
 */
KERNEL_TEST(tables_loaded_during_steps_three_and_four_are_initialised_once)
{
    static const uint8_t registers[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', '3', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t initializes[] = {0x70u, 0x72u, 0x77u, '\\', 'T', 'R',  'A', 'C', 0x0Au, 0x10u,
                                          0x00u, 0x01u, 0x00u, '\\', 'T', 'R',  'A', 'C', 0x5Bu, 0x20u,
                                          '\\',  'T',   'B',   'L',  '4', '\\', 'H', 'N', 'D',   '2'};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HND2", LOAD_UNTOUCHED);
    made_up_aml_begin(&device);
    load_registration(&device);
    made_up_aml_region(&device, "CPCI", 0x02u, 0u, 0x100u);
    load_present_initialization(&device, 3u);
    made_up_aml_device(&load_loaded, "CDEV", &device);
    load_region("TBL3", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_present_initialization(&device, 5u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "EDEV", &device);
    load_region("TBL4", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    made_up_aml_region(&device, "APCI", 0x02u, 0u, 0x100u);
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, registers, sizeof(registers));
    made_up_aml_method(&device, "_REG", 0x02u, &load_body);
    made_up_aml_device(&load_aml, "ADEV", &device);
    made_up_aml_begin(&device);
    load_method(&device, "_INI", initializes, sizeof(initializes));
    made_up_aml_device(&load_aml, "BDEV", &device);
    if (!load_run(test))
        return;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HND1") == LOAD_ONES && load_integer("HND2") == LOAD_ONES,
                      "ADEV's _REG loads the first table at step 3, BDEV's _INI the second at step 4");
    kernel_test_check(test, load_integer("TRAC") == 0x21135u,
                      "CDEV's scope gets one _REG after step 3; step 4 then runs BDEV's _INI, CDEV's, then EDEV's, "
                      "each once");
    kernel_test_check(test,
                      load_counts.late_initializations == 1u && load_counts.tables == 3u &&
                          load_counts.registrations == 2u && load_counts.devices == 6u,
                      "one late initialisation runs a registration, and every device is counted");
    load_close(test);
}

/**
 * @brief A table Load adds whose table-level code runs a Load of its own is declared on one level only:
 *        that inner Load fails with load_form_unsupported and stores nothing, the declarations after it
 *        are made, and a method of the table that loads the other table later succeeds (§3.6).
 */
KERNEL_TEST(a_load_the_code_of_a_loaded_table_runs_while_it_is_declared_is_refused)
{
    static const uint8_t loads_outer[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'A', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t loads_inner[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', '2', '\\', 'H', 'N', 'D', '2'};

    load_begin();
    made_up_aml_name_integer(&load_aml, "HND2", LOAD_UNTOUCHED);
    made_up_aml_begin(&load_inner);
    made_up_aml_name_integer(&load_inner, "SCND", 2u);
    load_region("TBL2", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_raw(&load_loaded, loads_inner, sizeof(loads_inner));
    made_up_aml_name_integer(&load_loaded, "AFTR", 7u);
    load_method(&load_loaded, "LDB2", loads_inner, sizeof(loads_inner));
    load_region("TBLA", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "LDA2", loads_outer, sizeof(loads_outer));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDA2") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "the outer Load succeeds and stores True");
    kernel_test_check(test,
                      load_load.evaluation.statuses[LOAD_STATUS(LOAD_FORM_UNSUPPORTED)] == 1u &&
                          load_integer("HND2") == LOAD_UNTOUCHED && load_integer("SCND") == 0xDEADu,
                      "the Load its table-level code runs fails with load_form_unsupported, and loads nothing");
    kernel_test_check(test, load_integer("AFTR") == 7u, "the declarations after that Load are made");
    kernel_test_check(test,
                      load_call("\\LDB2") == LOAD_STATUS(OK) && load_integer("HND2") == LOAD_ONES &&
                          load_integer("SCND") == 2u && load_load.table_count == 3u,
                      "a method of the loaded table loads the other table later");
    load_close(test);
}

/**
 * @brief A device a table loaded at step 4 declares under a device step 4 found neither present nor
 *        functioning is left alone by the late initialisation, as step 4 left that subtree (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_an_absent_one_is_left_alone)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t traces_nine[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                                          0x00u, 0x0Au, 0x09u, 0x00u, '\\', 'T', 'R', 'A', 'C'};
    static const uint8_t absent[] = {0xA4u, 0x00u};
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "LATE", &device);
    made_up_aml_scope(&load_loaded, "GONE", &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", absent, sizeof(absent));
    load_method(&device, "_INI", traces_nine, sizeof(traces_nine));
    made_up_aml_device(&load_aml, "GONE", &device);
    made_up_aml_begin(&device);
    load_method(&device, "_INI", loads, sizeof(loads));
    made_up_aml_device(&load_aml, "BDEV", &device);
    if (!load_run(test))
        return;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "BDEV's _INI loads the table at step 4");
    kernel_test_check(test,
                      load_integer("TRAC") == 0u && load_counts.status_evaluations == 1u &&
                          load_counts.late_initializations == 0u,
                      "neither GONE's _INI nor its loaded child's _STA and _INI run");
    kernel_test_check(test, load_counts.devices == 5u, "the loaded device is counted all the same");
    load_close(test);
}

/**
 * @brief Builds the made-up machine of the tests below: Device (GONE) whose _STA returns @p status and whose
 *        _INI traces 9, Device (BDEV) whose _INI loads a table, and Device (ZDEV), present, whose _INI traces 5,
 *        in the order @p loader_first says, BDEV first or GONE first; the table declares Device (LATE) in
 *        @p scope, present, whose _INI traces 7. Then initialises the namespace.
 */
static bool load_absent_machine(KernelTest_t *test, uint8_t status, bool loader_first, const char *scope)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t traces_nine[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                                          0x00u, 0x0Au, 0x09u, 0x00u, '\\', 'T', 'R', 'A', 'C'};
    static MadeUpAml_t device;
    static MadeUpAml_t loader;
    static MadeUpAml_t gone;
    const uint8_t returns[] = {0xA4u, 0x0Au, status};

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "LATE", &device);
    made_up_aml_scope(&load_loaded, scope, &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&gone);
    load_method(&gone, "_STA", returns, sizeof(returns));
    load_method(&gone, "_INI", traces_nine, sizeof(traces_nine));
    made_up_aml_begin(&loader);
    load_method(&loader, "_INI", loads, sizeof(loads));
    if (loader_first)
        made_up_aml_device(&load_aml, "BDEV", &loader);
    made_up_aml_device(&load_aml, "GONE", &gone);
    if (!loader_first)
        made_up_aml_device(&load_aml, "BDEV", &loader);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 5u);
    made_up_aml_device(&load_aml, "ZDEV", &device);
    if (!load_run(test))
        return false;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    if (kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "BDEV's _INI loads the table at step 4"))
        return true;
    load_close(test);
    return false;
}

/**
 * @brief A device a table loaded at step 4 declares under a device step 4 has not reached yet is left to
 *        step 4, which finds that device absent and leaves both alone (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_one_step_four_reaches_later_is_left_to_it)
{
    if (!load_absent_machine(test, 0x00u, true, "GONE"))
        return;
    kernel_test_check(test,
                      load_integer("TRAC") == 5u && load_counts.status_evaluations == 2u &&
                          load_counts.late_initializations == 0u,
                      "only ZDEV's _INI runs: GONE is absent, and LATE waits for step 4, which never reaches it");
    load_close(test);
}

/**
 * @brief A device found not present but functioning is not marked absent: a device a table loaded later
 *        declares below it gets its _STA and _INI (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_a_functioning_one_is_initialised)
{
    if (!load_absent_machine(test, 0x08u, false, "GONE"))
        return;
    kernel_test_check(test,
                      load_integer("TRAC") == 0x75u && load_counts.status_evaluations == 3u &&
                          load_counts.late_initializations == 1u,
                      "GONE's _INI does not run, but LATE's does, late, before ZDEV's");
    load_close(test);
}

/**
 * @brief A device a table loaded at step 4 declares under a present device step 4 reaches later runs its
 *        _INI once, when step 4 reaches it, never also at the late initialisation (§3.8).
 */
KERNEL_TEST(no_initialization_runs_twice)
{
    if (!load_absent_machine(test, 0x0Fu, true, "ZDEV"))
        return;
    kernel_test_check(test, load_integer("TRAC") == 0x957u && load_counts.late_initializations == 0u,
                      "GONE's, ZDEV's then LATE's _INI each run once, all at step 4");
    load_close(test);
}

/**
 * @brief Builds Device (@p first) and Device (@p second) under Device (PARE), present, with an _INI that
 *        traces 9 in SIBA and one that loads a table in BDEV, then Device (ZDEV), present, tracing 5; the
 *        table declares Device (LATE), present, tracing 7, in @p scope. Then initialises the namespace.
 *
 * @return false, the load closed, when the table was not loaded.
 */
static bool load_sibling_machine(KernelTest_t *test, const char *first, const char *second, const char *scope)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static MadeUpAml_t device;
    static MadeUpAml_t parent;
    static MadeUpAml_t scope_body;

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&scope_body);
    made_up_aml_device(&scope_body, "LATE", &device);
    made_up_aml_begin(&load_inner);
    made_up_aml_scope(&load_inner, scope, &scope_body);
    made_up_aml_scope(&load_loaded, "PARE", &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&parent);
    for (uint32_t index = 0u; index < 2u; ++index)
    {
        const char *name = (index == 0u) ? first : second;

        made_up_aml_begin(&device);
        if (memcmp(name, "BDEV", 4u) == 0)
            load_method(&device, "_INI", loads, sizeof(loads));
        else
            load_present_initialization(&device, 9u);
        made_up_aml_device(&parent, name, &device);
    }
    made_up_aml_device(&load_aml, "PARE", &parent);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 5u);
    made_up_aml_device(&load_aml, "ZDEV", &device);
    if (!load_run(test))
        return false;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    if (kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "BDEV's _INI loads the table at step 4"))
        return true;
    load_close(test);
    return false;
}

/**
 * @brief A device loaded under a sibling step 4 has walked, below the root, is initialised late: the
 *        lowest node above it and the device step 4 is at is their parent, not the root (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_a_sibling_step_four_walked_is_initialised_late)
{
    if (!load_sibling_machine(test, "SIBA", "BDEV", "SIBA"))
        return;
    kernel_test_check(test, load_integer("TRAC") == 0x975u && load_counts.late_initializations == 1u,
                      "SIBA's _INI, then LATE's, late, then ZDEV's");
    load_close(test);
}

/**
 * @brief A device loaded under the device whose _INI loads it is left to step 4, which descends into that
 *        device next: its _INI runs once (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_the_loader_runs_its_initialization_once)
{
    if (!load_sibling_machine(test, "SIBA", "BDEV", "BDEV"))
        return;
    kernel_test_check(test, load_integer("TRAC") == 0x975u && load_counts.late_initializations == 0u,
                      "SIBA's _INI, then LATE's once, at step 4, then ZDEV's");
    load_close(test);
}

/**
 * @brief Builds Device (NOIN), whose _STA returns 0 and which has no _INI, before or after Device (BDEV),
 *        whose _INI loads a table declaring Device (\NOIN.LATE), present, tracing 7, then Device (ZDEV),
 *        present, tracing 5; initialises the namespace and checks the result is the same either way.
 */
static void load_skipped_absent(KernelTest_t *test, bool absent_first)
{
    static const uint8_t loads[] = {0x5Bu, 0x20u, '\\', 'T', 'B', 'L', 'R', '\\', 'H', 'N', 'D', '1'};
    static const uint8_t absent[] = {0xA4u, 0x00u};
    static MadeUpAml_t device;
    static MadeUpAml_t loader;

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "LATE", &device);
    made_up_aml_scope(&load_loaded, "NOIN", &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", absent, sizeof(absent));
    made_up_aml_begin(&loader);
    load_method(&loader, "_INI", loads, sizeof(loads));
    if (absent_first)
        made_up_aml_device(&load_aml, "NOIN", &device);
    made_up_aml_device(&load_aml, "BDEV", &loader);
    if (!absent_first)
        made_up_aml_device(&load_aml, "NOIN", &device);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 5u);
    made_up_aml_device(&load_aml, "ZDEV", &device);
    if (!load_run(test))
        return;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "BDEV's _INI loads the table at step 4");
    kernel_test_check(test, load_integer("TRAC") == 5u && load_counts.status_evaluations == 2u,
                      "NOIN's _STA is read once, says absent, and LATE is left alone: only ZDEV's _INI runs");
    load_close(test);
}

/**
 * @brief A device loaded under an absent one without _INI declared after the loader is left alone: step 4
 *        reads that device's _STA when it reaches it (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_an_absent_one_step_four_reaches_later_is_left_alone)
{
    load_skipped_absent(test, false);
}

/**
 * @brief A device loaded under an absent one without _INI declared before the loader, which step 4 skipped
 *        without reading its _STA, is left alone all the same: the late initialisation reads that _STA
 *        (§3.8).
 */
KERNEL_TEST(a_device_loaded_under_an_absent_one_step_four_skipped_is_left_alone) { load_skipped_absent(test, true); }
