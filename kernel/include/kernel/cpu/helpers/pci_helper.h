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
 * @file pci_helper.h
 * @brief Serial report of the PCI enumeration and its base address registers.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-06-23
 **************************************************************************/

#ifndef KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_H
#define KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_H

#include <kernel/drivers/serial.h>

/**
 * @brief Print every PCI device recorded by the last enumeration scan to serial.
 *
 * Call peripheral_component_interconnect_scan() first.
 */
extern void write_peripheral_component_interconnect_info(Serial_t *serial);

#endif /* KERNEL_CPU_PERIPHERAL_COMPONENT_INTERCONNECT_HELPER_H */
