#include "made_up_aml.h"
#include "made_up_table.h"
#include "simulated_host.h"

#include <kernel/acpi/machine_language.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_regions, KERNEL_TEST_STAGE_INITIALIZATION);

#define REGIONS_BYTE_ACCESS  0x01u    /**< FieldFlags AccessType ByteAcc. */
#define REGIONS_DWORD_ACCESS 0x03u    /**< FieldFlags AccessType DWordAcc. */
#define REGIONS_QWORD_ACCESS 0x04u    /**< FieldFlags AccessType QWordAcc. */
#define REGIONS_LOCK         0x10u    /**< FieldFlags LockRule Lock. */
#define REGIONS_WRITE_ONES   0x20u    /**< FieldFlags UpdateRule WriteAsOnes. */
#define REGIONS_WRITE_ZEROS  0x40u    /**< FieldFlags UpdateRule WriteAsZeros. */
#define REGIONS_MEMORY       0x10000u /**< Where the simulated memory the tests read starts. */
#define REGIONS_STATUS(name) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_##name
#define REGIONS_ACCESS(kind) ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_##kind

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t regions_host;
static AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t regions_load;
static uint8_t regions_table[MADE_UP_TABLE_HEADER + MADE_UP_AML_CAPACITY];
static MadeUpAml_t regions_aml;

/**
 * @brief Opens the simulated host and empties the AML, for a test to give them what it needs.
 */
static void regions_begin(void)
{
    advanced_configuration_and_power_interface_simulated_host_open(&regions_host);
    made_up_aml_begin(&regions_aml);
}

/**
 * @brief Loads the AML written as a DSDT of @p revision through the simulated host as it stands.
 */
static bool regions_run(KernelTest_t *test, uint8_t revision)
{
    if (!kernel_test_check(test, !regions_aml.overflowed, "the made-up AML fits"))
        return false;
    return made_up_load_on(test, &regions_load, &regions_host.host, regions_table, revision, regions_aml.bytes,
                           regions_aml.length);
}

/**
 * @brief The node at the root called @p name, as an index.
 */
static uint32_t regions_node(const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &regions_load.space, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_ROOT,
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u));
}

/**
 * @brief The integer the Name at the root called @p name holds, or 0xDEAD when it holds none.
 */
static uint64_t regions_integer(const char *name)
{
    const AdvancedConfigurationAndPowerInterfaceMachineLanguageNode_t *node = made_up_root_node(&regions_load, name);

    if (node == NULL || node->value.kind != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER)
        return 0xDEADu;
    return node->value.payload;
}

/**
 * @brief How many runs of table-level code failed with @p status.
 */
static uint32_t regions_failed(AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t status)
{
    return regions_load.evaluation.statuses[status];
}

/**
 * @brief Whether access @p index of the log is of @p kind and @p width, at @p where (an address or a
 *        port), with @p value.
 */
static bool regions_access(uint32_t index, uint8_t kind, uint8_t width, uint64_t where, uint32_t value)
{
    const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &regions_host.accesses[index];
    const bool port = kind == REGIONS_ACCESS(PORT_READ) || kind == REGIONS_ACCESS(PORT_WRITE);

    return index < regions_host.access_count &&
           index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES && access->kind == kind &&
           access->width == width && (port ? access->port == where : access->address == where) &&
           access->value == value;
}

/**
 * @brief Closes the load, then checks that every map it made was unmapped as it was mapped, and gives
 *        the simulated memory back.
 */
static void regions_close(KernelTest_t *test)
{
    advanced_configuration_and_power_interface_machine_language_close(&regions_load);
    kernel_test_check(test,
                      regions_host.outstanding_maps == 0u && regions_host.mismatched_unmaps == 0u &&
                          regions_host.inconsistent_accesses == 0u,
                      "every access went where its mapping puts it, and closing the load unmaps every mapping, "
                      "each as it was mapped");
    advanced_configuration_and_power_interface_simulated_host_close(&regions_host);
}

/**
 * @brief A Field of ByteAcc units over SystemMemory reads the bits each unit covers, and a write to
 *        part of a byte keeps its other bits (Preserve); every access goes through one mapping of
 *        the page.
 */
KERNEL_TEST(a_memory_field_reads_and_writes_the_bits_it_covers)
{
    static const uint8_t memory[] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u};
    static const MadeUpFieldUnit_t units[] = {
        {"F0__", 8u },
        {"F1__", 16u},
        {"F3__", 4u },
        {"F3H_", 4u },
        {"F4__", 12u},
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "RAM0", 0x00u, REGIONS_MEMORY, sizeof(memory));
    made_up_aml_field(&regions_aml, "RAM0", REGIONS_BYTE_ACCESS, units, 5u);
    made_up_aml_name_integer(&regions_aml, "R0__", 0u);
    made_up_aml_name_integer(&regions_aml, "R1__", 0u);
    made_up_aml_name_integer(&regions_aml, "R3__", 0u);
    made_up_aml_name_integer(&regions_aml, "R3H_", 0u);
    made_up_aml_name_integer(&regions_aml, "R4__", 0u);
    made_up_aml_store_name(&regions_aml, "F0__", "R0__");
    made_up_aml_store_name(&regions_aml, "F1__", "R1__");
    made_up_aml_store_name(&regions_aml, "F3__", "R3__");
    made_up_aml_store_name(&regions_aml, "F3H_", "R3H_");
    made_up_aml_store_name(&regions_aml, "F4__", "R4__");
    made_up_aml_store_integer(&regions_aml, 0x0Au, "F3__");
    made_up_aml_store_integer(&regions_aml, 0x0BCDu, "F4__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("R0__") == 0x11u && regions_integer("R1__") == 0x3322u &&
                          regions_integer("R3__") == 0x4u && regions_integer("R3H_") == 0x4u &&
                          regions_integer("R4__") == 0x655u,
                      "the units read 0x11, 0x3322, 0x4, 0x4 and 0x655");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY + 3u,
                                                                                     1u) == 0x4Au &&
                          advanced_configuration_and_power_interface_simulated_host_peek(
                              &regions_host, REGIONS_MEMORY + 4u, 2u) == 0x6BCDu,
                      "writing 0xA to F3__ leaves 0x4A, writing 0xBCD to F4__ leaves 0x6BCD");
    kernel_test_check(test, regions_host.access_count == 12u,
                      "seven reads, then a read and a write for F3__, a write, a read and a write for F4__");
    kernel_test_check(test, regions_load.regions.map_calls == 1u && regions_load.regions.mapping_cache_hits == 11u,
                      "the page is mapped once, and the cache serves the eleven accesses after the first");
    kernel_test_check(test,
                      regions_load.regions.field_reads == 5u && regions_load.regions.field_writes == 2u &&
                          regions_load.evaluation.failures == 0u,
                      "five reads, two writes, nothing fails");
    regions_close(test);
}

