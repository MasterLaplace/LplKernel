#ifndef KERNEL_CORE_CONSOLE_H
#define KERNEL_CORE_CONSOLE_H

#include <kernel/drivers/serial.h>

/**
 * @brief Enters the interactive kernel console loop.
 *
 * Starts a non-blocking loop handling keyboard and serial input
 * to process basic kernel diagnostic commands.
 *
 * @param com1 Pointer to the primary serial interface.
 */
extern void kernel_console_run_interactive_loop(Serial_t *com1);

#endif /* KERNEL_CORE_CONSOLE_H */
