#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_machine_language, KERNEL_TEST_STAGE_INITIALIZATION);

#define MADE_UP_TABLE_CAPACITY 2048u           /**< Bytes of each buffer a made-up table is written in. */
#define MADE_UP_TABLE_COUNT    4u              /**< Made-up tables one test may load. */
#define MADE_UP_MAX_NODES      1024u           /**< The node cap of a made-up load. */
#define MADE_UP_MAX_ARENA      (1024u * 1024u) /**< The arena cap of a made-up load. */
#define MADE_UP_HEADER         36u             /**< Bytes of a table header. */
#define MADE_UP_MULTI_SEGMENTS 255u            /**< NameSegs of the longest multi path. */

static uint8_t made_up_tables[MADE_UP_TABLE_COUNT][MADE_UP_TABLE_CAPACITY];
static uint8_t made_up_name[2u + MADE_UP_MULTI_SEGMENTS * 4u + 1u];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t made_up_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t made_up_load;

/**
 * @brief Writes table number @p index: a header of @p signature, revision 2, then @p length bytes of
 *        AML, its checksum made to hold.
 *
 * @return The table's header.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *made_up_table(uint32_t index, const char *signature,
                                                                              const uint8_t *aml, uint32_t length)
{
    uint8_t *table = made_up_tables[index];
    uint8_t sum = 0u;

    memset(table, 0, MADE_UP_TABLE_CAPACITY);
    memcpy(table, signature, 4u);
    table[4] = (uint8_t) (MADE_UP_HEADER + length);
    table[5] = (uint8_t) ((MADE_UP_HEADER + length) >> 8u);
    table[8] = 2u;
    memcpy(table + 10, "LPLTST", 6u);
    memcpy(table + 16, "MADEUPML", 8u);
    memcpy(table + MADE_UP_HEADER, aml, length);
    for (uint32_t byte = 0u; byte < MADE_UP_HEADER + length; ++byte)
        sum = (uint8_t) (sum + table[byte]);
    table[9] = (uint8_t) (0u - sum);
    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) table;
}

/**
 * @brief Opens the made-up load on a fresh simulated host.
 */
static bool made_up_open(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_simulated_host_open(&made_up_host);
    return kernel_test_check(test,
                             advanced_configuration_and_power_interface_machine_language_open(
                                 &made_up_load, &made_up_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA),
                             "the namespace opens");
}

/**
 * @brief Checks the namespace holds together (every live node found by its parent and name, one slot
 *        each, as many as it counts), then closes the made-up load and checks that every page went
 *        back to the host.
 */
static void made_up_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_load.space;
    uint32_t live = 1u;
    uint32_t found = 0u;
    uint32_t slots = 0u;

    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT + 1u;
         index < space->node_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
            advanced_configuration_and_power_interface_machine_language_namespace_node(space, index);

        if ((node->flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FREE) != 0u)
            continue;
        ++live;
        found += (advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                      &made_up_load.space, node->parent, node->name) == index) ?
                     1u :
                     0u;
    }
    for (uint32_t slot = 0u; slot < space->slot_capacity; ++slot)
        slots += (space->slots[slot] != 0u) ? 1u : 0u;
    kernel_test_check(test, live == space->live_nodes && found + 1u == live && slots + 1u == live,
                      "every live node is found by its parent and name, in one slot each");
    advanced_configuration_and_power_interface_machine_language_close(&made_up_load);
    kernel_test_check(test, made_up_host.outstanding_pages == 0u, "every page goes back to the host");
}

/**
 * @brief Loads @p aml as one DSDT, both passes.
 */
static bool made_up_load_one(KernelTest_t *test, const uint8_t *aml, uint32_t length)
{
    if (!made_up_open(test))
        return false;
    advanced_configuration_and_power_interface_machine_language_declare_table(&made_up_load,
                                                                              made_up_table(0u, "DSDT", aml, length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_measure(test, "decode_errors", made_up_load.decode_errors);
    return true;
}

/**
 * @brief How many times the load decoded @p opcode.
 */
static uint32_t made_up_count(uint32_t opcode) { return made_up_load.opcodes[opcode]; }

/**
 * @brief The NameSeg of four characters as the namespace keys it.
 */
static uint32_t made_up_segment(const char *characters)
{
    return advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) characters, 0u);
}

/**
 * @brief Resolves the NameString written in @p text from @p scope.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t made_up_resolve(uint32_t scope, const char *text,
                                                                                     uint32_t *out_index)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;
    uint32_t after = 0u;
    const uint32_t length = (uint32_t) strlen(text);

    memcpy(made_up_name, text, length);
    if (advanced_configuration_and_power_interface_machine_language_read_name_string(made_up_name, 0u, length, &name,
                                                                                     &after) !=
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK)
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED;
    return advanced_configuration_and_power_interface_machine_language_namespace_resolve(
        &made_up_load.space, scope, made_up_name, &name, out_index);
}

/**
 * @brief A PkgLength of 1 to 4 bytes gives the value its bytes encode, and the construct it opens
 *        ends at its own first byte plus that value: the length counts its own bytes.
 */
KERNEL_TEST(a_package_length_of_one_to_four_bytes_ends_where_it_says)
{
    static const struct {
        uint8_t bytes[4];
        uint32_t size;
        uint32_t value;
    } encodings[] = {
        {{0x3Fu},                      1u, 0x3Fu     },
        {{0x41u, 0x12u},               2u, 0x121u    },
        {{0x82u, 0x34u, 0x12u},        3u, 0x12342u  },
        {{0xC3u, 0x56u, 0x34u, 0x12u}, 4u, 0x1234563u},
    };
    uint8_t bytes[8] = {0};

    for (uint32_t index = 0u; index < sizeof(encodings) / sizeof(encodings[0]); ++index)
    {
        uint32_t value = 0u;
        uint32_t after = 0u;
        uint32_t end = 0u;

        memcpy(bytes + 2, encodings[index].bytes, encodings[index].size);
        kernel_test_check(test,
                          advanced_configuration_and_power_interface_machine_language_read_package_length(
                              bytes, 2u, sizeof(bytes), &value, &after) ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                              value == encodings[index].value && after == 2u + encodings[index].size,
                          "the encoding gives its value and its size");
        kernel_test_check(test,
                          advanced_configuration_and_power_interface_machine_language_read_package_end(
                              bytes, 2u, UINT32_MAX, &end, &after) ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                              end == 2u + encodings[index].value,
                          "the construct ends at the length's first byte plus its value");
    }
}

/**
 * @brief A PkgLength that ends past the construct around it, that is shorter than its own bytes, or
 *        whose bytes pass the limit, is refused, each with its own status.
 */
