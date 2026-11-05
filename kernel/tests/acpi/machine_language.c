#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_machine_language, KERNEL_TEST_STAGE_INITIALIZATION);

#define MADE_UP_TABLE_CAPACITY 4096u /**< Bytes of each buffer a made-up table is written in. */
#define MADE_UP_TABLE_COUNT    4u    /**< Made-up tables one test may load. */
#define MADE_UP_MULTI_SEGMENTS 255u  /**< NameSegs of the longest multi path. */

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
    return made_up_definition_block(made_up_tables[index], signature, 2u, aml, length);
}

/**
 * @brief Opens the made-up load on a fresh simulated host.
 */
static bool made_up_open(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_simulated_host_open(&made_up_host);
    return kernel_test_check(test,
                             advanced_configuration_and_power_interface_machine_language_open(
                                 &made_up_load, &made_up_host.host, MADE_UP_MAX_NODES, MADE_UP_MAX_ARENA_BYTES),
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
    kernel_test_check(test, made_up_load.heap.stale_handles == 0u, "no retain or release met a dead object");
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
 * @brief The opcodes that declare a name at a statement's place, as the ACPI specification lists them (§20.2.5): each
 *        named object, a Scope, an Alias and the three field lists.
 */
static const uint32_t made_up_declaring_opcodes[] = {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NAME,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_METHOD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_EXTERNAL,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_MUTEX,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_EVENT,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_OPERATION_REGION,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_DATA_REGION,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_DEVICE,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_PROCESSOR,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_POWER_RESOURCE,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_THERMAL_ZONE,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CREATE_BIT_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CREATE_BYTE_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CREATE_WORD_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CREATE_DOUBLE_WORD_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CREATE_QUAD_WORD_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_CREATE_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_SCOPE,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ALIAS,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_INDEX_FIELD,
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_BANK_FIELD,
};

/**
 * @brief Whether a statement of @p opcode is code the interpreter runs at table level, as §3.6 defines it and the
 *        load decided it opcode by opcode before its table: neither a declaration, data, a local, an argument nor
 *        Noop. An opcode past the last one is the invalid one, which is code.
 */
static bool made_up_runs_at_table_level(uint32_t opcode)
{
    const uint8_t flags = advanced_configuration_and_power_interface_machine_language_descriptor(opcode)->flags;

    if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FLAG_DATA_OBJECT) != 0u ||
        opcode == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP ||
        (opcode >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_LOCAL0 &&
         opcode <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_ARGUMENT6))
        return false;
    for (uint32_t index = 0u; index < sizeof(made_up_declaring_opcodes) / sizeof(made_up_declaring_opcodes[0]); ++index)
        if (opcode == made_up_declaring_opcodes[index])
            return false;
    return true;
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
 * @brief Whether ACPI's grammar lets @p byte lead a NameSeg: LeadNameChar, `A` to `Z` or `_` (ACPI 6.6 §20.2.2).
 */
static bool made_up_leads_segment(uint32_t byte) { return (byte >= 'A' && byte <= 'Z') || byte == '_'; }

/**
 * @brief Whether ACPI's grammar lets @p byte follow the first character of a NameSeg: NameChar, a LeadNameChar or a
 *        DigitChar, `0` to `9` (ACPI 6.6 §20.2.2).
 */
static bool made_up_continues_segment(uint32_t byte)
{
    return made_up_leads_segment(byte) || (byte >= '0' && byte <= '9');
}

/**
 * @brief Each of the 256 bytes may start a NameSeg exactly when ACPI's grammar makes it a LeadNameChar, and follow its
 *        first character exactly when it makes it a NameChar; a NameString of one NameSeg whose first or last
 *        character is that byte reads exactly then.
 */
KERNEL_TEST(each_byte_is_a_name_segment_character_as_the_grammar_says)
{
    uint32_t starts = 0u;
    uint32_t continues = 0u;
    uint32_t read = 0u;

    for (uint32_t byte = 0u; byte <= UINT8_MAX; ++byte)
    {
        uint8_t first[] = {(uint8_t) byte, 'B', 'C', 'D'};
        uint8_t last[] = {'A', 'B', 'C', (uint8_t) byte};
        AdvancedConfigurationAndPowerInterfaceMachineLanguageNameString_t name;
        uint32_t after = 0u;
        const bool reads_first = advanced_configuration_and_power_interface_machine_language_read_name_string(
                                     first, 0u, sizeof(first), &name, &after) ==
                                 ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
        const bool reads_last = advanced_configuration_and_power_interface_machine_language_read_name_string(
                                    last, 0u, sizeof(last), &name, &after) ==
                                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
        const bool prefix = byte == '\\' || byte == '^' || byte == 0x2Eu || byte == 0x2Fu || byte == 0x00u;

        starts += (advanced_configuration_and_power_interface_machine_language_starts_segment((uint8_t) byte) ==
                   made_up_leads_segment(byte)) ?
                      1u :
                      0u;
        continues += (advanced_configuration_and_power_interface_machine_language_continues_segment((uint8_t) byte) ==
                      made_up_continues_segment(byte)) ?
                         1u :
                         0u;
        read +=
            ((prefix || reads_first == made_up_leads_segment(byte)) && reads_last == made_up_continues_segment(byte)) ?
                1u :
                0u;
    }
    kernel_test_check(test, starts == 256u, "a byte starts a NameSeg exactly when it is a LeadNameChar");
    kernel_test_check(test, continues == 256u, "a byte goes on with a NameSeg exactly when it is a NameChar");
    kernel_test_check(test, read == 256u, "a NameSeg reads exactly when its first and last characters are those");
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
 * @brief A node taken again from the free list reads as a new node whatever the removed node left in it: its first
 *        child, its next sibling, its flags and its value zero, its name, parent, type and owner the ones given, its
 *        previous sibling the last child its parent had.
 */
KERNEL_TEST(a_node_taken_again_reads_as_a_new_one_whatever_the_removed_one_left)
{
    static const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t zero = {0};
    uint32_t removed = 0u;
    uint32_t taken = 0u;

    if (!made_up_open(test))
        return;

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_load.space;
    const uint32_t root = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;

    (void) advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        space, root, made_up_segment("OLD_"), ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE,
        1u, &removed);
    advanced_configuration_and_power_interface_machine_language_namespace_remove(space, removed);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *left =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, removed);
    const uint32_t free_link = left->next_sibling;

    memset(left, 0xFF, sizeof(*left));
    left->next_sibling = free_link;

    const uint32_t last =
        advanced_configuration_and_power_interface_machine_language_namespace_node(
            space, advanced_configuration_and_power_interface_machine_language_namespace_node(space, root)->first_child)
            ->previous_sibling;

    (void) advanced_configuration_and_power_interface_machine_language_namespace_add_child(
        space, root, made_up_segment("NEW_"), ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD,
        2u, &taken);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node =
        advanced_configuration_and_power_interface_machine_language_namespace_node(space, taken);

    kernel_test_check(test, taken == removed, "the node removed last is taken again");
    kernel_test_check(test,
                      node->name == made_up_segment("NEW_") && node->parent == root &&
                          node->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_METHOD &&
                          node->owner == 2u && node->previous_sibling == last,
                      "it takes the name, parent, type and owner given, after its parent's last child");
    kernel_test_check(test,
                      node->first_child == 0u && node->next_sibling == 0u && node->flags == 0u &&
                          memcmp(&node->value, &zero, sizeof(zero)) == 0,
                      "it holds no child, no next sibling, no flag and no value of the node removed");
    made_up_close(test);
}

