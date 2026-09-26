/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
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
 * @file pinned_memory.h
 * @brief Pinned, physically backed pages for device access.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_PINNED_MEMORY_H_
#define KERNEL_MEMORY_PINNED_MEMORY_H_

#include <stdbool.h>
#include <stdint.h>

extern bool kernel_pinned_memory_initialize(void);
/**
 * @brief Allocates physically backed, pinned pages for device access.
 *
 * @note The virtual range comes from the VMM, which finds the first free one, so a pinned
 *       allocation is never handed a range already claimed by the framebuffer or others.
 *
 * @param size Bytes wanted, rounded up to whole pages.
 * @return The virtual address, or NULL.
 */
extern void *kernel_pinned_alloc(uint32_t size);
extern void kernel_pinned_free(void *ptr, uint32_t size);
extern uint32_t kernel_pinned_get_allocated_pages(void);
extern uint32_t kernel_pinned_get_released_pages(void);

#endif /* !KERNEL_MEMORY_PINNED_MEMORY_H_ */
