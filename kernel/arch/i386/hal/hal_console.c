#include <kernel/hal/hal.h>

#include <kernel/drivers/serial.h>

#include <stddef.h>

void hardware_abstraction_layer_console_write_string(const char *text)
{
    if (text == NULL)
        return;
    serial_write_string(&com1, text);
}
