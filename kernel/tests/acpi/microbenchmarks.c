#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_microbenchmarks, KERNEL_TEST_STAGE_INITIALIZATION);

#define MICROBENCHMARK_TABLE_CAPACITY 64u   /**< Bytes of the buffer a made-up DSDT is written in. */
#define MICROBENCHMARK_TURNS          1000u /**< Turns of the loop, and lookups, of window W4. */
#define MICROBENCHMARK_SCOPE_CHILDREN 3346u /**< Children of the scope the lookups search (§7.4). */
#define MICROBENCHMARK_MAX_NODES      4096u /**< The node cap of the load that holds that scope. */
#define MICROBENCHMARK_NODE(type)     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_##type
#define MICROBENCHMARK_AML_CAPACITY   8192u /**< Bytes of AML a load of one kind of declaration holds. */
#define MICROBENCHMARK_DECLARATIONS   256u  /**< Declarations most loads of one kind make. */
#define MICROBENCHMARK_PACKAGE_COUNT  32u   /**< Names the load of packages declares, each of 8 packages. */
#define MICROBENCHMARK_FIELD_LISTS    16u   /**< Field lists of 16 units each the load of field units declares. */

/**
 * @brief A kind of declaration whose load window W1 counts apart from the others (§7.4).
 */
typedef enum MicrobenchmarkKind {
    MICROBENCHMARK_INTEGER_NAMES, /**< `Name (Nxxx, 0x0001xxxx)`. */
    MICROBENCHMARK_STRING_NAMES,  /**< `Name (Nxxx, "PNP0C0F")`. */
    MICROBENCHMARK_BUFFER_NAMES,  /**< `Name (Nxxx, ResourceTemplate () { IO (...) IRQNoFlags () {1} })`. */
    MICROBENCHMARK_PACKAGE_NAMES, /**< `Name (Nxxx, Package () { 8 Package () { DWord, Byte, LNKA, Zero } })`, a
                                       _PRT's shape. */
    MICROBENCHMARK_METHODS,       /**< `Method (Mxxx, 1) { Return (Arg0) }`. */
    MICROBENCHMARK_DEVICES,       /**< `Device (Dxxx) {}`. */
    MICROBENCHMARK_SCOPES,        /**< `Scope (\_SB) { Name (Sxxx, Zero) }`. */
    MICROBENCHMARK_REGIONS,       /**< `OperationRegion (Rxxx, SystemIO, 0x1xxx, 4)`. */
    MICROBENCHMARK_FIELD_UNITS,   /**< 16 `Field (FREG, ByteAcc, NoLock, Preserve)` of 16 units of 8 bits. */
    MICROBENCHMARK_EXTERNALS,     /**< `External (\LNKA, DeviceObj)`, of a Device the table declares first. */
} MicrobenchmarkKind_t;

/**
 * @brief The AML of a load of one kind of declaration.
 */
typedef struct MicrobenchmarkAml {
    uint8_t bytes[MICROBENCHMARK_AML_CAPACITY]; /**< The AML. */
    uint32_t length;                            /**< Bytes written. */
    bool overflowed;                            /**< Whether a write did not fit, and was dropped. */
} MicrobenchmarkAml_t;

