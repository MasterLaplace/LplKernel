#define LAPLACE_KERNEL_PANIC
#include <kernel/config.h>

#include <stdio.h>
#include <kernel/tty.h>

void kernel_main(void)
{
    terminal_initialize();

    terminal_write_string("Laplace kernel loading ...\n");
    terminal_write_string(KERNEL_CONFIG_STRING);
}