KERNEL_TEST(a_package_length_that_crosses_its_parent_is_refused)
{
    static const uint8_t crossing[] = {0x10u, 0x00u};
    static const uint8_t shorter[] = {0x41u, 0x00u};
    static const uint8_t cut[] = {0x82u, 0x34u};
    uint32_t end = 0u;
    uint32_t after = 0u;

    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_read_package_end(crossing, 0u, 15u, &end, &after) ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_PACKAGE_LENGTH_OUT_OF_PARENT,
        "a length of 16 in a parent of 15 bytes crosses it");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_read_package_end(crossing, 0u, 16u, &end, &after) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            end == 16u,
        "one that ends at its parent's end fits");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_machine_language_read_package_end(shorter, 0u, 64u, &end, &after) ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
        "a value of 1 in two bytes is shorter than itself");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_package_end(cut, 0u, sizeof(cut),
                                                                                                   &end, &after) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "three bytes announced and two there are cut");
}

/**
 * @brief NameStrings of every prefix and length are read: the root alone, two parent prefixes, a
 *        dual path, a multi path of 255 segments; a multi path of 0 segments, a NameSeg with a
 *        lower-case character and a root followed by a parent prefix are refused.
 */
KERNEL_TEST(name_strings_of_every_prefix_and_length_are_read)
{
    static const uint8_t root[] = {'\\', 0x00u};
    static const uint8_t parents[] = {'^', '^', 'A', 'B', 'C', 'D'};
    static const uint8_t dual[] = {0x2Eu, 'A', 'B', 'C', 'D', '_', 'E', 'F', '0'};
    static const uint8_t empty_multi[] = {0x2Fu, 0x00u, 'A', 'B', 'C', 'D'};
    static const uint8_t lower[] = {'A', 'b', 'C', 'D'};
    static const uint8_t root_then_parent[] = {'\\', '^', 'A', 'B', 'C', 'D'};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;
    uint32_t after = 0u;

    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          root, 0u, sizeof(root), &name, &after) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          name.root && name.segment_count == 0u && after == 2u,
                      "`\\` and NullName name the root");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          parents, 0u, sizeof(parents), &name, &after) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          !name.root && name.parent_prefixes == 2u && name.segment_count == 1u && name.segments == 2u &&
                          after == 6u,
                      "`^^ABCD` climbs twice, then names one segment");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          dual, 0u, sizeof(dual), &name, &after) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          name.segment_count == 2u && name.segments == 1u && after == 9u,
                      "a dual path names two segments");

    made_up_name[0] = 0x2Fu;
    made_up_name[1] = (uint8_t) MADE_UP_MULTI_SEGMENTS;
    for (uint32_t segment = 0u; segment < MADE_UP_MULTI_SEGMENTS; ++segment)
        memcpy(made_up_name + 2u + segment * 4u, "A___", 4u);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          made_up_name, 0u, 2u + MADE_UP_MULTI_SEGMENTS * 4u, &name, &after) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          name.segment_count == MADE_UP_MULTI_SEGMENTS && after == 2u + MADE_UP_MULTI_SEGMENTS * 4u,
                      "a multi path names 255 segments");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          made_up_name, 0u, 1u + MADE_UP_MULTI_SEGMENTS * 4u, &name, &after) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "one byte short, it is cut");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          empty_multi, 0u, sizeof(empty_multi), &name, &after) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a multi path of 0 segments is refused");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          lower, 0u, sizeof(lower), &name, &after) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a lower-case character is refused");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_read_name_string(
                          root_then_parent, 0u, sizeof(root_then_parent), &name, &after) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a parent prefix after the root is refused");
}

/**
 * @brief Every descriptor is found again from its own encoding, and the bytes the byte table leaves
 *        out, 0x5B 0x10 and 0x5B 0x11 and 0xA9 included, are no opcode.
 */
KERNEL_TEST(every_descriptor_is_found_by_its_encoding)
{
    static const uint8_t not_opcodes[][2] = {
        {0x02u, 0x00u},
        {0xA9u, 0x00u},
        {0x5Bu, 0x10u},
        {0x5Bu, 0x11u},
        {0x5Bu, 0x00u},
        {'A',   0x00u}
    };
    uint32_t found = 0u;

    for (uint32_t opcode = 1u; opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
    {
        const uint16_t encoding =
            advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->encoding;
        const uint8_t bytes[2] = {(uint8_t) ((encoding > 0xFFu) ? (encoding >> 8u) : encoding), (uint8_t) encoding};
        uint32_t read = 0u;
        uint32_t after = 0u;

        if (advanced_configuration_and_power_interface_machine_language_read_opcode(bytes, 0u, 2u, &read, &after) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            read == opcode && after == ((encoding > 0xFFu) ? 2u : 1u))
            ++found;
    }
    kernel_test_measure(test, "descriptors", found);
    kernel_test_check(test, found == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT - 1u,
                      "each descriptor is found from its encoding");
    kernel_test_check(test, found == 113u, "the byte table holds 113 encodings, Processor and Unload included");
    for (uint32_t index = 0u; index < sizeof(not_opcodes) / sizeof(not_opcodes[0]); ++index)
    {
        uint32_t read = 0u;
        uint32_t after = 0u;

        kernel_test_check(test,
                          advanced_configuration_and_power_interface_machine_language_read_opcode(
                              not_opcodes[index], 0u, 2u, &read, &after) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE,
                          "a byte outside the table is no opcode");
    }
}

/**
 * @brief The structures the interpreter keeps by the thousand have the sizes the specification
 *        gives, on both targets: they hold no pointer.
 */
KERNEL_TEST(the_interpreter_structures_have_their_sizes)
{
    kernel_test_measure(test, "node_bytes",
                        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t));
    kernel_test_measure(test, "value_bytes",
                        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t));
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t) == 40u,
                      "a node is 40 bytes");
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t) == 16u,
                      "a value is 16 bytes");
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguagePendingOperator_t) == 16u,
                      "a pending operator is 16 bytes");
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageBlock_t) == 16u,
                      "a block is 16 bytes");
}

/**
 * @brief 450 children of the root, a load of almost half the first hash table, are each found by
 *        their parent and name, and stay in declaration order.
 */
KERNEL_TEST(a_child_is_found_by_its_parent_and_name_under_collisions)
{
    char name[5] = "C000";
    uint32_t found = 0u;
    uint32_t in_order = 0u;

    if (!made_up_open(test))
        return;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_load.space;
    const uint32_t first = space->node_count;

    for (uint32_t index = 0u; index < 450u; ++index)
    {
        uint32_t node = 0u;

        name[1] = (char) ('0' + index / 100u);
        name[2] = (char) ('0' + (index / 10u) % 10u);
        name[3] = (char) ('0' + index % 10u);
        advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment(name),
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE, 1u, &node);
    }
    space->lookups = 0u;
    space->probes = 0u;
    for (uint32_t index = 0u; index < 450u; ++index)
    {
        name[1] = (char) ('0' + index / 100u);
        name[2] = (char) ('0' + (index / 10u) % 10u);
        name[3] = (char) ('0' + index % 10u);
        found += (advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                      space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment(name)) ==
                  first + index) ?
                     1u :
                     0u;
    }
    kernel_test_measure(test, "lookups", space->lookups);
    kernel_test_measure(test, "probes", space->probes);
    for (uint32_t node = advanced_configuration_and_power_interface_machine_language_namespace_node(
                             space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT)
                             ->first_child;
         node != 0u;
         node = advanced_configuration_and_power_interface_machine_language_namespace_node(space, node)->next_sibling)
        in_order += (node >= first && node == first + in_order) ? 1u : (node < first ? 0u : 1000u);
    kernel_test_check(test, found == 450u, "every child is found");
    kernel_test_check(test, space->probes > space->lookups, "some lookups met a collision");
    kernel_test_check(test, in_order == 450u, "the children follow in declaration order");
    made_up_close(test);
}

