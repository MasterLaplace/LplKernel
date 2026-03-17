#define __LPL_KERNEL__

#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <kernel/mm/pinned_memory.h>
#include <kernel/mm/vmm.h>
#include <stddef.h>

static uint32_t pinned_virtual_cursor = 0xE0000000u;
static uint32_t pinned_allocated_pages = 0u;
static uint32_t pinned_released_pages = 0u;
static bool pinned_initialized = false;

bool kernel_pinned_memory_initialize(void)
{
    if (pinned_initialized)
        return true;
    pinned_virtual_cursor = 0xE0000000u;
    pinned_allocated_pages = 0u;
    pinned_released_pages = 0u;
    pinned_initialized = true;
    return true;
}

void *kernel_pinned_alloc(uint32_t size)
{
    if (!pinned_initialized || size == 0u)
        return NULL;

    uint32_t pages_needed = (size + PAGE_SIZE - 1u) / PAGE_SIZE;

    uint32_t start_virt = pinned_virtual_cursor;

    if (!kernel_vmm_reserve_at((void *) start_virt, pages_needed))
        return NULL;

    for (uint32_t i = 0u; i < pages_needed; ++i)
    {
        uint32_t phys = physical_memory_manager_page_frame_allocate();
        if (phys == 0u)
            return NULL; /* Memory exhaustion, leak handled here for simplicity */

        PageDirectoryEntry_t pde = {0};
        pde.present = 1;
        pde.read_write = 1;
        pde.user_supervisor = 0;

        PageTableEntry_t pte = {0};
        pte.present = 1;
        pte.read_write = 1;
        pte.user_supervisor = 0;
        pte.available = 1; /* Mark as non-evictable (pinned) */

        if (!paging_map_page(start_virt + (i * PAGE_SIZE), phys, pde, pte))
            return NULL;

        ++pinned_allocated_pages;
    }

    pinned_virtual_cursor += (pages_needed * PAGE_SIZE);

    return (void *) start_virt;
}

void kernel_pinned_free(void *ptr, uint32_t size)
{
    if (!pinned_initialized || !ptr || size == 0u)
        return;

    uint32_t pages_to_free = (size + PAGE_SIZE - 1u) / PAGE_SIZE;
    uint32_t start_virt = (uint32_t) ptr;

    for (uint32_t i = 0u; i < pages_to_free; ++i)
    {
        uint32_t virt = start_virt + (i * PAGE_SIZE);
        uint32_t phys = 0u;

        if (paging_get_physical_address(virt, &phys))
        {
            physical_memory_manager_page_frame_free(phys);
        }

        paging_unmap_page(virt);
        ++pinned_released_pages;
    }
    kernel_vmm_free_pages(ptr, pages_to_free);
}

uint32_t kernel_pinned_get_allocated_pages(void) { return pinned_allocated_pages; }

uint32_t kernel_pinned_get_released_pages(void) { return pinned_released_pages; }
