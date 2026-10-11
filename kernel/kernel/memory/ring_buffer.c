#define __LPL_KERNEL__

#include <kernel/memory/heap.h>
#include <kernel/memory/ring_buffer.h>
#include <kernel/memory/spsc_ring.h>
#include <string.h>

static KernelSpscRing_t kernel_ring_buffer_indices;
static uint8_t *kernel_ring_buffer_slots = NULL;
static uint32_t *kernel_ring_buffer_sizes = NULL;
static uint32_t kernel_ring_buffer_slot_size = 0u;
static bool kernel_ring_buffer_initialized = false;

static uint32_t kernel_ring_buffer_enqueue_count = 0u;        /**< Producer only. */
static uint32_t kernel_ring_buffer_high_watermark = 0u;       /**< Producer only. */
static uint32_t kernel_ring_buffer_dequeue_count = 0u;        /**< Consumer only. */
static uint32_t kernel_ring_buffer_failed_dequeue_count = 0u; /**< Consumer only. */

static uint32_t kernel_ring_buffer_align_up(uint32_t value, uint32_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

bool kernel_ring_buffer_initialize(uint32_t slot_size, uint32_t slot_count)
{
    if (kernel_ring_buffer_initialized)
        return true;

    if (slot_size == 0u || !kernel_spsc_ring_initialize(&kernel_ring_buffer_indices, slot_count))
        return false;

    kernel_ring_buffer_slot_size = kernel_ring_buffer_align_up(slot_size, 8u);
    kernel_ring_buffer_slots = (uint8_t *) kmalloc((size_t) kernel_ring_buffer_slot_size * slot_count);
    kernel_ring_buffer_sizes = (uint32_t *) kmalloc(sizeof(uint32_t) * slot_count);
    if (!kernel_ring_buffer_slots || !kernel_ring_buffer_sizes)
    {
        kfree(kernel_ring_buffer_slots);
        kfree(kernel_ring_buffer_sizes);
        kernel_ring_buffer_slots = NULL;
        kernel_ring_buffer_sizes = NULL;
        return false;
    }

    kernel_ring_buffer_initialized = true;
    return true;
}

bool kernel_ring_buffer_enqueue(const void *data, uint32_t size)
{
    uint32_t slot = 0u;

    if (!kernel_ring_buffer_initialized)
        return false;
    if (!data || size == 0u || size > kernel_ring_buffer_slot_size ||
        kernel_spsc_ring_reserve(&kernel_ring_buffer_indices, 1u, &slot) == 0u)
    {
        kernel_spsc_ring_count_rejected(&kernel_ring_buffer_indices, 1u);
        return false;
    }

    memcpy(kernel_ring_buffer_slots + (size_t) slot * kernel_ring_buffer_slot_size, data, size);
    kernel_ring_buffer_sizes[slot] = size;
    kernel_spsc_ring_publish(&kernel_ring_buffer_indices, 1u);
    ++kernel_ring_buffer_enqueue_count;

    const uint32_t count = kernel_spsc_ring_size(&kernel_ring_buffer_indices);
    if (count > kernel_ring_buffer_high_watermark)
        kernel_ring_buffer_high_watermark = count;
    return true;
}

bool kernel_ring_buffer_dequeue(void *out_data, uint32_t out_size)
{
    uint32_t slot = 0u;

    if (!kernel_ring_buffer_initialized)
        return false;
    if (!out_data || out_size == 0u || out_size > kernel_ring_buffer_slot_size ||
        kernel_spsc_ring_peek(&kernel_ring_buffer_indices, 1u, &slot) == 0u)
    {
        ++kernel_ring_buffer_failed_dequeue_count;
        return false;
    }

    const uint32_t size = kernel_ring_buffer_sizes[slot] < out_size ? kernel_ring_buffer_sizes[slot] : out_size;

    memcpy(out_data, kernel_ring_buffer_slots + (size_t) slot * kernel_ring_buffer_slot_size, size);
    memset((uint8_t *) out_data + size, 0, out_size - size);
    kernel_spsc_ring_release(&kernel_ring_buffer_indices, 1u);
    ++kernel_ring_buffer_dequeue_count;
    return true;
}

bool kernel_ring_buffer_is_initialized(void) { return kernel_ring_buffer_initialized; }

uint32_t kernel_ring_buffer_get_slot_size(void) { return kernel_ring_buffer_slot_size; }

uint32_t kernel_ring_buffer_get_capacity(void)
{
    return kernel_ring_buffer_initialized ? kernel_spsc_ring_capacity(&kernel_ring_buffer_indices) : 0u;
}

uint32_t kernel_ring_buffer_get_count(void) { return kernel_spsc_ring_size(&kernel_ring_buffer_indices); }

uint32_t kernel_ring_buffer_get_high_watermark(void) { return kernel_ring_buffer_high_watermark; }

uint32_t kernel_ring_buffer_get_enqueue_count(void) { return kernel_ring_buffer_enqueue_count; }

uint32_t kernel_ring_buffer_get_dequeue_count(void) { return kernel_ring_buffer_dequeue_count; }

uint32_t kernel_ring_buffer_get_failed_enqueue_count(void)
{
    return kernel_spsc_ring_rejected_count(&kernel_ring_buffer_indices);
}

uint32_t kernel_ring_buffer_get_failed_dequeue_count(void) { return kernel_ring_buffer_failed_dequeue_count; }
