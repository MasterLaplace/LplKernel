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
 * @file cpu_topology.h
 * @brief Minimal CPU topology abstraction for allocator domain routing.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_CPU_TOPOLOGY_H
#define KERNEL_CPU_TOPOLOGY_H

#include <kernel/lib/asmutils.h>

#include <stdbool.h>
#include <stdint.h>

#define CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC 32u
#define CPU_TOPOLOGY_NO_PROCESSOR            0xFFFFFFFFu /**< The identifier of an empty slot: none, x2APIC included. */
#define CPU_TOPOLOGY_NO_SLOT                 0xFFFFFFFFu /**< The slot of an identifier the topology does not hold. */

/**
 * @brief Everything the topology holds, so a check that rewrites it can put it back.
 *
 * @details The topology is boot state other subsystems rely on: which APs exist, which are online,
 *          which domain a slot belongs to. A smoke that registers made-up APIC IDs or rebuilds the
 *          table from CPUID alone leaves a kernel that has forgotten its own APs.
 */
typedef struct {
    uint8_t initialized;                                            /**< Initialization flag. */
    uint8_t forced_slot_enabled;                                    /**< Whether a slot is forced. */
    uint32_t forced_slot;                                           /**< The forced slot. */
    uint8_t apic_id_valid;                                          /**< Whether CPUID gave an APIC ID. */
    uint32_t local_apic_id;                                         /**< The identifier recorded at boot. */
    uint32_t apic_id_to_slot[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC]; /**< APIC ID held by each slot. */
    uint32_t discovered_cpu_count;                                  /**< Slots in use. */
    uint8_t online_by_slot[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];   /**< Online mark of each slot. */
    uint32_t online_cpu_count;                                      /**< Slots marked online. */
    uint32_t slot_domain[CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC];     /**< Domain of each slot. */
    const char *source_name;                                        /**< Where the topology came from. */
} CpuTopologySnapshot_t;

/**
 * @brief Initialize CPU topology subsystem and discover APIC IDs.
 *
 */
extern void cpu_topology_initialize(void);

/**
 * @brief The logical slot of the processor that runs the call: the one its own APIC ID register names.
 *
 * @details Before the local APICs run, only the bootstrap processor runs, and its slot is the one of the identifier
 *          recorded at boot. A slot forced by cpu_topology_debug_force_logical_slot wins over both. The heap's local
 *          domain and the local NUMA node follow it.
 *
 * @return The slot, 0 when CPUID reports no local APIC, or CPU_TOPOLOGY_NO_SLOT when the topology holds no slot for
 *         the processor's identifier.
 */
extern uint32_t cpu_topology_get_logical_slot(void);

/**
 * @brief Register a discovered APIC ID and get its logical slot.
 *
 * Called during topology discovery to map APIC IDs to logical slots.
 *
 * @param apic_id Local APIC ID or x2APIC ID to register, kept whole.
 * @return Logical slot index assigned to this APIC ID, or CPU_TOPOLOGY_NO_SLOT once every slot
 *         holds another.
 */
extern uint32_t cpu_topology_register_discovered_apic_id(uint32_t apic_id);

/**
 * @brief The APIC ID the topology recorded at boot: the bootstrap processor's, from CPUID, whichever processor calls.
 *
 * @return The identifier, or 0 when CPUID reports no local APIC.
 */
extern uint32_t cpu_topology_get_local_apic_id(void);

/**
 * @brief Replace the identifier the topology recorded at boot, and register it.
 *
 * @details Checks call it to give the bootstrap processor an identifier the machine lacks. The slot of the processor
 *          that runs, cpu_topology_get_logical_slot, does not follow it.
 *
 * @param apic_id Local APIC ID or x2APIC ID, kept whole.
 */
extern void cpu_topology_set_runtime_local_apic_id(uint32_t apic_id);

/**
 * @brief Mark a CPU with the given APIC ID as online.
 *
 * @param apic_id  APIC ID of the CPU to mark online.
 */
extern void cpu_topology_mark_apic_id_online(uint32_t apic_id);

/**
 * @brief Mark a CPU with the given APIC ID as offline (inverse of mark online).
 *
 * Clears the online flag for the resolved logical slot and decrements the
 * online CPU count when it was set. Used to restore state (e.g. by tests
 * that temporarily register a synthetic CPU) so global topology bookkeeping is
 * never left corrupted.
 *
 * @param apic_id  APIC ID of the CPU to mark offline.
 */
