#include <kernel/acpi/host.h>
#include <kernel/acpi/tables.h>

#include <string.h>

#define ACPI_MADT_SIGNATURE "APIC"
#define ACPI_RSDT_SIGNATURE "RSDT"
#define ACPI_XSDT_SIGNATURE "XSDT" /**< The root table of 64-bit entries, ACPI 2.0 and later. */
#define ACPI_FADT_SIGNATURE "FACP" /**< The FADT, which names the DSDT and the FACS. */

#define ACPI_TABLE_MAXIMUM_LENGTH 0x01000000u /**< A longer table is taken for a corrupt length. */

#define ACPI_FADT_FIRMWARE_CONTROL_OFFSET          36u  /**< FIRMWARE_CTRL: the FACS, 32 bits. */
#define ACPI_FADT_DSDT_OFFSET                      40u  /**< DSDT: the DSDT, 32 bits. */
#define ACPI_FADT_EXTENDED_FIRMWARE_CONTROL_OFFSET 132u /**< X_FIRMWARE_CTRL: the FACS, 64 bits. */
#define ACPI_FADT_EXTENDED_DSDT_OFFSET             140u /**< X_DSDT: the DSDT, 64 bits. */

/**
 * @name Where the fields of a FADT the normalisation reads lie
 * @{
 */
#define ACPI_FADT_SYSTEM_CONTROL_INTERRUPT_OFFSET  46u  /**< SCI_INT, 16 bits. */
#define ACPI_FADT_SYSTEM_MANAGEMENT_COMMAND_OFFSET 48u  /**< SMI_CMD, 32 bits. */
#define ACPI_FADT_ENABLE_COMMAND_OFFSET            52u  /**< ACPI_ENABLE, 8 bits. */
#define ACPI_FADT_PERFORMANCE_STATE_COMMAND_OFFSET 55u  /**< PSTATE_CNT, 8 bits, reserved in ACPI 1.0. */
#define ACPI_FADT_GENERAL_PURPOSE_EVENT_1_BASE     94u  /**< GPE1_BASE, 8 bits. */
#define ACPI_FADT_IDLE_STATE_COMMAND_OFFSET        95u  /**< CST_CNT, 8 bits, reserved in ACPI 1.0. */
#define ACPI_FADT_FLAGS_OFFSET                     112u /**< Flags, 32 bits. */
#define ACPI_FADT_RESET_REGISTER_OFFSET            116u /**< RESET_REG, a Generic Address Structure. */
#define ACPI_FADT_RESET_VALUE_OFFSET               128u /**< RESET_VALUE, 8 bits. */
#define ACPI_FADT_SLEEP_CONTROL_OFFSET             244u /**< SLEEP_CONTROL_REG, a Generic Address Structure. */
#define ACPI_FADT_SLEEP_STATUS_OFFSET              256u /**< SLEEP_STATUS_REG, a Generic Address Structure. */
#define ACPI_FADT_FIXED_HARDWARE_FIRST             46u  /**< The first byte a hardware-reduced platform ignores. */
#define ACPI_FADT_FIXED_HARDWARE_END               109u /**< The byte past the last of them before the flags. */
#define ACPI_FADT_EXTENDED_FIXED_HARDWARE_FIRST    148u /**< The first extended block it ignores, X_PM1a_EVT_BLK. */
#define ACPI_FADT_EXTENDED_FIXED_HARDWARE_END      244u /**< The byte past the last one, X_GPE1_BLK. */
/** @} */

/**
 * @name The longest FADT of each revision
 * @{
 */
#define ACPI_FADT_REVISION_1_LENGTH 116u /**< ACPI 1.0. */
#define ACPI_FADT_REVISION_2_LENGTH 132u /**< ACPI 1.0 with the reset register. */
#define ACPI_FADT_REVISION_3_LENGTH 244u /**< ACPI 2.0 to 4.0. */
#define ACPI_FADT_REVISION_5_LENGTH 268u /**< ACPI 5.0 and 5.1, with the sleep registers. */
/** @} */

/**
 * @brief Where a register block of a FADT lies: its 32-bit address, the byte that gives its length,
 *        and its Generic Address Structure.
 */
typedef struct AcpiFadtBlock {
    uint8_t legacy_offset;   /**< Its 32-bit address in the I/O space. */
    uint8_t length_offset;   /**< The byte that gives its length. */
    uint8_t extended_offset; /**< Its Generic Address Structure. */
} AcpiFadtBlock_t;

/**
 * @brief The register blocks of a FADT, in the order of acpi_fadt_blocks.
 */
typedef enum AcpiFadtBlockIndex {
    ACPI_FADT_PM1A_EVENT = 0,   /**< PM1a_EVT_BLK. */
    ACPI_FADT_PM1B_EVENT = 1,   /**< PM1b_EVT_BLK. */
    ACPI_FADT_PM1A_CONTROL = 2, /**< PM1a_CNT_BLK. */
    ACPI_FADT_PM1B_CONTROL = 3, /**< PM1b_CNT_BLK. */
    ACPI_FADT_PM_TIMER = 4,     /**< PM_TMR_BLK. */
    ACPI_FADT_GPE0 = 5,         /**< GPE0_BLK. */
    ACPI_FADT_GPE1 = 6,         /**< GPE1_BLK. */
    ACPI_FADT_BLOCKS = 7,       /**< How many there are. */
} AcpiFadtBlockIndex_t;

static const AcpiFadtBlock_t acpi_fadt_blocks[ACPI_FADT_BLOCKS] = {
    {56u, 88u, 148u}, /* PM1a_EVT_BLK, PM1_EVT_LEN, X_PM1a_EVT_BLK */
    {60u, 88u, 160u}, /* PM1b_EVT_BLK, PM1_EVT_LEN, X_PM1b_EVT_BLK */
    {64u, 89u, 172u}, /* PM1a_CNT_BLK, PM1_CNT_LEN, X_PM1a_CNT_BLK */
    {68u, 89u, 184u}, /* PM1b_CNT_BLK, PM1_CNT_LEN, X_PM1b_CNT_BLK */
    {76u, 91u, 208u}, /* PM_TMR_BLK, PM_TMR_LEN, X_PM_TMR_BLK */
    {80u, 92u, 220u}, /* GPE0_BLK, GPE0_BLK_LEN, X_GPE0_BLK */
    {84u, 93u, 232u}, /* GPE1_BLK, GPE1_BLK_LEN, X_GPE1_BLK */
};

#define ACPI_FADT_HARDWARE_REDUCED         (1u << 20u) /**< Flags bit HW_REDUCED_ACPI. */
#define ACPI_FADT_RESET_REGISTER_SUPPORTED (1u << 10u) /**< Flags bit RESET_REG_SUP. */
#define ACPI_FADT_FLAGS_IGNORED_WHEN_REDUCED                                                                           \
    0x0003618Eu /**< Flags bits 1, 2, 3, 7, 8, 13, 14, 16 and 17, which a hardware-reduced platform ignores. */
