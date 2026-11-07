#include <kernel/acpi/tables.h>
#include <kernel/cpu/mtrr.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stdint.h>

KERNEL_TEST_SUITE(peripheral_component_interconnect_express, KERNEL_TEST_STAGE_BOOTED);

#define PCI_EXPRESS_TEST_STATUS_CAPABILITIES  0x10u       /**< Status bit: the function has a capability list. */
#define PCI_EXPRESS_TEST_CAPABILITIES_POINTER 0x34u       /**< The offset of the first capability's offset. */
#define PCI_EXPRESS_TEST_INTERRUPT_LINE       0x3Cu       /**< A byte software may write, as the BIOS does. */
#define PCI_EXPRESS_TEST_CAPABILITY_EXPRESS   0x10u       /**< The PCI Express capability's identifier. */
#define PCI_EXPRESS_TEST_MAXIMUM_CAPABILITIES 48u         /**< Capabilities the first 256 bytes can hold. */
#define PCI_EXPRESS_TEST_ABSENT               0xFFFFFFFFu /**< What an absent register reads. */

/**
 * @brief Whether the boot's MCFG gives a window over the bus of @p device, so that its ECAM is reachable.
 */
static bool pci_express_test_reaches(const PeripheralComponentInterconnectDevice_t *device)
{
    return peripheral_component_interconnect_express_reaches(0u, device->bus);
}

/**
 * @brief Whether the capability list of @p device, read through configuration mechanism #1, holds the PCI Express
 *        capability.
 */
static bool pci_express_test_is_express(const PeripheralComponentInterconnectDevice_t *device)
{
    const uint16_t status =
        peripheral_component_interconnect_config_read_word(device->bus, device->device, device->function, 0x06u);
    uint8_t offset = 0u;

    if ((status & PCI_EXPRESS_TEST_STATUS_CAPABILITIES) == 0u)
        return false;
    offset = (uint8_t) (peripheral_component_interconnect_config_read_byte(
                            device->bus, device->device, device->function, PCI_EXPRESS_TEST_CAPABILITIES_POINTER) &
                        0xFCu);
    for (uint32_t step = 0u; offset != 0u && step < PCI_EXPRESS_TEST_MAXIMUM_CAPABILITIES; ++step)
    {
        const uint16_t header =
            peripheral_component_interconnect_config_read_word(device->bus, device->device, device->function, offset);

        if ((header & 0xFFu) == PCI_EXPRESS_TEST_CAPABILITY_EXPRESS)
            return true;
        offset = (uint8_t) ((header >> 8u) & 0xFCu);
    }
    return false;
}

/**
 * @brief The first scanned function whose bus a window covers, or NULL.
 */
static const PeripheralComponentInterconnectDevice_t *pci_express_test_first_reached(void)
{
    for (uint32_t index = 0u; index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (pci_express_test_reaches(device))
            return device;
    }
    return NULL;
}

/**
 * @brief Every function's first 256 bytes read the same through ECAM as through configuration mechanism #1: the two
 *        mechanisms address the same registers, which a wrong bus, device, function or offset shift would break.
 */
KERNEL_TEST(every_function_reads_its_first_256_bytes_alike_through_ecam_and_the_ports)
{
    uint32_t functions = 0u;
    uint32_t dwords = 0u;
    uint32_t differences = 0u;
    uint32_t refused = 0u;

    if (pci_express_test_first_reached() == NULL)
    {
        kernel_test_skip(test, "no MCFG window covers a scanned function: the machine has no ECAM");
        return;
    }
    for (uint32_t index = 0u; index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (!pci_express_test_reaches(device))
            continue;
        ++functions;
        for (uint16_t offset = 0u; offset < PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE; offset += 4u)
        {
            uint32_t through_window = 0u;
            const bool read = peripheral_component_interconnect_express_read(
                0u, device->bus, device->device, device->function, offset, 4u, &through_window);
            const uint32_t through_ports = peripheral_component_interconnect_config_read_dword(
                device->bus, device->device, device->function, (uint8_t) offset);

            refused += read ? 0u : 1u;
            differences += (through_window != through_ports) ? 1u : 0u;
            ++dwords;
        }
    }
    kernel_test_check(test, refused == 0u, "no access to a function a window covers is refused");
    kernel_test_check(test, differences == 0u, "every dword reads the same through both mechanisms");
    kernel_test_check(test, dwords == functions * 64u, "each function's 64 dwords are compared");
    kernel_test_measure(test, "functions", functions);
    kernel_test_measure(test, "dwords", dwords);
    kernel_test_measure(test, "differences", differences);
}

