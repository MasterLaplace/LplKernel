/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file xtensa_esp32.h
 * @brief Target declaration: xtensa on esp32.
 *
 * SCAFFOLD — declared, not implemented, and the one that does not fit.
 *
 * NO MEMORY MANAGEMENT UNIT. The classic ESP32 (Xtensa LX6) and the ESP32-C3
 * (RISC-V) both reach physical memory through a static, MMU-less mapping. That is
 * not a missing feature to stub around: this kernel is built on paging, links
 * higher-half, and enforces its read-only sections THROUGH page table entries. All
 * three are unavailable here, and the capability checks refuse to let this target
 * pretend otherwise.
 *
 * A port therefore means teaching the portable layer to run without paging. That is
 * the real work, and naming it is worth more than an empty directory that suggests
 * the job is nearly done.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-08-08
 **************************************************************************/

#ifndef KERNEL_ARCH_TARGETS_XTENSA_ESP32_H
#define KERNEL_ARCH_TARGETS_XTENSA_ESP32_H

#define KERNEL_ARCH_NAME          "xtensa"
#define KERNEL_ARCH_PLATFORM_NAME "esp32"

#define KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT              0
#define KERNEL_ARCH_HAS_PAGING                              0
#define KERNEL_ARCH_HAS_HIGHER_HALF                         0
#define KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT           0
#define KERNEL_ARCH_HAS_SYMMETRIC_MULTIPROCESSING           1
#define KERNEL_ARCH_HAS_FLOATING_POINT_UNIT                 1
#define KERNEL_ARCH_HAS_SINGLE_INSTRUCTION_MULTIPLE_DATA    0
#define KERNEL_ARCH_HAS_CACHE_COHERENT_DIRECT_MEMORY_ACCESS 0
#define KERNEL_ARCH_HAS_PORT_INPUT_OUTPUT                   0
#define KERNEL_ARCH_HAS_PERIPHERAL_COMPONENT_INTERCONNECT   0
#define KERNEL_ARCH_HAS_FIRMWARE_TABLES                     0
#define KERNEL_ARCH_HAS_DEVICE_TREE                         0

#define KERNEL_ARCH_POINTER_WIDTH_BITS          32
#define KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS 32
#define KERNEL_ARCH_PAGE_SIZE_BYTES             0

#endif /* KERNEL_ARCH_TARGETS_XTENSA_ESP32_H */
