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
 * @file host.h
 * @brief What the ACPI layer asks of the machine it runs on.
 *
 * The ACPI tables and their readers hold no architecture: they reach the machine through these
 * calls alone. kernel/arch/<arch>/cpu/acpi_host.c answers them in ring 0.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_HOST_H
#define KERNEL_ACPI_HOST_H

#include <stdint.h>

/**
 * @brief The root system description pointer the platform gives.
 *
 * @return Its physical address, or 0 when the platform gives none.
 */
extern uint64_t advanced_configuration_and_power_interface_host_root_system_description_pointer(void);

/**
 * @brief Maps memory the firmware wrote, cached and read-only.
 *
 * @param physical_address Physical address of the first byte.
 * @param length           Length in bytes.
 * @return The address of @p physical_address, or NULL when the range cannot be mapped.
 */
extern const void *advanced_configuration_and_power_interface_host_map_firmware(uint64_t physical_address,
                                                                                uint64_t length);

/**
 * @brief Unmaps a range advanced_configuration_and_power_interface_host_map_firmware mapped.
 *
 * @param address The address it returned.
 * @param length  The length it was given.
 */
extern void advanced_configuration_and_power_interface_host_unmap_firmware(const void *address, uint64_t length);

#endif /* KERNEL_ACPI_HOST_H */
