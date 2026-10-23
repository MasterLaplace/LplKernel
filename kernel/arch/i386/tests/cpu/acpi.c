#include <kernel/cpu/acpi.h>
#include <kernel/cpu/paging.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @name Where each table of the made-up firmware sits in its page
 * @{
 */
#define MADE_UP_FIRMWARE_RSDP_OFFSET       0x000u /**< The root system description pointer. */
#define MADE_UP_FIRMWARE_XSDT_OFFSET       0x040u /**< The XSDT. */
#define MADE_UP_FIRMWARE_RSDT_OFFSET       0x100u /**< The RSDT. */
#define MADE_UP_FIRMWARE_BOTH_OFFSET       0x200u /**< "TSTA", which both root tables list. */
#define MADE_UP_FIRMWARE_EXTENDED_OFFSET   0x240u /**< "TSTB", which only the XSDT lists. */
#define MADE_UP_FIRMWARE_CORRUPT_OFFSET    0x280u /**< "TSTC", whose checksum fails. */
#define MADE_UP_FIRMWARE_FADT_OFFSET       0x300u /**< The FADT the XSDT lists: 244 bytes, with X_DSDT. */
#define MADE_UP_FIRMWARE_SHORT_FADT_OFFSET 0x400u /**< The FADT the RSDT lists: the 116 bytes of ACPI 1.0. */
#define MADE_UP_FIRMWARE_DSDT_32_OFFSET    0x500u /**< The DSDT both FADTs name in their 32-bit field. */
#define MADE_UP_FIRMWARE_DSDT_64_OFFSET    0x600u /**< The DSDT X_DSDT names. */
#define MADE_UP_FIRMWARE_FACS_32_OFFSET    0x700u /**< Where FIRMWARE_CTRL puts a FACS, when a test names one. */
#define MADE_UP_FIRMWARE_FACS_64_OFFSET    0x740u /**< Where X_FIRMWARE_CTRL puts a FACS, when a test names one. */
/** @} */

#define MADE_UP_FIRMWARE_HEADER_BYTES                   36u /**< Bytes of a table header, and of a table that is nothing more. */
#define MADE_UP_FIRMWARE_FADT_BYTES                     244u /**< Bytes of a FADT of ACPI 6. */
#define MADE_UP_FIRMWARE_SHORT_FADT_BYTES               116u /**< Bytes of a FADT of ACPI 1.0, which ends before X_DSDT. */
#define MADE_UP_FIRMWARE_ABOVE_4_GIB                    0x100000000ull /**< Added to an address, puts it above 4 GiB. */
#define MADE_UP_FIRMWARE_FADT_FIRMWARE_CONTROL          36u            /**< Offset of a FADT's FIRMWARE_CTRL field. */
#define MADE_UP_FIRMWARE_FADT_DSDT                      40u            /**< Offset of a FADT's 32-bit DSDT field. */
#define MADE_UP_FIRMWARE_FADT_EXTENDED_FIRMWARE_CONTROL 132u           /**< Offset of a FADT's X_FIRMWARE_CTRL field. */
#define MADE_UP_FIRMWARE_FADT_EXTENDED_DSDT             140u           /**< Offset of a FADT's X_DSDT field. */
#define MADE_UP_FIRMWARE_RSDP_VERSION_1                 20u /**< Bytes the first checksum of a root pointer covers. */

/**
 * @brief A made-up firmware: every table in one page, so each lies in physically contiguous memory.
 */
typedef struct MadeUpFirmware {
    uint8_t *page;     /**< The page, as the kernel addresses it. */
    uint32_t physical; /**< Its physical address. */
} MadeUpFirmware_t;

static AdvancedConfigurationAndPowerInterfaceDirectory_t made_up_firmware_directory;

/**
 * @brief Sets the byte at @p checksum_offset so that the @p length bytes at @p bytes sum to zero.
 */
static void made_up_firmware_seal(uint8_t *bytes, uint32_t length, uint32_t checksum_offset)
{
    uint8_t sum = 0u;

    bytes[checksum_offset] = 0u;
    for (uint32_t index = 0u; index < length; ++index)
        sum = (uint8_t) (sum + bytes[index]);
    bytes[checksum_offset] = (uint8_t) (0u - sum);
}

/**
 * @brief Writes a table header at @p offset of the firmware's page.
 *
 * @return The header, whose body the caller fills before made_up_firmware_seal_table.
 */
static AdvancedConfigurationAndPowerInterfaceSdtHeader_t *made_up_firmware_table(const MadeUpFirmware_t *firmware,
                                                                                 uint32_t offset, const char *signature,
                                                                                 uint32_t length,
                                                                                 const char *oem_table_id)
{
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        (AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (firmware->page + offset);

    memcpy(header->signature, signature, sizeof(header->signature));
    header->length = length;
    header->revision = 1u;
    memcpy(header->oem_table_id, oem_table_id, sizeof(header->oem_table_id));
    return header;
}

/**
 * @brief Sets the checksum of the table at @p offset of the firmware's page, once its body is written.
 */
static void made_up_firmware_seal_table(const MadeUpFirmware_t *firmware, uint32_t offset)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (firmware->page + offset);

    made_up_firmware_seal(firmware->page + offset, header->length,
                          offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, checksum));
}

