#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/multiboot_info.h>
#include <kernel/serial.h>
#include <kernel/tty.h>

#define KERNEL_START 0xC0000000

static const char WELCOME_MESSAGE[] = ""
                                      "/==+--  _                                         ---+\n"
                                      "| \\|   | |                  _                        |\n"
                                      "+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
                                      "   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
                                      "   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
                                      "   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
                                      "   |                | |_                             | \\\n"
                                      "   +---             |___|                          --+==+\n\n";

static Serial_t com1;
static MultibootInfo_t *mbi = NULL;

extern MultibootInfo_t *g_multiboot_info;

static void *physical_to_virtual_low(uint32_t phys_addr)
{
    if (phys_addr < 0x100000)
        return (void *) (phys_addr + KERNEL_START);

    return NULL;
}

static void print_multiboot_string(uint32_t phys_addr, const char *label)
{
    char *str = (char *) physical_to_virtual_low(phys_addr);
    if (str)
    {
        terminal_write_string(label);
        terminal_write_string(str);
        terminal_write_string("\n");
    }
    else
    {
        terminal_write_string(label);
        terminal_write_string("(not accessible)\n");
    }
}

// === MULTIBOOT DISPLAY FUNCTIONS ===

static void print_section_header(const char *title, uint8_t color)
{
    terminal_setcolor(vga_entry_color(color, VGA_COLOR_BLACK));
    terminal_write_string("\n--- ");
    terminal_write_string(title);
    terminal_write_string(" ---\n");
}

static void print_memory_info(MultibootInfo_t *mbi)
{
    if (!(mbi->flags & 0x1))
        return;

    print_section_header("Memory Info", VGA_COLOR_LIGHT_GREEN);

    terminal_write_string("Lower memory: ");
    terminal_write_number(mbi->mem_lower, 10);
    terminal_write_string(" KB\n");

    terminal_write_string("Upper memory: ");
    terminal_write_number(mbi->mem_upper, 10);
    terminal_write_string(" KB\n");

    uint32_t total_kb = mbi->mem_lower + mbi->mem_upper;
    terminal_write_string("Total memory: ~");
    terminal_write_number(total_kb / 1024, 10);
    terminal_write_string(" MB\n");
}

static void print_boot_device_info(MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 1)))
        return;

    print_section_header("Boot Device", VGA_COLOR_LIGHT_BROWN);

    // According to the Multiboot1 specification, boot_device is an encoded uint32_t
    // The 4 bytes represent: [drive][part1][part2][part3]
    uint32_t device = (uint32_t) (long) mbi->boot_device;

    terminal_write_string("Boot device: 0x");
    terminal_write_number(device, 16);

    // Decode the boot device components
    uint8_t drive = (device >> 24) & 0xFF;
    uint8_t part1 = (device >> 16) & 0xFF;
    uint8_t part2 = (device >> 8) & 0xFF;
    uint8_t part3 = device & 0xFF;

    terminal_write_string(" (Drive: ");
    terminal_write_number(drive, 10);
    terminal_write_string(", Parts: ");
    terminal_write_number(part1, 10);
    terminal_write_string("/");
    terminal_write_number(part2, 10);
    terminal_write_string("/");
    terminal_write_number(part3, 10);
    terminal_write_string(")\n");

    if (drive == 0x80)
    {
        terminal_write_string("  -> First hard disk (0x80)\n");
    }
    else if (drive == 0x00)
    {
        terminal_write_string("  -> First floppy disk (0x00)\n");
    }
    else if (drive >= 0x80)
    {
        terminal_write_string("  -> Hard disk #");
        terminal_write_number(drive - 0x80 + 1, 10);
        terminal_write_string("\n");
    }
    else
    {
        terminal_write_string("  -> Unknown/Virtual device\n");
    }
}

static void print_modules_info(MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 3)))
        return;

    print_section_header("Modules", VGA_COLOR_LIGHT_MAGENTA);

    terminal_write_string("Modules count: ");
    terminal_write_number(mbi->mods_count, 10);
    terminal_write_string("\n");
    terminal_write_string("Modules addr: 0x");
    terminal_write_number((long) mbi->mods_addr, 16);
    terminal_write_string("\n");
}

static void print_memory_map_info(MultibootInfo_t *mbi)
{
    if (!(mbi->flags & (1 << 6)))
        return;

    print_section_header("Memory Map", VGA_COLOR_LIGHT_CYAN);

    terminal_write_string("Memory map length: ");
    terminal_write_number(mbi->mmap_length, 10);
    terminal_write_string(" bytes\n");
    terminal_write_string("Memory map addr: 0x");
    terminal_write_number((long) mbi->mmap_addr, 16);
    terminal_write_string("\n");

    MemoryMapEntry_t *mmap = (MemoryMapEntry_t *) physical_to_virtual_low((uint32_t) (long) mbi->mmap_addr);
    if (mmap && mbi->mmap_length >= sizeof(MemoryMapEntry_t))
    {
        terminal_write_string("First memory region:\n");
        terminal_write_string("  Base: 0x");
        terminal_write_number((uint32_t) mmap->base_addr, 16);
        terminal_write_string(", Length: 0x");
        terminal_write_number((uint32_t) mmap->length, 16);
        terminal_write_string(", Type: ");
        terminal_write_number(mmap->type, 10);
        const char *type_str = (mmap->type == 1) ? " (Available)" :
                               (mmap->type == 2) ? " (Reserved)" :
                               (mmap->type == 3) ? " (ACPI)" :
                                                   " (Other)";
        terminal_write_string(type_str);
        terminal_write_string("\n");
    }
    else
    {
        terminal_write_string("  Memory map not accessible\n");
    }
}

static void print_multiboot_summary(MultibootInfo_t *mbi)
{
    print_section_header("Multiboot Summary", VGA_COLOR_WHITE);

    terminal_write_string("Multiboot flags: 0x");
    terminal_write_number(mbi->flags, 16);
    terminal_write_string("\n");

    terminal_write_string("Multiboot info at: 0x");
    terminal_write_number((long) mbi, 16);
    terminal_write_string("\n");
}

__attribute__((constructor)) void kernel_initialize(void)
{
    mbi = g_multiboot_info;

    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: serial port initialisation successful.\n");

    terminal_initialize();

    if (mbi)
    {
        if (mbi->flags & (1 << 9))
        {
            print_multiboot_string(mbi->boot_loader_name, "Bootloader: ");
        }

        if (mbi->flags & (1 << 2))
        {
            print_multiboot_string(mbi->cmdline, "Command line: ");
        }

        print_memory_info(mbi);
        print_boot_device_info(mbi);
        print_modules_info(mbi);
        print_memory_map_info(mbi);
        print_multiboot_summary(mbi);
    }
    else
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_write_string("ERROR: Multiboot info is NULL in constructor!\n");
    }
}

void kernel_main(void)
{
    // terminal_write_string(WELCOME_MESSAGE);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    // terminal_write_string("["KERNEL_SYSTEM_STRING"]: loading ...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    // terminal_write_string(KERNEL_CONFIG_STRING);

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    uint8_t c = 0u;
    for (; (uint32_t) c != 27u;)
    {
        c = serial_read_char(&com1);
        terminal_putchar(c);
    }
}

__attribute__((destructor)) void kernel_cleanup(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("\n[" KERNEL_SYSTEM_STRING "]: exiting ... panic!\n");
}
