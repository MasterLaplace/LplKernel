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
 *            - Server:             Buddy Allocator with split/merge support.
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

/** @brief Number of 4 KB pages managed below PMM_MAX_PHYS_ADDR (1 GB). */
#define PMM_MAX_PAGE_COUNT (PMM_MAX_PHYS_ADDR / PAGE_SIZE)

/** @brief Maximum buddy order for 4 KB pages up to 1 GB (2^18 pages). */
#define PMM_BUDDY_MAX_ORDER 18u

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

/** @brief High-water mark for free_page_count (peak since init). */
static uint32_t free_page_count_watermark_high = 0;

/** @brief Low-water mark for free_page_count (trough since init). */
static uint32_t free_page_count_watermark_low = 0xFFFFFFFFu;

/** @brief Track whether watermarks have been seeded after first population. */
static bool watermarks_seeded = false;

#ifdef LPL_KERNEL_REAL_TIME_MODE
static const char pmm_strategy_name[] = "freelist-lifo-realtime";
#else
static const char pmm_strategy_name[] = "buddy-allocator-server";
#endif

/**
 * @brief Update watermarks after any change to free_page_count.
 */
static inline void pmm_update_watermarks(void)
{
    if (!watermarks_seeded)
    {
        free_page_count_watermark_high = free_page_count;
        free_page_count_watermark_low = free_page_count;
        watermarks_seeded = true;
        return;
    }
    if (free_page_count > free_page_count_watermark_high)
        free_page_count_watermark_high = free_page_count;
    if (free_page_count < free_page_count_watermark_low)
        free_page_count_watermark_low = free_page_count;
}

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
    pmm_update_watermarks();
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
    pmm_update_watermarks();
    return phys_addr;
}

#else /* Server mode — Buddy Allocator */

/** @brief Free-list heads for each buddy order (order 0 = 4 KB). */
static uint32_t buddy_free_list_heads[PMM_BUDDY_MAX_ORDER + 1u] = {0};

/** @brief Free-state marker for page indices (1 = free block base, 0 = otherwise). */
static uint8_t buddy_page_is_free[PMM_MAX_PAGE_COUNT] = {0};

/** @brief Order marker for page indices (valid only when buddy_page_is_free[idx] == 1). */
static uint8_t buddy_page_order[PMM_MAX_PAGE_COUNT] = {0};

/** @brief Allocation ownership map (1 = currently allocated to caller). */
static uint8_t buddy_page_allocated[PMM_MAX_PAGE_COUNT] = {0};

/** @brief PMM population phase flag (initial fill/extend mapping). */
static bool buddy_population_in_progress = false;

/** @brief Debug counters for invalid and double-free detection. */
static uint32_t buddy_rejected_free_count = 0u;
static uint32_t buddy_double_free_count = 0u;

static inline uint32_t buddy_phys_to_page_index(uint32_t phys_addr) { return phys_addr >> 12u; }

static inline uint32_t buddy_order_block_size_bytes(uint8_t order) { return PAGE_SIZE << order; }

static inline bool buddy_is_valid_phys(uint32_t phys_addr)
{
    return IS_PAGE_ALIGNED(phys_addr) && phys_addr < PMM_MAX_PHYS_ADDR;
}

static bool buddy_is_valid_order(uint8_t order) { return order <= PMM_BUDDY_MAX_ORDER; }

static bool buddy_is_order_aligned(uint32_t phys_addr, uint8_t order)
{
    return (phys_addr & (buddy_order_block_size_bytes(order) - 1u)) == 0u;
}

static void buddy_list_push(uint8_t order, uint32_t phys_addr)
{
    uint32_t *page_virt = (uint32_t *) pmm_phys_to_virt(phys_addr);
    uint32_t page_index = buddy_phys_to_page_index(phys_addr);

    *page_virt = buddy_free_list_heads[order];
    buddy_free_list_heads[order] = phys_addr;
    buddy_page_is_free[page_index] = 1u;
    buddy_page_order[page_index] = order;

    uint32_t start_page_index = buddy_phys_to_page_index(phys_addr);
    uint32_t page_count = 1u << order;

    for (uint32_t page_offset = 0u; page_offset < page_count; ++page_offset)
        buddy_page_allocated[start_page_index + page_offset] = 0u;
}

static uint32_t buddy_list_pop(uint8_t order)
{
    uint32_t phys_addr = buddy_free_list_heads[order];

    if (!phys_addr)
        return 0;

    uint32_t *page_virt = (uint32_t *) pmm_phys_to_virt(phys_addr);
    uint32_t page_index = buddy_phys_to_page_index(phys_addr);

    buddy_free_list_heads[order] = *page_virt;
    buddy_page_is_free[page_index] = 0u;

    uint32_t start_page_index = buddy_phys_to_page_index(phys_addr);
    uint32_t page_count = 1u << order;

    for (uint32_t page_offset = 0u; page_offset < page_count; ++page_offset)
        buddy_page_allocated[start_page_index + page_offset] = 1u;

    return phys_addr;
}

