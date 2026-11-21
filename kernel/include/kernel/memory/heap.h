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
 * @file heap.h
 * @brief Kernel heap allocator facade.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_HEAP_H_
#define KERNEL_MEMORY_HEAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct KernelHeapBlock {
    uint32_t size;
    uint16_t magic;
    uint8_t flags;
    uint8_t order;
    uint16_t reserved;
    uint16_t canary;
    struct KernelHeapBlock *next;
} __attribute__((aligned(8))) KernelHeapBlock_t;

extern void kernel_heap_initialize(void);

/**
 * @brief Allocate memory from the kernel heap.
 *
 * Real-time rule, stated in terms of LATENCY rather than call sites.
 *
 * The client heap is slab (O(1) free-list) in front of TLSF (O(1), WCET
 * bounded to a few hundred x86 instructions). Both are exactly the
 * "pre-allocated, deterministic" allocators the real-time contract asks
 * for, so serving a request from either inside a hot loop breaks no
 * timing guarantee — refusing it only pushes the caller onto a worse
 * path. What the hot loop must never do is take an unbounded path: grow
 * the pool through the VMM/buddy, walk a first-fit list, or fail.
 *
 * So the guard no longer refuses everything: it lets the bounded paths
 * through (counted separately, for budgeting) and counts a violation
 * exactly when the allocation escapes past them.
 *
 * Past tlsf raw, every remaining path is unbounded (page-granular VMM
 * growth, first-fit walk) or the request simply cannot be served. Neither
 * belongs in an authoritative tick: refuse, and let the caller's own OOM
 * contract surface it rather than silently blowing a deadline.
 *
 * @param size The size of the memory to allocate.
 * @return A pointer to the allocated memory, or NULL on failure.
 */
extern void *kmalloc(size_t size);

extern void *kmalloc_sensitive(size_t size);

/**
 * @brief The sizes a request of @p size bytes takes: its payload rounded up to the heap's alignment, and its block, the
 *        payload after a block header, rounded the same way.
 *
 * @param size         The bytes asked for.
 * @param payload_size Receives the payload's bytes.
 * @param block_size   Receives the block's bytes.
 * @return false when the block, rounded up to whole pages, would not fit in 32 bits: no address space holds it.
 */
extern bool kernel_heap_request_sizes(size_t size, uint32_t *payload_size, uint32_t *block_size);

/**
 * @brief Whether the small pool may take one more page of physical memory for a request it holds no free block for.
 *
 * A grown page joins the free pages beside it, so a block of several pages can come from the pool when the virtual
 * memory manager cannot serve one; but no growth serves a block larger than all the free memory left, and a request
 * takes at most one page more than its block spans.
 *
 * @param block_size  The block the request needs, header included.
 * @param pages_grown Pages already taken for this request.
 * @param free_pages  Free pages physical memory still holds.
 * @param free_bytes  Free bytes the small pool already holds.
 * @return false when the block is larger than @p free_pages and @p free_bytes together, or once @p pages_grown passes
 *         the pages the block spans.
 */
extern bool kernel_heap_small_pool_may_grow(uint32_t block_size, uint32_t pages_grown, uint32_t free_pages,
                                            uint32_t free_bytes);

extern void kfree(void *ptr);

extern const char *kernel_heap_get_strategy_name(void);

extern uint32_t kernel_heap_get_small_free_block_count(void);

extern uint32_t kernel_heap_get_small_free_bytes(void);

extern uint32_t kernel_heap_get_large_allocation_count(void);

extern uint32_t kernel_heap_debug_get_rejected_free_count(void);

extern uint32_t kernel_heap_debug_get_double_free_count(void);

/**
 * @brief Enter the client hot loop allocation guard scope.
 *
 * @note Server builds expose a no-op implementation.
 */
extern void kernel_heap_hot_loop_enter(void);

/**
 * @brief Leave the client hot loop allocation guard scope.
 *
 * @note Server builds expose a no-op implementation.
 */
extern void kernel_heap_hot_loop_leave(void);

/**
 * @brief Return current hot loop nesting depth.
 *
 * @return Hot loop nesting depth on client builds, 0 on server builds.
 */
extern uint32_t kernel_heap_get_hot_loop_depth(void);

/**
 * @brief Return total hot loop allocation guard violations.
 *
 * A violation is an allocation or release that had to take an UNBOUNDED path
 * while inside a hot loop (VMM/buddy growth, first-fit walk), or one that could
 * not be served at all. Requests satisfied by the slab caches or by TLSF are
 * O(1) with a bounded worst case, so they are permitted and counted by
 * @ref kernel_heap_get_hot_loop_bounded_count instead.
 *
 * @return Violation count on client builds, 0 on server builds.
 */
extern uint32_t kernel_heap_get_hot_loop_violation_count(void);

/**
 * @brief Return hot loop allocations/releases served by a bounded path.
 *
 * Not errors — a budgeting figure. Growth here means the tick is doing O(1) but
 * non-zero heap traffic, which is worth removing for throughput even though it
 * breaks no real-time guarantee.
 *
 * @return Bounded hot loop operation count on client builds, 0 on server builds.
 */
extern uint32_t kernel_heap_get_hot_loop_bounded_count(void);

/**
 * @brief Return available free blocks in a server size class.
 *
 * @param size_class_index Server size class index.
 * @return Free block count on server builds, 0 on client builds.
 */
extern uint32_t kernel_heap_get_size_class_free_count(uint32_t size_class_index);

/**
 * @brief Return hit count for a server size class fast path.
 *
 * @param size_class_index Server size class index.
 * @return Hit count on server builds, 0 on client builds.
 */
extern uint32_t kernel_heap_get_size_class_hit_count(uint32_t size_class_index);

extern bool kernel_heap_set_server_active_domain(uint32_t domain_index);
extern uint32_t kernel_heap_get_server_domain_count(void);
extern uint32_t kernel_heap_get_server_active_domain(void);
extern uint32_t kernel_heap_get_server_domain_refill_count(uint32_t domain_index, uint32_t size_class_index);
extern uint32_t kernel_heap_get_server_domain_first_fit_fallback_count(uint32_t domain_index);
extern uint32_t kernel_heap_get_server_domain_remote_probe_count(uint32_t domain_index);
extern uint32_t kernel_heap_get_server_domain_remote_hit_count(uint32_t domain_index);

extern void kernel_heap_initialize_ap_domain(uint32_t logical_slot);
extern uint32_t kernel_heap_get_server_per_cpu_hit_count(uint32_t slot);

#endif /* !KERNEL_MEMORY_HEAP_H_ */
