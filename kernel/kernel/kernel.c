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

static Serial_t com1;

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

    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));

    uint8_t c = 0u;
    for (; (uint32_t)c != 27u;)
    {
        c = serial_read_char(&com1);
        terminal_putchar(c);
    }
}

__attribute__ ((destructor)) void kernel_cleanup(void)
{
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write_string("\n["KERNEL_SYSTEM_STRING"]: exiting ... panic!\n");
}
