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
 * The ACPI tables and their readers hold no architecture: they reach the machine through a host
 * alone, a value they are given. kernel/arch/<arch>/cpu/acpi_host.c gives the machine's, in ring 0;
 * kernel/tests/acpi/simulated_host.c gives one that serves made-up and recorded firmware.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_ACPI_HOST_H
#define KERNEL_ACPI_HOST_H

#include <stdint.h>

/**
 * @brief What the ACPI layer asks of the machine it runs on.
 *
 * @details Each call is given back the context first. map_firmware maps the length bytes the
 *          firmware wrote at a physical address, cached and read-only, and returns their address,
 *          or NULL when the range cannot be mapped; unmap_firmware takes back what it returned,
 *          given the same length. allocate_pages gives count pages of 4 KiB, contiguous and
 *          writable, or NULL; release_pages takes them back, given the same count.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceHost {
    void *context;                                              /**< The host's own state, or NULL. */
    uint64_t (*root_system_description_pointer)(void *context); /**< The root pointer's physical address, or 0. */
    const void *(*map_firmware)(void *context, uint64_t physical_address, uint64_t length); /**< Maps firmware bytes. */
    void (*unmap_firmware)(void *context, const void *address, uint64_t length);            /**< Unmaps them. */
    void *(*allocate_pages)(void *context, uint32_t count);              /**< Gives pages of memory. */
    void (*release_pages)(void *context, void *address, uint32_t count); /**< Takes them back. */
} AdvancedConfigurationAndPowerInterfaceHost_t;

/**
 * @brief The host the boot reads this machine's firmware through.
 */
extern const AdvancedConfigurationAndPowerInterfaceHost_t advanced_configuration_and_power_interface_machine_host;

#endif /* KERNEL_ACPI_HOST_H */
