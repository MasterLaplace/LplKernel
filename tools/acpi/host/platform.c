#include <kernel/acpi/host.h>
#include <kernel/boot/boot_module.h>
#include <kernel/drivers/serial.h>
#include <kernel/memory/vmm.h>

#include <stdio.h>
#include <stdlib.h>

static uint64_t platform_no_root_system_description_pointer(void *context)
{
    (void) context;
    return 0u;
}

static const void *platform_map_no_firmware(void *context, uint64_t physical_address, uint64_t length)
{
    (void) context;
    (void) physical_address;
    (void) length;
    return NULL;
}

static void platform_unmap_no_firmware(void *context, const void *address, uint64_t length)
{
    (void) context;
    (void) address;
    (void) length;
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
    .root_system_description_pointer = platform_no_root_system_description_pointer,
    .map_firmware = platform_map_no_firmware,
    .unmap_firmware = platform_unmap_no_firmware,
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
