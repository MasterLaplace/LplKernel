/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file slab.h
 * @brief Fixed-size slab allocator.
 *
 * CLIENT profile: three statically-backed caches (16 B / 64 B / 256 B). All
 * objects are vended from pages pre-allocated in the boot pool. Allocation and free
 * are O(1) with no runtime PMM calls. Each cache owns a singly-linked free-list of
 * same-sized objects whose node is stored inside the object slot (the first
 * sizeof(void*) bytes of a free slot hold the next pointer), so no object carries a
 * header.
 *
 * SERVER profile: stub — returns NULL / no-op so heap.c falls through to the
 * first-fit path.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_SLAB_H_
#define KERNEL_MEMORY_SLAB_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @name Supported object sizes
 *
 * Keep in sync with the array in slab.c.
 * @{
 */
#define KERNEL_SLAB_SIZE_SMALL  16u
#define KERNEL_SLAB_SIZE_MEDIUM 64u
#define KERNEL_SLAB_SIZE_LARGE  256u
/** @} */

/** Maximum number of pages any single cache may own (client boot pool). */
#define KERNEL_SLAB_CACHE_MAX_PAGES 2u

/**
 * @brief Initialises all slab caches.
 *
 * @details Called once from kernel_heap_initialize() AFTER the physical pages for the boot
 *          pool have been mapped.
 *
 * @param backing_pages Virtual addresses (already mapped) donated to the slab subsystem.
 * @param page_count    Number of entries in @p backing_pages.
 */
extern void kernel_slab_initialize(void **backing_pages, uint32_t page_count);

/**
 * @brief Allocates an object of exactly @p size bytes from the slab.
 *
 * @param size Object size; must match one of the supported sizes.
 * @return The object, or NULL if no matching cache exists or the cache is exhausted.
 */
extern void *kernel_slab_alloc(uint32_t size);

/**
 * @brief Returns an object to its cache.
 *
 * @details @p ptr must have been returned by kernel_slab_alloc(); any other pointer is a
 *          no-op, guarded by the cache ownership check.
 *
 * @param ptr The object to release.
 * @return true when the object was accepted, false otherwise.
 */
extern bool kernel_slab_free(void *ptr);

/**
 * @brief Objects currently free in the cache of @p object_size.
 *
 * @param object_size One of the supported sizes.
 * @return The count, 0 for an unsupported size.
 */
extern uint32_t kernel_slab_get_free_count(uint32_t object_size);

/**
 * @brief Objects currently live in the cache of @p object_size.
 *
 * @param object_size One of the supported sizes.
 * @return The count, 0 for an unsupported size.
 */
extern uint32_t kernel_slab_get_used_count(uint32_t object_size);

#endif /* !KERNEL_MEMORY_SLAB_H_ */