static uint8_t microbenchmark_table[MICROBENCHMARK_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t microbenchmark_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t microbenchmark_load;
static MicrobenchmarkAml_t microbenchmark_aml;
static uint8_t microbenchmark_declarations_table[MADE_UP_TABLE_HEADER + MICROBENCHMARK_AML_CAPACITY];

/**
 * @brief Appends @p length bytes to @p aml, or marks it overflowed when they do not fit.
 */
static void microbenchmark_put(MicrobenchmarkAml_t *aml, const uint8_t *bytes, uint32_t length)
{
    if (length > MICROBENCHMARK_AML_CAPACITY - aml->length)
    {
        aml->overflowed = true;
        return;
    }
    memcpy(aml->bytes + aml->length, bytes, length);
    aml->length += length;
}

/**
 * @brief Appends the NameSeg made of @p letter and the three hexadecimal digits of @p index.
 */
static void microbenchmark_put_segment(MicrobenchmarkAml_t *aml, char letter, uint32_t index)
{
    static const char digits[] = "0123456789ABCDEF";
    const uint8_t segment[4] = {(uint8_t) letter, (uint8_t) digits[(index >> 8u) & 0xFu],
                                (uint8_t) digits[(index >> 4u) & 0xFu], (uint8_t) digits[index & 0xFu]};

    microbenchmark_put(aml, segment, sizeof(segment));
}

/**
 * @brief Appends the PkgLength of a construct whose @p content bytes follow it, in one byte below 64
 *        in all, in two otherwise.
 */
static void microbenchmark_put_package_length(MicrobenchmarkAml_t *aml, uint32_t content)
{
    if (content + 1u < 0x40u)
    {
        const uint8_t length[1] = {(uint8_t) (content + 1u)};

        microbenchmark_put(aml, length, sizeof(length));
        return;
    }

    const uint32_t total = content + 2u;
    const uint8_t length[2] = {(uint8_t) (0x40u | (total & 0x0Fu)), (uint8_t) (total >> 4u)};

    microbenchmark_put(aml, length, sizeof(length));
}

/**
 * @brief Appends `Name (Nxxx, Package () {...})`: 8 packages of an address, a pin, LNKA and a source index.
 */
static void microbenchmark_put_package_name(MicrobenchmarkAml_t *aml, uint32_t index)
{
    static const uint8_t name[] = {0x08u};
    static const uint8_t outer[] = {0x12u, 0x4Bu, 0x07u, 0x08u};
    static const uint8_t inner[] = {0x12u, 0x0Eu, 0x04u, 0x0Cu};
    static const uint8_t rest[] = {0x0Au, 0x00u, 'L', 'N', 'K', 'A', 0x00u};

    microbenchmark_put(aml, name, sizeof(name));
    microbenchmark_put_segment(aml, 'N', index);
    microbenchmark_put(aml, outer, sizeof(outer));
    for (uint32_t element = 0u; element < 8u; ++element)
    {
        const uint8_t address[4] = {0xFFu, 0xFFu, (uint8_t) element, 0x00u};

        microbenchmark_put(aml, inner, sizeof(inner));
        microbenchmark_put(aml, address, sizeof(address));
        microbenchmark_put(aml, rest, sizeof(rest));
    }
}

/**
 * @brief Appends `Field (FREG, ByteAcc, NoLock, Preserve)` of 16 units of 8 bits, Fxxx from 16 * @p index.
 */
static void microbenchmark_put_field_list(MicrobenchmarkAml_t *aml, uint32_t index)
{
    static const uint8_t field[] = {0x5Bu, 0x81u};
    static const uint8_t region[] = {'F', 'R', 'E', 'G', 0x01u};
    static const uint8_t bits[] = {0x08u};

    microbenchmark_put(aml, field, sizeof(field));
    microbenchmark_put_package_length(aml, (uint32_t) sizeof(region) + 16u * 5u);
    microbenchmark_put(aml, region, sizeof(region));
    for (uint32_t unit = 0u; unit < 16u; ++unit)
    {
        microbenchmark_put_segment(aml, 'F', index * 16u + unit);
        microbenchmark_put(aml, bits, sizeof(bits));
    }
}

/**
 * @brief Appends the declaration @p index of @p kind.
 */
static void microbenchmark_put_declaration(MicrobenchmarkAml_t *aml, MicrobenchmarkKind_t kind, uint32_t index)
{
    static const uint8_t name[] = {0x08u};
    static const uint8_t string[] = {0x0Du, 'P', 'N', 'P', '0', 'C', '0', 'F', 0x00u};
    static const uint8_t buffer[] = {0x11u, 0x10u, 0x0Au, 0x0Du, 0x47u, 0x01u, 0x60u, 0x00u, 0x60u,
                                     0x00u, 0x01u, 0x01u, 0x22u, 0x02u, 0x00u, 0x79u, 0x00u};
    static const uint8_t method[] = {0x14u, 0x08u};
    static const uint8_t method_body[] = {0x01u, 0xA4u, 0x68u};
    static const uint8_t device[] = {0x5Bu, 0x82u, 0x05u};
    static const uint8_t scope[] = {0x10u, 0x0Cu, '\\', '_', 'S', 'B', '_', 0x08u};
    static const uint8_t region[] = {0x5Bu, 0x80u};
    static const uint8_t external[] = {0x15u, '\\', 'L', 'N', 'K', 'A', 0x06u, 0x00u};
    static const uint8_t zero[] = {0x00u};
    const uint8_t integer[5] = {0x0Cu, (uint8_t) index, (uint8_t) (index >> 8u), 0x01u, 0x00u};
    const uint8_t region_operands[6] = {0x01u, 0x0Bu, (uint8_t) (index * 4u), (uint8_t) (0x10u + ((index * 4u) >> 8u)),
                                        0x0Au, 0x04u};

    switch (kind)
    {
    case MICROBENCHMARK_INTEGER_NAMES:
    case MICROBENCHMARK_STRING_NAMES:
    case MICROBENCHMARK_BUFFER_NAMES:
        microbenchmark_put(aml, name, sizeof(name));
        microbenchmark_put_segment(aml, 'N', index);
        if (kind == MICROBENCHMARK_INTEGER_NAMES)
            microbenchmark_put(aml, integer, sizeof(integer));
        else if (kind == MICROBENCHMARK_STRING_NAMES)
            microbenchmark_put(aml, string, sizeof(string));
        else
            microbenchmark_put(aml, buffer, sizeof(buffer));
        return;
    case MICROBENCHMARK_PACKAGE_NAMES: microbenchmark_put_package_name(aml, index); return;
    case MICROBENCHMARK_METHODS:
        microbenchmark_put(aml, method, sizeof(method));
        microbenchmark_put_segment(aml, 'M', index);
        microbenchmark_put(aml, method_body, sizeof(method_body));
        return;
    case MICROBENCHMARK_DEVICES:
        microbenchmark_put(aml, device, sizeof(device));
        microbenchmark_put_segment(aml, 'D', index);
        return;
    case MICROBENCHMARK_SCOPES:
        microbenchmark_put(aml, scope, sizeof(scope));
        microbenchmark_put_segment(aml, 'S', index);
        microbenchmark_put(aml, zero, sizeof(zero));
        return;
    case MICROBENCHMARK_REGIONS:
        microbenchmark_put(aml, region, sizeof(region));
        microbenchmark_put_segment(aml, 'R', index);
        microbenchmark_put(aml, region_operands, sizeof(region_operands));
        return;
    case MICROBENCHMARK_FIELD_UNITS: microbenchmark_put_field_list(aml, index); return;
    case MICROBENCHMARK_EXTERNALS: microbenchmark_put(aml, external, sizeof(external)); return;
    }
}

/**
 * @brief Writes the AML of a load of @p kind in microbenchmark_aml: the Device the packages and the
 *        Externals name, or the region the field units lie in, then the declarations.
 *
 * @return The objects the load must declare.
 */
static uint32_t microbenchmark_write_declarations(MicrobenchmarkKind_t kind)
{
    static const uint8_t link[] = {0x5Bu, 0x82u, 0x05u, 'L', 'N', 'K', 'A'};
    static const uint8_t field_region[] = {0x5Bu, 0x80u, 'F',   'R',   'E',   'G',  0x01u,
                                           0x0Bu, 0x00u, 0x10u, 0x0Bu, 0x00u, 0x10u};
    const uint32_t count = (kind == MICROBENCHMARK_PACKAGE_NAMES) ? MICROBENCHMARK_PACKAGE_COUNT :
                           (kind == MICROBENCHMARK_FIELD_UNITS)   ? MICROBENCHMARK_FIELD_LISTS :
                                                                    MICROBENCHMARK_DECLARATIONS;

    memset(&microbenchmark_aml, 0, sizeof(microbenchmark_aml));
    if (kind == MICROBENCHMARK_PACKAGE_NAMES || kind == MICROBENCHMARK_EXTERNALS)
        microbenchmark_put(&microbenchmark_aml, link, sizeof(link));
    if (kind == MICROBENCHMARK_FIELD_UNITS)
        microbenchmark_put(&microbenchmark_aml, field_region, sizeof(field_region));
    for (uint32_t index = 0u; index < count; ++index)
        microbenchmark_put_declaration(&microbenchmark_aml, kind, index);
    if (kind == MICROBENCHMARK_EXTERNALS)
        return 1u;
    if (kind == MICROBENCHMARK_FIELD_UNITS)
        return 1u + count * 16u;
    return count + ((kind == MICROBENCHMARK_PACKAGE_NAMES) ? 1u : 0u);
}

/**
 * @brief Loads a DSDT of one kind of declaration as the release kernel does, its method bodies skipped,
 *        and checks that it declares every object it holds without counting a fault.
 */
static void microbenchmark_declarations(KernelTest_t *test, MicrobenchmarkKind_t kind)
{
    const uint32_t declarations = microbenchmark_write_declarations(kind);

    if (!kernel_test_check(test, !microbenchmark_aml.overflowed, "the table fits its buffer"))
        return;
    advanced_configuration_and_power_interface_simulated_host_open(&microbenchmark_host);
    if (!kernel_test_check(
            test,
            advanced_configuration_and_power_interface_machine_language_open(
                &microbenchmark_load, &microbenchmark_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
            "the namespace opens"))
        return;
    microbenchmark_load.skips_bodies = true;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &microbenchmark_load, made_up_definition_block(microbenchmark_declarations_table, "DSDT", 2u,
                                                       microbenchmark_aml.bytes, microbenchmark_aml.length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&microbenchmark_load);
    kernel_test_check(test,
                      microbenchmark_load.declarations == declarations && microbenchmark_load.load_errors == 0u &&
                          microbenchmark_load.values_deferred == 0u,
                      "the load declares every object its table holds, without a fault");
    kernel_test_measure(test, "declarations", microbenchmark_load.declarations);
    kernel_test_measure(test, "bytes", microbenchmark_aml.length);
    advanced_configuration_and_power_interface_machine_language_close(&microbenchmark_load);
    advanced_configuration_and_power_interface_simulated_host_close(&microbenchmark_host);
}

/**
 * @brief The loop of window W4: LOOP sets Local0 to 0, then runs
 *        `While (LLess (Local0, 0x03E8)) { Increment (Local0) }` and returns Local0.
 */
static const uint8_t microbenchmark_loop_aml[] = {
    0x14u, 0x14u, 'L',   'O',   'O',   'P',   0x00u, 0x70u, 0x00u, 0x60u, 0xA2u,
    0x08u, 0x95u, 0x60u, 0x0Bu, 0xE8u, 0x03u, 0x75u, 0x60u, 0xA4u, 0x60u,
};

/**
 * @brief The NameSeg of child @p index of the wide scope: 'X' and three base-36 digits.
 */
static uint32_t microbenchmark_child_name(uint32_t index)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const uint8_t name[4] = {'X', (uint8_t) digits[(index / 1296u) % 36u], (uint8_t) digits[(index / 36u) % 36u],
                             (uint8_t) digits[index % 36u]};

    return advanced_configuration_and_power_interface_machine_language_name_segment(name, 0u);
}

/**
 * @brief Loads the loop as a DSDT of @p revision, evaluates LOOP, and checks that it took its 1,000
 *        turns without allocating.
 */
static void microbenchmark_loop(KernelTest_t *test, uint8_t revision)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    uint32_t objects = 0u;

    if (!made_up_load_table(test, &microbenchmark_load, &microbenchmark_host, microbenchmark_table, revision,
                            microbenchmark_loop_aml, sizeof(microbenchmark_loop_aml)))
        return;
    objects = microbenchmark_load.heap.objects;
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &microbenchmark_load, "\\LOOP", NULL, 0u, &result) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          result.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          result.payload == MICROBENCHMARK_TURNS,
                      revision < 2u ? "the loop takes 1,000 turns under revision 1" : "the loop takes 1,000 turns");
    kernel_test_check(test, microbenchmark_load.heap.objects == objects, "the loop allocates nothing");
    kernel_test_measure(test, revision < 2u ? "back_edges_revision_1" : "back_edges",
                        microbenchmark_load.evaluation.iterations);
    advanced_configuration_and_power_interface_machine_language_close(&microbenchmark_load);
    advanced_configuration_and_power_interface_simulated_host_close(&microbenchmark_host);
}

