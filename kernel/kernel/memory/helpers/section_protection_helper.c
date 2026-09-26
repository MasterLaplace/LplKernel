#include <kernel/diag/telemetry.h>
#include <kernel/memory/helpers/section_protection_helper.h>
#include <kernel/memory/section_protection.h>

void write_section_protection_info(Serial_t *serial, bool applied)
{
    kernel_telemetry_begin_record(serial, "section_protection");
    kernel_telemetry_write_boolean("applied", applied);
    kernel_telemetry_write_boolean("write_protect", kernel_section_protection_write_protect_is_enabled());
    kernel_telemetry_write_boolean("active", kernel_section_protection_is_active());
    kernel_telemetry_write_unsigned("read_only_pages", kernel_section_protection_get_read_only_page_count());
    kernel_telemetry_write_hexadecimal("range_start", kernel_section_protection_get_range_start());
    kernel_telemetry_write_hexadecimal("range_end", kernel_section_protection_get_range_end());
    kernel_telemetry_end_record();
}
