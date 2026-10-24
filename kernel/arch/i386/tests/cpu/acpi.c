#include <kernel/acpi/tables.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stddef.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface, KERNEL_TEST_STAGE_INITIALIZATION);

#define ACPI_TEST_HEADER_BYTES 36u /**< Bytes of a table header, and of a table that is nothing more. */

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
    kernel_test_check(test, dsdt != NULL && dsdt->length > ACPI_TEST_HEADER_BYTES,
                      "the DSDT the FADT names is found, with code after its header");
    kernel_test_check(test, in_windows == directory->table_count,
                      "every table is mapped through a firmware window, wherever the firmware put it");
    kernel_test_check(test, bad_checksums == 0u, "every table's checksum holds");
    kernel_test_measure(test, "rsdp_revision", directory->rsdp_revision);
    kernel_test_measure(test, "tables", directory->table_count);
    kernel_test_measure(test, "unreachable", directory->unreachable_count);
    kernel_test_measure(test, "dsdt_bytes", dsdt != NULL ? dsdt->length : 0u);
}

/**
 * @brief The machine host maps a firmware range below 4 GiB, the one that ends on its last byte
 *        included, and refuses one that starts or ends past it.
 *
 * @details Each page of a window used to be computed in 32 bits: a range that crossed 4 GiB came back
 *          mapped, its second page on physical page 0.
 */
KERNEL_TEST(a_firmware_range_past_4_gib_is_refused)
{
    const AdvancedConfigurationAndPowerInterfaceHost_t *host = &advanced_configuration_and_power_interface_machine_host;
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory =
        advanced_configuration_and_power_interface_directory();

    if (!directory->walked)
    {
        kernel_test_skip(test, "the firmware gives no root table");
        return;
    }

    const uint64_t root = directory->root_physical_address;
    const void *below = host->map_firmware(host->context, root, ACPI_TEST_HEADER_BYTES);
    const void *ends_on_the_last_byte = host->map_firmware(host->context, 0xFFFFFFDCull, ACPI_TEST_HEADER_BYTES);
    const void *starts_past = host->map_firmware(host->context, 0x100000000ull + root, ACPI_TEST_HEADER_BYTES);
    const void *crosses_on_a_page = host->map_firmware(host->context, 0xFFFFF000ull, 0x2000ull);
    const void *crosses_inside_a_page = host->map_firmware(host->context, 0xFFFFFFF0ull, ACPI_TEST_HEADER_BYTES);

    if (below != NULL)
        host->unmap_firmware(host->context, below, ACPI_TEST_HEADER_BYTES);
    if (ends_on_the_last_byte != NULL)
        host->unmap_firmware(host->context, ends_on_the_last_byte, ACPI_TEST_HEADER_BYTES);
    if (starts_past != NULL)
        host->unmap_firmware(host->context, starts_past, ACPI_TEST_HEADER_BYTES);
    if (crosses_on_a_page != NULL)
        host->unmap_firmware(host->context, crosses_on_a_page, 0x2000ull);
    if (crosses_inside_a_page != NULL)
        host->unmap_firmware(host->context, crosses_inside_a_page, ACPI_TEST_HEADER_BYTES);

    kernel_test_check(test, below != NULL, "the root table below 4 GiB is mapped");
    kernel_test_check(test, ends_on_the_last_byte != NULL, "36 bytes ending on 0xFFFFFFFF are mapped");
    kernel_test_check(test, starts_past == NULL, "a range that starts past 4 GiB is refused");
    kernel_test_check(test, crosses_on_a_page == NULL, "two pages from 0xFFFFF000 are refused");
    kernel_test_check(test, crosses_inside_a_page == NULL, "36 bytes from 0xFFFFFFF0 are refused");
}