/**
 * @brief Builds a scope of 3,346 children at the root, then looks 1,000 of them up by name, and checks
 *        that each lookup finds its child.
 */
static void microbenchmark_lookups(KernelTest_t *test)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &microbenchmark_load.space;
    uint32_t scope = 0u;
    uint32_t child = 0u;
    uint32_t found = 0u;
    bool built = true;

    advanced_configuration_and_power_interface_simulated_host_open(&microbenchmark_host);
    if (!kernel_test_check(
            test,
            advanced_configuration_and_power_interface_machine_language_open(
                &microbenchmark_load, &microbenchmark_host.host, MICROBENCHMARK_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
            "the namespace opens"))
        return;
    built = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
                space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "WIDE", 0u),
                MICROBENCHMARK_NODE(DEVICE), 1u,
                &scope) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    for (uint32_t index = 0u; built && index < MICROBENCHMARK_SCOPE_CHILDREN; ++index)
        built = advanced_configuration_and_power_interface_machine_language_namespace_add_child(
                    space, scope, microbenchmark_child_name(index), MICROBENCHMARK_NODE(INTEGER), 1u, &child) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    kernel_test_check(test, built, "the scope holds its 3,346 children");

    const uint32_t lookups = space->lookups;
    const uint32_t probes = space->probes;

    for (uint32_t turn = 0u; turn < MICROBENCHMARK_TURNS; ++turn)
    {
        const uint32_t name = microbenchmark_child_name((turn * 7u) % MICROBENCHMARK_SCOPE_CHILDREN);
        const uint32_t index =
            advanced_configuration_and_power_interface_machine_language_namespace_find_child(space, scope, name);

        found +=
            (index != 0u &&
             advanced_configuration_and_power_interface_machine_language_namespace_node(space, index)->name == name) ?
                1u :
                0u;
    }
    kernel_test_check(test, found == MICROBENCHMARK_TURNS && space->lookups - lookups == MICROBENCHMARK_TURNS,
                      "each of the 1,000 lookups finds its child");
    kernel_test_measure(test, "probes", space->probes - probes);
    advanced_configuration_and_power_interface_machine_language_close(&microbenchmark_load);
    advanced_configuration_and_power_interface_simulated_host_close(&microbenchmark_host);
}

