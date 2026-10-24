#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_madt, KERNEL_TEST_STAGE_INITIALIZATION);

#define MADE_UP_MADT_CAPACITY   8192u       /**< Bytes of the buffer each made-up MADT is written in. */
#define MADE_UP_MADT_HEADER     44u         /**< Bytes of a MADT before its first entry. */
#define MADE_UP_MADT_LOCAL_APIC 0xFEE00000u /**< The local APICs' base every made-up MADT gives. */
#define MADE_UP_MADT_ENABLED    0x1u        /**< The Enabled flag of a processor entry. */
#define MADE_UP_MADT_ONLINE     0x2u        /**< The Online Capable flag of a processor entry. */
#define MADE_UP_MADT_UNIQUE_IDENTIFIER_OFFSET                                                                          \
    0x100u                             /**< Added to an index to make a processor UID no APIC ID equals. */
#define MADE_UP_MADT_NOT_A_TABLE 0x7Fu /**< An entry type no specification defines. */

/**
 * @brief A MADT being written, entry after entry, into a buffer.
 */
typedef struct MadeUpMadt {
    uint8_t *bytes;  /**< The buffer. */
    uint32_t length; /**< Bytes written so far. */
} MadeUpMadt_t;

static uint8_t made_up_madt_buffer[MADE_UP_MADT_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceMadtState_t made_up_madt_state;

/**
 * @brief Stores @p value little-endian in @p width bytes at @p offset of the MADT.
 */
static void made_up_madt_put(MadeUpMadt_t *madt, uint32_t offset, uint32_t value, uint32_t width)
{
    for (uint32_t index = 0u; index < width; ++index)
        madt->bytes[offset + index] = (uint8_t) (value >> (8u * index));
}

/**
 * @brief Starts an empty MADT: its header and the local APICs' base, no entry yet.
 */
static MadeUpMadt_t made_up_madt_begin(void)
{
    MadeUpMadt_t madt = {made_up_madt_buffer, MADE_UP_MADT_HEADER};

    memset(made_up_madt_buffer, 0, sizeof(made_up_madt_buffer));
    memcpy(madt.bytes, "APIC", 4u);
    madt.bytes[8] = 5u;
    memcpy(madt.bytes + 10, "LPLTST", 6u);
    memcpy(madt.bytes + 16, "MADEUPMA", 8u);
    made_up_madt_put(&madt, 36u, MADE_UP_MADT_LOCAL_APIC, 4u);
    made_up_madt_put(&madt, 40u, 1u, 4u);
    return madt;
}

/**
 * @brief Appends an entry of @p type, @p length bytes long, whose body the caller fills.
 *
 * @return The offset of the entry's first byte.
 */
static uint32_t made_up_madt_entry(MadeUpMadt_t *madt, uint8_t type, uint8_t length)
{
    const uint32_t offset = madt->length;

    madt->bytes[offset] = type;
    madt->bytes[offset + 1u] = length;
    madt->length += length;
    return offset;
}

/**
 * @brief Appends a Processor Local APIC entry.
 */
static void made_up_madt_local_apic(MadeUpMadt_t *madt, uint8_t processor_unique_identifier, uint8_t apic_id,
                                    uint32_t flags)
{
    const uint32_t offset = made_up_madt_entry(madt, 0u, 8u);

    madt->bytes[offset + 2u] = processor_unique_identifier;
    madt->bytes[offset + 3u] = apic_id;
    made_up_madt_put(madt, offset + 4u, flags, 4u);
}

/**
 * @brief Appends a Processor Local x2APIC entry.
 */
static void made_up_madt_local_x2apic(MadeUpMadt_t *madt, uint32_t processor_unique_identifier, uint32_t x2apic_id,
                                      uint32_t flags)
{
    const uint32_t offset = made_up_madt_entry(madt, 9u, 16u);

    made_up_madt_put(madt, offset + 4u, x2apic_id, 4u);
    made_up_madt_put(madt, offset + 8u, flags, 4u);
    made_up_madt_put(madt, offset + 12u, processor_unique_identifier, 4u);
}

/**
 * @brief Appends a Local APIC NMI entry.
 */
static void made_up_madt_local_apic_non_maskable_interrupt(MadeUpMadt_t *madt, uint8_t processor_unique_identifier,
                                                           uint16_t flags, uint8_t local_interrupt)
{
    const uint32_t offset = made_up_madt_entry(madt, 4u, 6u);

    madt->bytes[offset + 2u] = processor_unique_identifier;
    made_up_madt_put(madt, offset + 3u, flags, 2u);
    madt->bytes[offset + 5u] = local_interrupt;
}

/**
 * @brief Appends a Local x2APIC NMI entry.
 */
static void made_up_madt_local_x2apic_non_maskable_interrupt(MadeUpMadt_t *madt, uint32_t processor_unique_identifier,
                                                             uint16_t flags, uint8_t local_interrupt)
{
    const uint32_t offset = made_up_madt_entry(madt, 0xAu, 12u);

    made_up_madt_put(madt, offset + 2u, flags, 2u);
    made_up_madt_put(madt, offset + 4u, processor_unique_identifier, 4u);
    madt->bytes[offset + 8u] = local_interrupt;
}

/**
 * @brief Appends an I/O APIC entry.
 */
static void made_up_madt_io_apic(MadeUpMadt_t *madt, uint8_t id, uint32_t address, uint32_t gsi_base)
{
    const uint32_t offset = made_up_madt_entry(madt, 1u, 12u);

    madt->bytes[offset + 2u] = id;
    made_up_madt_put(madt, offset + 4u, address, 4u);
    made_up_madt_put(madt, offset + 8u, gsi_base, 4u);
}

/**
 * @brief Appends an Interrupt Source Override entry.
 */
static void made_up_madt_override(MadeUpMadt_t *madt, uint8_t source, uint32_t gsi, uint16_t flags)
{
    const uint32_t offset = made_up_madt_entry(madt, 2u, 10u);

    madt->bytes[offset + 3u] = source;
    made_up_madt_put(madt, offset + 4u, gsi, 4u);
    made_up_madt_put(madt, offset + 8u, flags, 2u);
}

/**
 * @brief Writes the MADT's length, then the checksum byte that makes its bytes sum to zero.
 *
 * @return The MADT, as the reader takes it.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *made_up_madt_seal(MadeUpMadt_t *madt)
{
    uint8_t sum = 0u;

    made_up_madt_put(madt, 4u, madt->length, 4u);
    madt->bytes[9] = 0u;
    for (uint32_t index = 0u; index < madt->length; ++index)
        sum = (uint8_t) (sum + madt->bytes[index]);
    madt->bytes[9] = (uint8_t) (0u - sum);
    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) madt->bytes;
}

/**
 * @brief Whether the state's slot @p slot holds the processor of UID @p processor_unique_identifier and APIC ID
 *        @p apic_id.
 */
static bool made_up_madt_slot_is(uint32_t slot, uint32_t processor_unique_identifier, uint32_t apic_id)
{
    return slot < made_up_madt_state.processor_count &&
           made_up_madt_state.processors[slot].processor_unique_identifier == processor_unique_identifier &&
           made_up_madt_state.processors[slot].apic_id == apic_id;
}

/**
 * @brief Whether the last reading's state name is @p name.
 */
static bool made_up_madt_state_is(const char *name)
{
    const char *state_name = made_up_madt_state.state_name;

    return state_name != NULL && strlen(state_name) == strlen(name) && memcmp(state_name, name, strlen(name)) == 0;
}

/**
 * @brief A MADT that lists its processors only as local x2APIC entries, as three Intel clients of 2024
 *        do with 8 and 48 of them, gives each a CPU slot with its x2APIC ID and its UID.
 */
KERNEL_TEST(an_x2apic_only_table_gives_every_processor_a_slot)
{
    MadeUpMadt_t madt = made_up_madt_begin();
    uint32_t slots_as_listed = 0u;

    for (uint32_t index = 0u; index < 48u; ++index)
        made_up_madt_local_x2apic(&madt, MADE_UP_MADT_UNIQUE_IDENTIFIER_OFFSET + index, 2u * index,
                                  MADE_UP_MADT_ENABLED);

    const bool read =
        advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state);

    for (uint32_t index = 0u; index < 48u; ++index)
        slots_as_listed +=
            made_up_madt_slot_is(index, MADE_UP_MADT_UNIQUE_IDENTIFIER_OFFSET + index, 2u * index) ? 1u : 0u;

    kernel_test_check(test, read && made_up_madt_state.available, "the table is read to its end");
    kernel_test_check(test, made_up_madt_state.processor_count == 48u, "each of the 48 entries gets a CPU slot");
    kernel_test_check(test, slots_as_listed == 48u, "each slot holds its entry's x2APIC ID and UID, in table order");
    kernel_test_check(test, made_up_madt_state.skipped_count == 0u, "no entry is skipped");
    kernel_test_measure(test, "processors", made_up_madt_state.processor_count);
}

