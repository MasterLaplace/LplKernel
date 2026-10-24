#include <kernel/acpi/host.h>
#include <kernel/cpu/acpi.h>
#include <kernel/memory/vmm.h>

#include <stddef.h>

static uint64_t acpi_host_root_system_description_pointer(void *context)
{
    (void) context;
    return advanced_configuration_and_power_interface_find_root_system_description_pointer();
}

static const void *acpi_host_map_firmware(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    if (physical_address > UINT32_MAX || length > UINT32_MAX)
        return NULL;
    return (const void *) (uintptr_t) kernel_vmm_map_firmware_window((uint32_t) physical_address, (uint32_t) length);
}

static void acpi_host_unmap_firmware(void *context, const void *address, uint64_t length)
{
    (void) context;
    kernel_vmm_unmap_firmware_window((uint32_t) (uintptr_t) address, (uint32_t) length);
}

const AdvancedConfigurationAndPowerInterfaceHost_t advanced_configuration_and_power_interface_machine_host = {
    .context = NULL,
    .root_system_description_pointer = acpi_host_root_system_description_pointer,
    .map_firmware = acpi_host_map_firmware,
    .unmap_firmware = acpi_host_unmap_firmware,
};
