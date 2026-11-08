#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_memory_mapped_configuration,
                  KERNEL_TEST_STAGE_INITIALIZATION);

#define MADE_UP_MCFG_CAPACITY   1024u        /**< Bytes of the buffer each made-up MCFG is written in. */
#define MADE_UP_MCFG_HEADER     44u          /**< Bytes of an MCFG before its first allocation. */
#define MADE_UP_MCFG_ALLOCATION 16u          /**< Bytes of one allocation. */
#define MADE_UP_MCFG_BASE       0xE0000000u  /**< The base most machines of the corpus give. */
#define MADE_UP_MCFG_HIGH_BASE  0x400000000u /**< A base above 4 GiB, as a 64-bit platform may give one. */

/**
 * @brief An MCFG being written, allocation after allocation, into a buffer.
 */
typedef struct MadeUpMcfg {
    uint8_t *bytes;  /**< The buffer. */
    uint32_t length; /**< Bytes written so far. */
} MadeUpMcfg_t;

static uint8_t made_up_mcfg_buffer[MADE_UP_MCFG_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceMemoryMappedConfigurationState_t made_up_mcfg_state;

/**
 * @brief Stores @p value little-endian in @p width bytes at @p offset of the MCFG.
 */
static void made_up_mcfg_put(MadeUpMcfg_t *mcfg, uint32_t offset, uint64_t value, uint32_t width)
{
    for (uint32_t index = 0u; index < width; ++index)
        mcfg->bytes[offset + index] = (uint8_t) (value >> (8u * index));
}

/**
 * @brief Starts an empty MCFG: its header and its reserved bytes, no allocation yet.
 */
static MadeUpMcfg_t made_up_mcfg_begin(void)
{
    MadeUpMcfg_t mcfg = {made_up_mcfg_buffer, MADE_UP_MCFG_HEADER};

    memset(made_up_mcfg_buffer, 0, sizeof(made_up_mcfg_buffer));
    memcpy(mcfg.bytes, "MCFG", 4u);
    mcfg.bytes[8] = 1u;
    memcpy(mcfg.bytes + 10, "LPLTST", 6u);
    memcpy(mcfg.bytes + 16, "MADEUPMC", 8u);
    return mcfg;
}

/**
 * @brief Appends an allocation: the base of bus 0's window, the segment group, and the buses it decodes.
 */
static void made_up_mcfg_allocation(MadeUpMcfg_t *mcfg, uint64_t base_address, uint16_t segment_group,
                                    uint8_t start_bus, uint8_t end_bus)
{
    const uint32_t offset = mcfg->length;

    made_up_mcfg_put(mcfg, offset, base_address, 8u);
    made_up_mcfg_put(mcfg, offset + 8u, segment_group, 2u);
    mcfg->bytes[offset + 10u] = start_bus;
    mcfg->bytes[offset + 11u] = end_bus;
    mcfg->length += MADE_UP_MCFG_ALLOCATION;
}

/**
 * @brief Writes the MCFG's length, then the checksum byte that makes its bytes sum to zero.
 *
 * @return The MCFG, as the reader takes it.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *made_up_mcfg_seal(MadeUpMcfg_t *mcfg)
{
    uint8_t sum = 0u;

    made_up_mcfg_put(mcfg, 4u, mcfg->length, 4u);
    mcfg->bytes[9] = 0u;
    for (uint32_t index = 0u; index < mcfg->length; ++index)
        sum = (uint8_t) (sum + mcfg->bytes[index]);
    mcfg->bytes[9] = (uint8_t) (0u - sum);
    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) mcfg->bytes;
}

/**
 * @brief Whether the state's window @p index holds @p base_address, @p segment_group and the buses @p start_bus to
 *        @p end_bus.
 */
static bool made_up_mcfg_window_is(uint32_t index, uint64_t base_address, uint16_t segment_group, uint8_t start_bus,
                                   uint8_t end_bus)
{
    const AdvancedConfigurationAndPowerInterfaceConfigurationSpaceAllocation_t *window =
        &made_up_mcfg_state.allocations[index];

    return index < made_up_mcfg_state.allocation_count && window->base_address == base_address &&
           window->segment_group == segment_group && window->start_bus == start_bus && window->end_bus == end_bus;
}

/**
 * @brief Whether the last reading's state name is @p name.
 */
static bool made_up_mcfg_state_is(const char *name)
{
    const char *state_name = made_up_mcfg_state.state_name;

    return state_name != NULL && strlen(state_name) == strlen(name) && memcmp(state_name, name, strlen(name)) == 0;
}

/**
 * @brief The MCFG of most machines of the corpus, one window over the 256 buses of segment group 0, is kept as it
 *        is written.
 */
KERNEL_TEST(one_window_over_every_bus_is_kept_as_written)
{
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, 0u, 0u, 255u);

    const bool read = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        made_up_mcfg_seal(&mcfg), &made_up_mcfg_state);

    kernel_test_check(test, read && made_up_mcfg_state.available, "the table is read");
    kernel_test_check(test, made_up_mcfg_state.checksum_valid, "its checksum holds");
    kernel_test_check(test, made_up_mcfg_state.allocation_count == 1u, "it holds one window");
    kernel_test_check(test, made_up_mcfg_window_is(0u, MADE_UP_MCFG_BASE, 0u, 0u, 255u),
                      "the window keeps its base, its segment group and its buses");
    kernel_test_check(test,
                      made_up_mcfg_state.invalid_count == 0u && made_up_mcfg_state.allocations_dropped == 0u &&
                          made_up_mcfg_state.trailing_bytes == 0u,
                      "nothing is counted invalid, dropped or trailing");
    kernel_test_check(test, made_up_mcfg_state_is("acpi-mcfg-ready"), "the state says the table is ready");
    kernel_test_measure(test, "windows", made_up_mcfg_state.allocation_count);
}

