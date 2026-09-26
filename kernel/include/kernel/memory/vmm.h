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
 * @file vmm.h
 * @brief Virtual Memory Manager for the kernel address space.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_MEMORY_VMM_H_
#define KERNEL_MEMORY_VMM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @name VMM Regions
 * @{
 */
#define KERNEL_VMM_DYNAMIC_START 0xD0000000u
#define KERNEL_VMM_DYNAMIC_END   0xF0000000u
#define KERNEL_VMM_DYNAMIC_SIZE  (KERNEL_VMM_DYNAMIC_END - KERNEL_VMM_DYNAMIC_START)
/** @} */

/**
 * @brief Initialize the Virtual Memory Manager.
 */
bool kernel_vmm_initialize(void);

/**
 * @brief Allocate a contiguous range of virtual pages and map them to physical frames.
 *
 * @param page_count Number of pages to allocate.
 * @return Virtual address of the allocated range, or NULL on failure.
 */
void *kernel_vmm_alloc_pages(uint32_t page_count);

/**
 * @brief Reserve a range of virtual pages without mapping them.
 *
 * @param page_count Number of pages to reserve.
 * @return Virtual address of the reserved range, or NULL on failure.
 */
void *kernel_vmm_reserve_pages(uint32_t page_count);

/**
 * @brief Reserve a FIXED range of virtual pages without mapping them.
 *
 * @param virt Start virtual address (must be page-aligned).
 * @param page_count Number of pages to reserve.
 * @return true if reserved successfully, false if already in use or out of range.
 */
bool kernel_vmm_reserve_at(void *virt, uint32_t page_count);

/**
 * @brief Free a range of virtual pages (and their physical frames if mapped).
 *
 * @param ptr Virtual address to free.
 * @param page_count Number of pages to free.
 */
void kernel_vmm_free_pages(void *ptr, uint32_t page_count);

#endif /* !KERNEL_MEMORY_VMM_H_ */
