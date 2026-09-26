#include <kernel/hal/hal.h>

#include <kernel/memory/heap.h>

#include <stddef.h>
#include <stdint.h>

void *hardware_abstraction_layer_memory_reserve(uint32_t size_bytes, uint32_t alignment)
{
    if (size_bytes == 0u)
        return NULL;
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);

    const uint32_t total = size_bytes + alignment + (uint32_t) sizeof(void *);
    uint8_t *const raw = (uint8_t *) kmalloc((size_t) total);
    if (raw == NULL)
        return NULL;

    uint8_t *const candidate = raw + sizeof(void *);
    const uintptr_t misalignment = (uintptr_t) candidate % (uintptr_t) alignment;
    uint8_t *const aligned = (misalignment == 0u) ? candidate : candidate + (alignment - misalignment);

    ((void **) aligned)[-1] = raw;
    return aligned;
}

void hardware_abstraction_layer_memory_release(void *block, uint32_t size_bytes)
{
    (void) size_bytes;
    if (block == NULL)
        return;
    kfree(((void **) block)[-1]);
}

void hardware_abstraction_layer_memory_begin_real_time_section(void) { kernel_heap_hot_loop_enter(); }

void hardware_abstraction_layer_memory_end_real_time_section(void) { kernel_heap_hot_loop_leave(); }

uint32_t hardware_abstraction_layer_memory_real_time_violation_count(void)
{
    return kernel_heap_get_hot_loop_violation_count();
}

uint32_t hardware_abstraction_layer_memory_real_time_bounded_count(void)
{
    return kernel_heap_get_hot_loop_bounded_count();
}
