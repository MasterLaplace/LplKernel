#include <stdio.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#endif

int putchar(int ic)
{
#if defined(__is_libk)
    terminal_putchar((char) ic);
#else
    // TODO: Implement stdio and the write system call.
#endif
    return ic;
}
