#include "platform.h"

#include <kernel/acpi/host.h>
#include <kernel/boot/boot_module.h>
#include <kernel/drivers/serial.h>
#include <kernel/memory/vmm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const AdvancedConfigurationAndPowerInterfaceHost_t *platform_firmware;
static const char *platform_module_name;
static const uint8_t *platform_module_bytes;
static uint32_t platform_module_length;

static uint64_t platform_root_system_description_pointer(void *context)
{
    (void) context;
    if (platform_firmware == NULL)
        return 0u;
    return platform_firmware->root_system_description_pointer(platform_firmware->context);
}

static const void *platform_map_firmware(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    if (platform_firmware == NULL)
        return NULL;
    return platform_firmware->map_firmware(platform_firmware->context, physical_address, length);
}

static void platform_unmap_firmware(void *context, const void *address, uint64_t length)
{
    (void) context;
    if (platform_firmware != NULL)
        platform_firmware->unmap_firmware(platform_firmware->context, address, length);
}

static uint8_t platform_no_memory_type(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    (void) physical_address;
    (void) length;
    return ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MEMORY_UNKNOWN;
}

static void *platform_map_no_memory(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    (void) physical_address;
    (void) length;
    return NULL;
}

static void platform_unmap_no_memory(void *context, void *address, uint64_t length)
{
    (void) context;
    (void) address;
    (void) length;
}

static uint32_t platform_read_no_memory(void *context, const void *address, uint64_t physical_address, uint8_t width)
{
    (void) context;
    (void) address;
    (void) physical_address;
    (void) width;
    return 0u;
}

static void platform_write_no_memory(void *context, void *address, uint64_t physical_address, uint8_t width,
                                     uint32_t value)
{
    (void) context;
    (void) address;
    (void) physical_address;
    (void) width;
    (void) value;
}

static uint32_t platform_exchange_no_memory(void *context, void *address, uint64_t physical_address, uint32_t expected,
                                            uint32_t desired)
{
    (void) context;
    (void) address;
    (void) physical_address;
    (void) desired;
    return expected;
}

static uint32_t platform_read_no_port(void *context, uint16_t port, uint8_t width)
{
    (void) context;
    (void) port;
    (void) width;
    return 0u;
}

static void platform_write_no_port(void *context, uint16_t port, uint8_t width, uint32_t value)
{
    (void) context;
    (void) port;
    (void) width;
    (void) value;
}

static uint32_t platform_sleep_without_a_clock(void *context, uint32_t milliseconds)
{
    (void) context;
    (void) milliseconds;
    return 0u;
}

static uint32_t platform_stall_without_a_clock(void *context, uint32_t microseconds)
{
    (void) context;
    (void) microseconds;
    return 0u;
}

static uint64_t platform_no_clock(void *context)
{
    (void) context;
    return 0u;
}

static void *platform_allocate_pages(void *context, uint32_t count)
{
    (void) context;
    return kernel_vmm_alloc_pages(count);
}

static void platform_release_pages(void *context, void *address, uint32_t count)
{
    (void) context;
    kernel_vmm_free_pages(address, count);
}

const AdvancedConfigurationAndPowerInterfaceHost_t advanced_configuration_and_power_interface_machine_host = {
    .context = NULL,
    .root_system_description_pointer = platform_root_system_description_pointer,
    .map_firmware = platform_map_firmware,
    .unmap_firmware = platform_unmap_firmware,
    .memory_type = platform_no_memory_type,
    .map_firmware_writable = platform_map_no_memory,
    .unmap_firmware_writable = platform_unmap_no_memory,
    .map_device = platform_map_no_memory,
    .unmap_device = platform_unmap_no_memory,
    .read_memory = platform_read_no_memory,
    .write_memory = platform_write_no_memory,
    .compare_exchange = platform_exchange_no_memory,
    .port_read = platform_read_no_port,
    .port_write = platform_write_no_port,
    .sleep_milliseconds = platform_sleep_without_a_clock,
    .stall_microseconds = platform_stall_without_a_clock,
    .clock_100ns = platform_no_clock,
    .allocate_pages = platform_allocate_pages,
    .release_pages = platform_release_pages,
};

void *kernel_vmm_alloc_pages(uint32_t page_count)
{
    return page_count == 0u ? NULL : aligned_alloc(4096u, (size_t) page_count * 4096u);
}

void kernel_vmm_free_pages(void *ptr, uint32_t page_count)
{
    (void) page_count;
    free(ptr);
}

void serial_write_char(Serial_t *serial, char c)
{
    (void) serial;
    putchar(c);
}

void serial_write_string(Serial_t *serial, const char *data)
{
    (void) serial;
    if (data != NULL)
        fputs(data, stdout);
}

void serial_write_unsigned(Serial_t *serial, uint32_t value)
{
    (void) serial;
    printf("%u", value);
}

void serial_write_hex32(Serial_t *serial, uint32_t value)
{
    (void) serial;
    printf("0x%08X", value);
}

const char *boot_command_line_option(const char *option)
{
    (void) option;
    return NULL;
}

uint32_t boot_command_line_dropped(void) { return 0u; }
bool boot_module_find(const char *suffix, const uint8_t **out_bytes, uint32_t *out_size)
{
    const size_t suffix_length = strlen(suffix);
    const size_t name_length = (platform_module_name != NULL) ? strlen(platform_module_name) : 0u;

    if (platform_module_bytes == NULL || platform_module_length == 0u || name_length < suffix_length ||
        strcmp(platform_module_name + name_length - suffix_length, suffix) != 0)
        return false;
    *out_bytes = platform_module_bytes;
    *out_size = platform_module_length;
    return true;
}

void platform_serve_boot_module(const char *name, const uint8_t *bytes, uint32_t length)
{
    platform_module_name = name;
    platform_module_bytes = bytes;
    platform_module_length = length;
}

void platform_serve_firmware(const AdvancedConfigurationAndPowerInterfaceHost_t *firmware)
{
    platform_firmware = firmware;
}
