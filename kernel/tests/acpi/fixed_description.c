#include <kernel/acpi/tables.h>
#include <kernel/testing/test.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

KERNEL_TEST_SUITE(advanced_configuration_and_power_interface_fixed_description, KERNEL_TEST_STAGE_INITIALIZATION);

/**
 * @name Where each field of a FADT lies, from ACPI 6.5's table 5.9
 * @{
 */
#define MADE_UP_FADT_SCI_INT           46u  /**< SCI_INT, 2 bytes. */
#define MADE_UP_FADT_SMI_CMD           48u  /**< SMI_CMD, 4 bytes. */
#define MADE_UP_FADT_ACPI_ENABLE       52u  /**< ACPI_ENABLE, 1 byte. */
#define MADE_UP_FADT_PSTATE_CNT        55u  /**< PSTATE_CNT, 1 byte, reserved in ACPI 1.0. */
#define MADE_UP_FADT_PM1A_EVT_BLK      56u  /**< PM1a_EVT_BLK, 4 bytes. */
#define MADE_UP_FADT_PM1B_EVT_BLK      60u  /**< PM1b_EVT_BLK, 4 bytes. */
#define MADE_UP_FADT_PM1A_CNT_BLK      64u  /**< PM1a_CNT_BLK, 4 bytes. */
#define MADE_UP_FADT_PM1B_CNT_BLK      68u  /**< PM1b_CNT_BLK, 4 bytes. */
#define MADE_UP_FADT_PM_TMR_BLK        76u  /**< PM_TMR_BLK, 4 bytes. */
#define MADE_UP_FADT_GPE0_BLK          80u  /**< GPE0_BLK, 4 bytes. */
#define MADE_UP_FADT_PM1_EVT_LEN       88u  /**< PM1_EVT_LEN, 1 byte. */
#define MADE_UP_FADT_PM1_CNT_LEN       89u  /**< PM1_CNT_LEN, 1 byte. */
#define MADE_UP_FADT_PM_TMR_LEN        91u  /**< PM_TMR_LEN, 1 byte. */
#define MADE_UP_FADT_GPE0_BLK_LEN      92u  /**< GPE0_BLK_LEN, 1 byte. */
#define MADE_UP_FADT_GPE1_BASE         94u  /**< GPE1_BASE, 1 byte. */
#define MADE_UP_FADT_CST_CNT           95u  /**< CST_CNT, 1 byte, reserved in ACPI 1.0. */
#define MADE_UP_FADT_FLAGS             112u /**< Flags, 4 bytes. */
#define MADE_UP_FADT_RESET_REG         116u /**< RESET_REG, a Generic Address Structure. */
#define MADE_UP_FADT_RESET_VALUE       128u /**< RESET_VALUE, 1 byte. */
#define MADE_UP_FADT_MINOR_VERSION     131u /**< FADT Minor Version, 1 byte. */
#define MADE_UP_FADT_X_PM1A_EVT_BLK    148u /**< X_PM1a_EVT_BLK, a Generic Address Structure. */
#define MADE_UP_FADT_X_PM1B_EVT_BLK    160u /**< X_PM1b_EVT_BLK, a Generic Address Structure. */
#define MADE_UP_FADT_X_PM1A_CNT_BLK    172u /**< X_PM1a_CNT_BLK, a Generic Address Structure. */
#define MADE_UP_FADT_X_PM_TMR_BLK      208u /**< X_PM_TMR_BLK, a Generic Address Structure. */
#define MADE_UP_FADT_X_GPE0_BLK        220u /**< X_GPE0_BLK, a Generic Address Structure. */
#define MADE_UP_FADT_X_GPE1_BLK        232u /**< X_GPE1_BLK, a Generic Address Structure. */
#define MADE_UP_FADT_SLEEP_CONTROL_REG 244u /**< SLEEP_CONTROL_REG, a Generic Address Structure. */
#define MADE_UP_FADT_SLEEP_STATUS_REG  256u /**< SLEEP_STATUS_REG, a Generic Address Structure. */
/** @} */

/**
 * @name The flags the made-up FADTs set
 * @{
 */
#define MADE_UP_FADT_RESET_REG_SUP        (1u << 10u) /**< RESET_REG_SUP: RESET_REG resets the platform. */
#define MADE_UP_FADT_HW_REDUCED_ACPI      (1u << 20u) /**< HW_REDUCED_ACPI: no fixed hardware. */
#define MADE_UP_FADT_IGNORED_WHEN_REDUCED 0x0003618Eu /**< Bits 1, 2, 3, 7, 8, 13, 14, 16, 17: ignored if reduced. */
#define MADE_UP_FADT_KEPT_WHEN_REDUCED    0x00000071u /**< Bits 0, 4, 5 and 6: kept if reduced. */
/** @} */