/**
 * @brief A server MADT of 224 local APICs keeps every one: no cap of 32 CPUs.
 */
KERNEL_TEST(a_table_of_224_processors_keeps_them_all)
{
    MadeUpMadt_t madt = made_up_madt_begin();

    for (uint32_t index = 0u; index < 224u; ++index)
        made_up_madt_local_apic(&madt, (uint8_t) (223u - index), (uint8_t) index, MADE_UP_MADT_ENABLED);

    const bool read =
        advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state);

    kernel_test_check(test, read && made_up_madt_state.processor_count == 224u, "the 224 processors get a slot each");
    kernel_test_check(test,
                      made_up_madt_slot_is(0u, 223u, 0u) && made_up_madt_slot_is(32u, 191u, 32u) &&
                          made_up_madt_slot_is(223u, 0u, 223u),
                      "the first, the 33rd and the last hold their own APIC ID and UID");
    kernel_test_check(test, made_up_madt_state.processors_dropped == 0u, "none is dropped");
    kernel_test_measure(test, "processors", made_up_madt_state.processor_count);
}

/**
 * @brief Enabled processors get a slot; Online Capable, disabled and invalid entries are counted
 *        without one; types not read are skipped by their length; the I/O APIC and the override past
 *        them are still read.
 */
KERNEL_TEST(enabled_disabled_and_online_capable_entries_are_told_apart)
{
    MadeUpMadt_t madt = made_up_madt_begin();

    made_up_madt_local_apic(&madt, 10u, 1u, MADE_UP_MADT_ENABLED);
    made_up_madt_local_apic(&madt, 11u, 2u, 0u);
    made_up_madt_local_apic(&madt, 12u, 3u, MADE_UP_MADT_ONLINE);
    made_up_madt_local_apic(&madt, 13u, 0xFFu, MADE_UP_MADT_ENABLED);
    (void) made_up_madt_entry(&madt, MADE_UP_MADT_NOT_A_TABLE, 6u);
    (void) made_up_madt_entry(&madt, 0x10u, 16u);
    (void) made_up_madt_entry(&madt, 0x18u, 20u);
    made_up_madt_local_x2apic(&madt, 0x200u, 0x100u, MADE_UP_MADT_ENABLED);
    made_up_madt_local_x2apic(&madt, 0x201u, 0x101u, MADE_UP_MADT_ONLINE);
    made_up_madt_local_x2apic(&madt, 0x202u, 0x20u, MADE_UP_MADT_ENABLED);
    made_up_madt_local_x2apic(&madt, 0x203u, 0xFFFFFFFFu, MADE_UP_MADT_ENABLED);
    made_up_madt_io_apic(&madt, 4u, 0xFEC00000u, 0u);
    made_up_madt_override(&madt, 0u, 2u, 0u);

    const bool read =
        advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state);

    kernel_test_check(test, read && made_up_madt_state.available, "the table is read to its end");
    kernel_test_check(test, made_up_madt_state.processor_count == 2u, "only the two enabled processors get a slot");
    kernel_test_check(test, made_up_madt_slot_is(0u, 10u, 1u) && made_up_madt_slot_is(1u, 0x200u, 0x100u),
                      "the local APIC and the local x2APIC, each with its UID");
    kernel_test_check(test, made_up_madt_state.online_capable_count == 2u,
                      "the two Online Capable entries are counted");
    kernel_test_check(test, made_up_madt_state.disabled_count == 1u, "the entry with both flags clear is counted");
    kernel_test_check(test, made_up_madt_state.invalid_count == 3u,
                      "APIC ID 0xFF, x2APIC ID 0xFFFFFFFF, and an x2APIC ID below 0xFF beside local APICs name no "
                      "processor");
    kernel_test_check(test, made_up_madt_state.skipped_count == 3u,
                      "an unknown type, the Multiprocessor Wakeup structure and a RISC-V type are skipped");
    kernel_test_check(test,
                      made_up_madt_state.io_apic_count == 1u && made_up_madt_state.io_apics[0].id == 4u &&
                          made_up_madt_state.interrupt_source_override_count == 1u &&
                          made_up_madt_state.interrupt_source_overrides[0].global_system_interrupt == 2u,
                      "the I/O APIC and the override after them are read");
    kernel_test_measure(test, "online_capable", made_up_madt_state.online_capable_count);
    kernel_test_measure(test, "disabled", made_up_madt_state.disabled_count);
    kernel_test_measure(test, "invalid", made_up_madt_state.invalid_count);
    kernel_test_measure(test, "skipped", made_up_madt_state.skipped_count);
}