/**
 * @brief Two namespaces given the same declarations, one by find_child then add_child, the other by
 *        find_or_add_child, and what the declarations did.
 */
typedef struct MadeUpTwoNamespaces {
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t
        found_then_added; /**< Declared by find_child, then add_child when it finds nothing. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t
        found_or_added;    /**< Declared by find_or_add_child. */
    uint32_t declarations; /**< Declarations made in both. */
    uint32_t differences;  /**< Declarations whose status, node or report differ between the two. */
    uint32_t found;        /**< Declarations that found their child. */
    uint32_t taken_again;  /**< Children created on an index the free list gave. */
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t
        last_status; /**< What the last declaration returned. */
} MadeUpTwoNamespaces_t;

static MadeUpTwoNamespaces_t made_up_two;

/**
 * @brief Whether the two namespaces of made_up_two hold as many nodes, live and free, runs, slots and arena bytes, and
 *        counted the same lookups, probes and generations.
 */
static bool made_up_counts_alike(void)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *expected = &made_up_two.found_then_added;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_two.found_or_added;

    return space->node_count == expected->node_count && space->live_nodes == expected->live_nodes &&
           space->free_nodes == expected->free_nodes && space->run_count == expected->run_count &&
           space->slot_capacity == expected->slot_capacity && space->arena_bytes == expected->arena_bytes &&
           space->lookups == expected->lookups && space->probes == expected->probes &&
           space->generation == expected->generation;
}

/**
 * @brief Whether the two namespaces of made_up_two hold the same nodes, links, slots, free list, runs and arena, and
 *        counted the same lookups, probes and generations.
 */
static bool made_up_namespaces_alike(void)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *expected = &made_up_two.found_then_added;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_two.found_or_added;

    if (!made_up_counts_alike() ||
        memcmp(space->slots, expected->slots, (size_t) expected->slot_capacity * sizeof(uint32_t)) != 0)
        return false;
    for (uint32_t index = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
         index < expected->node_count; ++index)
        if (memcmp(advanced_configuration_and_power_interface_machine_language_namespace_node(space, index),
                   advanced_configuration_and_power_interface_machine_language_namespace_node(expected, index),
                   sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t)) != 0)
            return false;
    return true;
}

/**
 * @brief Declares the Device @p name, a child of @p parent, in both namespaces of made_up_two, and counts what the
 *        declaration did; it differs when the status, the node or the report differ, or the counts of
 *        made_up_counts_alike once it is made.
 *
 * @return The child's index find_child then add_child gave, or 0 when the creation failed.
 */
static uint32_t made_up_declare_in_both(uint32_t parent, uint32_t name)
{
    const uint8_t type = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_DEVICE;
    const uint32_t unused = made_up_two.found_then_added.node_count;
    const uint32_t existing = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &made_up_two.found_then_added, parent, name);
    uint32_t expected = (existing != 0u) ? existing : 0xFFFFFFFFu;
    uint32_t index = 0xFFFFFFFFu;
    bool created = existing != 0u;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        (existing != 0u) ? ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK :
                           advanced_configuration_and_power_interface_machine_language_namespace_add_child(
                               &made_up_two.found_then_added, parent, name, type, 1u, &expected);

    made_up_two.last_status = advanced_configuration_and_power_interface_machine_language_namespace_find_or_add_child(
        &made_up_two.found_or_added, parent, name, type, 1u, &index, &created);
    ++made_up_two.declarations;
    made_up_two.differences += (made_up_two.last_status != status || index != expected || created != (existing == 0u) ||
                                !made_up_counts_alike()) ?
                                   1u :
                                   0u;
    made_up_two.found += (existing != 0u) ? 1u : 0u;
    made_up_two.taken_again +=
        (existing == 0u && status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
         expected < unused) ?
            1u :
            0u;
    return (status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK) ? expected : 0u;
}

/**
 * @brief Declares in both namespaces of made_up_two, and compares them after each of four phases: 40 children of a new
 *        Device, the first its parent's first child; the same 40 again and a predefined object, all found; every
 *        third of the 40 removed, then 20 more children, the first 14 on the indices the free list gives; then
 *        children of the root and of the Device in turn, until a declaration fails, one differs, or 4,000 are made:
 *        a namespace that has gone wrong is not filled further, where a full hash table would never end a probe.
 *
 * @return The phases that ended with both namespaces alike, of 4.
 */
static uint32_t made_up_declare_until_refused(void)
{
    const uint32_t root = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT;
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint32_t children[40] = {0u};
    char name[5] = "C000";
    uint32_t alike = 0u;
    const uint32_t device = made_up_declare_in_both(root, made_up_segment("PARN"));

    for (uint32_t index = 0u; index < 40u; ++index)
    {
        name[2] = (char) ('0' + index / 10u);
        name[3] = (char) ('0' + index % 10u);
        children[index] = made_up_declare_in_both(device, made_up_segment(name));
    }
    alike += made_up_namespaces_alike() ? 1u : 0u;
    for (uint32_t index = 0u; index < 40u; ++index)
    {
        name[2] = (char) ('0' + index / 10u);
        name[3] = (char) ('0' + index % 10u);
        (void) made_up_declare_in_both(device, made_up_segment(name));
    }
    (void) made_up_declare_in_both(root, made_up_segment("_SB_"));
    alike += made_up_namespaces_alike() ? 1u : 0u;
    for (uint32_t index = 0u; index < 40u; index += 3u)
    {
        advanced_configuration_and_power_interface_machine_language_namespace_remove(&made_up_two.found_then_added,
                                                                                     children[index]);
        advanced_configuration_and_power_interface_machine_language_namespace_remove(&made_up_two.found_or_added,
                                                                                     children[index]);
    }
    name[0] = 'R';
    for (uint32_t index = 0u; index < 20u; ++index)
    {
        name[2] = (char) ('0' + index / 10u);
        name[3] = (char) ('0' + index % 10u);
        (void) made_up_declare_in_both(device, made_up_segment(name));
    }
    alike += made_up_namespaces_alike() ? 1u : 0u;
    name[0] = 'N';
    for (uint32_t index = 0u;
         index < 4000u && made_up_two.differences == 0u &&
         made_up_two.last_status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
         ++index)
    {
        name[1] = digits[index / 1296u];
        name[2] = digits[(index / 36u) % 36u];
        name[3] = digits[index % 36u];
        (void) made_up_declare_in_both((index % 2u == 0u) ? root : device, made_up_segment(name));
    }
    alike += made_up_namespaces_alike() ? 1u : 0u;
    return alike;
}

/**
 * @brief Opens both namespaces of made_up_two with a node cap of @p max_nodes and an arena of @p arena_pages pages,
 *        declares in them as made_up_declare_until_refused does, then checks that every declaration and every phase
 *        agreed, and that the last declaration was refused with @p refusal once the hash table held @p slot_capacity
 *        slots and the namespace @p run_count runs, as @p what says; last, closes both.
 */