#define MADE_UP_FADT_CAPACITY 512u  /**< Bytes of the buffer a made-up FADT is written in. */
#define MADE_UP_FADT_GARBAGE  0xA5u /**< What every byte past a made-up FADT's length holds. */
#define MADE_UP_FADT_IO       1u    /**< The I/O space of a Generic Address Structure. */
#define MADE_UP_FADT_MEMORY   0u    /**< The memory space of a Generic Address Structure. */

static uint8_t made_up_fadt[MADE_UP_FADT_CAPACITY];
static AdvancedConfigurationAndPowerInterfaceFixedDescription_t made_up_fadt_description;

/**
 * @brief Starts a FADT of @p length bytes whose header says @p revision; every byte past its length
 *        holds MADE_UP_FADT_GARBAGE, so that a read past it shows.
 */
static void made_up_fadt_begin(uint32_t length, uint8_t revision)
{
    AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        (AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) made_up_fadt;

    memset(made_up_fadt, MADE_UP_FADT_GARBAGE, sizeof(made_up_fadt));
    memset(made_up_fadt, 0, length);
    memcpy(header->signature, "FACP", sizeof(header->signature));
    header->length = length;
    header->revision = revision;
}

/**
 * @brief Stores @p value little-endian in @p width bytes at @p offset of the FADT.
 */
static void made_up_fadt_put(uint32_t offset, uint32_t width, uint64_t value)
{
    for (uint32_t index = 0u; index < width; ++index)
        made_up_fadt[offset + index] = (uint8_t) (value >> (8u * index));
}

/**
 * @brief Stores a Generic Address Structure at @p offset of the FADT.
 */
static void made_up_fadt_put_register(uint32_t offset, uint8_t space, uint8_t bit_width, uint8_t access_size,
                                      uint64_t address)
{
    made_up_fadt_put(offset, 1u, space);
    made_up_fadt_put(offset + 1u, 1u, bit_width);
    made_up_fadt_put(offset + 2u, 1u, 0u);
    made_up_fadt_put(offset + 3u, 1u, access_size);
    made_up_fadt_put(offset + 4u, 8u, address);
}

/**
 * @brief Normalises the FADT.
 *
 * @return Whether it was read.
 */
static bool made_up_fadt_read(void)
{
    return advanced_configuration_and_power_interface_read_fixed_description(
        (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) made_up_fadt, &made_up_fadt_description);
}

/**
 * @brief Whether @p reg is the register in @p space, @p bit_width bits wide from bit 0, accessed as
 *        @p access_size says, at @p address.
 */
static bool made_up_fadt_register_is(const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *reg, uint8_t space,
                                     uint8_t bit_width, uint8_t access_size, uint64_t address)
{
    return reg->space == space && reg->bit_width == bit_width && reg->bit_offset == 0u &&
           reg->access_size == access_size && reg->address == address;
}

/**
 * @brief Whether @p reg reads absent: every byte 0.
 */
static bool made_up_fadt_register_is_absent(const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *reg)
{
    return made_up_fadt_register_is(reg, 0u, 0u, 0u, 0u);
}

/**
 * @brief Writes the legacy fields every made-up FADT of a platform with fixed hardware gives.
 */
static void made_up_fadt_put_legacy_fields(void)
{
    made_up_fadt_put(MADE_UP_FADT_SCI_INT, 2u, 9u);
    made_up_fadt_put(MADE_UP_FADT_SMI_CMD, 4u, 0xB2u);
    made_up_fadt_put(MADE_UP_FADT_ACPI_ENABLE, 1u, 0xF1u);
    made_up_fadt_put(MADE_UP_FADT_PSTATE_CNT, 1u, 0x80u);
    made_up_fadt_put(MADE_UP_FADT_CST_CNT, 1u, 0x85u);
    made_up_fadt_put(MADE_UP_FADT_PM1A_EVT_BLK, 4u, 0x400u);
    made_up_fadt_put(MADE_UP_FADT_PM1A_CNT_BLK, 4u, 0x404u);
    made_up_fadt_put(MADE_UP_FADT_PM1B_CNT_BLK, 4u, 0x460u);
    made_up_fadt_put(MADE_UP_FADT_PM_TMR_BLK, 4u, 0x408u);
    made_up_fadt_put(MADE_UP_FADT_GPE0_BLK, 4u, 0x420u);
    made_up_fadt_put(MADE_UP_FADT_PM1_EVT_LEN, 1u, 4u);
    made_up_fadt_put(MADE_UP_FADT_PM1_CNT_LEN, 1u, 2u);
    made_up_fadt_put(MADE_UP_FADT_PM_TMR_LEN, 1u, 4u);
    made_up_fadt_put(MADE_UP_FADT_GPE0_BLK_LEN, 1u, 16u);
    made_up_fadt_put(MADE_UP_FADT_GPE1_BASE, 1u, 0x10u);
}

