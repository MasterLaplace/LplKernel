#include <kernel/cpu/acpi.h>
#include <kernel/memory/vmm.h>

#include <string.h>

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_MADT_SIGNATURE "APIC"
#define ACPI_RSDT_SIGNATURE "RSDT"
#define ACPI_XSDT_SIGNATURE "XSDT" /**< The root table of 64-bit entries, ACPI 2.0 and later. */
#define ACPI_FADT_SIGNATURE "FACP" /**< The FADT, which names the DSDT and the FACS. */

#define ACPI_RSDP_SCAN_ALIGN       16u
#define ACPI_RSDP_VERSION_1_LENGTH 20u         /**< Bytes the first checksum of a root pointer covers. */
#define ACPI_TABLE_MAXIMUM_LENGTH  0x01000000u /**< A longer table is taken for a corrupt length. */

#define ACPI_FADT_FIRMWARE_CONTROL_OFFSET          36u  /**< FIRMWARE_CTRL: the FACS, 32 bits. */
#define ACPI_FADT_DSDT_OFFSET                      40u  /**< DSDT: the DSDT, 32 bits. */
#define ACPI_FADT_EXTENDED_FIRMWARE_CONTROL_OFFSET 132u /**< X_FIRMWARE_CTRL: the FACS, 64 bits. */
#define ACPI_FADT_EXTENDED_DSDT_OFFSET             140u /**< X_DSDT: the DSDT, 64 bits. */

#define ACPI_BDA_EBDA_PTR_PHYS 0x0000040Eu
#define ACPI_BIOS_SCAN_START   0x000E0000u
#define ACPI_BIOS_SCAN_END     0x00100000u

static const char *acpi_madt_state_name = "acpi-not-initialized";
static uint8_t acpi_madt_available = 0u;
static uint32_t acpi_madt_local_apic_physical_base = 0u;
static uint8_t acpi_madt_local_apic_count = 0u;
static AdvancedConfigurationAndPowerInterfaceLocalApicInfo_t
    acpi_madt_local_apic_info[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_LOCAL_APIC_COUNT] = {0};
static uint8_t acpi_madt_io_apic_count = 0u;
static AdvancedConfigurationAndPowerInterfaceIoApicInfo_t
    acpi_madt_io_apic_info[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT] = {0};
static uint8_t acpi_madt_iso_count = 0u;
static AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t
    acpi_madt_iso_info[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT] = {0};
static AdvancedConfigurationAndPowerInterfaceDirectory_t acpi_directory;
static bool acpi_directory_read = false;

static uint8_t acpi_checksum_is_valid(const uint8_t *data, size_t length)
{
    uint8_t sum = 0u;

    if (!data || length == 0u)
        return 0u;

    for (size_t i = 0; i < length; ++i)
        sum = (uint8_t) (sum + data[i]);
    return (uint8_t) (sum == 0u);
}

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
 * @brief The kernel's address of a byte of the first mebibyte, which the boot maps at
 *        KERNEL_VIRTUAL_BASE.
 */
static const uint8_t *acpi_low_memory(uint32_t physical_address)
{
    return (const uint8_t *) (uintptr_t) (physical_address + KERNEL_VIRTUAL_BASE);
}

static uint32_t acpi_read_ebda_base_physical(void)
{
    const uint8_t *bda_ptr = acpi_low_memory(ACPI_BDA_EBDA_PTR_PHYS);
    const uint16_t ebda_segment = (uint16_t) bda_ptr[0] | ((uint16_t) bda_ptr[1] << 8u);

    return ((uint32_t) ebda_segment) << 4u;
}

/**
 * @brief Scans the first mebibyte from @p start_phys to @p end_phys, on 16-byte boundaries, for a
 *        root system description pointer.
 *
 * @return The physical address of the first one whose checksums hold, or 0.
 */
