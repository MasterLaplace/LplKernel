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
 * @file stack_allocator.h
 * @brief Fixed-size pre-allocated LIFO stack allocator.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_STACK_ALLOCATOR_H_
#define KERNEL_MEMORY_STACK_ALLOCATOR_H_

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

#endif /* !KERNEL_MEMORY_STACK_ALLOCATOR_H_ */