/**
 * @brief Whether the description holds the legacy blocks made_up_fadt_put_legacy_fields wrote, each
 *        converted to the I/O space, as wide as its length.
 */
static bool made_up_fadt_legacy_blocks_are_converted(void)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    return made_up_fadt_register_is(&fadt->power_management_event_blocks[0], MADE_UP_FADT_IO, 32u, 0u, 0x400u) &&
           made_up_fadt_register_is_absent(&fadt->power_management_event_blocks[1]) &&
           made_up_fadt_register_is(&fadt->power_management_control_blocks[0], MADE_UP_FADT_IO, 16u, 0u, 0x404u) &&
           made_up_fadt_register_is(&fadt->power_management_control_blocks[1], MADE_UP_FADT_IO, 16u, 0u, 0x460u) &&
           made_up_fadt_register_is(&fadt->power_management_timer_block, MADE_UP_FADT_IO, 32u, 0u, 0x408u) &&
           made_up_fadt_register_is(&fadt->general_purpose_event_blocks[0], MADE_UP_FADT_IO, 128u, 0u, 0x420u) &&
           made_up_fadt_register_is_absent(&fadt->general_purpose_event_blocks[1]) &&
           fadt->general_purpose_event_block_lengths[0] == 16u && fadt->general_purpose_event_block_lengths[1] == 0u;
}

/**
 * @brief A FADT of ACPI 1.0, 116 bytes long, reads nothing past them: its legacy blocks become
 *        Generic Address Structures in the I/O space, the bytes ACPI 1.0 reserved read 0, and the
 *        reset register its flags claim reads absent.
 */
KERNEL_TEST(a_revision_1_fadt_reads_nothing_past_its_116_bytes)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    made_up_fadt_begin(116u, 1u);
    made_up_fadt_put_legacy_fields();
    made_up_fadt_put(MADE_UP_FADT_FLAGS, 4u, MADE_UP_FADT_RESET_REG_SUP | 0x2Du);

    kernel_test_check(test, made_up_fadt_read() && fadt->present, "the FADT is read");
    kernel_test_check(test, fadt->length == 116u && fadt->revision == 1u, "116 bytes are revision 1");
    kernel_test_check(test,
                      fadt->system_control_interrupt == 9u && fadt->system_management_interrupt_command == 0xB2u &&
                          fadt->enable_command == 0xF1u && fadt->general_purpose_event_1_base == 0x10u,
                      "SCI_INT, SMI_CMD, ACPI_ENABLE and GPE1_BASE are read");
    kernel_test_check(test, fadt->performance_state_command == 0u && fadt->idle_state_command == 0u,
                      "PSTATE_CNT and CST_CNT, which ACPI 1.0 reserved, read 0");
    kernel_test_check(test, made_up_fadt_legacy_blocks_are_converted(),
                      "each legacy block is a Generic Address Structure in the I/O space, as wide as its length");
    kernel_test_check(test, made_up_fadt_register_is_absent(&fadt->reset_register) && fadt->reset_value == 0u,
                      "the reset register, past the FADT's end, reads absent although RESET_REG_SUP is set");
    kernel_test_check(test,
                      made_up_fadt_register_is_absent(&fadt->sleep_control_register) &&
                          made_up_fadt_register_is_absent(&fadt->sleep_status_register),
                      "the sleep registers, past the FADT's end, read absent");
    kernel_test_measure(test, "revision", fadt->revision);
}

/**
 * @brief A FADT of 132 bytes is revision 2, whatever its header says: it holds the reset register,
 *        and still reads 0 in the bytes ACPI 1.0 reserved.
 */