/**
 * @brief A write to part of a DWordAcc datum leaves its other bits as the UpdateRule says: what the
 *        datum held for Preserve, read first; ones for WriteAsOnes; zeros for WriteAsZeros, neither
 *        read first.
 */
KERNEL_TEST(update_rules_decide_the_bits_a_partial_write_leaves)
{
    static const uint8_t memory[] = {0x78u, 0x56u, 0x34u, 0x12u, 0x78u, 0x56u,
                                     0x34u, 0x12u, 0x78u, 0x56u, 0x34u, 0x12u};
    static const MadeUpFieldUnit_t preserve[] = {
        {NULL,   4u},
        {"UPP_", 8u}
    };
    static const MadeUpFieldUnit_t ones[] = {
        {NULL,   36u},
        {"UP1_", 8u }
    };
    static const MadeUpFieldUnit_t zeros[] = {
        {NULL,   68u},
        {"UP0_", 8u }
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "UPDR", 0x00u, REGIONS_MEMORY, sizeof(memory));
    made_up_aml_field(&regions_aml, "UPDR", REGIONS_DWORD_ACCESS, preserve, 2u);
    made_up_aml_field(&regions_aml, "UPDR", REGIONS_DWORD_ACCESS | REGIONS_WRITE_ONES, ones, 2u);
    made_up_aml_field(&regions_aml, "UPDR", REGIONS_DWORD_ACCESS | REGIONS_WRITE_ZEROS, zeros, 2u);
    made_up_aml_store_integer(&regions_aml, 0xABu, "UPP_");
    made_up_aml_store_integer(&regions_aml, 0xABu, "UP1_");
    made_up_aml_store_integer(&regions_aml, 0xABu, "UP0_");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY,
                                                                                     4u) == 0x12345AB8u,
                      "Preserve leaves 0x12345AB8");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY + 4u,
                                                                                     4u) == 0xFFFFFABFu,
                      "WriteAsOnes leaves 0xFFFFFABF");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY + 8u,
                                                                                     4u) == 0x00000AB0u,
                      "WriteAsZeros leaves 0x00000AB0");
    kernel_test_check(test,
                      regions_host.access_count == 4u &&
                          regions_access(0u, REGIONS_ACCESS(MEMORY_READ), 4u, REGIONS_MEMORY, 0x12345678u) &&
                          regions_access(1u, REGIONS_ACCESS(MEMORY_WRITE), 4u, REGIONS_MEMORY, 0x12345AB8u) &&
                          regions_access(2u, REGIONS_ACCESS(MEMORY_WRITE), 4u, REGIONS_MEMORY + 4u, 0xFFFFFABFu) &&
                          regions_access(3u, REGIONS_ACCESS(MEMORY_WRITE), 4u, REGIONS_MEMORY + 8u, 0x00000AB0u),
                      "only Preserve reads its datum before writing it");
    regions_close(test);
}

/**
 * @brief A QWordAcc datum of SystemMemory is two 32-bit accesses, low then high, written and read
 *        alike, and counted (§5.3).
 */
KERNEL_TEST(a_quad_word_datum_is_two_double_word_accesses_low_then_high)
{
    static const MadeUpFieldUnit_t units[] = {
        {"QW__", 64u}
    };

    regions_begin();
    made_up_aml_region(&regions_aml, "QWDR", 0x00u, REGIONS_MEMORY, 8u);
    made_up_aml_field(&regions_aml, "QWDR", REGIONS_QWORD_ACCESS, units, 1u);
    made_up_aml_name_integer(&regions_aml, "RQ__", 0u);
    made_up_aml_store_integer(&regions_aml, 0x1122334455667788ull, "QW__");
    made_up_aml_store_name(&regions_aml, "QW__", "RQ__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_host.access_count == 4u &&
                          regions_access(0u, REGIONS_ACCESS(MEMORY_WRITE), 4u, REGIONS_MEMORY, 0x55667788u) &&
                          regions_access(1u, REGIONS_ACCESS(MEMORY_WRITE), 4u, REGIONS_MEMORY + 4u, 0x11223344u) &&
                          regions_access(2u, REGIONS_ACCESS(MEMORY_READ), 4u, REGIONS_MEMORY, 0x55667788u) &&
                          regions_access(3u, REGIONS_ACCESS(MEMORY_READ), 4u, REGIONS_MEMORY + 4u, 0x11223344u),
                      "the low half is written, then the high half; read in the same order");
    kernel_test_check(
        test, regions_integer("RQ__") == 0x1122334455667788ull && regions_load.regions.split_qword_accesses == 2u,
        "the value comes back whole, and both accesses are counted as split");
    kernel_test_check(test, regions_load.evaluation.field_accesses == 4u,
                      "each half counts as a field access: two written, two read");
    regions_close(test);
}

/**
 * @brief A unit of an IndexField is reached by writing its datum's byte offset to the index register,
 *        then reading or writing the data register.
 */
KERNEL_TEST(an_index_field_writes_its_index_before_its_data)
{
    static const MadeUpFieldUnit_t registers[] = {
        {"INDX", 8u},
        {"DATA", 8u}
    };
    static const MadeUpFieldUnit_t units[] = {
        {NULL,   16u},
        {"U2__", 8u },
        {"U3__", 8u }
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, 0x0B01u, 0x5Au);
    made_up_aml_region(&regions_aml, "IDXR", 0x01u, 0x0B00u, 2u);
    made_up_aml_field(&regions_aml, "IDXR", REGIONS_BYTE_ACCESS, registers, 2u);
    made_up_aml_index_field(&regions_aml, "INDX", "DATA", REGIONS_BYTE_ACCESS, units, 3u);
    made_up_aml_name_integer(&regions_aml, "R2__", 0u);
    made_up_aml_store_name(&regions_aml, "U2__", "R2__");
    made_up_aml_store_integer(&regions_aml, 0x77u, "U3__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test, regions_integer("R2__") == 0x5Au, "U2__ reads what the data port gives");
    kernel_test_check(test,
                      regions_host.access_count == 4u &&
                          regions_access(0u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B00u, 2u) &&
                          regions_access(1u, REGIONS_ACCESS(PORT_READ), 1u, 0x0B01u, 0x5Au) &&
                          regions_access(2u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B00u, 3u) &&
                          regions_access(3u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B01u, 0x77u),
                      "index 2 then a data read, index 3 then a data write");
    kernel_test_check(test, regions_load.evaluation.field_accesses == 4u,
                      "each index write and each data access counts as a field access");
    regions_close(test);
}

/**
 * @brief A unit of a BankField is reached in its region once its BankValue is in the bank register.
 */
KERNEL_TEST(a_bank_field_selects_its_bank_before_its_datum)
{
    static const uint8_t memory[] = {0x10u, 0x20u, 0x30u, 0x40u};
    static const MadeUpFieldUnit_t bank[] = {
        {"BSEL", 8u}
    };
    static const MadeUpFieldUnit_t units[] = {
        {"BK0_", 8u},
        {"BK1_", 8u}
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "BNKR", 0x01u, 0x0B10u, 1u);
    made_up_aml_field(&regions_aml, "BNKR", REGIONS_BYTE_ACCESS, bank, 1u);
    made_up_aml_region(&regions_aml, "BRAM", 0x00u, REGIONS_MEMORY, sizeof(memory));
    made_up_aml_bank_field(&regions_aml, "BRAM", "BSEL", 2u, REGIONS_BYTE_ACCESS, units, 2u);
    made_up_aml_name_integer(&regions_aml, "RB__", 0u);
    made_up_aml_store_name(&regions_aml, "BK1_", "RB__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RB__") == 0x20u && regions_host.access_count == 2u &&
                          regions_access(0u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B10u, 2u) &&
                          regions_access(1u, REGIONS_ACCESS(MEMORY_READ), 1u, REGIONS_MEMORY + 1u, 0x20u),
                      "BankValue 2 goes to the bank port, then BK1_ reads 0x20");
    kernel_test_check(test, regions_load.evaluation.field_accesses == 2u,
                      "the bank selection and the datum count as two field accesses");
    regions_close(test);
}