/**
 * @brief Windows of several segment groups, one starting past bus 0 and one above 4 GiB, keep their order and
 *        their fields whole.
 */
KERNEL_TEST(windows_keep_their_order_their_start_bus_and_a_base_above_4_gib)
{
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, 0u, 0u, 63u);
    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_HIGH_BASE, 1u, 0x40u, 0x7Fu);
    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE + 0x10000000u, 2u, 7u, 7u);

    const bool read = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        made_up_mcfg_seal(&mcfg), &made_up_mcfg_state);

    kernel_test_check(test, read && made_up_mcfg_state.allocation_count == 3u, "the three windows are kept");
    kernel_test_check(test, made_up_mcfg_window_is(0u, MADE_UP_MCFG_BASE, 0u, 0u, 63u),
                      "the first keeps buses 0 to 63 of group 0");
    kernel_test_check(test, made_up_mcfg_window_is(1u, MADE_UP_MCFG_HIGH_BASE, 1u, 0x40u, 0x7Fu),
                      "the second keeps its base above 4 GiB whole and its start bus 0x40");
    kernel_test_check(test, made_up_mcfg_window_is(2u, MADE_UP_MCFG_BASE + 0x10000000u, 2u, 7u, 7u),
                      "the third keeps its single bus");
    kernel_test_measure(test, "windows", made_up_mcfg_state.allocation_count);
}

/**
 * @brief An allocation whose base is 0 or whose end bus is below its start bus is counted invalid and dropped, and
 *        the valid ones around it are kept.
 */
KERNEL_TEST(a_zero_base_or_a_reversed_bus_range_is_counted_invalid)
{
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    made_up_mcfg_allocation(&mcfg, 0u, 0u, 0u, 255u);
    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, 0u, 0u, 127u);
    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, 1u, 0x20u, 0x1Fu);

    const bool read = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        made_up_mcfg_seal(&mcfg), &made_up_mcfg_state);

    kernel_test_check(test, read && made_up_mcfg_state.available, "the table is read");
    kernel_test_check(test, made_up_mcfg_state.invalid_count == 2u, "the zero base and the reversed range are invalid");
    kernel_test_check(
        test, made_up_mcfg_state.allocation_count == 1u && made_up_mcfg_window_is(0u, MADE_UP_MCFG_BASE, 0u, 0u, 127u),
        "only the valid window is kept");
    kernel_test_measure(test, "invalid", made_up_mcfg_state.invalid_count);
}

