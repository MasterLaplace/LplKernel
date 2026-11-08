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
#include <kernel/cpu/pci.h>
#include <kernel/cpu/pic.h>

#include <stdbool.h>
#include <stdint.h>

#define INPUT_OUTPUT_APIC_MAX_COUNT                                                                                    \
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_IOAPIC_COUNT /**< One unit for each I/O APIC the MADT keeps. */
#define INPUT_OUTPUT_APIC_MAX_ROUTES 2u
#define INPUT_OUTPUT_APIC_MAX_DEVICE_LINES                                                                             \
    8u /**< Lines of PCI functions one boot routes; a function past them keeps no line. */
#define INPUT_OUTPUT_APIC_NO_LINE 0xFFFFFFFFu /**< What a function routed through no line reports. */

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
 * line is not routed when no entry can name that ID: its entry is written
 * masked all the same, the refusal is counted, and the state is then
 * ioapic-init-bootstrap-processor-unnameable.
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
 *         @p apic_id, and the route stays where it was.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_set_isa_route_destination(uint8_t isa_irq,
                                                                                                 uint32_t apic_id);

/**
 * @brief Return how many lines the routing scaffold refused to route, at its last run, because no
 *        redirection entry can name the bootstrap processor.
 *
 * @details A physical destination holds 8 bits, and 0xFF in it is a broadcast (Intel SDM 325384-093US,
 *          §13.6.2.1): a processor whose APIC ID is 0xFF or more is reached only through interrupt
 *          remapping (#108).
 *
 * @return The lines refused. A move that
 *         input_output_advanced_programmable_interrupt_controller_set_isa_route_destination() refuses is
 *         not counted: its caller learns of it from the value returned.
 */
extern uint32_t input_output_advanced_programmable_interrupt_controller_get_lines_refused(void);

/**
 * @brief Read back whether the redirection entry an ISA line resolves to is masked, whether the line is
 *        routed or not.
 *
 * @details The tests read with it what a refused line leaves in its entry, which no route records.
 *
 * @param isa_irq The ISA IRQ line.
 * @param masked Receives 1 when the entry's mask bit is set, 0 when it is clear.
 * @return 1 when the line resolves to an entry of a mapped I/O APIC, 0 when it does not or when @p masked
 *         is NULL, @p masked then left alone.
 */
extern uint8_t input_output_advanced_programmable_interrupt_controller_debug_read_line_mask(uint8_t isa_irq,
                                                                                            uint8_t *masked);

/**
 * @brief Routes the interrupt pin of PCI function @p device through its I/O APIC line to the processor of logical slot
 *        @p slot with @p vector, and unmasks the line.
 *
 * @details The line is the Global System Interrupt the boot's published routes give the pin (ACPI specification,
 *          §3.11), programmed with their trigger and polarity, fixed delivery and a physical destination. It is
 *          programmed only once the boot told firmware the APIC model (§3.8 step 5): the routes then answer for the
 *          I/O APIC. The line is masked while its entry is written.
 *
 * @param device   The function, on a root bus of segment 0.
 * @param slot     The logical CPU slot whose local APIC the line interrupts.
 * @param vector   The vector, from device_interrupt_allocate.
 * @param out_line Receives the Global System Interrupt, or INPUT_OUTPUT_APIC_NO_LINE when this fails.
 * @return false, nothing written, when the function has no interrupt pin, the APIC model was not told, no route names
 *         its pin, its line lies on no mapped I/O APIC, an ISA route or another function's vector holds it, the
 *         lines are all taken, the slot holds no processor or no entry can name its APIC ID, or the platform cannot
 *         deliver a device interrupt (device_interrupt_platform_delivers).
 */
extern bool input_output_advanced_programmable_interrupt_controller_route_device(
    const PeripheralComponentInterconnectDevice_t *device, uint32_t slot, uint8_t vector, uint32_t *out_line);

/**
 * @brief Reads back the redirection entry of Global System Interrupt @p line, whether a route holds it or not.
 *
 * @details The tests read with it what route_device wrote.
 *
 * @param line     The Global System Interrupt.
 * @param out_low  Receives the entry's low register: vector, trigger, polarity, mask.
 * @param out_high Receives its high register: the destination in bits 24 to 31.
 * @return false, nothing written, when the line lies on no mapped I/O APIC.
 */
extern bool input_output_advanced_programmable_interrupt_controller_debug_read_line(uint32_t line, uint32_t *out_low,
                                                                                    uint32_t *out_high);

#endif /* KERNEL_CPU_INPUT_OUTPUT_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H */
