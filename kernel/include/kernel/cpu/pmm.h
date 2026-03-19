/*
** LplKernel
** kernel/include/kernel/cpu/pmm.h
**
** Physical Memory Manager — Unified API
**
** The underlying allocation algorithm is selected at compile time:
**   - Client / Realtime build (LPL_KERNEL_REAL_TIME_MODE):
**       Free-List LIFO stack — O(1) deterministic alloc / free.
**   - Server build (default):
**       Buddy Allocator — efficient contiguous allocation (not yet implemented).
**
** Coverage at boot: physical pages from 1 MB to 16 MB (boot mapping limit).
** Pages beyond 16 MB are registered by physical_memory_manager_extend_mapping() once the paging
** subsystem can create new Page Tables.
*/

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

////////////////////////////////////////////////////////////
// Instrumentation: Watermarks, Histogram, Fragmentation
////////////////////////////////////////////////////////////

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
 *       After this call the PMM covers all usable RAM up to ~1 GB.
 */
extern void physical_memory_manager_extend_mapping(void);

#endif /* !PMM_H_ */