/**
 * @brief Window W4 of §7.4, counted in ring 0 with `tools/instructions.sh --inside
 *        kernel_test_body_the_window_w4_loop_of_a_thousand_turns` against
 *        tools/acpi-microbenchmark.debug.budget: the loop of 1,000 turns under revision 2, then the same
 *        under revision 1, neither allocating.
 */
KERNEL_TEST(the_window_w4_loop_of_a_thousand_turns)
{
    microbenchmark_loop(test, 2u);
    microbenchmark_loop(test, 1u);
}

/**
 * @brief The third microbenchmark of window W4: 1,000 lookups in a scope of 3,346 children, counted
 *        with `tools/instructions.sh --inside kernel_test_body_a_thousand_lookups_in_a_wide_scope`; the
 *        scope it builds first costs more than the lookups, so it has no budget of its own.
 */
KERNEL_TEST(a_thousand_lookups_in_a_wide_scope) { microbenchmark_lookups(test); }

/**
 * @brief Window W1 one kind of declaration at a time (§7.4): 256 `Name`s of a DWord constant, loaded as the
 *        release kernel loads, method bodies skipped. On a debug kernel, `tools/instructions.sh --machine q35
 *        --inside advanced_configuration_and_power_interface_machine_language_declare_table --skip 1 --times 1
 *        --append lpl.test=advanced_configuration_and_power_interface_microbenchmarks.<test> kernel/lpl.kernel`
 *        counts the load of the test's table alone: the boot's load of q35's DSDT is the call it skips.
 */
