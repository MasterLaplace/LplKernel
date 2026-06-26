/**
 * @file hal_graphics_memory.c
 * @brief Graphics-memory backend for the engine HAL.
 *
 * Implements the hal_graphics_memory_* contract over the kernel pinned-memory
 * allocator: pinned pages are never relocated, giving the stable mappings a GPU
 * uploader needs (WDDM-GPUVA semantics). Pinned memory is "contiguous-ish" and
 * exposes no single physical base, so callers that hand pages to a GPU must walk
 * the scatter-gather list; hal_graphics_memory_physical_address resolves one
 * page's physical address via the paging map for that walk.
 */
#include <kernel/hal/hal.h>

#include <kernel/cpu/paging.h>
#include <kernel/memory/pinned_memory.h>

#include <stddef.h>

void *hal_graphics_memory_allocate(uint32_t size_bytes) { return kernel_pinned_alloc(size_bytes); }

void hal_graphics_memory_free(void *pointer, uint32_t size_bytes) { kernel_pinned_free(pointer, size_bytes); }

bool hal_graphics_memory_physical_address(const void *virtual_address, uint32_t *out_physical_address)
{
    if (virtual_address == NULL || out_physical_address == NULL)
        return false;
    return paging_get_physical_address((uint32_t) virtual_address, out_physical_address);
}