static void made_up_find_or_add_until_refused(KernelTest_t *test, uint32_t max_nodes, uint32_t arena_pages,
                                              AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t refusal,
                                              uint32_t slot_capacity, uint32_t run_count, const char *what)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_two.found_then_added;
    const uint32_t arena = arena_pages * ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES;
    uint32_t alike = 0u;

    memset(&made_up_two, 0, sizeof(made_up_two));
    advanced_configuration_and_power_interface_simulated_host_open(&made_up_host);

    const bool opened = advanced_configuration_and_power_interface_machine_language_namespace_open(
                            &made_up_two.found_then_added, &made_up_host.host, max_nodes, arena) ==
                            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                        advanced_configuration_and_power_interface_machine_language_namespace_open(
                            &made_up_two.found_or_added, &made_up_host.host, max_nodes, arena) ==
                            ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;

    if (opened)
        alike = made_up_declare_until_refused();
    kernel_test_check(test, opened, "both namespaces open");
    kernel_test_check(test, made_up_two.differences == 0u,
                      "every declaration finds or creates the node find_child then add_child give, with their status");
    kernel_test_check(test, alike == 4u,
                      "both namespaces end each phase alike: nodes, links, slots, free list, runs, arena and counters");
    kernel_test_check(test, made_up_two.found == 41u, "the same 40 names again and a predefined one are found");
    kernel_test_check(test, made_up_two.taken_again == 14u,
                      "the 14 children declared first after the removals take the indices the free list gives");
    kernel_test_check(test,
                      made_up_two.last_status == refusal && space->slot_capacity == slot_capacity &&
                          space->run_count == run_count,
                      what);
    advanced_configuration_and_power_interface_machine_language_namespace_close(&made_up_two.found_then_added);
    advanced_configuration_and_power_interface_machine_language_namespace_close(&made_up_two.found_or_added);
    kernel_test_check(test, made_up_host.outstanding_pages == 0u, "every page goes back to the host");
}

/**
 * @brief A child found or added by find_or_add_child is the one find_child then add_child give, whatever the
 *        declaration meets: a first child, a duplicate, the free list, the hash table growing, a new run of nodes,
 *        then the node cap; with an arena too small, the growth or the run refused.
 */
KERNEL_TEST(a_child_found_or_added_at_once_is_the_one_found_then_added)
{
    const uint32_t first_slots =
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_PAGE_BYTES / (uint32_t) sizeof(uint32_t);

    made_up_find_or_add_until_refused(
        test, 1700u, 256u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_NAMESPACE_NODES,
        4u * first_slots, 2u,
        "the hash table grows twice and a second run is taken before the node cap refuses a child");
    kernel_test_measure(test, "declarations", made_up_two.declarations);
    kernel_test_measure(test, "lookups", made_up_two.found_then_added.lookups);
    kernel_test_measure(test, "probes", made_up_two.found_then_added.probes);
    made_up_find_or_add_until_refused(
        test, 4000u, 18u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY, first_slots,
        1u, "an arena with no room to grow the hash table refuses the child that needs it");
    made_up_find_or_add_until_refused(
        test, 4000u, 24u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY,
        4u * first_slots, 1u, "an arena with no room for a second run refuses the child that needs it");
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
 * @brief Loads, as a DSDT, Name (VAL_, 0x2A) then BAD2, a method whose Store takes an If as an operand, with method
 *        bodies decoded or, when @p skips holds, skipped as a release kernel loads (O2, §3.6); then evaluates BAD2.
 *
 * @return BAD2's status, or OK when the load does not open.
 */
static AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t made_up_load_bad_body(KernelTest_t *test,
                                                                                           bool skips)
{
    static const uint8_t aml[] = {0x08u, 'V', 'A', 'L',   '_',   0x0Au, 0x2Au, 0x14u, 0x0Cu, 'B',
                                  'A',   'D', '2', 0x00u, 0x70u, 0xA0u, 0x03u, 0x01u, 0xA3u, 0x60u};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;

    if (!made_up_open(test))
        return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK;
    made_up_load.skips_bodies = skips;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", aml, sizeof(aml)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status =
        advanced_configuration_and_power_interface_machine_language_evaluate(&made_up_load, "\\BAD2", NULL, 0u,
                                                                             &result);

    advanced_configuration_and_power_interface_machine_language_value_release(&made_up_load.heap, &made_up_load.space,
                                                                              &result);
    return status;
}

/**
 * @brief A load that skips method bodies, as a release kernel's (O2, §3.6), declares what one that decodes them
 *        declares, and decodes no body: the fault in BAD2's body is found when BAD2 runs, not at load, and nothing
 *        the decoding pass counts is counted.
 */
KERNEL_TEST(a_load_that_skips_bodies_declares_the_same_names_and_decodes_none)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t decoded = made_up_load_bad_body(test, false);
    const uint32_t declarations = made_up_load.declarations;

    kernel_test_check(test,
                      made_up_load.decode_errors == 1u && made_up_load.tables[0].terms != 0u &&
                          made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STORE) == 1u,
                      "a load that decodes bodies decodes BAD2's Store, and finds the fault in its operand");
    kernel_test_check(test,
                      decoded == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE,
                      "BAD2 fails when it runs");
    made_up_close(test);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t skipped = made_up_load_bad_body(test, true);

    kernel_test_check(test,
                      made_up_load.decode_errors == 0u && made_up_load.tables[0].terms == 0u &&
                          made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STORE) == 0u,
                      "a load that skips bodies decodes none, and counts nothing the decoding pass counts");
    kernel_test_check(test,
                      made_up_load.declarations == declarations && declarations == 2u && made_up_load.load_errors == 0u,
                      "it declares VAL_ and BAD2 as the other does");
    kernel_test_check(test,
                      skipped == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INVALID_OPCODE,
                      "BAD2 fails when it runs, the fault found then");
    made_up_close(test);
}

/**
 * @brief A load that skips method bodies still places the Externals the declaration pass could not: the DSDT's
 *        External (\ZDEV.ZINT, IntObj) names a device the SSDT declares after it.
 */
