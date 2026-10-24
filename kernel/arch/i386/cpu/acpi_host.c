#include <kernel/acpi/host.h>
#include <kernel/cpu/acpi.h>
#include <kernel/memory/vmm.h>

#include <stddef.h>

uint64_t advanced_configuration_and_power_interface_host_root_system_description_pointer(void)
{
    return advanced_configuration_and_power_interface_find_root_system_description_pointer();
}

const void *advanced_configuration_and_power_interface_host_map_firmware(uint64_t physical_address, uint64_t length)
{
    if (physical_address > UINT32_MAX || length > UINT32_MAX)
        return NULL;
    return (const void *) (uintptr_t) kernel_vmm_map_firmware_window((uint32_t) physical_address, (uint32_t) length);
}

void advanced_configuration_and_power_interface_host_unmap_firmware(const void *address, uint64_t length)
{
    kernel_vmm_unmap_firmware_window((uint32_t) (uintptr_t) address, (uint32_t) length);
}
