#include <kernel/acpi/tables.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/ioapic.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stddef.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface, KERNEL_TEST_STAGE_INITIALIZATION);

#define ACPI_TEST_HEADER_BYTES    36u   /**< Bytes of a table header, and of a table that is nothing more. */
#define ACPI_TEST_MASTER_PIC_MASK 0x21u /**< The master 8259's interrupt mask register. */
#define ACPI_TEST_SLAVE_PIC_MASK  0xA1u /**< The slave 8259's interrupt mask register. */
#define ACPI_TEST_PIC_LINES       16u   /**< Lines the two 8259s hold. */
#define ACPI_TEST_SCI_EN          0x1u  /**< SCI_EN, bit 0 of a PM1 control register. */

/**
 * @brief Whether 8259 line @p line is masked.
 */
static bool acpi_test_pic_line_is_masked(uint16_t line)
{
    const AdvancedConfigurationAndPowerInterfaceHost_t *host = &advanced_configuration_and_power_interface_machine_host;
    const uint16_t port = (line < 8u) ? ACPI_TEST_MASTER_PIC_MASK : ACPI_TEST_SLAVE_PIC_MASK;

    return ((host->port_read(host->context, port, 1u) >> (line % 8u)) & 1u) != 0u;
}

/**
 * @brief Whether every I/O APIC route the kernel programmed for @p global_system_interrupt is masked.
 */
static bool acpi_test_io_apic_routes_are_masked(uint32_t global_system_interrupt)
{
    for (uint8_t route = 0u;
         route < input_output_advanced_programmable_interrupt_controller_get_programmed_route_count(); ++route)
        if (input_output_advanced_programmable_interrupt_controller_get_programmed_route_gsi(route) ==
                global_system_interrupt &&
            !input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(route))
            return false;
    return true;
}

/**
 * @brief SCI_EN, read from the machine's PM1 control registers.
 */
static bool
acpi_test_system_control_interrupt_enabled(const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt)
{
    const AdvancedConfigurationAndPowerInterfaceHost_t *host = &advanced_configuration_and_power_interface_machine_host;
    uint32_t control = 0u;

    for (uint32_t index = 0u; index < 2u; ++index)
    {
        const uint16_t port = advanced_configuration_and_power_interface_input_output_port(
            &fadt->power_management_control_blocks[index], 2u);

        if (port != 0u)
            control |= host->port_read(host->context, port, 2u);
    }
    return (control & ACPI_TEST_SCI_EN) != 0u;
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

/**
 * @brief The boot put the platform in ACPI mode before any AML ran: SCI_EN reads set in the machine's
 *        PM1 control registers, and the SCI stays masked in the 8259 and in every I/O APIC route.
 */
KERNEL_TEST(the_boot_puts_the_platform_in_acpi_mode_with_its_interrupt_masked)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt =
        &advanced_configuration_and_power_interface_directory()->fixed_description;
    const AdvancedConfigurationAndPowerInterfaceModeSwitch_t *mode = advanced_configuration_and_power_interface_mode();
    const uint16_t interrupt = fadt->system_control_interrupt;
    uint32_t global_system_interrupt = interrupt;
    uint16_t flags = 0u;

    if (!fadt->present || fadt->hardware_reduced)
    {
        kernel_test_skip(test, "the firmware gives no FADT, or a hardware-reduced one, which boots in ACPI mode");
        return;
    }
    if (interrupt < ACPI_TEST_PIC_LINES)
        (void) advanced_configuration_and_power_interface_madt_resolve_isa_irq((uint8_t) interrupt,
                                                                               &global_system_interrupt, &flags);

    kernel_test_check(test,
                      mode->outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_SWITCHED ||
                          mode->outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_ALREADY,
                      "the boot switched the platform to ACPI mode, or found it there");
    kernel_test_check(test, acpi_test_system_control_interrupt_enabled(fadt),
                      "SCI_EN reads set in the machine's PM1 control registers");
    kernel_test_check(test, interrupt >= ACPI_TEST_PIC_LINES || acpi_test_pic_line_is_masked(interrupt),
                      "the SCI's 8259 line is masked");
    kernel_test_check(test, acpi_test_io_apic_routes_are_masked(global_system_interrupt),
                      "no I/O APIC route of the SCI is unmasked");
    kernel_test_measure(test, "outcome", mode->outcome);
    kernel_test_measure(test, "polls", mode->polls);
    kernel_test_measure(test, "waited_ms", mode->waited_milliseconds);
    kernel_test_measure(test, "sci_int", interrupt);
}

/**
 * @brief The machine host's sleep waits at least a whole tick for 1 ms, and says how many
 *        milliseconds it waited, no more than the ticks that passed around it.
 */
KERNEL_TEST(the_machine_host_sleeps_on_the_tick_and_says_how_long)
{
    const AdvancedConfigurationAndPowerInterfaceHost_t *host = &advanced_configuration_and_power_interface_machine_host;
    const uint32_t hertz = clock_get_tick_hz();
    const uint32_t before = clock_get_tick_count();
    const uint32_t waited = host->sleep_milliseconds(host->context, 1u);
    const uint32_t ticks = clock_get_tick_count() - before;

    if (hertz == 0u)
    {
        kernel_test_skip(test, "no tick runs");
        return;
    }
    kernel_test_check(test, ticks >= 2u, "a sleep of 1 ms lets a whole tick pass, the one it lands in not counted");
    kernel_test_check(test, (uint64_t) waited * hertz >= 1000u,
                      "it says it waited at least as many milliseconds as a tick lasts");
    kernel_test_check(test, (uint64_t) waited * hertz <= (uint64_t) ticks * 1000u,
                      "it says no more than the ticks that passed around it");
}
