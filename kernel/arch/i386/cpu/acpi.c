#include <kernel/acpi/tables.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/paging.h>

#include <string.h>

#define ACPI_RSDP_SCAN_ALIGN   16u
#define ACPI_BDA_EBDA_PTR_PHYS 0x0000040Eu
#define ACPI_BIOS_SCAN_START   0x000E0000u
#define ACPI_BIOS_SCAN_END     0x00100000u

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

        if (memcmp(candidate->signature,
                   ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE,
                   sizeof(candidate->signature)) != 0)
            continue;

        if (!advanced_configuration_and_power_interface_checksum_holds(
                candidate, ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_ROOT_SYSTEM_DESCRIPTION_POINTER_VERSION_1_LENGTH))
            continue;

        if (candidate->revision >= 2u &&
            (candidate->length < sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t) ||
             addr + candidate->length > end_phys ||
             !advanced_configuration_and_power_interface_checksum_holds(candidate, candidate->length)))
            continue;

        return addr;
    }

    return 0u;
}

uint32_t advanced_configuration_and_power_interface_find_root_system_description_pointer(void)
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
