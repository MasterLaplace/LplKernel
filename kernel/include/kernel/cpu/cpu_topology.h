/*
** LplKernel
** kernel/include/kernel/cpu/cpu_topology.h
**
** Minimal CPU topology abstraction for allocator domain routing.
*/

#ifndef CPU_TOPOLOGY_H_
#define CPU_TOPOLOGY_H_

#include <stdbool.h>
#include <stdint.h>

#define CPU_TOPOLOGY_MAX_LOGICAL_CPUS_PUBLIC 32u

/**
 * @brief Initialize CPU topology subsystem and discover APIC IDs.
 *
 */
extern void cpu_topology_initialize(void);

/**
 * @brief Get logical CPU slot for the current processor.
 *
 * Returns a compacted logical slot index (0 to CPU_TOPOLOGY_MAX_LOGICAL_CPUS-1)
 * corresponding to the current CPU. This is used for allocator domain routing.
 *
 * If topology discovery failed, returns 0 (BSP slot). If forced slot mode is
 * enabled, returns the forced slot instead.
 */
extern uint32_t cpu_topology_get_logical_slot(void);

/**
 * @brief Register a discovered APIC ID and get its logical slot.
 *
 * Called during topology discovery to map APIC IDs to logical slots.
 *
 * @param apic_id APIC ID to register (0-255).
 * @return Logical slot index assigned to this APIC ID, or 0 if registration failed.
 */
extern uint32_t cpu_topology_register_discovered_apic_id(uint32_t apic_id);

/**
 * @brief Get the local APIC ID of the current processor.
 *
 * Returns the APIC ID registered for the current CPU, or 0 if not available.
 */
extern uint32_t cpu_topology_get_local_apic_id(void);

/**
 * @brief Set the local APIC ID for the current processor at runtime.
 *
 * This is used by APs during their startup sequence to register their APIC ID
 * when topology discovery is already complete. It also marks the APIC ID as
 * valid and online.
 *
 * @param apic_id APIC ID to set for the current CPU (0-255).
 */
extern void cpu_topology_set_runtime_local_apic_id(uint32_t apic_id);

/**
 * @brief Mark a CPU with the given APIC ID as online.
 *
 * @param apic_id  APIC ID of the CPU to mark online.
 */
extern void cpu_topology_mark_apic_id_online(uint32_t apic_id);

/**
 * @brief Mark the current CPU as online using its runtime APIC ID.
 *
 * This is used by APs during their startup sequence after setting their runtime APIC ID.
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
 * @return APIC ID registered for this slot, or 0xFF if invalid.
 */
extern uint32_t cpu_topology_get_apic_id_at_slot(uint32_t slot);

/**
 * @brief Get the total number of CPUs discovered during topology initialization.
 *
 * @return Count of discovered CPUs (including offline).
 */
extern uint32_t cpu_topology_get_discovered_cpu_count(void);

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
 * @brief Debug function to clear the forced logical slot.
 */
extern void cpu_topology_debug_clear_forced_logical_slot(void);

/**
 * @brief Check if topology discovery is in forced slot mode.
 *
 * @return true if a logical slot is being forced, false otherwise.
 */
extern uint8_t cpu_topology_is_forced(void);

#endif /* !CPU_TOPOLOGY_H_ */
