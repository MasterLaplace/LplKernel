#define __LPL_KERNEL__

#include <kernel/mm/heap.h>
#include <kernel/mm/stack_allocator.h>

static uint8_t *kernel_stack_allocator_base = NULL;
static uint32_t kernel_stack_allocator_capacity = 0u;
static uint32_t kernel_stack_allocator_offset = 0u;
static uint32_t kernel_stack_allocator_peak = 0u;
static uint32_t kernel_stack_allocator_alloc_count = 0u;
static uint32_t kernel_stack_allocator_rollback_count = 0u;
static uint32_t kernel_stack_allocator_failed_alloc_count = 0u;
static bool kernel_stack_allocator_initialized = false;

static uint32_t kernel_stack_allocator_align_up(uint32_t value, uint32_t align)
{
    if (align == 0u)
        return value;
    return (value + (align - 1u)) & ~(align - 1u);
}

bool kernel_stack_allocator_initialize(uint32_t capacity_bytes)
{
    if (kernel_stack_allocator_initialized)
        return true;

    if (capacity_bytes == 0u)
        return false;

    uint32_t aligned_capacity = kernel_stack_allocator_align_up(capacity_bytes, 16u);
    void *backing = kmalloc((size_t) aligned_capacity);

    if (!backing)
        return false;

    kernel_stack_allocator_base = (uint8_t *) backing;
    kernel_stack_allocator_capacity = aligned_capacity;
    kernel_stack_allocator_offset = 0u;
    kernel_stack_allocator_peak = 0u;
    kernel_stack_allocator_alloc_count = 0u;
    kernel_stack_allocator_rollback_count = 0u;
    kernel_stack_allocator_failed_alloc_count = 0u;
    kernel_stack_allocator_initialized = true;
    return true;
}

void *kernel_stack_alloc_push(uint32_t size, uint32_t align)
{
    if (!kernel_stack_allocator_initialized || size == 0u)
        return NULL;

    uint32_t effective_align = (align == 0u) ? 8u : align;
    uint32_t aligned_offset = kernel_stack_allocator_align_up(kernel_stack_allocator_offset, effective_align);

    if (aligned_offset > kernel_stack_allocator_capacity || size > (kernel_stack_allocator_capacity - aligned_offset))
    {
        ++kernel_stack_allocator_failed_alloc_count;
        return NULL;
    }

    void *result = kernel_stack_allocator_base + aligned_offset;
    kernel_stack_allocator_offset = aligned_offset + size;
    ++kernel_stack_allocator_alloc_count;

    if (kernel_stack_allocator_offset > kernel_stack_allocator_peak)
        kernel_stack_allocator_peak = kernel_stack_allocator_offset;

    return result;
}

uint32_t kernel_stack_alloc_get_marker(void) { return kernel_stack_allocator_offset; }

void kernel_stack_alloc_rollback(uint32_t marker)
{
    if (!kernel_stack_allocator_initialized)
        return;

    if (marker <= kernel_stack_allocator_offset)
    {
        kernel_stack_allocator_offset = marker;
        ++kernel_stack_allocator_rollback_count;
    }
}

bool kernel_stack_allocator_is_initialized(void) { return kernel_stack_allocator_initialized; }

uint32_t kernel_stack_allocator_get_capacity(void) { return kernel_stack_allocator_capacity; }

uint32_t kernel_stack_allocator_get_used(void) { return kernel_stack_allocator_offset; }

uint32_t kernel_stack_allocator_get_peak_used(void) { return kernel_stack_allocator_peak; }

uint32_t kernel_stack_allocator_get_alloc_count(void) { return kernel_stack_allocator_alloc_count; }

uint32_t kernel_stack_allocator_get_rollback_count(void) { return kernel_stack_allocator_rollback_count; }

uint32_t kernel_stack_allocator_get_failed_alloc_count(void) { return kernel_stack_allocator_failed_alloc_count; }
