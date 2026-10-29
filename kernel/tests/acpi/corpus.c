#include "simulated_host.h"

#include <kernel/acpi/initialization.h>
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

static uint8_t corpus_image[CORPUS_IMAGE_CAPACITY];
static uint32_t corpus_image_length;
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t corpus_host;
static AdvancedConfigurationAndPowerInterfaceDirectory_t corpus_directory;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t corpus_load;

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
 * @brief Prints what the load of the directory read built and counted, as the records both sides
 *        compare: the caps it ran under, its tables, its namespace and its fold, what its second pass
 *        decoded and its fold, its faults, and what waits for the interpreter.
 */
static void corpus_measure_load(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &corpus_load.space;

    kernel_test_measure(test, "max_nodes", space->max_nodes);
    kernel_test_measure(test, "max_arena_bytes", space->max_arena_bytes);
    kernel_test_measure(test, "tables", corpus_directory.table_count);
    kernel_test_measure(test, "definition_blocks", corpus_load.table_count);
    kernel_test_measure(test, "bytes", corpus_load.bytes);
    kernel_test_measure(test, "integer_bits", corpus_load.integer_bits);
    kernel_test_measure(test, "declarations", corpus_load.declarations);
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
 * @brief Initialises the namespace of the load as the boot does, its processor objects bound to the
 *        directory's MADT, MONITOR/MWAIT taken as implemented and the platform never switched to ACPI mode,
 *        as the host harness does, and prints what it ran and counted, the tables Load added included, and
 *        the fold of the C-states it published.
 */
static void corpus_initialize(KernelTest_t *test)
{
    static AdvancedConfigurationAndPowerInterfaceInitialization_t initialization;
    static AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t idle;
    static AdvancedConfigurationAndPowerInterfaceMadtState_t madt;
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t machine = {
        .madt = advanced_configuration_and_power_interface_read_madt(
                    advanced_configuration_and_power_interface_find_in_directory(&corpus_directory, "APIC"), &madt) ?
                    &madt :
                    NULL,
        .monitor_wait = true,
        .fadt = &corpus_directory.fixed_description,
    };

    advanced_configuration_and_power_interface_initialize_namespace(&corpus_load, &machine, &initialization, &idle);
    kernel_test_measure(test, "initializations", initialization.initializations);
    kernel_test_measure(test, "initialization_failed", initialization.initialization_failed);
    kernel_test_measure(test, "registrations", initialization.registrations);
    kernel_test_measure(test, "region_registration_failed", initialization.region_registration_failed);
    kernel_test_measure(test, "devices", initialization.devices);
    kernel_test_measure(test, "status_evaluations", initialization.status_evaluations);
    kernel_test_measure(test, "status_failed", initialization.status_failed);
    kernel_test_measure(test, "status_invalid", initialization.status_invalid);
    kernel_test_measure(test, "pic_mode_deferred", initialization.pic_mode_deferred);
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
    kernel_test_measure(test, "idle_control", initialization.idle_control);
    kernel_test_measure(test, "processor_idle", initialization.processor_idle);
    kernel_test_measure(test, "processor_idle_failed", initialization.processor_idle_failed);
    kernel_test_measure(test, "processor_idle_absent", initialization.processor_idle_absent);
    kernel_test_measure(test, "processor_idle_unbound", initialization.processor_idle_unbound);
    kernel_test_measure(test, "idle_states_dropped", initialization.idle_states_dropped);
    kernel_test_measure(test, "processor_idle_changes", initialization.processor_idle_changes);
    kernel_test_measure_hexadecimal(test, "idle_fold",
                                    advanced_configuration_and_power_interface_processor_idle_fold(&idle));
    kernel_test_measure(test, "first_failure", initialization.first_failure);
    kernel_test_check(test, corpus_load.evaluation.frame_count == 0u && corpus_load.temporary_count == 0u,
                      "the initialisation leaves no frame and no temporary node behind");
}

/**
 * @brief Loads the definition blocks of the directory read, with the server profile's caps as the
 *        host harness does, prints the records of the load, initialises its namespace when
 *        @p initialize holds, then closes it and releases the directory.
 */
static void corpus_load_directory(KernelTest_t *test, bool initialize)
{
    if (kernel_test_check(test,
                          advanced_configuration_and_power_interface_machine_language_open(
                              &corpus_load, corpus_directory.host,
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_NODES,
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_SERVER_MAX_ARENA_BYTES),
                          "the namespace opens with the server profile's caps"))
    {
        advanced_configuration_and_power_interface_machine_language_load_directory(&corpus_load, &corpus_directory);
        kernel_test_check(test, corpus_load.declarations != 0u, "the machine's definition blocks declare objects");
        corpus_measure_load(test);
        if (initialize)
            corpus_initialize(test);
    }
    advanced_configuration_and_power_interface_machine_language_close(&corpus_load);
    advanced_configuration_and_power_interface_release_directory(&corpus_directory);
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
 *        its tables build the same namespace, and initialise it the same way, in ring 0 as on the build
 *        machine.
 */
KERNEL_TEST(the_boot_module_machine_loads)
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
    corpus_load_directory(test, true);
    kernel_test_check(test, corpus_host.outstanding_maps == 0u && corpus_host.mismatched_unmaps == 0u,
                      "every window is unmapped as it was mapped");
    kernel_test_check(test, corpus_host.outstanding_pages == 0u, "every page goes back to the host");
}

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
    corpus_load_directory(test, false);
}
