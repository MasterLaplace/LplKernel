#include "platform.h"

#include <kernel/acpi/host.h>
#include <kernel/acpi/log.h>
#include <kernel/boot/boot_module.h>
#include <kernel/config.h>
#include <kernel/drivers/serial.h>
#include <kernel/memory/vmm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define PLATFORM_MAX_RESERVATIONS 64u /**< Reservations the build machine's stand-in for the VMM holds at once. */

/**
 * @brief A run of address space kernel_vmm_reserve_pages gave on the build machine: mapped with no access, until
 *        kernel_vmm_commit_pages opens pages of it.
 */
typedef struct PlatformReservation {
    void *address; /**< Its first byte, or NULL when the entry is free. */
    size_t bytes;  /**< Its length. */
} PlatformReservation_t;

static PlatformReservation_t platform_reservations[PLATFORM_MAX_RESERVATIONS];

/**
 * @brief The reservation that holds @p address, or NULL when none does.
 */
static PlatformReservation_t *platform_reservation_holding(const void *address)
{
    for (uint32_t index = 0u; index < PLATFORM_MAX_RESERVATIONS; ++index)
    {
        PlatformReservation_t *reservation = &platform_reservations[index];
        const uint8_t *start = reservation->address;

        if (start != NULL && (const uint8_t *) address >= start &&
            (const uint8_t *) address < start + reservation->bytes)
            return reservation;
    }
    return NULL;
}

/**
 * @brief An entry of the table no reservation holds, or NULL when every one does.
 */
static PlatformReservation_t *platform_free_reservation(void)
{
    for (uint32_t index = 0u; index < PLATFORM_MAX_RESERVATIONS; ++index)
    {
        if (platform_reservations[index].address == NULL)
            return &platform_reservations[index];
    }
    return NULL;
}

static const AdvancedConfigurationAndPowerInterfaceHost_t *platform_firmware;
static const char *platform_module_name;
static const uint8_t *platform_module_bytes;
static uint32_t platform_module_length;
static const char *platform_boot_word;

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

static uint32_t platform_read_no_function(void *context, uint16_t segment, uint8_t bus, uint8_t device,
                                          uint8_t function, uint16_t offset, uint8_t width)
{
    (void) context;
    (void) segment;
    (void) bus;
    (void) device;
    (void) function;
    (void) offset;
    return (width >= 4u) ? 0xFFFFFFFFu : ((1u << (8u * width)) - 1u);
}

static void platform_write_no_function(void *context, uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
                                       uint16_t offset, uint8_t width, uint32_t value)
{
    (void) context;
    (void) segment;
    (void) bus;
    (void) device;
    (void) function;
    (void) offset;
    (void) width;
    (void) value;
}

static uint32_t platform_sleep_without_a_clock(void *context, uint32_t microseconds)
{
    (void) context;
    (void) microseconds;
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

static void *platform_reserve_pages(void *context, uint32_t count)
{
    (void) context;
    return kernel_vmm_reserve_pages(count);
}

static bool platform_commit_pages(void *context, void *address, uint32_t count)
{
    (void) context;
    return kernel_vmm_commit_pages(address, count);
}

static void platform_release_reservation(void *context, void *address, uint32_t reserved, uint32_t committed)
{
    (void) context;
    (void) committed;
    kernel_vmm_free_pages(address, reserved);
}

static void platform_log(void *context, uint8_t level, const char *text)
{
    (void) context;
    printf("[" KERNEL_SYSTEM_STRING "]: ACPI %s: %s\n",
           advanced_configuration_and_power_interface_log_level_name(level), text);
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
    .pci_read = platform_read_no_function,
    .pci_write = platform_write_no_function,
    .sleep_microseconds = platform_sleep_without_a_clock,
    .stall_microseconds = platform_stall_without_a_clock,
    .clock_100ns = platform_no_clock,
    .allocate_pages = platform_allocate_pages,
    .release_pages = platform_release_pages,
    .reserve_pages = platform_reserve_pages,
    .commit_pages = platform_commit_pages,
    .release_reservation = platform_release_reservation,
    .log = platform_log,
};

void *kernel_vmm_alloc_pages(uint32_t page_count)
{
    return page_count == 0u ? NULL : aligned_alloc(4096u, (size_t) page_count * 4096u);
}

void *kernel_vmm_reserve_pages(uint32_t page_count)
{
    PlatformReservation_t *free_entry = platform_free_reservation();
    void *pages = NULL;

    if (page_count == 0u || free_entry == NULL)
        return NULL;
    pages = mmap(NULL, (size_t) page_count * 4096u, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pages == MAP_FAILED)
        return NULL;
    free_entry->address = pages;
    free_entry->bytes = (size_t) page_count * 4096u;
    return pages;
}

bool kernel_vmm_commit_pages(void *address, uint32_t page_count)
{
    const PlatformReservation_t *reservation = platform_reservation_holding(address);
    const size_t bytes = (size_t) page_count * 4096u;

    return page_count != 0u && reservation != NULL &&
           bytes <= reservation->bytes - (size_t) ((uint8_t *) address - (uint8_t *) reservation->address) &&
           mprotect(address, bytes, PROT_READ | PROT_WRITE) == 0;
}

void kernel_vmm_free_pages(void *ptr, uint32_t page_count)
{
    PlatformReservation_t *reservation = platform_reservation_holding(ptr);

    (void) page_count;
    if (reservation == NULL)
    {
        free(ptr);
        return;
    }
    if (reservation->address != ptr)
        return;
    munmap(reservation->address, reservation->bytes);
    reservation->address = NULL;
    reservation->bytes = 0u;
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

bool boot_command_line_holds(const char *word)
{
    return platform_boot_word != NULL && strcmp(platform_boot_word, word) == 0;
}

bool advanced_configuration_and_power_interface_boot_line_holds(const char *word)
{
    return boot_command_line_holds(word);
}

void platform_hold_boot_word(const char *word) { platform_boot_word = word; }
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
