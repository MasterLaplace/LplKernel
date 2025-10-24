#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/boot/multiboot_info_helper.h>
#include <kernel/cpu/gdt_helper.h>
#include <kernel/cpu/paging.h>

static const char WELCOME_MESSAGE[] = ""
                                      "/==+--  _                                         ---+\n"
                                      "| \\|   | |                  _                        |\n"
                                      "+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
                                      "   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
                                      "   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
                                      "   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
                                      "   |                | |_                             | \\\n"
                                      "   +---             |___|                          --+==+\n\n";

extern MultibootInfo_t *multiboot_info;

static GlobalDescriptorTable_t global_descriptor_table = {0};
static Serial_t com1;

__attribute__((constructor)) void kernel_initialize(void)
{
    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: serial port initialisation successful.\n");

    terminal_initialize();

    if (!multiboot_info)
    {
        serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "] ERROR: Multiboot info is NULL in constructor!\n");
        return;
    }

    // print_multiboot_info(KERNEL_VIRTUAL_BASE, multiboot_info);
    write_multiboot_info(&com1, KERNEL_VIRTUAL_BASE, multiboot_info);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing GDT...\n");
    global_descriptor_table_init(&global_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: loading GDT into CPU...\n");
    global_descriptor_table_load(&global_descriptor_table);
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: GDT loaded successfully!\n");
    write_global_descriptor_table(&com1, &global_descriptor_table);

    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: initializing runtime paging...\n");
    paging_init_runtime();
    serial_write_string(&com1, "[" KERNEL_SYSTEM_STRING "]: runtime paging initialized successfully!\n");
}

void kernel_main(void)
{
    terminal_write_string(WELCOME_MESSAGE);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("[" KERNEL_SYSTEM_STRING "]: loading ...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write_string(KERNEL_CONFIG_STRING);

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    for (uint8_t c = 0u; (uint32_t) c != 27u;)
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
