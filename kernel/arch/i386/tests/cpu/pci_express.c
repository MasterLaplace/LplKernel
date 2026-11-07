#include <kernel/acpi/tables.h>
#include <kernel/cpu/mtrr.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(peripheral_component_interconnect_express, KERNEL_TEST_STAGE_BOOTED);

#define PCI_EXPRESS_TEST_STATUS_CAPABILITIES  0x10u       /**< Status bit: the function has a capability list. */
#define PCI_EXPRESS_TEST_CAPABILITIES_POINTER 0x34u       /**< The offset of the first capability's offset. */
#define PCI_EXPRESS_TEST_INTERRUPT_LINE       0x3Cu       /**< A byte software may write, as the BIOS does. */
#define PCI_EXPRESS_TEST_INTERRUPT_PIN        0x3Du       /**< Nonzero when the function uses an interrupt pin. */
#define PCI_EXPRESS_TEST_CAPABILITY_EXPRESS   0x10u       /**< The PCI Express capability's identifier. */
#define PCI_EXPRESS_TEST_MAXIMUM_CAPABILITIES 48u         /**< Capabilities the first 256 bytes can hold. */
#define PCI_EXPRESS_TEST_SPACE_DWORDS         1024u       /**< Dwords of one function's 4 KiB. */
#define PCI_EXPRESS_TEST_ABSENT               0xFFFFFFFFu /**< What an absent register reads. */
#define PCI_EXPRESS_TEST_MEBIBYTE             0x100000u   /**< One bus of ECAM, and where the kernel's RAM starts. */

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
 * @brief A made-up configuration space the walk reads, a dword at a time, and how many reads it took.
 */
typedef struct PciExpressTestSpace {
    uint32_t dwords[PCI_EXPRESS_TEST_SPACE_DWORDS]; /**< The 4 KiB. */
    uint16_t refused_from;                          /**< Reads at and past this offset are refused, or 0. */
    uint32_t reads;                                 /**< Reads the walk made. */
} PciExpressTestSpace_t;

static PciExpressTestSpace_t pci_express_test_space;
static PeripheralComponentInterconnectExpressExtendedCapability_t
    pci_express_test_capabilities[PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_MAX_EXTENDED_CAPABILITIES];

/**
 * @brief Reads one dword of the made-up space, counted.
 */
static bool pci_express_test_read_space(void *context, uint16_t offset, uint32_t *out_value)
{
    PciExpressTestSpace_t *space = context;

    ++space->reads;
    if ((space->refused_from != 0u && offset >= space->refused_from) || offset % 4u != 0u ||
        offset >= PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE)
        return false;
    *out_value = space->dwords[offset / 4u];
    return true;
}

/**
 * @brief Empties the made-up space: no extended capability, every read served.
 */
static PciExpressTestSpace_t *pci_express_test_empty_space(void)
{
    memset(&pci_express_test_space, 0, sizeof(pci_express_test_space));
    return &pci_express_test_space;
}

/**
 * @brief Writes an extended capability header at @p offset: its ID, its version and the next header's offset.
 */
static void pci_express_test_header(PciExpressTestSpace_t *space, uint16_t offset, uint16_t identifier, uint8_t version,
                                    uint16_t next)
{
    space->dwords[offset / 4u] = (uint32_t) identifier | (uint32_t) (version & 0xFu) << 16u | (uint32_t) next << 20u;
}

/**
 * @brief Walks the made-up space, room for @p capacity capabilities.
 */
static PeripheralComponentInterconnectExpressWalk_t pci_express_test_walk(uint32_t capacity, uint32_t *out_count)
{
    return peripheral_component_interconnect_express_walk_extended_capabilities(
        pci_express_test_read_space, &pci_express_test_space, pci_express_test_capabilities, capacity, out_count);
}

/**
 * @brief Whether capability @p index of the last walk lies at @p offset with @p identifier and @p version.
 */
static bool pci_express_test_capability_is(uint32_t index, uint16_t offset, uint16_t identifier, uint8_t version)
{
    return pci_express_test_capabilities[index].offset == offset &&
           pci_express_test_capabilities[index].identifier == identifier &&
           pci_express_test_capabilities[index].version == version;
}

/**
 * @brief The first scanned function whose bus a window covers and that uses an interrupt pin, so that its Interrupt
 *        Line register is writable (PCI Local Bus Specification 3.0, §6.2.4), or NULL.
 */