KERNEL_TEST(a_load_that_skips_bodies_still_places_its_externals)
{
    static const uint8_t external[] = {0x15u, '\\', 0x2Eu, 'Z', 'D', 'E', 'V', 'Z', 'I', 'N', 'T', 0x01u, 0x00u};
    static const uint8_t device[] = {0x5Bu, 0x82u, 0x05u, 'Z', 'D', 'E', 'V'};

    if (!made_up_open(test))
        return;
    made_up_load.skips_bodies = true;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", external, sizeof(external)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", device, sizeof(device)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);

    AdvancedConfigurationAndPowerInterfaceMachineLanguageNamespace_t *space = &made_up_load.space;
    const uint32_t zdev = advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) "ZDEV", 0u));

    kernel_test_check(test,
                      zdev != 0u && advanced_configuration_and_power_interface_machine_language_namespace_find_child(
                                        space, zdev,
                                        advanced_configuration_and_power_interface_machine_language_name_segment(
                                            (const uint8_t *) "ZINT", 0u)) != 0u,
                      "ZINT is placed under ZDEV once both tables are declared");
    kernel_test_check(test, made_up_load.load_errors == 0u, "without a fault");
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

    static uint8_t table[sizeof(aml) + MADE_UP_TABLE_HEADER];

    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_definition_block(table, "DSDT", 2u, aml, length));
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
                               &made_up_load, &made_up_host.host, nodes, MADE_UP_MAX_ARENA_BYTES),
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
 * @brief The node a name written from the root resolves to, its data object built first when its Name kept it in
 *        its table (O20), as a name resolution reaching it builds it; node 0's stand-in when it cannot be found.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *made_up_data(const char *text)
{
    const uint32_t index = made_up_node_at(text);

    if (index != 0u)
        (void) advanced_configuration_and_power_interface_machine_language_materialize(&made_up_load, index);
    return made_up_node(text);
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
        made_up_object(&made_up_data("BUF_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package =
        made_up_object(&made_up_data("PKG_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *elements =
        (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *variable =
        made_up_object(&made_up_data("VPK_")->value);
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
        made_up_object(&made_up_data("TRN_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *buffer =
        made_up_object(&made_up_data("BG__")->value);

    kernel_test_check(
        test,
        package->length == 1u &&
            ((const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1))[0].payload == 1u &&
            made_up_load.heap.packages_truncated == 2u,
        "Package (1) {One, 2} keeps One, and it and OVF_ are counted as cut");
    kernel_test_check(test, buffer->length == 3u && memcmp(buffer + 1, longer, 3u) == 0,
                      "Buffer (2) {1, 2, 3} holds the three bytes");

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *outer =
        made_up_object(&made_up_data("OVF_")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *inner =
        made_up_object((const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (outer + 1));

    kernel_test_check(test, outer->length == 1u && inner->length == 1u,
                      "Package (1) {Package (1) {One}, Zero} drops Zero without touching the package after it");
    kernel_test_check(test, made_up_node("DUP_")->value.payload == 1u && made_up_load.duplicates == 1u,
                      "a second Name (DUP_) is counted, and the first keeps its value");
    made_up_close(test);
}

/**
 * @brief A ThermalZone, a Device, a Method, a Processor and a PowerResource declared twice keep their first
 *        definition, and the second one's body is skipped whole, as acpiexec skips it: its names are not
 *        created, its table-level code does not run, and the decoding pass reads none of its opcodes; a
 *        Scope still opens the existing object.
 */
KERNEL_TEST(a_duplicate_declaration_is_skipped_with_its_body)
{
    static const uint8_t aml[] = {
        0x08u, 'M',   'A',   'I',   'N',   0x0Au, 0x00u, 0x5Bu, 0x85u, 0x05u, 'T',   'E',   'S',   'T',   0x5Bu, 0x85u,
        0x13u, 'T',   'E',   'S',   'T',   0x08u, 'I',   'N',   'N',   'R',   0x0Au, 0x01u, 0x70u, 0x0Au, 0x55u, 'M',
        'A',   'I',   'N',   0x5Bu, 0x82u, 0x0Cu, 'D',   'E',   'V',   '0',   0x08u, 'A',   'A',   'A',   'A',   0x0Au,
        0x01u, 0x5Bu, 0x82u, 0x0Du, 'D',   'E',   'V',   '0',   0x08u, 'B',   'B',   'B',   'B',   0x0Au, 0x02u, 0xA3u,
        0x14u, 0x08u, 'M',   'T',   'H',   '0',   0x00u, 0xA4u, 0x01u, 0x14u, 0x0Du, 'M',   'T',   'H',   '0',   0x00u,
        0x5Bu, 0x21u, 0x0Au, 0x01u, 0xA4u, 0x0Au, 0x02u, 0x5Bu, 0x83u, 0x12u, 'C',   'P',   'U',   '0',   0x01u, 0x10u,
        0x04u, 0x00u, 0x00u, 0x06u, 0x08u, 'P',   'R',   'A',   '_',   0x0Au, 0x01u, 0x5Bu, 0x83u, 0x12u, 'C',   'P',
        'U',   '0',   0x02u, 0x20u, 0x04u, 0x00u, 0x00u, 0x06u, 0x08u, 'P',   'R',   'B',   '_',   0x0Au, 0x02u, 0x5Bu,
        0x84u, 0x0Fu, 'P',   'W',   'R',   '0',   0x00u, 0x00u, 0x00u, 0x08u, 'P',   'W',   'A',   '_',   0x0Au, 0x01u,
        0x5Bu, 0x84u, 0x0Fu, 'P',   'W',   'R',   '0',   0x01u, 0x01u, 0x00u, 0x08u, 'P',   'W',   'B',   '_',   0x0Au,
        0x02u, 0x10u, 0x0Cu, 'D',   'E',   'V',   '0',   0x08u, 'C',   'C',   'C',   'C',   0x0Au, 0x03u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_measure(test, "duplicates", made_up_load.duplicates);
    kernel_test_check(test, made_up_load.duplicates == 5u, "the five second declarations are counted");
    kernel_test_check(test,
                      made_up_node_at("\x2E"
                                      "TEST"
                                      "INNR") == 0u &&
                          made_up_node_at("\x2E"
                                          "DEV0"
                                          "BBBB") == 0u &&
                          made_up_node_at("\x2E"
                                          "CPU0"
                                          "PRB_") == 0u &&
                          made_up_node_at("\x2E"
                                          "PWR0"
                                          "PWB_") == 0u,
                      "no name a second body declares exists");
    kernel_test_check(test,
                      made_up_node("\x2E"
                                   "DEV0"
                                   "AAAA")
                                  ->value.payload == 1u &&
                          made_up_node("\x2E"
                                       "CPU0"
                                       "PRA_")
                                  ->value.payload == 1u &&
                          made_up_node("\x2E"
                                       "PWR0"
                                       "PWA_")
                                  ->value.payload == 1u,
                      "the names the first bodies declare exist");
    kernel_test_check(test,
                      made_up_node("\x2E"
                                   "DEV0"
                                   "CCCC")
                              ->value.payload == 3u,
                      "a Scope opens the existing DEV0 and declares CCCC in it");
    kernel_test_check(test, made_up_node("MAIN")->value.payload == 0u,
                      "MAIN stays 0: the second ThermalZone's Store never runs");
    kernel_test_check(test,
                      (made_up_node("MTH0")->value.payload >> 32u) == 2u &&
                          made_up_node("CPU0")->value.payload == (0x410u | (6ull << 32u) | (1ull << 40u)) &&
                          made_up_node("PWR0")->value.auxiliary == 0u && made_up_node("PWR0")->value.payload == 0u,
                      "MTH0, CPU0 and PWR0 keep their first definitions");
    kernel_test_check(
        test,
        made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STORE) == 0u &&
            made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP) == 0u &&
            made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_STALL) == 0u &&
            made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_RETURN) == 1u,
        "the decoding pass reads the first Method's Return and nothing of the second bodies");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NAME) == 5u,
                      "it reads the Names of MAIN, of the three first bodies and of the Scope");
    made_up_close(test);
}

/**
 * @brief A Device an SSDT declares again at the very offset the DSDT declared it is still a duplicate: the
 *        table that declared it tells the two apart, and the SSDT's body is neither declared nor decoded.
 */
KERNEL_TEST(a_duplicate_at_the_same_offset_of_another_table_is_skipped)
{
    static const uint8_t first[] = {0x5Bu, 0x82u, 0x0Cu, 'D', 'E', 'V', '0', 0x08u, 'A', 'A', 'A', 'A', 0x0Au, 0x01u};
    static const uint8_t second[] = {0x5Bu, 0x82u, 0x0Du, 'D', 'E', 'V',   '0',  0xA3u,
                                     0x08u, 'B',   'B',   'B', 'B', 0x0Au, 0x02u};

    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", first, sizeof(first)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", second, sizeof(second)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test,
                      made_up_load.duplicates == 1u && made_up_node_at("\x2E"
                                                                       "DEV0"
                                                                       "BBBB") == 0u,
                      "the SSDT's Device is a duplicate, and BBBB is not created");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP) == 0u &&
                          made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NAME) == 1u,
                      "the decoding pass reads the DSDT's body alone");
    made_up_close(test);
}