/**
 * @brief Writes a FADT at @p offset, @p length bytes long, naming the DSDT in its 32-bit field and,
 *        when it is long enough, @p extended_dsdt in X_DSDT.
 */
static void made_up_firmware_write_fadt(const MadeUpFirmware_t *firmware, uint32_t offset, uint32_t length,
                                        uint64_t extended_dsdt)
{
    const uint32_t dsdt = firmware->physical + MADE_UP_FIRMWARE_DSDT_32_OFFSET;

    made_up_firmware_table(firmware, offset, "FACP", length, "LPLFADT ");
    memcpy(firmware->page + offset + MADE_UP_FIRMWARE_FADT_DSDT, &dsdt, sizeof(dsdt));
    if (length >= MADE_UP_FIRMWARE_FADT_EXTENDED_DSDT + sizeof(extended_dsdt))
        memcpy(firmware->page + offset + MADE_UP_FIRMWARE_FADT_EXTENDED_DSDT, &extended_dsdt, sizeof(extended_dsdt));
    made_up_firmware_seal_table(firmware, offset);
}

/**
 * @brief Names a FACS in the long FADT's FIRMWARE_CTRL and X_FIRMWARE_CTRL fields, then seals the
 *        FADT again.
 */
static void made_up_firmware_name_facs(const MadeUpFirmware_t *firmware, uint32_t firmware_control,
                                       uint64_t extended_firmware_control)
{
    uint8_t *fadt = firmware->page + MADE_UP_FIRMWARE_FADT_OFFSET;

    memcpy(fadt + MADE_UP_FIRMWARE_FADT_FIRMWARE_CONTROL, &firmware_control, sizeof(firmware_control));
    memcpy(fadt + MADE_UP_FIRMWARE_FADT_EXTENDED_FIRMWARE_CONTROL, &extended_firmware_control,
           sizeof(extended_firmware_control));
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_FADT_OFFSET);
}

/**
 * @brief Writes the made-up firmware into its page.
 *
 * @details The XSDT lists TSTA, TSTB, TSTC, TSTA again above 4 GiB, and the long FADT; the RSDT
 *          lists TSTA and the short FADT. Both FADTs name the DSDT "THIRTY2" in their 32-bit field;
 *          the long one names @p extended_dsdt in X_DSDT.
 *
 * @param rsdp_revision The root pointer's revision: 0 for ACPI 1.0, 2 for a pointer that names the XSDT.
 * @param extended_dsdt What the long FADT's X_DSDT holds.
 */