/**
 * @brief An x2APIC ID below 0xFF names a processor when no local APIC entry does.
 */
KERNEL_TEST(a_low_x2apic_id_counts_when_no_local_apic_names_a_processor)
{
    MadeUpMadt_t madt = made_up_madt_begin();

    made_up_madt_local_apic(&madt, 1u, 0xFFu, MADE_UP_MADT_ENABLED);
    made_up_madt_local_apic(&madt, 2u, 7u, 0u);
    made_up_madt_local_x2apic(&madt, 0x300u, 0x10u, MADE_UP_MADT_ENABLED);

    const bool read =
        advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state);

    kernel_test_check(test, read && made_up_madt_slot_is(0u, 0x300u, 0x10u) && made_up_madt_state.processor_count == 1u,
                      "the x2APIC entry gets the only slot");
}

/**
 * @brief NMI entries name their processor, or every processor, with their input and flags.
 */
KERNEL_TEST(non_maskable_interrupt_entries_name_their_processor_and_input)
{
    MadeUpMadt_t madt = made_up_madt_begin();

    made_up_madt_local_apic(&madt, 3u, 0u, MADE_UP_MADT_ENABLED);
    made_up_madt_local_apic_non_maskable_interrupt(&madt, 0xFFu, 0x5u, 1u);
    made_up_madt_local_apic_non_maskable_interrupt(&madt, 3u, 0x0u, 0u);
    made_up_madt_local_x2apic_non_maskable_interrupt(&madt, 0x200u, 0xDu, 1u);
    made_up_madt_local_x2apic_non_maskable_interrupt(&madt, 0xFFFFFFFFu, 0x0u, 1u);

    const bool read =
        advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state);
    const AdvancedConfigurationAndPowerInterfaceProcessorNonMaskableInterrupt_t *connections =
        made_up_madt_state.non_maskable_interrupts;

    kernel_test_check(test, read && made_up_madt_state.non_maskable_interrupt_count == 4u,
                      "the four NMI connections are kept");
    kernel_test_check(test,
                      connections[0].processor_unique_identifier ==
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ALL_PROCESSORS &&
                          connections[0].flags == 0x5u && connections[0].local_interrupt == 1u,
                      "a local APIC NMI for UID 0xFF applies to every processor");
    kernel_test_check(test, connections[1].processor_unique_identifier == 3u && connections[1].local_interrupt == 0u,
                      "a local APIC NMI names its processor's UID and LINT0");
    kernel_test_check(test,
                      connections[2].processor_unique_identifier == 0x200u && connections[2].flags == 0xDu &&
                          connections[2].local_interrupt == 1u,
                      "a local x2APIC NMI names its 32-bit UID, its flags and LINT1");
    kernel_test_check(
        test, connections[3].processor_unique_identifier == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ALL_PROCESSORS,
        "a local x2APIC NMI for UID 0xFFFFFFFF applies to every processor");
    kernel_test_check(test, made_up_madt_state.processor_count == 1u, "an NMI entry gives no CPU slot");
}

