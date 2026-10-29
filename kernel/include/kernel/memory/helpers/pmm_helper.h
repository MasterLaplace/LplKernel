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
 * @file pmm_helper.h
 * @brief Serial reports of the physical memory manager.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_MEMORY_PMM_HELPER_H
#define KERNEL_MEMORY_PMM_HELPER_H

#include <kernel/drivers/serial.h>

#include <stdint.h>

extern void write_pmm_strategy_info(Serial_t *serial);

/**
 * @brief Reports the first pass of the physical memory manager: the pages it freed, and those it unmapped
 *        from the boot map.
 *
 * @param serial                   The port the report goes to.
 * @param boot_map_pages_unmapped  What physical_memory_manager_restrict_boot_mapping returned.
 */
extern void write_pmm_pass_1_info(Serial_t *serial, uint32_t boot_map_pages_unmapped);
extern void write_pmm_pass_2_start_info(Serial_t *serial);
extern void write_pmm_ready_info(Serial_t *serial);
extern void write_pmm_buddy_info(Serial_t *serial);
extern void write_pmm_guard_info(Serial_t *serial);

#endif /* KERNEL_MEMORY_PMM_HELPER_H */
