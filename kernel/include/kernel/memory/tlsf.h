/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
 * @file tlsf.h
 * @brief Two-Level Segregated Fit (TLSF) deterministic allocator.
 *
 * Provides O(1) bounded-time allocation and deallocation for use in the client
 * realtime kernel profile. Operates on a single contiguous memory pool donated at
 * boot time — no runtime page allocation. Both operations are bounded by a constant
 * number of bit-scans and pointer dereferences.
 *
 * Design:
 *   - FLI (First-Level Index): log2 of the block size, up to 16 classes.
 *   - SLI (Second-Level Index): subdivision within each FLI class (4 bins).
 *   - Two bitmaps (fl_bitmap, sl_bitmap[]) for O(1) best-fit search.
 *   - Each block has a header: { size | prev_phys_block | free_prev | free_next }.
 *   - Immediate coalescing on free (boundary-tag style).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_TLSF_H_
#define KERNEL_MEMORY_TLSF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize the TLSF allocator with a pre-allocated memory pool.
 *
 * @param buffer   Start of the memory pool (must be 8-byte aligned).
 * @param size     Total size of the memory pool in bytes.
 * @return true on success, false if pool is too small or misaligned.
 */
extern bool kernel_tlsf_initialize(void *buffer, size_t size);

/**
 * @brief Allocate a block of at least @p size bytes.
 *
 * Beyond the mapping's reach: refuse rather than let the fl clamp hand back
 * a block that does not fit (see TLSF_MAX_BLOCK_SIZE).
 *
 * @param size  Requested allocation size (rounded up internally).
 * @return Pointer to usable memory, or NULL if no suitable block exists.
 */
extern void *kernel_tlsf_alloc(size_t size);

/**
 * @brief Free a previously allocated block.
 *
 * @param ptr  Pointer returned by kernel_tlsf_alloc (NULL is a no-op).
 */
extern void kernel_tlsf_free(void *ptr);

/**
 * @brief Check if a pointer belongs to the TLSF pool.
 *
 * @param ptr  Pointer to test.
 * @return true if ptr falls within the TLSF-managed memory region.
 */
extern bool kernel_tlsf_owns(const void *ptr);

/**
 * @brief Return true if the TLSF allocator has been initialized.
 */
extern bool kernel_tlsf_is_initialized(void);

/**
 * @brief Return the total pool capacity in bytes.
 */
extern uint32_t kernel_tlsf_get_pool_size(void);

/**
 * @brief Return the current number of free bytes in the pool.
 */
extern uint32_t kernel_tlsf_get_free_bytes(void);

/**
 * @brief Return the total number of successful allocations.
 */
extern uint32_t kernel_tlsf_get_alloc_count(void);

/**
 * @brief Return the total number of successful frees.
 */
extern uint32_t kernel_tlsf_get_free_count(void);

/**
 * @brief Return the number of failed allocation attempts.
 */
extern uint32_t kernel_tlsf_get_failed_alloc_count(void);

/**
 * @brief Return the peak allocation cycles (WCET) observed.
 */
extern uint32_t kernel_tlsf_get_wcet_alloc_cycles(void);

/**
 * @brief Return the peak free cycles (WCET) observed.
 */
extern uint32_t kernel_tlsf_get_wcet_free_cycles(void);

#endif /* !KERNEL_MEMORY_TLSF_H_ */