/**
 * @brief Past the capacity, valid windows are counted, and the first ones listed are those kept.
 */
KERNEL_TEST(windows_past_the_capacity_are_counted)
{
    const uint32_t listed = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_CONFIGURATION_SPACE_ALLOCATION_COUNT + 2u;
    const uint32_t last_kept = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_CONFIGURATION_SPACE_ALLOCATION_COUNT - 1u;
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    for (uint32_t index = 0u; index < listed; ++index)
        made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, (uint16_t) index, 0u, 255u);

    const bool read = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        made_up_mcfg_seal(&mcfg), &made_up_mcfg_state);

    kernel_test_check(test,
                      read && made_up_mcfg_state.allocation_count ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_CONFIGURATION_SPACE_ALLOCATION_COUNT,
                      "the capacity's worth is kept");
    kernel_test_check(test, made_up_mcfg_state.allocations_dropped == 2u, "the two past it are counted");
    kernel_test_check(test, made_up_mcfg_window_is(last_kept, MADE_UP_MCFG_BASE, (uint16_t) last_kept, 0u, 255u),
                      "the last kept is the last that fits, in table order");
    kernel_test_measure(test, "dropped", made_up_mcfg_state.allocations_dropped);
}

/**
 * @brief Bytes past the last whole allocation are counted, and the whole ones before them are read.
 */
KERNEL_TEST(bytes_past_the_last_whole_allocation_are_counted)
{
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, 0u, 0u, 255u);
    mcfg.length += 8u;

    const bool read = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        made_up_mcfg_seal(&mcfg), &made_up_mcfg_state);

    kernel_test_check(test, read && made_up_mcfg_state.allocation_count == 1u, "the whole allocation is read");
    kernel_test_check(test, made_up_mcfg_state.trailing_bytes == 8u, "the 8 bytes past it are counted");
    kernel_test_measure(test, "trailing", made_up_mcfg_state.trailing_bytes);
}

/**
 * @brief A table whose checksum fails is read all the same, and the failure recorded.
 */
KERNEL_TEST(a_failing_checksum_is_recorded_and_the_table_read)
{
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    made_up_mcfg_allocation(&mcfg, MADE_UP_MCFG_BASE, 0u, 0u, 255u);
    made_up_mcfg_seal(&mcfg);
    ++mcfg.bytes[9];

    const bool read = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) mcfg.bytes, &made_up_mcfg_state);

    kernel_test_check(test, read && made_up_mcfg_state.allocation_count == 1u, "the table is read");
    kernel_test_check(test, !made_up_mcfg_state.checksum_valid, "the failing checksum is recorded");
}

/**
 * @brief No table, and a table too short for its reserved bytes, are refused with two different states.
 */
KERNEL_TEST(no_table_and_a_short_table_are_two_refusals)
{
    MadeUpMcfg_t mcfg = made_up_mcfg_begin();

    const bool absent =
        advanced_configuration_and_power_interface_read_memory_mapped_configuration(NULL, &made_up_mcfg_state);
    const bool absent_named = made_up_mcfg_state_is("acpi-mcfg-not-found");

    mcfg.length = MADE_UP_MCFG_HEADER - 1u;

    const bool short_table = advanced_configuration_and_power_interface_read_memory_mapped_configuration(
        made_up_mcfg_seal(&mcfg), &made_up_mcfg_state);

    kernel_test_check(test, !absent && absent_named, "no table is refused as not found");
    kernel_test_check(test, !short_table && made_up_mcfg_state_is("acpi-mcfg-too-short"),
                      "a table that ends inside its reserved bytes is refused as too short");
    kernel_test_check(test, !made_up_mcfg_state.available && made_up_mcfg_state.allocation_count == 0u,
                      "a refused table gives no window");
}