/**
 * @brief A function behind a bridge, on a bus past 0, reads its first 256 bytes the same through ECAM as through the
 *        ports: on bus 0 a wrong bus shift reads the right function all the same.
 *
 * @details q35 puts every default device on bus 0; a root port and a device behind it put one past it:
 *          `qemu-system-i386 -machine q35 -device pcie-root-port,id=lplport,chassis=1 -device e1000e,bus=lplport
 *          -kernel kernel/lpl.kernel -append lpl.test.require=peripheral_component_interconnect_express.*`.
 */
KERNEL_TEST(a_function_behind_a_bridge_reads_alike_through_ecam_and_the_ports)
{
    const PeripheralComponentInterconnectDevice_t *behind = NULL;
    uint32_t differences = 0u;
    uint32_t refused = 0u;

    for (uint32_t index = 0u; behind == NULL && index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (device->bus != 0u && pci_express_test_reaches(device))
            behind = device;
    }
    if (behind == NULL)
    {
        kernel_test_skip(test, "no function a window covers sits on a bus past 0");
        return;
    }
    for (uint16_t offset = 0u; offset < PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE; offset += 4u)
    {
        uint32_t through_window = 0u;

        refused += peripheral_component_interconnect_express_read(0u, behind->bus, behind->device, behind->function,
                                                                  offset, 4u, &through_window) ?
                       0u :
                       1u;
        differences += (through_window != peripheral_component_interconnect_config_read_dword(
                                              behind->bus, behind->device, behind->function, (uint8_t) offset)) ?
                           1u :
                           0u;
    }
    kernel_test_check(test, refused == 0u, "no access is refused");
    kernel_test_check(test, differences == 0u, "its 64 dwords read the same through both mechanisms");
    kernel_test_measure(test, "bus", behind->bus);
}

/**
 * @brief A byte and a word read through ECAM are the parts of the dword that holds them.
 */
KERNEL_TEST(bytes_and_words_are_the_parts_of_their_dword)
{
    const PeripheralComponentInterconnectDevice_t *device = pci_express_test_first_reached();
    uint32_t dword = 0u;
    uint32_t parts = 0u;
    uint32_t value = 0u;

    if (device == NULL)
    {
        kernel_test_skip(test, "no MCFG window covers a scanned function: the machine has no ECAM");
        return;
    }
    kernel_test_check(test,
                      peripheral_component_interconnect_express_read(0u, device->bus, device->device, device->function,
                                                                     0x08u, 4u, &dword),
                      "the classification dword reads");
    for (uint16_t byte = 0u; byte < 4u; ++byte)
        if (peripheral_component_interconnect_express_read(0u, device->bus, device->device, device->function,
                                                           (uint16_t) (0x08u + byte), 1u, &value) &&
            value == ((dword >> (8u * byte)) & 0xFFu))
            ++parts;
    for (uint16_t word = 0u; word < 2u; ++word)
        if (peripheral_component_interconnect_express_read(0u, device->bus, device->device, device->function,
                                                           (uint16_t) (0x08u + 2u * word), 2u, &value) &&
            value == ((dword >> (16u * word)) & 0xFFFFu))
            ++parts;
    kernel_test_check(test, parts == 6u, "its 4 bytes and 2 words are its parts");
    kernel_test_measure_hexadecimal(test, "classification", dword);
}

