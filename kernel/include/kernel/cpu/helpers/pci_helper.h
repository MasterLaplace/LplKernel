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
