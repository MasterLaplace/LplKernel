/*
** LplKernel
** kernel/include/kernel/mm/frame_arena.h

mm (Memory Management) subsystem header for a pre-allocated frame arena allocator.
This allocator provides a simple linear allocation strategy from a fixed-size
**
** Pre-allocated frame arena allocator.
*/

#ifndef KERNEL_MM_FRAME_ARENA_H_
#define KERNEL_MM_FRAME_ARENA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern bool kernel_frame_arena_initialize(uint32_t capacity_bytes);

extern void *kernel_frame_arena_alloc(uint32_t size, uint32_t align);

extern void kernel_frame_arena_reset(void);

extern bool kernel_frame_arena_is_initialized(void);

extern uint32_t kernel_frame_arena_get_capacity_bytes(void);

extern uint32_t kernel_frame_arena_get_used_bytes(void);

extern uint32_t kernel_frame_arena_get_peak_used_bytes(void);

extern uint32_t kernel_frame_arena_get_reset_count(void);

extern uint32_t kernel_frame_arena_get_failed_alloc_count(void);

extern void kernel_frame_arena_set_frame_budget(uint32_t budget_bytes);

extern uint32_t kernel_frame_arena_get_budget_exceeded_count(void);

extern uint32_t kernel_frame_arena_get_wcet_alloc_cycles(void);

extern uint32_t kernel_frame_arena_get_wcet_reset_cycles(void);

#endif /* !KERNEL_MM_FRAME_ARENA_H_ */