/**
 * @brief A MADT whose checksum fails is read all the same, and recorded as failing: refusing it
 *        would leave the machine with one CPU.
 */
KERNEL_TEST(a_table_whose_checksum_fails_is_read_and_recorded)
{
    MadeUpMadt_t madt = made_up_madt_begin();

    made_up_madt_local_apic(&madt, 0u, 0u, MADE_UP_MADT_ENABLED);
    made_up_madt_local_apic(&madt, 1u, 1u, MADE_UP_MADT_ENABLED);

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *table = made_up_madt_seal(&madt);

    madt.bytes[16] ^= 0x01u;

    const bool read = advanced_configuration_and_power_interface_read_madt(table, &made_up_madt_state);

    kernel_test_check(test, read && made_up_madt_state.available, "the table is read");
    kernel_test_check(test, !made_up_madt_state.checksum_valid, "its failing checksum is recorded");
    kernel_test_check(test, made_up_madt_state.processor_count == 2u, "both processors get a slot");
}

/**
 * @brief Processors past the capacity are counted, not written past the slots.
 */
KERNEL_TEST(processors_past_the_capacity_are_counted)
{
    MadeUpMadt_t madt = made_up_madt_begin();
    const uint32_t listed = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT + 44u;

    for (uint32_t index = 0u; index < listed; ++index)
        made_up_madt_local_x2apic(&madt, index, 0x100u + index, MADE_UP_MADT_ENABLED);

    const bool read =
        advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state);

    kernel_test_check(test,
                      read && made_up_madt_state.processor_count ==
                                  ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT,
                      "every slot is filled");
    kernel_test_check(test, made_up_madt_state.processors_dropped == 44u, "the 44 processors past them are counted");
    kernel_test_measure(test, "processors_dropped", made_up_madt_state.processors_dropped);
}