extern void cpu_topology_unmark_apic_id_online(uint32_t apic_id);

/**
 * @brief Mark online the processor of the identifier recorded at boot: the bootstrap processor.
 */
extern void cpu_topology_mark_runtime_cpu_online(void);

/**
 * @brief Get the number of CPUs currently marked as online.
 *
 * @return Count of online CPUs.
 */
extern uint32_t cpu_topology_get_online_cpu_count(void);

/**
 * @brief Check if a logical CPU slot is marked as online.
 *
 * @param slot Logical CPU slot index to check.
 * @return true if the slot is online, false otherwise.
 */
extern bool cpu_topology_is_logical_slot_online(uint32_t slot);

/**
 * @brief Bind a logical CPU slot to an allocator domain.
 *
 * This is used to route allocations to specific CPUs based on their logical slots.
 *
 * @param slot Logical CPU slot index to bind.
 * @param domain Allocator domain index to bind to this slot.
 */
extern void cpu_topology_bind_slot_to_domain(uint32_t slot, uint32_t domain);

/**
 * @brief Get the allocator domain bound to a logical CPU slot.
 *
 * @param slot Logical CPU slot index to query.
 * @return Allocator domain index bound to this slot, or 0 if not bound.
 */
extern uint32_t cpu_topology_get_slot_domain(uint32_t slot);

/**
 * @brief Get the APIC ID registered for a logical CPU slot.
 *
 * @param slot Logical CPU slot index to query.
 * @return APIC ID registered for this slot, or CPU_TOPOLOGY_NO_PROCESSOR when the slot is empty or
 *         past the last.
 */
extern uint32_t cpu_topology_get_apic_id_at_slot(uint32_t slot);

/**
 * @brief Get the logical CPU slot associated with a given APIC ID.
 *
 * @param apic_id APIC ID to query.
 * @return Logical slot index, or CPU_TOPOLOGY_NO_SLOT if the APIC ID is unknown.
 */
extern uint32_t cpu_topology_get_slot_for_apic_id(uint32_t apic_id);

/**
 * @brief Get the total number of CPUs discovered during topology initialization.
 *
 * @return Count of discovered CPUs (including offline).
 */
extern uint32_t cpu_topology_get_discovered_cpu_count(void);

/**
 * @brief The slots the topology gives @p processors processors: one each, up to its capacity.
 *
 * @param processors How many processors the firmware enables.
 * @return @p processors, or CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC when there are more.
 */
extern uint32_t cpu_topology_slots_for(uint32_t processors);

/**
 * @brief Get a human-readable name of the topology discovery source.
 *
 * This indicates how the CPU topology was discovered (e.g., "cpuid-apic-id",
 * "madt", "forced-slot", etc.) and is useful for diagnostics.
 *
 * @return String name of the topology discovery source.
 */
extern const char *cpu_topology_get_source_name(void);

/**
 * @brief Debug function to force a logical slot assignment.
 *
 * @param slot Logical CPU slot index to force.
 * @return true if the slot was successfully forced, false otherwise.
 */
extern bool cpu_topology_debug_force_logical_slot(uint32_t slot);

/**
 * @brief Debug function to reset topology discovery.
 */
extern void cpu_topology_debug_reset_discovery(void);

/**
 * @brief Copies everything the topology holds.
 *
 * @param snapshot Receives the copy.
 */
extern void cpu_topology_debug_save(CpuTopologySnapshot_t *snapshot);

/**
 * @brief Puts the topology back exactly as a cpu_topology_debug_save() found it.
 *
 * @param snapshot The copy to restore.
 */
extern void cpu_topology_debug_restore(const CpuTopologySnapshot_t *snapshot);

/**
 * @brief Debug function to clear the forced logical slot.
 */
extern void cpu_topology_debug_clear_forced_logical_slot(void);

/**
 * @brief Check if topology discovery is in forced slot mode.
 *
 * @return true if a logical slot is being forced, false otherwise.
 */
extern uint8_t cpu_topology_is_forced(void);

#endif /* KERNEL_CPU_TOPOLOGY_H */
