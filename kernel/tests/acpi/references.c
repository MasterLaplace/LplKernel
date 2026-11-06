#include "fixtures/references.h"
#include "fixtures/common.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_references, KERNEL_TEST_STAGE_INITIALIZATION);

#define REFERENCES_TABLE_CAPACITY 2048u                 /**< Bytes of the buffer a made-up DSDT is written in. */
#define REFERENCES_ONES           0xFFFFFFFFFFFFFFFFull /**< Ones, and True, under a revision-2 table. */
#define REFERENCES_FIELD_BYTES    16u                   /**< Bytes of the Buffer the bit-exact field test walks. */
#define REFERENCES_FOLD_OFFSET    0x811C9DC5u           /**< FNV-1a's offset basis. */
#define REFERENCES_FOLD_PRIME     0x01000193u           /**< FNV-1a's prime. */

static uint8_t references_table[REFERENCES_TABLE_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t references_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t references_load;

/**
 * @brief Loads @p aml as a DSDT of revision 2, running its table-level code.
 */
static bool references_run(KernelTest_t *test, const uint8_t *aml, uint32_t length)
{
    return made_up_load_table(test, &references_load, &references_host, references_table, 2u, aml, length);
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t references_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&references_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief Whether a value is a String or a Buffer, as @p kind says, of exactly @p length bytes equal to @p bytes.
 */
static bool references_value_holds(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *value,
                                   uint8_t kind, const void *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *object =
        (value != NULL && value->kind == kind) ?
            advanced_configuration_and_power_interface_machine_language_object(&references_load.heap, value->handle) :
            NULL;

    return object != NULL && object->length == length && memcmp(object + 1, bytes, length) == 0;
}

/**
 * @brief Whether the Name at the root called @p name holds a value of @p kind of @p length bytes equal to @p bytes.
 */
static bool references_holds(const char *name, uint8_t kind, const void *bytes, uint32_t length)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&references_load, name);

    return node != NULL && references_value_holds(&node->value, kind, bytes, length);
}

/**
 * @brief Element @p index of the Package the Name at the root called @p name holds, or NULL.
 */
static const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *references_element(const char *name,
                                                                                              uint32_t index)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&references_load, name);
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *package =
        (node != NULL &&
         node->value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_PACKAGE) ?
            advanced_configuration_and_power_interface_machine_language_object(&references_load.heap,
                                                                               node->value.handle) :
            NULL;

    if (package == NULL || index >= package->length)
        return NULL;
    return (const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t *) (package + 1) + index;
}

/**
 * @brief How many runs failed with @p status.
 */
static uint32_t references_failed(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status)
{
    return references_load.evaluation.statuses[status];
}

/**
 * @brief Checks that every run left the evaluation's stacks empty and no release found nothing to
 *        release, then closes the load.
 */
static void references_close(KernelTest_t *test)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageEvaluation_t *evaluation = &references_load.evaluation;

    kernel_test_check(test,
                      evaluation->frame_count == 0u && evaluation->value_count == 0u &&
                          evaluation->operator_count == 0u && evaluation->block_count == 0u &&
                          references_load.temporary_count == 0u,
                      "every run leaves no frame, value, operator, block or temporary node behind");
    kernel_test_check(test, references_load.heap.stale_handles == 0u, "no retain or release met a dead object");
    advanced_configuration_and_power_interface_machine_language_close(&references_load);
    kernel_test_check(test, references_host.outstanding_pages == 0u, "every page goes back to the host");
}

/**
 * @brief RefOf, CondRefOf and DerefOf (§5.2): DerefOf follows a reference to a name and a chain of
 *        references to its bottom, and reads the object a path String names; a store to a local that
 *        holds a reference writes through it, as does a store to an argument that holds one; CondRefOf
 *        is True for a name that exists, \_OSI included, and False for one that does not or that only
 *        an External declares; a reference to a local that a method returns gives its caller the
 *        value (§6.3); a reference to a table's local lives as long as the table's locals do; a store
 *        through a local keeps the type of what it refers to, unless the value is a reference or what
 *        it refers to holds no data, which replace the reference; one to an element that holds a
 *        reference writes through it; a reference to a local or to a name of a method that returned
 *        fails, and Return gives the caller what it referred to instead; a package element that names
 *        the name referring to it fails after 8 names (§5.1).
 */
