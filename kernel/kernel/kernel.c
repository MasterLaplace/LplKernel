#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <kernel/tty.h>
#include <stdio.h>

const char WELCOME_MESSAGE[] = ""
"/==+--  _                                         ---+\n"
"| \\|   | |                  _                        |\n"
"+  |   | |     __ _  ____  | |    __ _  ____  ___    |\n"
"   |   | |    / _` ||  _ \\ | |   / _` |/ ___\\/ _ \\   |\n"
"   |   | |___| (_| || |_| || |__| (_| | (___|  __/   |\\\n"
"   |   \\_____|\\__,_|| ___/ \\____|\\__,_|\\____/\\___|   ||\n"
"   |                | |_                             | \\\n"
"   +---             |___|                          --+==+\n\n";

__attribute__ ((constructor)) void initialize(void)
{
    terminal_initialize();
}

void kernel_main(void)
{
    terminal_write_string(WELCOME_MESSAGE);
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_write_string("Laplace kernel loading ...\n\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write_string(KERNEL_CONFIG_STRING);

    for (;;);
}

__attribute__ ((destructor)) void cleanup(void)
{
    terminal_write_string("\nLaplace kernel exiting ...\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("Kernel panic!\n");
}