/**
 * @brief The ports the kernel drives read as 0 and drop writes, counted, and no access reaches them;
 *        the ports beside each protected run are reached.
 */
KERNEL_TEST(a_protected_port_reads_zero_and_drops_writes)
{
    static const uint16_t ports[] = {0x0021u, 0x0022u, 0x04D1u, 0x04D2u, 0x0CF7u, 0x0CFCu};
    static const char *const names[] = {"P021", "P022", "P4D1", "P4D2", "PCF7", "PCFC"};
    static const char *const results[] = {"R021", "R022", "R4D1", "R4D2", "RCF7", "RCFC"};
    char region[5] = "PR00";

    regions_begin();
    for (uint32_t index = 0u; index < 6u; ++index)
    {
        const MadeUpFieldUnit_t unit = {names[index], 8u};

        region[3] = (char) ('0' + index);
        advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, ports[index], 0x33u);
        made_up_aml_region(&regions_aml, region, 0x01u, ports[index], 1u);
        made_up_aml_field(&regions_aml, region, REGIONS_BYTE_ACCESS, &unit, 1u);
        made_up_aml_name_integer(&regions_aml, results[index], 0xEEu);
        made_up_aml_store_name(&regions_aml, names[index], results[index]);
    }
    made_up_aml_store_integer(&regions_aml, 0x12u, "P021");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("R021") == 0u && regions_integer("R4D1") == 0u && regions_integer("RCFC") == 0u,
                      "0x21, 0x4D1 and 0xCFC read as 0");
    kernel_test_check(
        test, regions_integer("R022") == 0x33u && regions_integer("R4D2") == 0x33u && regions_integer("RCF7") == 0x33u,
        "0x22, 0x4D2 and 0xCF7 read what the port gives");
    kernel_test_check(test, regions_host.access_count == 3u && regions_load.regions.protected_port_accesses == 4u,
                      "three ports reached; three protected reads and a protected write counted");
    regions_close(test);
}

/**
 * @brief Without a global lock, as in a made-up load that names no FACS, a Lock field is accessed
 *        unlocked, counted, and nothing touches a lock word.
 */
KERNEL_TEST(without_a_global_lock_a_lock_field_is_accessed_unlocked)
{
    static const uint8_t memory[] = {0x5Au};
    static const MadeUpFieldUnit_t units[] = {
        {"LK__", 8u}
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "LCKR", 0x00u, REGIONS_MEMORY, 1u);
    made_up_aml_field(&regions_aml, "LCKR", REGIONS_BYTE_ACCESS | REGIONS_LOCK, units, 1u);
    made_up_aml_name_integer(&regions_aml, "RL__", 0x55u);
    made_up_aml_store_name(&regions_aml, "LK__", "RL__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RL__") == 0x5Au && regions_load.regions.unlocked_lock_accesses == 1u &&
                          regions_host.access_count == 1u,
                      "LK__ reads 0x5A unlocked, counted, in one access");
    regions_close(test);
}

/**
 * @brief An access must fit its region, whose length may be rounded up to the access width, counted;
 *        past that it fails with region_bounds.
 */
KERNEL_TEST(an_access_must_fit_its_region_rounded_to_its_width)
{
    static const MadeUpFieldUnit_t rounded[] = {
        {"RD0_", 8u}
    };
    static const MadeUpFieldUnit_t past[] = {
        {NULL,   24u},
        {"RD3_", 8u }
    };

    regions_begin();
    made_up_aml_region(&regions_aml, "RNDR", 0x00u, REGIONS_MEMORY, 3u);
    made_up_aml_field(&regions_aml, "RNDR", REGIONS_DWORD_ACCESS, rounded, 1u);
    made_up_aml_field(&regions_aml, "RNDR", REGIONS_BYTE_ACCESS, past, 2u);
    made_up_aml_name_integer(&regions_aml, "R0__", 0xEEu);
    made_up_aml_name_integer(&regions_aml, "R3__", 0xEEu);
    made_up_aml_store_name(&regions_aml, "RD0_", "R0__");
    made_up_aml_store_name(&regions_aml, "RD3_", "R3__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test, regions_integer("R0__") == 0u && regions_load.regions.region_lengths_rounded == 1u,
                      "a DWordAcc datum over 3 bytes reads, rounded and counted");
    kernel_test_check(test, regions_integer("R3__") == 0xEEu && regions_failed(REGIONS_STATUS(REGION_BOUNDS)) == 1u,
                      "a byte past the 3 fails with region_bounds");
    regions_close(test);
}

/**
 * @brief A region that reaches past 4 GiB fails with address_unreachable on i686, never truncated,
 *        and so does one whose offset wraps.
 */
KERNEL_TEST(a_region_past_4_gib_is_unreachable)
{
    static const MadeUpFieldUnit_t above[] = {
        {"HI__", 8u}
    };
    static const MadeUpFieldUnit_t across[] = {
        {"AC__", 32u}
    };
    static const MadeUpFieldUnit_t wrap[] = {
        {NULL,   32u},
        {"WR__", 8u }
    };

    regions_begin();
    made_up_aml_region(&regions_aml, "HIGR", 0x00u, 0x100010000ull, 4u);
    made_up_aml_field(&regions_aml, "HIGR", REGIONS_BYTE_ACCESS, above, 1u);
    made_up_aml_region(&regions_aml, "ACRR", 0x00u, 0xFFFFFFFEull, 4u);
    made_up_aml_field(&regions_aml, "ACRR", REGIONS_DWORD_ACCESS, across, 1u);
    made_up_aml_region(&regions_aml, "WRPR", 0x00u, 0xFFFFFFFFFFFFFFFCull, 8u);
    made_up_aml_field(&regions_aml, "WRPR", REGIONS_BYTE_ACCESS, wrap, 2u);
    made_up_aml_name_integer(&regions_aml, "RH__", 0xEEu);
    made_up_aml_store_name(&regions_aml, "HI__", "RH__");
    made_up_aml_store_name(&regions_aml, "AC__", "RH__");
    made_up_aml_store_name(&regions_aml, "WR__", "RH__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RH__") == 0xEEu && regions_failed(REGIONS_STATUS(ADDRESS_UNREACHABLE)) == 3u &&
                          regions_host.access_count == 0u && regions_host.outstanding_maps == 0u,
                      "the three reads fail with address_unreachable, and map nothing");
    regions_close(test);
}

