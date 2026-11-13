#include "simulated_host.h"

#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_power_management_registers,
                  KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @name The made-up platform's PM1 ports
 * @{
 */
#define REGISTERS_PM1A_EVENT   0x400u /**< PM1a_EVT_BLK: PM1a_STS, then PM1a_EN at 0x402. */
#define REGISTERS_PM1B_EVENT   0x440u /**< PM1b_EVT_BLK: PM1b_STS, then PM1b_EN at 0x442. */
#define REGISTERS_PM1A_CONTROL 0x404u /**< PM1a_CNT_BLK, 2 bytes. */
#define REGISTERS_PM1B_CONTROL 0x444u /**< PM1b_CNT_BLK, 2 bytes. */
#define REGISTERS_PROTECTED    0xCF8u /**< PCI configuration mechanism #1's address port, which ACPI protects. */
/** @} */

#define REGISTERS_PM1A_HOLDS 0x2001u     /**< What PM1a_CNT reads: SCI_EN, and SLP_EN, which some hardware reads set. */
#define REGISTERS_PM1B_HOLDS 0x0201u     /**< What PM1b_CNT reads: SCI_EN and bit 9, which ACPI ignores. */
#define REGISTERS_NOT_LOGGED 0xFFFFFFFFu /**< What registers_write_index gives for a write the log does not hold. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t registers_host;

/**
 * @brief A register of @p bits bits at port @p port, in the I/O space.
 */
static AdvancedConfigurationAndPowerInterfaceGenericAddress_t registers_port(uint16_t port, uint8_t bits)
{
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t reg = {
        .address = port,
        .space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_INPUT_OUTPUT,
        .bit_width = bits,
    };

    return reg;
}

/**
 * @brief A normalised FADT with both PM1 groups, their event blocks 4 bytes long.
 */
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t registers_both_groups(void)
{
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t fadt;

    memset(&fadt, 0, sizeof(fadt));
    fadt.present = true;
    fadt.power_management_event_blocks[0] = registers_port(REGISTERS_PM1A_EVENT, 32u);
    fadt.power_management_event_blocks[1] = registers_port(REGISTERS_PM1B_EVENT, 32u);
    fadt.power_management_control_blocks[0] = registers_port(REGISTERS_PM1A_CONTROL, 16u);
    fadt.power_management_control_blocks[1] = registers_port(REGISTERS_PM1B_CONTROL, 16u);
    return fadt;
}

/**
 * @brief Opens the simulated host with PM1a_CNT and PM1b_CNT holding REGISTERS_PM1A_HOLDS and REGISTERS_PM1B_HOLDS.
 */
static void registers_open_host(void)
{
    advanced_configuration_and_power_interface_simulated_host_open(&registers_host);
    (void) advanced_configuration_and_power_interface_simulated_host_set_port(&registers_host, REGISTERS_PM1A_CONTROL,
                                                                              REGISTERS_PM1A_HOLDS);
    (void) advanced_configuration_and_power_interface_simulated_host_set_port(&registers_host, REGISTERS_PM1B_CONTROL,
                                                                              REGISTERS_PM1B_HOLDS);
}

/**
 * @brief Where the host's log holds a 2-byte write of @p value to @p port, or REGISTERS_NOT_LOGGED.
 */
static uint32_t registers_write_index(uint16_t port, uint32_t value)
{
    for (uint32_t index = 0u; index < registers_host.access_count &&
                              index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
         ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &registers_host.accesses[index];

        if (access->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE && access->port == port &&
            access->width == 2u && access->value == value)
            return index;
    }
    return REGISTERS_NOT_LOGGED;
}

/**
 * @brief The status registers are the first half of each PM1 event block, PM1a's then PM1b's.
 */
KERNEL_TEST(the_status_registers_are_the_first_half_of_each_event_block)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t fadt = registers_both_groups();
    uint16_t ports[2] = {1u, 1u};

    kernel_test_check(test, advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "both groups give status registers");
    kernel_test_check(test, ports[0] == REGISTERS_PM1A_EVENT, "PM1a_STS is PM1a_EVT_BLK");
    kernel_test_check(test, ports[1] == REGISTERS_PM1B_EVENT, "PM1b_STS is PM1b_EVT_BLK");
}

