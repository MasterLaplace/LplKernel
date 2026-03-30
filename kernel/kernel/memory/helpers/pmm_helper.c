#include <kernel/memory/helpers/pmm_helper.h>
#include <kernel/cpu/pmm.h>
#include <kernel/config.h>

void write_pmm_strategy_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PMM strategy=");
    serial_write_string(serial, physical_memory_manager_get_strategy_name());
    serial_write_string(serial, "\n");
}

void write_pmm_pass_1_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PMM pass 1 (0-64MB) ready: ");
    serial_write_int(serial, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(serial, " pages free\n");
}

void write_pmm_pass_2_start_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PMM pass 2 (>16MB mapping)...\n");
}

void write_pmm_ready_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PMM ready - total free: ");
    serial_write_int(serial, (int32_t) physical_memory_manager_get_free_page_count());
    serial_write_string(serial, " pages (~");
    serial_write_int(serial, (int32_t) (physical_memory_manager_get_free_page_count() * 4 / 1024));
    serial_write_string(serial, " MB free), watermark_high=");
    serial_write_int(serial, (int32_t) physical_memory_manager_get_watermark_high());
    serial_write_string(serial, ", watermark_low=");
    serial_write_int(serial, (int32_t) physical_memory_manager_get_watermark_low());
    serial_write_string(serial, ", frag=");
    serial_write_int(serial, (int32_t) physical_memory_manager_get_fragmentation_ratio());
    serial_write_string(serial, "%\n");
}

void write_pmm_buddy_info(Serial_t *serial)
{
#if !defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PMM buddy free blocks by order: ");
    for (uint8_t order = 0u; order <= 18u; ++order)
    {
        uint32_t count = physical_memory_manager_debug_get_free_block_count(order);

        if (!count)
            continue;

        serial_write_string(serial, "o");
        serial_write_int(serial, (int32_t) order);
        serial_write_string(serial, "=");
        serial_write_int(serial, (int32_t) count);
        serial_write_string(serial, " ");
    }
    serial_write_string(serial, "\n");
#else
    (void) serial;
#endif
}

void write_pmm_guard_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: PMM guard counters: rejected_free=");
    serial_write_int(serial, (int32_t) physical_memory_manager_debug_get_rejected_free_count());
    serial_write_string(serial, ", double_free=");
    serial_write_int(serial, (int32_t) physical_memory_manager_debug_get_double_free_count());
    serial_write_string(serial, "\n");
}
