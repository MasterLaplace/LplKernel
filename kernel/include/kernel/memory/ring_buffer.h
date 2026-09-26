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
 * @file ring_buffer.h
 * @brief Fixed-slot pre-allocated ring buffer.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_RING_BUFFER_H_
#define KERNEL_MEMORY_RING_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum KernelRingBufferMode {
    KERNEL_RING_BUFFER_MODE_LOCAL = 0u,
    KERNEL_RING_BUFFER_MODE_SPSC = 1u,
    KERNEL_RING_BUFFER_MODE_MPSC = 2u,
    KERNEL_RING_BUFFER_MODE_MPMC = 3u,
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

#endif /* !KERNEL_MEMORY_RING_BUFFER_H_ */
