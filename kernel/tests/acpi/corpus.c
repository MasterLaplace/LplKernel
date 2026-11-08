#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/interrupt_routing.h>
#include <kernel/acpi/processor_idle.h>
#include <kernel/acpi/tables.h>
#include <kernel/boot/boot_module.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_corpus, KERNEL_TEST_STAGE_INITIALIZATION);

#define CORPUS_IMAGE_CAPACITY 256u        /**< Bytes of the buffer a made-up image is written in. */
#define CORPUS_HEADER_BYTES   24u         /**< Bytes of an image's header: magic, root pointer, range count. */
#define CORPUS_RANGE_BYTES    16u         /**< Bytes of a range's head: physical address, length. */
#define CORPUS_ROOT_ADDRESS   0x000E0010u /**< The root pointer of the made-up images. */
#define CORPUS_LOW_ADDRESS    0x000E0000u /**< Where the first range of a made-up image lies. */
#define CORPUS_HIGH_ADDRESS   0x7FFE0000u /**< Where the second range of a made-up image lies. */
#define CORPUS_LAST_ADDRESS   0xFFFFFFFFFFFFFFF0ull /**< A range there wraps past the last address. */
#define CORPUS_HUGE_LENGTH    0xFFFFFFFFFFFFFFF9ull /**< A range length that, padded to eight, wraps to 0. */
#define CORPUS_TOO_MANY_RANGES                                                                                         \
    (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES + 1u) /**< One more than a host holds. */
#define CORPUS_MODULE_SUFFIX ".acpi"     /**< How the boot module holding a recorded machine's image is named. */
#define CORPUS_FOLD_OFFSET   0x811C9DC5u /**< FNV-1a's offset basis. */
#define CORPUS_FOLD_PRIME    0x01000193u /**< FNV-1a's prime. */
#define CORPUS_KEY_BYTES     48u /**< Bytes of a record's key built from a prefix and a name, its NUL included. */

static uint8_t corpus_image[CORPUS_IMAGE_CAPACITY];
static uint32_t corpus_image_length;
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t corpus_host;
static AdvancedConfigurationAndPowerInterfaceDirectory_t corpus_directory;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t corpus_load;
static char corpus_key[CORPUS_KEY_BYTES];
static AdvancedConfigurationAndPowerInterfaceModeSwitch_t corpus_mode;

/**
 * @brief Appends @p value to the made-up image, as @p width little-endian bytes, written one by one
 *        so that the layout tested is the documented one and not a structure's.
 */
static void corpus_image_put(uint64_t value, uint32_t width)
{
    for (uint32_t byte = 0u; byte < width; ++byte)
        corpus_image[corpus_image_length++] = (uint8_t) (value >> (8u * byte));
}

/**
 * @brief Starts a made-up image: @p magic, then the root pointer and the range count.
 */
static void corpus_image_header(const char *magic, uint64_t root, uint64_t range_count)
{
    memset(corpus_image, 0, sizeof(corpus_image));
    corpus_image_length = 0u;
    for (uint32_t letter = 0u; letter < 8u; ++letter)
        corpus_image_put((uint8_t) magic[letter], 1u);
    corpus_image_put(root, 8u);
    corpus_image_put(range_count, 8u);
}

/**
 * @brief Appends a range to the made-up image: its head, its @p length bytes, then zeros to a
 *        multiple of eight.
 */
static void corpus_image_range(uint64_t physical_address, const char *bytes, uint32_t length)
{
    corpus_image_put(physical_address, 8u);
    corpus_image_put(length, 8u);
    memcpy(corpus_image + corpus_image_length, bytes, length);
    corpus_image_length += (length + 7u) & ~7u;
}

/**
 * @brief Whether the @p length bytes the host maps at @p physical_address are @p expected, unmapping
 *        them after.
 */
