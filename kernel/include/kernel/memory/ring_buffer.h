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
 * @file ring_buffer.h
 * @brief Fixed-slot pre-allocated ring buffer.
 *
 * The boot's byte-record ring: slots of a fixed size, allocated once, one producer and one
 * consumer. It runs on the SPSC ring primitive (kernel/memory/spsc_ring.h), so its producer and
 * consumer keep their indices apart and share nothing else; each side counts its own operations.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_RING_BUFFER_H_
#define KERNEL_MEMORY_RING_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Allocates the ring, once.
 *
 * @param slot_size  Most bytes one record holds, rounded up to 8.
 * @param slot_count Records the ring holds: a power of two.
 * @return true when the ring is ready, including when it already was.
 */
extern bool kernel_ring_buffer_initialize(uint32_t slot_size, uint32_t slot_count);

/**
 * @brief Producer: copies @p size bytes into the next slot.
 * @return false when the ring is full, not initialized, or @p size is 0 or over the slot size.
 */
extern bool kernel_ring_buffer_enqueue(const void *data, uint32_t size);

/**
 * @brief Consumer: copies the oldest record into @p out_data and removes it.
 *
 * @details Bytes of @p out_data past the record's own size are zeroed.
 * @return false when the ring is empty, not initialized, or @p out_size is 0 or over the slot size.
 */
extern bool kernel_ring_buffer_dequeue(void *out_data, uint32_t out_size);

extern bool kernel_ring_buffer_is_initialized(void);

extern uint32_t kernel_ring_buffer_get_slot_size(void);

extern uint32_t kernel_ring_buffer_get_capacity(void);

/** @brief Records in the ring, as one snapshot. */
extern uint32_t kernel_ring_buffer_get_count(void);

/** @brief The most records the ring held after an enqueue. */
extern uint32_t kernel_ring_buffer_get_high_watermark(void);

extern uint32_t kernel_ring_buffer_get_enqueue_count(void);

extern uint32_t kernel_ring_buffer_get_dequeue_count(void);

extern uint32_t kernel_ring_buffer_get_failed_enqueue_count(void);

extern uint32_t kernel_ring_buffer_get_failed_dequeue_count(void);

#endif /* !KERNEL_MEMORY_RING_BUFFER_H_ */
