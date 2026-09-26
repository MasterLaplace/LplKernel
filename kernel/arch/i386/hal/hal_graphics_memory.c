#include <kernel/hal/hal.h>

#include <kernel/cpu/paging.h>
#include <kernel/memory/pinned_memory.h>

#include <stddef.h>

void *hardware_abstraction_layer_graphics_memory_allocate(uint32_t size_bytes)
{
    return kernel_pinned_alloc(size_bytes);
}

void hardware_abstraction_layer_graphics_memory_free(void *pointer, uint32_t size_bytes)
{
    kernel_pinned_free(pointer, size_bytes);
}

bool hardware_abstraction_layer_graphics_memory_physical_address(const void *virtual_address,
                                                                 uint32_t *out_physical_address)
{
    if (virtual_address == NULL || out_physical_address == NULL)
        return false;
    return paging_get_physical_address((uint32_t) virtual_address, out_physical_address);
}
