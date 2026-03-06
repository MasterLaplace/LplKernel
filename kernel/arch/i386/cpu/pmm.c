#define __LPL_KERNEL__

/**
 * @file pmm.c
 * @brief Physical Memory Manager — algorithm-agnostic implementation.
 *
 * @details The public API
 *          (physical_memory_manager_initialize,
 *           physical_memory_manager_page_frame_allocate,
 *           physical_memory_manager_page_frame_free, ...)
 *          is identical for both kernel modes.  The compile-time flag
 *          LPL_KERNEL_REAL_TIME_MODE selects the backing data structure:
 *            - Realtime / client:  intrusive Free-List LIFO stack, O(1).
 *            - Server:             Buddy Allocator (stub, to be implemented).
 */

#include <kernel/boot/multiboot_info.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pmm.h>
#include <stdbool.h>
#include <stddef.h>

////////////////////////////////////////////////////////////
// Constants
////////////////////////////////////////////////////////////

/** @brief Boot-time mapping ceiling (4 page tables x 4 MB). */
#define PMM_BOOT_MAP_LIMIT 0x01000000UL

/** @brief Low memory reservation (BIOS, VGA, Multiboot structures). */
#define PMM_LOW_MEMORY_LIMIT 0x00100000UL

/** @brief Physical address ceiling (phys + 0xC0000000 must not overflow). */
#define PMM_MAX_PHYS_ADDR 0x40000000UL

/** @brief Multiboot flag bit indicating presence of a memory map. */
#define MULTIBOOT_FLAG_MMAP (1 << 6)

/** @brief Multiboot memory region type: available RAM. */
#define MMAP_TYPE_AVAILABLE 1

/** @brief Minimum e820 entry payload size (bytes). */
#define MMAP_MIN_ENTRY_SIZE 20

////////////////////////////////////////////////////////////
// Address Translation
////////////////////////////////////////////////////////////

/**
 * @brief Translate a physical address to its higher-half virtual alias.
 */
static inline uint32_t pmm_phys_to_virt(uint32_t phys_addr) { return phys_addr + KERNEL_VIRTUAL_BASE; }

/**
 * @brief Translate a higher-half virtual address to its physical counterpart.
 */
static inline uint32_t pmm_virt_to_phys(uint32_t virt_addr) { return virt_addr - KERNEL_VIRTUAL_BASE; }

////////////////////////////////////////////////////////////
// Shared Accounting
////////////////////////////////////////////////////////////

/** @brief Number of free pages currently in the pool. */
static uint32_t free_page_count = 0;

////////////////////////////////////////////////////////////
// Algorithm-Specific Helpers
////////////////////////////////////////////////////////////

#ifdef LPL_KERNEL_REAL_TIME_MODE

/** @brief Head of the intrusive LIFO stack (physical address, 0 = empty). */
static uint32_t free_list_head = 0;

/**
 * @brief Push a page onto the Free-List LIFO stack.
 * @details Stores the current head pointer in the page's first 4 bytes
 *          (via the higher-half virtual alias), then makes this page the
 *          new head.
 * @param phys_addr 4 KB-aligned physical address to push.
 */
static void freelist_push(uint32_t phys_addr)
{
    uint32_t *page_virt = (uint32_t *) pmm_phys_to_virt(phys_addr);
    *page_virt = free_list_head;
    free_list_head = phys_addr;
    ++free_page_count;
}

/**
 * @brief Pop a page from the Free-List LIFO stack.
 * @details Reads the next-pointer from the head page and advances the head.
 * @return Physical address of the popped page, or 0 if empty.
 */
static uint32_t freelist_pop(void)
{
    if (free_list_head == 0)
        return 0;
    uint32_t phys_addr = free_list_head;
    uint32_t *page_virt = (uint32_t *) pmm_phys_to_virt(phys_addr);
    free_list_head = *page_virt;
    --free_page_count;
    return phys_addr;
}

#else /* Server mode — Buddy Allocator */

/**
 * @brief Insert a page into the Buddy Allocator.
 * @note Stub — will be implemented in a future phase.
 */
static void buddy_insert(uint32_t phys_addr) { (void) phys_addr; }

/**
 * @brief Remove a page from the Buddy Allocator.
 * @note Stub — will be implemented in a future phase.
 * @return Always 0 (not yet implemented).
 */
static uint32_t buddy_remove(void) { return 0; }

#endif /* LPL_KERNEL_REAL_TIME_MODE */

////////////////////////////////////////////////////////////
// Multiboot Memory Map Helpers
////////////////////////////////////////////////////////////

/**
 * @brief Validate that Multiboot provides a usable memory map.
 * @param[out] out_base   Virtual pointer to the mmap buffer.
 * @param[out] out_length Length of the mmap buffer in bytes.
 * @return true if the memory map is valid and ready for iteration.
 */
static bool pmm_get_multiboot_mmap(uint8_t **out_base, uint32_t *out_length)
{
    extern MultibootInfo_t *multiboot_info;

    if (!multiboot_info)
        return false;
    if (!(multiboot_info->flags & MULTIBOOT_FLAG_MMAP))
        return false;
    if (!multiboot_info->mmap_length || !multiboot_info->mmap_addr)
        return false;

    *out_base = (uint8_t *) pmm_phys_to_virt((uint32_t) (uintptr_t) multiboot_info->mmap_addr);
    *out_length = multiboot_info->mmap_length;
    return true;
}

