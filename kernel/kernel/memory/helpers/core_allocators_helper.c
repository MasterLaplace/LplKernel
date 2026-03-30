#include <kernel/memory/helpers/core_allocators_helper.h>
#include <kernel/memory/frame_arena.h>
#include <kernel/memory/pool_allocator.h>
#include <kernel/memory/stack_allocator.h>
#include <kernel/memory/ring_buffer.h>
#include <kernel/config.h>

void write_core_allocators_info(Serial_t *serial, bool frame_arena_ok, bool stack_allocator_ok, bool pool_allocator_ok, bool pinned_ok)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: core allocators ready: arena=");
    serial_write_int(serial, (int32_t) frame_arena_ok);
    serial_write_string(serial, ", stack=");
    serial_write_int(serial, (int32_t) stack_allocator_ok);
    serial_write_string(serial, ", pool=");
    serial_write_int(serial, (int32_t) pool_allocator_ok);
    serial_write_string(serial, ", pinned=");
    serial_write_int(serial, (int32_t) pinned_ok);
    serial_write_string(serial, ", arena_capacity=");
    serial_write_int(serial, (int32_t) kernel_frame_arena_get_capacity_bytes());
    serial_write_string(serial, ", arena_budget_exceeded=");
    serial_write_int(serial, (int32_t) kernel_frame_arena_get_budget_exceeded_count());
    serial_write_string(serial, ", stack_capacity=");
    serial_write_int(serial, (int32_t) kernel_stack_allocator_get_capacity());
    serial_write_string(serial, ", pool_object_size=");
    serial_write_int(serial, (int32_t) kernel_pool_get_object_size());
    serial_write_string(serial, ", pool_capacity=");
    serial_write_int(serial, (int32_t) kernel_pool_get_capacity());
    serial_write_string(serial, ", free=");
    serial_write_int(serial, (int32_t) kernel_pool_get_free_count());
    serial_write_string(serial, "\n");
}

void write_ring_buffer_info(Serial_t *serial, bool ring_buffer_ok)
{
    serial_write_string(serial, "[" KERNEL_SYSTEM_STRING "]: ring buffer init=");
    serial_write_int(serial, (int32_t) ring_buffer_ok);
    serial_write_string(serial, ", mode=");
    serial_write_int(serial, (int32_t) kernel_ring_buffer_get_mode());
    serial_write_string(serial, ", slot_size=");
    serial_write_int(serial, (int32_t) kernel_ring_buffer_get_slot_size());
    serial_write_string(serial, ", capacity=");
    serial_write_int(serial, (int32_t) kernel_ring_buffer_get_capacity());
    serial_write_string(serial, ", count=");
    serial_write_int(serial, (int32_t) kernel_ring_buffer_get_count());
    serial_write_string(serial, ", enq=");
    serial_write_int(serial, (int32_t) kernel_ring_buffer_get_enqueue_count());
    serial_write_string(serial, ", deq=");
    serial_write_int(serial, (int32_t) kernel_ring_buffer_get_dequeue_count());
    serial_write_string(serial, "\n");
}