KERNEL_TEST(a_revision_2_fadt_reads_its_reset_register_and_not_the_reserved_bytes)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    made_up_fadt_begin(132u, 3u);
    made_up_fadt_put_legacy_fields();
    made_up_fadt_put(MADE_UP_FADT_FLAGS, 4u, MADE_UP_FADT_RESET_REG_SUP);
    made_up_fadt_put_register(MADE_UP_FADT_RESET_REG, MADE_UP_FADT_IO, 8u, 1u, 0xCF9u);
    made_up_fadt_put(MADE_UP_FADT_RESET_VALUE, 1u, 6u);

    kernel_test_check(test, made_up_fadt_read() && fadt->revision == 2u, "132 bytes are revision 2, not the 3 claimed");
    kernel_test_check(test,
                      made_up_fadt_register_is(&fadt->reset_register, MADE_UP_FADT_IO, 8u, 1u, 0xCF9u) &&
                          fadt->reset_value == 6u,
                      "the reset register and its value are read");
    kernel_test_check(test, fadt->performance_state_command == 0u && fadt->idle_state_command == 0u,
                      "PSTATE_CNT and CST_CNT still read 0");
    kernel_test_check(test, made_up_fadt_legacy_blocks_are_converted(),
                      "the extended blocks, past the FADT's end, leave the legacy ones in place");
    kernel_test_measure(test, "revision", fadt->revision);
}

/**
 * @brief A FADT of revision 3 takes an extended block over its legacy one, completes an extended block
 *        that gives no width, and reads PSTATE_CNT and CST_CNT.
 */
KERNEL_TEST(a_revision_3_fadt_prefers_its_extended_blocks)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    for (uint8_t claimed = 3u; claimed <= 5u; claimed = (uint8_t) (claimed + 2u))
    {
        made_up_fadt_begin(244u, claimed);
        made_up_fadt_put_legacy_fields();
        made_up_fadt_put(MADE_UP_FADT_GPE0_BLK_LEN, 1u, 32u);
        made_up_fadt_put_register(MADE_UP_FADT_X_PM1A_EVT_BLK, MADE_UP_FADT_IO, 32u, 2u, 0x1800u);
        made_up_fadt_put_register(MADE_UP_FADT_X_PM_TMR_BLK, MADE_UP_FADT_IO, 32u, 3u, 0x1808u);
        made_up_fadt_put_register(MADE_UP_FADT_X_GPE0_BLK, MADE_UP_FADT_IO, 0u, 1u, 0x1880u);
        made_up_fadt_put_register(MADE_UP_FADT_X_GPE1_BLK, MADE_UP_FADT_MEMORY, 64u, 1u, 0xFED40000u);

        kernel_test_check(test, made_up_fadt_read() && fadt->revision == 3u,
                          (claimed == 3u) ? "244 bytes are revision 3" : "244 bytes are revision 3, not the 5 claimed");
    }
    kernel_test_check(
        test,
        made_up_fadt_register_is(&fadt->power_management_event_blocks[0], MADE_UP_FADT_IO, 32u, 2u, 0x1800u) &&
            made_up_fadt_register_is(&fadt->power_management_timer_block, MADE_UP_FADT_IO, 32u, 3u, 0x1808u),
        "an extended block that gives an address wins over its legacy block, which names another");
    kernel_test_check(
        test, made_up_fadt_register_is(&fadt->power_management_control_blocks[0], MADE_UP_FADT_IO, 16u, 0u, 0x404u),
        "a legacy block whose extended block is 0 is converted");
    kernel_test_check(
        test,
        made_up_fadt_register_is(&fadt->general_purpose_event_blocks[0], MADE_UP_FADT_IO, 255u, 1u, 0x1880u) &&
            fadt->general_purpose_event_block_lengths[0] == 32u,
        "an extended block of width 0 takes its legacy length, capped at 255 bits, which the GPE "
        "length keeps whole");
    kernel_test_check(
        test,
        made_up_fadt_register_is(&fadt->general_purpose_event_blocks[1], MADE_UP_FADT_MEMORY, 64u, 1u, 0xFED40000u) &&
            fadt->general_purpose_event_block_lengths[1] == 8u,
        "an extended block without a legacy length gives the GPE length from its width");
    kernel_test_check(test, fadt->performance_state_command == 0x80u && fadt->idle_state_command == 0x85u,
                      "PSTATE_CNT and CST_CNT are read from revision 3");
    kernel_test_check(test,
                      made_up_fadt_register_is_absent(&fadt->sleep_control_register) &&
                          made_up_fadt_register_is_absent(&fadt->sleep_status_register),
                      "the sleep registers, past the FADT's end, read absent");
}

/**
 * @brief The reset register is read only when RESET_REG_SUP says it resets the platform.
 */