/**
 * @brief A region whose offset is computed, Add (BASE, Zero), takes it when its declaration runs; one
 *        declared in a branch whose predicate failed, without running, has no address, and its
 *        fields fail with region_unevaluated.
 */
KERNEL_TEST(a_region_takes_computed_operands_when_its_declaration_runs)
{
    static const uint8_t memory[] = {0xC3u};
    static const uint8_t region_at_base[] = {0x5Bu, 0x80u, 'N', 'A', 'M',   'R',   0x00u, 0x72u,
                                             'B',   'A',   'S', 'E', 0x00u, 0x00u, 0x01u};
    static const uint8_t skipped_region[] = {0x5Bu, 0x80u, 'S', 'K', 'P',   'R',   0x00u, 0x72u,
                                             'B',   'A',   'S', 'E', 0x00u, 0x00u, 0x01u};
    static const MadeUpFieldUnit_t named[] = {
        {"NMF_", 8u}
    };
    static const MadeUpFieldUnit_t controller[] = {
        {"ECF_", 8u}
    };
    static const MadeUpFieldUnit_t skipped[] = {
        {"SKF_", 8u}
    };
    MadeUpAml_t branch;

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY + 0x30u, memory, 1u);
    made_up_aml_name_integer(&regions_aml, "BASE", REGIONS_MEMORY + 0x30u);
    made_up_aml_raw(&regions_aml, region_at_base, sizeof(region_at_base));
    made_up_aml_field(&regions_aml, "NAMR", REGIONS_BYTE_ACCESS, named, 1u);
    made_up_aml_region(&regions_aml, "ECR_", 0x03u, 0u, 1u);
    made_up_aml_field(&regions_aml, "ECR_", REGIONS_BYTE_ACCESS, controller, 1u);
    made_up_aml_begin(&branch);
    made_up_aml_name(&branch, "ECF_");
    made_up_aml_raw(&branch, skipped_region, sizeof(skipped_region));
    made_up_aml_field(&branch, "SKPR", REGIONS_BYTE_ACCESS, skipped, 1u);
    made_up_aml_raw(&regions_aml, (const uint8_t[]){0xA0u, (uint8_t) (branch.length + 1u)}, 2u);
    made_up_aml_raw(&regions_aml, branch.bytes, branch.length);
    made_up_aml_name_integer(&regions_aml, "RN__", 0u);
    made_up_aml_name_integer(&regions_aml, "RS__", 0xEEu);
    made_up_aml_store_name(&regions_aml, "NMF_", "RN__");
    made_up_aml_store_name(&regions_aml, "SKF_", "RS__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test, regions_integer("RN__") == 0xC3u, "NAMR, at BASE, reads 0xC3 there");
    kernel_test_check(test,
                      regions_integer("RS__") == 0xEEu && regions_failed(REGIONS_STATUS(REGION_UNEVALUATED)) == 1u &&
                          regions_load.declared_not_run == 2u,
                      "SKPR and SKF_ are declared without running, and SKF_ fails with region_unevaluated");
    regions_close(test);
}

/**
 * @brief The mapping cache maps each page an access touches once, cached for ACPI NVS and uncached
 *        elsewhere; past its 64 entries it gives back the least recently used.
 */
KERNEL_TEST(the_mapping_cache_maps_each_page_once_and_follows_the_memory_type)
{
    static const uint8_t store_to_local[] = {0x70u};
    MadeUpFieldUnit_t units[2u * 65u];
    char names[65][5];
    uint32_t cached = 0u;

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_describe(
        &regions_host, 0x200000u, 4096u, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_ACPI_NVS);
    for (uint32_t page = 0u; page < 65u; ++page)
    {
        names[page][0] = 'P';
        names[page][1] = (char) ('0' + page / 10u);
        names[page][2] = (char) ('0' + page % 10u);
        names[page][3] = '_';
        names[page][4] = '\0';
        units[2u * page].name = names[page];
        units[2u * page].bits = 8u;
        units[2u * page + 1u].name = NULL;
        units[2u * page + 1u].bits = 8u * 4096u - 8u;
    }
    made_up_aml_region(&regions_aml, "PGSR", 0x00u, 0x200000u, 65u * 4096u);
    made_up_aml_field(&regions_aml, "PGSR", REGIONS_BYTE_ACCESS, units, 2u * 65u - 1u);
    for (uint32_t page = 0u; page < 65u; ++page)
    {
        made_up_aml_raw(&regions_aml, store_to_local, 1u);
        made_up_aml_name(&regions_aml, names[page]);
        made_up_aml_raw(&regions_aml, (const uint8_t[]){0x60u}, 1u);
    }
    made_up_aml_raw(&regions_aml, (const uint8_t[]){0x70u, 'P', '0', '1', '_', 0x60u}, 6u);
    made_up_aml_raw(&regions_aml, (const uint8_t[]){0x70u, 'P', '0', '0', '_', 0x60u}, 6u);
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_load.regions.map_calls == 66u && regions_load.regions.mapping_cache_hits == 1u &&
                          regions_load.evaluation.failures == 0u,
                      "65 pages mapped once each, P01_ again from the cache, P00_ mapped again once evicted");
    kernel_test_check(test,
                      regions_load.regions.mapping_count == 64u && regions_host.outstanding_maps == 64u &&
                          regions_host.mismatched_unmaps == 0u,
                      "the cache holds 64 entries, and every eviction unmapped what it mapped");
    for (uint32_t index = 0u; index < regions_host.outstanding_maps; ++index)
        cached += (regions_host.maps[index].kind ==
                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_MAP_FIRMWARE_WRITABLE) ?
                      1u :
                      0u;
    kernel_test_check(test,
                      cached == 1u && regions_load.regions.mappings[2].physical_address == 0x200000u &&
                          regions_load.regions.mappings[2].kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_CACHED &&
                          regions_load.regions.mappings[0].physical_address == 0x200000u + 64u * 4096u &&
                          regions_load.regions.mappings[0].kind ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_MAPPING_DEVICE,
                      "the NVS page is mapped cached, again in the entry of page 2, the least recently used; "
                      "page 64, in the entry page 0 left, uncached");
    regions_close(test);
}

/**
 * @brief A unit wider than the load's integers reads as a Buffer of its bytes, one no wider as an
 *        Integer: 72 bits and 64 bits under revision 2, 40 bits and 32 bits under revision 1.
 */
