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
 * @file mtrr.h
 * @brief The memory type range registers the firmware left, read once: whether they make a page write-back.
 *
 * A window on a reserved page is write-back where these registers say write-back over it, as Linux maps ACPI
 * memory (kernel_vmm_map_device_window). Intel SDM vol. 3 (325384-093US), §14.11: CPUID.01H:EDX bit 12 says the
 * registers exist, IA32_MTRRCAP counts the variable ranges, IA32_MTRR_DEF_TYPE enables them and gives the type of
 * memory no range covers, and §14.11.4.1 orders the ranges that overlap.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-06
 **************************************************************************/

#ifndef KERNEL_CPU_MTRR_H
#define KERNEL_CPU_MTRR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY 16u /**< Enabled variable ranges a snapshot holds. */

#define MEMORY_TYPE_RANGE_REGISTERS_UNCACHEABLE   0x00u   /**< Table 14-8's UC encoding. */
#define MEMORY_TYPE_RANGE_REGISTERS_WRITE_THROUGH 0x04u   /**< Table 14-8's WT encoding. */
#define MEMORY_TYPE_RANGE_REGISTERS_WRITE_BACK    0x06u   /**< Table 14-8's WB encoding. */
#define MEMORY_TYPE_RANGE_REGISTERS_TYPE_MASK     0xFFull /**< The type field, bits 7:0 of a base and of the default. */
#define MEMORY_TYPE_RANGE_REGISTERS_FIXED_ENABLED (1ull << 10) /**< IA32_MTRR_DEF_TYPE's FE flag. */
#define MEMORY_TYPE_RANGE_REGISTERS_ENABLED       (1ull << 11) /**< IA32_MTRR_DEF_TYPE's E flag. */
#define MEMORY_TYPE_RANGE_REGISTERS_RANGE_VALID   (1ull << 11) /**< IA32_MTRR_PHYSMASKn's V flag. */

/**
 * @brief One variable range: a pair IA32_MTRR_PHYSBASEn and IA32_MTRR_PHYSMASKn, as the processor holds them.
 */
typedef struct MemoryTypeRangeRegistersRange {
    uint64_t base; /**< PhysBase in bits MAXPHYADDR-1:12, the range's type in bits 7:0. */
    uint64_t mask; /**< PhysMask in bits MAXPHYADDR-1:12, the V flag in bit 11. */
} MemoryTypeRangeRegistersRange_t;

/**
 * @brief The memory type range registers that decide the type of memory past the first MiB: the default type and
 *        the enabled variable ranges.
 */
typedef struct MemoryTypeRangeRegisters {
    uint64_t default_type; /**< IA32_MTRR_DEF_TYPE: the default type, FE and E; 0, every range disabled, when the
                                processor has no such registers. */
    uint32_t range_count;  /**< Enabled variable ranges, those past the capacity counted but not held. */
    MemoryTypeRangeRegistersRange_t ranges[MEMORY_TYPE_RANGE_REGISTERS_RANGE_CAPACITY]; /**< The first ones held. */
} MemoryTypeRangeRegisters_t;

/**
 * @brief Says whether @p registers make the page at @p page write-back, by the precedences of Intel SDM vol. 3,
 *        §14.11.4.1.
 *
 * @details Write-back when the registers are enabled and every variable range that holds the page says
 *          write-back, or none holds it and the default type is write-back. Anything else is not: a range that
 *          says uncached, or any other type, over the page, disabled registers, ranges past the capacity, which
 *          could say uncached, and a page in the first MiB while the fixed ranges are enabled, since the snapshot
 *          does not hold them: the kernel's map of physical memory keeps that MiB, so no window asks.
 *
 * @param registers The registers, as memory_type_range_registers_initialize read them or a test made them up.
 * @param page      The physical address of the page; its offset within the page plays no part, since a range
 *                  is 4 KiB-aligned.
 * @return true when the page is write-back, false otherwise.
 */
bool memory_type_range_registers_make_write_back(const MemoryTypeRangeRegisters_t *registers, uint64_t page);

/**
 * @brief Reads the bootstrap processor's memory type range registers once, before any window is mapped.
 *
 * @details Without CPUID's MTRR flag, the snapshot holds disabled registers, and no page is write-back by them.
 *          §14.11.8 requires every processor to hold the same values, and the kernel loads none: it relies on the
 *          firmware for that (SeaBIOS writes them on every processor), so the bootstrap processor's stand for all.
 */
void memory_type_range_registers_initialize(void);

/**
 * @brief Says whether the registers memory_type_range_registers_initialize read make @p frame write-back.
 *
 * @param frame The physical address of the page.
 * @return memory_type_range_registers_make_write_back on that snapshot; false before it is read.
 */
bool memory_type_range_registers_frame_is_write_back(uint32_t frame);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_CPU_MTRR_H */
