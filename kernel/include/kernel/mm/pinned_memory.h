#ifndef KERNEL_MM_PINNED_MEMORY_H_
#define KERNEL_MM_PINNED_MEMORY_H_

#include <stdbool.h>
#include <stdint.h>

extern bool kernel_pinned_memory_initialize(void);
extern void *kernel_pinned_alloc(uint32_t size);
extern void kernel_pinned_free(void *ptr, uint32_t size);
extern uint32_t kernel_pinned_get_allocated_pages(void);
extern uint32_t kernel_pinned_get_released_pages(void);

#endif /* !KERNEL_MM_PINNED_MEMORY_H_ */
