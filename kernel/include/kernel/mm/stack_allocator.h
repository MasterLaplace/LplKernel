/*
** LplKernel
** kernel/include/kernel/mm/stack_allocator.h
**
** Fixed-size pre-allocated LIFO stack allocator.
*/

#ifndef KERNEL_MM_STACK_ALLOCATOR_H_
#define KERNEL_MM_STACK_ALLOCATOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern bool kernel_stack_allocator_initialize(uint32_t capacity_bytes);

extern void *kernel_stack_alloc_push(uint32_t size, uint32_t align);

extern uint32_t kernel_stack_alloc_get_marker(void);

extern void kernel_stack_alloc_rollback(uint32_t marker);

extern bool kernel_stack_allocator_is_initialized(void);

extern uint32_t kernel_stack_allocator_get_capacity(void);

extern uint32_t kernel_stack_allocator_get_used(void);

extern uint32_t kernel_stack_allocator_get_peak_used(void);

extern uint32_t kernel_stack_allocator_get_alloc_count(void);

extern uint32_t kernel_stack_allocator_get_rollback_count(void);

extern uint32_t kernel_stack_allocator_get_failed_alloc_count(void);

#endif /* !KERNEL_MM_STACK_ALLOCATOR_H_ */