/**
 * @brief Past the first 256 bytes, which configuration mechanism #1 cannot address, a PCI Express function's
 *        extended space reads through ECAM: its first header is never all ones, which an absent register reads.
 */
KERNEL_TEST(the_extended_space_of_an_express_function_reads_through_ecam)
{
    uint32_t express = 0u;
    uint32_t read = 0u;
    uint32_t with_capability = 0u;

    if (pci_express_test_first_reached() == NULL)
    {
        kernel_test_skip(test, "no MCFG window covers a scanned function: the machine has no ECAM");
        return;
    }
    for (uint32_t index = 0u; index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);
        uint32_t header = PCI_EXPRESS_TEST_ABSENT;

        if (!pci_express_test_reaches(device) || !pci_express_test_is_express(device))
            continue;
        ++express;
        if (peripheral_component_interconnect_express_read(0u, device->bus, device->device, device->function,
                                                           PERIPHERAL_COMPONENT_INTERCONNECT_LEGACY_CONFIGURATION_SIZE,
                                                           4u, &header) &&
            header != PCI_EXPRESS_TEST_ABSENT)
            ++read;
        with_capability += (header != PCI_EXPRESS_TEST_ABSENT && header != 0u) ? 1u : 0u;
    }
    if (express == 0u)
    {
        kernel_test_skip(test, "no function a window covers has the PCI Express capability");
        return;
    }
    kernel_test_check(test, read == express, "the header at 0x100 of every PCI Express function reads");
    kernel_test_measure(test, "express_functions", express);
    kernel_test_measure(test, "with_extended_capability", with_capability);
}

/**
 * @brief A byte written through ECAM reads back through configuration mechanism #1, and the register is restored.
 */
KERNEL_TEST(a_write_through_ecam_reads_back_through_the_ports)
{
    const PeripheralComponentInterconnectDevice_t *device = pci_express_test_first_reached();

    if (device == NULL)
    {
        kernel_test_skip(test, "no MCFG window covers a scanned function: the machine has no ECAM");
        return;
    }

    const uint8_t original = peripheral_component_interconnect_config_read_byte(
        device->bus, device->device, device->function, PCI_EXPRESS_TEST_INTERRUPT_LINE);
    const uint8_t written = (uint8_t) (original ^ 0x5Au);
    const bool wrote = peripheral_component_interconnect_express_write(
        0u, device->bus, device->device, device->function, PCI_EXPRESS_TEST_INTERRUPT_LINE, 1u, written);
    const uint8_t read_back = peripheral_component_interconnect_config_read_byte(
        device->bus, device->device, device->function, PCI_EXPRESS_TEST_INTERRUPT_LINE);

    peripheral_component_interconnect_config_write_byte(device->bus, device->device, device->function,
                                                        PCI_EXPRESS_TEST_INTERRUPT_LINE, original);
    kernel_test_check(test, wrote, "the write is not refused");
    kernel_test_check(test, read_back == written, "the ports read the byte ECAM wrote");
}

/**
 * @brief Accesses no window can serve are refused and read all ones, whether or not the machine has ECAM: a bad
 *        width, an offset its width does not divide, one past the 4 KiB, a device or a function out of range, and
 *        a segment group no window covers.
 */