static uint32_t acpi_find_rsdp_in_range(uint32_t start_phys, uint32_t end_phys)
{
    for (uint32_t addr = start_phys; addr + sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t) <= end_phys;
         addr += ACPI_RSDP_SCAN_ALIGN)
    {
        const AdvancedConfigurationAndPowerInterfaceRsdp_t *candidate =
            (const AdvancedConfigurationAndPowerInterfaceRsdp_t *) acpi_low_memory(addr);

        if (!acpi_signature_matches(candidate->signature, ACPI_RSDP_SIGNATURE, 8u))
            continue;

        if (!acpi_checksum_is_valid((const uint8_t *) candidate, ACPI_RSDP_VERSION_1_LENGTH))
            continue;

        if (candidate->revision >= 2u && (candidate->length < sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t) ||
                                          addr + candidate->length > end_phys ||
                                          !acpi_checksum_is_valid((const uint8_t *) candidate, candidate->length)))
            continue;

        return addr;
    }

    return 0u;
}

/**
 * @brief Finds the root system description pointer: in the first KiB of the EBDA, then in the BIOS
 *        area from E0000h to FFFFFh.
 *
 * @return Its physical address, or 0.
 */
static uint32_t acpi_find_rsdp(void)
{
    const uint32_t ebda_base = acpi_read_ebda_base_physical();

    if (ebda_base >= 0x00080000u && ebda_base < ACPI_BIOS_SCAN_END)
    {
        const uint32_t rsdp = acpi_find_rsdp_in_range(ebda_base, ebda_base + 1024u);
        if (rsdp != 0u)
            return rsdp;
    }

    return acpi_find_rsdp_in_range(ACPI_BIOS_SCAN_START, ACPI_BIOS_SCAN_END);
}

/**
 * @brief Maps every byte of the table at @p physical_address through a firmware window: its header
 *        first, for its length, then the whole of it.
 *
 * @return Its header, or NULL when it cannot be mapped, or its length is shorter than a header or
 *         longer than ACPI_TABLE_MAXIMUM_LENGTH.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *acpi_map_table(uint32_t physical_address)
{
    const uint32_t header_window =
        kernel_vmm_map_firmware_window(physical_address, sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t));

    if (header_window == 0u)
        return NULL;

    const uint32_t length =
        ((const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (uintptr_t) header_window)->length;

    kernel_vmm_unmap_firmware_window(header_window, sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t));
    if (length < sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t) || length > ACPI_TABLE_MAXIMUM_LENGTH)
        return NULL;

    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) (uintptr_t) kernel_vmm_map_firmware_window(
        physical_address, length);
}

/**
 * @brief Unmaps a table acpi_map_table mapped.
 */
static void acpi_unmap_table(const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header)
{
    kernel_vmm_unmap_firmware_window((uint32_t) (uintptr_t) header, header->length);
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

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = acpi_map_table(physical_address);

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
    table->checksum_valid = acpi_checksum_is_valid((const uint8_t *) header, header->length);
    table->header = header;
}

/**
 * @brief Maps the root table at @p physical_address when its signature is @p signature and its
 *        checksum holds.
 *
 * @return Its header, or NULL.
 */
