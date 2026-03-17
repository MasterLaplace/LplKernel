/*
** LplKernel
** kernel/include/kernel/mm/tlsf.h
**
** Two-Level Segregated Fit (TLSF) deterministic allocator.
**
** Provides O(1) bounded-time allocation and deallocation for use in
** the client realtime kernel profile.  Operates on a pre-allocated
** memory pool donated at boot time — no runtime page allocation.
**
** FLI: First-Level Index  — log2 of block size, up to 16 classes.
** SLI: Second-Level Index — subdivision within each FLI class (4 bins).
*/

#ifndef KERNEL_MM_TLSF_H_
#define KERNEL_MM_TLSF_H_

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

/** @brief Return true if the TLSF allocator has been initialized. */
extern bool kernel_tlsf_is_initialized(void);

/** @brief Return the total pool capacity in bytes. */
extern uint32_t kernel_tlsf_get_pool_size(void);

/** @brief Return the current number of free bytes in the pool. */
extern uint32_t kernel_tlsf_get_free_bytes(void);

/** @brief Return the total number of successful allocations. */
extern uint32_t kernel_tlsf_get_alloc_count(void);

/** @brief Return the total number of successful frees. */
extern uint32_t kernel_tlsf_get_free_count(void);

/** @brief Return the number of failed allocation attempts. */
extern uint32_t kernel_tlsf_get_failed_alloc_count(void);

/** @brief Return the peak allocation cycles (WCET) observed. */
extern uint32_t kernel_tlsf_get_wcet_alloc_cycles(void);

/** @brief Return the peak free cycles (WCET) observed. */
extern uint32_t kernel_tlsf_get_wcet_free_cycles(void);

#endif /* !KERNEL_MM_TLSF_H_ */