#define ACPI_GENERIC_ADDRESS_MAXIMUM_WIDTH   255u    /**< The widest register a Generic Address Structure holds. */
#define ACPI_SYSTEM_CONTROL_INTERRUPT_ENABLE 0x0001u /**< PM1 control bit SCI_EN: the OS owns the fixed hardware. */
#define ACPI_FIXED_EVENT_ENABLES                                                                                       \
    0x0721u /**< PM1 enable bits TMR_EN, GBL_EN, PWRBTN_EN, SLPBTN_EN and RTC_EN; bit 14, PCIEXP_WAKE_DIS, is no event \
               enable. */
#define ACPI_MODE_POLL_MILLISECONDS 1u /**< What the switch to ACPI mode sleeps between two reads of SCI_EN. */

/**
 * @brief A run of I/O ports the ACPI layer never reaches, since the kernel drives them (§5.3).
 */
typedef struct AcpiPortRange {
    uint16_t first; /**< Its first port. */
    uint16_t last;  /**< Its last port. */
} AcpiPortRange_t;

static const AcpiPortRange_t acpi_protected_ports[] = {
    {0x0020u, 0x0021u}, /* the master PIC */
    {0x00A0u, 0x00A1u}, /* the slave PIC */
    {0x04D0u, 0x04D1u}, /* the ELCR */
    {0x0CF8u, 0x0CFFu}, /* PCI configuration mechanism #1 */
};

/**
 * @brief The ports the switch to ACPI mode reads and writes, each 0 for a block the FADT does not give.
 */
typedef struct AcpiModeRegisters {
    uint16_t control_ports[2];                 /**< PM1a_CNT and PM1b_CNT, where SCI_EN is read. */
    uint16_t event_enable_ports[2];            /**< PM1a_EN and PM1b_EN, the second half of each PM1 event block. */
    uint8_t event_enable_widths[2];            /**< Their width in bytes, 1 or 2. */
    uint16_t general_purpose_enable_ports[2];  /**< GPE0_EN and GPE1_EN, the second half of each GPE block. */
    uint8_t general_purpose_enable_lengths[2]; /**< Their length in bytes, each written alone. */
} AcpiModeRegisters_t;

static const char *const acpi_mode_outcome_names[] = {
    "not-run", "hardware-reduced", "unreachable", "already", "no-command-port", "no-legacy-mode", "switched", "failed",
};
#define ACPI_INPUT_OUTPUT_PORTS 0x10000u /**< Ports in the I/O space. */

/**
 * @name The MADT entry types this reader reads
 * @{
 */
#define ACPI_MADT_LOCAL_APIC                          0x0u /**< Processor Local APIC. */
#define ACPI_MADT_IO_APIC                             0x1u /**< I/O APIC. */
#define ACPI_MADT_INTERRUPT_SOURCE_OVERRIDE           0x2u /**< Interrupt Source Override. */
#define ACPI_MADT_LOCAL_APIC_NON_MASKABLE_INTERRUPT   0x4u /**< Local APIC NMI. */
#define ACPI_MADT_LOCAL_APIC_ADDRESS                  0x5u /**< Local APIC Address Override. */
#define ACPI_MADT_LOCAL_X2APIC                        0x9u /**< Processor Local x2APIC. */
#define ACPI_MADT_LOCAL_X2APIC_NON_MASKABLE_INTERRUPT 0xAu /**< Local x2APIC NMI. */
/** @} */

#define ACPI_MADT_INVALID_APIC_ID   0xFFu       /**< A local APIC ID that names no processor. */
#define ACPI_MADT_INVALID_X2APIC_ID 0xFFFFFFFFu /**< An x2APIC ID that names no processor. */

static AdvancedConfigurationAndPowerInterfaceMadtState_t acpi_madt = {.state_name = "acpi-not-initialized"};
static AdvancedConfigurationAndPowerInterfaceDirectory_t acpi_directory;
static bool acpi_directory_read = false;
static AdvancedConfigurationAndPowerInterfaceModeSwitch_t acpi_mode;

static uint8_t acpi_signature_matches(const char *lhs, const char *rhs, size_t length)
{
    if (!lhs || !rhs)
        return 0u;

    for (size_t i = 0; i < length; ++i)
    {
        if (lhs[i] != rhs[i])
            return 0u;
    }
    return 1u;
}

/**
 * @brief Maps every byte of the table at @p physical_address through a firmware window of @p host:
 *        its header first, for its length, then the whole of it.
 *
 * @return Its header, or NULL when it cannot be mapped, or its length is shorter than a header or
 *         longer than ACPI_TABLE_MAXIMUM_LENGTH.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
acpi_map_table(const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t physical_address)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header =
        host->map_firmware(host->context, physical_address, sizeof(*header));

    if (header == NULL)
        return NULL;

    const uint32_t length = header->length;

    host->unmap_firmware(host->context, header, sizeof(*header));
    if (length < sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) || length > ACPI_TABLE_MAXIMUM_LENGTH)
        return NULL;

    return host->map_firmware(host->context, physical_address, length);
}

/**
 * @brief Unmaps a table acpi_map_table mapped through @p host.
 */
static void acpi_unmap_table(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                             const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    host->unmap_firmware(host->context, header, header->length);
}

/**
 * @brief Records the table at @p physical_address in @p directory, mapped and checksummed.
 *
 * @details A table past the directory's capacity, or that cannot be mapped, is counted unreachable.
 */
static void acpi_record_table(AdvancedConfigurationAndPowerInterfaceDirectory_t *directory, uint32_t physical_address)
{
    if (directory->table_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_TABLE_COUNT)
    {
        ++directory->unreachable_count;
        return;
    }

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = acpi_map_table(directory->host, physical_address);

    if (header == NULL)
    {
        ++directory->unreachable_count;
        return;
    }

    AdvancedConfigurationAndPowerInterfaceTable_t *table = &directory->tables[directory->table_count++];

    memcpy(table->signature, header->signature, sizeof(table->signature));
    table->physical_address = physical_address;
    table->length = header->length;
    table->revision = header->revision;
    table->checksum_valid = advanced_configuration_and_power_interface_checksum_holds(header, header->length);
    table->header = header;
}