KERNEL_TEST(the_reset_register_needs_its_supported_flag)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    made_up_fadt_begin(244u, 3u);
    made_up_fadt_put_register(MADE_UP_FADT_RESET_REG, MADE_UP_FADT_IO, 8u, 1u, 0xCF9u);
    made_up_fadt_put(MADE_UP_FADT_RESET_VALUE, 1u, 6u);
    kernel_test_check(
        test, made_up_fadt_read() && made_up_fadt_register_is_absent(&fadt->reset_register) && fadt->reset_value == 0u,
        "without RESET_REG_SUP, the reset register and its value read absent");
    made_up_fadt_put(MADE_UP_FADT_FLAGS, 4u, MADE_UP_FADT_RESET_REG_SUP);
    kernel_test_check(test,
                      made_up_fadt_read() &&
                          made_up_fadt_register_is(&fadt->reset_register, MADE_UP_FADT_IO, 8u, 1u, 0xCF9u) &&
                          fadt->reset_value == 6u,
                      "with RESET_REG_SUP, they are read");
}

/**
 * @brief A FADT of ACPI 5.0 reads its sleep registers; a hardware-reduced one keeps them and its reset
 *        register, and reads 0 in every fixed-hardware field and flag ACPI tells OSPM to ignore.
 */
KERNEL_TEST(a_revision_5_fadt_reads_no_fixed_hardware_when_it_is_hardware_reduced)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    for (uint32_t reduced = 0u; reduced < 2u; ++reduced)
    {
        made_up_fadt_begin(268u, 5u);
        made_up_fadt_put_legacy_fields();
        made_up_fadt_put(MADE_UP_FADT_FLAGS, 4u,
                         MADE_UP_FADT_RESET_REG_SUP | MADE_UP_FADT_IGNORED_WHEN_REDUCED |
                             MADE_UP_FADT_KEPT_WHEN_REDUCED | ((reduced != 0u) ? MADE_UP_FADT_HW_REDUCED_ACPI : 0u));
        made_up_fadt_put_register(MADE_UP_FADT_RESET_REG, MADE_UP_FADT_IO, 8u, 1u, 0xCF9u);
        made_up_fadt_put(MADE_UP_FADT_RESET_VALUE, 1u, 6u);
        made_up_fadt_put_register(MADE_UP_FADT_X_PM1A_CNT_BLK, MADE_UP_FADT_IO, 16u, 2u, 0x1804u);
        made_up_fadt_put(MADE_UP_FADT_PM1B_EVT_BLK, 4u, 0x440u);
        made_up_fadt_put_register(MADE_UP_FADT_X_PM1B_EVT_BLK, MADE_UP_FADT_IO, 32u, 2u, 0x1840u);
        made_up_fadt_put_register(MADE_UP_FADT_X_GPE1_BLK, MADE_UP_FADT_IO, 64u, 1u, 0x18A0u);
        made_up_fadt_put_register(MADE_UP_FADT_SLEEP_CONTROL_REG, MADE_UP_FADT_MEMORY, 8u, 1u, 0xFED80000u);
        made_up_fadt_put_register(MADE_UP_FADT_SLEEP_STATUS_REG, MADE_UP_FADT_MEMORY, 8u, 1u, 0xFED80001u);
        if (!kernel_test_check(test, made_up_fadt_read() && fadt->revision == 5u, "268 bytes are revision 5"))
            return;
        kernel_test_check(
            test,
            made_up_fadt_register_is(&fadt->sleep_control_register, MADE_UP_FADT_MEMORY, 8u, 1u, 0xFED80000u) &&
                made_up_fadt_register_is(&fadt->sleep_status_register, MADE_UP_FADT_MEMORY, 8u, 1u, 0xFED80001u),
            "the sleep registers are read");
        kernel_test_check(test,
                          made_up_fadt_register_is(&fadt->reset_register, MADE_UP_FADT_IO, 8u, 1u, 0xCF9u) &&
                              fadt->reset_value == 6u,
                          "the reset register is read");
    }
    kernel_test_check(test, fadt->hardware_reduced, "HW_REDUCED_ACPI is read");
    kernel_test_check(test,
                      fadt->system_control_interrupt == 0u && fadt->system_management_interrupt_command == 0u &&
                          fadt->enable_command == 0u && fadt->performance_state_command == 0u &&
                          fadt->idle_state_command == 0u && fadt->general_purpose_event_1_base == 0u,
                      "SCI_INT, SMI_CMD, ACPI_ENABLE, PSTATE_CNT, CST_CNT and GPE1_BASE read 0");
    kernel_test_check(test,
                      made_up_fadt_register_is_absent(&fadt->power_management_event_blocks[0]) &&
                          made_up_fadt_register_is_absent(&fadt->power_management_event_blocks[1]) &&
                          made_up_fadt_register_is_absent(&fadt->general_purpose_event_blocks[1]) &&
                          made_up_fadt_register_is_absent(&fadt->power_management_control_blocks[0]) &&
                          made_up_fadt_register_is_absent(&fadt->power_management_control_blocks[1]) &&
                          made_up_fadt_register_is_absent(&fadt->power_management_timer_block) &&
                          made_up_fadt_register_is_absent(&fadt->general_purpose_event_blocks[0]) &&
                          fadt->general_purpose_event_block_lengths[0] == 0u,
                      "every PM1, PM timer and GPE block, legacy or extended, reads absent");
    kernel_test_check(test,
                      fadt->flags ==
                          (MADE_UP_FADT_HW_REDUCED_ACPI | MADE_UP_FADT_RESET_REG_SUP | MADE_UP_FADT_KEPT_WHEN_REDUCED),
                      "the flags a hardware-reduced platform ignores read 0, and the others are kept");
}