static void made_up_firmware_write(const MadeUpFirmware_t *firmware, uint8_t rsdp_revision, uint64_t extended_dsdt)
{
    const uint32_t base = firmware->physical;
    const uint64_t extended_entries[] = {
        base + MADE_UP_FIRMWARE_BOTH_OFFSET,    base + MADE_UP_FIRMWARE_EXTENDED_OFFSET,
        base + MADE_UP_FIRMWARE_CORRUPT_OFFSET, MADE_UP_FIRMWARE_ABOVE_4_GIB + base + MADE_UP_FIRMWARE_BOTH_OFFSET,
        base + MADE_UP_FIRMWARE_FADT_OFFSET,
    };
    const uint32_t entries[] = {base + MADE_UP_FIRMWARE_BOTH_OFFSET, base + MADE_UP_FIRMWARE_SHORT_FADT_OFFSET};

    memset(firmware->page, 0, PAGE_SIZE);

    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_BOTH_OFFSET, "TSTA", MADE_UP_FIRMWARE_HEADER_BYTES, "BOTH    ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_BOTH_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET, "TSTB", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "EXTENDED");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_CORRUPT_OFFSET, "TSTC", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "CORRUPT ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_CORRUPT_OFFSET);
    firmware->page[MADE_UP_FIRMWARE_CORRUPT_OFFSET +
                   offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;

    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_DSDT_32_OFFSET, "DSDT", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "THIRTY2 ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_DSDT_32_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_DSDT_64_OFFSET, "DSDT", MADE_UP_FIRMWARE_HEADER_BYTES,
                           "SIXTY4  ");
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    made_up_firmware_write_fadt(firmware, MADE_UP_FIRMWARE_FADT_OFFSET, MADE_UP_FIRMWARE_FADT_BYTES, extended_dsdt);
    made_up_firmware_write_fadt(firmware, MADE_UP_FIRMWARE_SHORT_FADT_OFFSET, MADE_UP_FIRMWARE_SHORT_FADT_BYTES, 0u);

    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_XSDT_OFFSET, "XSDT",
                           MADE_UP_FIRMWARE_HEADER_BYTES + sizeof(extended_entries), "ROOT64  ");
    memcpy(firmware->page + MADE_UP_FIRMWARE_XSDT_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES, extended_entries,
           sizeof(extended_entries));
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_XSDT_OFFSET);
    made_up_firmware_table(firmware, MADE_UP_FIRMWARE_RSDT_OFFSET, "RSDT",
                           MADE_UP_FIRMWARE_HEADER_BYTES + sizeof(entries), "ROOT32  ");
    memcpy(firmware->page + MADE_UP_FIRMWARE_RSDT_OFFSET + MADE_UP_FIRMWARE_HEADER_BYTES, entries, sizeof(entries));
    made_up_firmware_seal_table(firmware, MADE_UP_FIRMWARE_RSDT_OFFSET);

    AdvancedConfigurationAndPowerInterfaceRsdp_t *rsdp =
        (AdvancedConfigurationAndPowerInterfaceRsdp_t *) (firmware->page + MADE_UP_FIRMWARE_RSDP_OFFSET);

    memcpy(rsdp->signature, "RSD PTR ", sizeof(rsdp->signature));
    memcpy(rsdp->oem_id, "LPLTST", sizeof(rsdp->oem_id));
    rsdp->revision = rsdp_revision;
    rsdp->rsdt_address = base + MADE_UP_FIRMWARE_RSDT_OFFSET;
    rsdp->length = sizeof(*rsdp);
    rsdp->xsdt_address = base + MADE_UP_FIRMWARE_XSDT_OFFSET;
    made_up_firmware_seal((uint8_t *) rsdp, MADE_UP_FIRMWARE_RSDP_VERSION_1,
                          offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, checksum));
    made_up_firmware_seal((uint8_t *) rsdp, sizeof(*rsdp),
                          offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, extended_checksum));
}

/**
 * @brief Takes a page for a made-up firmware.
 *
 * @return false when no page could be taken, after the test is skipped.
 */
