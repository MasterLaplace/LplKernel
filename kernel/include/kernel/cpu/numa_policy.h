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
 * @file numa_policy.h
 * @brief NUMA placement policy for allocator domains.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-19
 **************************************************************************/

#ifndef KERNEL_CPU_NUMA_POLICY_H
#define KERNEL_CPU_NUMA_POLICY_H

#include <kernel/cpu/acpi.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/drivers/serial.h>

#include <stdint.h>

#define NUMA_POLICY_MAX_NODES 8u

typedef struct {
    uint32_t node_id;
    uint32_t memory_ranges;
    uint32_t cpus_bound;
} NumaNodeInfo_t;

/**
 * @brief Initialize the NUMA policy.
 */
extern void numa_policy_initialize(void);

/**
 * @brief Get the NUMA node ID for a given CPU slot.
 *
 * @param slot CPU slot number to query.
 * @return NUMA node ID associated with the slot, or 0 if out of range.
 */
extern uint32_t numa_policy_get_node_for_slot(uint32_t slot);

/**
 * @brief Get the NUMA node ID local to the currently executing CPU.
 *
 * @return NUMA node ID local to the current CPU.
 */
extern uint32_t numa_policy_get_local_node(void);

/**
 * @brief Get the total number of NUMA nodes detected in the system.
 *
 * @return Total count of NUMA nodes.
 */
extern uint32_t numa_policy_get_node_count(void);

/**
 * @brief Get the NUMA node ID for a given physical address.
 *
 * @param phys_addr Physical address to query.
 * @return NUMA node ID associated with the address, or 0 if not found.
 */
extern uint32_t numa_policy_get_node_for_address(uint32_t phys_addr);

#endif /* KERNEL_CPU_NUMA_POLICY_H */