/**
 * @brief A platform without a PM1b event block has PM1a's status register alone.
 */
KERNEL_TEST(without_a_second_event_block_the_status_is_the_first_one_alone)
{
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t fadt = registers_both_groups();
    uint16_t ports[2] = {1u, 1u};

    memset(&fadt.power_management_event_blocks[1], 0, sizeof(fadt.power_management_event_blocks[1]));
    kernel_test_check(test, advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "PM1a alone gives a status register");
    kernel_test_check(test, ports[0] == REGISTERS_PM1A_EVENT && ports[1] == 0u, "PM1a_STS, and no PM1b_STS");
}

/**
 * @brief A status register is refused when PM1a's event block is absent or shorter than 4 bytes, when either block
 *        reaches a protected port, and when PM1b's gives an address outside the I/O space.
 */
KERNEL_TEST(a_status_register_needs_an_event_block_of_four_bytes_on_ports_it_may_read)
{
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t fadt = registers_both_groups();
    uint16_t ports[2] = {1u, 1u};

    fadt.power_management_event_blocks[0].bit_width = 16u;
    kernel_test_check(test, !advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "a 2-byte PM1a event block is refused");
    kernel_test_check(test, ports[0] == 0u && ports[1] == 0u, "a refusal gives no port");

    fadt = registers_both_groups();
    memset(&fadt.power_management_event_blocks[0], 0, sizeof(fadt.power_management_event_blocks[0]));
    kernel_test_check(test, !advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "an absent PM1a event block is refused");

    fadt = registers_both_groups();
    fadt.power_management_event_blocks[0].address = REGISTERS_PROTECTED;
    kernel_test_check(test, !advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "a PM1a status on a protected port is refused");

    fadt = registers_both_groups();
    fadt.power_management_event_blocks[1].address = REGISTERS_PROTECTED;
    kernel_test_check(test, !advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "a PM1b status on a protected port is refused");

    fadt = registers_both_groups();
    fadt.power_management_event_blocks[1].space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY;
    kernel_test_check(test, !advanced_configuration_and_power_interface_power_management_status_ports(&fadt, ports),
                      "a PM1b event block in memory is refused");
}

/**
 * @brief The control registers are each PM1 control block as a 16-bit port, 0 for one outside the I/O space.
 */
KERNEL_TEST(the_control_registers_are_each_control_block_as_a_port)
{
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t fadt = registers_both_groups();
    uint16_t ports[2] = {1u, 1u};

    advanced_configuration_and_power_interface_power_management_control_ports(&fadt, ports);
    kernel_test_check(test, ports[0] == REGISTERS_PM1A_CONTROL && ports[1] == REGISTERS_PM1B_CONTROL,
                      "PM1a_CNT and PM1b_CNT");

    fadt.power_management_control_blocks[1].space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY;
    advanced_configuration_and_power_interface_power_management_control_ports(&fadt, ports);
    kernel_test_check(test, ports[0] == REGISTERS_PM1A_CONTROL && ports[1] == 0u, "a PM1b_CNT in memory is no port");
}

/**
 * @brief Setting a control bit writes both registers what they hold together, the write-only bits cleared and the
 *        bit set, PM1a first.
 */
