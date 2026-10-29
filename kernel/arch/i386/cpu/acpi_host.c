#include <kernel/acpi/host.h>
#include <kernel/acpi/tables.h>
#include <kernel/cpu/acpi.h>
#include <kernel/cpu/clock.h>
#include <kernel/cpu/irq.h>
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pmm.h>
#include <kernel/lib/asmutils.h>
#include <kernel/memory/vmm.h>

#include <stdbool.h>
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

static bool acpi_host_reaches(uint64_t physical_address, uint64_t length)
{
    return length != 0u && physical_address <= UINT32_MAX && length - 1u <= UINT32_MAX - physical_address;
}

static uint8_t acpi_host_memory_type(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    switch (physical_memory_manager_memory_type(physical_address, length))
    {
    case 1u: return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RAM;
    case 2u:
    case 5u: return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_RESERVED;
    case 3u: return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_ACPI_RECLAIM;
    case 4u: return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_ACPI_NVS;
    default: return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_UNKNOWN;
    }
}

static void *acpi_host_map_firmware_writable(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    if (!acpi_host_reaches(physical_address, length))
        return NULL;
    return (void *) (uintptr_t) kernel_vmm_map_firmware_writable_window((uint32_t) physical_address, (uint32_t) length);
}

static void acpi_host_unmap_firmware_writable(void *context, void *address, uint64_t length)
{
    (void) context;
    kernel_vmm_unmap_firmware_window((uint32_t) (uintptr_t) address, (uint32_t) length);
}

static void *acpi_host_map_device(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    if (!acpi_host_reaches(physical_address, length))
        return NULL;
    return (void *) (uintptr_t) kernel_vmm_map_device_window((uint32_t) physical_address, (uint32_t) length);
}

static void acpi_host_unmap_device(void *context, void *address, uint64_t length)
{
    (void) context;
    kernel_vmm_unmap_device_window((uint32_t) (uintptr_t) address, (uint32_t) length);
}

static uint32_t acpi_host_read_memory(void *context, const void *address, uint64_t physical_address, uint8_t width)
{
    (void) context;
    (void) physical_address;
    switch (width)
    {
    case 1u: return *(const volatile uint8_t *) address;
    case 2u: return *(const volatile uint16_t *) address;
    default: return *(const volatile uint32_t *) address;
    }
}

static void acpi_host_write_memory(void *context, void *address, uint64_t physical_address, uint8_t width,
                                   uint32_t value)
{
    (void) context;
    (void) physical_address;
    switch (width)
    {
    case 1u: *(volatile uint8_t *) address = (uint8_t) value; break;
    case 2u: *(volatile uint16_t *) address = (uint16_t) value; break;
    default: *(volatile uint32_t *) address = value; break;
    }
}

