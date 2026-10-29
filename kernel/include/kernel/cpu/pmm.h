/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file pmm.h
 * @brief Physical Memory Manager: one API, two algorithms.
 *
 * The public API (physical_memory_manager_initialize,
 * physical_memory_manager_page_frame_allocate, physical_memory_manager_page_frame_free,
 * ...) is identical for both kernel modes. The compile-time flag
 * LPL_KERNEL_REAL_TIME_MODE selects the backing data structure:
 *   - Client / realtime build: intrusive free-list LIFO stack, O(1) deterministic
 *     alloc / free.
 *   - Server build (default): buddy allocator with split / merge support.
 *
 * Coverage at boot: physical pages from 1 MB to 16 MB (boot mapping limit). Pages
 * beyond 16 MB are registered by physical_memory_manager_extend_mapping() once the
 * paging subsystem can create new page tables.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-04
 **************************************************************************/

#ifndef PMM_H_
#define PMM_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the Physical Memory Manager.
 *
 * @details Parses the Multiboot memory map and registers every usable
 *          physical page (4 KB aligned) into the allocator pool, skipping
 *          the first 1 MB, the kernel footprint, and pages beyond the
 *          16 MB boot mapping limit.
 *
 * @note Must be called AFTER paging_initialize_runtime().
 */
extern void physical_memory_manager_initialize(void);

/**
 * @brief What physical_memory_manager_restrict_boot_mapping found and did.
 */
typedef struct PhysicalMemoryManagerBootMapRestriction {
    bool memory_map;         /**< The bootloader gave a memory map; without one nothing is unmapped. */
    uint32_t runs_unmapped;  /**< Runs of pages unmapped, each with one TLB shootdown. */
    uint32_t pages_unmapped; /**< Pages unmapped. */
} PhysicalMemoryManagerBootMapRestriction_t;

/**
 * @brief The first run of pages, from @p from on, past the first MiB and below the end of the boot map (64 MiB),
 *        that no usable RAM entry of the Multiboot memory map @p map holds whole: what
 *        physical_memory_manager_restrict_boot_mapping unmaps.
 *
 * @details The first MiB stays, as Linux keeps it mapped: the kernel reads VGA text, the BIOS data area and the
 *          BIOS area through it. Past it, a page stays only when one usable RAM entry holds it whole, as Linux maps
 *          only RAM there; ACPI memory and everything else are reached through windows. Entries are read as
 *          Multiboot lays them out, each its size then that many bytes, up to the first whose size is below 20 or
 *          that runs past @p length; an entry or the part of one above 4 GiB holds nothing.
 *
 * @param map       The memory map's first entry, or NULL.
 * @param length    The memory map's length in bytes; 0, like a NULL @p map, means no map, and no hole.
 * @param from      Where to search from, rounded down to its page and raised to the first MiB's end.
 * @param out_first Receives the run's first page.
 * @param out_end   Receives the page past its last, at most 64 MiB.
 * @return true and the run, or false when none is left.
 */
extern bool physical_memory_manager_boot_map_next_hole(const uint8_t *map, uint32_t length, uint32_t from,
                                                       uint32_t *out_first, uint32_t *out_end);

/**
 * @brief Unmaps from the boot map every run physical_memory_manager_boot_map_next_hole finds in the bootloader's
 *        memory map.
 *
 * @details boot.S maps 0 to 64 MiB write-back before the memory map is read, device memory, reserved ranges and
 *          holes included; an uncached window on such a page would then alias a write-back one, which Intel SDM
 *          vol. 3, §14.12.4 calls undefined. Without a memory map nothing is unmapped.
 *
 * @note Called once, after physical_memory_manager_initialize() and before any window is mapped or any other
 *       processor starts.
 * @return Whether there was a map, and the runs and pages unmapped.
 */
extern PhysicalMemoryManagerBootMapRestriction_t physical_memory_manager_restrict_boot_mapping(void);

/**
 * @brief Allocate one physical 4 KB page.
 *
 * @return Physical address of the allocated page (4 KB aligned),
 *         or 0 if no free pages remain.
 */
extern uint32_t physical_memory_manager_page_frame_allocate(void);

/**
 * @brief Allocate a contiguous 2^order block of 4 KB physical pages.
 *
 * @param order Buddy order (0 => 1 page, 1 => 2 pages, ...).
 * @return Base physical address of the allocated block, or 0 on failure.
 */
extern uint32_t physical_memory_manager_page_frame_allocate_order(uint8_t order);

