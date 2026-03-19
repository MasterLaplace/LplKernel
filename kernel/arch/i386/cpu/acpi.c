#include <kernel/cpu/acpi.h>

#include <kernel/cpu/paging.h>

#include <stddef.h>

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_MADT_SIGNATURE "APIC"
#define ACPI_RSDT_SIGNATURE "RSDT"

#define ACPI_RSDP_SCAN_ALIGN 16u

#define ACPI_BDA_EBDA_PTR_PHYS 0x0000040Eu
#define ACPI_BIOS_SCAN_START   0x000E0000u
#define ACPI_BIOS_SCAN_END     0x00100000u
#define ACPI_LOW_MEM_END       0x00100000u

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

static const uint8_t *acpi_map_physical_const(uint32_t physical_address, uint32_t size)
{
    uint32_t virt_start;
    uint32_t virt_end;
    uint32_t map_phys;
    uint32_t map_virt;
    PageDirectoryEntry_t pde_flags = {0};
    PageTableEntry_t pte_flags = {0};

    if (size == 0u)
        return NULL;

    virt_start = physical_address + KERNEL_VIRTUAL_BASE;
    virt_end = virt_start + (size - 1u);

    if (physical_address < ACPI_LOW_MEM_END)
        return (const uint8_t *) virt_start;

    pde_flags.present = 1u;
    pde_flags.read_write = 1u;
    pde_flags.user_supervisor = 0u;

    pte_flags.present = 1u;
    pte_flags.read_write = 1u;
    pte_flags.user_supervisor = 0u;

    map_phys = physical_address & 0xFFFFF000u;
    map_virt = map_phys + KERNEL_VIRTUAL_BASE;

    while (map_virt <= virt_end)
    {
        if (!paging_is_mapped(map_virt))
        {
            if (!paging_map_page(map_virt, map_phys, pde_flags, pte_flags))
                return NULL;
        }

        if (map_phys >= 0xFFFFF000u)
            break;
        map_phys += PAGE_SIZE;
        map_virt += PAGE_SIZE;
    }

    return (const uint8_t *) virt_start;
}

static uint32_t acpi_read_ebda_base_physical(void)
{
    const uint8_t *bda_ptr;
    uint16_t ebda_segment;

    bda_ptr = acpi_map_physical_const(ACPI_BDA_EBDA_PTR_PHYS, sizeof(uint16_t));
    if (!bda_ptr)
        return 0u;

    ebda_segment = (uint16_t) bda_ptr[0] | ((uint16_t) bda_ptr[1] << 8u);
    return ((uint32_t) ebda_segment) << 4u;
}

static const AdvancedConfigurationAndPowerInterfaceRsdp_t *acpi_find_rsdp_in_range(uint32_t start_phys,
                                                                                   uint32_t end_phys)
{
    for (uint32_t addr = start_phys; addr + sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t) <= end_phys;
         addr += ACPI_RSDP_SCAN_ALIGN)
    {
        const AdvancedConfigurationAndPowerInterfaceRsdp_t *candidate =
            (const AdvancedConfigurationAndPowerInterfaceRsdp_t *) acpi_map_physical_const(
                addr, sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t));
        if (!candidate)
            continue;

        if (!acpi_signature_matches(candidate->signature, ACPI_RSDP_SIGNATURE, 8u))
            continue;

        if (!acpi_checksum_is_valid((const uint8_t *) candidate, 20u))
            continue;

        if (candidate->revision >= 2u)
        {
            uint32_t rsdp_length = candidate->length;

            if (rsdp_length < sizeof(AdvancedConfigurationAndPowerInterfaceRsdp_t))
                continue;

            candidate =
                (const AdvancedConfigurationAndPowerInterfaceRsdp_t *) acpi_map_physical_const(addr, rsdp_length);
            if (!candidate)
                continue;

            if (!acpi_checksum_is_valid((const uint8_t *) candidate, rsdp_length))
                continue;
        }

        return candidate;
    }

    return NULL;
}

