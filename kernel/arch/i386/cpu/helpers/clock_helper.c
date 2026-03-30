#include <kernel/cpu/helpers/clock_helper.h>
#include <kernel/cpu/clock.h>
#include <kernel/config.h>

void write_clock_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: clock profile=");
    serial_write_string(serial, clock_get_profile_name());
    serial_write_string(serial, ", backend=");
    serial_write_string(serial, clock_get_backend_name());
    serial_write_string(serial, ", hz=");
    serial_write_int(serial, (int32_t) clock_get_tick_hz());
    serial_write_string(serial, ", rtc_periodic=");
    serial_write_int(serial, (int32_t) clock_is_rtc_periodic_enabled());
    serial_write_string(serial, "\n");
}
