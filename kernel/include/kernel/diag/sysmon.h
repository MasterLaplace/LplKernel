/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
 * @file sysmon.h
 * @brief Real-time, purely visual system monitor of the client profile.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-07-01
 **************************************************************************/

#ifndef KERNEL_DIAG_SYSMON_H
#define KERNEL_DIAG_SYSMON_H

#include <kernel/drivers/serial.h>

/**
 * @brief Real-time, purely-visual system monitor for the client/graphics mode.
 *
 * A "graphical htop": instead of text it renders, every frame, a live 2D view
 * of what the machine is actually doing — physical-memory / buddy-allocator
 * occupancy, the kernel heap size-classes, the frame-arena / pool / stack
 * allocators, pinned (GPU) memory, the SPSC transfer ring, and the PCI bus —
 * as animated gauges, heatmaps and flowing data particles.
 *
 * It samples the kernel telemetry counters each frame, animates the deltas as
 * moving particles (so you literally see data move between subsystems), draws
 * to the linear framebuffer, and loops until a key is pressed. Replaces the old
 * static smoke_test_run_graphics_demo.
 *
 * Pure observability: it reads live, non-authoritative counters and is NOT part
 * of the deterministic engine contract.
 *
 * @param com1 Pointer to the primary serial interface (status logging).
 */
extern void kernel_sysmon_run(Serial_t *com1);

#endif /* KERNEL_DIAG_SYSMON_H */
