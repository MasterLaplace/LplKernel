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
 * @brief Free (return) a physical 4 KB page to the pool.
 *
 * @param phys_addr Physical address to free (must be 4 KB aligned).
 */
extern void physical_memory_manager_page_frame_free(uint32_t phys_addr);

/**
 * @brief Return the current number of free pages.
 *
 * @return Number of pages currently in the free pool.
 */
extern uint32_t physical_memory_manager_get_free_page_count(void);

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