/**
 * @brief A Device of the name of a Name is a duplicate too: the Name stays, and the Device's body is
 *        neither declared nor decoded.
 */
KERNEL_TEST(a_device_of_the_name_of_a_name_is_a_duplicate)
{
    static const uint8_t aml[] = {0x08u, 'N',   'D', 'E', 'V', 0x0Au, 0x01u, 0x5Bu,
                                  0x82u, 0x06u, 'N', 'D', 'E', 'V',   0xA3u};

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.duplicates == 1u && made_up_node("NDEV")->value.payload == 1u,
                      "the Device is counted as a duplicate, and NDEV stays the Name 1");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP) == 0u,
                      "the decoding pass reads nothing of its body");
    made_up_close(test);
}

/**
 * @brief A Device whose path cannot be created is not a duplicate, even inside an object that has one: the
 *        decoding pass still decodes its body, its faults counted apart (§3.6).
 */
KERNEL_TEST(a_device_whose_path_names_nothing_is_still_decoded)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x82u, 0x05u, 'D',   'E',   'V',   '0',  0x5Bu, 0x82u, 0x05u, 'D', 'E', 'V', '0', 0x10u, 0x13u, 'D',
        'E',   'V',   '0',   0x5Bu, 0x82u, 0x0Cu, '\\', 0x2Eu, 'N',   'O',   'P', 'E', 'X', 'X', 'X',   'X',   0xA3u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.duplicates == 1u && made_up_load.load_errors == 1u,
                      "the second DEV0 is a duplicate, and \\NOPE.XXXX a fault of the declaration pass");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP) == 1u,
                      "the decoding pass reads the body of the Device that names nothing");
    made_up_close(test);
}

/**
 * @brief A duplicate that names the scope it stands in is skipped by every pass: `Scope (DEV0) { Device
 *        (\DEV0) { Noop } }` and `Device (DEV1) { Device (^DEV1) { Noop } }`, whose names are found, are no
 *        constructs naming nothing.
 */
KERNEL_TEST(a_duplicate_that_names_its_own_scope_is_skipped)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x82u, 0x05u, 'D',   'E',   'V',   '0', 0x10u, 0x0Eu, 'D',   'E',   'V',   '0',
        0x5Bu, 0x82u, 0x07u, '\\',  'D',   'E',   'V', '0',   0xA3u, 0x5Bu, 0x82u, 0x0Eu, 'D',
        'E',   'V',   '1',   0x5Bu, 0x82u, 0x07u, '^', 'D',   'E',   'V',   '1',   0xA3u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.duplicates == 2u && made_up_load.decode_errors == 0u,
                      "Device (\\DEV0) and Device (^DEV1) are the two duplicates");
    kernel_test_check(test,
                      made_up_count(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_NOOP) == 0u,
                      "the decoding pass reads neither body");
    made_up_close(test);
}

/**
 * @brief The Externals pass skips a duplicate's body too: an External a second Device holds, whose parent
 *        does not exist, is never placed, as in acpiexec, while an External outside it is.
 */
KERNEL_TEST(the_externals_pass_skips_a_duplicate_s_body)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x82u, 0x05u, 'D', 'E', 'V', '2', 0x5Bu, 0x82u, 0x12u, 'D',   'E',   'V',   '2',
        0x15u, '\\',  0x2Eu, 'M', 'I', 'S', 'S', 'F',   'O',   'O',   '_',   0x08u, 0x00u, 0x15u,
        '\\',  0x2Eu, 'O',   'T', 'H', 'R', 'B', 'A',   'R',   '_',   0x08u, 0x00u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.duplicates == 1u && made_up_node_at("\\MISS") == 0u,
                      "the second DEV2 is a duplicate, and \\MISS is not created");
    kernel_test_check(test,
                      made_up_node("\\\x2E"
                                   "OTHR"
                                   "BAR_")
                              ->type == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_NODE_EXTERNAL,
                      "the External outside it, \\OTHR.BAR_, is placed");
    made_up_close(test);
}

/**
 * @brief An Alias of an object that does not exist is counted and skipped alone, and the table goes on, as
 *        acpiexec goes on loading: the Name after it is still declared. acpiexec also creates the alias and
 *        an Untyped node for its source; the kernel creates neither, since ACPI 6.6 §19.6.4 says the source
 *        must already exist.
 */
KERNEL_TEST(an_alias_of_a_missing_object_is_skipped_alone)
{
    static const uint8_t aml[] = {0x06u, 'Z',   'O', 'O', '_', 'B', 'A',   'R',
                                  '_',   0x08u, 'A', 'F', 'T', 'R', 0x0Au, 0x07u};

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test, made_up_load.load_errors == 1u && made_up_load.decode_errors == 0u,
                      "the missing object is the one fault, met while declaring");
    kernel_test_check(test, made_up_node_at("BAR_") == 0u, "BAR_ is not created");
    kernel_test_check(test, made_up_node("AFTR")->value.payload == 7u, "AFTR, after it, is declared");
    made_up_close(test);
}

/**
 * @brief A region takes its space, offset and length; one built from an expression takes what its
 *        declaration computes when it runs; each unit of a field takes the bit the list has reached,
 *        past a reserved field, and the access an AccessAs gives.
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
    kernel_test_check(test, computed->state == 1u && computed->offset == 2u && made_up_load.values_deferred == 0u,
                      "REG1, whose offset is Add (One, One), takes the offset 2 when its declaration runs");
    kernel_test_check(test,
                      first->region == made_up_node_at("REG0") && first->bit_offset == 0u && first->bit_length == 8u &&
                          first->access == 1u,
                      "F0__ is the first byte, byte access");
    kernel_test_check(test, second->bit_offset == 16u && second->bit_length == 16u && second->access == 1u,
                      "F1__ starts past the 8 reserved bits");
    kernel_test_check(test, third->bit_offset == 32u && third->bit_length == 32u && third->access == 3u,
                      "F2__ takes the DWordAcc an AccessAs gives");
    kernel_test_measure_hexadecimal(
        test, "namespace_fold",
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&made_up_load));
    made_up_close(test);
}

/**
 * @brief The region of @p name, the object its node holds.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *made_up_region(const char *name)
{
    return (const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *) (made_up_object(
                                                                                        &made_up_node(name)->value) +
                                                                                    1);
}

/**
 * @brief Two regions built from expressions each take what their own declaration computes, one run per
 *        expression: RGA_'s offset and length, then RGB_'s offset, its length a constant.
 */
