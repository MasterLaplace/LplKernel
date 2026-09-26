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
 * @file core_allocators_helper.h
 * @brief Serial reports of the core allocators and the ring buffer.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_MEMORY_CORE_ALLOCATORS_HELPER_H
#define KERNEL_MEMORY_CORE_ALLOCATORS_HELPER_H

#include <kernel/drivers/serial.h>
#include <stdbool.h>

extern void write_core_allocators_info(Serial_t *serial, bool frame_arena_ok, bool stack_allocator_ok,
                                       bool pool_allocator_ok, bool pinned_ok);
extern void write_ring_buffer_info(Serial_t *serial, bool ring_buffer_ok);

#endif /* KERNEL_MEMORY_CORE_ALLOCATORS_HELPER_H */
