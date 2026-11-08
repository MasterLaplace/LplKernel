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
 * @file interrupt_routing.h
 * @brief Where each PCI interrupt pin of a root bus goes: every PCI root bridge's _PRT evaluated, the interrupt
 *        links it names resolved, and the routes published as plain data.
 *
 * Layer L6 of the specification (docs/acpi/specification.md, §3.11): ordinary kernel code that asks the
 * interpreter to evaluate objects and reads plain results.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-08
 **************************************************************************/

#ifndef KERNEL_ACPI_INTERRUPT_ROUTING_H
#define KERNEL_ACPI_INTERRUPT_ROUTING_H

#include <kernel/acpi/initialization.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROUTES                                        \
    256u /**< Routes one boot publishes; the entries after them are counted and not read (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_LINKS                                         \
    64u /**< Interrupt links one boot resolves; an entry that names another is counted as unrouted (§6.1). */

/**
 * @brief Where one interrupt pin of a device on a root bus goes, as a _PRT entry and its link say.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInterruptRoute {
    uint32_t interrupt; /**< The input of the interrupt controller the pin reaches: a Global System Interrupt in the
                             APIC model, an ISA IRQ in the PIC model. */
    uint32_t link;      /**< The node of the interrupt link whose _CRS gave it, or 0 for a pin wired to it. */
    uint16_t segment;   /**< The root bridge's _SEG, 0 without one. */
    uint8_t bus;        /**< Its _BBN, 0 without one. */
    uint8_t device;     /**< The device number the entry's Address names, 0 to 31. */
    uint8_t pin;        /**< The pin: 0 to 3, INTA to INTD. */
    uint8_t level;      /**< 1 when level-triggered, 0 when edge-triggered. */
    uint8_t active_low; /**< 1 when active-low, 0 when active-high. */
} AdvancedConfigurationAndPowerInterfaceInterruptRoute_t;

/**
 * @brief The routes of one boot, and what reading them counted.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable {
    uint8_t model;              /**< The interrupt model step 5 told firmware: an
                                     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_* value. */
    uint32_t count;             /**< Routes published. */
    uint32_t bridges;           /**< PCI root bridges: Devices whose _HID or _CID names PNP0A03 or PNP0A08. */
    uint32_t bridges_absent;    /**< Of those, the ones whose _STA says neither present nor functioning, or fails:
                                     their _PRT is not read. */
    uint32_t bridges_unrouted;  /**< Of those present, the ones without a _PRT. */
    uint32_t tables_failed;     /**< _PRT evaluations that failed or gave no Package. */
    uint32_t entries;           /**< Elements of the _PRT packages read. */
    uint32_t entries_malformed; /**< Of those, the ones left out: no Package of four, an Address, a Pin or a
                                     SourceIndex that is no Integer, a device past 31, a pin past 3, or a Source that
                                     is neither an Integer nor a name that resolves. */
    uint32_t entries_dropped;   /**< Of those, the ones read once the table was full. */
    uint32_t links;             /**< Of those, the ones whose Source names an interrupt link. */
    uint32_t links_unrouted;    /**< Of those, the ones not published: a SourceIndex other than 0, a link _STA says
                                     neither present nor functioning, no current interrupt its _CRS gives that the
                                     first interrupt descriptor of its _PRS lists, or a link past the ones a boot
                                     resolves. */
    uint32_t links_failed;      /**< Of those, the ones whose link's _STA, _PRS or _CRS failed, or gave no Buffer. */
    AdvancedConfigurationAndPowerInterfaceInterruptRoute_t
        routes[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROUTES]; /**< The routes, in the order
                                                                                             their entries were
                                                                                             read. */
} AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t;

/**
 * @brief Publishes where each PCI interrupt pin of every root bus goes (§3.11): evaluates the _PRT of each PCI root
 *        bridge _STA keeps in, in declaration order, and reads each entry as ACPICA and Linux read it.
 *
 * @details A Source that is an Integer wires the pin to the interrupt its SourceIndex gives, level-triggered and
 *          active-low, as PCI interrupts are (Linux v6.17 acpi_pci_irq_enable). A Source that names an interrupt
 *          link gives the link's current interrupt, as Linux v6.17 reads it (acpi_pci_link_get_current,
 *          acpi_pci_link_allocate): the first interrupt of the first IRQ or Extended Interrupt descriptor of its
 *          _CRS that names one, kept only when the first such descriptor of its _PRS lists it, with that
 *          descriptor's trigger and polarity. Each link is read once. The tables an evaluation loads get their late
 *          initialisation as soon as it returns.
 *
 * @param load           A load whose namespace the initialisation has initialised (§3.8).
 * @param machine        The interrupt model step 5 told firmware.
 * @param initialization The counts the sequence keeps: the _STA evaluations and the failures, per status.
 * @param out_table      Receives the routes and the counts, the counts zeroed first; the routes past its count are
 *                       left as they were.
 */
extern void advanced_configuration_and_power_interface_interrupt_routing_publish(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t *machine,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *initialization,
    AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *out_table);

/**
 * @brief Finds where pin @p pin of device @p device on bus @p bus of segment @p segment goes: the first route the
 *        table holds for it, as Linux takes the first matching _PRT entry.
 *
 * @param table     A published table.
 * @param segment   The PCI segment group.
 * @param bus       The bus, a root bus.
 * @param device    The device number, 0 to 31.
 * @param pin       The pin: 0 to 3, INTA to INTD.
 * @param out_route Receives the route when there is one.
 * @return false when the table holds no route for that pin.
 */
extern bool advanced_configuration_and_power_interface_interrupt_routing_find(
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *table, uint16_t segment, uint8_t bus,
    uint8_t device, uint8_t pin, AdvancedConfigurationAndPowerInterfaceInterruptRoute_t *out_route);

/**
 * @brief Folds what @p table publishes with FNV-1a: its model and count, then each route's interrupt, segment, bus,
 *        device, pin, trigger and polarity, little-endian. The link's node is left out: a node index is no fixed
 *        identity of the firmware.
 *
 * @param table A published table.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_interrupt_routing_fold(
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *table);

/**
 * @brief The routes the boot published.
 *
 * @return The table, never NULL: no route until advanced_configuration_and_power_interface_machine_language_initialize
 *         runs, and when the boot's namespace could not be opened.
 */
extern const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *
advanced_configuration_and_power_interface_interrupt_routing(void);

#endif /* KERNEL_ACPI_INTERRUPT_ROUTING_H */
