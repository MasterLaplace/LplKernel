#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/tty.h>
#include <kernel/serial.h>

static const char WELCOME_MESSAGE[] = ""
"/==+--  _                                         ---+\n"
"| \\|   | |                  _                        |\n"
"+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
"   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
"   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
"   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
"   |                | |_                             | \\\n"
"   +---             |___|                          --+==+\n\n";

static serial_t com1;

__attribute__ ((constructor)) void kernel_initialize(void)
{
    serial_initialize(&com1, COM1, 9600);
    serial_write_string(&com1, "["KERNEL_SYSTEM_STRING"]: serial port initialisation successful.\n");
    terminal_initialize();
}

void kernel_main(void)
{
    terminal_write_string(WELCOME_MESSAGE);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("["KERNEL_SYSTEM_STRING"]: loading ...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write_string(KERNEL_CONFIG_STRING);

    for (;;)
        inb(42);
}

__attribute__ ((destructor)) void kernel_cleanup(void)
{
    terminal_write_string("\nLaplace kernel exiting ...\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("Kernel panic!\n");
}
