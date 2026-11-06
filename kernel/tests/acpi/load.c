#include "fixtures/load.h"
#include "fixtures/common.h"
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
#define LOAD_FULL_DEPTH                                                                                                \
    62u /**< The Arg0 of REC_ that fills the evaluation's 64 frames: the host's, then 63 of REC_'s. */
#define LOAD_OPERATOR_CAP_ADDS                                                                                         \
    252u /**< The Adds of LDIT that start its loaded table's frame at the 255th of the 256 operators (§6.1). */
#define LOAD_BLOCK_CAP_IFS                                                                                             \
    509u /**< The If (One) of LDIT that start its loaded table's frame at the 512th of the 512 blocks (§6.1). */

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
static MadeUpAml_t load_long_method;
static uint8_t load_long_aml[2u * MADE_UP_AML_CAPACITY];
static uint8_t load_long_dsdt[MADE_UP_TABLE_HEADER + 2u * MADE_UP_AML_CAPACITY];

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
    static const uint8_t zero[MADE_UP_TABLE_HEADER + 64u] = {0};
    static MadeUpAml_t device;

    load_begin();
    load_method(&load_loaded, "LMTH", load_returns_seventy_seven_aml, sizeof(load_returns_seventy_seven_aml));

    const uint32_t length = load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS);

    load_region("TBLR", LOAD_TABLE_ADDRESS, length);
    made_up_aml_name_integer(&load_aml, "HND2", LOAD_UNTOUCHED);
    made_up_aml_begin(&device);
    load_method(&device, "LDIT", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    load_method(&device, "LDAG", load_runs_copy_loads_again_aml, sizeof(load_runs_copy_loads_again_aml));
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
 * @brief A table Load adds to a load that skips bodies, as a release kernel's (O2, §3.6), is declared, its method
 *        undecoded: LMTH exists and runs, and the table counts no term; the same Load on a load that decodes bodies
 *        counts them.
 */
KERNEL_TEST(a_table_load_adds_while_bodies_are_skipped_is_declared_undecoded)
{
    for (uint32_t skips = 0u; skips < 2u; ++skips)
    {
        load_begin();
        load_method(&load_loaded, "LMTH", load_returns_seventy_seven_aml, sizeof(load_returns_seventy_seven_aml));
        load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
        load_method(&load_aml, "LDIT", load_loads_table_into_first_handle_aml,
                    sizeof(load_loads_table_into_first_handle_aml));
        if (!load_run(test))
            return;
        load_load.skips_bodies = skips != 0u;
        kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_load.table_count == 2u,
                          "LDIT loads the table");
        kernel_test_check(test, load_call("\\LMTH") == LOAD_STATUS(OK), "the loaded LMTH runs");
        kernel_test_check(test, (load_load.tables[1].terms == 0u) == (skips != 0u),
                          skips ? "the loaded table counts no term: its body is not decoded" :
                                  "the loaded table counts its terms: its body is decoded");
        load_close(test);
    }
}

/**
 * @brief The data a table Load adds keeps in its table (O20) is checked once that table is decoded, as the boot's
 *        tables' are: `Name (PKFL, Package (1) {...})`, which holds the byte 0x02, which starts no data object, is
 *        kept and counted in data_faults, once, the DSDT's own `Name (PKBT, Package (1) {...})` of the same byte not
 *        counted again; a load that skips bodies, as a release kernel's (O2, §3.6), keeps it and counts nothing. The
 *        DSDT's method TMPM declares two Names, whose nodes the decoding pass of its body gives back, so that PKFL
 *        stands on a node given back, below the nodes the load held before the Load.
 *
 * Its Packages are raw bytes: iasl does not emit an element that is the byte 0x02, which opens no term.
 */
KERNEL_TEST(a_table_load_adds_has_its_kept_data_checked_once_decoded)
{
    static const uint8_t faulty[] = {0x08u, 'P', 'K', 'F', 'L', 0x12u, 0x03u, 0x01u, 0x02u};
    static const uint8_t faulty_at_boot[] = {0x08u, 'P', 'K', 'B', 'T', 0x12u, 0x03u, 0x01u, 0x02u};
    uint32_t reused[2] = {0u, 0u};
    uint32_t counted[2] = {0u, 0u};
    uint32_t kept[2] = {0u, 0u};

    for (uint32_t skips = 0u; skips < 2u; ++skips)
    {
        load_begin();
        made_up_aml_raw(&load_loaded, faulty, sizeof(faulty));
        load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
        load_method(&load_aml, "LDIT", load_loads_table_into_first_handle_aml,
                    sizeof(load_loads_table_into_first_handle_aml));
        made_up_aml_raw(&load_aml, faulty_at_boot, sizeof(faulty_at_boot));
        load_method(&load_aml, "TMPM", load_table_load_adds_temporaries_aml,
                    sizeof(load_table_load_adds_temporaries_aml));
        if (!load_run(test))
            return;
        kernel_test_check(test, load_load.data_faults == 1u, "the boot's decoding pass counts PKBT");
        load_load.skips_bodies = skips != 0u;

        const uint32_t faults = load_load.data_faults;
        const uint32_t data_kept = load_load.data_kept;
        const uint32_t nodes = load_load.space.node_count;
        uint32_t pkfl = 0u;

        kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_load.table_count == 2u,
                          "LDIT loads the table");
        counted[skips] = load_load.data_faults - faults;
        kept[skips] = load_load.data_kept - data_kept;
        pkfl = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            &load_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
            advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "PKFL", 0u));
        reused[skips] = (pkfl != 0u && pkfl < nodes) ? 1u : 0u;
        load_close(test);
    }
    kernel_test_check(test, kept[0] == 1u && kept[1] == 1u && reused[0] == 1u && reused[1] == 1u,
                      "the loaded table keeps PKFL either way, on a node the decoding pass gave back");
    kernel_test_check(test, counted[0] == 1u && counted[1] == 0u,
                      "decoding the loaded table counts PKFL in data_faults; skipping its bodies counts nothing");
    kernel_test_measure(test, "data_faults", counted[0]);
}

/**
 * @brief A kept package that a loaded table's code reads before the check of kept data runs is counted at that read,
 *        and the check counts the others (O20, §3.6): the table Load adds keeps PKFL and PKF2, each of the byte 0x02,
 *        which starts no data object, and PKOK, `Package (1) {One}`, then runs `If (LEqual (SizeOf (PKFL), One)) {}`;
 *        data_faults_read rises by 1, for PKFL, data_faults by 1, for PKF2, and the three are kept.
 *
 * Its table is raw bytes: iasl does not emit a Package element that is the byte 0x02, which opens no term.
 */
KERNEL_TEST(a_kept_package_a_loaded_table_reads_first_is_counted_at_that_read)
{
    static const uint8_t code[] = {0x08u, 'P',   'K',   'F',   'L',   0x12u, 0x03u, 0x01u, 0x02u, 0x08u, 'P', 'K',
                                   'F',   '2',   0x12u, 0x03u, 0x01u, 0x02u, 0x08u, 'P',   'K',   'O',   'K', 0x12u,
                                   0x03u, 0x01u, 0x01u, 0xA0u, 0x08u, 0x93u, 0x87u, 'P',   'K',   'F',   'L', 0x01u};

    load_begin();
    made_up_aml_raw(&load_loaded, code, sizeof(code));
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "LDIT", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    if (!load_run(test))
        return;

    const uint32_t kept = load_load.data_kept;
    const uint32_t faults = load_load.data_faults;
    const uint32_t faults_read = load_load.data_faults_read;

    kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_load.table_count == 2u,
                      "LDIT loads the table");
    kernel_test_check(test,
                      load_load.data_kept - kept == 3u && load_load.data_faults_read - faults_read == 1u &&
                          load_load.data_faults - faults == 1u,
                      "the read counts PKFL, the check PKF2, once each");
    load_close(test);
}

/**
 * @brief Load from a field unit reads the unit as a Buffer and loads the table it holds (§5.1).
 */
KERNEL_TEST(a_load_from_a_field_reads_the_unit_as_a_buffer)
{
    load_begin();

    const uint32_t length = load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS);
    const MadeUpFieldUnit_t units[] = {
        {"TABL", 8u * length},
    };

    load_region("TBLR", LOAD_TABLE_ADDRESS, length);
    made_up_aml_field(&load_aml, "TBLR", 0x01u, units, 1u);
    load_method(&load_aml, "LDFD", load_field_reads_loads_aml, sizeof(load_field_reads_loads_aml));
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
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = NULL;

    load_begin();
    made_up_aml_name_integer(&load_aml, "RES1", LOAD_UNTOUCHED);
    made_up_aml_name_integer(&load_aml, "RES2", LOAD_UNTOUCHED);
    made_up_aml_name_integer(&load_aml, "RES3", LOAD_UNTOUCHED);
    load_method(&load_aml, "LTB1", load_table_runs_table_finds_aml, sizeof(load_table_runs_table_finds_aml));
    load_method(&load_aml, "LTB2", load_table_runs_table_finds_any_aml, sizeof(load_table_runs_table_finds_any_aml));
    load_method(&load_aml, "LTB3", load_table_runs_table_misses_aml, sizeof(load_table_runs_table_misses_aml));
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
 *
 * Its Load from a local is raw bytes: iasl does not emit that form of Load (§8.2).
 */
