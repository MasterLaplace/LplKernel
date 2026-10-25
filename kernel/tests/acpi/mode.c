#include "simulated_host.h"

#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_mode, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @name The made-up platform's ports
 * @{
 */
#define MODE_SMI_COMMAND  0xB2u  /**< SMI_CMD. */
#define MODE_ENABLE       0xF1u  /**< ACPI_ENABLE. */
#define MODE_PM1A_EVENT   0x400u /**< PM1a_EVT_BLK, 4 bytes: PM1a_STS, then PM1a_EN at 0x402. */
#define MODE_PM1A_CONTROL 0x404u /**< PM1a_CNT_BLK, 2 bytes. */
#define MODE_PM1B_EVENT   0x440u /**< PM1b_EVT_BLK, 4 bytes: PM1b_STS, then PM1b_EN at 0x442. */
#define MODE_PM1B_CONTROL 0x444u /**< PM1b_CNT_BLK, 2 bytes. */
#define MODE_GPE0         0x420u /**< GPE0_BLK, 8 bytes: GPE0_STS, then GPE0_EN from 0x424. */
#define MODE_GPE1         0x430u /**< GPE1_BLK, 4 bytes: GPE1_STS, then GPE1_EN from 0x432. */
/** @} */

#define MODE_PM1A_ENABLES  0x4721u     /**< What PM1a_EN holds: every fixed-event enable, and PCIEXP_WAKE_DIS. */
#define MODE_PM1A_LEFT     0x4000u     /**< What clearing its fixed-event enables leaves: PCIEXP_WAKE_DIS. */
#define MODE_PM1B_ENABLES  0x0100u     /**< What PM1b_EN holds: PWRBTN_EN. */
#define MODE_ENABLE_WRITES 8u          /**< GPE0_EN's 4 bytes, GPE1_EN's 2, PM1a_EN and PM1b_EN. */
#define MODE_NOT_LOGGED    0xFFFFFFFFu /**< What mode_write_index gives for a write the log does not hold. */

static AdvancedConfigurationAndPowerInterfaceSimulatedHost_t mode_host;
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t mode_fadt;
static AdvancedConfigurationAndPowerInterfaceModeSwitch_t mode_switch;

/**
 * @brief A register of @p bits bits at port @p port.
 */
static AdvancedConfigurationAndPowerInterfaceGenericAddress_t mode_port_register(uint16_t port, uint8_t bits)
{
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t reg = {
        .address = port,
        .space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_INPUT_OUTPUT,
        .bit_width = bits,
    };

    return reg;
}

/**
 * @brief Opens a simulated host for a platform in legacy mode, whose firmware sets SCI_EN in PM1a's
 *        control register @p enable_milliseconds after ACPI_ENABLE, and the platform's normalised
 *        FADT: both PM1 groups, both GPE blocks, SMI_CMD and ACPI_ENABLE.
 */
static void mode_open(uint32_t enable_milliseconds)
{
    advanced_configuration_and_power_interface_simulated_host_open(&mode_host);
    (void) advanced_configuration_and_power_interface_simulated_host_set_port(&mode_host, MODE_PM1A_EVENT + 2u,
                                                                              MODE_PM1A_ENABLES);
    (void) advanced_configuration_and_power_interface_simulated_host_set_port(&mode_host, MODE_PM1B_EVENT + 2u,
                                                                              MODE_PM1B_ENABLES);
    mode_host.firmware_command_port = MODE_SMI_COMMAND;
    mode_host.firmware_enable_command = MODE_ENABLE;
    mode_host.firmware_control_port = MODE_PM1A_CONTROL;
    mode_host.firmware_enable_milliseconds = enable_milliseconds;

    memset(&mode_fadt, 0, sizeof(mode_fadt));
    mode_fadt.present = true;
    mode_fadt.revision = 3u;
    mode_fadt.system_control_interrupt = 9u;
    mode_fadt.system_management_interrupt_command = MODE_SMI_COMMAND;
    mode_fadt.enable_command = MODE_ENABLE;
    mode_fadt.power_management_event_blocks[0] = mode_port_register(MODE_PM1A_EVENT, 32u);
    mode_fadt.power_management_event_blocks[1] = mode_port_register(MODE_PM1B_EVENT, 32u);
    mode_fadt.power_management_control_blocks[0] = mode_port_register(MODE_PM1A_CONTROL, 16u);
    mode_fadt.power_management_control_blocks[1] = mode_port_register(MODE_PM1B_CONTROL, 16u);
    mode_fadt.general_purpose_event_blocks[0] = mode_port_register(MODE_GPE0, 64u);
    mode_fadt.general_purpose_event_blocks[1] = mode_port_register(MODE_GPE1, 32u);
    mode_fadt.general_purpose_event_block_lengths[0] = 8u;
    mode_fadt.general_purpose_event_block_lengths[1] = 4u;
}

/**
 * @brief Switches the made-up platform to ACPI mode.
 */
static void mode_enter(void)
{
    advanced_configuration_and_power_interface_enter_acpi_mode(&mode_host.host, &mode_fadt, &mode_switch);
}

/**
 * @brief Whether the log holds every access the host was asked for.
 */
static bool mode_log_is_whole(void)
{
    return mode_host.access_count <= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
}

/**
 * @brief The port writes the log holds.
 */
static uint32_t mode_port_writes(void)
{
    uint32_t writes = 0u;

    for (uint32_t index = 0u; index < mode_host.access_count &&
                              index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
         ++index)
        writes += (mode_host.accesses[index].kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE);
    return writes;
}

/**
 * @brief Where the log holds the write of @p value, @p width bytes wide, to @p port, or MODE_NOT_LOGGED.
 */
static uint32_t mode_write_index(uint16_t port, uint8_t width, uint32_t value)
{
    for (uint32_t index = 0u; index < mode_host.access_count &&
                              index < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_ACCESSES;
         ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceSimulatedAccess_t *access = &mode_host.accesses[index];

        if (access->kind == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_PORT_WRITE && access->port == port &&
            access->width == width && access->value == value)
            return index;
    }
    return MODE_NOT_LOGGED;
}

/**
 * @brief Whether every enable register was written, before the write at @p command: each GPE enable
 *        byte 0, and each PM1 enable register without its fixed-event enables.
 */
static bool mode_enables_cleared_before(uint32_t command)
{
    static const uint16_t general_purpose_enables[] = {0x424u, 0x425u, 0x426u, 0x427u, 0x432u, 0x433u};

    for (uint32_t index = 0u; index < sizeof(general_purpose_enables) / sizeof(general_purpose_enables[0]); ++index)
        if (mode_write_index(general_purpose_enables[index], 1u, 0u) >= command)
            return false;
    return mode_write_index(MODE_PM1A_EVENT + 2u, 2u, MODE_PM1A_LEFT) < command &&
           mode_write_index(MODE_PM1B_EVENT + 2u, 2u, 0u) < command;
}

/**
 * @brief Measures the switch's outcome and counts.
 */
static void mode_measure(KernelTest_t *test)
{
    kernel_test_measure(test, "outcome", mode_switch.outcome);
    kernel_test_measure(test, "enable_registers_cleared", mode_switch.enable_registers_cleared);
    kernel_test_measure(test, "polls", mode_switch.polls);
    kernel_test_measure(test, "waited_ms", mode_switch.waited_milliseconds);
}

/**
 * @brief A platform whose SCI_EN starts clear has its event enables cleared, then ACPI_ENABLE written
 *        to SMI_CMD, and is polled 1 ms apart until its firmware sets SCI_EN.
 */
KERNEL_TEST(a_platform_whose_sci_en_starts_clear_is_switched_once_the_firmware_sets_it)
{
    mode_open(5u);
    mode_enter();

    const uint32_t command = mode_write_index(MODE_SMI_COMMAND, 1u, MODE_ENABLE);

    kernel_test_check(test, mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_SWITCHED,
                      "the platform is switched");
    kernel_test_check(test, mode_log_is_whole() && command != MODE_NOT_LOGGED,
                      "ACPI_ENABLE is written to SMI_CMD, one byte");
    kernel_test_check(test, mode_enables_cleared_before(command),
                      "every GPE enable byte is written 0, and every PM1 fixed-event enable cleared, before it");
    kernel_test_check(test,
                      mode_port_writes() == MODE_ENABLE_WRITES + 1u &&
                          mode_switch.enable_registers_cleared == MODE_ENABLE_WRITES,
                      "nothing else is written: no status register, no control register");
    kernel_test_check(test, mode_switch.polls == 6u && mode_switch.waited_milliseconds == 5u,
                      "SCI_EN is read 6 times, 1 ms apart, and reads set at the sixth");
    kernel_test_check(test, mode_host.clock_milliseconds == 5u, "the host waited 5 ms");
    mode_measure(test);
}

/**
 * @brief SCI_EN counts in PM1b's control register too, where a platform may put it.
 */
KERNEL_TEST(sci_en_counts_in_either_pm1_control_register)
{
    mode_open(0u);
    mode_host.firmware_control_port = MODE_PM1B_CONTROL;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_SWITCHED &&
                          mode_switch.polls == 1u && mode_switch.waited_milliseconds == 0u,
                      "SCI_EN set in PM1b's control register switches the platform at the first read");
    mode_measure(test);
}