static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *acpi_map_root(uint32_t physical_address,
                                                                              const char *signature)
{
    if (physical_address == 0u)
        return NULL;

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *root = acpi_map_table(physical_address);

    if (root == NULL)
        return NULL;
    if (acpi_signature_matches(root->signature, signature, 4u) &&
        acpi_checksum_is_valid((const uint8_t *) root, root->length))
        return root;

    acpi_unmap_table(root);
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

static uint8_t acpi_parse_madt(const AdvancedConfigurationAndPowerInterfaceMadt_t *madt)
{
    const uint8_t *entry_ptr;
    const uint8_t *entry_end;

    if (!madt)
        return 0u;

    if (!acpi_checksum_is_valid((const uint8_t *) madt, madt->header.length))
    {
        acpi_madt_state_name = "acpi-madt-checksum-invalid";
        return 0u;
    }

    acpi_madt_local_apic_physical_base = madt->local_apic_address;

    entry_ptr = (const uint8_t *) madt + sizeof(AdvancedConfigurationAndPowerInterfaceMadt_t);
    entry_end = (const uint8_t *) madt + madt->header.length;

    while (entry_ptr + sizeof(AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t) <= entry_end)
    {
        const AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t *entry =
            (const AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t *) entry_ptr;

        if (entry->length < sizeof(AdvancedConfigurationAndPowerInterfaceMadtEntryHeader_t))
        {
            acpi_madt_state_name = "acpi-madt-entry-length-invalid";
            return 0u;
        }

        if (entry_ptr + entry->length > entry_end)
        {
            acpi_madt_state_name = "acpi-madt-entry-out-of-bounds";
            return 0u;
        }

        if (entry->type == 0u && entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t *lapic_entry =
                (const AdvancedConfigurationAndPowerInterfaceMadtLocalApicEntry_t *) entry_ptr;

            if (acpi_madt_local_apic_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_LOCAL_APIC_COUNT)
            {
                AdvancedConfigurationAndPowerInterfaceLocalApicInfo_t *info =
                    &acpi_madt_local_apic_info[acpi_madt_local_apic_count];
                info->acpi_processor_id = lapic_entry->acpi_processor_id;
                info->apic_id = lapic_entry->apic_id;
                info->flags = lapic_entry->flags;
                acpi_madt_local_apic_count++;
            }
        }
        else if (entry->type == 1u && entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t *ioapic_entry =
                (const AdvancedConfigurationAndPowerInterfaceMadtIoApicEntry_t *) entry_ptr;

            if (acpi_madt_io_apic_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT)
            {
                AdvancedConfigurationAndPowerInterfaceIoApicInfo_t *info =
                    &acpi_madt_io_apic_info[acpi_madt_io_apic_count];
                info->id = ioapic_entry->io_apic_id;
                info->physical_base = ioapic_entry->io_apic_address;
                info->gsi_base = ioapic_entry->global_system_interrupt_base;
                acpi_madt_io_apic_count++;
            }
        }
        else if (entry->type == 2u &&
                 entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t *iso_entry =
                (const AdvancedConfigurationAndPowerInterfaceMadtInterruptSourceOverrideEntry_t *) entry_ptr;

            if (acpi_madt_iso_count < ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_ISO_COUNT)
            {
                AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t *info =
                    &acpi_madt_iso_info[acpi_madt_iso_count];
                info->bus = iso_entry->bus;
                info->source_irq = iso_entry->source;
                info->global_system_interrupt = iso_entry->global_system_interrupt;
                info->flags = iso_entry->flags;
                acpi_madt_iso_count++;
            }
        }
        else if (entry->type == 5u &&
                 entry->length >= sizeof(AdvancedConfigurationAndPowerInterfaceMadtLocalApicAddressOverrideEntry_t))
        {
            const AdvancedConfigurationAndPowerInterfaceMadtLocalApicAddressOverrideEntry_t *lapic_override_entry =
                (const AdvancedConfigurationAndPowerInterfaceMadtLocalApicAddressOverrideEntry_t *) entry_ptr;

            if ((lapic_override_entry->local_apic_address >> 32u) != 0u)
            {
                acpi_madt_state_name = "acpi-madt-lapic-addr-64bit-unsupported";
                return 0u;
            }

            acpi_madt_local_apic_physical_base = (uint32_t) lapic_override_entry->local_apic_address;
        }

        entry_ptr += entry->length;
    }

    acpi_madt_available = 1u;
    acpi_madt_state_name =
        (acpi_madt_io_apic_count == 0u) ? "acpi-madt-ready-no-ioapic" : "acpi-madt-ready-ioapic-found";
    return 1u;
}

bool advanced_configuration_and_power_interface_read_directory(
    uint32_t rsdp_physical_address, AdvancedConfigurationAndPowerInterfaceDirectory_t *out_directory)
{
    if (out_directory == NULL)
        return false;
    memset(out_directory, 0, sizeof(*out_directory));

    const uint32_t rsdp_window =
        kernel_vmm_map_firmware_window(rsdp_physical_address, sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t));

    if (rsdp_window == 0u)
        return false;

    const AdvancedConfigurationAndPowerInterfaceRsdp_t *rsdp =
        (const AdvancedConfigurationAndPowerInterfaceRsdp_t *) (uintptr_t) rsdp_window;
    const bool valid = acpi_signature_matches(rsdp->signature, ACPI_RSDP_SIGNATURE, 8u) &&
                       acpi_checksum_is_valid((const uint8_t *) rsdp, ACPI_RSDP_VERSION_1_LENGTH);
    const bool extended = valid && rsdp->revision >= 2u &&
                          rsdp->length == sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t) &&
                          acpi_checksum_is_valid((const uint8_t *) rsdp, rsdp->length);
    const uint32_t rsdt_physical_address = rsdp->rsdt_address;
    const uint64_t xsdt_physical_address = extended ? rsdp->xsdt_address : 0u;

    out_directory->rsdp_revision = rsdp->revision;
    kernel_vmm_unmap_firmware_window(rsdp_window, sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t));
    if (!valid)
        return false;

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *root = NULL;

    if (xsdt_physical_address != 0u && (xsdt_physical_address >> 32u) == 0u)
        root = acpi_map_root((uint32_t) xsdt_physical_address, ACPI_XSDT_SIGNATURE);
    if (root != NULL)
    {
        out_directory->extended_root = true;
        out_directory->root_physical_address = (uint32_t) xsdt_physical_address;
    }
    else
    {
        root = acpi_map_root(rsdt_physical_address, ACPI_RSDT_SIGNATURE);
        if (root == NULL)
            return false;
        out_directory->root_physical_address = rsdt_physical_address;
    }

    acpi_record_root_entries(out_directory, root, out_directory->extended_root ? sizeof(uint64_t) : sizeof(uint32_t));
    acpi_unmap_table(root);

    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *fadt =
        advanced_configuration_and_power_interface_find_in_directory(out_directory, ACPI_FADT_SIGNATURE);

    if (fadt != NULL)
    {
        const uint32_t dsdt = acpi_fadt_dsdt_address(fadt);

        if (dsdt != 0u)
            acpi_record_table(out_directory, dsdt);
        out_directory->firmware_control_physical_address = acpi_fadt_facs_address(fadt);
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
        acpi_unmap_table(directory->tables[index].header);
    memset(directory, 0, sizeof(*directory));
}