static const PeripheralComponentInterconnectDevice_t *pci_express_test_first_reached_with_a_pin(void)
{
    for (uint32_t index = 0u; index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (pci_express_test_reaches(device) &&
            peripheral_component_interconnect_config_read_byte(device->bus, device->device, device->function,
                                                               PCI_EXPRESS_TEST_INTERRUPT_PIN) != 0u)
            return device;
    }
    return NULL;
}

/**
 * @brief Whether @p header, read at 0x100, is an extended capability header and not the identity dword at 0x00 an
 *        offset truncated to the first 256 bytes would read: its next offset is 0 or past the first 256 bytes, its
 *        two low bits 0 (PCI Express Base Specification, §7.6.3).
 */
static bool pci_express_test_is_extended_header(uint32_t header, uint32_t identity)
{
    const uint32_t next = header >> 20u;

    return header != identity && (next == 0u || (next >= 0x100u && (next & 3u) == 0u));
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
 * @details q35 puts every default device on bus 0; a root port and a device behind it put one past it. This bench
 *          runs every test of the suite, the uncached one included:
 *          `qemu-system-i386 -machine q35 -cpu max -device pcie-root-port,id=lplport,chassis=1
 *          -device e1000e,bus=lplport -kernel kernel/lpl.kernel
 *          -append lpl.test.require=peripheral_component_interconnect_express.*`.
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
 *        extended space reads through ECAM: its first header is never all ones, which an absent register reads, and
 *        is an extended capability header rather than the function's identity, which an offset that lost its ninth
 *        bit would read.
 */
KERNEL_TEST(the_extended_space_of_an_express_function_reads_through_ecam)
{
    uint32_t express = 0u;
    uint32_t read = 0u;
    uint32_t well_formed = 0u;
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
        well_formed +=
            pci_express_test_is_extended_header(header, peripheral_component_interconnect_config_read_dword(
                                                            device->bus, device->device, device->function, 0x00u)) ?
                1u :
                0u;
        with_capability += (header != PCI_EXPRESS_TEST_ABSENT && header != 0u) ? 1u : 0u;
    }
    if (express == 0u)
    {
        kernel_test_skip(test, "no function a window covers has the PCI Express capability");
        return;
    }
    kernel_test_check(test, read == express, "the header at 0x100 of every PCI Express function reads");
    kernel_test_check(test, well_formed == express,
                      "each is an extended capability header, not the identity dword at 0x00");
    kernel_test_measure(test, "express_functions", express);
    kernel_test_measure(test, "with_extended_capability", with_capability);
}

/**
 * @brief A byte written through ECAM reads back through configuration mechanism #1, and the register is restored.
 *
 * @details The byte is the Interrupt Line of a function that uses an interrupt pin: a function without one, a host
 *          bridge often, need not implement that register.
 */