/**
 * @brief A platform whose SCI_EN is already set is left as it is: nothing is written.
 */
KERNEL_TEST(a_platform_already_in_acpi_mode_is_left_alone)
{
    mode_open(0u);
    (void) advanced_configuration_and_power_interface_simulated_host_set_port(&mode_host, MODE_PM1A_CONTROL, 0x0001u);
    mode_enter();
    kernel_test_check(test, mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_ALREADY,
                      "the platform is found in ACPI mode");
    kernel_test_check(test, mode_log_is_whole() && mode_port_writes() == 0u && mode_switch.polls == 0u,
                      "nothing is written, and SCI_EN is not polled");
    mode_measure(test);
}

/**
 * @brief A platform without an SMI command port, or without legacy mode, is left as it is.
 */
KERNEL_TEST(a_platform_without_an_smi_command_or_legacy_mode_is_left_alone)
{
    mode_open(0u);
    mode_fadt.system_management_interrupt_command = 0u;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NO_COMMAND_PORT &&
                          mode_log_is_whole() && mode_port_writes() == 0u,
                      "SMI_CMD 0: nothing is written");
    mode_measure(test);

    mode_open(0u);
    mode_fadt.enable_command = 0u;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NO_LEGACY_MODE &&
                          mode_log_is_whole() && mode_port_writes() == 0u,
                      "ACPI_ENABLE 0: nothing is written, not even 0 to SMI_CMD");
    mode_measure(test);
}

