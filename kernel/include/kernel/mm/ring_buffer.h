/*
** LplKernel
** kernel/include/kernel/mm/ring_buffer.h
**
** Fixed-slot pre-allocated ring buffer.
*/

#ifndef KERNEL_MM_RING_BUFFER_H_
#define KERNEL_MM_RING_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum KernelRingBufferMode {
	KERNEL_RING_BUFFER_MODE_LOCAL = 0u,
	KERNEL_RING_BUFFER_MODE_SPSC  = 1u,
    KERNEL_RING_BUFFER_MODE_MPSC  = 2u,
    KERNEL_RING_BUFFER_MODE_MPMC  = 3u,
} KernelRingBufferMode_t;

extern bool kernel_ring_buffer_initialize(uint32_t slot_size, uint32_t slot_count);

extern bool kernel_ring_buffer_initialize_ex(uint32_t slot_size, uint32_t slot_count, KernelRingBufferMode_t mode);

extern bool kernel_ring_buffer_enqueue(const void *data, uint32_t size);

extern bool kernel_ring_buffer_dequeue(void *out_data, uint32_t out_size);

extern bool kernel_ring_buffer_is_initialized(void);

extern uint32_t kernel_ring_buffer_get_slot_size(void);

extern uint32_t kernel_ring_buffer_get_capacity(void);

extern uint32_t kernel_ring_buffer_get_count(void);

extern KernelRingBufferMode_t kernel_ring_buffer_get_mode(void);

extern uint32_t kernel_ring_buffer_get_high_watermark(void);

extern uint32_t kernel_ring_buffer_get_enqueue_count(void);

extern uint32_t kernel_ring_buffer_get_dequeue_count(void);

extern uint32_t kernel_ring_buffer_get_failed_enqueue_count(void);

extern uint32_t kernel_ring_buffer_get_failed_dequeue_count(void);

#endif /* !KERNEL_MM_RING_BUFFER_H_ */
