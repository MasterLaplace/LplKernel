#include <kernel/cpu/mtrr.h>
#include <kernel/testing/test.h>

KERNEL_TEST_SUITE(memory_type_range_registers, KERNEL_TEST_STAGE_INITIALIZATION);

#define MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED                                                                       \
    MEMORY_TYPE_RANGE_REGISTERS_ENABLED /**< E, in a made-up default type. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_FIXED_ENABLED                                                                 \
    MEMORY_TYPE_RANGE_REGISTERS_FIXED_ENABLED /**< FE, in a made-up default type. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE     MEMORY_TYPE_RANGE_REGISTERS_UNCACHEABLE /**< UC. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_COMBINING 0x01u                                   /**< WC, Table 14-8. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_THROUGH   MEMORY_TYPE_RANGE_REGISTERS_WRITE_THROUGH /**< WT. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_PROTECTED 0x05u                                  /**< WP, Table 14-8. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK      MEMORY_TYPE_RANGE_REGISTERS_WRITE_BACK /**< WB. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_ADDRESS_LIMIT                                                                 \
    (1ull << 36) /**< A made-up MAXPHYADDR of 36 bits, the SDM's default. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE 0x40000000ull /**< 1 GiB. */
#define MEMORY_TYPE_RANGE_REGISTERS_TEST_SEABIOS_PCI                                                                   \
    0xC0000000ull /**< Where SeaBIOS starts the uncached range on q35. */

/**
 * @brief A variable range of @p size bytes, a power of two, from @p base, of memory type @p type, enabled or not.
 */
static MemoryTypeRangeRegistersRange_t memory_type_range_registers_test_range(uint64_t base, uint64_t size,
                                                                              uint32_t type, bool valid)
{
    const uint64_t mask = ~(size - 1u) & (MEMORY_TYPE_RANGE_REGISTERS_TEST_ADDRESS_LIMIT - 1u);

    return (MemoryTypeRangeRegistersRange_t){base | type,
                                             mask | (valid ? MEMORY_TYPE_RANGE_REGISTERS_RANGE_VALID : 0u)};
}

/**
 * @brief Registers with default type @p default_type, flags included, and the @p count ranges of @p ranges.
 */
static MemoryTypeRangeRegisters_t
memory_type_range_registers_test_registers(uint64_t default_type, const MemoryTypeRangeRegistersRange_t *ranges,
                                           uint32_t count)
{
    MemoryTypeRangeRegisters_t registers = {.default_type = default_type, .range_count = count};

    for (uint32_t index = 0u; index < count && index < MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY; ++index)
        registers.ranges[index] = ranges[index];
    return registers;
}

/**
 * @brief Disabled registers make no page write-back, whatever their default type and ranges say: §14.11.4.1 applies
 *        UC to all of physical memory while E is clear.
 */
KERNEL_TEST(disabled_registers_make_no_page_write_back)
{
    const MemoryTypeRangeRegistersRange_t ranges[] = {memory_type_range_registers_test_range(
        0u, 4u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE, MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, true)};
    const MemoryTypeRangeRegisters_t disabled =
        memory_type_range_registers_test_registers(MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, ranges, 1u);
    const MemoryTypeRangeRegisters_t absent = memory_type_range_registers_test_registers(0u, NULL, 0u);

    kernel_test_check(test, !memory_type_range_registers_make_write_back(&disabled, 0x10000000u),
                      "a write-back default and a write-back range make nothing write-back without E");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&absent, 0x10000000u),
                      "nor do the registers of a processor without them");
}

/**
 * @brief The default type decides a page no enabled range holds, as SeaBIOS leaves it: write-back by default, an
 *        uncached range from the start of PCI memory to 4 GiB.
 */
KERNEL_TEST(the_default_type_decides_a_page_no_range_holds)
{
    const MemoryTypeRangeRegistersRange_t seabios[] = {memory_type_range_registers_test_range(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_SEABIOS_PCI, MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
        MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, true)};
    const MemoryTypeRangeRegisters_t write_back = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, NULL, 0u);
    const MemoryTypeRangeRegisters_t uncached = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, NULL, 0u);
    const MemoryTypeRangeRegisters_t combining = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_COMBINING, NULL, 0u);
    const MemoryTypeRangeRegisters_t q35 = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, seabios, 1u);

    kernel_test_check(test, memory_type_range_registers_make_write_back(&write_back, 0x0FFE0000u),
                      "a write-back default makes a page no range holds write-back");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&uncached, 0x0FFE0000u),
                      "an uncached default does not");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&combining, 0x0FFE0000u),
                      "nor does a write-combining one");
    kernel_test_check(test,
                      memory_type_range_registers_make_write_back(&q35, 0x0FFE0000u) &&
                          memory_type_range_registers_make_write_back(&q35, 0xBFFFF000u),
                      "SeaBIOS's registers make its tables and the page below PCI memory write-back");
    kernel_test_check(test,
                      !memory_type_range_registers_make_write_back(&q35, 0xC0000000u) &&
                          !memory_type_range_registers_make_write_back(&q35, 0xFFFFF000u),
                      "and the first and the last page of PCI memory uncached");
}

/**
 * @brief A page that ranges hold is write-back only when every one of them says write-back: §14.11.4.1 makes UC win
 *        over any type, WT win over WB, and leaves any other overlap undefined.
 */
