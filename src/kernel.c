#define LAPLACE_KERNEL_PANIC
#define LIB_C_IMPLEMENTATION
#define TERMINAL_IMPLEMENTATION
#include "terminal.h"

void kernel_main(void)
{
    terminal_initialize();

    terminal_write_string("Laplace kernel loading ...\n");
    terminal_write_string(KERNEL_CONFIG_STRING);
}