static bool buddy_list_remove_exact(uint8_t order, uint32_t phys_addr)
{
    uint32_t current_phys = buddy_free_list_heads[order];
    uint32_t previous_phys = 0;

    while (current_phys)
    {
        uint32_t *current_virt = (uint32_t *) pmm_phys_to_virt(current_phys);
        uint32_t next_phys = *current_virt;

        if (current_phys == phys_addr)
        {
            uint32_t page_index = buddy_phys_to_page_index(current_phys);

            if (previous_phys)
            {
                uint32_t *previous_virt = (uint32_t *) pmm_phys_to_virt(previous_phys);
                *previous_virt = next_phys;
            }
            else
                buddy_free_list_heads[order] = next_phys;

            buddy_page_is_free[page_index] = 0u;
            return true;
        }

        previous_phys = current_phys;
        current_phys = next_phys;
    }

    return false;
}

static bool buddy_can_merge(uint8_t order, uint32_t phys_addr)
{
    if (!buddy_is_valid_phys(phys_addr))
        return false;

    uint32_t page_index = buddy_phys_to_page_index(phys_addr);

    if (!buddy_page_is_free[page_index])
        return false;

    return buddy_page_order[page_index] == order;
}

static bool buddy_range_is_allocated(uint32_t base_phys, uint8_t order)
{
    uint32_t start_page_index = buddy_phys_to_page_index(base_phys);
    uint32_t page_count = 1u << order;

    for (uint32_t page_offset = 0u; page_offset < page_count; ++page_offset)
    {
        if (!buddy_page_allocated[start_page_index + page_offset])
            return false;
    }

    return true;
}

static bool buddy_debug_is_free_block(uint32_t phys_addr, uint8_t order)
{
    if (order > PMM_BUDDY_MAX_ORDER)
        return false;
    if (!buddy_is_valid_phys(phys_addr))
        return false;
    if ((phys_addr & (buddy_order_block_size_bytes(order) - 1u)) != 0u)
        return false;

    uint32_t page_index = buddy_phys_to_page_index(phys_addr);

    if (!buddy_page_is_free[page_index])
        return false;

    return buddy_page_order[page_index] == order;
}

static uint32_t buddy_debug_get_free_block_count(uint8_t order)
{
    if (order > PMM_BUDDY_MAX_ORDER)
        return 0u;

    uint32_t count = 0u;
    uint32_t current_phys = buddy_free_list_heads[order];

    while (current_phys)
    {
        uint32_t *current_virt = (uint32_t *) pmm_phys_to_virt(current_phys);
        current_phys = *current_virt;
        ++count;
    }

    return count;
}

/**
 * @brief Insert a page into the Buddy Allocator.
 * @details Free path with upward coalescing.
 *          Keeps server build functional for runtime page-table growth.
 */
static void buddy_insert_order(uint32_t phys_addr, uint8_t requested_order)
{
    if (!buddy_is_valid_order(requested_order))
    {
        ++buddy_rejected_free_count;
        return;
    }
    if (!buddy_is_valid_phys(phys_addr))
    {
        ++buddy_rejected_free_count;
        return;
    }
    if (!buddy_is_order_aligned(phys_addr, requested_order))
    {
        ++buddy_rejected_free_count;
        return;
    }

    if (!buddy_population_in_progress)
    {
        if (!buddy_range_is_allocated(phys_addr, requested_order))
        {
            ++buddy_rejected_free_count;
            ++buddy_double_free_count;
            return;
        }
    }

    uint8_t current_order = requested_order;
    uint32_t current_phys = phys_addr;
    uint32_t page_count = 1u << requested_order;

    free_page_count += page_count;

    while (current_order < PMM_BUDDY_MAX_ORDER)
    {
        uint32_t block_size = buddy_order_block_size_bytes(current_order);
        uint32_t buddy_phys = current_phys ^ block_size;

        if (!buddy_can_merge(current_order, buddy_phys))
            break;
        if (!buddy_list_remove_exact(current_order, buddy_phys))
            break;

        current_phys = (buddy_phys < current_phys) ? buddy_phys : current_phys;
        ++current_order;
    }

    buddy_list_push(current_order, current_phys);
    pmm_update_watermarks();
}

/**
 * @brief Remove a page from the Buddy Allocator.
 * @details Allocation path with downward splitting.
 * @return Physical address, or 0 if empty.
 */
static uint32_t buddy_remove_order(uint8_t requested_order)
{
    if (!buddy_is_valid_order(requested_order))
        return 0;

    uint8_t order = requested_order;

    while (order <= PMM_BUDDY_MAX_ORDER && buddy_free_list_heads[order] == 0)
        ++order;

    if (order > PMM_BUDDY_MAX_ORDER)
        return 0;

    uint32_t block_phys = buddy_list_pop(order);
    if (!block_phys)
        return 0;

    while (order > 0u)
    {
        --order;
        uint32_t split_size = buddy_order_block_size_bytes(order);
        uint32_t right_buddy_phys = block_phys + split_size;

        if (order >= requested_order)
            buddy_list_push(order, right_buddy_phys);
    }

    free_page_count -= (1u << requested_order);
    pmm_update_watermarks();
    return block_phys;
}