KERNEL_TEST(a_unit_wider_than_an_integer_reads_as_a_buffer)
{
    static const uint8_t memory[] = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u};
    static const MadeUpFieldUnit_t units[] = {
        {"W72_", 72u},
        {"W64_", 64u},
        {"W40_", 40u},
        {"W32_", 32u}
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t value;

    for (uint8_t revision = 1u; revision <= 2u; ++revision)
    {
        const uint32_t wide = (revision == 2u) ? 0u : 2u;
        const uint32_t bytes = (revision == 2u) ? 9u : 5u;

        regions_begin();
        advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                        sizeof(memory));
        made_up_aml_region(&regions_aml, "WIDR", 0x00u, REGIONS_MEMORY, sizeof(memory));
        made_up_aml_field(&regions_aml, "WIDR", REGIONS_BYTE_ACCESS, &units[wide], 1u);
        made_up_aml_field(&regions_aml, "WIDR", REGIONS_BYTE_ACCESS, &units[wide + 1u], 1u);
        if (!regions_run(test, revision))
            return;

        const AdvancedConfigurationAndPowerInterfaceMachineLanguageStatus_t wide_status =
            advanced_configuration_and_power_interface_machine_language_field_read(
                &regions_load, regions_node(units[wide].name), &value);
        const AdvancedConfigurationAndPowerInterfaceMachineLanguageObjectHeader_t *header =
            advanced_configuration_and_power_interface_machine_language_object(&regions_load.heap, value.handle);

        kernel_test_check(test,
                          wide_status == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                              value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER &&
                              header != NULL && header->length == bytes && memcmp(header + 1, memory, bytes) == 0,
                          "the wide unit reads as a Buffer of its bytes");
        kernel_test_check(test,
                          advanced_configuration_and_power_interface_machine_language_field_read(
                              &regions_load, regions_node(units[wide + 1u].name), &value) ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_OK &&
                              value.kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_INTEGER &&
                              value.payload == ((revision == 2u) ? 0x0807060504030201ull : 0x04030201u),
                          "the unit as wide as an integer reads as an Integer");
        regions_close(test);
    }
}

/**
 * @brief A Buffer or a String written to a unit gives its bytes, cut to the unit or zero-extended.
 */
KERNEL_TEST(a_buffer_written_to_a_unit_is_cut_or_zero_extended)
{
    static const uint8_t memory[] = {0xEEu, 0xEEu, 0xEEu, 0xEEu, 0xEEu, 0xEEu, 0xEEu, 0xEEu, 0xEEu, 0xEEu};
    static const uint8_t three[] = {0x01u, 0x02u, 0x03u};
    static const MadeUpFieldUnit_t units[] = {
        {"F16_", 16u},
        {NULL,   8u },
        {"F24_", 24u},
        {"F32_", 32u}
    };
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t buffer = {0};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t one = {0};
    AdvancedConfigurationAndPowerInterfaceMachineLanguageValue_t string = {0};

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "BUFR", 0x00u, REGIONS_MEMORY, sizeof(memory));
    made_up_aml_field(&regions_aml, "BUFR", REGIONS_BYTE_ACCESS, units, 4u);
    if (!regions_run(test, 2u))
        return;
    buffer.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER;
    one.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_BUFFER;
    string.kind = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_VALUE_STRING;
    advanced_configuration_and_power_interface_machine_language_object_allocate(&regions_load.heap, &regions_load.space,
                                                                                buffer.kind, 3u, &buffer.handle);
    advanced_configuration_and_power_interface_machine_language_object_allocate(&regions_load.heap, &regions_load.space,
                                                                                one.kind, 1u, &one.handle);
    advanced_configuration_and_power_interface_machine_language_object_allocate(&regions_load.heap, &regions_load.space,
                                                                                string.kind, 2u, &string.handle);
    memcpy(advanced_configuration_and_power_interface_machine_language_object(&regions_load.heap, buffer.handle) + 1,
           three, 3u);
    memset(advanced_configuration_and_power_interface_machine_language_object(&regions_load.heap, one.handle) + 1, 0xAA,
           1u);
    memcpy(advanced_configuration_and_power_interface_machine_language_object(&regions_load.heap, string.handle) + 1,
           "AB", 2u);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_machine_language_field_write(
                          &regions_load, regions_node("F16_"), &buffer) == REGIONS_STATUS(OK) &&
                          advanced_configuration_and_power_interface_machine_language_field_write(
                              &regions_load, regions_node("F24_"), &one) == REGIONS_STATUS(OK) &&
                          advanced_configuration_and_power_interface_machine_language_field_write(
                              &regions_load, regions_node("F32_"), &string) == REGIONS_STATUS(OK),
                      "the three writes succeed");
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY, 3u) == 0xEE0201u,
        "a Buffer of three bytes gives F16_ its first two, and the byte after keeps 0xEE");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY + 3u,
                                                                                     3u) == 0x0000AAu,
                      "a Buffer of one byte gives F24_ 0xAA, then zeros");
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_simulated_host_peek(&regions_host, REGIONS_MEMORY + 6u,
                                                                                     4u) == 0x00004241u,
                      "the String \"AB\" gives F32_ 0x41, 0x42, then zeros");
    regions_close(test);
}

/**
 * @brief An index or a BankValue its register cannot hold is refused with index_out_of_range, and
 *        nothing is written: an IndexField unit at byte 256 behind an index register of 8 bits, a
 *        BankValue of 0x102 for a bank register of 8 bits.
 */
KERNEL_TEST(an_index_or_bank_value_wider_than_its_register_is_refused)
{
    static const MadeUpFieldUnit_t registers[] = {
        {"INDX", 8u},
        {"DATA", 8u}
    };
    static const MadeUpFieldUnit_t far[] = {
        {NULL,   2048u},
        {"U256", 8u   }
    };
    static const MadeUpFieldUnit_t bank[] = {
        {"BSEL", 8u}
    };
    static const MadeUpFieldUnit_t banked[] = {
        {"BKX_", 8u}
    };

    regions_begin();
    made_up_aml_region(&regions_aml, "IDXR", 0x01u, 0x0B00u, 2u);
    made_up_aml_field(&regions_aml, "IDXR", REGIONS_BYTE_ACCESS, registers, 2u);
    made_up_aml_index_field(&regions_aml, "INDX", "DATA", REGIONS_BYTE_ACCESS, far, 2u);
    made_up_aml_region(&regions_aml, "BNKR", 0x01u, 0x0B10u, 1u);
    made_up_aml_field(&regions_aml, "BNKR", REGIONS_BYTE_ACCESS, bank, 1u);
    made_up_aml_region(&regions_aml, "BRAM", 0x00u, REGIONS_MEMORY, 1u);
    made_up_aml_bank_field(&regions_aml, "BRAM", "BSEL", 0x102u, REGIONS_BYTE_ACCESS, banked, 1u);
    made_up_aml_store_integer(&regions_aml, 0x77u, "U256");
    made_up_aml_store_integer(&regions_aml, 0x77u, "BKX_");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test, regions_failed(REGIONS_STATUS(INDEX_OUT_OF_RANGE)) == 2u && regions_host.access_count == 0u,
                      "both writes fail with index_out_of_range, and nothing is written");
    regions_close(test);
}