/**
 * @brief Removing every third of 450 children leaves no tombstone: the others are all found, the
 *        removed ones are not, and new children reuse the removed indices.
 */
KERNEL_TEST(a_removed_node_leaves_no_tombstone)
{
    char name[5] = "C000";
    uint32_t kept = 0u;
    uint32_t gone = 0u;
    uint32_t reused = 0u;

    if (!made_up_open(test))
        return;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_load.space;
    const uint32_t first = space->node_count;

    for (uint32_t index = 0u; index < 450u; ++index)
    {
        uint32_t node = 0u;

        name[1] = (char) ('0' + index / 100u);
        name[2] = (char) ('0' + (index / 10u) % 10u);
        name[3] = (char) ('0' + index % 10u);
        advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment(name),
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE, 1u, &node);
    }
    for (uint32_t index = 0u; index < 450u; index += 3u)
        advanced_configuration_and_power_interface_machine_language_namespace_remove(space, first + index);
    for (uint32_t index = 0u; index < 450u; ++index)
    {
        name[1] = (char) ('0' + index / 100u);
        name[2] = (char) ('0' + (index / 10u) % 10u);
        name[3] = (char) ('0' + index % 10u);

        const uint32_t node = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
            space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment(name));

        kept += (index % 3u != 0u && node == first + index) ? 1u : 0u;
        gone += (index % 3u == 0u && node == 0u) ? 1u : 0u;
    }
    for (uint32_t index = 0u; index < 150u; ++index)
    {
        uint32_t node = 0u;

        name[0] = 'R';
        name[1] = (char) ('0' + index / 100u);
        name[2] = (char) ('0' + (index / 10u) % 10u);
        name[3] = (char) ('0' + index % 10u);
        advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment(name),
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE, 1u, &node);
        reused +=
            (node < first + 450u && advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                                        space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                                        made_up_segment(name)) == node) ?
                1u :
                0u;
    }
    kernel_test_check(test, kept == 300u, "every child kept is found");
    kernel_test_check(test, gone == 150u, "no removed child is found");
    kernel_test_check(test, reused == 150u, "new children take the removed indices and are found");
    made_up_close(test);
}

/**
 * @brief A single NameSeg searches its scope, then each parent, from 0 to 6 levels up; a dual path
 *        and a path with parent prefixes never search, and a prefix that climbs above the root
 *        names nothing.
 */
KERNEL_TEST(a_single_name_searches_up_to_the_root_and_a_path_does_not)
{
    static const char *const path[] = {"A___", "B___", "C___", "D___", "E___", "F___"};
    uint32_t nodes[6] = {0u};
    uint32_t root_x = 0u;
    uint32_t a_y = 0u;
    uint32_t a_y_z = 0u;
    uint32_t found = 0u;

    if (!made_up_open(test))
        return;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_load.space;
    uint32_t parent = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;

    for (uint32_t depth = 0u; depth < 6u; ++depth)
    {
        advanced_configuration_and_power_interface_machine_language_namespace_add_child(
            space, parent, made_up_segment(path[depth]),
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE, 1u, &nodes[depth]);
        parent = nodes[depth];
    }
    advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment("X___"),
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE, 1u, &root_x);
    advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        space, nodes[0], made_up_segment("Y___"),
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE, 1u, &a_y);
    advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        space, a_y, made_up_segment("Z___"), ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE,
        1u, &a_y_z);

    kernel_test_check(test,
                      made_up_resolve(nodes[4], "F___", &found) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          found == nodes[5],
                      "a child of the scope is found without climbing");
    kernel_test_check(test,
                      made_up_resolve(nodes[5], "X___", &found) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          found == root_x,
                      "a child of the root is found 6 levels up");
    kernel_test_check(test,
                      made_up_resolve(nodes[2], "Y___", &found) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          found == a_y,
                      "a child of an ancestor is found on the way up");
    kernel_test_check(test,
                      made_up_resolve(nodes[2], ".Y___Z___", &found) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND,
                      "a dual path is read from the scope alone");
    kernel_test_check(test,
                      made_up_resolve(nodes[0], ".Y___Z___", &found) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          found == a_y_z,
                      "and found there when it exists");
    kernel_test_check(test,
                      made_up_resolve(nodes[5], "^^X___", &found) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND,
                      "a parent prefix stops the search");
    kernel_test_check(test,
                      made_up_resolve(nodes[5], "^^^^^^X___", &found) ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                          found == root_x,
                      "six prefixes from depth 6 reach the root");
    kernel_test_check(test,
                      made_up_resolve(nodes[5], "^^^^^^^X___", &found) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND,
                      "a seventh climbs above it");
    made_up_close(test);
}

/**
 * @brief A call takes the arguments its method declares, even when the method is declared after
 *        the call: `Store (MTHA (One, Zero), Local0)`, `Store (MTHA (Zero, One), Local1)` and
 *        `Store (MTH4 (Zero, Zero, Zero, Zero), Local2)` keep every Zero as an argument, where a wrong
 *        count turns one into a NullName target or an orphan.
 */
KERNEL_TEST(a_method_call_takes_the_arguments_its_method_declares)
{
    static const uint8_t aml[] = {
        0x14u, 0x20u, 'M',   'A',   'I',   'N',   0x00u, 0x70u, 'M',   'T',   'H',   'A',   0x01u,
        0x00u, 0x60u, 0x70u, 'M',   'T',   'H',   'A',   0x00u, 0x01u, 0x61u, 0x70u, 'M',   'T',
        'H',   '4',   0x00u, 0x00u, 0x00u, 0x00u, 0x62u, 0x14u, 0x08u, 'M',   'T',   'H',   'A',
        0x02u, 0xA4u, 0x68u, 0x14u, 0x08u, 'M',   'T',   'H',   '4',   0x04u, 0xA4u, 0x6Bu,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u, "the table decodes without a fault");
    kernel_test_check(test, made_up_load.method_calls == 3u, "every call is seen as a call");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ZERO) == 6u,
                      "every Zero is an argument, four of them to a method of four arguments");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_METHOD) == 3u,
                      "every method is decoded");
    made_up_close(test);
}

/**
 * @brief CondRefOf never calls its operand: `CondRefOf (MTHB, Local0)` names the method, and only
 *        `MTHB (Zero)` in the body calls it. Calling it there would take Local0 as its argument.
 */
