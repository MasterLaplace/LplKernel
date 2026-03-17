/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** vmm — Virtual Memory Manager for kernel address space
*/

#ifndef KERNEL_MM_VMM_H_
#define KERNEL_MM_VMM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* VMM Regions */
#define KERNEL_VMM_DYNAMIC_START 0xD0000000u
#define KERNEL_VMM_DYNAMIC_END   0xF0000000u
#define KERNEL_VMM_DYNAMIC_SIZE  (KERNEL_VMM_DYNAMIC_END - KERNEL_VMM_DYNAMIC_START)

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

#endif /* !KERNEL_MM_VMM_H_ */