/**
 * @brief An IndexField counts its index in bytes at every access width: DWordAcc units at bytes 4 and
 *        8 write the indices 4 and 8, then read or write the data register whole.
 */
KERNEL_TEST(an_index_field_counts_its_index_in_bytes_at_every_width)
{
    static const MadeUpFieldUnit_t registers[] = {
        {"INDX", 8u },
        {NULL,   24u},
        {"DATA", 32u}
    };
    static const MadeUpFieldUnit_t units[] = {
        {"D0__", 32u},
        {"D1__", 32u},
        {"D2__", 32u}
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, 0x0B24u, 0x11223344u);
    made_up_aml_region(&regions_aml, "IDXR", 0x01u, 0x0B20u, 8u);
    made_up_aml_field(&regions_aml, "IDXR", REGIONS_DWORD_ACCESS, registers, 3u);
    made_up_aml_index_field(&regions_aml, "INDX", "DATA", REGIONS_DWORD_ACCESS, units, 3u);
    made_up_aml_name_integer(&regions_aml, "R2__", 0u);
    made_up_aml_store_name(&regions_aml, "D2__", "R2__");
    made_up_aml_store_integer(&regions_aml, 0xCAFEu, "D1__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("R2__") == 0x11223344u && regions_host.access_count == 6u &&
                          regions_access(0u, REGIONS_ACCESS(PORT_READ), 4u, 0x0B20u, 0u) &&
                          regions_access(1u, REGIONS_ACCESS(PORT_WRITE), 4u, 0x0B20u, 8u) &&
                          regions_access(2u, REGIONS_ACCESS(PORT_READ), 4u, 0x0B24u, 0x11223344u) &&
                          regions_access(4u, REGIONS_ACCESS(PORT_WRITE), 4u, 0x0B20u, 4u) &&
                          regions_access(5u, REGIONS_ACCESS(PORT_WRITE), 4u, 0x0B24u, 0xCAFEu),
                      "index 8, then the data read; index 4, then the data written; the index register keeps "
                      "the bits of its datum it does not cover");
    regions_close(test);
}

/**
 * @brief An index register of 16 bits read in ByteAcc takes its index in two datums, low then high,
 *        and the data register is cut to the access width before a partial write merges into it.
 */
KERNEL_TEST(registers_are_written_datum_by_datum_and_data_cut_to_the_access_width)
{
    static const MadeUpFieldUnit_t registers[] = {
        {"INDX", 16u},
        {"DATA", 16u}
    };
    static const MadeUpFieldUnit_t units[] = {
        {NULL,   8u * 0x123u},
        {"NIB_", 4u         }
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, 0x0B32u, 0xCDu);
    advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, 0x0B33u, 0xABu);
    made_up_aml_region(&regions_aml, "IDXR", 0x01u, 0x0B30u, 4u);
    made_up_aml_field(&regions_aml, "IDXR", REGIONS_BYTE_ACCESS, registers, 2u);
    made_up_aml_index_field(&regions_aml, "INDX", "DATA", REGIONS_BYTE_ACCESS, units, 2u);
    made_up_aml_store_integer(&regions_aml, 0x5u, "NIB_");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_access(0u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B30u, 0x23u) &&
                          regions_access(1u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B31u, 0x01u),
                      "the index 0x123 goes out low byte first, high byte second");
    kernel_test_check(test,
                      regions_access(2u, REGIONS_ACCESS(PORT_READ), 1u, 0x0B32u, 0xCDu) &&
                          regions_access(3u, REGIONS_ACCESS(PORT_READ), 1u, 0x0B33u, 0xABu) &&
                          regions_access(6u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B32u, 0xC5u) &&
                          regions_access(7u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B33u, 0x00u),
                      "the data register reads 0xABCD and keeps 0xCD, the access width; the merged 0xC5 is "
                      "written, its high byte 0");
    regions_close(test);
}

/**
 * @brief A register that is not a unit of a Field is refused with type_mismatch.
 */
KERNEL_TEST(a_register_that_is_not_a_unit_of_a_field_is_refused)
{
    static const MadeUpFieldUnit_t units[] = {
        {"NU0_", 8u}
    };

    regions_begin();
    made_up_aml_name_integer(&regions_aml, "INDX", 0u);
    made_up_aml_name_integer(&regions_aml, "DATA", 0u);
    made_up_aml_index_field(&regions_aml, "INDX", "DATA", REGIONS_BYTE_ACCESS, units, 1u);
    made_up_aml_name_integer(&regions_aml, "RN__", 0xEEu);
    made_up_aml_store_name(&regions_aml, "NU0_", "RN__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RN__") == 0xEEu && regions_failed(REGIONS_STATUS(TYPE_MISMATCH)) == 1u &&
                          regions_host.access_count == 0u,
                      "an index register that is a Name fails with type_mismatch, touching nothing");
    regions_close(test);
}

/**
 * @brief The edges of what an access reaches: port 0xFFFF in a byte, but no double word from 0xFFFE;
 *        no page 0, as in ring 0; a reserved AccessType and a QWordAcc port fail with
 *        access_width_invalid; a unit of no bits reads 0 and writes nothing.
 */
