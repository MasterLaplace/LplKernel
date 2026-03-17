/*
** LplKernel
** kernel/include/kernel/mm/pool_allocator.h
**
** Fixed-size pre-allocated pool allocator.
*/

#ifndef KERNEL_MM_POOL_ALLOCATOR_H_
#define KERNEL_MM_POOL_ALLOCATOR_H_

#include <stdbool.h>
#include <stdint.h>

extern bool kernel_pool_allocator_initialize(uint32_t object_size, uint32_t object_count);

extern void *kernel_pool_alloc(void);

extern bool kernel_pool_free(void *ptr);

extern bool kernel_pool_allocator_is_initialized(void);

extern uint32_t kernel_pool_get_object_size(void);

extern uint32_t kernel_pool_get_capacity(void);

extern uint32_t kernel_pool_get_free_count(void);

extern uint32_t kernel_pool_get_peak_used_count(void);

extern uint32_t kernel_pool_get_failed_alloc_count(void);

extern uint32_t kernel_pool_get_wcet_alloc_cycles(void);

extern uint32_t kernel_pool_get_wcet_free_cycles(void);

#endif /* !KERNEL_MM_POOL_ALLOCATOR_H_ */