KERNEL_TEST(references_follow_names_locals_and_arguments)
{
    if (!references_run(test, references_follow_aml, sizeof(references_follow_aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 0x1234u, "DerefOf (RefOf (INT0)) is 0x1234");
    kernel_test_check(test, references_integer("R001") == 9u, "a store to Local0 = RefOf (Local1) sets Local1");
    kernel_test_check(test, references_integer("R002") == 7u, "Arg0 = 7 sets the caller's Local0 it refers to");
    kernel_test_check(test, references_integer("R003") == 0x1234u, "CondRefOf (INT0, Local0) stores a reference");
    kernel_test_check(test,
                      references_integer("R004") == 0u && references_integer("R005") == REFERENCES_ONES &&
                          references_integer("R006") == 0u,
                      "CondRefOf is False for an External and a missing name, True for \\_OSI");
    kernel_test_check(test, references_integer("R007") == 0x42u, "Return (RefOf (Local0)) gives the caller 0x42");
    kernel_test_check(test, references_integer("R008") == 0x11u,
                      "Increment through three references reaches Local0, and DerefOf reads it");
    kernel_test_check(test, references_integer("R009") == 0x1234u, "DerefOf (\"\\\\INT0\") reads INT0");
    kernel_test_check(test, references_integer("R010") == 0x77u,
                      "a reference to a table's Local0, made by one statement, is read by the next ones");
    kernel_test_check(test, references_integer("R011") == 2u,
                      "a store to a local that refers to a String keeps the String, as a store to it does");
    kernel_test_check(test, references_integer("R012") == 7u,
                      "a reference to the Local0 of a method that returned fails, though another frame took its place");
    kernel_test_check(test, references_integer("R013") == 0x11u,
                      "Return (RefOf (TMPN)) of a name the method created gives the caller 0x11");
    kernel_test_check(
        test, references_integer("R014") == 7u,
        "a reference to a name a method created fails once it returned, though another name took its node");
    kernel_test_check(test, references_integer("R015") == 7u,
                      "a reference to the Local0 of a method that returned fails, though no frame took its place");
    kernel_test_check(test, references_integer("R016") == 9u,
                      "a store to an element that holds a reference writes through it, as Windows does");
    kernel_test_check(test, references_integer("R018") == 0x1237u && references_integer("INT0") == 0x1234u,
                      "CondRefOf to a Local0 that refers to a method, then to INT0, replaces the reference each time");
    kernel_test_check(
        test, references_integer("R019") == 0x11u,
        "a Buffer stored to a Local0 that refers to \\_OSI replaces the reference, as firmware's _OSC needs");
    kernel_test_check(test, references_integer("R020") == 0x44u,
                      "a store to a Local1 whose reference outlived its frame replaces the reference");
    kernel_test_check(test, references_integer("R021") == 5u,
                      "a store to a Local0 that refers to an element naming a Device replaces the reference");
    kernel_test_check(
        test,
        references_integer("R017") == 7u &&
            references_failed(
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_RESOLUTION_LOOP) == 1u &&
            made_up_limit_peak(&references_load, "name_indirections_peak") == 8u,
        "an element naming the name that refers to it fails after 8 names with name_resolution_loop");
    kernel_test_check(
        test,
        references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND) == 3u,
        "the three references to a returned method's local or name fail with name_not_found");
    kernel_test_check(test, references_load.evaluation.failures == 4u, "nothing else fails");
    references_close(test);
}

/**
 * @brief Index refers to the slot of a Package, a Buffer or a String, never to a copy (§5.2): a store
 *        through it changes the named object, Increment works in place, an element that is a name is
 *        resolved when read, an index past the end fails with index_out_of_range, and a store through
 *        an argument that holds an Index overwrites the element. DerefOf (Index (...)) builds no
 *        reference, unless it names a target, as ObjectType's operand does (§7.2, O8).
 */
KERNEL_TEST(index_refers_to_the_element_not_a_copy)
{
    if (!references_run(test, references_index_aml, sizeof(references_index_aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 5u, "Store (5, Index (PKG0, 1)) changes PKG0");
    kernel_test_check(test, references_integer("R001") == 0xFFu, "Store (0x1FF, Index (BUF0, 0)) stores the low byte");
    kernel_test_check(
        test,
        references_holds("STR0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, "AXC", 3u),
        "Store (0x58, Index (STR0, 1)) makes \"AXC\"");
    kernel_test_check(test, references_integer("R002") == 9u, "a store to a local that holds Index (PKG0, 2) sets it");
    kernel_test_check(test, references_integer("R003") == 3u, "Increment (Index (BUF0, 1)) takes 2 to 3");
    kernel_test_check(test, references_integer("R004") == 0x77u, "a store through a nested Index changes NEST");
    kernel_test_check(test, references_integer("R005") == 0x55u, "an element naming INT1 reads INT1");
    kernel_test_check(
        test,
        references_integer("R006") == 7u &&
            references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE) ==
                1u,
        "Index (PKG0, 3) fails with index_out_of_range");
    kernel_test_check(test, references_integer("R007") == 2u, "Arg0 = \"STR\" through Index (PKG0, 0) makes a String");
    kernel_test_check(test, references_load.evaluation.references_built == 8u,
                      "eight Index results build references: DerefOf reads the others at once");
    references_close(test);
}

/**
 * @brief Match gives the first element from the start that both tests accept, each element converted
 *        to the match object's type, and Ones when none does.
 */
KERNEL_TEST(match_gives_the_first_element_both_tests_accept)
{
    if (!references_run(test, references_match_aml, sizeof(references_match_aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 1u, "the first 5 is at 1");
    kernel_test_check(test, references_integer("R001") == 4u, "from 2, the next 5 is at 4");
    kernel_test_check(test, references_integer("R002") == 1u, "the first element above 4 and below 9 is at 1");
    kernel_test_check(test, references_integer("R003") == 3u, "the first element of 9 or more is at 3");
    kernel_test_check(test, references_integer("R004") == REFERENCES_ONES, "no element equals 8");
    kernel_test_check(test, references_integer("R005") == 5u, "Buffer { 7 } converts to 7, at 5");
    kernel_test_check(test, references_load.evaluation.failures == 0u, "nothing fails");
    references_close(test);
}

/**
 * @brief Buffer fields read and write their bits (§5.2): the fixed-width ones read as Integers,
 *        CreateField as a Buffer; a write changes only the field's bits; a field over an argument
 *        writes the argument's Buffer, as _OSC does; CreateField past the Buffer fails with
 *        index_out_of_range (§6.3), as does a read of a field past its Buffer; a field of length 0
 *        reads as an empty Buffer and writes nothing; a field that takes its own Buffer takes it as it
 *        was before the store.
 */
KERNEL_TEST(buffer_fields_read_and_write_their_bits)
{
    static const uint8_t created[] = {0x30u, 0x02u};
    static const uint8_t answered[] = {0x01u, 0x02u, 0x00u, 0x00u, 0x11u, 0x02u, 0x00u, 0x00u};
    static const uint8_t written[] = {0x01u, 0xA8u, 0x5Au, 0x5Au, 0x89u, 0xABu, 0xCDu, 0xEFu};
    static const uint8_t zeros[] = {0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t overlapped[] = {0xFFu, 0x0Fu};
    static const uint8_t ahead[] = {0x11u, 0x32u, 0x43u, 0x44u};

    if (!references_run(test, references_buffer_fields_aml, sizeof(references_buffer_fields_aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 0x23u, "CreateByteField reads byte 1");
    kernel_test_check(test, references_integer("R001") == 0x6745u, "CreateWordField reads bytes 2 and 3");
    kernel_test_check(test, references_integer("R002") == 0xEFCDAB89u, "CreateDWordField reads bytes 4 to 7");
    kernel_test_check(test, references_integer("R003") == 0xEFCDAB8967452301ull, "CreateQWordField reads all 8");
    kernel_test_check(test, references_integer("R004") == 1u, "CreateBitField reads bit 9");
    kernel_test_check(test,
                      references_holds("R005", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       created, sizeof(created)),
                      "CreateField (4, 12) reads Buffer { 0x30, 0x02 }");
    kernel_test_check(test,
                      references_holds("R007", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       written, sizeof(written)),
                      "the byte, bit and word writes change their bits only");
    kernel_test_check(test,
                      references_holds("R006", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       answered, sizeof(answered)),
                      "CDW2 over Arg0 takes CDW1 | 0x10, and the method returns the Buffer it wrote");
    kernel_test_check(test, references_integer("R008") == 7u, "CreateField (BUF1, 0xFFFFFFF0, 0x20, FBIG) fails");
    kernel_test_check(
        test,
        references_integer("R010") == 7u &&
            references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_INDEX_OUT_OF_RANGE) ==
                2u,
        "PAST, 32 bits from byte 2 of a 4-byte Buffer, fails when read: both with index_out_of_range");
    kernel_test_check(
        test,
        references_holds("R009", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, zeros, 0u) &&
            references_holds("BUF1", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, zeros,
                             sizeof(zeros)),
        "a field of length 0 reads as an empty Buffer and writes nothing");
    kernel_test_check(test,
                      references_holds("R011", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                                       overlapped, sizeof(overlapped)),
                      "FLD2, bits 4 to 15 of BUF2, takes BUF2's first 12 bits as they were before the store");
    kernel_test_check(
        test,
        references_holds("R012", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER, ahead,
                         sizeof(ahead)),
        "FLD3, bits 12 to 23 of BUF3, takes the low 12 bits of BUF3's last two bytes, which lie ahead of it");
    references_close(test);
}

/**
 * @brief A store that would make an object reach itself fails with reference_cycle (§3.5): a package
 *        that would refer to its own name, directly or through another package, a temporary package
 *        that would refer to its local, a local that would refer to itself; references to the same
 *        package from two elements are no cycle.
 */
KERNEL_TEST(a_store_that_would_close_a_cycle_is_refused)
{
    if (!references_run(test, references_cycle_aml, sizeof(references_cycle_aml)))
        return;
    kernel_test_check(
        test,
        references_failed(ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_REFERENCE_CYCLE) == 4u,
        "the four cyclic stores fail with reference_cycle");
    kernel_test_check(test,
                      references_element("PKG0", 0u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                          references_element("PKG0", 1u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
                      "PKG0 keeps its two Integers");
    kernel_test_check(test,
                      references_element("PKG1", 0u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE &&
                          references_element("PKG2", 0u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE &&
                          references_element("PKG2", 1u)->kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_REFERENCE_NODE,
                      "PKG1 refers to PKG0, and PKG2 twice to PKG1");
    kernel_test_check(test, references_integer("R000") == 7u && references_integer("R001") == 7u,
                      "the two methods that close a cycle store nothing");
    references_close(test);
}

/**
 * @brief A Name that holds a reference is an alias of what it refers to (§5.2): it reads as its
 *        target, a store to it reaches the target, ObjectType gives the target's type; a store through
 *        a reference to an argument writes into the object the caller shares with it, its size kept.
 */
KERNEL_TEST(a_name_that_holds_a_reference_is_an_alias)
{
    static const uint8_t shared[] = {0x41u, 0x42u, 0x43u, 0x44u, 0x00u, 0x00u, 0x00u, 0x00u};

    if (!references_run(test, references_alias_aml, sizeof(references_alias_aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 0x1234u, "ALS0 reads as TGT0");
    kernel_test_check(test, references_integer("R001") == 5u, "Store (5, ALS0) sets TGT0");
    kernel_test_check(test, references_integer("R002") == 1u, "ObjectType (ALS0) is TGT0's");
    kernel_test_check(test,
                      references_holds("STR0", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                       shared, sizeof(shared)) &&
                          references_integer("R003") == 8u,
                      "STR0 takes 0x44434241's bytes through Arg0 and keeps its 8 characters");
    kernel_test_check(test, references_load.evaluation.failures == 0u, "nothing fails");
    references_close(test);
}

/**
 * @brief CopyObject to a Name that refers to a package element replaces the reference, as Windows
 *        does, where a Name that refers to a named object has its target replaced: the element keeps
 *        its value, and the Name takes the copied object.
 */
KERNEL_TEST(copy_object_replaces_a_name_that_refers_to_a_package_element)
{
    if (!references_run(test, references_copy_over_element_aml, sizeof(references_copy_over_element_aml)))
        return;
    kernel_test_check(test, references_integer("R000") == 2u, "PKG0[1] keeps 2");
    kernel_test_check(
        test,
        references_holds("ALS2", ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING, "ab", 2u),
        "ALS2 holds the String \"ab\"");
    kernel_test_check(test, references_load.evaluation.failures == 0u, "nothing fails");
    references_close(test);
}

/**
 * @brief The bits a field of @p length bits at bit @p offset reads from a 16-byte Buffer, computed
 *        with two 64-bit words rather than bit by bit.
 */
static uint64_t references_model_read(const uint8_t *bytes, uint32_t offset, uint32_t length)
{
    uint64_t low = 0u;
    uint64_t high = 0u;

    for (uint32_t byte = 0u; byte < 8u; ++byte)
    {
        low |= (uint64_t) bytes[byte] << (8u * byte);
        high |= (uint64_t) bytes[8u + byte] << (8u * byte);
    }

    const uint64_t window = (offset == 0u) ? low : ((low >> offset) | (high << (64u - offset)));

    return (length == 64u) ? window : (window & ((1ull << length) - 1u));
}

/**
 * @brief Writes into @p bytes, a copy of the 16-byte Buffer, what a field of @p length bits at bit
 *        @p offset leaves after taking @p value, computed with two 64-bit words.
 */
static void references_model_write(uint8_t *bytes, uint32_t offset, uint32_t length, uint64_t value)
{
    const uint64_t mask = (length == 64u) ? ~0ull : ((1ull << length) - 1u);
    uint64_t low = 0u;
    uint64_t high = 0u;

    for (uint32_t byte = 0u; byte < 8u; ++byte)
    {
        low |= (uint64_t) bytes[byte] << (8u * byte);
        high |= (uint64_t) bytes[8u + byte] << (8u * byte);
    }
    value &= mask;
    low = (low & ~(mask << offset)) | (value << offset);
    if (offset != 0u)
        high = (high & ~(mask >> (64u - offset))) | (value >> (64u - offset));
    for (uint32_t byte = 0u; byte < 8u; ++byte)
    {
        bytes[byte] = (uint8_t) (low >> (8u * byte));
        bytes[8u + byte] = (uint8_t) (high >> (8u * byte));
    }
}

/**
 * @brief Buffer fields read and write exactly at every bit offset from 0 to 63 and every length from
 *        1 to 64 (§6.3, §8.2): each read equals the bits two 64-bit words give, each write leaves the
 *        Buffer as they do. The reads are folded, so that the host and ring 0 compare them.
 */
KERNEL_TEST(buffer_fields_read_and_write_every_bit_offset_and_length)
{
    uint8_t pattern[REFERENCES_FIELD_BYTES];
    uint8_t expected[REFERENCES_FIELD_BYTES];
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t buffer;
    uint32_t wrong_reads = 0u;
    uint32_t wrong_writes = 0u;
    uint32_t fold = REFERENCES_FOLD_OFFSET;

    if (!references_run(test, common_result_zero_aml, sizeof(common_result_zero_aml)))
        return;
    for (uint32_t byte = 0u; byte < REFERENCES_FIELD_BYTES; ++byte)
        pattern[byte] = (uint8_t) (0x5Bu + 0x1Du * byte);
    if (!kernel_test_check(test,
                           advanced_configuration_and_power_interface_machine_language_value_allocate(
                               &references_load.heap, &references_load.space,
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER,
                               REFERENCES_FIELD_BYTES,
                               &buffer) == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK,
                           "the Buffer is allocated"))
        return;

    uint8_t *bytes = advanced_configuration_and_power_interface_machine_language_object_payload(&references_load.heap,
                                                                                                buffer.handle);

    for (uint32_t offset = 0u; offset < 64u; ++offset)
    {
        for (uint32_t length = 1u; length <= 64u; ++length)
        {
            const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t field = {
                .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER_FIELD,
                .auxiliary = 1u,
                .handle = buffer.handle,
                .payload = offset | ((uint64_t) length << 32u),
            };
            const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t source = {
                .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
                .payload = 0xA5C3F00F96E1B42Dull ^ ((uint64_t) offset << 40u) ^ length,
            };
            AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t read;

            memcpy(bytes, pattern, sizeof(pattern));
            advanced_configuration_and_power_interface_machine_language_buffer_field_read(
                &references_load.heap, &references_load.space, &field, 64u, &read);
            wrong_reads += (read.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER ||
                            read.payload != references_model_read(pattern, offset, length)) ?
                               1u :
                               0u;
            for (uint32_t byte = 0u; byte < 8u; ++byte)
                fold = (fold ^ (uint8_t) (read.payload >> (8u * byte))) * REFERENCES_FOLD_PRIME;
            memcpy(expected, pattern, sizeof(pattern));
            references_model_write(expected, offset, length, source.payload);
            advanced_configuration_and_power_interface_machine_language_buffer_field_write(&references_load.heap,
                                                                                           &field, &source, 64u);
            wrong_writes += (memcmp(bytes, expected, sizeof(expected)) != 0) ? 1u : 0u;
        }
    }
    kernel_test_measure(test, "fold", fold);
    kernel_test_check(test, wrong_reads == 0u, "the 4,096 reads equal the model's");
    kernel_test_check(test, wrong_writes == 0u, "the 4,096 writes leave the Buffer as the model does");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &buffer);
    references_close(test);
}

/**
 * @brief An evaluation by path runs a method with the arguments it is given and returns its result,
 *        reads a named object's value, and refuses a path that names nothing, a malformed path and the
 *        wrong number of arguments.
 */
KERNEL_TEST(an_evaluation_by_path_runs_its_method_or_reads_its_object)
{
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t arguments[2];
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t result;
    AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status;

    if (!references_run(test, references_evaluation_by_path_aml, sizeof(references_evaluation_by_path_aml)))
        return;
    advanced_configuration_and_power_interface_machine_language_value_to_string(
        &references_load.heap, &references_load.space,
        &(const AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){
            .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
            .payload = 0xABu},
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STRING_HEXADECIMAL, 64u, &arguments[0]);
    arguments[1] = (AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t){
        .kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER,
        .payload = 0x2Au};
    status = advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\_SB_.MCAT",
                                                                                  arguments, 2u, &result);
    kernel_test_check(
        test,
        status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            references_value_holds(&result, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                   "0xAB000000000000002A", 20u),
        "\\_SB_.MCAT (\"0xAB\", 0x2A) returns their concatenation");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &result);
    status = advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\STR0", NULL, 0u,
                                                                                  &result);
    kernel_test_check(
        test,
        status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
            references_value_holds(&result, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING,
                                   "XY", 2u),
        "\\STR0 reads \"XY\"");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &result);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\NOPE",
                                                                                           NULL, 0u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_NAME_NOT_FOUND,
                      "\\NOPE names nothing");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &references_load, "_SB_.MCAT", NULL, 0u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a path that does not start at the root is malformed");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(&references_load, "\\STR0.",
                                                                                           NULL, 0u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TRUNCATED,
                      "a path that ends with a dot is malformed");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_evaluate(
                          &references_load, "\\_SB_.MCAT", arguments, 1u, &result) ==
                          ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_TYPE_MISMATCH,
                      "MCAT takes two arguments, not one");
    advanced_configuration_and_power_interface_machine_language_value_release(&references_load.heap,
                                                                              &references_load.space, &arguments[0]);
    references_close(test);
}
