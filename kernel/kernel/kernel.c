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

void kernel_main(void)
{
    terminal_initialize();

    terminal_write_string(WELCOME_MESSAGE);

    terminal_write_string("Laplace kernel loading ...\n\n");
    terminal_write_string(KERNEL_CONFIG_STRING);

    for (;;);
}