static uint32_t acpi_host_compare_exchange(void *context, void *address, uint64_t physical_address, uint32_t expected,
                                           uint32_t desired)
{
    uint32_t held = expected;

    (void) context;
    (void) physical_address;
    __atomic_compare_exchange_n((volatile uint32_t *) address, &held, desired, false, __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return held;
}

static uint32_t acpi_host_port_read(void *context, uint16_t port, uint8_t width)
{
    (void) context;
    switch (width)
    {
    case 1u: return asmutils_input_byte(port);
    case 2u: return asmutils_input_word(port);
    default: return asmutils_input_dword(port);
    }
}

static void acpi_host_port_write(void *context, uint16_t port, uint8_t width, uint32_t value)
{
    (void) context;
    switch (width)
    {
    case 1u: asmutils_output_byte(port, (uint8_t) value); break;
    case 2u: asmutils_output_word(port, (uint16_t) value); break;
    default: asmutils_output_dword(port, value); break;
    }
}

static bool acpi_host_mechanism_one_reaches(uint16_t segment, uint16_t offset)
{
    return segment == 0u && offset <= 0xFFu;
}

static uint32_t acpi_host_pci_read(void *context, uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
                                   uint16_t offset, uint8_t width)
{
    (void) context;
    if (!acpi_host_mechanism_one_reaches(segment, offset))
        return (width >= 4u) ? 0xFFFFFFFFu : ((1u << (8u * width)) - 1u);
    return peripheral_component_interconnect_config_read(bus, device, function, (uint8_t) offset, width);
}

static void acpi_host_pci_write(void *context, uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
                                uint16_t offset, uint8_t width, uint32_t value)
{
    (void) context;
    if (acpi_host_mechanism_one_reaches(segment, offset))
        peripheral_component_interconnect_config_write(bus, device, function, (uint8_t) offset, width, value);
}

static uint32_t acpi_host_sleep_milliseconds(void *context, uint32_t milliseconds)
{
    const uint32_t hertz = clock_get_tick_hz();
    const uint32_t start = clock_get_tick_count();
    InterruptRequestDeadline_t deadline;

    (void) context;
    if (hertz == 0u)
        return 0u;
    interrupt_request_deadline_start(&deadline,
                                     (milliseconds > UINT32_MAX / 1000u) ? UINT32_MAX : milliseconds * 1000u);
    while (!interrupt_request_deadline_passed(&deadline))
        asmutils_halt();
    return (uint32_t) (((uint64_t) (clock_get_tick_count() - start) * 1000u) / hertz);
}

static uint32_t acpi_host_stall_microseconds(void *context, uint32_t microseconds)
{
    const AdvancedConfigurationAndPowerInterfacePowerManagementTimer_t *timer =
        advanced_configuration_and_power_interface_power_management_timer();
    const uint32_t hertz = clock_get_tick_hz();
    const uint32_t start = clock_get_tick_count();

    (void) context;
    if (timer->state == ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_POWER_MANAGEMENT_TIMER_USABLE)
        return advanced_configuration_and_power_interface_power_management_timer_stall(
            &advanced_configuration_and_power_interface_machine_host, timer, microseconds);
    if (hertz == 0u)
        return 0u;
    interrupt_request_spin_microseconds(microseconds);
    return (uint32_t) (((uint64_t) (clock_get_tick_count() - start) * 1000000u) / hertz);
}

static uint64_t acpi_host_clock_100ns(void *context)
{
    static uint32_t last_ticks;
    static uint64_t wrapped_ticks;
    const uint32_t hertz = clock_get_tick_hz();
    const uint32_t ticks = clock_get_tick_count();

    (void) context;
    if (hertz == 0u)
        return 0u;
    if (ticks < last_ticks)
        wrapped_ticks += 1ull << 32u;
    last_ticks = ticks;
    return ((wrapped_ticks + ticks) * 10000000u) / hertz;
}

static void *acpi_host_allocate_pages(void *context, uint32_t count)
{
    (void) context;
    return kernel_vmm_alloc_pages(count);
}

static void acpi_host_release_pages(void *context, void *address, uint32_t count)
{
    (void) context;
    kernel_vmm_free_pages(address, count);
}

const AdvancedConfigurationAndPowerInterfaceHost_t advanced_configuration_and_power_interface_machine_host = {
    .context = NULL,
    .root_system_description_pointer = acpi_host_root_system_description_pointer,
    .map_firmware = acpi_host_map_firmware,
    .unmap_firmware = acpi_host_unmap_firmware,
    .memory_type = acpi_host_memory_type,
    .map_firmware_writable = acpi_host_map_firmware_writable,
    .unmap_firmware_writable = acpi_host_unmap_firmware_writable,
    .map_device = acpi_host_map_device,
    .unmap_device = acpi_host_unmap_device,
    .read_memory = acpi_host_read_memory,
    .write_memory = acpi_host_write_memory,
    .compare_exchange = acpi_host_compare_exchange,
    .port_read = acpi_host_port_read,
    .port_write = acpi_host_port_write,
    .pci_read = acpi_host_pci_read,
    .pci_write = acpi_host_pci_write,
    .sleep_milliseconds = acpi_host_sleep_milliseconds,
    .stall_microseconds = acpi_host_stall_microseconds,
    .clock_100ns = acpi_host_clock_100ns,
    .allocate_pages = acpi_host_allocate_pages,
    .release_pages = acpi_host_release_pages,
};
