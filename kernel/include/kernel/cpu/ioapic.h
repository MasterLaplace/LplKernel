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
 * @file ioapic.h
 * @brief I/O APIC discovery and interrupt routing.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-09
 **************************************************************************/

#ifndef KERNEL_CPU_INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H
#define KERNEL_CPU_INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H

#include <kernel/acpi/tables.h>
#include <kernel/cpu/paging.h>
#include <kernel/cpu/pic.h>

#include <stdint.h>

#define INPUT_OUTPUT_APIC_MAX_COUNT  8u
#define INPUT_OUTPUT_APIC_MAX_ROUTES 2u

typedef struct {
    uint8_t id;
    uint32_t physical_base;
    uint32_t virtual_base;
    uint32_t gsi_base;
    uint32_t redirection_entry_count;
    uint8_t mapped;
} InputOutputApicUnit_t;

typedef struct {
    uint8_t isa_irq;
    uint32_t global_system_interrupt;
    uint8_t vector;
    uint8_t io_apic_index;
    uint8_t io_apic_id;
    uint8_t masked;
    uint16_t iso_flags;
} InputOutputApicRouteInfo_t;

/**
 * @brief Prepare IOAPIC MMIO access and program masked ISA route entries.
 *
 * Current scope programs discovery-backed redirection entries for IRQ0/IRQ1
 * in masked mode only, preserving active interrupt ownership behavior. Each
 * entry names the bootstrap processor's APIC ID, as the topology gives it: a
 * line is not routed when no entry can name that ID, the refusal is counted,
 * and the state is then ioapic-init-bootstrap-processor-unnameable.
 */
extern void input_output_advanced_programmable_interrupt_controller_initialize_routing_scaffold(void);

/**
 * @brief Return current IOAPIC scaffold state name.
 */
extern const char *input_output_advanced_programmable_interrupt_controller_get_state_name(void);

/**
 * @brief Return number of IOAPIC MMIO windows mapped by scaffold.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_get_mapped_count(void);

/**
 * @brief Return number of ISA route entries programmed by scaffold.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_count(void);

/**
 * @brief Return programmed ISA IRQ line for route index.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_irq(uint8_t route_index);

/**
 * @brief Return programmed GSI for route index.
 */
extern uint32_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_gsi(uint8_t route_index);

/**
 * @brief Return programmed IDT vector for route index.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_get_programmed_route_vector(uint8_t route_index);

/**
 * @brief Return route index IOAPIC source index.
 */
extern uint8_t
input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_index(uint8_t route_index);

/**
 * @brief Return route index IOAPIC hardware id.
 */
extern uint8_t
input_output_advanced_programmable_interrupt_controller_get_programmed_route_io_apic_id(uint8_t route_index);

/**
 * @brief Return non-zero when programmed route remains masked.
 */
extern uint8_t
input_output_advanced_programmable_interrupt_controller_get_programmed_route_is_masked(uint8_t route_index);

/**
 * @brief Return raw MADT ISO flags consumed for the programmed route.
 */
extern uint16_t
input_output_advanced_programmable_interrupt_controller_get_programmed_route_iso_flags(uint8_t route_index);

/**
 * @brief Unmask a previously programmed ISA route in IOAPIC redirection table.
 *
 * This routine only flips the IOAPIC mask bit for the selected ISA route and
 * keeps vector/polarity/trigger fields unchanged.
 *
 * @return non-zero when route is active (unmasked), zero on failure.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_enable_isa_route(uint8_t isa_irq);

/**
 * @brief Read back the destination APIC ID a programmed route's redirection entry holds.
 *
 * @param route_index The route, below the programmed route count.
 * @return The 8-bit destination field of the route's entry, as its I/O APIC holds it, or 0 when
 *         @p route_index names no programmed route.
 */
extern uint8_t
input_output_advanced_programmable_interrupt_controller_get_programmed_route_destination_apic_id(uint8_t route_index);

/**
 * @brief Set the destination APIC ID for a previously programmed ISA route.
 *
 * @param isa_irq The ISA IRQ number.
 * @param apic_id The target CPU's local APIC ID or x2APIC ID, kept whole.
 * @return non-zero on success, zero on failure: the route is missing, or no redirection entry can name
 *         @p apic_id, which is then counted and leaves the route where it was.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_set_isa_route_destination(uint8_t isa_irq,
                                                                                                 uint32_t apic_id);

/**
 * @brief Return how many destinations were refused since the routes were last initialized, because a
 *        redirection entry cannot name them.
 *
 * @details A physical destination holds 8 bits, and 0xFF in it is a broadcast (Intel SDM 325384-093US,
 *          §13.6.2.1): a processor whose APIC ID is 0xFF or more is reached only through interrupt
 *          remapping (#108).
 *
 * @return The lines the routing scaffold refused to route to the bootstrap processor, plus the moves
 *         input_output_advanced_programmable_interrupt_controller_set_isa_route_destination() refused.
 */
extern uint32_t input_output_advanced_programmable_interrupt_controller_get_destinations_refused(void);

#endif /* KERNEL_CPU_INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H */