/**
 * @brief Maps the root table at @p physical_address through @p host when its signature is
 *        @p signature and its checksum holds.
 *
 * @return Its header, or NULL.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
acpi_map_root(const AdvancedConfigurationAndPowerInterfaceHost_t *host, uint32_t physical_address,
              const char *signature)
{
    if (physical_address == 0u)
        return NULL;

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *root = acpi_map_table(host, physical_address);

    if (root == NULL)
        return NULL;
    if (acpi_signature_matches(root->signature, signature, 4u) &&
        advanced_configuration_and_power_interface_checksum_holds(root, root->length))
        return root;

    acpi_unmap_table(host, root);
    return NULL;
}

/**
 * @brief Records every table the entries of @p root name, each @p entry_bytes wide.
 *
 * @details An entry above 4 GiB is counted unreachable rather than truncated, which would name
 *          another table.
 */
static void acpi_record_root_entries(AdvancedConfigurationAndPowerInterfaceDirectory_t *directory,
                                     const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *root,
                                     uint32_t entry_bytes)
{
    const uint8_t *entries = (const uint8_t *) root + sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t);
    const uint32_t entry_count =
        (root->length - sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t)) / entry_bytes;

    for (uint32_t index = 0u; index < entry_count; ++index)
    {
        uint64_t physical_address = 0u;

        memcpy(&physical_address, entries + index * entry_bytes, entry_bytes);
        if ((physical_address >> 32u) != 0u)
            ++directory->unreachable_count;
        else
            acpi_record_table(directory, (uint32_t) physical_address);
    }
}

/**
 * @brief Reads the address a FADT gives in its 32-bit field at @p offset_32 and in its 64-bit field
 *        at @p offset_64, each left 0 when the FADT ends before it.
 */
static void acpi_fadt_read_addresses(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt, uint32_t offset_32,
                                     uint32_t offset_64, uint32_t *address_32, uint64_t *address_64)
{
    *address_32 = 0u;
    *address_64 = 0u;
    if (fadt->length >= offset_32 + sizeof(*address_32))
        memcpy(address_32, (const uint8_t *) fadt + offset_32, sizeof(*address_32));
    if (fadt->length >= offset_64 + sizeof(*address_64))
        memcpy(address_64, (const uint8_t *) fadt + offset_64, sizeof(*address_64));
}

/**
 * @brief The DSDT a FADT names.
 *
 * @details X_DSDT wins when it is not 0, as in ACPICA's AcpiTbSelectAddress, unless it lies above
 *          4 GiB, which this kernel cannot map.
 *
 * @return Its address, or 0 when neither field gives one below 4 GiB.
 */
static uint32_t acpi_fadt_dsdt_address(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt)
{
    uint32_t address_32;
    uint64_t address_64;

    acpi_fadt_read_addresses(fadt, ACPI_FADT_DSDT_OFFSET, ACPI_FADT_EXTENDED_DSDT_OFFSET, &address_32, &address_64);
    if (address_64 != 0u && (address_64 >> 32u) == 0u)
        return (uint32_t) address_64;
    return address_32;
}

/**
 * @brief The FACS a FADT names.
 *
 * @details FIRMWARE_CTRL wins when it is not 0, as ACPICA and uACPI choose by default because some
 *          machines fail to resume when the 64-bit address is taken; X_FIRMWARE_CTRL otherwise,
 *          unless it lies above 4 GiB.
 *
 * @return Its address, or 0 when neither field gives one below 4 GiB.
 */
static uint32_t acpi_fadt_facs_address(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt)
{
    uint32_t address_32;
    uint64_t address_64;

    acpi_fadt_read_addresses(fadt, ACPI_FADT_FIRMWARE_CONTROL_OFFSET, ACPI_FADT_EXTENDED_FIRMWARE_CONTROL_OFFSET,
                             &address_32, &address_64);
    if (address_32 != 0u)
        return address_32;
    if ((address_64 >> 32u) == 0u)
        return (uint32_t) address_64;
    return 0u;
}

/**
 * @brief The earliest revision of a FADT @p length bytes long.
 */
static uint8_t acpi_fadt_revision(uint32_t length)
{
    if (length <= ACPI_FADT_REVISION_1_LENGTH)
        return 1u;
    if (length <= ACPI_FADT_REVISION_2_LENGTH)
        return 2u;
    if (length <= ACPI_FADT_REVISION_3_LENGTH)
        return 3u;
    if (length <= ACPI_FADT_REVISION_5_LENGTH)
        return 5u;
    return 6u;
}

/**
 * @brief Reads the Generic Address Structure at @p offset of a FADT's copy; one whose address is 0
 *        reads absent, every field 0.
 */
static void acpi_fadt_read_register(const uint8_t *copy, uint32_t offset,
                                    AdvancedConfigurationAndPowerInterfaceGenericAddress_t *out_register)
{
    memset(out_register, 0, sizeof(*out_register));
    memcpy(&out_register->address, copy + offset + 4u, sizeof(out_register->address));
    if (out_register->address == 0u)
        return;
    out_register->space = copy[offset];
    out_register->bit_width = copy[offset + 1u];
    out_register->bit_offset = copy[offset + 2u];
    out_register->access_size = copy[offset + 3u];
}

/**
 * @brief Reads register block @p index of a FADT's copy: its extended block when that gives an
 *        address, as wide as the legacy length when it gives no width; else its legacy block, in the
 *        I/O space.
 *
 * @return The legacy length, in bytes.
 */
static uint8_t acpi_fadt_read_block(const uint8_t *copy, AcpiFadtBlockIndex_t index,
                                    AdvancedConfigurationAndPowerInterfaceGenericAddress_t *out_block)
{
    const AcpiFadtBlock_t *block = &acpi_fadt_blocks[index];
    const uint8_t length = copy[block->length_offset];
    const uint32_t bits = 8u * length;
    const uint8_t width =
        (bits > ACPI_GENERIC_ADDRESS_MAXIMUM_WIDTH) ? ACPI_GENERIC_ADDRESS_MAXIMUM_WIDTH : (uint8_t) bits;
    uint32_t legacy_address = 0u;

    acpi_fadt_read_register(copy, block->extended_offset, out_block);
    if (out_block->address != 0u)
    {
        if (out_block->bit_width == 0u)
            out_block->bit_width = width;
        return length;
    }
    memcpy(&legacy_address, copy + block->legacy_offset, sizeof(legacy_address));
    if (legacy_address == 0u)
        return length;
    out_block->address = legacy_address;
    out_block->space = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_INPUT_OUTPUT;
    out_block->bit_width = width;
    return length;
}

/**
 * @brief Reads GPE block @p index of a FADT's copy, and its length in bytes: the legacy length, else
 *        the block's own width; 0 when the block is absent.
 */
static void acpi_fadt_read_event_block(const uint8_t *copy, AcpiFadtBlockIndex_t index,
                                       AdvancedConfigurationAndPowerInterfaceGenericAddress_t *out_block,
                                       uint8_t *out_length)
{
    const uint8_t length = acpi_fadt_read_block(copy, index, out_block);

    if (out_block->address == 0u)
        *out_length = 0u;
    else
        *out_length = (length != 0u) ? length : (uint8_t) (out_block->bit_width / 8u);
}