/**
 * @brief Free (return) a physical 4 KB page to the pool.
 *
 * @param phys_addr Physical address to free (must be 4 KB aligned).
 */
extern void physical_memory_manager_page_frame_free(uint32_t phys_addr);

/**
 * @brief Free a contiguous 2^order block of 4 KB physical pages.
 *
 * @param phys_addr Base physical address of the allocated block.
 * @param order Buddy order used at allocation time.
 */
extern void physical_memory_manager_page_frame_free_order(uint32_t phys_addr, uint8_t order);

/**
 * @brief Return the current number of free pages.
 *
 * @return Number of pages currently in the free pool.
 */
extern uint32_t physical_memory_manager_get_free_page_count(void);

/**
 * @brief Says what the boot memory map holds at a physical range.
 *
 * @param physical_address First byte of the range.
 * @param length           Its length in bytes.
 * @details It reads the copy of the map initialisation kept, its first 64 entries, never the
 *          loader's, whose memory may be reused by then.
 * @return The Multiboot type of the entry that holds the whole range (1 usable, 2 reserved, 3 ACPI
 *         reclaimable, 4 ACPI NVS, 5 defective), or 0 when no entry kept holds it whole, the range is
 *         empty or wraps, or the boot gave no memory map.
 */
extern uint32_t physical_memory_manager_memory_type(uint64_t physical_address, uint64_t length);

/**
 * @brief Return the active PMM strategy name.
 *
 * @return Human-readable strategy string selected at compile time.
 */
extern const char *physical_memory_manager_get_strategy_name(void);

/**
 * @brief Debug helper: check whether a buddy free block exists at address/order.
 *
 * @param phys_addr Base physical address of the candidate block.
 * @param order Buddy order (0=4KB, 1=8KB, ...).
 * @return true when the exact block is currently free in server buddy mode.
 */
extern bool physical_memory_manager_debug_is_free_block(uint32_t phys_addr, uint8_t order);

/**
 * @brief Debug helper: number of rejected free requests since last PMM init.
 */
extern uint32_t physical_memory_manager_debug_get_rejected_free_count(void);

/**
 * @brief Debug helper: number of rejected frees classified as double-free.
 */
extern uint32_t physical_memory_manager_debug_get_double_free_count(void);

/**
 * @brief Debug helper: number of free blocks available at a buddy order.
 *
 * @param order Buddy order (0 => 4 KB, 1 => 8 KB, ...).
 * @return Number of currently free blocks at this order.
 */
extern uint32_t physical_memory_manager_debug_get_free_block_count(uint8_t order);

extern uint32_t physical_memory_manager_get_uaf_anomaly_count(void);

/**
 * @brief Return the peak (high-water mark) free page count observed since init.
 */
extern uint32_t physical_memory_manager_get_watermark_high(void);

/**
 * @brief Return the trough (low-water mark) free page count observed since init.
 */
extern uint32_t physical_memory_manager_get_watermark_low(void);

/**
 * @brief Export the buddy free-block histogram (server only).
 *
 * @param out_counts  Output array, filled with free-block counts per order.
 * @param max_orders  Maximum number of order slots in out_counts.
 * @return Number of orders actually written.
 */
extern uint32_t physical_memory_manager_get_buddy_histogram(uint32_t *out_counts, uint32_t max_orders);

/**
 * @brief Return the fragmentation ratio (0..100) of the buddy allocator.
 *
 * @details Defined as 100 - (largest_contiguous_free_pages * 100 / total_free_pages).
 *          Returns 0 on client mode or when no free pages exist.
 */
extern uint32_t physical_memory_manager_get_fragmentation_ratio(void);

/**
 * @brief Extend the free pool beyond the 16 MB boot mapping limit.
 *
 * @details Uses paging_map_page() — which relies on an already-populated
 *          PMM pool — to create virtual mappings for every available
 *          physical page above 16 MB, then registers them in the pool.
 *
 * @note Must be called AFTER physical_memory_manager_initialize().  Resolves
 *       the chicken-and-egg dependency between paging_map_page()
 *       (needs physical_memory_manager_page_frame_allocate)
 *       and the PMM (needs virtual mappings to access physical pages).
 *       After this call the PMM covers all usable RAM below 256 MiB, where the
 *       mapping at phys + 0xC0000000 meets the virtual memory manager's range.
 */
extern void physical_memory_manager_extend_mapping(void);

#endif /* !PMM_H_ */