KERNEL_TEST(regions_built_from_expressions_each_take_what_their_declaration_computes)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'R', 'G', 'A', '_', 0x01u, 0x72u, 0x01u, 0x01u, 0x00u, 0x72u, 0x01u, 0x0Au, 0x02u, 0x00u,
        0x5Bu, 0x80u, 'R', 'G', 'B', '_', 0x01u, 0x72u, 0x0Au, 0x02u, 0x0Au, 0x02u, 0x00u, 0x0Au, 0x08u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *first = made_up_region("RGA_");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *second = made_up_region("RGB_");

    kernel_test_check(test, first->state == 1u && first->offset == 2u && first->length == 3u,
                      "RGA_ takes the offset Add (One, One) and the length Add (One, 2)");
    kernel_test_check(test, second->state == 1u && second->offset == 4u && second->length == 8u,
                      "RGB_ takes the offset Add (2, 2) and the length 8");
    kernel_test_check(test, made_up_load.values_deferred == 0u && made_up_load.evaluation.runs == 3u,
                      "nothing waits, and the three expressions are three runs");
    made_up_close(test);
}

/**
 * @brief Two regions at the same offset of two tables each take what their own declaration computes: the
 *        SSDT's region takes nothing the DSDT's computed.
 */
KERNEL_TEST(regions_at_the_same_offset_of_two_tables_each_take_their_own_offset)
{
    static const uint8_t first[] = {0x5Bu, 0x80u, 'R', 'G', 'A', '_', 0x01u, 0x72u, 0x01u, 0x01u, 0x00u, 0x0Au, 0x04u};
    static const uint8_t second[] = {0x5Bu, 0x80u, 'R',   'G',   'C',   '_',   0x01u, 0x72u,
                                     0x0Au, 0x02u, 0x0Au, 0x03u, 0x00u, 0x0Au, 0x04u};

    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", first, sizeof(first)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", second, sizeof(second)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test, made_up_region("RGA_")->state == 1u && made_up_region("RGA_")->offset == 2u,
                      "RGA_ takes the offset Add (One, One)");
    kernel_test_check(test, made_up_region("RGC_")->state == 1u && made_up_region("RGC_")->offset == 5u,
                      "RGC_, at the same offset of the SSDT, takes the offset Add (2, 3)");
    made_up_close(test);
}

/**
 * @brief A region whose offset fails to run waits for the interpreter, the failure counted, and its length
 *        is not run: DerefOf (Index (PKG_, 5)) is past the one element of PKG_.
 */
KERNEL_TEST(a_region_whose_offset_fails_waits_for_the_interpreter)
{
    static const uint8_t aml[] = {
        0x08u, 'P',   'K',   'G', '_', 0x12u, 0x03u, 0x01u, 0x00u, 0x5Bu, 0x80u, 'R',   'G',   'F',   '_',
        0x01u, 0x83u, 0x88u, 'P', 'K', 'G',   '_',   0x0Au, 0x05u, 0x00u, 0x72u, 0x01u, 0x01u, 0x00u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t *region = made_up_region("RGF_");

    kernel_test_check(test, region->state == 0u && region->offset == 0u && made_up_load.values_deferred == 1u,
                      "RGF_ waits for the interpreter");
    kernel_test_check(test, made_up_load.evaluation.failures == 1u && made_up_load.evaluation.runs == 1u,
                      "the offset's run fails, counted, and the length is not run");
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
                      indexed->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_INDEX &&
                          indexed->region == made_up_node_at("IDX_") &&
                          indexed->data_or_bank == made_up_node_at("DAT_"),
                      "R0__ goes through IDX_ and DAT_");
    kernel_test_check(test,
                      banked->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_FIELD_UNIT_BANK &&
                          banked->region == made_up_node_at("REG0") &&
                          banked->data_or_bank == made_up_node_at("BNK_") && banked->bank_value == 2u,
                      "B0__ is in REG0 when BNK_ holds 2");
    kernel_test_measure_hexadecimal(
        test, "namespace_fold",
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&made_up_load));
    made_up_close(test);
}

/**
 * @brief A buffer field holds its Buffer, with a reference, and its bits: CreateDWordField at byte 2,
 *        CreateBitField at bit 3, CreateField of 12 bits at bit 4, which alone reads as a Buffer.
 */
KERNEL_TEST(a_buffer_field_holds_its_buffer_and_bits)
{
    static const uint8_t aml[] = {
        0x08u, 'B',   'U',   'F', '_', 0x11u, 0x03u, 0x0Au, 0x08u, 0x8Au, 'B',   'U',   'F', '_', 0x0Au,
        0x02u, 'D',   'W',   '_', '_', 0x8Du, 'B',   'U',   'F',   '_',   0x0Au, 0x03u, 'B', 'I', 'T',
        '_',   0x5Bu, 0x13u, 'B', 'U', 'F',   '_',   0x0Au, 0x04u, 0x0Au, 0x0Cu, 'C',   'F', '_', '_',
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const uint32_t buffer = made_up_node("BUF_")->value.handle;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *word = &made_up_node("DW__")->value;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *bit = &made_up_node("BIT_")->value;
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *any = &made_up_node("CF__")->value;

    kernel_test_check(test,
                      word->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD &&
                          word->handle == buffer && word->payload == (16u | (32ull << 32u)) && word->auxiliary == 4u,
                      "DW__ is 32 bits from bit 16 of BUF_'s Buffer, read 4 bytes at a time");
    kernel_test_check(
        test, bit->handle == buffer && bit->payload == (3u | (1ull << 32u)) && bit->auxiliary == 1u && bit->flags == 0u,
        "BIT_ is bit 3, and reads as an Integer");
    kernel_test_check(test,
                      any->handle == buffer && any->payload == (4u | (12ull << 32u)) && any->auxiliary == 1u &&
                          any->flags ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_BUFFER_FIELD_READS_AS_BUFFER,
                      "CF__ is 12 bits from bit 4, and reads as a Buffer");
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        advanced_configuration_and_power_interface_machine_language_object(&made_up_load.heap, buffer);

    kernel_test_check(test, object != NULL && object->references == 4u, "BUF_ and its three fields hold the Buffer");
    kernel_test_measure_hexadecimal(
        test, "namespace_fold",
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&made_up_load));
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
    kernel_test_measure_hexadecimal(
        test, "namespace_fold",
        advanced_configuration_and_power_interface_machine_language_namespace_fold(&made_up_load));
    made_up_close(test);
}

/**
 * @brief An If, a Store and a name at table level are code the interpreter would run, counted; a declaration,
 *        Noop and orphan data are not.
 */