/**
 * @brief A FADT of ACPI 6.5, 276 bytes long, is revision 6; a longer one is read to its first 276
 *        bytes, and is revision 6 too.
 */
KERNEL_TEST(a_revision_6_5_fadt_is_read_to_its_276_bytes)
{
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt = &made_up_fadt_description;

    for (uint32_t length = 276u; length <= 300u; length += 24u)
    {
        made_up_fadt_begin(length, 6u);
        made_up_fadt_put_legacy_fields();
        made_up_fadt_put(MADE_UP_FADT_MINOR_VERSION, 1u, 5u);
        made_up_fadt_put_register(MADE_UP_FADT_X_PM1A_CNT_BLK, MADE_UP_FADT_IO, 16u, 2u, 0x1804u);
        made_up_fadt_put_register(MADE_UP_FADT_SLEEP_STATUS_REG, MADE_UP_FADT_IO, 8u, 1u, 0x1801u);
        kernel_test_check(test, made_up_fadt_read() && fadt->length == length && fadt->revision == 6u,
                          (length == 276u) ? "276 bytes are revision 6" : "300 bytes are revision 6");
        kernel_test_check(
            test,
            made_up_fadt_register_is(&fadt->power_management_control_blocks[0], MADE_UP_FADT_IO, 16u, 2u, 0x1804u) &&
                made_up_fadt_register_is(&fadt->sleep_status_register, MADE_UP_FADT_IO, 8u, 1u, 0x1801u),
            "its extended blocks and its sleep status register are read");
    }
}

/**
 * @brief No FADT gives a description that is not present.
 */
KERNEL_TEST(no_fadt_gives_no_fixed_description)
{
    made_up_fadt_description.present = true;
    kernel_test_check(
        test,
        !advanced_configuration_and_power_interface_read_fixed_description(NULL, &made_up_fadt_description) &&
            !made_up_fadt_description.present,
        "a NULL FADT is refused, and the description is not present");
}

/**
 * @brief A register is an I/O port only when it lies wholly in the I/O space.
 */
KERNEL_TEST(a_register_is_a_port_only_when_it_lies_wholly_in_the_input_output_space)
{
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t control = {.address = 0x404u, .space = 1u};
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t last = {.address = 0xFFFEu, .space = 1u};
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t past = {.address = 0xFFFFu, .space = 1u};
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t memory = {.address = 0x404u, .space = 0u};
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t none = {.address = 0u, .space = 1u};

    kernel_test_check(test, advanced_configuration_and_power_interface_input_output_port(&control, 2u) == 0x404u,
                      "a register in the I/O space is its port");
    kernel_test_check(test, advanced_configuration_and_power_interface_input_output_port(&last, 2u) == 0xFFFEu,
                      "two bytes from 0xFFFE end on the last port");
    kernel_test_check(test, advanced_configuration_and_power_interface_input_output_port(&past, 2u) == 0u,
                      "two bytes from 0xFFFF reach past the last port");
    kernel_test_check(test, advanced_configuration_and_power_interface_input_output_port(&memory, 2u) == 0u,
                      "a register in memory is no port");
    kernel_test_check(test, advanced_configuration_and_power_interface_input_output_port(&none, 2u) == 0u,
                      "a register at 0 is absent");
}