KERNEL_TEST(cond_ref_of_never_calls_its_operand)
{
    static const uint8_t aml[] = {
        0x14u, 0x08u, 'M',   'T',   'H', 'B', 0x01u, 0xA4u, 0x68u, 0x14u, 0x16u, 'M', 'A', 'I', 'N',   0x00u,
        0xA0u, 0x0Fu, 0x5Bu, 0x12u, 'M', 'T', 'H',   'B',   0x60u, 0x70u, 'M',   'T', 'H', 'B', 0x00u, 0x61u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u, "the table decodes without a fault");
    kernel_test_check(test, made_up_load.method_calls == 1u, "only the call in the body calls the method");
    kernel_test_check(
        test,
        made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CONDITIONAL_REFERENCE_OF) ==
            1u,
        "CondRefOf is decoded");
    made_up_close(test);
}

/**
 * @brief A name a method declares hides the same name further up while the method lasts, and is
 *        gone after it: in MAIN, FOO_ is the Name, not the method of one argument at the root, and a
 *        Name MAIN creates at the root no longer resolves in AFTR.
 */
KERNEL_TEST(a_name_a_method_declares_lasts_as_long_as_the_method)
{
    static const uint8_t aml[] = {
        0x14u, 0x08u, 'F',   'O', 'O', '_', 0x01u, 0xA4u, 0x68u, 0x14u, 0x1Au, 'M', 'A', 'I',
        'N',   0x00u, 0x08u, 'F', 'O', 'O', '_',   0x0Au, 0x05u, 0x70u, 'F',   'O', 'O', '_',
        0x60u, 0x08u, '\\',  'T', 'M', 'P', 'N',   0x01u, 0x14u, 0x13u, 'A',   'F', 'T', 'R',
        0x00u, 0x70u, 'F',   'O', 'O', '_', 0x00u, 0x61u, 0x70u, 'T',   'M',   'P', 'N', 0x62u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u, "the table decodes without a fault");
    kernel_test_check(test, made_up_load.method_calls == 1u, "FOO_ is a call only in AFTR");
    kernel_test_check(test, made_up_load.unresolved_names == 1u, "TMPN no longer resolves in AFTR");
    kernel_test_check(test, made_up_load.temporary_count == 0u && made_up_load.frame_count == 0u,
                      "no temporary node outlives its method");
    made_up_close(test);
}

/**
 * @brief An External gives the argument count of a method no table declares, under a path that
 *        exists or that no table declares either: `\_SB_.PCI0.ECRD (Zero)` and `\_SB_.MTHE (Zero)`
 *        keep their Zero as an argument.
 */
KERNEL_TEST(an_external_names_a_method_no_table_declares)
{
    static const uint8_t aml[] = {
        0x15u, '\\',  0x2Fu, 0x03u, '_',   'S',  'B',   '_', 'P', 'C',   'I',   '0',   'E',   'C',
        'R',   'D',   0x08u, 0x01u, 0x15u, '\\', 0x2Eu, '_', 'S', 'B',   '_',   'M',   'T',   'H',
        'E',   0x08u, 0x01u, 0x14u, 0x25u, 'M',  'A',   'I', 'N', 0x00u, 0x70u, '\\',  0x2Fu, 0x03u,
        '_',   'S',   'B',   '_',   'P',   'C',  'I',   '0', 'E', 'C',   'R',   'D',   0x00u, 0x60u,
        0x70u, '\\',  0x2Eu, '_',   'S',   'B',  '_',   'M', 'T', 'H',   'E',   0x00u, 0x61u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u && made_up_load.load_errors == 0u,
                      "the table loads and decodes without a fault");
    kernel_test_check(test, made_up_load.method_calls == 2u, "both Externals are called");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ZERO) == 2u,
                      "each Zero is an argument, not a NullName target");
    made_up_close(test);
}

/**
 * @brief A declaration whose path does not exist is counted and skipped alone: the method after it
 *        is still declared.
 */
KERNEL_TEST(a_declaration_whose_path_does_not_exist_is_skipped_alone)
{
    static const uint8_t aml[] = {
        0x08u, '\\',  0x2Eu, 'N',   'O', 'P', 'E', 'A', 'B',   'C',
        'D',   0x01u, 0x14u, 0x07u, 'G', 'O', 'O', 'D', 0x00u, 0xA3u,
    };
    uint32_t good = 0u;

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.load_errors == 1u && made_up_load.decode_errors == 0u,
                      "the missing path is the one fault, met while declaring");
    kernel_test_check(
        test,
        made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, "GOOD", &good) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            advanced_configuration_and_power_interface_machine_language_namespace_node(&made_up_load.space, good)
                    ->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD,
        "the method after it is declared");
    made_up_close(test);
}

/**
 * @brief A Connection to an inline buffer decodes the buffer as a term, and the unit after it is
 *        declared.
 */
KERNEL_TEST(a_connection_buffer_is_decoded_as_a_buffer)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'G',   'I',   'O',   '0',   0x08u, 0x00u, 0x01u, 0x5Bu, 0x81u, 0x10u, 'G',   'I',
        'O',   '0',   0x01u, 0x02u, 0x11u, 0x03u, 0x01u, 0x8Cu, 'P',   'I',   'N',   '0',   0x01u,
    };
    uint32_t pin = 0u;

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u, "the table decodes without a fault");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BUFFER) == 1u,
                      "the connection's buffer is an opcode");
    kernel_test_check(test, made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ONE) == 2u,
                      "its size is decoded, as is the region's length");
    kernel_test_check(
        test,
        made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, "PIN0", &pin) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            advanced_configuration_and_power_interface_machine_language_namespace_node(&made_up_load.space, pin)
                    ->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_FIELD_UNIT,
        "the unit after it is declared");
    made_up_close(test);
}

/**
 * @brief A PkgLength that passes its method's end abandons the rest of its table, an unterminated
 *        string is cut, a statement where an operand is expected is refused, and each next table loads
 *        all the same.
 */