/**
 * @brief Reads every register block of a FADT's copy: PM1 event and control, PM timer and GPE.
 */
static void acpi_fadt_read_blocks(const uint8_t *copy, AdvancedConfigurationAndPowerInterfaceFixedDescription_t *out)
{
    (void) acpi_fadt_read_block(copy, ACPI_FADT_PM1A_EVENT, &out->power_management_event_blocks[0]);
    (void) acpi_fadt_read_block(copy, ACPI_FADT_PM1B_EVENT, &out->power_management_event_blocks[1]);
    (void) acpi_fadt_read_block(copy, ACPI_FADT_PM1A_CONTROL, &out->power_management_control_blocks[0]);
    (void) acpi_fadt_read_block(copy, ACPI_FADT_PM1B_CONTROL, &out->power_management_control_blocks[1]);
    (void) acpi_fadt_read_block(copy, ACPI_FADT_PM_TIMER, &out->power_management_timer_block);
    acpi_fadt_read_event_block(copy, ACPI_FADT_GPE0, &out->general_purpose_event_blocks[0],
                               &out->general_purpose_event_block_lengths[0]);
    acpi_fadt_read_event_block(copy, ACPI_FADT_GPE1, &out->general_purpose_event_blocks[1],
                               &out->general_purpose_event_block_lengths[1]);
}

/**
 * @brief Clears, in a FADT's copy and its flags, every field and flag ACPI tells OSPM to ignore on a
 *        hardware-reduced platform.
 */
static void acpi_fadt_forget_fixed_hardware(uint8_t *copy, uint32_t *flags)
{
    memset(copy + ACPI_FADT_FIXED_HARDWARE_FIRST, 0, ACPI_FADT_FIXED_HARDWARE_END - ACPI_FADT_FIXED_HARDWARE_FIRST);
    memset(copy + ACPI_FADT_EXTENDED_FIXED_HARDWARE_FIRST, 0,
           ACPI_FADT_EXTENDED_FIXED_HARDWARE_END - ACPI_FADT_EXTENDED_FIXED_HARDWARE_FIRST);
    *flags &= ~ACPI_FADT_FLAGS_IGNORED_WHEN_REDUCED;
}

/**
 * @brief Reads the fields of a FADT's copy that are not registers: SCI_INT, SMI_CMD, the commands
 *        written to it, and GPE1_BASE.
 */
static void acpi_fadt_read_fields(const uint8_t *copy, AdvancedConfigurationAndPowerInterfaceFixedDescription_t *out)
{
    memcpy(&out->system_control_interrupt, copy + ACPI_FADT_SYSTEM_CONTROL_INTERRUPT_OFFSET,
           sizeof(out->system_control_interrupt));
    memcpy(&out->system_management_interrupt_command, copy + ACPI_FADT_SYSTEM_MANAGEMENT_COMMAND_OFFSET,
           sizeof(out->system_management_interrupt_command));
    out->enable_command = copy[ACPI_FADT_ENABLE_COMMAND_OFFSET];
    out->performance_state_command = copy[ACPI_FADT_PERFORMANCE_STATE_COMMAND_OFFSET];
    out->idle_state_command = copy[ACPI_FADT_IDLE_STATE_COMMAND_OFFSET];
    out->general_purpose_event_1_base = copy[ACPI_FADT_GENERAL_PURPOSE_EVENT_1_BASE];
}

/**
 * @brief Whether one of the @p bytes ports from @p first is a protected port.
 */
static bool acpi_mode_reaches_protected_port(uint16_t first, uint32_t bytes)
{
    for (uint32_t offset = 0u; offset < bytes; ++offset)
        if (advanced_configuration_and_power_interface_port_is_protected((uint16_t) (first + offset)))
            return true;
    return false;
}

/**
 * @brief Locates the enable register of a PM1 event or GPE block @p bytes long: its second half. A
 *        block too short to hold one, a GPE block of length 0 among them, is absent, as in ACPICA.
 *
 * @return false when the block gives an address but does not lie wholly in the I/O space, or its
 *         enable register reaches a protected port; @p out_port is 0 for an absent block.
 */
static bool acpi_mode_locate_enables(const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *block,
                                     uint32_t bytes, uint16_t *out_port, uint8_t *out_bytes)
{
    const uint32_t half = bytes / 2u;
    uint16_t first = 0u;

    *out_port = 0u;
    *out_bytes = 0u;
    if (block->address == 0u || half == 0u)
        return true;
    first = advanced_configuration_and_power_interface_input_output_port(block, bytes);
    if (first == 0u || acpi_mode_reaches_protected_port((uint16_t) (first + half), half))
        return false;
    *out_port = (uint16_t) (first + half);
    *out_bytes = (uint8_t) half;
    return true;
}

/**
 * @brief Locates the PM1 control registers of @p fadt, where SCI_EN is read.
 *
 * @return false when PM1a's is not a port, PM1b's gives an address that is not one, or either
 *         reaches a protected port.
 */
static bool acpi_mode_locate_control(const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt,
                                     AcpiModeRegisters_t *registers)
{
    for (uint32_t index = 0u; index < 2u; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *block =
            &fadt->power_management_control_blocks[index];

        registers->control_ports[index] = advanced_configuration_and_power_interface_input_output_port(block, 2u);
        if (registers->control_ports[index] == 0u && (index == 0u || block->address != 0u))
            return false;
        if (registers->control_ports[index] != 0u &&
            acpi_mode_reaches_protected_port(registers->control_ports[index], 2u))
            return false;
    }
    return true;
}

/**
 * @brief Locates the PM1 and GPE enable registers of @p fadt, which the switch clears.
 *
 * @return false when a block that gives an address is not one this layer reaches.
 */
static bool acpi_mode_locate_event_enables(const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt,
                                           AcpiModeRegisters_t *registers)
{
    for (uint32_t index = 0u; index < 2u; ++index)
    {
        uint8_t half = 0u;

        if (!acpi_mode_locate_enables(&fadt->power_management_event_blocks[index],
                                      fadt->power_management_event_blocks[index].bit_width / 8u,
                                      &registers->event_enable_ports[index], &half) ||
            !acpi_mode_locate_enables(
                &fadt->general_purpose_event_blocks[index], fadt->general_purpose_event_block_lengths[index],
                &registers->general_purpose_enable_ports[index], &registers->general_purpose_enable_lengths[index]))
            return false;
        registers->event_enable_widths[index] = (half >= 2u) ? 2u : half;
    }
    return true;
}

/**
 * @brief Whether SCI_EN reads set in either PM1 control register.
 */