/**
 * @brief A hardware-reduced platform boots in ACPI mode: no port is read or written, whatever blocks
 *        its description holds.
 */
KERNEL_TEST(a_hardware_reduced_platform_is_never_touched)
{
    mode_open(0u);
    mode_fadt.hardware_reduced = true;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_HARDWARE_REDUCED &&
                          mode_host.access_count == 0u,
                      "no access is made");
    mode_measure(test);
}

/**
 * @brief Firmware that never sets SCI_EN is given up after 3,000 reads, the platform left in legacy
 *        mode; a tick of 10 ms ends the wait after 3,000 ms instead, 301 reads.
 */
KERNEL_TEST(firmware_that_never_sets_sci_en_is_given_up_after_the_bound)
{
    mode_open(UINT32_MAX);
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_FAILED &&
                          mode_switch.polls == 3000u && mode_switch.waited_milliseconds == 2999u,
                      "3,000 reads, 2,999 ms apart in all, then the platform is left in legacy mode");
    mode_measure(test);

    mode_open(UINT32_MAX);
    mode_host.tick_milliseconds = 10u;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_FAILED &&
                          mode_switch.polls == 301u && mode_switch.waited_milliseconds == 3000u,
                      "on a 10 ms tick, 301 reads: the 3,000 ms the host waited end the wait first");
    mode_measure(test);
}

/**
 * @brief A block outside the I/O space is never written: the switch needs every register it reads or
 *        clears to be a port.
 */
KERNEL_TEST(a_register_outside_the_input_output_space_is_never_written)
{
    mode_open(0u);
    mode_fadt.power_management_control_blocks[0].space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_MEMORY;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_UNREACHABLE &&
                          mode_log_is_whole() && mode_port_writes() == 0u,
                      "a PM1 control block in memory: nothing is written");
    mode_measure(test);

    mode_open(0u);
    mode_fadt.general_purpose_event_blocks[1].address = 0xFFFEu;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_UNREACHABLE &&
                          mode_log_is_whole() && mode_port_writes() == 0u,
                      "a GPE block that reaches past port 0xFFFF: nothing is written");
    mode_measure(test);
}

/**
 * @brief No FADT, no switch.
 */
KERNEL_TEST(no_fadt_is_no_switch)
{
    mode_open(0u);
    mode_fadt.present = false;
    mode_enter();
    kernel_test_check(test,
                      mode_switch.outcome == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NOT_RUN &&
                          mode_host.access_count == 0u,
                      "nothing is accessed");
}