KERNEL_TEST(table_level_code_is_counted)
{
    static const uint8_t aml[] = {
        0xA0u, 0x08u, 0x01u, 0x08u, 'N',   '1',   '_', '_', 0x01u, 0x70u, 0x01u,
        'N',   '1',   '_',   '_',   0xA3u, 0x00u, 'N', '1', '_',   '_',
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_measure(test, "table_level_code", made_up_load.table_level_code);
    kernel_test_check(test, made_up_load.table_level_code == 3u,
                      "the If, the Store and the name are counted, nothing else");
    made_up_close(test);
}

/**
 * @brief A Package keeps a name as a name even when the name exists already: it is resolved when the
 *        element is read, never when the package is built (§5.1).
 */
KERNEL_TEST(a_package_keeps_a_name_that_exists_as_a_name)
{
    static const uint8_t aml[] = {
        0x08u, 'T', 'G', 'T', '_', 0x0Au, 0x05u, 0x08u, 'P', 'K', 'N', '_', 0x12u, 0x06u, 0x01u, 'T', 'G', 'T', '_',
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package =
        made_up_object(&made_up_data("PKN_")->value);

    kernel_test_check(
        test,
        package->length == 1u &&
            ((const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1))[0].kind ==
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_NAME_REFERENCE,
        "PKN_ holds TGT_ as a name, though TGT_ is the Integer 5 already");
    made_up_close(test);
}

/**
 * @brief The objects take the sizes §3.4 gives them on both targets: a 16-byte header, a 32-byte
 *        region, a 24-byte field unit.
 */
KERNEL_TEST(objects_take_the_sizes_the_specification_gives)
{
    kernel_test_measure(test, "header_bytes",
                        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t));
    kernel_test_measure(test, "region_bytes",
                        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t));
    kernel_test_measure(test, "field_unit_bytes",
                        (uint32_t) sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t));
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t) == 16u,
                      "an object header is 16 bytes");
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageRegion_t) == 32u,
                      "a region is 32 bytes");
    kernel_test_check(test, sizeof(AdvancedConfigurationAndPowerInterfaceMachineLanguageFieldUnit_t) == 24u,
                      "a field unit is 24 bytes");
}

/**
 * @brief Writes at @p aml `Name (SIZE, size)`, then @p count Names of zeroed buffers of SIZE bytes, named
 *        @p letter then three digits: a size a name gives, so that the load builds each buffer, where it keeps
 *        in its table a buffer whose size is a literal (O20).
 *
 * @return The bytes written.
 */
static uint32_t made_up_buffers(uint8_t *aml, char letter, uint32_t count, uint16_t size)
{
    const uint8_t size_name[] = {0x08u, 'S', 'I', 'Z', 'E', 0x0Bu, (uint8_t) size, (uint8_t) (size >> 8u)};

    memcpy(aml, size_name, sizeof(size_name));
    for (uint32_t index = 0u; index < count; ++index)
    {
        const uint8_t name[] = {
            0x08u,
            (uint8_t) letter,
            (uint8_t) ('0' + index / 100u),
            (uint8_t) ('0' + index / 10u % 10u),
            (uint8_t) ('0' + index % 10u),
            0x11u,
            0x05u,
            'S',
            'I',
            'Z',
            'E',
        };

        memcpy(aml + sizeof(size_name) + index * sizeof(name), name, sizeof(name));
    }
    return (uint32_t) sizeof(size_name) + count * 11u;
}

/**
 * @brief An arena exhausted while a table builds its objects stops that table (§6.2), and spoils
 *        nothing: with room for the first run of objects only, the 16th buffer of 4,000 bytes in a
 *        Scope, each in a block of 4 KiB, does not fit, the Name after the Scope is never declared, and
 *        a String a second table builds afterwards lands past the buffers, never over them. A name gives
 *        the buffers their size, so that the load builds them rather than keep them in the table (O20).
 */
KERNEL_TEST(an_exhausted_arena_stops_the_table_and_spoils_nothing)
{
    static uint8_t aml[5u + 8u + 16u * 11u + 6u];
    static const uint8_t scope[] = {0x10u, 0x4Cu, 0x0Bu, '\\', 0x00u};
    static const uint8_t string[] = {0x08u, 'S', 'T', 'R', '1', 0x0Du, 'a', 'b', 'c', 0x00u};
    static const uint8_t last[] = {0x08u, 'L', 'A', 'S', 'T', 0x01u};
    const uint32_t length = (uint32_t) sizeof(scope) + made_up_buffers(aml + sizeof(scope), 'B', 16u, 0x0FA0u);
    uint32_t arena = 0u;
    bool untouched = true;

    memcpy(aml, scope, sizeof(scope));
    memcpy(aml + length, last, sizeof(last));
    if (!made_up_open(test))
        return;
    arena = made_up_load.space.arena_bytes;
    advanced_configuration_and_power_interface_machine_language_close(&made_up_load);
    advanced_configuration_and_power_interface_simulated_host_open(&made_up_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &made_up_load, &made_up_host.host, MADE_UP_MAX_NODES, arena),
                           "the namespace opens with no room past its first run of objects"))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(0u, "DSDT", aml, length + (uint32_t) sizeof(last)));
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_table(1u, "SSDT", string, sizeof(string)));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);

    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *first =
        made_up_object(&made_up_node("B000")->value);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *text =
        made_up_object(&made_up_node("STR1")->value);

    for (uint32_t byte = 0u; byte < first->length; ++byte)
        untouched = untouched && ((const uint8_t *) (first + 1))[byte] == 0u;
    kernel_test_check(test,
                      made_up_load.first_load_error ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OUT_OF_MEMORY,
                      "the 16th buffer runs out of memory");
    kernel_test_check(test,
                      made_up_object(&made_up_node("B014")->value)->length == 0x0FA0u &&
                          made_up_node("B015")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                      "15 buffers are built, the 16th is not");
    kernel_test_check(test, made_up_node_at("LAST") == 0u, "the table stops there: LAST is never declared");
    kernel_test_check(test, first->length == 0x0FA0u && untouched, "B000 still holds 4,000 zeros");
    kernel_test_check(test, text->length == 3u && memcmp(text + 1, "abc", 4u) == 0,
                      "the next table's String is built where it fits");
    made_up_close(test);
}

/**
 * @brief The object heap holds 256 runs: a Buffer of 4,096 bytes takes a run of its own, and the one
 *        that would take the 257th is refused as limit_object_runs, not as out_of_memory. A name gives
 *        the buffers their size, so that the load builds them rather than keep them in the table (O20).
 */
KERNEL_TEST(the_object_heap_refuses_a_run_past_its_last)
{
    static uint8_t aml[8u + 256u * 11u];
    const uint32_t length = made_up_buffers(aml, 'L', 256u, 0x1000u);

    advanced_configuration_and_power_interface_simulated_host_open(&made_up_host);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_open(
                               &made_up_load, &made_up_host.host, MADE_UP_MAX_NODES, 4u * MADE_UP_MAX_ARENA_BYTES),
                           "the namespace opens"))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(&made_up_load,
                                                                              made_up_table(0u, "DSDT", aml, length));
    advanced_configuration_and_power_interface_machine_language_decode_tables(&made_up_load);
    kernel_test_check(test,
                      made_up_load.load_errors == 1u &&
                          made_up_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_OBJECT_RUNS,
                      "one buffer is refused, as limit_object_runs");
    kernel_test_check(test,
                      made_up_object(&made_up_node("L254")->value)->length == 0x1000u &&
                          made_up_node("L255")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                      "the first run holds the small objects, 255 hold a buffer each, the 256th buffer has none");
    made_up_close(test);
}