KERNEL_TEST(a_write_through_ecam_reads_back_through_the_ports)
{
    const PeripheralComponentInterconnectDevice_t *device = pci_express_test_first_reached_with_a_pin();

    if (device == NULL)
    {
        kernel_test_skip(test, "no function a window covers uses an interrupt pin");
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
    kernel_test_measure(test, "bus", device->bus);
    kernel_test_measure(test, "device", device->device);
    kernel_test_measure(test, "function", device->function);
}

/**
 * @brief Accesses no window can serve are refused and read their width of ones, as an absent function reads, whether
 *        or not the machine has ECAM: a bad width, an offset its width does not divide, one past the 4 KiB, a device
 *        or a function out of range, and a segment group no window covers.
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
        uint32_t ones;
    } cases[] = {
        {0u,      0u,  0u, 0x00u,   3u, PCI_EXPRESS_TEST_ABSENT},
        {0u,      0u,  0u, 0x101u,  2u, 0xFFFFu                },
        {0u,      0u,  0u, 0xFFEu,  4u, PCI_EXPRESS_TEST_ABSENT},
        {0u,      0u,  0u, 0x1000u, 1u, 0xFFu                  },
        {0u,      32u, 0u, 0x00u,   4u, PCI_EXPRESS_TEST_ABSENT},
        {0u,      0u,  8u, 0x00u,   4u, PCI_EXPRESS_TEST_ABSENT},
        {0xFFFFu, 0u,  0u, 0x00u,   4u, PCI_EXPRESS_TEST_ABSENT},
    };

    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        value = 0u;
        refused += peripheral_component_interconnect_express_read(cases[index].segment_group, 0u, cases[index].device,
                                                                  cases[index].function, cases[index].offset,
                                                                  cases[index].width, &value) ?
                       0u :
                       1u;
        all_ones += (value == cases[index].ones) ? 1u : 0u;
    }
    kernel_test_check(test, refused == sizeof(cases) / sizeof(cases[0]), "each of the 7 accesses is refused");
    kernel_test_check(test, all_ones == sizeof(cases) / sizeof(cases[0]), "each refused read gives its width of ones");
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
 *
 * @details Where the registers do not say write-back, every window over a reserved page is uncached already, so the
 *          test could not fail: QEMU's default processor has no memory type range registers, and the test needs
 *          `qemu-system-i386 -machine q35 -cpu max -kernel kernel/lpl.kernel`.
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

    if (!memory_type_range_registers_frame_is_write_back(base))
    {
        kernel_test_skip(test, "the memory type range registers do not say write-back over it: boot with -cpu max");
        return;
    }

    const uint32_t window =
        kernel_vmm_map_device_window(base, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE);
    const bool mapped = window != 0u && paging_get_page_table_entry(window, &entry);

    if (window != 0u)
        kernel_vmm_unmap_device_window(window, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_CONFIGURATION_SIZE);
    kernel_test_check(test, mapped, "a window over the first function's 4 KiB is mapped");
    kernel_test_check(test, entry.cache_disable && entry.write_through, "its page is cache-disabled and write-through");
}

/**
 * @brief A window is refused when its buses end past 4 GiB, the sum taken without wrapping however near 2^64 the
 *        base lies, or when they cover usable RAM; one that ends at 4 GiB exactly, over no RAM, is usable.
 */
KERNEL_TEST(a_window_past_4_gib_or_over_ram_is_refused)
{
    const struct {
        uint64_t base_address;
        uint8_t start_bus;
        uint8_t end_bus;
        PeripheralComponentInterconnectExpressWindow_t verdict;
    } cases[] = {
        {0xFFFFFFFFFFF00000ull, 1u, 1u,    PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH},
        {0x0000000100000000ull, 0u, 0u,    PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH},
        {0x00000000F0000000ull, 0u, 0xFFu, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_USABLE    },
        {0x00000000F0100000ull, 0u, 0xFFu, PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_PAST_REACH},
        {0x0000000000100000ull, 0u, 0u,    PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_OVER_RAM  },
    };
    uint32_t agreeing = 0u;

    if (!physical_memory_manager_reaches_ram(PCI_EXPRESS_TEST_MEBIBYTE, PCI_EXPRESS_TEST_MEBIBYTE))
    {
        kernel_test_skip(test, "the boot memory map gives no usable RAM at 1 MiB");
        return;
    }
    for (uint32_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        agreeing +=
            peripheral_component_interconnect_express_judge_window(cases[index].base_address, cases[index].start_bus,
                                                                   cases[index].end_bus) == cases[index].verdict ?
                1u :
                0u;
    kernel_test_check(test, agreeing == sizeof(cases) / sizeof(cases[0]), "each of the 5 windows gets its verdict");
}

/**
 * @brief The boot declared exactly the windows of its MCFG the judge calls usable, and accesses reach the buses of
 *        those windows and of no other.
 */
KERNEL_TEST(the_boot_declares_the_usable_windows_and_reaches_through_them_alone)
{
    const AdvancedConfigurationAndPowerInterfaceMemoryMappedConfigurationState_t *mcfg =
        advanced_configuration_and_power_interface_memory_mapped_configuration();
    const PeripheralComponentInterconnectExpressWindowCounts_t *counts =
        peripheral_component_interconnect_express_window_counts();
    uint32_t usable = 0u;
    uint32_t agreeing = 0u;

    for (uint32_t index = 0u; index < mcfg->allocation_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceConfigurationSpaceAllocation_t *window = &mcfg->allocations[index];
        const bool judged_usable = peripheral_component_interconnect_express_judge_window(
                                       window->base_address, window->start_bus, window->end_bus) ==
                                   PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WINDOW_USABLE;

        const bool first_reached =
            peripheral_component_interconnect_express_reaches(window->segment_group, window->start_bus);
        const bool last_reached =
            peripheral_component_interconnect_express_reaches(window->segment_group, window->end_bus);

        usable += judged_usable ? 1u : 0u;
        agreeing += (first_reached == judged_usable && last_reached == judged_usable) ? 1u : 0u;
    }
    kernel_test_check(
        test, counts->declared + counts->past_reach + counts->over_ram + counts->no_room == mcfg->allocation_count,
        "every window the MCFG keeps is counted once");
    kernel_test_check(test, counts->declared == usable, "every usable window is declared");
    kernel_test_check(test, agreeing == mcfg->allocation_count,
                      "accesses reach a window's first and last bus exactly when it is usable");
    kernel_test_measure(test, "windows", mcfg->allocation_count);
    kernel_test_measure(test, "declared", counts->declared);
}

/**
 * @brief A list of three capabilities, out of offset order, one of version 10 and one whose next offset has its
 *        reserved low bits set, is walked in list order to its end: the low bits are masked, as Linux masks them.
 */
KERNEL_TEST(a_list_is_walked_in_its_own_order_to_a_next_offset_of_0)
{
    PciExpressTestSpace_t *space = pci_express_test_empty_space();
    uint32_t count = 0u;

    pci_express_test_header(space, 0x100u, 0x0001u, 2u, 0x200u);
    pci_express_test_header(space, 0x200u, 0x000Eu, 0xAu, 0x143u);
    pci_express_test_header(space, 0x140u, 0x0003u, 1u, 0x000u);

    const PeripheralComponentInterconnectExpressWalk_t walk = pci_express_test_walk(4u, &count);

    kernel_test_check(test, walk == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_LISTED, "the walk ends listed");
    kernel_test_check(test, count == 3u, "it passes 3 capabilities");
    kernel_test_check(test,
                      pci_express_test_capability_is(0u, 0x100u, 0x0001u, 2u) &&
                          pci_express_test_capability_is(1u, 0x200u, 0x000Eu, 0xAu) &&
                          pci_express_test_capability_is(2u, 0x140u, 0x0003u, 1u),
                      "each keeps its offset, its ID and its version, in list order");
    kernel_test_check(test, space->reads == 3u, "it reads one dword per header");
}

/**
 * @brief A header of 0 at 0x100 means no capability, and all ones, or a refused read, no extended space at all: three
 *        ends told apart.
 */
KERNEL_TEST(no_capability_and_no_extended_space_are_two_ends)
{
    uint32_t none_count = 1u;
    uint32_t absent_count = 1u;
    uint32_t refused_count = 1u;
    PciExpressTestSpace_t *space = pci_express_test_empty_space();

    const PeripheralComponentInterconnectExpressWalk_t none = pci_express_test_walk(4u, &none_count);

    space->dwords[0x100u / 4u] = 0xFFFFFFFFu;

    const PeripheralComponentInterconnectExpressWalk_t absent = pci_express_test_walk(4u, &absent_count);

    space = pci_express_test_empty_space();
    space->refused_from = 0x100u;

    const PeripheralComponentInterconnectExpressWalk_t refused = pci_express_test_walk(4u, &refused_count);

    kernel_test_check(test, none == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NONE && none_count == 0u,
                      "a header of 0 lists no capability");
    kernel_test_check(test, absent == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NO_SPACE && absent_count == 0u,
                      "all ones at 0x100 is no extended space");
    kernel_test_check(test, refused == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NO_SPACE && refused_count == 0u,
                      "a refused read at 0x100 is no extended space");
}

/**
 * @brief A next offset below 0x100 that is not 0, and a header past the first that reads all ones or is refused, end
 *        the walk malformed, the capabilities before them kept.
 */
KERNEL_TEST(a_link_below_the_extended_space_or_an_absent_header_is_malformed)
{
    uint32_t low_count = 0u;
    uint32_t absent_count = 0u;
    uint32_t refused_count = 0u;
    PciExpressTestSpace_t *space = pci_express_test_empty_space();

    pci_express_test_header(space, 0x100u, 0x0001u, 2u, 0x0F8u);

    const PeripheralComponentInterconnectExpressWalk_t low = pci_express_test_walk(4u, &low_count);

    pci_express_test_header(space, 0x100u, 0x0001u, 2u, 0x180u);
    space->dwords[0x180u / 4u] = 0xFFFFFFFFu;

    const PeripheralComponentInterconnectExpressWalk_t absent = pci_express_test_walk(4u, &absent_count);

    space->refused_from = 0x180u;

    const PeripheralComponentInterconnectExpressWalk_t refused = pci_express_test_walk(4u, &refused_count);

    kernel_test_check(test, low == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_MALFORMED && low_count == 1u,
                      "a next offset of 0xF8 is malformed, after the first capability");
    kernel_test_check(test, absent == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_MALFORMED && absent_count == 1u,
                      "a second header of all ones is malformed");
    kernel_test_check(test, refused == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_MALFORMED && refused_count == 1u,
                      "a second header the read refuses is malformed");
    kernel_test_check(test, pci_express_test_capability_is(0u, 0x100u, 0x0001u, 2u), "the first capability is kept");
}

/**
 * @brief A list that points back at itself ends at the bound of 480 headers, a list longer than the room the caller
 *        gave keeps that many and counts the rest, and one as long as the room is listed.
 */
KERNEL_TEST(a_loop_ends_at_the_bound_and_a_long_list_is_counted_past_the_room)
{
    uint32_t loop_count = 0u;
    uint32_t long_count = 0u;
    PciExpressTestSpace_t *space = pci_express_test_empty_space();

    pci_express_test_header(space, 0x100u, 0x0019u, 1u, 0x100u);

    const PeripheralComponentInterconnectExpressWalk_t loop =
        pci_express_test_walk(PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_MAX_EXTENDED_CAPABILITIES, &loop_count);
    const uint32_t loop_reads = space->reads;

    space = pci_express_test_empty_space();
    pci_express_test_header(space, 0x100u, 0x0001u, 2u, 0x140u);
    pci_express_test_header(space, 0x140u, 0x0003u, 1u, 0x180u);
    pci_express_test_header(space, 0x180u, 0x000Du, 1u, 0x000u);

    const PeripheralComponentInterconnectExpressWalk_t truncated = pci_express_test_walk(2u, &long_count);
    uint32_t fitting_count = 0u;
    const PeripheralComponentInterconnectExpressWalk_t fitting = pci_express_test_walk(3u, &fitting_count);

    kernel_test_check(test,
                      loop == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_LOOPED &&
                          loop_count == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_MAX_EXTENDED_CAPABILITIES,
                      "a header that names itself ends looped after 480 headers");
    kernel_test_check(test, loop_reads == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_MAX_EXTENDED_CAPABILITIES,
                      "the loop reads no more than the bound");
    kernel_test_check(test, truncated == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_TRUNCATED && long_count == 3u,
                      "three capabilities with room for two end truncated, all three counted");
    kernel_test_check(test, fitting == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_LISTED && fitting_count == 3u,
                      "three capabilities with room for three end listed");
    kernel_test_check(test,
                      pci_express_test_capability_is(0u, 0x100u, 0x0001u, 2u) &&
                          pci_express_test_capability_is(1u, 0x140u, 0x0003u, 1u),
                      "the first two are kept");
}

/**
 * @brief Through ECAM, the walk of every PCI Express function a window covers ends listed or with no capability, and
 *        a conventional function, whose extended space reads all ones, with none.
 */
KERNEL_TEST(every_express_function_lists_its_extended_capabilities_to_their_end)
{
    uint32_t express = 0u;
    uint32_t well_ended = 0u;
    uint32_t capabilities = 0u;
    uint32_t count = 0u;

    if (pci_express_test_first_reached() == NULL)
    {
        kernel_test_skip(test, "no MCFG window covers a scanned function: the machine has no ECAM");
        return;
    }
    for (uint32_t index = 0u; index < peripheral_component_interconnect_get_device_count(); ++index)
    {
        const PeripheralComponentInterconnectDevice_t *device = peripheral_component_interconnect_get_device(index);

        if (!pci_express_test_reaches(device) || !pci_express_test_is_express(device))
            continue;
        ++express;

        const PeripheralComponentInterconnectExpressWalk_t walk =
            peripheral_component_interconnect_express_list_extended_capabilities(
                0u, device->bus, device->device, device->function, pci_express_test_capabilities,
                PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_MAX_EXTENDED_CAPABILITIES, &count);

        well_ended += (walk == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_LISTED ||
                       walk == PERIPHERAL_COMPONENT_INTERCONNECT_EXPRESS_WALK_NONE) ?
                          1u :
                          0u;
        capabilities += count;
    }
    if (express == 0u)
    {
        kernel_test_skip(test, "no function a window covers has the PCI Express capability");
        return;
    }
    kernel_test_check(test, well_ended == express, "each walk ends listed or with no capability");
    kernel_test_measure(test, "express_functions", express);
    kernel_test_measure(test, "extended_capabilities", capabilities);
}