static bool corpus_serves(uint64_t physical_address, const char *expected, uint32_t length)
{
    const void *mapped = corpus_host.host.map_firmware(corpus_host.host.context, physical_address, length);
    const bool same = (mapped != NULL && memcmp(mapped, expected, length) == 0);

    if (mapped != NULL)
        corpus_host.host.unmap_firmware(corpus_host.host.context, mapped, length);
    return same;
}

/**
 * @brief Whether the texts @p lhs and @p rhs are the same, read no further than the first one's end.
 */
static bool corpus_same_text(const char *lhs, const char *rhs)
{
    while (*lhs != '\0' && *lhs == *rhs)
    {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

/**
 * @brief Checks that the made-up image is refused for @p reason, and that the host serves nothing.
 */
static void corpus_refuses(KernelTest_t *test, const char *reason, const char *claim)
{
    const char *error = NULL;
    const bool opened = advanced_configuration_and_power_interface_simulated_host_open_image(
        &corpus_host, corpus_image, corpus_image_length, &error);

    kernel_test_check(test, !opened && error != NULL && corpus_same_text(error, reason), claim);
    kernel_test_check(test, corpus_host.range_count == 0u && corpus_host.root_system_description_pointer == 0u,
                      "a refused image serves nothing");
}

/**
 * @brief Folds the opcodes the load decoded, as FNV-1a over each descriptor's count in descriptor
 *        order, four little-endian bytes each.
 */
static uint32_t corpus_opcode_fold(void)
{
    uint32_t fold = CORPUS_FOLD_OFFSET;

    for (uint32_t opcode = 0u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
    {
        for (uint32_t byte = 0u; byte < 4u; ++byte)
            fold = (fold ^ ((corpus_load.opcodes[opcode] >> (8u * byte)) & 0xFFu)) * CORPUS_FOLD_PRIME;
    }
    return fold;
}

/**
 * @brief The key @p prefix then @p name, cut to CORPUS_KEY_BYTES, in a buffer the next call writes again.
 */
static const char *corpus_key_of(const char *prefix, const char *name)
{
    uint32_t length = 0u;

    for (; *prefix != '\0' && length + 1u < CORPUS_KEY_BYTES; ++prefix)
        corpus_key[length++] = *prefix;
    for (; *name != '\0' && length + 1u < CORPUS_KEY_BYTES; ++name)
        corpus_key[length++] = *name;
    corpus_key[length] = '\0';
    return corpus_key;
}

/**
 * @brief The key @p prefix then @p number in two hexadecimal digits, as corpus_key_of writes it.
 */
static const char *corpus_key_of_number(const char *prefix, uint32_t number)
{
    static const char digits[] = "0123456789ABCDEF";
    const char name[3] = {digits[(number >> 4u) & 0xFu], digits[number & 0xFu], '\0'};

    return corpus_key_of(prefix, name);
}

/**
 * @brief Prints what the needs path executed (§4.1): each opcode it ran, each region space a field access reached
 *        and each AccessType of a unit AML read or wrote, those counted at least once, then each opcode it reached
 *        that the interpreter does not run and their total, which must be 0.
 */
static void corpus_measure_executed(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &corpus_load.evaluation;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegions_t *regions = &corpus_load.regions;
    uint32_t unimplemented = 0u;

    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        if (evaluation->executed[opcode] != 0u)
            kernel_test_measure(
                test,
                corpus_key_of("executed_",
                              advanced_configuration_and_power_interface_machine_language_opcode_name(opcode)),
                evaluation->executed[opcode]);
    for (uint32_t space = 0u; space < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SPACES; ++space)
        if (regions->space_accesses[space] != 0u)
            kernel_test_measure(test, corpus_key_of_number("space_", space), regions->space_accesses[space]);
    for (uint32_t access = 0u; access < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ACCESS_TYPES;
         ++access)
        if (regions->type_accesses[access] != 0u)
            kernel_test_measure(test, corpus_key_of_number("access_type_", access), regions->type_accesses[access]);
    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
    {
        if (evaluation->unimplemented[opcode] == 0u)
            continue;
        kernel_test_measure(
            test,
            corpus_key_of("unimplemented_",
                          advanced_configuration_and_power_interface_machine_language_opcode_name(opcode)),
            evaluation->unimplemented[opcode]);
        unimplemented += evaluation->unimplemented[opcode];
    }
    kernel_test_measure(test, "unimplemented", unimplemented);
    kernel_test_check(test, unimplemented == 0u, "the needs path executes no opcode the interpreter does not run");
}

/**
 * @brief Prints what the load of the directory read built and counted, as the records both sides
 *        compare: the target it ran for, its physical address width and its caps (§8.7), its tables, its
 *        namespace and its fold, what its second pass decoded and its fold, its faults, and what waits for
 *        the interpreter.
 */
static void corpus_measure_load(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &corpus_load.space;

    kernel_test_measure(test, "physical_address_bits", space->host->target(space->host->context).physical_address_bits);
    kernel_test_measure(test, "max_nodes", space->max_nodes);
    kernel_test_measure(test, "max_arena_bytes", space->max_arena_bytes);
    kernel_test_measure(test, "tables", corpus_directory.table_count);
    kernel_test_measure(test, "definition_blocks", corpus_load.table_count);
    kernel_test_measure(test, "bytes", corpus_load.bytes);
    kernel_test_measure(test, "integer_bits", corpus_load.integer_bits);
    kernel_test_measure(test, "declarations", corpus_load.declarations);
    kernel_test_measure(test, "data_stepped_over", corpus_load.data_stepped_over);
    kernel_test_measure(test, "declarations_decoded_directly", corpus_load.declarations_decoded_directly);
    kernel_test_measure(test, "data_kept", corpus_load.data_kept);
    kernel_test_measure(test, "duplicates", corpus_load.duplicates);
    kernel_test_measure(test, "nodes", space->live_nodes);
    kernel_test_measure(test, "objects", corpus_load.heap.objects);
    kernel_test_measure(test, "lookups", space->lookups);
    kernel_test_measure(test, "probes", space->probes);
    kernel_test_measure_hexadecimal(
        test, "namespace_fold",
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&corpus_load));
    kernel_test_measure(test, "name_references", corpus_load.name_references);
    kernel_test_measure(test, "method_calls", corpus_load.method_calls);
    kernel_test_measure(test, "unresolved_names", corpus_load.unresolved_names);
    kernel_test_measure_hexadecimal(test, "opcode_fold", corpus_opcode_fold());
    kernel_test_measure(test, "load_errors", corpus_load.load_errors);
    kernel_test_measure(test, "decode_errors", corpus_load.decode_errors);
    kernel_test_measure(test, "values_deferred", corpus_load.values_deferred);
    kernel_test_measure(test, "table_level_code", corpus_load.table_level_code);
}

/**
 * @brief Puts the recorded machine of the directory read in ACPI mode, as step 0 does at boot before any
 *        AML runs (§3.8), its firmware answering as the platform its FADT describes boots, as the host
 *        harness does, and prints how the switch ended and what it counted.
 */
static void corpus_enter_mode(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_simulated_host_boot_platform(&corpus_host,
                                                                            &corpus_directory.fixed_description);
    advanced_configuration_and_power_interface_enter_mode(&corpus_host.host, &corpus_directory.fixed_description,
                                                          &corpus_mode);
    kernel_test_check(test,
                      !corpus_directory.fixed_description.present ||
                          corpus_mode.outcome != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NOT_RUN,
                      "step 0 runs on the recorded machine's FADT, when it has one");
    kernel_test_check(test, corpus_mode.outcome != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_FAILED,
                      "the recorded machine's firmware answers ACPI_ENABLE");
    kernel_test_measure(test, "mode_outcome", corpus_mode.outcome);
    kernel_test_measure(test, "mode_enables_cleared", corpus_mode.enable_registers_cleared);
    kernel_test_measure(test, "mode_polls", corpus_mode.polls);
    kernel_test_measure(test, "mode_waited_ms", corpus_mode.waited_milliseconds);
}

/**
 * @brief Prints the limits of §6.1 the load counted, the hits of each, then its peak when it is the same on every
 *        target: the pages and runs memory takes are left out (§8.7).
 */
static void corpus_measure_limits(KernelTest_t *test)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLimit_t
        limits[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS];

    advanced_configuration_and_power_interface_machine_language_limits(&corpus_load, limits);
    for (uint32_t index = 0u; index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_LIMITS; ++index)
    {
        if (limits[index].hits_name != NULL)
            kernel_test_measure(test, limits[index].hits_name, limits[index].hits);
        if (limits[index].comparable)
            kernel_test_measure(test, limits[index].peak_name, limits[index].peak);
    }
}

/**
 * @brief Initialises the namespace of the load as the boot does, its processor objects bound to the
 *        directory's MADT, MONITOR/MWAIT taken as implemented and the platform as step 0 left it, as the
 *        host harness does, and prints what it ran and counted, the tables Load added included, the kept
 *        data found faulty by the check and by the reads (O20), the fold of the C-states it published, the limits of
 *        §6.1 it reached and their peaks, and what the load and the initialisation executed, which must hold no
 *        opcode the interpreter does not run (§4.1).
 */
static void corpus_initialize(KernelTest_t *test)
{
    static AdvancedConfigurationAndPowerInterfaceInitialization_t initialization;
    static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t idle;
    static AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t routing;
    static AdvancedConfigurationAndPowerInterfaceMadtState_t madt;
    const bool described = advanced_configuration_and_power_interface_read_madt(
        advanced_configuration_and_power_interface_find_in_directory(&corpus_directory, "APIC"), &madt);
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {
        .madt = described ? &madt : NULL,
        .monitor_wait = true,
        .fadt = &corpus_directory.fixed_description,
        .mode = corpus_mode.outcome,
        .interrupt_model = (described && madt.io_apic_count != 0u) ?
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_APIC :
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_PIC,
    };

    advanced_configuration_and_power_interface_initialize_namespace(&corpus_load, &machine, &initialization, &idle);
    advanced_configuration_and_power_interface_interrupt_routing_publish(&corpus_load, &machine, &initialization,
                                                                         &routing);
    kernel_test_check(test,
                      corpus_mode.outcome != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_SWITCHED ||
                          corpus_directory.fixed_description.idle_state_command == 0u ||
                          initialization.idle_control ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_WRITTEN,
                      "step 8 writes CST_CNT to SMI_CMD once step 0 switched a platform whose FADT names it");
    kernel_test_measure(test, "initializations", initialization.initializations);
    kernel_test_measure(test, "initialization_failed", initialization.initialization_failed);
    kernel_test_measure(test, "registrations", initialization.registrations);
    kernel_test_measure(test, "region_registration_failed", initialization.region_registration_failed);
    kernel_test_measure(test, "devices", initialization.devices);
    kernel_test_measure(test, "status_evaluations", initialization.status_evaluations);
    kernel_test_measure(test, "status_failed", initialization.status_failed);
    kernel_test_measure(test, "status_invalid", initialization.status_invalid);
    kernel_test_measure(test, "pic_mode_deferred", initialization.pic_mode_deferred);
    kernel_test_measure(test, "pic_mode_told", initialization.pic_mode_told);
    kernel_test_measure(test, "pic_mode_failed", initialization.pic_mode_failed);
    kernel_test_measure(test, "platform_capabilities", initialization.platform_capabilities);
    kernel_test_measure(test, "processor_capabilities", initialization.processor_capabilities);
    kernel_test_measure(test, "processor_capabilities_legacy", initialization.processor_capabilities_legacy);
    kernel_test_measure(test, "processor_capabilities_refused", initialization.processor_capabilities_refused);
    kernel_test_measure(test, "processor_capabilities_failed", initialization.processor_capabilities_failed);
    kernel_test_measure(test, "processors_unbound", initialization.processors_unbound);
    kernel_test_measure(test, "tables", initialization.tables);
    kernel_test_measure(test, "late_initializations", initialization.late_initializations);
    kernel_test_measure(test, "tables_loaded", corpus_load.tables_loaded);
    kernel_test_measure(test, "load_duplicates", corpus_load.load_duplicates);
    kernel_test_measure(test, "table_bytes_copied", corpus_load.table_bytes_copied);
    kernel_test_measure(test, "data_faults", corpus_load.data_faults);
    kernel_test_measure(test, "data_faults_read", corpus_load.data_faults_read);
    kernel_test_measure(test, "idle_control", initialization.idle_control);
    kernel_test_measure(test, "processor_idle", initialization.processor_idle);
    kernel_test_measure(test, "processor_idle_failed", initialization.processor_idle_failed);
    kernel_test_measure(test, "processor_idle_absent", initialization.processor_idle_absent);
    kernel_test_measure(test, "processor_idle_unbound", initialization.processor_idle_unbound);
    kernel_test_measure(test, "idle_states_dropped", initialization.idle_states_dropped);
    kernel_test_measure(test, "processor_idle_changes", initialization.processor_idle_changes);
    kernel_test_measure_hexadecimal(test, "idle_fold",
                                    advanced_configuration_and_power_interface_processor_idle_fold(&idle));
    kernel_test_measure(test, "prt_bridges", routing.bridges);
    kernel_test_measure(test, "prt_bridges_absent", routing.bridges_absent);
    kernel_test_measure(test, "prt_bridges_failed", routing.bridges_failed);
    kernel_test_measure(test, "prt_bridges_unrouted", routing.bridges_unrouted);
    kernel_test_measure(test, "prt_tables_failed", routing.tables_failed);
    kernel_test_measure(test, "prt_entries", routing.entries);
    kernel_test_measure(test, "prt_entries_malformed", routing.entries_malformed);
    kernel_test_measure(test, "prt_entries_repaired", routing.entries_repaired);
    kernel_test_measure(test, "prt_entries_dropped", routing.entries_dropped);
    kernel_test_measure(test, "prt_links", routing.links);
    kernel_test_measure(test, "prt_links_unrouted", routing.links_unrouted);
    kernel_test_measure(test, "prt_links_failed", routing.links_failed);
    kernel_test_measure(test, "prt_links_dropped", routing.links_dropped);
    kernel_test_measure(test, "prt_routes", routing.count);
    kernel_test_measure_hexadecimal(test, "prt_fold",
                                    advanced_configuration_and_power_interface_interrupt_routing_fold(&routing));
    kernel_test_measure(test, "first_failure", initialization.first_failure);
    corpus_measure_limits(test);
    kernel_test_measure(test, "flushes", corpus_load.predecoding.flushes);
    kernel_test_measure(test, "records_peak", corpus_load.predecoding.records_peak);
    kernel_test_measure(test, "record_operators_peak", corpus_load.predecoding.nodes_peak);
    kernel_test_measure(test, "record_constants_peak", corpus_load.predecoding.constants_peak);
    kernel_test_measure(test, "record_names_peak", corpus_load.predecoding.names_peak);
    corpus_measure_executed(test);
    kernel_test_check(test, corpus_load.evaluation.frame_count == 0u && corpus_load.temporary_count == 0u,
                      "the initialisation leaves no frame and no temporary node behind");
}

/**
 * @brief Counts the opcodes the decoding pass decoded, over every table the load holds, those Load added
 *        included.
 */
static uint32_t corpus_terms_decoded(void)
{
    uint32_t terms = 0u;

    for (uint32_t table = 0u; table < corpus_load.table_count; ++table)
        terms += corpus_load.tables[table].terms;
    return terms;
}

/**
 * @brief Prints the work record of §8.7 over what the load, and the initialisation when it ran, did: the operations
 *        executed, the bytes decoded and the maps the cache asked the host for, the counters that tell a run that did
 *        nothing apart, then the fold.
 */
static void corpus_measure_work(KernelTest_t *test)
{
    uint32_t executed = 0u;

    for (uint32_t opcode = 0u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
        executed += corpus_load.evaluation.executed[opcode];
    kernel_test_measure(test, "operations_executed", executed);
    kernel_test_measure(test, "bytes_decoded", corpus_load.bytes_decoded);
    kernel_test_measure(test, "map_calls", corpus_load.regions.map_calls);
    kernel_test_measure_hexadecimal(
        test, "work_fold", advanced_configuration_and_power_interface_machine_language_work_fold(&corpus_load));
}

/**
 * @brief Prints the evaluations record of §8.7 once the initialisation has run, over a fixed list of objects that
 *        take no argument and that the boot or a consumer of the namespace reads: the sleep states, the year the
 *        `_OSI` ladder of `_INI` sets (§5.4), and the identity, bus, resources and interrupt routing of the host
 *        bridge under both names firmware gives it. A path that names nothing on a machine folds its status alone.
 *        How many evaluated comes first, then the fold.
 */
static void corpus_measure_evaluations(KernelTest_t *test)
{
    static const char *const paths[] = {
        "\\_S0_",           "\\_S3_",           "\\_S4_",           "\\_S5_",           "\\OSYS",
        "\\_SB_.PCI0._HID", "\\_SB_.PCI0._BBN", "\\_SB_.PCI0._UID", "\\_SB_.PCI0._CRS", "\\_SB_.PCI0._PRT",
        "\\_SB_.PC00._HID", "\\_SB_.PC00._BBN", "\\_SB_.PC00._UID", "\\_SB_.PC00._CRS", "\\_SB_.PC00._PRT",
    };
    uint32_t succeeded = 0u;
    const uint32_t fold = advanced_configuration_and_power_interface_machine_language_evaluations_fold(
        &corpus_load, paths, sizeof(paths) / sizeof(paths[0]), &succeeded);

    kernel_test_measure(test, "evaluations_succeeded", succeeded);
    kernel_test_measure_hexadecimal(test, "evaluations_fold", fold);
}

/**
 * @brief Loads the definition blocks of the directory read, with the caps of the target of the host it
 *        was read through (§8.7), decoding no method body when @p skips_bodies holds, as a release kernel
 *        loads (O2), prints the records of the load, initialises its namespace when @p initialize
 *        holds, prints the work record of both, then, once initialised, the evaluations record, and closes
 *        it and releases the directory.
 */
static void corpus_load_directory(KernelTest_t *test, bool initialize, bool skips_bodies)
{
    const AdvancedConfigurationAndPowerInterfaceTarget_t target =
        corpus_directory.host->target(corpus_directory.host->context);

    if (kernel_test_check(test,
                          advanced_configuration_and_power_interface_machine_language_open(
                              &corpus_load, corpus_directory.host, target.max_nodes, target.max_arena_bytes),
                          "the namespace opens with the caps of the host's target"))
    {
        corpus_load.skips_bodies = skips_bodies;
        advanced_configuration_and_power_interface_machine_language_load_directory(&corpus_load, &corpus_directory);
        kernel_test_check(test, corpus_load.declarations != 0u, "the machine's definition blocks declare objects");
        corpus_measure_load(test);
        if (initialize)
            corpus_initialize(test);
        if (skips_bodies)
            kernel_test_check(test, corpus_terms_decoded() == 0u && corpus_load.name_references == 0u,
                              "the load decodes no method body, nor those of a table Load added");
        corpus_measure_work(test);
        if (initialize)
            corpus_measure_evaluations(test);
    }
    advanced_configuration_and_power_interface_machine_language_close(&corpus_load);
    advanced_configuration_and_power_interface_release_directory(&corpus_directory);
}

/**
 * @brief Loads and initialises the recorded machine a boot module holds, as an image, through a simulated
 *        host, once step 0 has put it in ACPI mode, decoding no method body when @p skips_bodies holds, then
 *        gives back the pages of simulated memory its AML wrote; skips @p test without a module.
 */
static void corpus_load_boot_module(KernelTest_t *test, bool skips_bodies)
{
    const uint8_t *image = NULL;
    uint32_t length = 0u;
    const char *error = NULL;

    if (!boot_module_find(CORPUS_MODULE_SUFFIX, &image, &length))
    {
        kernel_test_skip(test, "no boot module is named *" CORPUS_MODULE_SUFFIX);
        return;
    }
    if (!kernel_test_check(
            test,
            advanced_configuration_and_power_interface_simulated_host_open_image(&corpus_host, image, length, &error),
            "the boot module is a whole image"))
    {
        kernel_test_note(test, error);
        return;
    }
    if (!kernel_test_check(
            test, advanced_configuration_and_power_interface_read_directory(&corpus_host.host, &corpus_directory),
            "the recorded machine's directory is read"))
    {
        advanced_configuration_and_power_interface_release_directory(&corpus_directory);
        return;
    }
    corpus_enter_mode(test);
    corpus_load_directory(test, true, skips_bodies);
    kernel_test_check(test, corpus_host.outstanding_maps == 0u && corpus_host.mismatched_unmaps == 0u,
                      "every window is unmapped as it was mapped");
    kernel_test_check(test, corpus_host.outstanding_pages == 0u, "every page goes back to the host");
    advanced_configuration_and_power_interface_simulated_host_close(&corpus_host);
}

/**
 * @brief An image serves each range at its address and its root pointer, its bytes in place, and
 *        neither its padding nor a byte past a range.
 */
KERNEL_TEST(an_image_serves_each_range_where_it_says)
{
    const char *error = NULL;

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 2u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image_range(CORPUS_HIGH_ADDRESS, "DSDT", 4u);

    const bool opened = advanced_configuration_and_power_interface_simulated_host_open_image(
        &corpus_host, corpus_image, corpus_image_length, &error);

    if (!kernel_test_check(test, opened, "a whole image is opened"))
    {
        kernel_test_note(test, error);
        return;
    }
    kernel_test_check(test, corpus_host.root_system_description_pointer == CORPUS_ROOT_ADDRESS,
                      "the root pointer is the image's");
    kernel_test_check(test, corpus_host.range_count == 2u, "each range of the image is placed");
    kernel_test_check(test, corpus_serves(CORPUS_LOW_ADDRESS + 4u, "PTR AND", 7u),
                      "the first range is served at its address");
    kernel_test_check(test, corpus_serves(CORPUS_HIGH_ADDRESS, "DSDT", 4u),
                      "the second range is served at its address");
    const void *second = corpus_host.host.map_firmware(corpus_host.host.context, CORPUS_HIGH_ADDRESS, 1u);

    kernel_test_check(test, second == corpus_image + CORPUS_HEADER_BYTES + 2u * CORPUS_RANGE_BYTES + 24u,
                      "a range's bytes are served in place, after the first range padded to 24 bytes");
    if (second != NULL)
        corpus_host.host.unmap_firmware(corpus_host.host.context, second, 1u);
    kernel_test_check(test, !corpus_serves(CORPUS_HIGH_ADDRESS, "DSDT\0", 5u), "no byte past a range is served");
    kernel_test_check(test, !corpus_serves(CORPUS_LOW_ADDRESS + 20u, "\0", 1u), "a range's padding is not served");
    kernel_test_check(test, corpus_host.outstanding_maps == 0u && corpus_host.mismatched_unmaps == 0u,
                      "every map is unmapped as it was mapped");
}

/**
 * @brief An image cut short, misnamed, holding too many ranges, a range longer than the image, a range
 *        padded with anything but zeros, an empty range, a range that wraps or overlaps another, or bytes
 *        after its last range is refused, with what it does wrong.
 */
KERNEL_TEST(a_malformed_image_is_refused_and_says_why)
{
    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 0u);
    --corpus_image_length;
    corpus_refuses(test, "the image is shorter than its header", "an image cut inside its header is refused");

    corpus_image_header("LPLACPIX", CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR ", 8u);
    corpus_refuses(
        test, "the image does not start with " ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC,
        "an image of another magic is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS,
                        CORPUS_TOO_MANY_RANGES);
    corpus_refuses(test, "the image holds more ranges than a simulated host",
                   "an image of more ranges than a host holds is refused before any is read");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR ", 8u);
    corpus_image_length -= 8u + 4u;
    corpus_refuses(test, "a range runs past the end of the image", "an image cut inside a range's head is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image_length -= 4u;
    corpus_refuses(test, "a range runs past the end of the image", "an image cut inside a range's padding is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_put(CORPUS_LOW_ADDRESS, 8u);
    corpus_image_put(CORPUS_HUGE_LENGTH, 8u);
    corpus_refuses(test, "a range runs past the end of the image",
                   "a range whose length rounds up past 2^64 is refused, not taken for a padded length of 0");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image[corpus_image_length - 1u] = 0xFFu;
    corpus_refuses(test, "a range's padding is not zero", "a range padded with anything but zeros is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "", 0u);
    corpus_refuses(test, "a range is empty", "an empty range is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LAST_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_refuses(test, "a range wraps past the last address", "a range that wraps is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 2u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR AND THE REST", 20u);
    corpus_image_range(CORPUS_LOW_ADDRESS + 19u, "DSDT", 4u);
    corpus_refuses(test, "a range overlaps another", "a range that overlaps another by one byte is refused");

    corpus_image_header(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_IMAGE_MAGIC, CORPUS_ROOT_ADDRESS, 1u);
    corpus_image_range(CORPUS_LOW_ADDRESS, "RSD PTR ", 8u);
    corpus_image_length += 8u;
    corpus_refuses(test, "the image holds bytes after its last range", "an image longer than its ranges is refused");
}

/**
 * @brief The recorded machine a boot module holds, as an image, loads and initialises through a
 *        simulated host: its records, compared with the host harness's on the same module, show that
 *        its tables build the same namespace, and initialise it the same way, with the same work and the
 *        same results (§8.7), in ring 0 as on the build machine.
 */
KERNEL_TEST(the_boot_module_machine_loads) { corpus_load_boot_module(test, false); }

/**
 * @brief The recorded machine a boot module holds loads and initialises as a release kernel loads its own
 *        tables: no method body decoded, nor the data its Names keep in their tables checked (O2, O20). Its
 *        windows count the release path on real firmware (§7.4), and its records, compared with the host
 *        harness's, show that the path ends the same in ring 0.
 */
KERNEL_TEST(the_boot_module_machine_loads_on_the_release_path) { corpus_load_boot_module(test, true); }

/**
 * @brief The tables of the machine the kernel boots on load: in ring 0 they are the firmware's, on the
 *        host harness those a debug kernel printed, so that the two records compare the namespace of
 *        the same bytes. Their initialisation is not run again: in ring 0 it reads the machine, on the
 *        host a simulated one (§8.7).
 */
KERNEL_TEST(the_booted_machine_loads)
{
    if (!advanced_configuration_and_power_interface_read_directory(
            &advanced_configuration_and_power_interface_machine_host, &corpus_directory))
    {
        advanced_configuration_and_power_interface_release_directory(&corpus_directory);
        kernel_test_skip(test, "the machine gives no ACPI directory");
        return;
    }
    corpus_load_directory(test, false, false);
}
