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
 * @file ioapic_helper.h
 * @brief Serial reports of the I/O APIC routes.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_CPU_IOAPIC_HELPER_H
#define KERNEL_CPU_IOAPIC_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_ioapic_scaffold_info(Serial_t *serial);
extern void write_ioapic_routes_info(Serial_t *serial);

#endif /* KERNEL_CPU_IOAPIC_HELPER_H */