/**
 * @brief A table too short for a MADT header, an entry shorter than two bytes, and an entry that runs
 *        past the table are refused, each with its own state name.
 */
KERNEL_TEST(a_malformed_table_is_refused_and_says_why)
{
    MadeUpMadt_t madt = made_up_madt_begin();

    madt.length = MADE_UP_MADT_HEADER - 4u;
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state) &&
            made_up_madt_state_is("acpi-madt-too-short"),
        "a table shorter than a MADT header is refused as too short");

    madt = made_up_madt_begin();
    made_up_madt_local_apic(&madt, 0u, 0u, MADE_UP_MADT_ENABLED);
    (void) made_up_madt_entry(&madt, 0u, 1u);
    madt.length += 1u;
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state) &&
            made_up_madt_state_is("acpi-madt-entry-length-invalid") && made_up_madt_state.processor_count == 1u,
        "an entry of one byte is refused, and what came before it is kept");

    madt = made_up_madt_begin();
    made_up_madt_local_apic(&madt, 0u, 0u, MADE_UP_MADT_ENABLED);
    madt.length -= 1u;
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_read_madt(made_up_madt_seal(&madt), &made_up_madt_state) &&
            made_up_madt_state_is("acpi-madt-entry-out-of-bounds"),
        "an entry that runs past the table's end is refused");
}
