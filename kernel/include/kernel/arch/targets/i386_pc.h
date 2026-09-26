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
 * @file i386_pc.h
 * @brief Target declaration: i686 on pc.
 *
 * The only target that builds today, and the reference the arch contract was
 * derived from. Everything below is measured on the running kernel rather than
 * asserted: CR0.WP is read from the register, the 4 KiB page size is what the boot
 * page tables install, and the 32-bit physical width is why the HAL address fields
 * are u32 — no PAE, so a frame number never reaches past 4 GiB.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-08-08
 **************************************************************************/

#ifndef KERNEL_ARCH_TARGETS_I386_PC_H
#define KERNEL_ARCH_TARGETS_I386_PC_H

#define KERNEL_ARCH_NAME          "i686"
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

#define KERNEL_ARCH_POINTER_WIDTH_BITS          32
#define KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS 32
#define KERNEL_ARCH_PAGE_SIZE_BYTES             4096

#endif /* KERNEL_ARCH_TARGETS_I386_PC_H */