KERNEL_TEST(the_forms_the_kernel_does_not_load_fail_with_load_form_unsupported)
{
    static const uint8_t loads_local[] = {0x70u, '\\',  'B',  'U', 'F', 'F', 0x60u, 0x5Bu,
                                          0x20u, 0x60u, '\\', 'H', 'N', 'D', '1'};
    static const char *const methods[] = {"\\LDBF", "\\LDLC", "\\LTRT", "\\LTPP", "\\LTPD"};

    load_begin();
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_raw(&load_aml, load_forms_does_not_buffer_aml, sizeof(load_forms_does_not_buffer_aml));
    load_method(&load_aml, "LDBF", load_forms_does_not_loads_buffer_aml, sizeof(load_forms_does_not_loads_buffer_aml));
    load_method(&load_aml, "LDLC", loads_local, sizeof(loads_local));
    load_method(&load_aml, "LTRT", load_forms_does_not_with_root_aml, sizeof(load_forms_does_not_with_root_aml));
    load_method(&load_aml, "LTPP", load_forms_does_not_with_parameter_aml,
                sizeof(load_forms_does_not_with_parameter_aml));
    load_method(&load_aml, "LTPD", load_forms_does_not_with_data_aml, sizeof(load_forms_does_not_with_data_aml));
    made_up_aml_raw(&load_aml, load_loads_table_into_first_handle_aml, sizeof(load_loads_table_into_first_handle_aml));
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
 *
 * Its cut header is raw bytes: iasl writes no table header shorter than itself.
 */
KERNEL_TEST(a_load_refuses_what_is_not_a_whole_table_of_system_memory)
{
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
    load_method(&load_aml, "LDIO", load_refuses_what_from_input_output_aml,
                sizeof(load_refuses_what_from_input_output_aml));
    load_method(&load_aml, "LDSH", load_refuses_what_from_short_aml, sizeof(load_refuses_what_from_short_aml));
    load_method(&load_aml, "LDTN", load_refuses_what_from_tiny_aml, sizeof(load_refuses_what_from_tiny_aml));
    load_method(&load_aml, "LDCT", load_refuses_what_from_cut_aml, sizeof(load_refuses_what_from_cut_aml));
    load_method(&load_aml, "LDIN", load_refuses_what_from_integer_aml, sizeof(load_refuses_what_from_integer_aml));
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
    load_method(&load_aml, "LDEA", load_table_loads_stop_loads_each_aml, sizeof(load_table_loads_stop_loads_each_aml));
    load_method(&load_aml, "LDON", load_table_loads_stop_loads_once_aml, sizeof(load_table_loads_stop_loads_once_aml));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDEA") == LOAD_STATUS(LIMIT_TABLES),
                      "loading a new table each turn fails with limit_tables");
    if (!kernel_test_check(test,
                           load_load.table_count ==
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES &&
                               load_load.tables_loaded ==
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES - 1u &&
                               made_up_limit_peak(&load_load, "tables_peak") ==
                                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLES,
                           "the load holds 128 tables: the DSDT and the 127 Load added"))
    {
        load_close(test);
        return;
    }

    const uint32_t arena_bytes = load_load.space.arena_bytes;

    --load_load.table_count;
    advanced_configuration_and_power_interface_machine_language_namespace_give_pages(
        &load_load.space, (void *) (uintptr_t) load_load.tables[load_load.table_count].header,
        load_load.tables[load_load.table_count].pages);
    load_load.table_bytes_copied =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLE_BYTES - MADE_UP_TABLE_HEADER;
    kernel_test_check(test,
                      load_call("\\LDON") == LOAD_STATUS(LIMIT_TABLE_BYTES) && load_integer("HND1") == LOAD_UNTOUCHED &&
                          made_up_limit_peak(&load_load, "table_bytes_peak") ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_TABLE_BYTES -
                                  MADE_UP_TABLE_HEADER,
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
    load_begin();
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_raw(&load_aml, load_method_belongs_mutex_aml, sizeof(load_method_belongs_mutex_aml));
    load_method(&load_aml, "LPLD", load_method_belongs_loops_around_aml, sizeof(load_method_belongs_loops_around_aml));
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
 * @brief A method that runs in the frame a loaded table's code ran in, once that table is declared, runs as
 *        a method: the Name it declares is a temporary node, gone when it returns (§5.1), not a declaration
 *        of table-level code.
 */
KERNEL_TEST(a_method_in_the_frame_a_loaded_table_ran_in_runs_as_a_method)
{
    load_begin();
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "LDIT", load_method_frame_loaded_loads_aml, sizeof(load_method_frame_loaded_loads_aml));
    load_method(&load_aml, "INNR", load_method_frame_loaded_inner_aml, sizeof(load_method_frame_loaded_inner_aml));
    load_method(&load_aml, "OUTR", load_method_frame_loaded_outer_aml, sizeof(load_method_frame_loaded_outer_aml));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "LDIT loads the table, its code run in the frame above LDIT's");
    kernel_test_check(test, load_call("\\OUTR") == LOAD_STATUS(OK) && load_integer("TRAC") == 7u,
                      "INNR, called by OUTR in that same frame, returns the 7 its Name holds");

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *method = made_up_root_node(&load_load, "INNR");

    kernel_test_check(test, method != NULL && method->first_child == 0u, "INNR has no child once it has returned");
    load_close(test);
}

/**
 * @brief A method that the code of a loaded table calls, and that fails, fails that statement only: the table's
 *        code goes on past it, and the Load stores True (§3.6).
 */
KERNEL_TEST(a_failure_in_a_method_a_loaded_table_calls_goes_on_past_its_statement)
{
    load_begin();
    made_up_aml_raw(&load_loaded, load_failure_method_loaded_calls_aml, sizeof(load_failure_method_loaded_calls_aml));
    made_up_aml_name_integer(&load_loaded, "AFTR", 7u);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "FALT", common_return_missing_name_aml, sizeof(common_return_missing_name_aml));
    load_method(&load_aml, "LDIT", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "the Load succeeds and stores True");
    kernel_test_check(test,
                      load_load.evaluation.statuses[LOAD_STATUS(NAME_NOT_FOUND)] == 1u && load_integer("TRAC") == 0u,
                      "the statement that calls FALT fails, counted, and stores nothing");
    kernel_test_check(test, load_integer("AFTR") == 7u, "the declaration after it is made");
    load_close(test);
}

/**
 * @brief A Load run where the evaluation has no room left for its table's frame declares the table's code without
 *        running it, the failure counted: its Name exists, its Store never ran, and the Load stores True (§3.6).
 */
KERNEL_TEST(a_table_whose_frame_finds_no_room_is_declared_without_running)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t depth = {
        .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
        .payload = LOAD_FULL_DEPTH};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;

    load_begin();
    made_up_aml_store_integer(&load_loaded, 5u, "TRAC");
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, load_frame_no_room_recurses_aml, sizeof(load_frame_no_room_recurses_aml));
    made_up_aml_method(&load_aml, "REC_", 0x01u, &load_body);
    if (!load_run(test))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&load_load, "\\REC_", &depth, 1u, &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&load_load.heap, &load_load.space,
                                                                              &result);
    kernel_test_check(test, status == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "the Load at the deepest call succeeds and stores True");
    kernel_test_check(test,
                      load_load.evaluation.statuses[LOAD_STATUS(LIMIT_FRAMES)] == 1u && load_integer("LDED") == 0x2Au &&
                          load_integer("TRAC") == 0u,
                      "its table finds no frame, counted: LDED is declared, the Store to TRAC never runs");
    load_close(test);
}

/**
 * @brief Whether DEVB, which the table load_table_at_caps_fails declares at the root, holds INDV.
 */
static bool load_device_holds_its_name(void)
{
    const uint32_t device = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &load_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "DEVB", 0u));

    return device != 0u && advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                               &load_load.space, device,
                               advanced_configuration_and_power_interface_machine_language_name_segment(
                                   (const uint8_t *) "INDV", 0u)) != 0u;
}

/**
 * @brief Writes in the DSDT Method (INNR) { Load (\TBLR, \HND1) } and Method (LDIT) { Store (Add (One, Add (One, ...
 *        INNR () ...)), Local0) }, @p adds Add deep, over the table load_table_at_caps_fails. The Store and the Adds
 *        are raw bytes, written at run time: they are partial terms, and their depth is a parameter (§8.2).
 */