KERNEL_TEST(a_faulty_table_gives_way_to_the_next)
{
    static const uint8_t crossing[] = {0x14u, 0x0Au, 'B', 'A', 'D', '_', 0x00u, 0xA0u, 0x3Fu, 0x00u, 0xA3u};
    static const uint8_t unterminated[] = {0x08u, 'S', 'T', 'R', '_', 0x0Du, 'a', 'b', 'c'};
    static const uint8_t good[] = {0x14u, 0x07u, 'G', 'O', 'O', 'D', 0x00u, 0xA3u};
    static const uint8_t statement_operand[] = {0x14u, 0x0Cu, 'B',   'A',   'D',   '2',  0x00u,
                                                0x70u, 0xA0u, 0x03u, 0x01u, 0xA3u, 0x60u};

    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", crossing, sizeof(crossing)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", unterminated, sizeof(unterminated)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(2u, "SSDT", good, sizeof(good)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(3u, "SSDT", statement_operand, sizeof(statement_operand)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(
        test,
        made_up_load.tables[0].decode_errors == 1u &&
            made_up_load.tables[0].first_error ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_PACKAGE_LENGTH_OUT_OF_PARENT,
        "the crossing PkgLength is the first table's only fault");
    kernel_test_check(test,
                      made_up_load.tables[1].decode_errors == 1u &&
                          made_up_load.tables[1].first_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "the unterminated string is cut");
    kernel_test_check(test,
                      made_up_load.tables[2].decode_errors == 0u &&
                          made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP) == 1u,
                      "the third table decodes whole");
    kernel_test_check(test,
                      made_up_load.tables[3].decode_errors == 1u &&
                          made_up_load.tables[3].first_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE,
                      "an If where Store expects an operand is refused");
    made_up_close(test);
}

/**
 * @brief An External cut short after its name at the end of its table reads nothing past the table,
 *        and an External of NullName creates no node and is counted.
 */
KERNEL_TEST(an_external_cut_short_or_without_a_name_creates_nothing)
{
    static const uint8_t cut[] = {0x15u, 0x2Eu, 'A', 'A', 'A', 'A', 'B', 'B', 'B', 'B'};
    static const uint8_t nameless[] = {0x15u, 0x00u, 0x08u, 0x01u};

    if (!made_up_open(test))
        return;

    const uint32_t nodes = made_up_load.space.live_nodes;

    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", cut, sizeof(cut)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", nameless, sizeof(nameless)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test, made_up_load.space.live_nodes == nodes, "neither External creates a node");
    kernel_test_check(test, made_up_load.load_errors == 2u, "the cut one and the nameless one are counted, once each");
    made_up_close(test);
}

/**
 * @brief Three hundred Externals on paths no table declares are all placed, with no cap: a call to
 *        the last keeps its argument.
 */
KERNEL_TEST(three_hundred_externals_on_missing_paths_are_all_placed)
{
    static uint8_t aml[300u * 18u + 32u];
    uint32_t length = 0u;

    for (uint32_t index = 0u; index < 300u; ++index)
    {
        static const uint8_t prefix[] = {0x15u, '\\', 0x2Fu, 0x03u, '_', 'S', 'B', '_'};

        memcpy(aml + length, prefix, sizeof(prefix));
        length += (uint32_t) sizeof(prefix);
        aml[length++] = 'D';
        aml[length++] = (uint8_t) ('0' + index / 100u);
        aml[length++] = (uint8_t) ('0' + (index / 10u) % 10u);
        aml[length++] = (uint8_t) ('0' + index % 10u);
        memcpy(aml + length, "MTH_", 4u);
        length += 4u;
        aml[length++] = 0x08u;
        aml[length++] = 0x01u;
    }
    {
        static const uint8_t main_method[] = {0x14u, 0x18u, 'M', 'A', 'I', 'N',   0x00u, 0x70u, '\\',
                                              0x2Fu, 0x03u, '_', 'S', 'B', '_',   'D',   '2',   '9',
                                              '9',   'M',   'T', 'H', '_', 0x00u, 0x60u};

        memcpy(aml + length, main_method, sizeof(main_method));
        length += (uint32_t) sizeof(main_method);
    }
    if (!made_up_open(test))
        return;

    static uint8_t table[sizeof(aml) + 36u];
    uint8_t sum = 0u;

    memset(table, 0, sizeof(table));
    memcpy(table, "DSDT", 4u);
    table[4] = (uint8_t) (36u + length);
    table[5] = (uint8_t) ((36u + length) >> 8u);
    table[8] = 2u;
    memcpy(table + 36, aml, length);
    for (uint32_t byte = 0u; byte < 36u + length; ++byte)
        sum = (uint8_t) (sum + table[byte]);
    table[9] = (uint8_t) (0u - sum);
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) table);
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test, made_up_load.load_errors == 0u && made_up_load.decode_errors == 0u,
                      "every External is placed, without a fault");
    kernel_test_check(test,
                      made_up_load.method_calls == 1u &&
                          made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ZERO) == 1u,
                      "the call to the 300th keeps its Zero as an argument");
    made_up_close(test);
}

/**
 * @brief An Alias declared twice keeps its first target, and an Alias of the same name in a method
 *        body leaves the permanent one alone.
 */
KERNEL_TEST(an_alias_keeps_its_first_target)
{
    static const uint8_t first[] = {0x08u, 'T',   'G', 'T', '1', 0x01u, 0x08u, 'T', 'G', 'T', '2',
                                    0x01u, 0x06u, 'T', 'G', 'T', '1',   'A',   'L', 'I', '_'};
    static const uint8_t second[] = {0x06u, 'T', 'G',   'T',   '2', 'A', 'L', 'I', '_',  0x14u, 0x10u, 'M', 'A',
                                     'I',   'N', 0x00u, 0x06u, 'T', 'G', 'T', '2', '\\', 'A',   'L',   'I', '_'};

    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", first, sizeof(first)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", second, sizeof(second)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);

    uint32_t alias = 0u;
    uint32_t target = 0u;
    uint32_t first_target = 0u;

    kernel_test_check(
        test,
        made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, "TGT1", &first_target) ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
        "TGT1 is declared");
    alias = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &made_up_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, made_up_segment("ALI_"));
    target = advanced_configuration_and_power_interface_machine_language_namespace_node(&made_up_load.space, alias)
                 ->value.handle;
    kernel_test_check(test, target == first_target && made_up_load.duplicates == 1u,
                      "ALI_ still names TGT1, and the second Alias is counted");
    made_up_close(test);
}

/**
 * @brief Load yields a value, as ACPICA parses it: `If (Load (RG00, Local0))` decodes.
 */
KERNEL_TEST(load_may_stand_where_a_value_is_expected)
{
    static const uint8_t aml[] = {0x14u, 0x10u, 'L', 'D', 'T', 'B', 0x00u, 0xA0u, 0x08u,
                                  0x5Bu, 0x20u, 'R', 'G', '0', '0', 0x60u, 0xA3u};

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u, "the If around Load decodes");
    made_up_close(test);
}

/**
 * @brief The same External on a missing path, in two tables, makes one node, not two.
 */
KERNEL_TEST(the_same_external_twice_makes_one_node)
{
    static const uint8_t external[] = {0x15u, '\\', 0x2Eu, 'N', 'O', 'P', 'E', 'M', 'T', 'H', '_', 0x08u, 0x02u};

    if (!made_up_open(test))
        return;

    const uint32_t nodes = made_up_load.space.live_nodes;

    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", external, sizeof(external)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", external, sizeof(external)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test, made_up_load.space.live_nodes == nodes + 2u,
                      "one External for \\NOPE.MTH_ and one for the missing NOPE");
    kernel_test_check(test, made_up_load.load_errors == 0u, "the second External finds the first and counts no fault");
    made_up_close(test);
}

/**
 * @brief Two Externals through an Alias a later table declares make one node, under the Alias, which
 *        the namespace finds by its parent and name.
 */