static bool made_up_firmware_open(KernelTest_t *test, MadeUpFirmware_t *firmware)
{
    firmware->page = (uint8_t *) kernel_vmm_alloc_pages(1u);
    if (firmware->page != NULL &&
        paging_get_physical_address((uint32_t) (uintptr_t) firmware->page, &firmware->physical))
        return true;

    if (firmware->page != NULL)
        kernel_vmm_free_pages(firmware->page, 1u);
    kernel_test_skip(test, "no page for the made-up firmware");
    return false;
}

/**
 * @brief Unmaps what the directory read, and gives the firmware's page back.
 */
static void made_up_firmware_close(MadeUpFirmware_t *firmware)
{
    advanced_configuration_and_power_interface_release_directory(&made_up_firmware_directory);
    kernel_vmm_free_pages(firmware->page, 1u);
}

/**
 * @brief Whether the directory's DSDT is the one whose OEM table identifier is @p oem_table_id.
 */
static bool made_up_firmware_dsdt_is(const char *oem_table_id)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *dsdt =
        advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "DSDT");

    return dsdt != NULL && memcmp(dsdt->oem_table_id, oem_table_id, sizeof(dsdt->oem_table_id)) == 0;
}

/**
 * @brief Whether the directory records the table at @p header as one whose checksum fails.
 */
static bool made_up_firmware_checksum_fails(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    for (uint32_t index = 0u; index < made_up_firmware_directory.table_count; ++index)
    {
        if (made_up_firmware_directory.tables[index].header == header)
            return !made_up_firmware_directory.tables[index].checksum_valid;
    }
    return false;
}

/**
 * @brief A root pointer of revision 2 that names an XSDT has the XSDT walked: its 64-bit entries,
 *        and X_DSDT for the DSDT.
 */
KERNEL_TEST(the_extended_root_is_walked_when_the_pointer_names_one)
{
    MadeUpFirmware_t firmware;

    if (!made_up_firmware_open(test, &firmware))
        return;
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    const bool walked = advanced_configuration_and_power_interface_read_directory(
        firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET, &made_up_firmware_directory);
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *corrupt =
        advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTC");

    kernel_test_check(test, walked && made_up_firmware_directory.walked, "the directory is read");
    kernel_test_check(test,
                      made_up_firmware_directory.extended_root && made_up_firmware_directory.root_physical_address ==
                                                                      firmware.physical + MADE_UP_FIRMWARE_XSDT_OFFSET,
                      "the XSDT is the root walked");
    kernel_test_check(
        test, advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTB") != NULL,
        "a table only the XSDT lists is found");
    kernel_test_check(test, corrupt != NULL && made_up_firmware_checksum_fails(corrupt),
                      "a table whose checksum fails is given out, recorded as failing");
    kernel_test_check(test, made_up_firmware_directory.unreachable_count == 1u,
                      "the entry above 4 GiB is counted unreachable, not truncated");
    kernel_test_check(test, made_up_firmware_directory.table_count == 5u,
                      "three tables, the FADT and the DSDT are recorded, and nothing else");
    kernel_test_check(test, made_up_firmware_dsdt_is("SIXTY4  "), "the DSDT is the one X_DSDT names");
    made_up_firmware_close(&firmware);
}

/**
 * @brief A root pointer of ACPI 1.0 has the RSDT walked, and the 32-bit DSDT of its FADT taken.
 */