KERNEL_TEST(the_edges_of_what_an_access_reaches)
{
    static const MadeUpFieldUnit_t last_port[] = {
        {"PFF_", 8u}
    };
    static const MadeUpFieldUnit_t past_port[] = {
        {"PFE_", 32u}
    };
    static const MadeUpFieldUnit_t page_zero[] = {
        {"P0__", 8u}
    };
    static const MadeUpFieldUnit_t reserved[] = {
        {"RSV_", 8u}
    };
    static const MadeUpFieldUnit_t quad_port[] = {
        {"QP__", 64u}
    };
    static const MadeUpFieldUnit_t empty[] = {
        {NULL,   3u},
        {"Z0__", 0u}
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, 0xFFFFu, 0x42u);
    made_up_aml_region(&regions_aml, "PFFR", 0x01u, 0xFFFFu, 1u);
    made_up_aml_field(&regions_aml, "PFFR", REGIONS_BYTE_ACCESS, last_port, 1u);
    made_up_aml_region(&regions_aml, "PFER", 0x01u, 0xFFFEu, 4u);
    made_up_aml_field(&regions_aml, "PFER", REGIONS_DWORD_ACCESS, past_port, 1u);
    made_up_aml_region(&regions_aml, "P0R_", 0x00u, 0u, 1u);
    made_up_aml_field(&regions_aml, "P0R_", REGIONS_BYTE_ACCESS, page_zero, 1u);
    made_up_aml_region(&regions_aml, "RSVR", 0x00u, REGIONS_MEMORY, 1u);
    made_up_aml_field(&regions_aml, "RSVR", 0x06u, reserved, 1u);
    made_up_aml_region(&regions_aml, "QPR_", 0x01u, 0x0B50u, 8u);
    made_up_aml_field(&regions_aml, "QPR_", REGIONS_QWORD_ACCESS, quad_port, 1u);
    made_up_aml_field(&regions_aml, "RSVR", REGIONS_BYTE_ACCESS, empty, 2u);
    made_up_aml_name_integer(&regions_aml, "RF__", 0u);
    made_up_aml_name_integer(&regions_aml, "RZ__", 0xEEu);
    made_up_aml_store_name(&regions_aml, "PFF_", "RF__");
    made_up_aml_store_name(&regions_aml, "PFE_", "RF__");
    made_up_aml_store_name(&regions_aml, "P0__", "RF__");
    made_up_aml_store_name(&regions_aml, "RSV_", "RF__");
    made_up_aml_store_name(&regions_aml, "QP__", "RF__");
    made_up_aml_store_name(&regions_aml, "Z0__", "RZ__");
    made_up_aml_store_integer(&regions_aml, 1u, "Z0__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RF__") == 0x42u && regions_failed(REGIONS_STATUS(ADDRESS_UNREACHABLE)) == 2u,
                      "port 0xFFFF reads 0x42; a double word from 0xFFFE and page 0 are unreachable");
    kernel_test_check(test, regions_failed(REGIONS_STATUS(ACCESS_WIDTH_INVALID)) == 2u,
                      "AccessType 6 and a QWordAcc port fail with access_width_invalid");
    kernel_test_check(test, regions_integer("RZ__") == 0u && regions_host.access_count == 1u,
                      "the unit of no bits reads 0 and writes nothing: the only access is port 0xFFFF");
    regions_close(test);
}

/**
 * @brief A datum at an address its width does not divide is made byte by byte, counted, each byte in
 *        the page that holds it: a double word at the last two bytes of a page and the first two of
 *        the next.
 */
KERNEL_TEST(an_unaligned_datum_is_made_byte_by_byte)
{
    static const uint8_t memory[] = {0x11u, 0x22u, 0x33u, 0x44u};
    static const MadeUpFieldUnit_t units[] = {
        {"UNA_", 32u}
    };

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY + 0xFFEu, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "UNAR", 0x00u, REGIONS_MEMORY + 0xFFEu, 4u);
    made_up_aml_field(&regions_aml, "UNAR", REGIONS_DWORD_ACCESS, units, 1u);
    made_up_aml_name_integer(&regions_aml, "RU__", 0u);
    made_up_aml_store_name(&regions_aml, "UNA_", "RU__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RU__") == 0x44332211u && regions_host.access_count == 4u &&
                          regions_access(0u, REGIONS_ACCESS(MEMORY_READ), 1u, REGIONS_MEMORY + 0xFFEu, 0x11u) &&
                          regions_access(3u, REGIONS_ACCESS(MEMORY_READ), 1u, REGIONS_MEMORY + 0x1001u, 0x44u),
                      "four byte reads give 0x44332211");
    kernel_test_check(test, regions_load.regions.unaligned_accesses == 1u && regions_load.regions.map_calls == 2u,
                      "one unaligned access, over two pages each mapped once");
    kernel_test_check(test, regions_load.evaluation.field_accesses == 1u,
                      "the unaligned datum counts as one field access, its four byte reads included");
    regions_close(test);
}

/**
 * @brief The node a declaration in method @p method created, called @p name, or 0 once the method has
 *        ended and its temporary nodes are gone.
 */
static uint32_t regions_method_child(const char *method, const char *name)
{
    return advanced_configuration_and_power_interface_machine_language_namespace_find_child(
        &regions_load.space, regions_node(method),
        advanced_configuration_and_power_interface_machine_language_name_segment((const uint8_t *) name, 0u));
}

/**
 * @brief A method creates its OperationRegion, at an offset it computes, and its Field at each call;
 *        both are temporary, so the second call creates them again and the method's end removes them
 *        (§5.1, §5.3).
 */
KERNEL_TEST(a_method_creates_its_region_and_field_at_each_call)
{
    static const uint8_t memory[] = {0x5Au, 0xA5u};
    static const uint8_t region_at_argument[] = {0x5Bu, 0x80u, 'M', 'R', '_', '_', 0x00u, 0x68u, 0x0Au, 0x02u};
    static const uint8_t return_sum[] = {0xA4u, 0x72u, 'M', 'F', '0', '_', 'M', 'F', '1', '_', 0x00u};
    static const uint8_t store_call[] = {0x70u, 'M', 'R', 'E', 'G', 0x0Cu, 0x40u, 0x00u, 0x01u, 0x00u};
    static const MadeUpFieldUnit_t units[] = {
        {"MF0_", 8u},
        {"MF1_", 8u}
    };
    MadeUpAml_t body;

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY + 0x40u, memory,
                                                                    sizeof(memory));
    made_up_aml_begin(&body);
    made_up_aml_raw(&body, region_at_argument, sizeof(region_at_argument));
    made_up_aml_field(&body, "MR__", REGIONS_BYTE_ACCESS, units, 2u);
    made_up_aml_raw(&body, return_sum, sizeof(return_sum));
    made_up_aml_method(&regions_aml, "MREG", 0x01u, &body);
    made_up_aml_name_integer(&regions_aml, "R1__", 0u);
    made_up_aml_name_integer(&regions_aml, "R2__", 0u);
    made_up_aml_raw(&regions_aml, store_call, sizeof(store_call));
    made_up_aml_name(&regions_aml, "R1__");
    made_up_aml_raw(&regions_aml, store_call, sizeof(store_call));
    made_up_aml_name(&regions_aml, "R2__");
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test, regions_integer("R1__") == 0xFFu && regions_integer("R2__") == 0xFFu,
                      "each call reads 0x5A and 0xA5 at the offset Arg0 gives, and returns their sum");
    kernel_test_check(test, regions_load.evaluation.failures == 0u && regions_load.regions.field_reads == 4u,
                      "both calls run, each reading the two units");
    kernel_test_check(test, regions_method_child("MREG", "MR__") == 0u && regions_method_child("MREG", "MF0_") == 0u,
                      "the region and its units are gone once the method ends");
    regions_close(test);
}

/**
 * @brief A method creates an IndexField over the table's index and data registers, and a BankField of
 *        the table's region behind the table's bank register; their units reach the hardware as the
 *        table's own would.
 */