KERNEL_TEST(two_externals_through_a_later_alias_make_one_node)
{
    static const uint8_t externals[] = {
        0x15u, '\\', 0x2Eu, 'A', 'L', 'S', '_', 'F', 'O', 'O', '_', 0x08u, 0x01u,
        0x15u, '\\', 0x2Eu, 'A', 'L', 'S', '_', 'F', 'O', 'O', '_', 0x08u, 0x01u,
    };
    static const uint8_t alias[] = {0x06u, '\\', '_', 'S', 'B', '_', '\\', 'A', 'L', 'S', '_'};

    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", externals, sizeof(externals)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", alias, sizeof(alias)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test, made_up_load.load_errors == 0u, "both Externals are placed without a fault");
    made_up_close(test);
}

/**
 * @brief The Externals pass places nothing from a Scope or a Device it cannot find: the External in
 *        their body names no path any table declares, and a name that climbs to the root still
 *        resolves to nothing. An External on a missing path makes the pass run.
 */
KERNEL_TEST(an_external_in_a_scope_that_cannot_be_found_is_not_placed)
{
    static const uint8_t aml[] = {
        0x10u, 0x0Du, '\\',  'N',   'O',   'P',   'E', 0x15u, 'F', 'O', 'O',   '_',   0x08u, 0x02u, 0x5Bu,
        0x82u, 0x12u, '\\',  0x2Eu, 'N',   'O',   'P', 'E',   'D', 'E', 'V',   '0',   0x15u, 'B',   'A',
        'R',   '_',   0x08u, 0x01u, 0x14u, 0x0Au, 'M', 'A',   'I', 'N', 0x00u, 'F',   'O',   'O',   '_',
        0x15u, '\\',  0x2Eu, 'M',   'I',   'S',   'S', 'I',   'N', 'G', '_',   0x08u, 0x00u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    uint32_t index = 0u;

    kernel_test_check(
        test,
        made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, "FOO_", &index) !=
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, "BAR_", &index) !=
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
        "neither FOO_ nor BAR_ lands at the root");
    kernel_test_check(test, made_up_load.unresolved_names == 1u && made_up_load.decode_errors == 0u,
                      "FOO_ in MAIN resolves to nothing, and decodes");
    made_up_close(test);
}

/**
 * @brief A second Alias of the same name in a method body leaves the first's target: the call
 *        through it takes the one argument of MTH1, not the two of MTH2, and the Noop after it stays
 *        a statement.
 */
KERNEL_TEST(a_second_alias_in_a_body_keeps_the_first_target)
{
    static const uint8_t aml[] = {
        0x14u, 0x07u, 'M', 'T', 'H', '1', 0x01u, 0xA3u, 0x14u, 0x07u, 'M', 'T', 'H', '2',   0x02u, 0xA3u,
        0x14u, 0x1Eu, 'M', 'A', 'I', 'N', 0x00u, 0x06u, 'M',   'T',   'H', '1', 'A', 'L',   'I',   '_',
        0x06u, 'M',   'T', 'H', '2', 'A', 'L',   'I',   '_',   'A',   'L', 'I', '_', 0x01u, 0xA3u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.decode_errors == 0u, "ALI_ (One) takes one argument, and Noop follows it");
    made_up_close(test);
}

/**
 * @brief An External in a method body never rewrites the argument count of the External the table
 *        declares.
 */
KERNEL_TEST(an_external_in_a_body_keeps_the_table_s_argument_count)
{
    static const uint8_t aml[] = {
        0x15u, '\\', 'E',   'X',   'T',  '_', 0x08u, 0x01u, 0x14u, 0x0Eu, 'M',   'A',
        'I',   'N',  0x00u, 0x15u, '\\', 'E', 'X',   'T',   '_',   0x08u, 0x03u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    uint32_t index = 0u;

    kernel_test_check(
        test,
        made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, "EXT_", &index) ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            advanced_configuration_and_power_interface_machine_language_namespace_node(&made_up_load.space, index)
                    ->value.payload == 1u,
        "EXT_ still takes one argument");
    made_up_close(test);
}

/**
 * @brief An External the namespace has no node for is counted once, by the Externals pass, not by
 *        the declaration pass as well.
 */
KERNEL_TEST(an_external_past_the_node_cap_is_counted_once)
{
    static const uint8_t aml[] = {0x15u, '\\', 'X', 'X', 'X', '_', 0x08u, 0x01u};
    uint32_t nodes = 0u;

    if (!made_up_open(test))
        return;
    nodes = made_up_load.space.node_count - 1u;
    advanced_configuration_and_power_interface_machine_language_close(&made_up_load);
    advanced_configuration_and_power_interface_simulated_host_open(&made_up_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &made_up_load, &made_up_host.host, nodes, MADE_UP_MAX_ARENA),
                           "the namespace opens full"))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", aml, sizeof(aml)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test,
                      made_up_load.load_errors == 1u &&
                          made_up_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_NAMESPACE_NODES,
                      "the External is counted once, as limit_namespace_nodes");
    made_up_close(test);
}

/**
 * @brief An External through an Alias a later table declares, to an object that exists, places
 *        nothing: the Alias stays without a child.
 */
KERNEL_TEST(an_external_through_a_later_alias_to_an_object_places_nothing)
{
    static const uint8_t external[] = {0x15u, '\\', 0x2Eu, 'A', 'L', 'S', '_', 'F', 'O', 'O', '_', 0x01u, 0x00u};
    static const uint8_t declarations[] = {
        0x06u, '\\',  '_', 'S', 'B', '_', '\\', 'A', 'L', 'S', '_',   0x08u,
        '\\',  0x2Eu, '_', 'S', 'B', '_', 'F',  'O', 'O', '_', 0x01u,
    };

    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", external, sizeof(external)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", declarations, sizeof(declarations)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *alias =
        advanced_configuration_and_power_interface_machine_language_namespace_node(
            &made_up_load.space,
            advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                &made_up_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
                made_up_segment("ALS_")));

    kernel_test_check(test, alias != NULL && alias->first_child == 0u && made_up_load.load_errors == 0u,
                      "ALS_ has no child, and nothing is counted");
    made_up_close(test);
}

/**
 * @brief The node a name written from the root resolves to, or 0.
 */
static uint32_t made_up_node_at(const char *text)
{
    uint32_t index = 0u;

    return (made_up_resolve(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT, text, &index) ==
            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK) ?
               index :
               0u;
}