KERNEL_TEST(an_acpi_1_pointer_walks_the_root_of_32_bit_entries)
{
    MadeUpFirmware_t firmware;

    if (!made_up_firmware_open(test, &firmware))
        return;
    made_up_firmware_write(&firmware, 0u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    const bool walked = advanced_configuration_and_power_interface_read_directory(
        firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET, &made_up_firmware_directory);

    kernel_test_check(test, walked, "the directory is read");
    kernel_test_check(test,
                      !made_up_firmware_directory.extended_root && made_up_firmware_directory.root_physical_address ==
                                                                       firmware.physical + MADE_UP_FIRMWARE_RSDT_OFFSET,
                      "the RSDT is the root walked, although the pointer's XSDT field is filled");
    kernel_test_check(
        test, !advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTB"),
        "a table only the XSDT lists is not found");
    kernel_test_check(test, made_up_firmware_directory.table_count == 3u,
                      "one table, the FADT and the DSDT are recorded");
    kernel_test_check(test, made_up_firmware_dsdt_is("THIRTY2 "), "the DSDT is the one a FADT of ACPI 1.0 names");
    made_up_firmware_close(&firmware);
}

/**
 * @brief An XSDT whose checksum fails gives way to the RSDT, and a pointer whose own checksum fails
 *        is refused.
 */
KERNEL_TEST(a_root_whose_checksum_fails_is_not_walked)
{
    MadeUpFirmware_t firmware;

    if (!made_up_firmware_open(test, &firmware))
        return;
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    firmware.page[MADE_UP_FIRMWARE_XSDT_OFFSET +
                  offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;

    const bool walked = advanced_configuration_and_power_interface_read_directory(
        firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET, &made_up_firmware_directory);

    kernel_test_check(test, walked && !made_up_firmware_directory.extended_root, "the RSDT is walked instead");
    kernel_test_check(
        test, !advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTB"),
        "nothing the XSDT lists is taken from it");
    advanced_configuration_and_power_interface_release_directory(&made_up_firmware_directory);

    firmware.page[MADE_UP_FIRMWARE_RSDP_OFFSET + offsetof(AdvancedConfigurationAndPowerInterfaceRsdp_t, oem_id)] ^=
        0x01u;
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_read_directory(
                          firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET, &made_up_firmware_directory) &&
                          made_up_firmware_directory.table_count == 0u,
                      "a root pointer whose checksum fails is refused, and nothing is recorded");
    made_up_firmware_close(&firmware);
}

/**
 * @brief An X_DSDT above 4 GiB gives way to the 32-bit DSDT, which this kernel can map.
 */
KERNEL_TEST(a_dsdt_above_4_gib_gives_way_to_the_32_bit_one)
{
    MadeUpFirmware_t firmware;

    if (!made_up_firmware_open(test, &firmware))
        return;
    made_up_firmware_write(&firmware, 2u,
                           MADE_UP_FIRMWARE_ABOVE_4_GIB + firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);

    const bool walked = advanced_configuration_and_power_interface_read_directory(
        firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET, &made_up_firmware_directory);

    kernel_test_check(test, walked && made_up_firmware_directory.extended_root, "the XSDT is walked");
    kernel_test_check(test, made_up_firmware_dsdt_is("THIRTY2 "), "the DSDT is the one the 32-bit field names");
    made_up_firmware_close(&firmware);
}

/**
 * @brief Of two tables with one signature, the one whose checksum holds is given out, although the
 *        one whose checksum fails comes first.
 */
KERNEL_TEST(a_table_whose_checksum_holds_wins_over_one_that_fails)
{
    MadeUpFirmware_t firmware;

    if (!made_up_firmware_open(test, &firmware))
        return;
    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    firmware.page[MADE_UP_FIRMWARE_BOTH_OFFSET +
                  offsetof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t, oem_table_id)] ^= 0x01u;
    memcpy(firmware.page + MADE_UP_FIRMWARE_EXTENDED_OFFSET, "TSTA", 4u);
    made_up_firmware_seal_table(&firmware, MADE_UP_FIRMWARE_EXTENDED_OFFSET);

    const bool walked = advanced_configuration_and_power_interface_read_directory(
        firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET, &made_up_firmware_directory);
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *table =
        advanced_configuration_and_power_interface_find_in_directory(&made_up_firmware_directory, "TSTA");

    kernel_test_check(test, walked, "the directory is read");
    kernel_test_check(test, table != NULL && memcmp(table->oem_table_id, "EXTENDED", sizeof(table->oem_table_id)) == 0,
                      "the second TSTA, whose checksum holds, is the one given out");
    made_up_firmware_close(&firmware);
}