static void load_write_operator_loader(uint32_t adds)
{
    static const uint8_t store[] = {0x70u};
    static const uint8_t add_one[] = {0x72u, 0x01u};
    static const uint8_t no_target[] = {0x00u};
    static const uint8_t local0[] = {0x60u};

    load_begin();
    made_up_aml_raw(&load_loaded, load_table_at_caps_fails_aml, sizeof(load_table_at_caps_fails_aml));
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "INNR", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    made_up_aml_begin(&load_inner);
    made_up_aml_raw(&load_inner, store, sizeof(store));
    for (uint32_t add = 0u; add < adds; ++add)
        made_up_aml_raw(&load_inner, add_one, sizeof(add_one));
    made_up_aml_name(&load_inner, "INNR");
    for (uint32_t add = 0u; add < adds; ++add)
        made_up_aml_raw(&load_inner, no_target, sizeof(no_target));
    made_up_aml_raw(&load_inner, local0, sizeof(local0));
    made_up_aml_method(&load_aml, "LDIT", 0x00u, &load_inner);
}

/**
 * @brief The operator cap of §6.1 and a table's code (§3.6): LDIT's Store, its 252 Adds and the evaluation's own
 *        result hold 254 operators, the one Load leaves to wait for the table's frame the 255th, and that frame starts
 *        there. Both Stores of \NOPE fail, and so does the offset of RGNA, whose Add finds no operator left once the
 *        one that waits for that offset holds the 256th; each failure is counted once, and the declarations the
 *        failures hand the load are made: RGNA, without its offset, then DEVB with INDV, and AFTR. One Add more puts
 *        the operator Load leaves 256th: the frame finds no room, and the table is declared without running, counted.
 */
KERNEL_TEST(a_table_loaded_at_the_operator_cap_goes_on_past_its_failed_statements)
{
    for (uint32_t adds = LOAD_OPERATOR_CAP_ADDS; adds <= LOAD_OPERATOR_CAP_ADDS + 1u; ++adds)
    {
        const bool runs = adds == LOAD_OPERATOR_CAP_ADDS;
        const uint32_t *statuses = load_load.evaluation.statuses;

        load_write_operator_loader(adds);
        if (!load_run(test))
            return;
        kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                          "the Load succeeds and stores True");
        kernel_test_check(
            test,
            runs ?
                statuses[LOAD_STATUS(NAME_NOT_FOUND)] == 2u && statuses[LOAD_STATUS(LIMIT_PENDING_OPERATORS)] == 1u &&
                    load_load.evaluation.operators_peak ==
                        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_OPERATORS :
                statuses[LOAD_STATUS(NAME_NOT_FOUND)] == 0u && statuses[LOAD_STATUS(LIMIT_PENDING_OPERATORS)] == 1u &&
                    load_load.declared_not_run == 5u,
            runs ? "the table's code runs at the 255th operator: both Stores and RGNA's offset fail, "
                   "each counted once, no declaration after them" :
                   "at the 256th the table's frame finds no room, counted, and its five names are "
                   "declared without running");
        kernel_test_check(test,
                          made_up_root_node(&load_load, "RGNA") != NULL && load_device_holds_its_name() &&
                              load_integer("AFTR") == 7u,
                          "RGNA, DEVB with INDV, and AFTR are declared");
        load_close(test);
    }
}

/**
 * @brief Writes in @p method Method (LDIT) { If (One) { ... Load (\TBLR, \HND1) ... } }, @p depth If (One) deep. The
 *        If (One) are raw bytes, written at run time: they are partial terms, and their depth is a parameter (§8.2).
 */
static void load_write_nested_loader(MadeUpAml_t *method, uint32_t depth)
{
    static const uint8_t if_lead[] = {0xA0u};
    static const uint8_t one[] = {0x01u};
    bool overflowed = false;

    made_up_aml_begin(&load_inner);
    made_up_aml_raw(&load_inner, load_loads_table_into_first_handle_aml,
                    sizeof(load_loads_table_into_first_handle_aml));
    for (uint32_t level = 0u; level < depth; ++level)
    {
        made_up_aml_begin(&load_body);
        made_up_aml_raw(&load_body, one, sizeof(one));
        made_up_aml_raw(&load_body, load_inner.bytes, load_inner.length);
        overflowed = overflowed || load_inner.overflowed || load_body.overflowed;
        made_up_aml_begin(&load_inner);
        made_up_aml_construct(&load_inner, if_lead, sizeof(if_lead), &load_body);
    }
    made_up_aml_begin(method);
    made_up_aml_method(method, "LDIT", 0x00u, &load_inner);
    method->overflowed = method->overflowed || overflowed;
}

/**
 * @brief Loads the DSDT written followed by @p method, which together hold more than one builder does.
 */
static bool load_run_with(KernelTest_t *test, const MadeUpAml_t *method)
{
    if (!kernel_test_check(test, !load_aml.overflowed && !load_loaded.overflowed && !method->overflowed,
                           "the made-up AML fits"))
        return false;
    memcpy(load_long_aml, load_aml.bytes, load_aml.length);
    memcpy(load_long_aml + load_aml.length, method->bytes, method->length);
    return made_up_load_on(test, &load_load, &load_host.host, load_long_dsdt, 2u, load_long_aml,
                           load_aml.length + method->length);
}

/**
 * @brief The block cap of §6.1 and a table's code (§3.6): the host's frame, LDIT's body and its 509 If (One) hold 511
 *        blocks, and the frame of the table LDIT loads opens the 512th. Both Stores of \NOPE fail, counted, and the
 *        declarations each hands the load are made: RGNA, then DEVB, whose TermList finds no block and is refused, a
 *        fault of the load and no failure of the evaluation, so that INDV is not declared, and AFTR after it.
 */
KERNEL_TEST(a_table_loaded_at_the_block_cap_goes_on_past_its_failed_statements)
{
    const uint32_t *statuses = load_load.evaluation.statuses;

    load_begin();
    made_up_aml_raw(&load_loaded, load_table_at_caps_fails_aml, sizeof(load_table_at_caps_fails_aml));
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_write_nested_loader(&load_long_method, LOAD_BLOCK_CAP_IFS);
    if (!load_run_with(test, &load_long_method))
        return;

    const uint32_t load_errors = load_load.load_errors;

    kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "the Load succeeds and stores True");
    kernel_test_check(test,
                      load_load.evaluation.blocks_peak ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAX_EVALUATION_BLOCKS &&
                          statuses[LOAD_STATUS(NAME_NOT_FOUND)] == 2u && statuses[LOAD_STATUS(LIMIT_BLOCKS)] == 0u,
                      "the table's code runs at the 512th block: both Stores fail, counted, and no declaration "
                      "after them");
    kernel_test_check(
        test, load_load.load_errors == load_errors + 1u && load_load.first_load_error == LOAD_STATUS(LIMIT_BLOCKS),
        "DEVB's TermList is refused with limit_blocks, the one fault of the load");
    kernel_test_check(test,
                      made_up_root_node(&load_load, "RGNA") != NULL && made_up_root_node(&load_load, "DEVB") != NULL &&
                          !load_device_holds_its_name() && load_integer("AFTR") == 7u,
                      "RGNA, DEVB without INDV, and AFTR are declared");
    load_close(test);
}

/**
 * @brief An External of a loaded table whose parent the table declares after it is placed under that parent
 *        once the table is declared, by the Externals pass the Load runs (§3.6).
 *
 * Its External is raw bytes: iasl emits an External at the root only inside an If (Zero).
 */
KERNEL_TEST(a_loaded_table_places_an_external_whose_parent_it_declares_later)
{
    static const uint8_t external[] = {0x15u, '\\', 0x2Eu, 'Z', 'D', 'E', 'V', 'Z', 'I', 'N', 'T', 0x01u, 0x00u};

    load_begin();
    made_up_aml_raw(&load_loaded, external, sizeof(external));
    made_up_aml_raw(&load_loaded, common_device_zdev_aml, sizeof(common_device_zdev_aml));
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "LDIT", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    if (!load_run(test))
        return;
    kernel_test_check(test, load_call("\\LDIT") == LOAD_STATUS(OK) && load_integer("HND1") == LOAD_ONES,
                      "the Load succeeds and stores True");

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &load_load.space;
    const uint32_t zdev = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "ZDEV", 0u));

    kernel_test_check(test,
                      zdev != 0u && advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                                        space, zdev,
                                        advanced_configuration_and_power_interface_machine_language_name_segment(
                                            (const uint8_t *) "ZINT", 0u)) != 0u,
                      "ZINT is placed under ZDEV");
    load_close(test);
}

/**
 * @brief Appends Method (_REG, 2), which traces its two arguments, Arg0 then Arg1, to \TRAC.
 */
static void load_registration(MadeUpAml_t *aml)
{
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, common_traces_two_arguments_aml, sizeof(common_traces_two_arguments_aml));
    made_up_aml_method(aml, "_REG", 0x02u, &load_body);
}

/**
 * @brief Appends Method (_INI), which appends @p digit to \TRAC, and Method (_STA) { Return (0x0F) }.
 *
 * Its pieces are raw bytes: the Store and the Add it opens around @p digit, partial terms.
 */