KERNEL_TEST(a_load_of_256_integer_names) { microbenchmark_declarations(test, MICROBENCHMARK_INTEGER_NAMES); }

/**
 * @brief Window W1 one kind at a time: 256 `Name`s of a String, counted as a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_string_names) { microbenchmark_declarations(test, MICROBENCHMARK_STRING_NAMES); }

/**
 * @brief Window W1 one kind at a time: 256 `Name`s of a 13-byte ResourceTemplate, counted as
 *        a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_buffer_names) { microbenchmark_declarations(test, MICROBENCHMARK_BUFFER_NAMES); }

/**
 * @brief Window W1 one kind at a time: 32 `Name`s of a package of 8 packages of 4 elements, a _PRT's shape,
 *        1,280 elements in all, counted as a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_32_names_of_eight_packages) { microbenchmark_declarations(test, MICROBENCHMARK_PACKAGE_NAMES); }

/**
 * @brief Window W1 one kind at a time: 256 `Method`s, counted as a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_methods) { microbenchmark_declarations(test, MICROBENCHMARK_METHODS); }

/**
 * @brief Window W1 one kind at a time: 256 empty `Device`s, each TermList handed back to the interpreter,
 *        counted as a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_devices) { microbenchmark_declarations(test, MICROBENCHMARK_DEVICES); }

/**
 * @brief Window W1 one kind at a time: 256 `Scope (\_SB)` that each declare one `Name`, counted as
 *        a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_scopes) { microbenchmark_declarations(test, MICROBENCHMARK_SCOPES); }

/**
 * @brief Window W1 one kind at a time: 256 `OperationRegion`s of constant operands, counted as
 *        a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_operation_regions) { microbenchmark_declarations(test, MICROBENCHMARK_REGIONS); }

/**
 * @brief Window W1 one kind at a time: 256 field units in 16 field lists, counted as
 *        a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_field_units) { microbenchmark_declarations(test, MICROBENCHMARK_FIELD_UNITS); }

/**
 * @brief Window W1 one kind at a time: 256 `External`s of a Device the table declares, which create nothing,
 *        counted as a_load_of_256_integer_names says.
 */
KERNEL_TEST(a_load_of_256_externals) { microbenchmark_declarations(test, MICROBENCHMARK_EXTERNALS); }
