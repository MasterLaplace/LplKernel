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