/**
 * @brief The node a name written from the root resolves to; a test that cannot find it reads node 0's
 *        stand-in, all zero, and fails its checks.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *made_up_node(const char *text)
{
    static const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t none;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(&made_up_load.space,
                                                                                   made_up_node_at(text));

    return (node != NULL) ? node : &none;
}

/**
 * @brief The header of the object a value names, or a stand-in, all zero.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *
made_up_object(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value)
{
    static const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t none;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&made_up_load.heap, value->handle);

    return (object != NULL) ? object : &none;
}

/**
 * @brief The field unit a node holds, or a stand-in, all zero.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *made_up_unit(const char *text)
{
    static const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t none;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_node(text);

    if (node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT)
        return &none;
    return (const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *) (made_up_object(&node->value) +
                                                                                       1);
}

static const uint8_t made_up_data_objects[] = {
    0x08u, 'I', 'N',   'T',   '_',   0x0Bu, 0x34u, 0x12u, 0x08u, 'S',   'T',   'R',   '_',   0x0Du, 'a',
    'b',   'c', 0x00u, 0x08u, 'B',   'U',   'F',   '_',   0x11u, 0x05u, 0x0Au, 0x04u, 0x01u, 0x02u, 0x08u,
    'P',   'K', 'G',   '_',   0x12u, 0x0Du, 0x03u, 0x01u, 0x0Du, 'x',   0x00u, 0x12u, 0x06u, 0x01u, 'A',
    'B',   'C', 'D',   0x08u, 'V',   'P',   'K',   '_',   0x13u, 0x04u, 0x0Au, 0x02u, 0x00u,
};

/**
 * @brief A Name holds the data object it declares: an Integer, a String, a Buffer padded to its size,
 *        a Package whose third element is a Package holding a name kept as a name, and a VarPackage
 *        of two elements with one initialiser, the other left Uninitialized.
 */
