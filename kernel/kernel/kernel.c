#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/cpu/gdt.h>
#include <kernel/boot/multiboot_info_helper.h>

extern const uint32_t global_kernel_start;
#define KERNEL_START ((uint32_t) (uintptr_t) &global_kernel_start)

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

extern MultibootInfo_t *global_multiboot_info;

__attribute__((constructor)) void kernel_initialize(void)
{
    mbi = global_multiboot_info;

    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: serial port initialisation successful.\n");

    terminal_initialize();

    if (!mbi)
    {
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        terminal_write_string("ERROR: Multiboot info is NULL in constructor!\n");
        return;
    }

    write_multiboot_info(&com1, KERNEL_START, mbi);
}

void kernel_main(void)
{
    terminal_write_string(WELCOME_MESSAGE);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("[" KERNEL_SYSTEM_STRING "]: loading ...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write_string(KERNEL_CONFIG_STRING);

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