KERNEL_TEST(setting_a_control_bit_writes_both_registers_what_they_hold_without_the_write_only_bits)
{
    static const uint16_t ports[2] = {REGISTERS_PM1A_CONTROL, REGISTERS_PM1B_CONTROL};
    const uint32_t expected = ((REGISTERS_PM1A_HOLDS | REGISTERS_PM1B_HOLDS) &
                               ~ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_CONTROL_WRITE_ONLY) |
                              ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_RELOAD;

    registers_open_host();
    kernel_test_check(test,
                      advanced_configuration_and_power_interface_set_power_management_control_bits(
                          &registers_host.host, ports, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_RELOAD),
                      "a register was written");
    const uint32_t first = registers_write_index(REGISTERS_PM1A_CONTROL, expected);
    const uint32_t second = registers_write_index(REGISTERS_PM1B_CONTROL, expected);
    kernel_test_check(test, expected == 0x0203u, "the value is SCI_EN, bit 9 and BM_RLD");
    kernel_test_check(test, first != REGISTERS_NOT_LOGGED, "PM1a_CNT is written the value");
    kernel_test_check(test, second != REGISTERS_NOT_LOGGED, "PM1b_CNT is written the value");
    kernel_test_check(test, first < second, "PM1a first");
    advanced_configuration_and_power_interface_simulated_host_close(&registers_host);
}

/**
 * @brief A control register is neither read nor written through a port that is 0 or protected, and nothing written
 *        is said when no register was.
 */
KERNEL_TEST(a_control_bit_is_never_set_through_a_missing_or_protected_port)
{
    static const uint16_t first_alone[2] = {REGISTERS_PM1A_CONTROL, 0u};
    static const uint16_t protected_alone[2] = {REGISTERS_PROTECTED, 0u};
    static const uint16_t none[2] = {0u, 0u};

    registers_open_host();
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_set_power_management_control_bits(
            &registers_host.host, first_alone, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_RELOAD),
        "PM1a alone is written");
    kernel_test_check(test, registers_host.access_count == 2u, "one read and one write, PM1b untouched");
    advanced_configuration_and_power_interface_simulated_host_close(&registers_host);

    registers_open_host();
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_set_power_management_control_bits(
            &registers_host.host, protected_alone, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_RELOAD),
        "a protected port writes nothing");
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_set_power_management_control_bits(
                          &registers_host.host, none, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_RELOAD),
                      "no port writes nothing");
    kernel_test_check(test, registers_host.access_count == 0u, "and neither reads");
    advanced_configuration_and_power_interface_simulated_host_close(&registers_host);
}

/**
 * @brief Clearing a status bit writes it alone to each status register, PM1a first, reads nothing, and leaves bit 11,
 *        which ACPI ignores, 0; a port that is 0 or protected is not written.
 */
KERNEL_TEST(clearing_a_status_bit_writes_it_alone_to_each_status_register)
{
    static const uint16_t both[2] = {REGISTERS_PM1A_EVENT, REGISTERS_PM1B_EVENT};
    static const uint16_t protected_alone[2] = {REGISTERS_PROTECTED, 0u};
    const uint32_t bus_master = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_BUS_MASTER_STATUS;

    registers_open_host();
    kernel_test_check(
        test,
        advanced_configuration_and_power_interface_clear_power_management_status_bits(
            &registers_host.host, both, bus_master | ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_STATUS_IGNORED),
        "a register was written");
    const uint32_t first = registers_write_index(REGISTERS_PM1A_EVENT, bus_master);
    const uint32_t second = registers_write_index(REGISTERS_PM1B_EVENT, bus_master);
    kernel_test_check(test, first != REGISTERS_NOT_LOGGED && second != REGISTERS_NOT_LOGGED,
                      "BM_STS alone is written to PM1a_STS and PM1b_STS, bit 11 left 0");
    kernel_test_check(test, first < second && registers_host.access_count == 2u, "PM1a first, and nothing read");
    advanced_configuration_and_power_interface_simulated_host_close(&registers_host);

    registers_open_host();
    kernel_test_check(test,
                      !advanced_configuration_and_power_interface_clear_power_management_status_bits(
                          &registers_host.host, protected_alone, bus_master),
                      "a protected port is not written");
    kernel_test_check(test, registers_host.access_count == 0u, "nor read");
    advanced_configuration_and_power_interface_simulated_host_close(&registers_host);
}
