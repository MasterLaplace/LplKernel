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
 * @file simulated_host.h
 * @brief A host whose firmware is bytes placed at physical addresses of its own.
 *
 * The ACPI tests read made-up firmware through it in ring 0 and on the host alike, and the host
 * harness reads the firmware of recorded machines through it, so that both sides run the same
 * code on the same bytes.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-24
 **************************************************************************/

#ifndef KERNEL_TESTS_ACPI_SIMULATED_HOST_H
#define KERNEL_TESTS_ACPI_SIMULATED_HOST_H

#include <kernel/acpi/host.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES                                           \
    128u /**< Ranges one simulated host holds: a recorded machine's tables, each placed once. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MAPS                                             \
    128u /**< Maps one simulated host keeps open at once: a full directory, and the one being opened. */

/**
 * @brief Bytes a simulated host serves at a physical address.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedRange {
    uint64_t physical_address; /**< Physical address of the first byte. */
    uint64_t length;           /**< Length in bytes. */
    const uint8_t *bytes;      /**< The bytes, which the caller owns and keeps until the host is no longer used. */
} AdvancedConfigurationAndPowerInterfaceSimulatedRange_t;

/**
 * @brief A map a simulated host gave and that has not been unmapped.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedMap {
    const void *address; /**< What map_firmware returned. */
    uint64_t length;     /**< The length it was given. */
} AdvancedConfigurationAndPowerInterfaceSimulatedMap_t;

/**
 * @brief A host whose physical memory is the ranges placed in it: a map succeeds when one range
 *        holds every byte asked for, and an unmap must name an open map by its address and length.
 *
 * @details It points at itself through host.context, so it is used where it was opened and never
 *          copied.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceSimulatedHost {
    AdvancedConfigurationAndPowerInterfaceHost_t host; /**< What the ACPI layer is given. */
    uint64_t root_system_description_pointer;          /**< What the host gives as the root pointer, or 0. */
    uint32_t outstanding_maps;                         /**< Maps not unmapped yet, each kept in maps. */
    uint32_t mismatched_unmaps;                        /**< Unmaps whose address and length name no open map. */
    uint32_t range_count;                              /**< Ranges placed. */
    uint32_t outstanding_pages;                        /**< Pages given and not taken back yet. */
    AdvancedConfigurationAndPowerInterfaceSimulatedRange_t
        ranges[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_RANGES]; /**< In the order placed. */
    AdvancedConfigurationAndPowerInterfaceSimulatedMap_t
        maps[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_SIMULATED_HOST_MAX_MAPS]; /**< The open maps, in no order. */
} AdvancedConfigurationAndPowerInterfaceSimulatedHost_t;

/**
 * @brief Empties a simulated host: no range, no root pointer, nothing mapped, no page given. Its pages
 *        come from kernel_vmm_alloc_pages, which the host harness gives too.
 *
 * @param simulated The host, which must stay where it is from now on.
 */
extern void advanced_configuration_and_power_interface_simulated_host_open(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated);

/**
 * @brief Serves @p length bytes at @p physical_address.
 *
 * @param simulated        The host.
 * @param physical_address Physical address of the first byte.
 * @param bytes            The bytes, kept by the caller as long as the host is used.
 * @param length           Length in bytes, at least 1.
 * @return false when the range is empty, wraps, overlaps one already placed, or the host is full.
 */
extern bool advanced_configuration_and_power_interface_simulated_host_place(
    AdvancedConfigurationAndPowerInterfaceSimulatedHost_t *simulated, uint64_t physical_address, const void *bytes,
    uint64_t length);

#endif /* KERNEL_TESTS_ACPI_SIMULATED_HOST_H */