static const AdvancedConfigurationAndPowerInterfaceRsdp_t *acpi_find_rsdp(void)
{
    uint32_t ebda_base = acpi_read_ebda_base_physical();

    if (ebda_base >= 0x00080000u && ebda_base < ACPI_BIOS_SCAN_END)
    {
        const AdvancedConfigurationAndPowerInterfaceRsdp_t *rsdp =
            acpi_find_rsdp_in_range(ebda_base, ebda_base + 1024u);
        if (rsdp)
            return rsdp;
    }

    return acpi_find_rsdp_in_range(ACPI_BIOS_SCAN_START, ACPI_BIOS_SCAN_END);
}

static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *acpi_map_sdt_header(uint32_t physical_address)
{
    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) acpi_map_physical_const(
        physical_address, sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t));
}

static const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *acpi_map_sdt_full(uint32_t physical_address)
{
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *header = acpi_map_sdt_header(physical_address);
    if (!header)
        return NULL;

    if (header->length < sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t))
        return NULL;

    return (const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *) acpi_map_physical_const(physical_address,
                                                                                               header->length);
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
                info->gsi = iso_entry->global_system_interrupt;
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
    if (acpi_madt_io_apic_count == 0u)
        acpi_madt_state_name = "acpi-madt-ready-no-ioapic";
    else
        acpi_madt_state_name = "acpi-madt-ready-ioapic-found";
    return 1u;
}

const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *advanced_configuration_and_power_interface_find_table(const char *signature)
{
    const AdvancedConfigurationAndPowerInterfaceRsdp_t *rsdp;
    const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *rsdt;
    size_t entry_count;
    const uint32_t *entry_physical_addresses;

    rsdp = acpi_find_rsdp();
    if (!rsdp || rsdp->rsdt_address == 0u)
        return NULL;

    rsdt = acpi_map_sdt_full(rsdp->rsdt_address);
    if (!rsdt)
        return NULL;

    if (!acpi_signature_matches(rsdt->signature, ACPI_RSDT_SIGNATURE, 4u))
        return NULL;

    if (!acpi_checksum_is_valid((const uint8_t *) rsdt, rsdt->length))
        return NULL;

    entry_count = (rsdt->length - sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t)) / sizeof(uint32_t);
    entry_physical_addresses =
        (const uint32_t *) ((const uint8_t *) rsdt + sizeof(AdvancedConfigurationAndPowerInterfaceSdtHeader_t));

    for (size_t i = 0; i < entry_count; ++i)
    {
        uint32_t sdt_physical_address = entry_physical_addresses[i];
        const AdvancedConfigurationAndPowerInterfaceSdtHeader_t *sdt = acpi_map_sdt_full(sdt_physical_address);

        if (!sdt)
            continue;

        if (acpi_signature_matches(sdt->signature, signature, 4u))
        {
            if (acpi_checksum_is_valid((const uint8_t *) sdt, sdt->length))
                return sdt;
        }
    }

    return NULL;
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
    return acpi_madt_iso_info[index].gsi;
}

uint16_t advanced_configuration_and_power_interface_madt_get_interrupt_source_override_flags(uint8_t index)
{
    if (index >= acpi_madt_iso_count)
        return 0u;
    return acpi_madt_iso_info[index].flags;
}

uint8_t advanced_configuration_and_power_interface_madt_resolve_isa_irq(uint8_t irq, uint32_t *gsi, uint16_t *flags)
{
    if (!gsi || !flags)
        return 0u;

    if (!acpi_madt_available)
        return 0u;

    *gsi = irq;
    *flags = 0u;

    for (uint8_t index = 0u; index < acpi_madt_iso_count; ++index)
    {
        const AdvancedConfigurationAndPowerInterfaceInterruptSourceOverrideInfo_t *iso = &acpi_madt_iso_info[index];

        if (iso->bus != 0u)
            continue;
        if (iso->source_irq != irq)
            continue;

        *gsi = iso->gsi;
        *flags = iso->flags;
        break;
    }

    return 1u;
}

uint8_t advanced_configuration_and_power_interface_madt_find_io_apic_for_gsi(uint32_t gsi, uint8_t *io_apic_index)
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

        if (current_base > gsi)
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
