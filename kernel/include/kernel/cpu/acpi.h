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
 * @file acpi.h
 * @brief Where the BIOS puts the root system description pointer.
 *
 * The tables it leads to are read by the architecture-free code of kernel/acpi/tables.h.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-09
 **************************************************************************/

#ifndef KERNEL_CPU_ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_H
#define KERNEL_CPU_ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_H

#include <stdint.h>

/**
 * @brief Finds the root system description pointer: in the first KiB of the EBDA, then in the BIOS
 *        area from E0000h to FFFFFh.
 *
 * @return The physical address of the first one whose checksums hold, or 0.
 */
extern uint32_t advanced_configuration_and_power_interface_find_root_system_description_pointer(void);

#endif /* KERNEL_CPU_ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_H */