static void load_present_initialization(MadeUpAml_t *aml, uint8_t digit)
{
    const uint8_t traces[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                              0x00u, 0x0Au, digit, 0x00u, '\\', 'T', 'R', 'A', 'C'};

    load_method(aml, "_STA", load_returns_present_aml, sizeof(load_returns_present_aml));
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
    made_up_aml_raw(&load_body, load_table_loaded_run_platform_aml, sizeof(load_table_loaded_run_platform_aml));
    made_up_aml_method(&device, "_OSC", 0x04u, &load_body);
    made_up_aml_scope(&load_aml, "_SB_", &device);
    made_up_aml_begin(&device);
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, load_table_loaded_run_driver_aml, sizeof(load_table_loaded_run_driver_aml));
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
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_loads_c_states_and_returns_one_aml,
                sizeof(load_loads_c_states_and_returns_one_aml));
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
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", load_table_status_step_status_aml, sizeof(load_table_status_step_status_aml));
    load_method(&device, "_CST", load_returns_one_c_state_aml, sizeof(load_returns_one_c_state_aml));
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
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "CNT_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_notifies_and_loads_c_states_once_aml,
                sizeof(load_notifies_and_loads_c_states_once_aml));
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
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, load_late_processor_driver_aml, sizeof(load_late_processor_driver_aml));
    made_up_aml_method(&device, "_PDC", 0x01u, &load_body);
    load_method(&device, "_CST", load_traces_two_and_returns_one_c_state_aml,
                sizeof(load_traces_two_and_returns_one_c_state_aml));
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
    load_method(&device, "_CST", load_loads_c_states_and_returns_one_aml,
                sizeof(load_loads_c_states_and_returns_one_aml));
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
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "FLAG", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_traces_two_and_returns_one_c_state_aml,
                sizeof(load_traces_two_and_returns_one_c_state_aml));
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_scope(&load_loaded, "DEVB", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_flags_loads_and_returns_one_c_state_aml,
                sizeof(load_flags_loads_and_returns_one_c_state_aml));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", load_round_pass_reads_status_aml, sizeof(load_round_pass_reads_status_aml));
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
 * @brief A round of the pass after step 8 reads again a device step 8 found absent (§3.10): DEVB keeps CPUX out
 *        in step 8's walk, then the _CST of CPU0, declared after it, sets the flag that makes DEVB present and
 *        loads the table that declares CPU1 below DEVB, which publishes.
 */
KERNEL_TEST(a_round_of_the_pass_reads_again_a_device_step_eight_found_absent)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "FLAG", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_traces_two_and_returns_one_c_state_aml,
                sizeof(load_traces_two_and_returns_one_c_state_aml));
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_scope(&load_loaded, "DEVB", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_returns_one_c_state_aml, sizeof(load_returns_one_c_state_aml));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", load_round_pass_reads_status_2_aml, sizeof(load_round_pass_reads_status_2_aml));
    made_up_aml_processor(&scope, "CPUX", LOAD_PROCESSOR_IDENTIFIER + 2u, 0u, 0u, &device);
    made_up_aml_device(&load_aml, "DEVB", &scope);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_flags_loads_and_returns_one_c_state_aml,
                sizeof(load_flags_loads_and_returns_one_c_state_aml));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_madt.processor_count = 3u;
    for (uint32_t slot = 0u; slot < 3u; ++slot)
        load_madt.processors[slot].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER + slot;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
    kernel_test_check(test,
                      load_idle.slots[2].processor == 0u && load_counts.processor_idle_absent == 1u &&
                          load_idle.slots[0].count == 1u,
                      "step 8 keeps CPUX out, DEVB absent, and CPU0 publishes");
    kernel_test_check(test, load_integer("TRAC") == 0x2u && load_idle.slots[1].count == 1u,
                      "CPU1 publishes: DEVB is read again, present");
    load_close(test);
}

/**
 * @brief The pass after step 8 goes on while the tables it loads declare processor objects (§3.8): CPU0's
 *        _CST loads the table of CPU1, whose _CST loads the table of CPU2; each publishes once.
 */
KERNEL_TEST(the_pass_after_step_eight_goes_on_while_its_tables_declare_processors)
{
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "HC1_", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_pass_after_step_second_aml, sizeof(load_pass_after_step_second_aml));
    made_up_aml_processor(&load_loaded, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&load_inner);
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_returns_one_c_state_aml, sizeof(load_returns_one_c_state_aml));
    made_up_aml_processor(&load_inner, "CPU2", LOAD_PROCESSOR_IDENTIFIER + 2u, 0u, 0u, &device);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_loads_c_states_and_returns_one_aml,
                sizeof(load_loads_c_states_and_returns_one_aml));
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
 * @brief Appends Method (@p name, @p arguments) to @p aml: the @p length bytes at @p before, then, when @p digit is
 *        not 0, the append of @p digit to \TRAC, then, when @p states holds, the Return of a _CST package of one C1.
 *
 * Its pieces are raw bytes: the Store and the Add it opens around @p digit, partial terms.
 */
static void load_traced_method(MadeUpAml_t *aml, const char *name, uint8_t arguments, const uint8_t *before,
                               uint32_t length, uint8_t digit, bool states)
{
    const uint8_t traces[] = {0x70u, 0x72u, 0x77u, '\\',  'T',  'R', 'A', 'C', 0x0Au, 0x10u,
                              0x00u, 0x0Au, digit, 0x00u, '\\', 'T', 'R', 'A', 'C'};

    made_up_aml_begin(&load_body);
    if (before != NULL)
        made_up_aml_raw(&load_body, before, length);
    if (digit != 0u)
        made_up_aml_raw(&load_body, traces, sizeof(traces));
    if (states)
        made_up_aml_raw(&load_body, load_returns_one_c_state_aml, sizeof(load_returns_one_c_state_aml));
    made_up_aml_method(aml, name, arguments, &load_body);
}

/**
 * @brief Gives the made-up MADT @p count CPU slots, of UIDs 1 to @p count, then initialises the namespace loaded.
 */
static void load_initialize(uint32_t count)
{
    load_madt.processor_count = count;
    for (uint32_t slot = 0u; slot < count; ++slot)
        load_madt.processors[slot].processor_unique_identifier = LOAD_PROCESSOR_IDENTIFIER + slot;

    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {.madt = &load_madt,
                                                                                   .monitor_wait = false};

    advanced_configuration_and_power_interface_initialize_namespace(&load_load, &machine, &load_counts, &load_idle);
}

/**
 * @brief Writes a DSDT whose CPU0 has a _PDC that loads a table declaring CPU1, with a _PDC and a _CST that
 *        append 1 and 2 to \TRAC: at the root, after CPU0, or, when @p behind holds, in the scope of DEVZ, a
 *        device declared before CPU0, which step 7's walk has gone past; then loads it and initialises it with
 *        the CPU slots of both.
 */
static bool load_step_seven_processor(KernelTest_t *test, bool behind)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    if (behind)
    {
        made_up_aml_begin(&scope);
        made_up_aml_name_integer(&scope, "ZNAM", 0u);
        made_up_aml_device(&load_aml, "DEVZ", &scope);
    }
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, NULL, 0u, 1u, false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 2u, true);
    made_up_aml_begin(&scope);
    made_up_aml_processor(behind ? &scope : &load_loaded, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    if (behind)
        made_up_aml_scope(&load_loaded, "DEVZ", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return false;
    load_initialize(2u);
    return true;
}

/**
 * @brief Checks what step 7 and step 8 gave CPU1, which the table CPU0's _PDC loads declares: its _PDC, then its
 *        _CST, each once, and both processors their states (§3.8).
 */
static void load_check_step_seven_processor(KernelTest_t *test)
{
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
                      "CPU0's _PDC loads the table that declares CPU1");
    kernel_test_check(test, load_integer("TRAC") == 0x12u, "CPU1's _PDC runs, then its _CST, each once");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 2u && load_counts.processor_capabilities_legacy == 2u &&
                          load_counts.processor_capabilities_failed == 0u,
                      "CPU0 and CPU1 are given step 7's capabilities through their _PDC, once each");
    kernel_test_check(test,
                      load_counts.processor_idle == 2u && load_idle.slots[0].count == 1u &&
                          load_idle.slots[1].count == 1u && load_counts.processor_idle_unbound == 0u,
                      "CPU0 and CPU1 publish their states");
}

/**
 * @brief A processor that a table step 7 loads declares after the processor whose _PDC loads it is left to
 *        step 7's walk, which gives it its capabilities once (§3.8).
 */
KERNEL_TEST(a_processor_a_pdc_loads_ahead_of_step_seven_gets_its_capabilities_once)
{
    if (!load_step_seven_processor(test, false))
        return;
    load_check_step_seven_processor(test);
    load_close(test);
}

/**
 * @brief A processor that a table step 7 loads declares below a device step 7's walk has gone past gets its
 *        capabilities once the _PDC that loads it returns, before step 8 reads its _CST (§3.8).
 */
KERNEL_TEST(a_processor_a_pdc_loads_behind_step_seven_gets_its_capabilities_then_its_cst)
{
    if (!load_step_seven_processor(test, true))
        return;
    load_check_step_seven_processor(test);
    load_close(test);
}

/**
 * @brief Step 7 goes on while the tables loaded behind its walk declare processor objects (§3.8): CPU0's _PDC
 *        loads the table of CPU1, whose _PDC loads the table of CPU2, both in the scope of DEVZ, declared before
 *        CPU0; each gets its capabilities, then its _CST.
 */