/**
 * @brief The FACS is the one FIRMWARE_CTRL names when it is not 0, and the one X_FIRMWARE_CTRL names
 *        otherwise, as ACPICA and uACPI choose.
 */
KERNEL_TEST(the_facs_is_the_one_the_32_bit_field_names_when_it_is_set)
{
    MadeUpFirmware_t firmware;

    if (!made_up_firmware_open(test, &firmware))
        return;

    const uint32_t rsdp = firmware.physical + MADE_UP_FIRMWARE_RSDP_OFFSET;
    const uint32_t facs_32 = firmware.physical + MADE_UP_FIRMWARE_FACS_32_OFFSET;
    const uint32_t facs_64 = firmware.physical + MADE_UP_FIRMWARE_FACS_64_OFFSET;

    made_up_firmware_write(&firmware, 2u, firmware.physical + MADE_UP_FIRMWARE_DSDT_64_OFFSET);
    made_up_firmware_name_facs(&firmware, facs_32, facs_64);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_read_directory(rsdp, &made_up_firmware_directory) &&
                          made_up_firmware_directory.firmware_control_physical_address == facs_32,
                      "with both fields set, the 32-bit one is taken");
    advanced_configuration_and_power_interface_release_directory(&made_up_firmware_directory);

    made_up_firmware_name_facs(&firmware, 0u, facs_64);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_read_directory(rsdp, &made_up_firmware_directory) &&
                          made_up_firmware_directory.firmware_control_physical_address == facs_64,
                      "with the 32-bit field at 0, X_FIRMWARE_CTRL is taken");
    advanced_configuration_and_power_interface_release_directory(&made_up_firmware_directory);

    made_up_firmware_name_facs(&firmware, 0u, MADE_UP_FIRMWARE_ABOVE_4_GIB + facs_64);
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_read_directory(rsdp, &made_up_firmware_directory) &&
                          made_up_firmware_directory.firmware_control_physical_address == 0u,
                      "an X_FIRMWARE_CTRL above 4 GiB, with the 32-bit field at 0, gives no FACS");
    made_up_firmware_close(&firmware);
}

/**
 * @brief The firmware's own tables are walked, the DSDT included, each through a firmware window.
 */
KERNEL_TEST(the_firmware_tables_are_found_through_windows)
{
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory =
        advanced_configuration_and_power_interface_directory();

    if (!directory->walked)
    {
        kernel_test_skip(test, "the firmware gives no root table");
        return;
    }

    uint32_t in_windows = 0u;
    uint32_t bad_checksums = 0u;

    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        const uint32_t address = (uint32_t) (uintptr_t) directory->tables[index].header;

        if (address >= KERNEL_VMM_DYNAMIC_START && address < KERNEL_VMM_DYNAMIC_END)
            ++in_windows;
        if (!directory->tables[index].checksum_valid)
            ++bad_checksums;
    }

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *dsdt =
        advanced_configuration_and_power_interface_find_table("DSDT");

    kernel_test_check(test, advanced_configuration_and_power_interface_find_table("FACP") != NULL, "the FADT is found");
    kernel_test_check(test, advanced_configuration_and_power_interface_find_table("APIC") != NULL, "the MADT is found");
    kernel_test_check(test, dsdt != NULL && dsdt->length > MADE_UP_FIRMWARE_HEADER_BYTES,
                      "the DSDT the FADT names is found, with code after its header");
    kernel_test_check(test, in_windows == directory->table_count,
                      "every table is mapped through a firmware window, wherever the firmware put it");
    kernel_test_check(test, bad_checksums == 0u, "every table's checksum holds");
    kernel_test_measure(test, "rsdp_revision", directory->rsdp_revision);
    kernel_test_measure(test, "tables", directory->table_count);
    kernel_test_measure(test, "unreachable", directory->unreachable_count);
    kernel_test_measure(test, "dsdt_bytes", dsdt != NULL ? dsdt->length : 0u);
}
