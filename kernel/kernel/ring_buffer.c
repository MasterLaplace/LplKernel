#define __LPL_KERNEL__

#include <kernel/mm/heap.h>
#include <kernel/mm/ring_buffer.h>
#include <string.h>

static uint8_t *kernel_ring_buffer_base = NULL;
static uint32_t kernel_ring_buffer_slot_size = 0u;
static uint32_t kernel_ring_buffer_capacity = 0u;
static uint32_t kernel_ring_buffer_head = 0u;
static uint32_t kernel_ring_buffer_tail = 0u;
static uint32_t kernel_ring_buffer_count = 0u;
static KernelRingBufferMode_t kernel_ring_buffer_mode = KERNEL_RING_BUFFER_MODE_LOCAL;
static uint32_t kernel_ring_buffer_high_watermark = 0u;
static uint32_t kernel_ring_buffer_enqueue_count = 0u;
static uint32_t kernel_ring_buffer_dequeue_count = 0u;
static uint32_t kernel_ring_buffer_failed_enqueue_count = 0u;
static uint32_t kernel_ring_buffer_failed_dequeue_count = 0u;
static bool kernel_ring_buffer_initialized = false;

static uint32_t kernel_ring_buffer_align_up(uint32_t value, uint32_t align)
{
    if (align == 0u)
        return value;
    return (value + (align - 1u)) & ~(align - 1u);
}

bool kernel_ring_buffer_initialize(uint32_t slot_size, uint32_t slot_count)
{
    return kernel_ring_buffer_initialize_ex(slot_size, slot_count, KERNEL_RING_BUFFER_MODE_LOCAL);
}

bool kernel_ring_buffer_initialize_ex(uint32_t slot_size, uint32_t slot_count, KernelRingBufferMode_t mode)
{
    if (kernel_ring_buffer_initialized)
        return true;

    if (slot_size == 0u || slot_count == 0u)
        return false;

    kernel_ring_buffer_slot_size = kernel_ring_buffer_align_up(slot_size, 8u);
    kernel_ring_buffer_capacity = slot_count;

    void *backing = kmalloc((size_t) (kernel_ring_buffer_slot_size * slot_count));
    if (!backing)
        return false;

    kernel_ring_buffer_base = (uint8_t *) backing;
    kernel_ring_buffer_head = 0u;
    kernel_ring_buffer_tail = 0u;
    kernel_ring_buffer_count = 0u;
    kernel_ring_buffer_mode = mode;
    kernel_ring_buffer_high_watermark = 0u;
    kernel_ring_buffer_enqueue_count = 0u;
    kernel_ring_buffer_dequeue_count = 0u;
    kernel_ring_buffer_failed_enqueue_count = 0u;
    kernel_ring_buffer_failed_dequeue_count = 0u;
    kernel_ring_buffer_initialized = true;
    return true;
}

bool kernel_ring_buffer_enqueue(const void *data, uint32_t size)
{
    if (!kernel_ring_buffer_initialized || !data || size == 0u || size > kernel_ring_buffer_slot_size)
    {
        if (kernel_ring_buffer_initialized)
            ++kernel_ring_buffer_failed_enqueue_count;
        return false;
    }

    if (kernel_ring_buffer_count >= kernel_ring_buffer_capacity)
    {
        ++kernel_ring_buffer_failed_enqueue_count;
        return false;
    }

    uint8_t *slot = kernel_ring_buffer_base + (kernel_ring_buffer_tail * kernel_ring_buffer_slot_size);
    memset(slot, 0, kernel_ring_buffer_slot_size);
    memcpy(slot, data, size);

    uint32_t current_tail = __atomic_load_n(&kernel_ring_buffer_tail, __ATOMIC_ACQUIRE);
    uint32_t next_tail = (current_tail + 1u) % kernel_ring_buffer_capacity;
    __atomic_store_n(&kernel_ring_buffer_tail, next_tail, __ATOMIC_RELEASE);
    __atomic_add_fetch(&kernel_ring_buffer_count, 1u, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&kernel_ring_buffer_enqueue_count, 1u, __ATOMIC_ACQ_REL);
    if (kernel_ring_buffer_count > kernel_ring_buffer_high_watermark)
        kernel_ring_buffer_high_watermark = kernel_ring_buffer_count;

    return true;
}

bool kernel_ring_buffer_dequeue(void *out_data, uint32_t out_size)
{
    if (!kernel_ring_buffer_initialized || !out_data || out_size == 0u || out_size > kernel_ring_buffer_slot_size)
    {
        if (kernel_ring_buffer_initialized)
            ++kernel_ring_buffer_failed_dequeue_count;
        return false;
    }

    if (kernel_ring_buffer_count == 0u)
    {
        ++kernel_ring_buffer_failed_dequeue_count;
        return false;
    }

    uint8_t *slot = kernel_ring_buffer_base + (kernel_ring_buffer_head * kernel_ring_buffer_slot_size);
    memcpy(out_data, slot, out_size);

    uint32_t current_head = __atomic_load_n(&kernel_ring_buffer_head, __ATOMIC_ACQUIRE);
    uint32_t next_head = (current_head + 1u) % kernel_ring_buffer_capacity;
    __atomic_store_n(&kernel_ring_buffer_head, next_head, __ATOMIC_RELEASE);
    __atomic_sub_fetch(&kernel_ring_buffer_count, 1u, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&kernel_ring_buffer_dequeue_count, 1u, __ATOMIC_ACQ_REL);
    return true;
}

bool kernel_ring_buffer_is_initialized(void) { return kernel_ring_buffer_initialized; }

uint32_t kernel_ring_buffer_get_slot_size(void) { return kernel_ring_buffer_slot_size; }

uint32_t kernel_ring_buffer_get_capacity(void) { return kernel_ring_buffer_capacity; }

uint32_t kernel_ring_buffer_get_count(void) { return kernel_ring_buffer_count; }

KernelRingBufferMode_t kernel_ring_buffer_get_mode(void) { return kernel_ring_buffer_mode; }

uint32_t kernel_ring_buffer_get_high_watermark(void) { return kernel_ring_buffer_high_watermark; }

uint32_t kernel_ring_buffer_get_enqueue_count(void) { return kernel_ring_buffer_enqueue_count; }

uint32_t kernel_ring_buffer_get_dequeue_count(void) { return kernel_ring_buffer_dequeue_count; }

uint32_t kernel_ring_buffer_get_failed_enqueue_count(void) { return kernel_ring_buffer_failed_enqueue_count; }

uint32_t kernel_ring_buffer_get_failed_dequeue_count(void) { return kernel_ring_buffer_failed_dequeue_count; }