KERNEL_TEST(step_seven_goes_on_while_the_tables_loaded_behind_its_walk_declare_processors)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "HC1_", 0u);
    made_up_aml_begin(&scope);
    made_up_aml_name_integer(&scope, "ZNAM", 0u);
    made_up_aml_device(&load_aml, "DEVZ", &scope);
    made_up_aml_begin(&load_inner);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, NULL, 0u, 3u, false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 4u, true);
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPU2", LOAD_PROCESSOR_IDENTIFIER + 2u, 0u, 0u, &device);
    made_up_aml_scope(&load_inner, "DEVZ", &scope);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_step_seven_goes_second_aml, sizeof(load_step_seven_goes_second_aml),
                       1u, false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 2u, true);
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_scope(&load_loaded, "DEVZ", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(3u);
    kernel_test_check(
        test, load_integer("HC0_") == LOAD_ONES && load_integer("HC1_") == LOAD_ONES && load_load.tables_loaded == 2u,
        "CPU0's _PDC loads CPU1's table, and CPU1's loads CPU2's");
    kernel_test_check(test, load_integer("TRAC") == 0x1324u,
                      "CPU1's _PDC runs, then CPU2's, then step 8 reads CPU1's _CST, then CPU2's");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 3u && load_counts.processor_idle == 3u &&
                          load_idle.slots[1].count == 1u && load_idle.slots[2].count == 1u,
                      "the three processors are given their capabilities, then publish their states");
    load_close(test);
}

/**
 * @brief A table that step 7 loads while it reads what a processor object is gets its late initialisation once
 *        that evaluation returns, though the object binds to no CPU slot (§3.8): the _HID of CPUD, a Device with a
 *        _PDC and no _UID, loads the table of LDEV, whose _INI runs before the _PDC of CPU0, declared after CPUD.
 */
KERNEL_TEST(a_table_a_hid_of_step_seven_loads_is_initialised_once_it_returns)
{
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_HID", 0x00u, load_table_hid_step_identifies_aml,
                       sizeof(load_table_hid_step_identifies_aml), 0u, false);
    load_traced_method(&device, "_PDC", 0x01u, NULL, 0u, 9u, false);
    made_up_aml_device(&load_aml, "CPUD", &device);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, NULL, 0u, 1u, false);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(1u);
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_counts.processors_unbound == 1u,
                      "CPUD's _HID loads the table, and CPUD, without a _UID, binds to no slot");
    kernel_test_check(test, load_integer("TRAC") == 0x71u && load_counts.late_initializations == 1u,
                      "LDEV's _INI runs once CPUD's _HID returns, before CPU0's _PDC");
    load_close(test);
}

/**
 * @brief Writes a DSDT whose CPU0 has a _PDC that loads a table adding a _PDC, which appends 1 to \TRAC, to CPUZ, a
 *        processor without one whose _CST appends 2: declared after CPU0, or, when @p behind holds, before it, where
 *        step 7's walk has gone past; the _PDC is a method, or, when @p alias holds, an Alias of PDCM, a method of
 *        the DSDT; then loads it and initialises it with the CPU slots of both.
 */
static bool load_capabilities_added(KernelTest_t *test, bool behind, bool alias)
{
    static MadeUpAml_t first;
    static MadeUpAml_t added;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    load_traced_method(&load_aml, "PDCM", 0x01u, NULL, 0u, 1u, false);
    made_up_aml_begin(&added);
    load_traced_method(&added, "_CST", 0x00u, NULL, 0u, 2u, true);
    if (behind)
        made_up_aml_processor(&load_aml, "CPUZ", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &added);
    made_up_aml_begin(&scope);
    if (alias)
        made_up_aml_raw(&scope, load_aliases_pdcm_as_pdc_aml, sizeof(load_aliases_pdcm_as_pdc_aml));
    else
        load_traced_method(&scope, "_PDC", 0x01u, NULL, 0u, 1u, false);
    made_up_aml_scope(&load_loaded, "CPUZ", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&first);
    load_traced_method(&first, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    load_traced_method(&first, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &first);
    if (!behind)
        made_up_aml_processor(&load_aml, "CPUZ", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &added);
    if (!load_run(test))
        return false;
    load_initialize(2u);
    return true;
}

/**
 * @brief Checks what step 7 and step 8 gave CPUZ, to which the table CPU0's _PDC loads adds a _PDC: that _PDC, then
 *        its _CST, each once, and both processors their states (§3.8).
 */
static void load_check_capabilities_added(KernelTest_t *test)
{
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
                      "CPU0's _PDC loads the table that adds a _PDC to CPUZ");
    kernel_test_check(test, load_integer("TRAC") == 0x12u, "CPUZ's _PDC runs, then its _CST, each once");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 2u && load_counts.processor_capabilities_legacy == 2u &&
                          load_counts.processor_capabilities_failed == 0u,
                      "CPU0 and CPUZ are given step 7's capabilities through their _PDC, once each");
    kernel_test_check(test,
                      load_counts.processor_idle == 2u && load_idle.slots[0].count == 1u &&
                          load_idle.slots[1].count == 1u && load_counts.processor_idle_unbound == 0u,
                      "CPU0 and CPUZ publish their states");
}

/**
 * @brief A _PDC that a table step 7 loads adds to a processor declared after the one whose _PDC loads it is left to
 *        step 7's walk, which runs it once (§3.8).
 */
KERNEL_TEST(a_pdc_a_table_adds_ahead_of_step_seven_runs_once)
{
    if (!load_capabilities_added(test, false, false))
        return;
    load_check_capabilities_added(test);
    load_close(test);
}

/**
 * @brief A _PDC that a table step 7 loads adds to a processor step 7's walk has gone past runs once the _PDC that
 *        loads it returns, before step 8 reads that processor's _CST (§3.8).
 */
KERNEL_TEST(a_pdc_a_table_adds_behind_step_seven_runs_before_its_cst)
{
    if (!load_capabilities_added(test, true, false))
        return;
    load_check_capabilities_added(test);
    load_close(test);
}

/**
 * @brief An Alias of an older method that a table step 7 loads adds as the _PDC of a processor step 7's walk has gone
 *        past is that processor's first _PDC: it runs once the _PDC that loads the table returns, before step 8 reads
 *        that processor's _CST (§3.8).
 */
KERNEL_TEST(an_alias_pdc_a_table_adds_behind_step_seven_runs_before_its_cst)
{
    if (!load_capabilities_added(test, true, true))
        return;
    load_check_capabilities_added(test);
    load_close(test);
}

/**
 * @brief A processor object that a table step 7 loads declares behind its walk, with a _PDC that is an Alias of an
 *        older method, gets its capabilities once the _PDC that loads it returns (§3.8): the table CPU0's _PDC loads
 *        declares CPUZ in the scope of DEVZ, a device declared before CPU0, its _PDC an Alias of PDCM, a method of
 *        the DSDT, and nothing else that holds an _OSC or a _PDC.
 */
KERNEL_TEST(a_processor_a_table_declares_behind_step_seven_with_an_older_pdc_gets_its_capabilities)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    load_traced_method(&load_aml, "PDCM", 0x01u, NULL, 0u, 1u, false);
    made_up_aml_begin(&scope);
    made_up_aml_name_integer(&scope, "ZNAM", 0u);
    made_up_aml_device(&load_aml, "DEVZ", &scope);
    made_up_aml_begin(&device);
    made_up_aml_raw(&device, load_aliases_pdcm_as_pdc_aml, sizeof(load_aliases_pdcm_as_pdc_aml));
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 2u, true);
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPUZ", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_scope(&load_loaded, "DEVZ", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(2u);
    load_check_capabilities_added(test);
    load_close(test);
}

/**
 * @brief A processor object that a table step 7 loads declares behind its walk, in place of an External another table
 *        gave a _PDC, gets its capabilities once the _PDC that loads it returns (§3.8): the DSDT names \DEVZ.CPUZ by an
 *        External, the _INI of DEV1 loads a table that gives it a _PDC, and the _PDC of CPU0 a table that declares it.
 *
 * Its External is raw bytes: iasl emits an External at the root only inside an If (Zero).
 */
