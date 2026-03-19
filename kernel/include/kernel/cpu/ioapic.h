/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** ioapic
*/

#ifndef IOAPIC_H_
#define IOAPIC_H_

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
 * in masked mode only, preserving active interrupt ownership behavior.
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

#endif /* !IOAPIC_H_ */