/**
 * @brief An offset past 32 bits is refused as index_out_of_range and builds nothing: a DWord field at
 *        byte 2^29, a CreateField at bit 2^32, a field list whose reserved bits pass 2^32 before its
 *        unit.
 */
KERNEL_TEST(an_offset_past_32_bits_is_refused)
{
    static uint8_t aml[160];
    static const uint8_t head[] = {
        0x08u, 'B',   'U',   'F',   '_',   0x11u, 0x03u, 0x0Au, 0x08u, 0x8Au, 'B', 'U', 'F',   '_',   0x0Cu, 0x00u,
        0x00u, 0x00u, 0x20u, 'D',   'W',   'X',   '_',   0x5Bu, 0x13u, 'B',   'U', 'F', '_',   0x0Eu, 0x00u, 0x00u,
        0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x08u, 'C',   'F',   'X', '_', 0x5Bu, 0x80u, 'R',   'E',
        'G',   '0',   0x00u, 0x00u, 0x0Au, 0x10u, 0x5Bu, 0x81u, 0x41u, 0x06u, 'R', 'E', 'G',   '0',   0x01u,
    };
    static const uint8_t reserved[] = {0x00u, 0xCFu, 0xFFu, 0xFFu, 0xFFu};
    static const uint8_t unit[] = {'F', '9', '_', '_', 0x08u};
    uint32_t length = (uint32_t) sizeof(head);

    memcpy(aml, head, sizeof(head));
    for (uint32_t index = 0u; index < 17u; ++index, length += (uint32_t) sizeof(reserved))
        memcpy(aml + length, reserved, sizeof(reserved));
    memcpy(aml + length, unit, sizeof(unit));
    length += (uint32_t) sizeof(unit);
    if (!made_up_load_one(test, aml, length))
        return;
    kernel_test_check(test,
                      made_up_node("DWX_")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD &&
                          made_up_node("CFX_")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD,
                      "neither buffer field is built");
    kernel_test_check(test, made_up_node_at("F9__") == 0u, "the field list is refused before its unit");
    kernel_test_check(test,
                      made_up_load.load_errors == 3u &&
                          made_up_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE &&
                          made_up_load.decode_errors == 0u,
                      "three faults, the first index_out_of_range, and nothing the decoder refuses");
    made_up_close(test);
}

/**
 * @brief A field list builds no unit when a name it needs is missing, counted once as name_not_found;
 *        a BankField whose BankValue is an expression takes what its declaration computes when it
 *        runs.
 */
KERNEL_TEST(a_field_list_that_cannot_be_laid_out_builds_no_unit)
{
    static const uint8_t aml[] = {
        0x5Bu, 0x80u, 'R', 'E',   'G',   '0',   0x00u, 0x00u, 0x0Au, 0x10u, 0x5Bu, 0x81u, 0x0Bu, 'R',   'E',   'G',
        '0',   0x01u, 'B', 'N',   'K',   '_',   0x08u, 0x5Bu, 0x81u, 0x10u, 'N',   'O',   'P',   'E',   0x01u, 'F',
        '8',   '_',   '_', 0x08u, 'F',   '7',   '_',   '_',   0x08u, 0x5Bu, 0x87u, 0x13u, 'R',   'E',   'G',   '0',
        'B',   'N',   'K', '_',   0x72u, 0x01u, 0x01u, 0x00u, 0x01u, 'B',   '1',   '_',   '_',   0x08u,
    };

    if (!made_up_load_one(test, aml, sizeof(aml)))
        return;
    kernel_test_check(test,
                      made_up_node("F8__")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT &&
                          made_up_node("F7__")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT &&
                          made_up_load.load_errors == 1u &&
                          made_up_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND,
                      "the units of a field in the missing NOPE get no object, and NOPE is counted once");
    kernel_test_check(test,
                      made_up_node("B1__")->value.kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_FIELD_UNIT &&
                          made_up_unit("B1__")->bank_value == 2u && made_up_load.values_deferred == 0u,
                      "B1__, banked on Add (One, One), takes the BankValue 2 when its declaration runs");
    made_up_close(test);
}

/**
 * @brief A String of 65,536 characters, the cap of §6.1, is built with its NUL; one of 65,537 is
 *        refused as limit_buffer_bytes.
 */
KERNEL_TEST(a_string_of_the_largest_length_is_built)
{
    static uint8_t aml[2u * (6u + 65537u + 1u)];
    static uint8_t table[MADE_UP_TABLE_HEADER + sizeof(aml)];
    static const uint8_t names[2][5] = {
        {0x08u, 'S', 'B', 'I', 'G'},
        {0x08u, 'S', 'O', 'V', 'R'}
    };
    const uint32_t lengths[2] = {65536u, 65537u};
    uint32_t length = 0u;

    for (uint32_t name = 0u; name < 2u; ++name)
    {
        memcpy(aml + length, names[name], 5u);
        aml[length + 5u] = 0x0Du;
        memset(aml + length + 6u, 'x', lengths[name]);
        aml[length + 6u + lengths[name]] = 0x00u;
        length += 6u + lengths[name] + 1u;
    }
    if (!made_up_open(test))
        return;
    advanced_configuration_and_power_interface_machine_language_declare_table(
        &made_up_load, made_up_definition_block(table, "DSDT", 2u, aml, length));
    kernel_test_check(test, made_up_object(&made_up_node("SBIG")->value)->length == 65536u,
                      "a String of 65,536 characters is built");
    kernel_test_check(test,
                      made_up_node("SOVR")->value.kind !=
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING &&
                          made_up_load.load_errors == 1u &&
                          made_up_load.first_load_error ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_LIMIT_BUFFER_BYTES,
                      "one of 65,537 is refused as limit_buffer_bytes");
    made_up_close(test);
}

/**
 * @brief The load decides whether a statement at table level is code the interpreter runs by a table built from the
 *        opcode X-macros: for every opcode, the invalid one and one past the last included, it decides as the
 *        definition of §3.6 decides, computed here from the descriptors and the specification's list of declarations.
 */
KERNEL_TEST(the_load_decides_table_level_code_for_every_opcode_as_section_3_6_defines_it)
{
    uint32_t disagreements = 0u;
    uint32_t code = 0u;

    for (uint32_t opcode = 0u; opcode <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT;
         ++opcode)
    {
        const bool runs = advanced_configuration_and_power_interface_machine_language_runs_at_table_level(opcode);

        if (runs != made_up_runs_at_table_level(opcode))
        {
            kernel_test_check(test, false,
                              advanced_configuration_and_power_interface_machine_language_opcode_name(opcode));
            ++disagreements;
        }
        code += (runs && opcode < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT) ? 1u : 0u;
    }
    kernel_test_check(test, disagreements == 0u, "every opcode is decided as the definition decides it");
    kernel_test_measure(test, "opcodes", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_OPCODE_COUNT);
    kernel_test_measure(test, "table_level_code_opcodes", code);
}