KERNEL_TEST(a_processor_declared_behind_step_seven_over_an_external_with_a_pdc_gets_its_capabilities)
{
    static const uint8_t external[] = {0x15u, '\\', 0x2Eu, 'D', 'E', 'V', 'Z', 'C', 'P', 'U', 'Z', 0x0Cu, 0x00u};
    static MadeUpAml_t device;
    static MadeUpAml_t scope;
    static MadeUpAml_t inner;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "HC1_", 0u);
    made_up_aml_begin(&scope);
    made_up_aml_name_integer(&scope, "ZNAM", 0u);
    made_up_aml_device(&load_aml, "DEVZ", &scope);
    made_up_aml_raw(&load_aml, external, sizeof(external));
    made_up_aml_begin(&inner);
    load_traced_method(&inner, "_PDC", 0x01u, NULL, 0u, 1u, false);
    made_up_aml_begin(&scope);
    made_up_aml_scope(&scope, "CPUZ", &inner);
    made_up_aml_scope(&load_loaded, "DEVZ", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 2u, true);
    made_up_aml_begin(&scope);
    made_up_aml_processor(&scope, "CPUZ", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_begin(&load_inner);
    made_up_aml_scope(&load_inner, "DEVZ", &scope);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_INI", 0x00u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    made_up_aml_device(&load_aml, "DEV1", &device);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_step_seven_goes_second_aml, sizeof(load_step_seven_goes_second_aml),
                       0u, false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(2u);
    kernel_test_check(
        test, load_integer("HC0_") == LOAD_ONES && load_integer("HC1_") == LOAD_ONES && load_load.tables_loaded == 2u,
        "DEV1's _INI loads the table that gives the External CPUZ a _PDC, and CPU0's _PDC the one that declares CPUZ");
    kernel_test_check(test, load_integer("TRAC") == 0x12u, "CPUZ's _PDC runs, then its _CST, each once");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 2u && load_counts.processor_capabilities_legacy == 2u &&
                          load_idle.slots[0].count == 1u && load_idle.slots[1].count == 1u,
                      "CPU0 and CPUZ are given step 7's capabilities through their _PDC, then publish their states");
    load_close(test);
}

/**
 * @brief The pass behind step 7's walk goes on while any evaluation of a round loaded a table that gives a processor
 *        its first _PDC (§3.8): CPU0's _PDC loads a table adding a _PDC to CPUA, which loads a table adding one to
 *        CPUC, and to CPUB, which loads nothing; the three are declared before CPU0, CPUB after CPUA.
 */
KERNEL_TEST(the_pass_behind_step_seven_goes_on_when_an_earlier_processor_of_its_round_loads)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "HC1_", 0u);
    made_up_aml_begin(&device);
    made_up_aml_processor(&load_aml, "CPUA", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_processor(&load_aml, "CPUB", LOAD_PROCESSOR_IDENTIFIER + 2u, 0u, 0u, &device);
    made_up_aml_processor(&load_aml, "CPUC", LOAD_PROCESSOR_IDENTIFIER + 3u, 0u, 0u, &device);
    made_up_aml_begin(&load_inner);
    made_up_aml_begin(&scope);
    load_traced_method(&scope, "_PDC", 0x01u, NULL, 0u, 3u, false);
    made_up_aml_scope(&load_inner, "CPUC", &scope);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&scope);
    load_traced_method(&scope, "_PDC", 0x01u, load_step_seven_goes_second_aml, sizeof(load_step_seven_goes_second_aml),
                       1u, false);
    made_up_aml_scope(&load_loaded, "CPUA", &scope);
    made_up_aml_begin(&scope);
    load_traced_method(&scope, "_PDC", 0x01u, NULL, 0u, 2u, false);
    made_up_aml_scope(&load_loaded, "CPUB", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(4u);
    kernel_test_check(
        test, load_integer("HC0_") == LOAD_ONES && load_integer("HC1_") == LOAD_ONES && load_load.tables_loaded == 2u,
        "CPU0's _PDC loads the table of CPUA's and CPUB's, and CPUA's loads the table of CPUC's");
    kernel_test_check(test, load_integer("TRAC") == 0x123u, "CPUA's _PDC runs, then CPUB's, then CPUC's, each once");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 4u && load_counts.processor_capabilities_legacy == 4u &&
                          load_counts.processor_capabilities_failed == 0u,
                      "the four processors are given step 7's capabilities through their _PDC, once each");
    load_close(test);
}

/**
 * @brief A _PDC that a table step 7 loads adds to the processor object above the one step 7 is at, which its walk
 *        has passed, runs once the _PDC that loads it returns (§3.8): CPUZ, a Device of _HID ACPI0007 and _UID 1
 *        without a _PDC, holds CPU0, whose _PDC loads the table.
 */
KERNEL_TEST(a_pdc_a_table_adds_above_the_processor_step_seven_is_at_runs_before_its_cst)
{
    static MadeUpAml_t inner;
    static MadeUpAml_t outer;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&scope);
    load_traced_method(&scope, "_PDC", 0x01u, NULL, 0u, 1u, false);
    made_up_aml_scope(&load_loaded, "CPUZ", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&inner);
    load_traced_method(&inner, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    load_traced_method(&inner, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_begin(&outer);
    made_up_aml_raw(&outer, load_table_loaded_before_identifies_aml, sizeof(load_table_loaded_before_identifies_aml));
    load_traced_method(&outer, "_CST", 0x00u, NULL, 0u, 2u, true);
    made_up_aml_processor(&outer, "CPU0", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &inner);
    made_up_aml_device(&load_aml, "CPUZ", &outer);
    if (!load_run(test))
        return;
    load_initialize(2u);
    load_check_capabilities_added(test);
    load_close(test);
}

/**
 * @brief A table step 7 loads that adds an _OSC to processors gives step 7's capabilities only to the one that held
 *        neither an _OSC nor a _PDC, and never twice (§3.8): CPU0's _PDC loads a table adding an _OSC that appends 6
 *        to \TRAC to CPUY, which has none, to CPUZ, whose _PDC ran, both declared before CPU0, and to CPU0 itself.
 */
KERNEL_TEST(an_osc_a_table_adds_behind_step_seven_runs_only_where_no_capabilities_were_given)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 5u, true);
    made_up_aml_processor(&load_aml, "CPUY", LOAD_PROCESSOR_IDENTIFIER + 2u, 0u, 0u, &device);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, NULL, 0u, 1u, false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 2u, true);
    made_up_aml_processor(&load_aml, "CPUZ", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_begin(&scope);
    load_traced_method(&scope, "_OSC", 0x04u, load_traces_six_and_returns_capabilities_aml,
                       sizeof(load_traces_six_and_returns_capabilities_aml), 0u, false);
    made_up_aml_scope(&load_loaded, "CPUY", &scope);
    made_up_aml_scope(&load_loaded, "CPUZ", &scope);
    made_up_aml_scope(&load_loaded, "CPU0", &scope);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, load_loads_c_state_table_aml, sizeof(load_loads_c_state_table_aml), 0u,
                       false);
    load_traced_method(&device, "_CST", 0x00u, NULL, 0u, 0u, true);
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(3u);
    kernel_test_check(test, load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
                      "CPU0's _PDC loads the table that adds an _OSC to CPUY, CPUZ and CPU0");
    kernel_test_check(test, load_integer("TRAC") == 0x1652u,
                      "CPUZ's _PDC runs, then CPUY's _OSC, then the _CST of CPUY and of CPUZ; no other _OSC runs");
    kernel_test_check(
        test,
        load_counts.processor_capabilities == 3u && load_counts.processor_capabilities_legacy == 2u &&
            load_counts.processor_capabilities_refused == 0u && load_counts.processor_capabilities_failed == 0u,
        "CPUZ and CPU0 are given step 7's capabilities through their _PDC, CPUY through its _OSC, once each");
    kernel_test_check(test,
                      load_counts.processor_idle == 3u && load_idle.slots[0].count == 1u &&
                          load_idle.slots[1].count == 1u && load_idle.slots[2].count == 1u,
                      "the three processors publish their states");
    load_close(test);
}

/**
 * @brief A processor that a table step 9 loads declares gets step 7's capabilities, then its _CST, then step 9
 *        again for the Notify 0x81 its _CST queues (§3.8): CPU0's _CST notifies CPU0 and loads, on its second
 *        run, the table of CPU1, whose _CST notifies CPU1; the Notify of CPU0's second run is dropped.
 */