const AdvancedConfigurationAndPowerInterfaceDirectory_t *advanced_configuration_and_power_interface_directory(void)
{
    if (!acpi_directory_read)
    {
        const uint32_t rsdp_physical_address = acpi_find_rsdp();

        acpi_directory_read = true;
        if (rsdp_physical_address != 0u)
            (void) advanced_configuration_and_power_interface_read_directory(rsdp_physical_address, &acpi_directory);
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

void advanced_configuration_and_power_interface_madt_initialize(void)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *sdt;

    acpi_madt_state_name = "acpi-madt-search";
    acpi_madt_available = 0u;
    acpi_madt_local_apic_physical_base = 0u;
    acpi_madt_local_apic_count = 0u;
    acpi_madt_io_apic_count = 0u;
    acpi_madt_iso_count = 0u;

    sdt = advanced_configuration_and_power_interface_find_table(ACPI_MADT_SIGNATURE);
    if (!sdt)
    {
        acpi_madt_state_name = "acpi-madt-not-found";
        return;
    }

    acpi_parse_madt((const AdvancedConfigurationAndPowerInterfaceMadt_t *) sdt);
}

const char *advanced_configuration_and_power_interface_madt_get_state_name(void) { return acpi_madt_state_name; }

uint8_t advanced_configuration_and_power_interface_madt_is_available(void) { return acpi_madt_available; }

uint32_t advanced_configuration_and_power_interface_madt_get_local_apic_physical_base(void)
{
    return acpi_madt_local_apic_physical_base;
}

uint8_t advanced_configuration_and_power_interface_madt_get_local_apic_count(void)
{
    return acpi_madt_local_apic_count;
}

uint8_t advanced_configuration_and_power_interface_madt_get_local_apic_id(uint8_t index)
{
    if (index >= acpi_madt_local_apic_count)
        return 0u;
    return acpi_madt_local_apic_info[index].apic_id;
}

uint8_t advanced_configuration_and_power_interface_madt_is_local_apic_enabled(uint8_t index)
{
    if (index >= acpi_madt_local_apic_count)
        return 0u;
    return (uint8_t) ((acpi_madt_local_apic_info[index].flags & 0x1u) != 0u);
}

uint8_t advanced_configuration_and_power_interface_madt_get_enabled_local_apic_count(void)
{
    uint8_t enabled_count = 0u;

    for (uint8_t index = 0u; index < acpi_madt_local_apic_count; ++index)
    {
        if ((acpi_madt_local_apic_info[index].flags & 0x1u) != 0u)
            ++enabled_count;
    }

    return enabled_count;
}

uint8_t advanced_configuration_and_power_interface_madt_get_io_apic_count(void) { return acpi_madt_io_apic_count; }

uint8_t advanced_configuration_and_power_interface_madt_get_io_apic_id(uint8_t index)
{
    if (index >= acpi_madt_io_apic_count)
        return 0u;
    return acpi_madt_io_apic_info[index].id;
}

uint32_t advanced_configuration_and_power_interface_madt_get_io_apic_physical_base(uint8_t index)
{
    if (index >= acpi_madt_io_apic_count)
        return 0u;
    return acpi_madt_io_apic_info[index].physical_base;
}

uint32_t advanced_configuration_and_power_interface_madt_get_io_apic_gsi_base(uint8_t index)
{
    if (index >= acpi_madt_io_apic_count)
        return 0u;
    return acpi_madt_io_apic_info[index].gsi_base;
}

uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_count(void)
{
    return acpi_madt_iso_count;
}

uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_bus(uint8_t index)
{
    if (index >= acpi_madt_iso_count)
        return 0u;
    return acpi_madt_iso_info[index].bus;
}

uint8_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_source_irq(uint8_t index)
{
    if (index >= acpi_madt_iso_count)
        return 0u;
    return acpi_madt_iso_info[index].source_irq;
}

uint32_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_gsi(uint8_t index)
{
    if (index >= acpi_madt_iso_count)
        return 0u;
    return acpi_madt_iso_info[index].global_system_interrupt;
}

uint16_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(uint8_t index)
{
    if (index >= acpi_madt_iso_count)
        return 0u;
    return acpi_madt_iso_info[index].flags;
}

uint8_t advanced_configuration_and_power_interface_madt_resolve_isa_irq(uint8_t interrupt_request,
                                                                        uint32_t *global_system_interrupt,
                                                                        uint16_t *flags)
{
    if (!global_system_interrupt || !flags)
        return 0u;

    if (!acpi_madt_available)
        return 0u;

    *global_system_interrupt = interrupt_request;
    *flags = 0u;

    for (uint8_t index = 0u; index < acpi_madt_iso_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t *iso = &acpi_madt_iso_info[index];

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

    if (!acpi_madt_available || acpi_madt_io_apic_count == 0u)
        return 0u;

    for (uint8_t index = 0u; index < acpi_madt_io_apic_count; ++index)
    {
        uint32_t current_base = acpi_madt_io_apic_info[index].gsi_base;

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