static bool acpi_mode_system_control_interrupt_enabled(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                       const AcpiModeRegisters_t *registers)
{
    uint32_t control = 0u;

    for (uint32_t index = 0u; index < 2u; ++index)
        if (registers->control_ports[index] != 0u)
            control |= host->port_read(host->context, registers->control_ports[index], 2u);
    return (control & ACPI_SYSTEM_CONTROL_INTERRUPT_ENABLE) != 0u;
}

/**
 * @brief Writes every GPE enable byte 0, and clears the fixed-event enables of each PM1 enable
 *        register, writing its other bits back.
 *
 * @return The enable registers written.
 */
static uint32_t acpi_mode_clear_event_enables(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                              const AcpiModeRegisters_t *registers)
{
    uint32_t written = 0u;

    for (uint32_t index = 0u; index < 2u; ++index)
    {
        for (uint32_t byte = 0u; byte < registers->general_purpose_enable_lengths[index]; ++byte, ++written)
            host->port_write(host->context, (uint16_t) (registers->general_purpose_enable_ports[index] + byte), 1u, 0u);
    }
    for (uint32_t index = 0u; index < 2u; ++index)
    {
        const uint16_t port = registers->event_enable_ports[index];
        const uint8_t width = registers->event_enable_widths[index];

        if (port == 0u)
            continue;
        host->port_write(host->context, port, width,
                         host->port_read(host->context, port, width) & ~ACPI_FIXED_EVENT_ENABLES);
        ++written;
    }
    return written;
}

/**
 * @brief Reads SCI_EN until it reads set, the host sleeping ACPI_MODE_POLL_MILLISECONDS between two
 *        reads, at most ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_POLLS times and for
 *        at most ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_MILLISECONDS.
 */
static void acpi_mode_wait_for_system_control_interrupt(const AdvancedConfigurationAndPowerInterfaceHost_t *host,
                                                        const AcpiModeRegisters_t *registers,
                                                        AdvancedConfigurationAndPowerInterfaceModeSwitch_t *out_switch)
{
    for (;;)
    {
        ++out_switch->polls;
        if (acpi_mode_system_control_interrupt_enabled(host, registers))
        {
            out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_SWITCHED;
            return;
        }
        if (out_switch->polls == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_POLLS ||
            out_switch->waited_milliseconds >= ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_MAXIMUM_MILLISECONDS)
        {
            out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_FAILED;
            return;
        }

        const uint32_t slept = host->sleep_milliseconds(host->context, ACPI_MODE_POLL_MILLISECONDS);

        out_switch->waited_milliseconds = (slept > UINT32_MAX - out_switch->waited_milliseconds) ?
                                              UINT32_MAX :
                                              out_switch->waited_milliseconds + slept;
    }
}

/**
 * @brief Whether a processor entry's flags let the processor run, now or once enabled at run time.
 */
static bool acpi_madt_processor_usable(uint32_t flags)
{
    return (flags & (ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MADT_ENABLED |
                     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MADT_ONLINE_CAPABLE)) != 0u;
}

/**
 * @brief Whether a local APIC entry of @p madt names a processor that can run, which makes an
 *        x2APIC entry below 0xFF name none.
 */
static bool acpi_madt_has_local_apic_processors(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *madt)
{
    const uint8_t *entry = (const uint8_t *) madt + sizeof(AdvancedConfigurationAndPowerInterfaceMadt_t);
    const uint8_t *end = (const uint8_t *) madt + madt->length;

    while (entry + sizeof(AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t) <= end &&
           entry[1] >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t) && entry + entry[1] <= end)
    {
        const AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t *local_apic =
            (const AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t *) entry;

        if (entry[0] == ACPI_MADT_LOCAL_APIC && entry[1] >= sizeof(*local_apic) &&
            local_apic->apic_id != ACPI_MADT_INVALID_APIC_ID && acpi_madt_processor_usable(local_apic->flags))
            return true;
        entry += entry[1];
    }
    return false;
}

/**
 * @brief Gives the processor of a local APIC or local x2APIC entry a CPU slot when its Enabled flag
 *        is set and @p apic_id_valid, and counts it otherwise.
 */
static void acpi_madt_add_processor(AdvancedConfigurationAndPowerInterfaceMadtState_t *state, uint32_t flags,
                                    bool apic_id_valid, uint32_t apic_id, uint32_t processor_unique_identifier)
{
    if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MADT_ENABLED) == 0u)
    {
        if ((flags & ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MADT_ONLINE_CAPABLE) != 0u)
            ++state->online_capable_count;
        else
            ++state->disabled_count;
        return;
    }
    if (!apic_id_valid)
    {
        ++state->invalid_count;
        return;
    }
    if (state->processor_count == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT)
    {
        ++state->processors_dropped;
        return;
    }

    AdvancedConfigurationAndPowerInterfaceProcessor_t *processor = &state->processors[state->processor_count++];

    processor->processor_unique_identifier = processor_unique_identifier;
    processor->apic_id = apic_id;
}

/**
 * @brief Keeps the NMI connection of a local APIC NMI or local x2APIC NMI entry, or counts it past the
 *        capacity.
 */
static void acpi_madt_add_non_maskable_interrupt(AdvancedConfigurationAndPowerInterfaceMadtState_t *state,
                                                 uint32_t processor_unique_identifier, uint16_t flags,
                                                 uint8_t local_interrupt)
{
    if (state->non_maskable_interrupt_count ==
        ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_NON_MASKABLE_INTERRUPT_COUNT)
    {
        ++state->non_maskable_interrupts_dropped;
        return;
    }

    AdvancedConfigurationAndPowerInterfaceProcessorNonMaskableInterrupt_t *connection =
        &state->non_maskable_interrupts[state->non_maskable_interrupt_count++];

    connection->processor_unique_identifier = processor_unique_identifier;
    connection->flags = flags;
    connection->local_interrupt = local_interrupt;
}

/**
 * @brief Reads one MADT entry of a type that names a processor or its NMI connection.
 *
 * @return false when its type is none of those, or it is shorter than its type.
 */