KERNEL_TEST(a_processor_a_step_nine_evaluation_loads_gets_its_capabilities_then_its_cst)
{
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_name_integer(&load_aml, "CNT_", 0u);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_PDC", 0x01u, NULL, 0u, 1u, false);
    load_traced_method(&device, "_CST", 0x00u, common_notifies_cpu1_c_states_aml,
                       sizeof(common_notifies_cpu1_c_states_aml), 2u, true);
    made_up_aml_processor(&load_loaded, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_notifies_and_loads_c_states_once_aml,
                sizeof(load_notifies_and_loads_c_states_once_aml));
    made_up_aml_processor(&load_aml, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    if (!load_run(test))
        return;
    load_initialize(2u);
    kernel_test_check(
        test, load_integer("CNT_") == 2u && load_integer("HC0_") == LOAD_ONES && load_load.tables_loaded == 1u,
        "CPU0's _CST runs twice, its second run, step 9's, loads the table of CPU1, and its Notify is dropped");
    kernel_test_check(test, load_integer("TRAC") == 0x122u,
                      "CPU1's _PDC runs, then its _CST, then its _CST once more for the Notify it queued");
    kernel_test_check(test,
                      load_counts.processor_capabilities == 1u && load_counts.processor_idle_changes == 2u &&
                          load_counts.processor_idle == 4u,
                      "CPU1 is given step 7's capabilities, and step 9 evaluates CPU0's _CST and CPU1's once more");
    kernel_test_check(test, load_idle.slots[0].count == 1u && load_idle.slots[1].count == 1u,
                      "CPU0 and CPU1 publish their states");
    load_close(test);
}

/**
 * @brief A _CST that loads a table, then fails, is the first failure, before the _INI of the loaded device
 *        that fails in the late initialisation its return starts (§3.8): the _CST loads, then divides by
 *        zero; the loaded _INI reads a name that does not exist.
 */
KERNEL_TEST(a_cst_that_loads_a_table_then_fails_is_the_first_failure)
{
    static MadeUpAml_t device;
    char path[12];

    load_begin();
    made_up_aml_name_integer(&load_aml, "HC0_", 0u);
    made_up_aml_begin(&device);
    load_method(&device, "_STA", load_returns_present_aml, sizeof(load_returns_present_aml));
    load_method(&device, "_INI", load_cst_loads_table_missing_aml, sizeof(load_cst_loads_table_missing_aml));
    made_up_aml_device(&load_loaded, "LDEV", &device);
    load_region("CSTR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_CST", load_cst_loads_table_body_aml, sizeof(load_cst_loads_table_body_aml));
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
    load_method(&device, "_CST", load_loads_c_states_and_returns_one_aml,
                sizeof(load_loads_c_states_and_returns_one_aml));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", load_counts_present_status_aml, sizeof(load_counts_present_status_aml));
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
    load_method(&device, "_STA", load_late_initialisation_decides_absent_aml,
                sizeof(load_late_initialisation_decides_absent_aml));
    load_method(&device, "_CST", common_return_one_aml, sizeof(common_return_one_aml));
    made_up_aml_processor(&load_aml, "CPU1", LOAD_PROCESSOR_IDENTIFIER + 1u, 0u, 0u, &device);
    made_up_aml_begin(&device);
    load_method(&device, "_STA", load_counts_present_status_aml, sizeof(load_counts_present_status_aml));
    load_method(&device, "_CST", load_loads_c_states_and_returns_one_aml,
                sizeof(load_loads_c_states_and_returns_one_aml));
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
    load_method(&device, "_CST", load_loads_c_states_and_returns_one_aml,
                sizeof(load_loads_c_states_and_returns_one_aml));
    made_up_aml_begin(&scope);
    load_method(&scope, "_STA", common_status_by_flag_aml, sizeof(common_status_by_flag_aml));
    load_method(&scope, "_INI", common_noop_aml, sizeof(common_noop_aml));
    made_up_aml_processor(&scope, "CPU0", LOAD_PROCESSOR_IDENTIFIER, 0u, 0u, &device);
    made_up_aml_device(&load_aml, "DEVB", &scope);
    made_up_aml_begin(&scope);
    load_method(&scope, "_INI", common_sets_flag_aml, sizeof(common_sets_flag_aml));
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
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_name_integer(&load_aml, "EPDC", 0u);
    made_up_aml_begin(&device);
    made_up_aml_raw(&device, load_table_loaded_before_identifies_aml, sizeof(load_table_loaded_before_identifies_aml));
    made_up_aml_begin(&load_body);
    made_up_aml_raw(&load_body, load_table_loaded_before_keeps_aml, sizeof(load_table_loaded_before_keeps_aml));
    made_up_aml_method(&device, "_PDC", 0x01u, &load_body);
    load_present_initialization(&device, 4u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "EPRC", &device);
    made_up_aml_scope(&load_loaded, "_SB_", &load_inner);
    load_region("EARL", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_INI", load_table_loaded_before_loads_aml, sizeof(load_table_loaded_before_loads_aml));
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
    made_up_aml_raw(&load_body, load_tables_loaded_during_registers_aml,
                    sizeof(load_tables_loaded_during_registers_aml));
    made_up_aml_method(&device, "_REG", 0x02u, &load_body);
    made_up_aml_device(&load_aml, "ADEV", &device);
    made_up_aml_begin(&device);
    load_method(&device, "_INI", load_tables_loaded_during_initializes_aml,
                sizeof(load_tables_loaded_during_initializes_aml));
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
    load_begin();
    made_up_aml_name_integer(&load_aml, "HND2", LOAD_UNTOUCHED);
    made_up_aml_begin(&load_inner);
    made_up_aml_name_integer(&load_inner, "SCND", 2u);
    load_region("TBL2", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_raw(&load_loaded, load_code_loaded_loads_inner_aml, sizeof(load_code_loaded_loads_inner_aml));
    made_up_aml_name_integer(&load_loaded, "AFTR", 7u);
    load_method(&load_loaded, "LDB2", load_code_loaded_loads_inner_aml, sizeof(load_code_loaded_loads_inner_aml));
    load_region("TBLA", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    load_method(&load_aml, "LDA2", load_code_loaded_loads_outer_aml, sizeof(load_code_loaded_loads_outer_aml));
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
    static MadeUpAml_t device;

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "LATE", &device);
    made_up_aml_scope(&load_loaded, "GONE", &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", load_returns_absent_aml, sizeof(load_returns_absent_aml));
    load_method(&device, "_INI", load_traces_nine_aml, sizeof(load_traces_nine_aml));
    made_up_aml_device(&load_aml, "GONE", &device);
    made_up_aml_begin(&device);
    load_method(&device, "_INI", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
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
 *
 * Its Return opcode is raw bytes: a partial term around @p status.
 */
static bool load_absent_machine(KernelTest_t *test, uint8_t status, bool loader_first, const char *scope)
{
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
    load_method(&gone, "_INI", load_traces_nine_aml, sizeof(load_traces_nine_aml));
    made_up_aml_begin(&loader);
    load_method(&loader, "_INI", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
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
 * @brief Builds the made-up machine of the tests below and initialises it: the DSDT names \PCIX by an External
 *        before DEV1, whose _INI loads load_loaded, which the caller wrote; load_inner, which this writes, declares
 *        Device (\PCIX), whose _INI appends 5 to \TRAC, then what the @p length bytes at @p body declare in it, and
 *        an _INI of load_loaded loads it into \HC1.
 *
 * Its External is raw bytes: iasl emits an External at the root only inside an If (Zero).
 *
 * @return false, the load closed, when DEV1's _INI did not load load_loaded.
 */
static bool load_external_machine(KernelTest_t *test, const uint8_t *body, uint32_t length)
{
    static const uint8_t external[] = {0x15u, '\\', 'P', 'C', 'I', 'X', 0x06u, 0x00u};
    static MadeUpAml_t device;

    made_up_aml_name_integer(&load_aml, "HC1_", 0u);
    made_up_aml_raw(&load_aml, external, sizeof(external));
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&device);
    load_traced_method(&device, "_INI", 0x00u, NULL, 0u, 5u, false);
    made_up_aml_raw(&device, body, length);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "PCIX", &device);
    load_region("OTHR", LOAD_OTHER_ADDRESS, load_place(&load_inner, load_other_table, LOAD_OTHER_ADDRESS));
    made_up_aml_begin(&device);
    load_method(&device, "_INI", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    made_up_aml_device(&load_aml, "DEV1", &device);
    if (!load_run(test))
        return false;
    load_initialize(0u);
    if (kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "DEV1's _INI loads the first table at step 4"))
        return true;
    load_close(test);
    return false;
}

/**
 * @brief A device a table loaded at step 4 declares over an External, above a device a table loaded before placed
 *        and initialised, walks that subtree late without running the _INI below it again (§3.8): the first table
 *        declares Device (\PCIX.DEVA), whose _INI appends 3 to \TRAC and loads the table declaring Device (\PCIX).
 */
KERNEL_TEST(a_device_declared_over_an_external_above_an_initialised_one_runs_no_initialization_twice)
{
    static MadeUpAml_t device;
    static MadeUpAml_t scope;

    load_begin();
    made_up_aml_begin(&device);
    load_traced_method(&device, "_INI", 0x00u, load_step_seven_goes_second_aml, sizeof(load_step_seven_goes_second_aml),
                       3u, false);
    made_up_aml_begin(&scope);
    made_up_aml_device(&scope, "DEVA", &device);
    made_up_aml_scope(&load_loaded, "PCIX", &scope);
    if (!load_external_machine(test, NULL, 0u))
        return;
    kernel_test_check(test, load_integer("HC1_") == LOAD_ONES && load_load.tables_loaded == 2u,
                      "DEVA's _INI loads the table declaring PCIX, once");
    kernel_test_check(test, load_integer("TRAC") == 0x35u,
                      "DEVA's _INI runs once, late, then PCIX's, late, without DEVA's again");
    kernel_test_check(
        test, load_counts.initializations == 3u && load_counts.late_initializations == 2u && load_counts.tables == 3u,
        "three _INI run, DEV1's, DEVA's and PCIX's, two of them in late initialisations, which three tables declare");
    load_close(test);
}

/**
 * @brief The late walk of a device declared over an External goes into the devices below it a walk did not find
 *        absent, and not into those it found neither present nor functioning (§3.8): the first table declares, in
 * \PCIX, Device (DEVA), absent, holding Device (DEVB), whose _INI appends 9 to \TRAC, then Device (DEVC), whose _INI
 *        appends 3 and loads the table declaring Device (\PCIX), which gives DEVC.DEVD, a device without _INI, an
 *        _INI appending 7.
 */
KERNEL_TEST(the_late_walk_over_an_external_goes_into_the_devices_a_walk_did_not_find_absent)
{
    static MadeUpAml_t device;
    static MadeUpAml_t inner;
    static MadeUpAml_t scope;
    static MadeUpAml_t added;
    static MadeUpAml_t devices;

    load_begin();
    made_up_aml_begin(&inner);
    load_method(&inner, "_INI", load_traces_nine_aml, sizeof(load_traces_nine_aml));
    made_up_aml_begin(&device);
    load_method(&device, "_STA", load_returns_absent_aml, sizeof(load_returns_absent_aml));
    made_up_aml_device(&device, "DEVB", &inner);
    made_up_aml_begin(&scope);
    made_up_aml_device(&scope, "DEVA", &device);
    made_up_aml_begin(&inner);
    made_up_aml_name_integer(&inner, "DNAM", 0u);
    made_up_aml_begin(&device);
    load_traced_method(&device, "_INI", 0x00u, load_step_seven_goes_second_aml, sizeof(load_step_seven_goes_second_aml),
                       3u, false);
    made_up_aml_device(&device, "DEVD", &inner);
    made_up_aml_device(&scope, "DEVC", &device);
    made_up_aml_scope(&load_loaded, "PCIX", &scope);
    made_up_aml_begin(&inner);
    load_traced_method(&inner, "_INI", 0x00u, NULL, 0u, 7u, false);
    made_up_aml_begin(&added);
    made_up_aml_scope(&added, "DEVD", &inner);
    made_up_aml_begin(&devices);
    made_up_aml_scope(&devices, "DEVC", &added);
    if (!load_external_machine(test, devices.bytes, devices.length))
        return;
    kernel_test_check(test, load_integer("HC1_") == LOAD_ONES && load_load.tables_loaded == 2u,
                      "DEVC's _INI loads the table declaring PCIX, once");
    kernel_test_check(test, load_integer("TRAC") == 0x357u,
                      "DEVC's _INI runs once, then PCIX's, then DEVD's; DEVB's never, below the absent DEVA");
    kernel_test_check(test, load_counts.status_evaluations == 1u && load_counts.initializations == 4u,
                      "DEVA's _STA is read once, and four _INI run: DEV1's, DEVC's, PCIX's and DEVD's");
    load_close(test);
}

/**
 * @brief A device a table loaded at step 4 declares in a power resource of one of its devices, behind step 4's walk,
 *        runs its _INI once (§3.8): the walk of the device's subtree reaches it, and it roots a subtree of the table
 *        of its own, its parent no device. The DSDT declares Device (PARE), without _INI, before BDEV, whose _INI
 *        loads a table declaring Device (\PARE.LDEV) { PowerResource (PWR0) { Device (INNR) } }, INNR's _INI
 *        appending 7 to \TRAC.
 *
 * Its PowerResource is raw bytes: made_up_aml writes no PowerResource.
 */
KERNEL_TEST(a_device_in_a_power_resource_of_a_late_device_runs_its_initialization_once)
{
    static const uint8_t power_resource[] = {0x5Bu, 0x84u};
    static const uint8_t power_resource_fields[] = {'P', 'W', 'R', '0', 0x00u, 0x00u, 0x00u};
    static MadeUpAml_t device;
    static MadeUpAml_t resource;
    static MadeUpAml_t parent;

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 7u);
    made_up_aml_begin(&resource);
    made_up_aml_raw(&resource, power_resource_fields, sizeof(power_resource_fields));
    made_up_aml_device(&resource, "INNR", &device);
    made_up_aml_begin(&device);
    made_up_aml_construct(&device, power_resource, sizeof(power_resource), &resource);
    made_up_aml_begin(&load_inner);
    made_up_aml_device(&load_inner, "LDEV", &device);
    made_up_aml_scope(&load_loaded, "PARE", &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&parent);
    made_up_aml_name_integer(&parent, "PNAM", 0u);
    made_up_aml_device(&load_aml, "PARE", &parent);
    made_up_aml_begin(&device);
    load_method(&device, "_INI", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    made_up_aml_device(&load_aml, "BDEV", &device);
    if (!load_run(test))
        return;
    load_initialize(0u);
    kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "BDEV's _INI loads the table at step 4");
    kernel_test_check(test,
                      load_integer("TRAC") == 0x7u && load_counts.status_evaluations == 1u &&
                          load_counts.initializations == 2u && load_counts.late_initializations == 1u,
                      "INNR's _STA and _INI run once, late, after BDEV's _INI");
    load_close(test);
}

/**
 * @brief Builds Device (GIVN), without _INI, whose _STA returns @p status, and Device (BDEV), whose _INI loads a
 *        table, in the order @p loader_first says, BDEV first or GIVN first; the table gives GIVN an _INI appending 7
 *        to \TRAC, and declares Device (\GIVN.LATE), present, whose _INI appends 3. Then initialises the namespace.
 *
 * Its Return opcode is raw bytes: a partial term around @p status.
 *
 * @return false, the load closed, when the table was not loaded.
 */
static bool load_given_initialization_machine(KernelTest_t *test, uint8_t status, bool loader_first)
{
    static MadeUpAml_t device;
    static MadeUpAml_t given;
    static MadeUpAml_t loader;
    const uint8_t returns[] = {0xA4u, 0x0Au, status};

    load_begin();
    made_up_aml_begin(&device);
    load_present_initialization(&device, 3u);
    made_up_aml_begin(&load_inner);
    load_traced_method(&load_inner, "_INI", 0x00u, NULL, 0u, 7u, false);
    made_up_aml_device(&load_inner, "LATE", &device);
    made_up_aml_scope(&load_loaded, "GIVN", &load_inner);
    load_region("TBLR", LOAD_TABLE_ADDRESS, load_place(&load_loaded, load_table, LOAD_TABLE_ADDRESS));
    made_up_aml_begin(&given);
    load_method(&given, "_STA", returns, sizeof(returns));
    made_up_aml_begin(&loader);
    load_method(&loader, "_INI", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
    if (loader_first)
        made_up_aml_device(&load_aml, "BDEV", &loader);
    made_up_aml_device(&load_aml, "GIVN", &given);
    if (!loader_first)
        made_up_aml_device(&load_aml, "BDEV", &loader);
    if (!load_run(test))
        return false;
    load_initialize(0u);
    if (kernel_test_check(test, load_integer("HND1") == LOAD_ONES, "BDEV's _INI loads the table at step 4"))
        return true;
    load_close(test);
    return false;
}

/**
 * @brief An _INI a table loaded at step 4 gives a present device step 4 skipped, above a device of that table, runs
 *        once, before that device's, as step 4 runs it when it reaches the device after the load (§3.8): the late
 *        initialisation reads the device's _STA under step 4's rule.
 */
KERNEL_TEST(an_initialization_a_late_table_gives_a_device_above_one_of_its_own_runs_first)
{
    if (!load_given_initialization_machine(test, 0x0Fu, false))
        return;
    kernel_test_check(test,
                      load_integer("TRAC") == 0x73u && load_counts.status_evaluations == 2u &&
                          load_counts.late_initializations == 1u,
                      "GIVN's _STA and _INI run once, late, then LATE's");
    load_close(test);
}

/**
 * @brief The same machine, BDEV declared before GIVN: step 4 reaches GIVN after the load and runs its _INI, then
 *        LATE's, the order the late initialisation follows when GIVN comes first (§3.8).
 */
KERNEL_TEST(an_initialization_a_table_gives_a_device_step_four_reaches_later_runs_first)
{
    if (!load_given_initialization_machine(test, 0x0Fu, true))
        return;
    kernel_test_check(test,
                      load_integer("TRAC") == 0x73u && load_counts.status_evaluations == 2u &&
                          load_counts.late_initializations == 0u,
                      "GIVN's _STA and _INI run once, at step 4, then LATE's");
    load_close(test);
}

/**
 * @brief An _INI a table loaded at step 4 gives a device step 4 skipped, functioning but not present, does not run,
 *        and the late initialisation goes into the device all the same (§3.8 step 4).
 */
KERNEL_TEST(an_initialization_a_late_table_gives_a_device_not_present_does_not_run)
{
    if (!load_given_initialization_machine(test, 0x08u, false))
        return;
    kernel_test_check(test,
                      load_integer("TRAC") == 0x3u && load_counts.status_evaluations == 2u &&
                          load_counts.late_initializations == 1u,
                      "GIVN's _STA is read once, its _INI does not run, and LATE's does");
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
            load_method(&device, "_INI", load_loads_table_into_first_handle_aml,
                        sizeof(load_loads_table_into_first_handle_aml));
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
    load_method(&device, "_STA", load_returns_absent_aml, sizeof(load_returns_absent_aml));
    made_up_aml_begin(&loader);
    load_method(&loader, "_INI", load_loads_table_into_first_handle_aml,
                sizeof(load_loads_table_into_first_handle_aml));
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
