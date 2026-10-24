#include <kernel/acpi/host.h>
#include <kernel/boot/boot_module.h>
#include <kernel/drivers/serial.h>

#include <stdio.h>

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

const AdvancedConfigurationAndPowerInterfaceHost_t advanced_configuration_and_power_interface_machine_host = {
    .context = NULL,
    .root_system_description_pointer = platform_no_root_system_description_pointer,
    .map_firmware = platform_map_no_firmware,
    .unmap_firmware = platform_unmap_no_firmware,
};

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