static bool acpi_madt_read_processor_entry(AdvancedConfigurationAndPowerInterfaceMadtState_t *state,
                                           const uint8_t *entry, bool local_apic_processors)
{
    const uint8_t type = entry[0];
    const uint8_t length = entry[1];

    if (type == ACPI_MADT_LOCAL_APIC && length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t))
    {
        const AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t *local_apic =
            (const AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t *) entry;

        acpi_madt_add_processor(state, local_apic->flags, local_apic->apic_id != ACPI_MADT_INVALID_APIC_ID,
                                local_apic->apic_id, local_apic->acpi_processor_id);
        return true;
    }
    if (type == ACPI_MADT_LOCAL_X2APIC &&
        length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicEntry_t))
    {
        const AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicEntry_t *local_x2apic =
            (const AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicEntry_t *) entry;
        const bool valid = local_x2apic->x2apic_id != ACPI_MADT_INVALID_X2APIC_ID &&
                           !(local_apic_processors && local_x2apic->x2apic_id < ACPI_MADT_INVALID_APIC_ID);

        acpi_madt_add_processor(state, local_x2apic->flags, valid, local_x2apic->x2apic_id,
                                local_x2apic->processor_unique_identifier);
        return true;
    }
    if (type == ACPI_MADT_LOCAL_APIC_NON_MASKABLE_INTERRUPT &&
        length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalApicNonMaskableInterruptEntry_t))
    {
        const AdvancedConfigurationAndPowerInterfaceMadtLocalApicNonMaskableInterruptEntry_t *connection =
            (const AdvancedConfigurationAndPowerInterfaceMadtLocalApicNonMaskableInterruptEntry_t *) entry;
        const uint32_t processor_unique_identifier =
            (connection->processor_unique_identifier == ACPI_MADT_INVALID_APIC_ID) ?
                ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ALL_PROCESSORS :
                connection->processor_unique_identifier;

        acpi_madt_add_non_maskable_interrupt(state, processor_unique_identifier, connection->flags,
                                             connection->local_interrupt);
        return true;
    }
    if (type == ACPI_MADT_LOCAL_X2APIC_NON_MASKABLE_INTERRUPT &&
        length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicNonMaskableInterruptEntry_t))
    {
        const AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicNonMaskableInterruptEntry_t *connection =
            (const AdvancedConfigurationAndPowerInterfaceMadtLocalX2ApicNonMaskableInterruptEntry_t *) entry;

        acpi_madt_add_non_maskable_interrupt(state, connection->processor_unique_identifier, connection->flags,
                                             connection->local_interrupt);
        return true;
    }
    return false;
}

/**
 * @brief Reads one MADT entry of a type that names an I/O APIC or an interrupt source override.
 *
 * @return false when its type is neither, or it is shorter than its type.
 */
static bool acpi_madt_read_interrupt_entry(AdvancedConfigurationAndPowerInterfaceMadtState_t *state,
                                           const uint8_t *entry)
{
    const uint8_t type = entry[0];
    const uint8_t length = entry[1];

    if (type == ACPI_MADT_IO_APIC && length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t))
    {
        const AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t *io_apic =
            (const AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t *) entry;

        if (state->io_apic_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT)
        {
            AdvancedConfigurationAndPowerInterfaceIoApicInfo_t *info = &state->io_apics[state->io_apic_count++];

            info->id = io_apic->io_apic_id;
            info->physical_base = io_apic->io_apic_address;
            info->gsi_base = io_apic->global_system_interrupt_base;
        }
        return true;
    }
    if (type == ACPI_MADT_INTERRUPT_SOURCE_OVERRIDE &&
        length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t))
    {
        const AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t *override =
            (const AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t *) entry;

        if (state->interrupt_source_override_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT)
        {
            AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t *info =
                &state->interrupt_source_overrides[state->interrupt_source_override_count++];

            info->bus = override->bus;
            info->source_irq = override->source;
            info->global_system_interrupt = override->global_system_interrupt;
            info->flags = override->flags;
        }
        return true;
    }
    return false;
}

bool advanced_configuration_and_power_interface_port_is_protected(uint16_t port)
{
    for (uint32_t index = 0u; index < sizeof(acpi_protected_ports) / sizeof(acpi_protected_ports[0]); ++index)
        if (port >= acpi_protected_ports[index].first && port <= acpi_protected_ports[index].last)
            return true;
    return false;
}

bool advanced_configuration_and_power_interface_checksum_holds(const void *bytes, size_t length)
{
    const uint8_t *byte = bytes;
    uint8_t sum = 0u;

    if (byte == NULL || length == 0u)
        return false;

    for (size_t index = 0u; index < length; ++index)
        sum = (uint8_t) (sum + byte[index]);
    return sum == 0u;
}

bool advanced_configuration_and_power_interface_read_fixed_description(
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt,
    AdvancedConfigurationAndPowerInterfaceFixedDescription_t *out_description)
{
    uint8_t copy[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_FIXED_DESCRIPTION_LENGTH] = {0};

    memset(out_description, 0, sizeof(*out_description));
    if (fadt == NULL || fadt->length < sizeof(*fadt))
        return false;

    memcpy(copy, fadt, (fadt->length < sizeof(copy)) ? fadt->length : sizeof(copy));
    out_description->present = true;
    out_description->length = fadt->length;
    out_description->revision = acpi_fadt_revision(fadt->length);
    memcpy(&out_description->flags, copy + ACPI_FADT_FLAGS_OFFSET, sizeof(out_description->flags));
    out_description->hardware_reduced = (out_description->flags & ACPI_FADT_HARDWARE_REDUCED) != 0u;
    if (out_description->hardware_reduced)
        acpi_fadt_forget_fixed_hardware(copy, &out_description->flags);
    if (out_description->revision <= 2u)
    {
        copy[ACPI_FADT_PERFORMANCE_STATE_COMMAND_OFFSET] = 0u;
        copy[ACPI_FADT_IDLE_STATE_COMMAND_OFFSET] = 0u;
    }
    acpi_fadt_read_fields(copy, out_description);
    acpi_fadt_read_blocks(copy, out_description);
    if (out_description->revision >= 2u && (out_description->flags & ACPI_FADT_RESET_REGISTER_SUPPORTED) != 0u)
    {
        acpi_fadt_read_register(copy, ACPI_FADT_RESET_REGISTER_OFFSET, &out_description->reset_register);
        out_description->reset_value = copy[ACPI_FADT_RESET_VALUE_OFFSET];
    }
    acpi_fadt_read_register(copy, ACPI_FADT_SLEEP_CONTROL_OFFSET, &out_description->sleep_control_register);
    acpi_fadt_read_register(copy, ACPI_FADT_SLEEP_STATUS_OFFSET, &out_description->sleep_status_register);
    return true;
}

uint16_t advanced_configuration_and_power_interface_input_output_port(
    const AdvancedConfigurationAndPowerInterfaceGenericAddress_t *reg, uint32_t bytes)
{
    if (reg == NULL || reg->space != ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SPACE_SYSTEM_INPUT_OUTPUT ||
        reg->address == 0u || bytes == 0u || bytes > ACPI_INPUT_OUTPUT_PORTS ||
        reg->address > ACPI_INPUT_OUTPUT_PORTS - bytes)
        return 0u;
    return (uint16_t) reg->address;
}