KERNEL_TEST(a_name_holds_the_data_object_it_declares)
{
    if (!made_up_load_one(test, made_up_data_objects, sizeof(made_up_data_objects)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *integer = made_up_node("INT_");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *string =
        made_up_object(&made_up_node("STR_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *buffer =
        made_up_object(&made_up_node("BUF_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package =
        made_up_object(&made_up_node("PKG_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *elements =
        (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *variable =
        made_up_object(&made_up_node("VPK_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *variable_elements =
        (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (variable + 1);
    static const uint8_t buffer_bytes[] = {0x01u, 0x02u, 0x00u, 0x00u};

    kernel_test_check(test,
                      integer->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_INTEGER &&
                          integer->value.payload == 0x1234u,
                      "INT_ is the Integer 0x1234");
    kernel_test_check(test, string->length == 3u && memcmp(string + 1, "abc", 4u) == 0,
                      "STR_ is the String \"abc\", NUL kept");
    kernel_test_check(test, buffer->length == 4u && memcmp(buffer + 1, buffer_bytes, 4u) == 0,
                      "BUF_ is 4 bytes, its two initialisers then zeros");
    kernel_test_check(test,
                      package->length == 3u && elements[0].payload == 1u &&
                          elements[1].kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING &&
                          elements[2].kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE,
                      "PKG_ holds One, a String and a Package");
    kernel_test_check(
        test,
        made_up_object(&elements[2])->length == 1u &&
            ((const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (made_up_object(&elements[2]) +
                                                                                     1))[0]
                    .kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_NAME_REFERENCE,
        "the inner package keeps ABCD as a name, not resolved");
    kernel_test_check(test,
                      variable->length == 2u &&
                          variable_elements[0].kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          variable_elements[1].kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_UNINITIALIZED &&
                          made_up_load.heap.packages_padded == 1u,
                      "VPK_ has two elements, the second Uninitialized, and is counted as padded");
    kernel_test_measure_hexadecimal(
        test, "namespace_fold",
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&made_up_load));
    made_up_close(test);
}

/**
 * @brief The namespace fold follows the values: the same table with INT_ one higher folds otherwise,
 *        and the same table folds the same twice.
 */
KERNEL_TEST(the_namespace_fold_follows_the_values)
{
    uint8_t changed[sizeof(made_up_data_objects)];
    uint32_t folds[3] = {0u};

    memcpy(changed, made_up_data_objects, sizeof(changed));
    changed[6] = 0x35u;
    for (uint32_t run = 0u; run < 3u; ++run)
    {
        if (!made_up_load_one(test, (run == 2u) ? changed : made_up_data_objects, sizeof(made_up_data_objects)))
            return;
        folds[run] = advanced_configuration_and_power_interface_machine_language_namespace_fold(&made_up_load);
        made_up_close(test);
    }
    kernel_test_check(test, folds[0] == folds[1], "the same table folds the same twice");
    kernel_test_check(test, folds[0] != folds[2], "one integer changed changes the fold");
}

/**
 * @brief A Buffer takes the longer of its size and its initialiser, a Package with more initialisers
 *        than elements keeps the first, the cut is counted, and the dropped initialiser writes nothing;
 *        a Name declared twice keeps its first value.
 */
KERNEL_TEST(a_package_or_buffer_whose_size_and_initialisers_differ)
{
    static const uint8_t aml[] = {
        0x08u, 'T', 'R',   'N',   '_',   0x12u, 0x05u, 0x01u, 0x01u, 0x0Au, 0x02u, 0x08u, 'B',
        'G',   '_', '_',   0x11u, 0x06u, 0x0Au, 0x02u, 0x01u, 0x02u, 0x03u, 0x08u, 'O',   'V',
        'F',   '_', 0x12u, 0x07u, 0x01u, 0x12u, 0x03u, 0x01u, 0x01u, 0x00u, 0x08u, 'D',   'U',
        'P',   '_', 0x01u, 0x08u, 'D',   'U',   'P',   '_',   0x0Au, 0x02u,
    };
    static const uint8_t longer[] = {0x01u, 0x02u, 0x03u};

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package =
        made_up_object(&made_up_node("TRN_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *buffer =
        made_up_object(&made_up_node("BG__")->value);

    kernel_test_check(
        test,
        package->length == 1u &&
            ((const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1))[0].payload == 1u &&
            made_up_load.heap.packages_truncated == 2u,
        "Package (1) {One, 2} keeps One, and it and OVF_ are counted as cut");
    kernel_test_check(test, buffer->length == 3u && memcmp(buffer + 1, longer, 3u) == 0,
                      "Buffer (2) {1, 2, 3} holds the three bytes");

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *outer =
        made_up_object(&made_up_node("OVF_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *inner =
        made_up_object((const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (outer + 1));

    kernel_test_check(test, outer->length == 1u && inner->length == 1u,
                      "Package (1) {Package (1) {One}, Zero} drops Zero without touching the package after it");
    kernel_test_check(test, made_up_node("DUP_")->value.payload == 1u && made_up_load.duplicates == 1u,
                      "a second Name (DUP_) is counted, and the first keeps its value");
    made_up_close(test);
}

/**
 * @brief A region takes its space, offset and length; one built from an expression waits for the
 *        interpreter; each unit of a field takes the bit the list has reached, past a reserved field,
 *        and the access an AccessAs gives.
 */
KERNEL_TEST(a_region_and_its_field_units_take_their_layout)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'R', 'E', 'G',   '0',   0x01u, 0x0Au, 0x80u, 0x0Au, 0x10u, 0x5Bu, 0x80u,
        'R',   'E',   'G', '1', 0x00u, 0x72u, 0x01u, 0x01u, 0x00u, 0x0Au, 0x04u, 0x5Bu, 0x81u,
        0x1Au, 'R',   'E', 'G', '0',   0x01u, 'F',   '0',   '_',   '_',   0x08u, 0x00u, 0x08u,
        'F',   '1',   '_', '_', 0x10u, 0x01u, 0x03u, 0x00u, 'F',   '2',   '_',   '_',   0x20u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region =
        (const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *) (made_up_object(
                                                                                     &made_up_node("REG0")->value) +
                                                                                 1);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *computed =
        (const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *) (made_up_object(
                                                                                     &made_up_node("REG1")->value) +
                                                                                 1);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *first = made_up_unit("F0__");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *second = made_up_unit("F1__");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *third = made_up_unit("F2__");

    kernel_test_check(test,
                      region->space == 1u && region->state == 1u && region->offset == 0x80u && region->length == 0x10u,
                      "REG0 is 16 ports from 0x80");
    kernel_test_check(test, computed->state == 0u && made_up_load.values_deferred == 1u,
                      "REG1, whose offset is Add (One, One), waits for the interpreter");
    kernel_test_check(test,
                      first->region == made_up_node_at("REG0") && first->bit_offset == 0u && first->bit_length == 8u &&
                          first->access == 1u,
                      "F0__ is the first byte, byte access");
    kernel_test_check(test, second->bit_offset == 16u && second->bit_length == 16u && second->access == 1u,
                      "F1__ starts past the 8 reserved bits");
    kernel_test_check(test, third->bit_offset == 32u && third->bit_length == 32u && third->access == 3u,
                      "F2__ takes the DWordAcc an AccessAs gives");
    made_up_close(test);
}

/**
 * @brief The units of an IndexField name its index and data fields, those of a BankField its region,
 *        its bank field and its BankValue.
 */
KERNEL_TEST(index_and_bank_field_units_name_their_registers)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'R', 'E',   'G',   '0',   0x01u, 0x0Au, 0x80u, 0x0Au, 0x10u, 0x5Bu, 0x81u, 0x15u,
        'R',   'E',   'G', '0',   0x01u, 'I',   'D',   'X',   '_',   0x08u, 'D',   'A',   'T',   '_',
        0x08u, 'B',   'N', 'K',   '_',   0x08u, 0x5Bu, 0x86u, 0x0Fu, 'I',   'D',   'X',   '_',   'D',
        'A',   'T',   '_', 0x01u, 'R',   '0',   '_',   '_',   0x08u, 0x5Bu, 0x87u, 0x11u, 'R',   'E',
        'G',   '0',   'B', 'N',   'K',   '_',   0x0Au, 0x02u, 0x01u, 'B',   '0',   '_',   '_',   0x08u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *indexed = made_up_unit("R0__");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t *banked = made_up_unit("B0__");

    kernel_test_check(test,
                      indexed->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INDEX_FIELD &&
                          indexed->region == made_up_node_at("IDX_") &&
                          indexed->data_or_bank == made_up_node_at("DAT_"),
                      "R0__ goes through IDX_ and DAT_");
    kernel_test_check(test,
                      banked->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BANK_FIELD &&
                          banked->region == made_up_node_at("REG0") &&
                          banked->data_or_bank == made_up_node_at("BNK_") && banked->bank_value == 2u,
                      "B0__ is in REG0 when BNK_ holds 2");
    made_up_close(test);
}

/**
 * @brief A buffer field names its buffer and its bits: CreateDWordField at byte 2, CreateBitField at
 *        bit 3, CreateField of 12 bits at bit 4.
 */
KERNEL_TEST(a_buffer_field_names_its_buffer_and_bits)
{
    static const uint8_t aml[] = {
        0x08u, 'B',   'U',   'F', '_', 0x11u, 0x03u, 0x0Au, 0x08u, 0x8Au, 'B',   'U',   'F', '_', 0x0Au,
        0x02u, 'D',   'W',   '_', '_', 0x8Du, 'B',   'U',   'F',   '_',   0x0Au, 0x03u, 'B', 'I', 'T',
        '_',   0x5Bu, 0x13u, 'B', 'U', 'F',   '_',   0x0Au, 0x04u, 0x0Au, 0x0Cu, 'C',   'F', '_', '_',
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const uint32_t buffer = made_up_node_at("BUF_");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *word = &made_up_node("DW__")->value;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *bit = &made_up_node("BIT_")->value;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *any = &made_up_node("CF__")->value;

    kernel_test_check(test,
                      word->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD &&
                          word->handle == buffer && word->payload == (16u | (32ull << 32u)) && word->auxiliary == 4u,
                      "DW__ is 32 bits from bit 16 of BUF_, read 4 bytes at a time");
    kernel_test_check(test, bit->handle == buffer && bit->payload == (3u | (1ull << 32u)) && bit->auxiliary == 1u,
                      "BIT_ is bit 3");
    kernel_test_check(test, any->handle == buffer && any->payload == (4u | (12ull << 32u)) && any->auxiliary == 1u,
                      "CF__ is 12 bits from bit 4");
    made_up_close(test);
}

/**
 * @brief A Processor keeps its ID and processor block, a Mutex its SyncLevel, a PowerResource its
 *        system level and resource order; `\_OS_` and `\_REV` answer as Windows does.
 */
KERNEL_TEST(processors_mutexes_and_identity_keep_their_fields)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x83u, 0x0Bu, 'C', 'P',   'U',   '0',   0x01u, 0x10u, 0x08u, 0x00u, 0x00u, 0x06u, 0x5Bu, 0x01u,
        'M',   'U',   'T',   '_', 0x05u, 0x5Bu, 0x84u, 0x08u, 'P',   'W',   'R',   '_',   0x01u, 0x03u, 0x02u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *name =
        made_up_object(&made_up_node("_OS_")->value);

    kernel_test_check(test, made_up_node("CPU0")->value.payload == (0x810u | (6ull << 32u) | (1ull << 40u)),
                      "CPU0 is processor 1, its block 6 bytes at 0x810");
    kernel_test_check(test, made_up_node("MUT_")->value.auxiliary == 5u, "MUT_ has SyncLevel 5");
    kernel_test_check(test,
                      made_up_node("PWR_")->value.auxiliary == 1u && made_up_node("PWR_")->value.payload == 0x203u,
                      "PWR_ is level 1, order 0x203");
    kernel_test_check(test, name->length == 20u && memcmp(name + 1, "Microsoft Windows NT", 20u) == 0,
                      "\\_OS_ is \"Microsoft Windows NT\"");
    kernel_test_check(test, made_up_node("_REV")->value.payload == 2u, "\\_REV is 2");
    made_up_close(test);
}

/**
 * @brief An If and a Store at table level are code the interpreter would run, counted; a declaration,
 *        Noop and orphan data are not.
 */
KERNEL_TEST(table_level_code_is_counted)
{
    static const uint8_t aml[] = {
        0xA0u, 0x08u, 0x01u, 0x08u, 'N', '1', '_', '_', 0x01u, 0x70u, 0x01u, 'N', '1', '_', '_', 0xA3u, 0x00u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_measure(test, "table_level_code", made_up_load.table_level_code);
    kernel_test_check(test, made_up_load.table_level_code == 2u, "the If and the Store are counted, nothing else");
    made_up_close(test);
}