KERNEL_TEST(a_method_creates_index_and_bank_fields_over_the_table_s_registers)
{
    static const uint8_t memory[] = {0x10u, 0x20u, 0x30u, 0x40u};
    static const MadeUpFieldUnit_t registers[] = {
        {"INDX", 8u},
        {"DATA", 8u}
    };
    static const MadeUpFieldUnit_t bank[] = {
        {"BSEL", 8u}
    };
    static const MadeUpFieldUnit_t indexed[] = {
        {NULL,   16u},
        {"U2__", 8u }
    };
    static const MadeUpFieldUnit_t banked[] = {
        {"BK0_", 8u},
        {"BK1_", 8u}
    };
    static const uint8_t return_indexed[] = {0xA4u, 'U', '2', '_', '_'};
    static const uint8_t return_banked[] = {0xA4u, 'B', 'K', '1', '_'};
    static const uint8_t store_calls[] = {0x70u, 'M', 'I', 'D', 'X', 'R', 'I', '_', '_',
                                          0x70u, 'M', 'B', 'N', 'K', 'R', 'B', '_', '_'};
    MadeUpAml_t body;

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_set_port(&regions_host, 0x0B01u, 0x5Au);
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "IDXR", 0x01u, 0x0B00u, 2u);
    made_up_aml_field(&regions_aml, "IDXR", REGIONS_BYTE_ACCESS, registers, 2u);
    made_up_aml_region(&regions_aml, "BNKR", 0x01u, 0x0B10u, 1u);
    made_up_aml_field(&regions_aml, "BNKR", REGIONS_BYTE_ACCESS, bank, 1u);
    made_up_aml_region(&regions_aml, "BRAM", 0x00u, REGIONS_MEMORY, sizeof(memory));
    made_up_aml_begin(&body);
    made_up_aml_index_field(&body, "INDX", "DATA", REGIONS_BYTE_ACCESS, indexed, 2u);
    made_up_aml_raw(&body, return_indexed, sizeof(return_indexed));
    made_up_aml_method(&regions_aml, "MIDX", 0x00u, &body);
    made_up_aml_begin(&body);
    made_up_aml_bank_field(&body, "BRAM", "BSEL", 2u, REGIONS_BYTE_ACCESS, banked, 2u);
    made_up_aml_raw(&body, return_banked, sizeof(return_banked));
    made_up_aml_method(&regions_aml, "MBNK", 0x00u, &body);
    made_up_aml_name_integer(&regions_aml, "RI__", 0u);
    made_up_aml_name_integer(&regions_aml, "RB__", 0u);
    made_up_aml_raw(&regions_aml, store_calls, sizeof(store_calls));
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test, regions_integer("RI__") == 0x5Au && regions_integer("RB__") == 0x20u,
                      "U2__ reads the data port, BK1_ reads 0x20 in bank 2");
    kernel_test_check(test,
                      regions_host.access_count == 4u &&
                          regions_access(0u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B00u, 2u) &&
                          regions_access(1u, REGIONS_ACCESS(PORT_READ), 1u, 0x0B01u, 0x5Au) &&
                          regions_access(2u, REGIONS_ACCESS(PORT_WRITE), 1u, 0x0B10u, 2u) &&
                          regions_access(3u, REGIONS_ACCESS(MEMORY_READ), 1u, REGIONS_MEMORY + 1u, 0x20u),
                      "index 2 then a data read; BankValue 2 then the datum");
    regions_close(test);
}

/**
 * @brief A field list in a method whose region resolves to nothing fails with name_not_found, and one
 *        with a Connection fails as unimplemented; neither leaves a unit behind.
 */
KERNEL_TEST(a_field_list_in_a_method_refuses_a_missing_region_and_a_connection)
{
    static const MadeUpFieldUnit_t units[] = {
        {"NU__", 8u}
    };
    static const uint8_t connection_field[] = {0x5Bu, 0x81u, 0x0Bu, 'R', 'A', 'M', 'R', 0x01u, 0x02u,
                                               'R',   'A',   'M',   'R', 'C', 'U', '_', 0x08u};
    static const uint8_t calls[] = {'M', 'N', 'O', 'R', 'M', 'C', 'O', 'N'};
    MadeUpAml_t body;

    regions_begin();
    made_up_aml_region(&regions_aml, "RAMR", 0x00u, REGIONS_MEMORY, 4u);
    made_up_aml_begin(&body);
    made_up_aml_field(&body, "NOPE", REGIONS_BYTE_ACCESS, units, 1u);
    made_up_aml_method(&regions_aml, "MNOR", 0x00u, &body);
    made_up_aml_begin(&body);
    made_up_aml_raw(&body, connection_field, sizeof(connection_field));
    made_up_aml_method(&regions_aml, "MCON", 0x00u, &body);
    made_up_aml_raw(&regions_aml, calls, sizeof(calls));
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_failed(REGIONS_STATUS(NAME_NOT_FOUND)) == 1u &&
                          regions_failed(REGIONS_STATUS(UNIMPLEMENTED_OPERATOR)) == 1u,
                      "the missing region fails with name_not_found, the Connection as unimplemented");
    kernel_test_check(test, regions_load.temporary_count == 0u, "no unit is left behind");
    regions_close(test);
}

/**
 * @brief An AccessAs in a field list a method declares sets the access width of the units after it: a
 *        32-bit unit after AccessAs (DWordAcc) of a ByteAcc Field is one 4-byte read.
 */
KERNEL_TEST(an_access_as_in_a_method_s_field_list_widens_the_units_after_it)
{
    static const uint8_t memory[] = {0x11u, 0x22u, 0x33u, 0x44u};
    static const uint8_t field[] = {0x5Bu, 0x81u, 0x0Bu, 'R', 'A', 'M', 'R', 0x01u,
                                    0x01u, 0x03u, 0x00u, 'D', 'W', '_', '_', 0x20u};
    static const uint8_t return_unit[] = {0xA4u, 'D', 'W', '_', '_'};
    static const uint8_t store_call[] = {0x70u, 'M', 'A', 'C', 'C', 'R', 'D', '_', '_'};
    MadeUpAml_t body;

    regions_begin();
    advanced_configuration_and_power_interface_simulated_host_place(&regions_host, REGIONS_MEMORY, memory,
                                                                    sizeof(memory));
    made_up_aml_region(&regions_aml, "RAMR", 0x00u, REGIONS_MEMORY, 4u);
    made_up_aml_begin(&body);
    made_up_aml_raw(&body, field, sizeof(field));
    made_up_aml_raw(&body, return_unit, sizeof(return_unit));
    made_up_aml_method(&regions_aml, "MACC", 0x00u, &body);
    made_up_aml_name_integer(&regions_aml, "RD__", 0u);
    made_up_aml_raw(&regions_aml, store_call, sizeof(store_call));
    if (!regions_run(test, 2u))
        return;
    kernel_test_check(test,
                      regions_integer("RD__") == 0x44332211u && regions_host.access_count == 1u &&
                          regions_access(0u, REGIONS_ACCESS(MEMORY_READ), 4u, REGIONS_MEMORY, 0x44332211u),
                      "DW__ is one 4-byte read of 0x44332211");
    regions_close(test);
}