void advanced_configuration_and_power_interface_enter_mode(
    const AdvancedConfigurationAndPowerInterfaceHost_t *host,
    const AdvancedConfigurationAndPowerInterfaceFixedDescription_t *fadt,
    AdvancedConfigurationAndPowerInterfaceModeSwitch_t *out_switch)
{
    AcpiModeRegisters_t registers;

    memset(out_switch, 0, sizeof(*out_switch));
    memset(&registers, 0, sizeof(registers));
    if (!fadt->present)
        return;
    if (fadt->hardware_reduced)
    {
        out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_HARDWARE_REDUCED;
        return;
    }
    if (!acpi_mode_locate_control(fadt, &registers))
    {
        out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_UNREACHABLE;
        return;
    }
    if (acpi_mode_system_control_interrupt_enabled(host, &registers))
    {
        out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_ALREADY;
        return;
    }
    if (fadt->system_management_interrupt_command == 0u)
    {
        out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NO_COMMAND_PORT;
        return;
    }
    if (fadt->enable_command == 0u)
    {
        out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_NO_LEGACY_MODE;
        return;
    }
    if (fadt->system_management_interrupt_command >= ACPI_INPUT_OUTPUT_PORTS ||
        acpi_mode_reaches_protected_port((uint16_t) fadt->system_management_interrupt_command, 1u) ||
        !acpi_mode_locate_event_enables(fadt, &registers))
    {
        out_switch->outcome = ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MODE_UNREACHABLE;
        return;
    }

    out_switch->enable_registers_cleared = acpi_mode_clear_event_enables(host, &registers);
    host->port_write(host->context, (uint16_t) fadt->system_management_interrupt_command, 1u, fadt->enable_command);
    acpi_mode_wait_for_system_control_interrupt(host, &registers, out_switch);
}

void advanced_configuration_and_power_interface_mode_initialize(void)
{
    advanced_configuration_and_power_interface_enter_mode(
        &advanced_configuration_and_power_interface_machine_host,
        &advanced_configuration_and_power_interface_directory()->fixed_description, &acpi_mode);
}

const AdvancedConfigurationAndPowerInterfaceModeSwitch_t *advanced_configuration_and_power_interface_mode(void)
{
    return &acpi_mode;
}

const char *advanced_configuration_and_power_interface_mode_outcome_name(uint8_t outcome)
{
    if (outcome >= sizeof(acpi_mode_outcome_names) / sizeof(acpi_mode_outcome_names[0]))
        return "unknown";
    return acpi_mode_outcome_names[outcome];
}

bool advanced_configuration_and_power_interface_read_directory(
    const AdvancedConfigurationAndPowerInterfaceHost_t *host,
    AdvancedConfigurationAndPowerInterfaceDirectory_t *out_directory)
{
    if (host == NULL || out_directory == NULL)
        return false;
    memset(out_directory, 0, sizeof(*out_directory));
    out_directory->host = host;

    const uint64_t rsdp_physical_address = host->root_system_description_pointer(host->context);

    if (rsdp_physical_address == 0u)
        return false;
    if ((rsdp_physical_address >> 32u) != 0u)
    {
        ++out_directory->unreachable_count;
        return false;
    }

    const AdvancedConfigurationAndPowerInterfaceRsdp_t *rsdp =
        host->map_firmware(host->context, rsdp_physical_address, sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t));

    if (rsdp == NULL)
        return false;

    const bool valid =
        acpi_signature_matches(rsdp->signature,
                               ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE,
                               8u) &&
        advanced_configuration_and_power_interface_checksum_holds(
            rsdp, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_VERSION_1_LENGTH);
    const bool extended = valid && rsdp->revision >= 2u &&
                          rsdp->length == sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t) &&
                          advanced_configuration_and_power_interface_checksum_holds(rsdp, rsdp->length);
    const uint32_t rsdt_physical_address = rsdp->rsdt_address;
    const uint64_t xsdt_physical_address = extended ? rsdp->xsdt_address : 0u;

    out_directory->rsdp_revision = rsdp->revision;
    host->unmap_firmware(host->context, rsdp, sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t));
    if (!valid)
        return false;

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *root = NULL;

    if (xsdt_physical_address != 0u && (xsdt_physical_address >> 32u) == 0u)
        root = acpi_map_root(host, (uint32_t) xsdt_physical_address, ACPI_XSDT_SIGNATURE);
    if (root != NULL)
    {
        out_directory->extended_root = true;
        out_directory->root_physical_address = (uint32_t) xsdt_physical_address;
    }
    else
    {
        root = acpi_map_root(host, rsdt_physical_address, ACPI_RSDT_SIGNATURE);
        if (root == NULL)
            return false;
        out_directory->root_physical_address = rsdt_physical_address;
    }

    acpi_record_root_entries(out_directory, root, out_directory->extended_root ? sizeof(uint64_t) : sizeof(uint32_t));
    acpi_unmap_table(host, root);

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt =
        advanced_configuration_and_power_interface_find_in_directory(out_directory, ACPI_FADT_SIGNATURE);

    if (fadt != NULL)
    {
        const uint32_t dsdt = acpi_fadt_dsdt_address(fadt);

        if (dsdt != 0u)
            acpi_record_table(out_directory, dsdt);
        out_directory->firmware_control_physical_address = acpi_fadt_facs_address(fadt);
        (void) advanced_configuration_and_power_interface_read_fixed_description(fadt,
                                                                                 &out_directory->fixed_description);
    }

    out_directory->walked = true;
    return true;
}

void advanced_configuration_and_power_interface_release_directory(
    AdvancedConfigurationAndPowerInterfaceDirectory_t *directory)
{
    if (directory == NULL)
        return;

    for (uint32_t index = 0u; index < directory->table_count; ++index)
        acpi_unmap_table(directory->host, directory->tables[index].header);
    memset(directory, 0, sizeof(*directory));
}

const AdvancedConfigurationAndPowerInterfaceDirectory_t *advanced_configuration_and_power_interface_directory(void)
{
    if (!acpi_directory_read)
    {
        acpi_directory_read = true;
        (void) advanced_configuration_and_power_interface_read_directory(
            &advanced_configuration_and_power_interface_machine_host, &acpi_directory);
    }
    return &acpi_directory;
}

const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *advanced_configuration_and_power_interface_find_in_directory(
    const AdvancedConfigurationAndPowerInterfaceDirectory_t *directory, const char *signature)
{
    if (directory == NULL || signature == NULL)
        return NULL;

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *checksum_fails = NULL;

    for (uint32_t index = 0u; index < directory->table_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceTable_t *table = &directory->tables[index];

        if (!acpi_signature_matches(table->signature, signature, 4u))
            continue;
        if (table->checksum_valid)
            return table->header;
        if (checksum_fails == NULL)
            checksum_fails = table->header;
    }
    return checksum_fails;
}

