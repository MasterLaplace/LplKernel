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
 * @details The run is unmapped with one TLB shootdown, and its frames go back to the physical memory
 *          manager only once every CPU has acknowledged it (paging_unmap_range).
 *
 * @param ptr Virtual address to free.
 * @param page_count Number of pages to free.
 */
void kernel_vmm_free_pages(void *ptr, uint32_t page_count);

/**
 * @brief Maps a device's memory window into a free run of kernel virtual pages, uncached.
 *
 * @details Cache-disabled and write-through, because these are registers: a cached read would
 *          return whatever the line held when it was filled, which for a status register is the
 *          answer to a question nobody asked. A window that cannot be mapped whole is given back
 *          without freeing its frames, as kernel_vmm_unmap_device_window does.
 *
 * @param physical_base Physical address of the window, as its base address register gives it.
 * @param size          Window length in bytes.
 * @return The virtual address of @p physical_base, its offset within its page kept, or 0.
 */
uint32_t kernel_vmm_map_device_window(uint32_t physical_base, uint32_t size);

/**
 * @brief Unmaps a window kernel_vmm_map_device_window gave, and frees none of its frames.
 *
 * @details The frames are the device's registers, not memory the physical memory manager handed
 *          out: kernel_vmm_free_pages would give them to it, and the next allocation would land on
 *          the device.
 *
 * @param window The address kernel_vmm_map_device_window returned.
 * @param size   The size it was given.
 */
void kernel_vmm_unmap_device_window(uint32_t window, uint32_t size);

/**
 * @brief Maps memory the firmware wrote, such as its ACPI tables, into a free run of kernel virtual
 *        pages, cached and read-only.
 *
 * @details Unlike a device window it is cached, since these frames are memory, and read-only, since
 *          the kernel has no reason to write what the firmware describes. Like one, it does not rely
 *          on the frames lying where the kernel's own mapping of physical memory reaches, and its
 *          frames are never handed to the physical memory manager.
 *
 * @param physical_base Physical address of the first byte.
 * @param size          Length in bytes.
 * @return The virtual address of @p physical_base, its offset within its page kept, or 0.
 */
uint32_t kernel_vmm_map_firmware_window(uint32_t physical_base, uint32_t size);

/**
 * @brief Unmaps a window kernel_vmm_map_firmware_window gave, and frees none of its frames.
 *
 * @param window The address kernel_vmm_map_firmware_window returned.
 * @param size   The size it was given.
 */
void kernel_vmm_unmap_firmware_window(uint32_t window, uint32_t size);

/**
 * @brief Freed runs that had at least one page mapped, each unmapped with one TLB shootdown.
 * @return The count since boot.
 */
uint32_t kernel_vmm_get_unmapped_range_count(void);

#endif /* !KERNEL_MEMORY_VMM_H_ */