/**
 * @brief Compute page-aligned boundaries of an available memory region.
 * @param entry      Multiboot memory map entry.
 * @param[out] out_start First usable page-aligned address.
 * @param[out] out_end   End address (exclusive, page-aligned down).
 * @return true if the region contains at least one usable page.
 */
static bool pmm_compute_region_bounds(const MemoryMapEntry_t *entry, uint32_t *out_start, uint32_t *out_end)
{
    if ((entry->base_addr >> 32) != 0)
        return false;

    uint32_t region_start = (uint32_t) entry->base_addr;
    uint32_t region_length = (entry->length >> 32) ? 0xFFFFFFFFUL - region_start : (uint32_t) entry->length;

    uint32_t region_end = (region_start + region_length < region_start) ? 0xFFFFFFFFUL : region_start + region_length;

    *out_start = PAGE_ALIGN_UP(region_start);
    *out_end = PAGE_ALIGN_DOWN(region_end);
    return *out_start < *out_end;
}

/**
 * @brief Test whether a physical address falls inside the kernel image.
 */
static bool pmm_is_kernel_page(uint32_t phys_addr, uint32_t kernel_start, uint32_t kernel_end)
{
    return phys_addr >= kernel_start && phys_addr < kernel_end;
}

/**
 * @brief Advance to the next Multiboot mmap entry.
 * @param mmap_base   Virtual base of the mmap buffer.
 * @param mmap_length Total length of the mmap buffer.
 * @param[in,out] offset Current byte offset; updated on success.
 * @param[out] out_entry Pointer to the current entry.
 * @return true if a valid entry was read, false to stop iteration.
 */
static bool pmm_next_mmap_entry(const uint8_t *mmap_base, uint32_t mmap_length, uint32_t *offset,
                                MemoryMapEntry_t **out_entry)
{
    if (*offset + sizeof(uint32_t) > mmap_length)
        return false;

    MemoryMapEntry_t *entry = (MemoryMapEntry_t *) (mmap_base + *offset);

    if (entry->size < MMAP_MIN_ENTRY_SIZE)
        return false;
    if (*offset + sizeof(uint32_t) + entry->size > mmap_length)
        return false;

    *out_entry = entry;
    return true;
}

////////////////////////////////////////////////////////////
// Public API functions of the PMM module
////////////////////////////////////////////////////////////

void physical_memory_manager_page_frame_free(uint32_t phys_addr)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    freelist_push(phys_addr);
#else
    buddy_insert(phys_addr);
#endif
}

uint32_t physical_memory_manager_page_frame_allocate(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return freelist_pop();
#else
    return buddy_remove();
#endif
}

uint32_t physical_memory_manager_get_free_page_count(void) { return free_page_count; }

void physical_memory_manager_initialize(void)
{
    uint8_t *mmap_base = NULL;
    uint32_t mmap_length = 0;

    if (!pmm_get_multiboot_mmap(&mmap_base, &mmap_length))
        return;

    uint32_t kernel_phys_start = PMM_LOW_MEMORY_LIMIT;
    uint32_t kernel_phys_end = pmm_virt_to_phys((uint32_t) (uintptr_t) &global_kernel_end);

    uint32_t offset = 0;
    MemoryMapEntry_t *entry = NULL;

    while (pmm_next_mmap_entry(mmap_base, mmap_length, &offset, &entry))
    {
        if (entry->type == MMAP_TYPE_AVAILABLE)
        {
            uint32_t region_start, region_end;

            if (pmm_compute_region_bounds(entry, &region_start, &region_end))
            {
                for (uint32_t addr = region_start; addr < region_end; addr += PAGE_SIZE)
                {
                    if (addr < PMM_LOW_MEMORY_LIMIT)
                        continue;
                    if (pmm_is_kernel_page(addr, kernel_phys_start, kernel_phys_end))
                        continue;
                    if (addr >= PMM_BOOT_MAP_LIMIT)
                        continue;
                    if (addr >= PMM_MAX_PHYS_ADDR)
                        continue;
                    physical_memory_manager_page_frame_free(addr);
                }
            }
        }
        offset += sizeof(uint32_t) + entry->size;
    }
}

void physical_memory_manager_extend_mapping(void)
{
    uint8_t *mmap_base = NULL;
    uint32_t mmap_length = 0;

    if (!pmm_get_multiboot_mmap(&mmap_base, &mmap_length))
        return;

    PageDirectoryEntry_t pde_flags = {0};
    pde_flags.present = 1;
    pde_flags.read_write = 1;

    PageTableEntry_t pte_flags = {0};
    pte_flags.present = 1;
    pte_flags.read_write = 1;

    uint32_t offset = 0;
    MemoryMapEntry_t *entry = NULL;

    while (pmm_next_mmap_entry(mmap_base, mmap_length, &offset, &entry))
    {
        if (entry->type == MMAP_TYPE_AVAILABLE)
        {
            uint32_t region_start, region_end;

            if (pmm_compute_region_bounds(entry, &region_start, &region_end))
            {
                for (uint32_t addr = region_start; addr < region_end; addr += PAGE_SIZE)
                {
                    if (addr < PMM_BOOT_MAP_LIMIT)
                        continue;
                    if (addr >= PMM_MAX_PHYS_ADDR)
                        continue;

                    uint32_t virt_addr = pmm_phys_to_virt(addr);
                    if (!paging_map_page(virt_addr, addr, pde_flags, pte_flags))
                        continue;
                    physical_memory_manager_page_frame_free(addr);
                }
            }
        }
        offset += sizeof(uint32_t) + entry->size;
    }
}
