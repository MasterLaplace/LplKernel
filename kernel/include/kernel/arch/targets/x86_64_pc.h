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
 * @file x86_64_pc.h
 * @brief Target declaration: x86_64 on pc.
 *
 * SCAFFOLD — declared, not implemented.
 *
 * The nearest port by a wide margin: the platform is the same PC, so the platform half
 * of `kernel/arch/i386` (9728 lines) is reused verbatim and only the 32-bit mode layer
 * is rewritten — measured at 1643 lines. Protection keys, which chapter 10 §10.3 builds SASOS on, exist ONLY here:
 * the key bits are 59-62 of a page table entry, and a 32-bit entry has no room for
 * them. That is why a third of chapter 10 is unreachable until this target exists.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-08-08
 **************************************************************************/

#ifndef KERNEL_ARCH_TARGETS_X86_64_PC_H
#define KERNEL_ARCH_TARGETS_X86_64_PC_H

#define KERNEL_ARCH_NAME          "x86_64"
#define KERNEL_ARCH_PLATFORM_NAME "pc"

#define KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT              1
#define KERNEL_ARCH_HAS_PAGING                              1
#define KERNEL_ARCH_HAS_HIGHER_HALF                         1
#define KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT           1
#define KERNEL_ARCH_HAS_SYMMETRIC_MULTIPROCESSING           1
#define KERNEL_ARCH_HAS_FLOATING_POINT_UNIT                 1
#define KERNEL_ARCH_HAS_SINGLE_INSTRUCTION_MULTIPLE_DATA    1
#define KERNEL_ARCH_HAS_CACHE_COHERENT_DIRECT_MEMORY_ACCESS 1
#define KERNEL_ARCH_HAS_PORT_INPUT_OUTPUT                   1
#define KERNEL_ARCH_HAS_PERIPHERAL_COMPONENT_INTERCONNECT   1
#define KERNEL_ARCH_HAS_FIRMWARE_TABLES                     1
#define KERNEL_ARCH_HAS_DEVICE_TREE                         0

#define KERNEL_ARCH_POINTER_WIDTH_BITS          64
#define KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS 52
#define KERNEL_ARCH_PAGE_SIZE_BYTES             4096

#endif /* KERNEL_ARCH_TARGETS_X86_64_PC_H */
