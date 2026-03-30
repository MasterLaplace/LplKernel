#include <kernel/config.h>
#include <kernel/drivers/helpers/keyboard_helper.h>
#include <kernel/drivers/keyboard.h>

void write_keyboard_runtime_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: keyboard runtime: irq=");
    serial_write_int(serial, (int32_t) keyboard_get_irq_count());
    serial_write_string(serial, ", printable=");
    serial_write_int(serial, (int32_t) keyboard_get_printable_count());
    serial_write_string(serial, ", pending=");
    serial_write_int(serial, (int32_t) keyboard_get_pending_char_count());
    serial_write_string(serial, ", dropped=");
    serial_write_int(serial, (int32_t) keyboard_get_dropped_char_count());
    serial_write_string(serial, ", last='");
    serial_write_int(serial, (int32_t) keyboard_get_last_printable_char());
    serial_write_string(serial, "'\n");
}