static void buddy_insert(uint32_t phys_addr) { buddy_insert_order(phys_addr, 0u); }

static uint32_t buddy_remove(void) { return buddy_remove_order(0u); }

static void buddy_reset_state(void)
{
    for (uint32_t order = 0u; order <= PMM_BUDDY_MAX_ORDER; ++order)
        buddy_free_list_heads[order] = 0;

    for (uint32_t page_index = 0u; page_index < PMM_MAX_PAGE_COUNT; ++page_index)
    {
        buddy_page_is_free[page_index] = 0u;
        buddy_page_order[page_index] = 0u;
        buddy_page_allocated[page_index] = 0u;
    }

    buddy_population_in_progress = false;
    buddy_rejected_free_count = 0u;
    buddy_double_free_count = 0u;
}

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

void physical_memory_manager_page_frame_free_order(uint32_t phys_addr, uint8_t order)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (order != 0u)
        return;
    freelist_push(phys_addr);
#else
    buddy_insert_order(phys_addr, order);
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

uint32_t physical_memory_manager_page_frame_allocate_order(uint8_t order)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    if (order != 0u)
        return 0;
    return freelist_pop();
#else
    return buddy_remove_order(order);
#endif
}

uint32_t physical_memory_manager_get_free_page_count(void) { return free_page_count; }

const char *physical_memory_manager_get_strategy_name(void) { return pmm_strategy_name; }

bool physical_memory_manager_debug_is_free_block(uint32_t phys_addr, uint8_t order)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    (void) phys_addr;
    (void) order;
    return false;
#else
    return buddy_debug_is_free_block(phys_addr, order);
#endif
}

uint32_t physical_memory_manager_debug_get_rejected_free_count(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return 0u;
#else
    return buddy_rejected_free_count;
#endif
}

uint32_t physical_memory_manager_debug_get_double_free_count(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return 0u;
#else
    return buddy_double_free_count;
#endif
}

uint32_t physical_memory_manager_debug_get_free_block_count(uint8_t order)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    (void) order;
    return 0u;
#else
    return buddy_debug_get_free_block_count(order);
#endif
}

uint32_t physical_memory_manager_get_watermark_high(void) { return free_page_count_watermark_high; }

uint32_t physical_memory_manager_get_watermark_low(void)
{
    return watermarks_seeded ? free_page_count_watermark_low : 0u;
}

uint32_t physical_memory_manager_get_buddy_histogram(uint32_t *out_counts, uint32_t max_orders)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    (void) out_counts;
    (void) max_orders;
    return 0u;
#else
    uint32_t orders_to_write = max_orders;
    if (orders_to_write > PMM_BUDDY_MAX_ORDER + 1u)
        orders_to_write = PMM_BUDDY_MAX_ORDER + 1u;

    for (uint32_t order = 0u; order < orders_to_write; ++order)
        out_counts[order] = buddy_debug_get_free_block_count((uint8_t) order);

    return orders_to_write;
#endif
}

uint32_t physical_memory_manager_get_fragmentation_ratio(void)
{
#ifdef LPL_KERNEL_REAL_TIME_MODE
    return 0u;
#else
    if (free_page_count == 0u)
        return 0u;

    /* Find the highest order with at least one free block. */
    uint32_t largest_contiguous_pages = 0u;

    for (int order = (int) PMM_BUDDY_MAX_ORDER; order >= 0; --order)
    {
        uint32_t count = buddy_debug_get_free_block_count((uint8_t) order);
        if (count > 0u)
        {
            largest_contiguous_pages = 1u << (uint32_t) order;
            break;
        }
    }

    if (largest_contiguous_pages == 0u)
        return 100u;

    if (largest_contiguous_pages >= free_page_count)
        return 0u;

    return 100u - (largest_contiguous_pages * 100u / free_page_count);
#endif
}

void physical_memory_manager_initialize(void)
{
    uint8_t *mmap_base = NULL;
    uint32_t mmap_length = 0;

    if (!pmm_get_multiboot_mmap(&mmap_base, &mmap_length))
        return;

    free_page_count = 0u;
    free_page_count_watermark_high = 0u;
    free_page_count_watermark_low = 0xFFFFFFFFu;
    watermarks_seeded = false;

#ifndef LPL_KERNEL_REAL_TIME_MODE
    buddy_reset_state();
#else
    free_list_head = 0u;
#endif

    uint32_t kernel_phys_start = PMM_LOW_MEMORY_LIMIT;
    uint32_t kernel_phys_end = pmm_virt_to_phys((uint32_t) (uintptr_t) &global_kernel_end);

    uint32_t offset = 0;
    MemoryMapEntry_t *entry = NULL;

#ifndef LPL_KERNEL_REAL_TIME_MODE
    buddy_population_in_progress = true;
#endif

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

#ifndef LPL_KERNEL_REAL_TIME_MODE
    buddy_population_in_progress = false;
#endif
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

#ifndef LPL_KERNEL_REAL_TIME_MODE
    buddy_population_in_progress = true;
#endif

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

#ifndef LPL_KERNEL_REAL_TIME_MODE
    buddy_population_in_progress = false;
#endif
}
