/**
 * @file hal_console.c
 * @brief Console backend for the engine HAL.
 *
 * Implements the hardware_abstraction_layer_console_* contract over the COM1 serial port, the same
 * sink the kernel's own boot diagnostics use. The engine installs a logger on
 * top of this (see lpl::platform::kernel::KernelLogger), so engine-side
 * lpl::core::Log calls appear interleaved with kernel output on one console.
 *
 * Text only: no glyphs, no framebuffer. Drawing stays engine-side.
 */
#include <kernel/hal/hal.h>

#include <kernel/drivers/serial.h>

#include <stddef.h>

void hardware_abstraction_layer_console_write_string(const char *text)
{
    if (text == NULL)
        return;
    serial_write_string(&com1, text);
}