const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *
advanced_configuration_and_power_interface_find_table(const char *signature)
{
    return advanced_configuration_and_power_interface_find_in_directory(
        advanced_configuration_and_power_interface_directory(), signature);
}

bool advanced_configuration_and_power_interface_read_madt(
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *table,
    AdvancedConfigurationAndPowerInterfaceMadtState_t *out_state)
{
    if (out_state == NULL)
        return false;
    memset(out_state, 0, sizeof(*out_state));
    if (table == NULL)
    {
        out_state->state_name = "acpi-madt-not-found";
        return false;
    }
    if (table->length < sizeof(AdvancedConfigurationAndPowerInterfaceMadt_t))
    {
        out_state->state_name = "acpi-madt-too-short";
        return false;
    }

    const AdvancedConfigurationAndPowerInterfaceMadt_t *madt =
        (const AdvancedConfigurationAndPowerInterfaceMadt_t *) table;
    const bool local_apic_processors = acpi_madt_has_local_apic_processors(table);
    const uint8_t *entry = (const uint8_t *) madt + sizeof(AdvancedConfigurationAndPowerInterfaceMadt_t);
    const uint8_t *end = (const uint8_t *) madt + table->length;

    out_state->checksum_valid = advanced_configuration_and_power_interface_checksum_holds(table, table->length);
    out_state->local_apic_physical_base = madt->local_apic_address;

    while (entry + sizeof(AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t) <= end)
    {
        const uint8_t type = entry[0];
        const uint8_t length = entry[1];

        if (length < sizeof(AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t))
        {
            out_state->state_name = "acpi-madt-entry-length-invalid";
            return false;
        }
        if (entry + length > end)
        {
            out_state->state_name = "acpi-madt-entry-out-of-bounds";
            return false;
        }

        if (type == ACPI_MADT_LOCAL_APIC_ADDRESS &&
            length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalApicAddressOverrideEntry_t))
        {
            const uint64_t address =
                ((const AdvancedConfigurationAndPowerInterfaceMadtLocalApicAddressOverrideEntry_t *) entry)
                    ->local_apic_address;

            if ((address >> 32u) != 0u)
            {
                out_state->state_name = "acpi-madt-lapic-addr-64bit-unsupported";
                return false;
            }
            out_state->local_apic_physical_base = (uint32_t) address;
        }
        else if (!acpi_madt_read_processor_entry(out_state, entry, local_apic_processors) &&
                 !acpi_madt_read_interrupt_entry(out_state, entry))
        {
            ++out_state->skipped_count;
        }
        entry += length;
    }

    out_state->available = true;
    out_state->state_name =
        (out_state->io_apic_count == 0u) ? "acpi-madt-ready-no-ioapic" : "acpi-madt-ready-ioapic-found";
    return true;
}

void advanced_configuration_and_power_interface_madt_initialize(void)
{
    (void) advanced_configuration_and_power_interface_read_madt(
        advanced_configuration_and_power_interface_find_table(ACPI_MADT_SIGNATURE), &acpi_madt);
}

const AdvancedConfigurationAndPowerInterfaceMadtState_t *advanced_configuration_and_power_interface_madt(void)
{
    return &acpi_madt;
}

const char *advanced_configuration_and_power_interface_madt_get_state_name(void) { return acpi_madt.state_name; }

uint8_t advanced_configuration_and_power_interface_madt_is_available(void) { return acpi_madt.available ? 1u : 0u; }

uint32_t advanced_configuration_and_power_interface_madt_get_local_apic_physical_base(void)
{
    return acpi_madt.local_apic_physical_base;
}

uint8_t advanced_configuration_and_power_interface_madt_get_io_apic_count(void) { return acpi_madt.io_apic_count; }

uint8_t advanced_configuration_and_power_interface_madt_get_io_apic_id(uint8_t index)
{
    if (index >= acpi_madt.io_apic_count)
        return 0u;
    return acpi_madt.io_apics[index].id;
}

uint32_t advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(uint8_t index)
{
    if (index >= acpi_madt.io_apic_count)
        return 0u;
    return acpi_madt.io_apics[index].physical_base;
}

uint32_t advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(uint8_t index)
{
    if (index >= acpi_madt.io_apic_count)
        return 0u;
    return acpi_madt.io_apics[index].gsi_base;
}

uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count(void)
{
    return acpi_madt.interrupt_source_override_count;
}

uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_bus(uint8_t index)
{
    if (index >= acpi_madt.interrupt_source_override_count)
        return 0u;
    return acpi_madt.interrupt_source_overrides[index].bus;
}

uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_source_irq(uint8_t index)
{
    if (index >= acpi_madt.interrupt_source_override_count)
        return 0u;
    return acpi_madt.interrupt_source_overrides[index].source_irq;
}

uint32_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_gsi(uint8_t index)
{
    if (index >= acpi_madt.interrupt_source_override_count)
        return 0u;
    return acpi_madt.interrupt_source_overrides[index].global_system_interrupt;
}

uint16_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(uint8_t index)
{
    if (index >= acpi_madt.interrupt_source_override_count)
        return 0u;
    return acpi_madt.interrupt_source_overrides[index].flags;
}

uint8_t advanced_configuration_and_power_interface_madt_resolve_isa_irq(uint8_t interrupt_request,
                                                                        uint32_t *global_system_interrupt,
                                                                        uint16_t *flags)
{
    if (!global_system_interrupt || !flags)
        return 0u;

    if (!acpi_madt.available)
        return 0u;

    *global_system_interrupt = interrupt_request;
    *flags = 0u;

    for (uint8_t index = 0u; index < acpi_madt.interrupt_source_override_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t *iso =
            &acpi_madt.interrupt_source_overrides[index];

        if (iso->bus != 0u)
            continue;
        if (iso->source_irq != interrupt_request)
            continue;

        *global_system_interrupt = iso->global_system_interrupt;
        *flags = iso->flags;
        break;
    }

    return 1u;
}

uint8_t advanced_configuration_and_power_interface_madt_find_io_apic_for_gsi(uint32_t global_system_interrupt,
                                                                             uint8_t *io_apic_index)
{
    uint8_t found = 0u;
    uint32_t best_base = 0u;
    uint8_t best_index = 0u;

    if (!io_apic_index)
        return 0u;

    if (!acpi_madt.available || acpi_madt.io_apic_count == 0u)
        return 0u;

    for (uint8_t index = 0u; index < acpi_madt.io_apic_count; ++index)
    {
        uint32_t current_base = acpi_madt.io_apics[index].gsi_base;

        if (current_base > global_system_interrupt)
            continue;

        if (!found || current_base > best_base)
        {
            found = 1u;
            best_base = current_base;
            best_index = index;
        }
    }

    if (!found)
        return 0u;

    *io_apic_index = best_index;
    return 1u;
}