KERNEL_TEST(accesses_no_window_serves_are_refused)
{
    uint32_t value = 0u;
    uint32_t refused = 0u;
    uint32_t all_ones = 0u;
    const struct {
        uint16_t segment_group;
        uint8_t device;
        uint8_t function;
        uint16_t offset;
        uint8_t width;
    } cases[] = {
        {0u,      0u,  0u, 0x00u,   3u},
        {0u,      0u,  0u, 0x101u,  2u},
        {0u,      0u,  0u, 0xFFEu,  4u},
        {0u,      0u,  0u, 0x1000u, 1u},
        {0u,      32u, 0u, 0x00u,   4u},
        {0u,      0u,  8u, 0x00u,   4u},
        {0xFFFFu, 0u,  0u, 0x00u,   4u},
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        value = 0u;
        refused += peripheral_component_interconnect_express_read(cases[index].segment_group, 0u, cases[index].device,
                                                                  cases[index].function, cases[index].offset,
                                                                  cases[index].width, &value) ?
                       0u :
                       1u;
        all_ones += (value == PCI_EXPRESS_TEST_ABSENT) ? 1u : 0u;
    }
    kernel_test_check(test, refused == sizeof(cases) / sizeof(cases[0]), "each of the 7 accesses is refused");
    kernel_test_check(test, all_ones == sizeof(cases) / sizeof(cases[0]), "each refused read gives all ones");
    kernel_test_check(test, !peripheral_component_interconnect_express_write(0xFFFFu, 0u, 0u, 0u, 0x3Cu, 1u, 0u),
                      "a write to a segment group no window covers is refused");
}

/**
 * @brief Accesses to one function map its 4 KiB once: the window moves when the next access names another function,
 *        and only then.
 */
KERNEL_TEST(the_window_moves_once_per_change_of_function)
{
    const PeripheralComponentInterconnectDevice_t *first = pci_express_test_first_reached();
    const PeripheralComponentInterconnectDevice_t *other = NULL;
    uint32_t value = 0u;

    for (uint32_t index = 0u; first != NULL && index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (pci_express_test_reaches(device) && device != first)
            other = device;
    }
    if (other == NULL)
    {
        kernel_test_skip(test, "no two scanned functions a window covers");
        return;
    }
    (void) peripheral_component_interconnect_express_read(0u, first->bus, first->device, first->function, 0u, 4u,
                                                          &value);

    const uint32_t before = peripheral_component_interconnect_express_window_moves();

    for (uint16_t offset = 0u; offset < 0x40u; offset += 4u)
        (void) peripheral_component_interconnect_express_read(0u, first->bus, first->device, first->function, offset,
                                                              4u, &value);

    const uint32_t staying = peripheral_component_interconnect_express_window_moves() - before;

    (void) peripheral_component_interconnect_express_read(0u, other->bus, other->device, other->function, 0u, 4u,
                                                          &value);
    (void) peripheral_component_interconnect_express_read(0u, other->bus, other->device, other->function, 4u, 4u,
                                                          &value);

    const uint32_t moving = peripheral_component_interconnect_express_window_moves() - before;

    kernel_test_check(test, staying == 0u, "16 reads of the function the window holds move it 0 times");
    kernel_test_check(test, moving == 1u, "two reads of another function move it once");
}

/**
 * @brief The configuration space is mapped uncached, even where the memory type range registers say write-back, as
 *        SeaBIOS leaves q35's: a cached read of a status register would return what the line held when it was filled.
 */
KERNEL_TEST(the_configuration_space_is_mapped_uncached)
{
    const AdvancedConfigurationAndPowerInterfaceMemoryMappedConfigurationState_t *mcfg =
        advanced_configuration_and_power_interface_memory_mapped_configuration();
    PageTableEntry_t entry = {0};

    if (mcfg->allocation_count == 0u || mcfg->allocations[0].base_address > UINT32_MAX)
    {
        kernel_test_skip(test, "no MCFG window below 4 GiB");
        return;
    }

    const uint32_t base =
        (uint32_t) mcfg->allocations[0].base_address + ((uint32_t) mcfg->allocations[0].start_bus << 20u);
    const uint32_t window =
        kernel_vmm_map_device_window(base, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE);
    const bool mapped = window != 0u && paging_get_page_table_entry(window, &entry);

    if (window != 0u)
        kernel_vmm_unmap_device_window(window, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE);
    kernel_test_check(test, mapped, "a window over the first function's 4 KiB is mapped");
    kernel_test_check(test, entry.cache_disable && entry.write_through, "its page is cache-disabled and write-through");
    kernel_test_measure(test, "registers_say_write_back",
                        memory_type_range_registers_frame_is_write_back(base) ? 1u : 0u);
}
