#include <kernel/config.h>
#include <kernel/memory/heap.h>
#include <kernel/memory/helpers/heap_helper.h>
#include <kernel/memory/slab.h>
#include <kernel/memory/tlsf.h>

void write_heap_info(Serial_t *serial)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: heap strategy=");
    serial_write_string(serial, kernel_heap_get_strategy_name());
#ifdef LPL_KERNEL_REAL_TIME_MODE
    serial_write_string(serial, ", tlsf_pool=");
    serial_write_int(serial, (int32_t) (kernel_tlsf_get_pool_size() / 1024));
    serial_write_string(serial, "KB\n");
#else
    serial_write_string(serial, "\n");
#endif
    serial_write_string(serial, ", small_free_blocks=");
    serial_write_int(serial, (int32_t) kernel_heap_get_small_free_block_count());
    serial_write_string(serial, ", small_free_bytes=");
    serial_write_int(serial, (int32_t) kernel_heap_get_small_free_bytes());
    serial_write_string(serial, ", rejected_free=");
    serial_write_int(serial, (int32_t) kernel_heap_debug_get_rejected_free_count());
    serial_write_string(serial, ", double_free=");
    serial_write_int(serial, (int32_t) kernel_heap_debug_get_double_free_count());
    serial_write_string(serial, "\n");
}

void write_heap_extended_info(Serial_t *serial)
{
#if defined(LPL_KERNEL_REAL_TIME_MODE)
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: slab free: sz16=");
    serial_write_int(serial, (int32_t) kernel_slab_get_free_count(16u));
    serial_write_string(serial, ", sz64=");
    serial_write_int(serial, (int32_t) kernel_slab_get_free_count(64u));
    serial_write_string(serial, ", sz256=");
    serial_write_int(serial, (int32_t) kernel_slab_get_free_count(256u));
    serial_write_string(serial, ", hot_loop_depth=");
    serial_write_int(serial, (int32_t) kernel_heap_get_hot_loop_depth());
    serial_write_string(serial, ", hot_loop_violations=");
    serial_write_int(serial, (int32_t) kernel_heap_get_hot_loop_violation_count());
    serial_write_string(serial, "\n");
#else
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: server heap domains=");
    serial_write_int(serial, (int32_t) kernel_heap_get_server_domain_count());
    serial_write_string(serial, ", active=");
    serial_write_int(serial, (int32_t) kernel_heap_get_server_active_domain());
    serial_write_string(serial, ", first_fit_fallbacks=");
    serial_write_int(serial, (int32_t) kernel_heap_get_server_domain_first_fit_fallback_count(
                                 kernel_heap_get_server_active_domain()));
    serial_write_string(serial, ", remote_probe=");
    serial_write_int(
        serial, (int32_t) kernel_heap_get_server_domain_remote_probe_count(kernel_heap_get_server_active_domain()));
    serial_write_string(serial, ", remote_hit=");
    serial_write_int(serial,
                     (int32_t) kernel_heap_get_server_domain_remote_hit_count(kernel_heap_get_server_active_domain()));
    serial_write_string(serial, "\n");

    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: sizeclass buckets free/hits: ");
    for (uint32_t sc = 0u; sc < 7u; ++sc)
    {
        serial_write_string(serial, "s");
        serial_write_int(serial, (int32_t) sc);
        serial_write_string(serial, "=");
        serial_write_int(serial, (int32_t) kernel_heap_get_size_class_free_count(sc));
        serial_write_string(serial, "/");
        serial_write_int(serial, (int32_t) kernel_heap_get_size_class_hit_count(sc));
        serial_write_string(serial, "/");
        serial_write_int(
            serial, (int32_t) kernel_heap_get_server_domain_refill_count(kernel_heap_get_server_active_domain(), sc));
        serial_write_string(serial, " ");
    }
    serial_write_string(serial, "\n");
#endif
}
