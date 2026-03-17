/*
** LplKernel
** kernel/include/kernel/mm/slab.h
**
** Fixed-size slab allocator.
**
** CLIENT profile: three statically-backed caches (16 B / 64 B / 256 B).
**   All objects are vended from pages pre-allocated in the boot pool.
**   Allocation and free are O(1) with no runtime PMM calls.
**
** SERVER profile: stub — returns NULL / no-op so heap.c falls through
**   to the first-fit path.
*/

#ifndef KERNEL_MM_SLAB_H_
#define KERNEL_MM_SLAB_H_

#include <stdbool.h>
#include <stdint.h>

/* Supported object sizes.  Keep in sync with the array in slab.c. */
#define KERNEL_SLAB_SIZE_SMALL  16u
#define KERNEL_SLAB_SIZE_MEDIUM 64u
#define KERNEL_SLAB_SIZE_LARGE  256u

/* Maximum number of pages any single cache may own (client boot pool). */
#define KERNEL_SLAB_CACHE_MAX_PAGES 2u

/*
 * Initialise all slab caches.  Called once from kernel_heap_initialize()
 * AFTER the physical pages for the boot pool have been mapped.
 *
 * backing_pages[]: array of virtual addresses (already mapped) to donate
 *                  to the slab subsystem.
 * page_count: number of entries in that array.
 */
extern void kernel_slab_initialize(void **backing_pages, uint32_t page_count);

/*
 * Attempt to allocate an object of exactly `size` bytes from the slab.
 * Returns a pointer to the object, or NULL if no matching cache exists
 * or the cache is exhausted.
 */
extern void *kernel_slab_alloc(uint32_t size);

/*
 * Return an object to its cache.  ptr must have been returned by
 * kernel_slab_alloc(); passing any other pointer is a no-op (guarded by
 * the cache ownership check).
 * Returns true when the object was accepted, false otherwise.
 */
extern bool kernel_slab_free(void *ptr);

/* Telemetry helpers (per-cache). */
extern uint32_t kernel_slab_get_free_count(uint32_t object_size);
extern uint32_t kernel_slab_get_used_count(uint32_t object_size);

#endif /* !KERNEL_MM_SLAB_H_ */
