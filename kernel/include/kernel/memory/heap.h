/*
** LplKernel
** kernel/include/kernel/memory/heap.h
**
** Kernel heap allocator facade.
*/

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

extern void *kmalloc(size_t size);

extern void *kmalloc_sensitive(size_t size);

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
 * @return Violation count on client builds, 0 on server builds.
 */
extern uint32_t kernel_heap_get_hot_loop_violation_count(void);

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