KERNEL_TEST(a_page_is_write_back_only_when_every_range_that_holds_it_says_write_back)
{
    const MemoryTypeRangeRegistersRange_t ranges[] = {
        memory_type_range_registers_test_range(0u, 2u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, true),
        memory_type_range_registers_test_range(0u, MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, true),
        memory_type_range_registers_test_range(MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE, 0x10000000u,
                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, true),
        memory_type_range_registers_test_range(0x60000000u, 0x10000000u, MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_THROUGH,
                                               true),
        memory_type_range_registers_test_range(0x70000000u, 0x10000000u,
                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_PROTECTED, true),
        memory_type_range_registers_test_range(2u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, true)};
    const MemoryTypeRangeRegisters_t registers = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, ranges,
        sizeof(ranges) / sizeof(ranges[0]));

    kernel_test_check(test, memory_type_range_registers_make_write_back(&registers, 0x20000000u),
                      "two write-back ranges over a page make it write-back");
    kernel_test_check(test, memory_type_range_registers_make_write_back(&registers, 0x50000000u),
                      "one write-back range over a page under an uncached default makes it write-back");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&registers, 0x40000000u),
                      "an uncached range over a write-back one makes the page uncached");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&registers, 0x6FFFF000u),
                      "a write-through range over a write-back one makes it write-through");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&registers, 0x70000000u),
                      "a write-protected range over a write-back one, an undefined overlap, is not write-back");
    kernel_test_check(test,
                      memory_type_range_registers_make_write_back(&registers, 0x80000000u) &&
                          memory_type_range_registers_make_write_back(&registers, 0xBFFFF000u),
                      "a range holds its first and its last page");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&registers, 0xC0000000u),
                      "and not the page past it, where the uncached default decides");
}

/**
 * @brief A range holds a page by its mask over every address bit, is ignored when disabled, and the offset of an
 *        address within its page plays no part.
 *
 * @details An uncached range of 4 GiB at 4 GiB has a mask whose low 32 bits are clear: compared on 32 bits, it would
 *          hold every page below 4 GiB.
 */
KERNEL_TEST(a_range_holds_a_page_by_its_mask_over_every_address_bit)
{
    const MemoryTypeRangeRegistersRange_t above[] = {memory_type_range_registers_test_range(
        4u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE, 4u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
        MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, true)};
    const MemoryTypeRangeRegistersRange_t disabled[] = {memory_type_range_registers_test_range(
        0u, 4u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE, MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, false)};
    const MemoryTypeRangeRegistersRange_t seabios[] = {memory_type_range_registers_test_range(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_SEABIOS_PCI, MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
        MEMORY_TYPE_RANGE_REGISTERS_TEST_UNCACHEABLE, true)};
    const MemoryTypeRangeRegisters_t high = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, above, 1u);
    const MemoryTypeRangeRegisters_t off = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, disabled, 1u);
    const MemoryTypeRangeRegisters_t q35 = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, seabios, 1u);

    kernel_test_check(test,
                      memory_type_range_registers_make_write_back(&high, 0x10000000u) &&
                          !memory_type_range_registers_make_write_back(&high, 0x100000000ull),
                      "an uncached range at 4 GiB holds no page below it, and holds 4 GiB");
    kernel_test_check(test, memory_type_range_registers_make_write_back(&off, 0x10000000u),
                      "a disabled uncached range holds nothing");
    kernel_test_check(test,
                      memory_type_range_registers_make_write_back(&q35, 0xBFFFFFFFu) &&
                          !memory_type_range_registers_make_write_back(&q35, 0xC0000FFFu),
                      "the last byte of a page answers for its page");
}

/**
 * @brief A page of the first MiB is not write-back by the snapshot while the fixed ranges are enabled, since they
 *        decide it and the snapshot does not hold them; with them disabled, the variable ranges decide it as any
 *        other page (§14.11.2.1).
 */
KERNEL_TEST(the_first_mebibyte_is_not_write_back_while_the_fixed_ranges_decide_it)
{
    const MemoryTypeRangeRegisters_t fixed = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_FIXED_ENABLED |
            MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK,
        NULL, 0u);
    const MemoryTypeRangeRegisters_t variable = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, NULL, 0u);

    kernel_test_check(test,
                      !memory_type_range_registers_make_write_back(&fixed, 0x000E0000u) &&
                          !memory_type_range_registers_make_write_back(&fixed, 0x000FF000u),
                      "the fixed ranges enabled, no page of the first MiB is write-back by the snapshot");
    kernel_test_check(test, memory_type_range_registers_make_write_back(&fixed, 0x00100000u),
                      "the first page past it is");
    kernel_test_check(test, memory_type_range_registers_make_write_back(&variable, 0x000E0000u),
                      "the fixed ranges disabled, the default decides the first MiB too");
}

/**
 * @brief Enabled ranges past what a snapshot holds make no page write-back, since one of them could say uncached.
 */
KERNEL_TEST(ranges_past_the_capacity_make_no_page_write_back)
{
    MemoryTypeRangeRegistersRange_t ranges[MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY];

    for (uint32_t index = 0u; index < MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY; ++index)
        ranges[index] = memory_type_range_registers_test_range(0u, 4u * MEMORY_TYPE_RANGE_REGISTERS_TEST_GIBIBYTE,
                                                               MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, true);

    const MemoryTypeRangeRegisters_t full = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, ranges,
        MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY);
    const MemoryTypeRangeRegisters_t past = memory_type_range_registers_test_registers(
        MEMORY_TYPE_RANGE_REGISTERS_TEST_ENABLED | MEMORY_TYPE_RANGE_REGISTERS_TEST_WRITE_BACK, ranges,
        MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY + 1u);

    kernel_test_check(test, memory_type_range_registers_make_write_back(&full, 0x10000000u),
                      "as many write-back ranges as the snapshot holds make a page write-back");
    kernel_test_check(test, !memory_type_range_registers_make_write_back(&past, 0x10000000u),
                      "one more, which it does not hold, makes no page write-back");
}
