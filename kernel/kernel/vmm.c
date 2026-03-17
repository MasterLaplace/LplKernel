/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** vmm — Virtual Memory Manager implementation
*/

#include <kernel/mm/vmm.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <string.h>

#define VMM_PAGE_COUNT (KERNEL_VMM_DYNAMIC_SIZE / PAGE_SIZE)
#define VMM_BITMAP_SIZE (VMM_PAGE_COUNT / 8)

static uint8_t vmm_bitmap[VMM_BITMAP_SIZE];
static uint32_t vmm_last_search_index = 0u;
static bool vmm_initialized = false;

static void vmm_bitmap_set(uint32_t index)
{
    vmm_bitmap[index / 8] |= (1 << (index % 8));
}

static void vmm_bitmap_clear(uint32_t index)
{
    vmm_bitmap[index / 8] &= ~(1 << (index % 8));
}

static bool vmm_bitmap_test(uint32_t index)
{
    return (vmm_bitmap[index / 8] & (1 << (index % 8))) != 0;
}

bool kernel_vmm_initialize(void)
{
    memset(vmm_bitmap, 0, sizeof(vmm_bitmap));
    vmm_last_search_index = 0u;
    vmm_initialized = true;
    return true;
}

void *kernel_vmm_reserve_pages(uint32_t page_count)
{
    if (!vmm_initialized || page_count == 0 || page_count > VMM_PAGE_COUNT)
        return NULL;

    uint32_t found_index = 0xFFFFFFFFu;
    uint32_t consecutive = 0u;

    /* Simple first-fit search from last index (with wrap-around) */
    for (uint32_t i = 0u; i < VMM_PAGE_COUNT; ++i)
    {
        uint32_t idx = (vmm_last_search_index + i) % VMM_PAGE_COUNT;
        
        if (!vmm_bitmap_test(idx))
        {
            if (++consecutive == page_count)
            {
                found_index = (idx - page_count + 1);
                break;
            }
        }
        else
        {
            consecutive = 0u;
        }
    }

    if (found_index == 0xFFFFFFFFu)
        return NULL;

    for (uint32_t i = 0u; i < page_count; ++i)
        vmm_bitmap_set(found_index + i);

    vmm_last_search_index = found_index + page_count;
    
    return (void *)(uintptr_t)(KERNEL_VMM_DYNAMIC_START + (found_index * PAGE_SIZE));
}

bool kernel_vmm_reserve_at(void *virt, uint32_t page_count)
{
    if (!vmm_initialized || !virt || page_count == 0)
        return false;

    uint32_t virt_addr = (uint32_t)(uintptr_t)virt;
    if (virt_addr < KERNEL_VMM_DYNAMIC_START || (virt_addr + page_count * PAGE_SIZE) > KERNEL_VMM_DYNAMIC_END)
        return false;

    uint32_t start_index = (virt_addr - KERNEL_VMM_DYNAMIC_START) / PAGE_SIZE;

    /* Check if already reserved */
    for (uint32_t i = 0u; i < page_count; ++i)
    {
        if (vmm_bitmap_test(start_index + i))
            return false;
    }

    /* Mark as reserved */
    for (uint32_t i = 0u; i < page_count; ++i)
        vmm_bitmap_set(start_index + i);

    return true;
}

void *kernel_vmm_alloc_pages(uint32_t page_count)
{
    void *virt_base = kernel_vmm_reserve_pages(page_count);
    if (!virt_base)
        return NULL;

    uint32_t start_virt = (uint32_t)(uintptr_t)virt_base;

    for (uint32_t i = 0u; i < page_count; ++i)
    {
        uint32_t phys = physical_memory_manager_page_frame_allocate();
        if (phys == 0u)
        {
            /* Error: cleanup and return NULL (simplified cleanup) */
            kernel_vmm_free_pages(virt_base, i);
            return NULL;
        }

        PageDirectoryEntry_t pde = {0};
        pde.present = 1;
        pde.read_write = 1;
        pde.user_supervisor = 0;

        PageTableEntry_t pte = {0};
        pte.present = 1;
        pte.read_write = 1;
        pte.user_supervisor = 0;

        if (!paging_map_page(start_virt + (i * PAGE_SIZE), phys, pde, pte))
        {
            physical_memory_manager_page_frame_free(phys);
            kernel_vmm_free_pages(virt_base, i);
            return NULL;
        }
    }

    return virt_base;
}

void kernel_vmm_free_pages(void *ptr, uint32_t page_count)
{
    if (!vmm_initialized || !ptr || page_count == 0)
        return;

    uint32_t virt_start = (uint32_t)(uintptr_t)ptr;
    if (virt_start < KERNEL_VMM_DYNAMIC_START || virt_start >= KERNEL_VMM_DYNAMIC_END)
        return;

    uint32_t start_index = (virt_start - KERNEL_VMM_DYNAMIC_START) / PAGE_SIZE;

    for (uint32_t i = 0u; i < page_count; ++i)
    {
        uint32_t virt = virt_start + (i * PAGE_SIZE);
        uint32_t phys = 0u;

        if (paging_get_physical_address(virt, &phys))
            physical_memory_manager_page_frame_free(phys);

        paging_unmap_page(virt);
        vmm_bitmap_clear(start_index + i);
    }
}
