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
 * @brief Where each PCI interrupt pin goes: the _PRT of every PCI root bridge and of every bridge below one
 *        evaluated, the interrupt links they name resolved, and the routes published as plain data.
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
    64u /**< Interrupt links one boot resolves; an entry that names another is counted as dropped (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_BRIDGES                                       \
    64u /**< Bridges below a root bridge one boot publishes; one past them is counted as dropped (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_DEPTH                                         \
    8u /**< Bridges between a root bridge and the deepest one published, itself included (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_CHILDREN                                      \
    256u /**< Devices of one bridge whose _ADR names a PCI function compared to choose its bridges, one for each       \
              function of a bus (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_NO_INTERRUPT                                      \
    0xFFFFFFFFu /**< The interrupt of an entry whose link gives none: a lookup that finds it first finds no route. */

/**
 * @brief Where one interrupt pin of a device goes, as a _PRT entry and its link say.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInterruptRoute {
    uint32_t interrupt; /**< The input of the interrupt controller the pin reaches: a Global System Interrupt in the
                             APIC model, an ISA IRQ in the PIC model;
                             ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_NO_INTERRUPT when the entry
                             names a link that gives none. */
    uint32_t link;      /**< The node of the Device the entry's Source names, or 0 for a pin wired to it. */
    uint16_t segment;   /**< The root bridge's _SEG, 0 without one. */
    uint16_t bridge;    /**< The bridge below the root bridge whose _PRT holds the entry, counted from 1 in the table's
                             below; 0 for the root bridge's own _PRT, whose device sits on the root bus. */
    uint8_t bus;        /**< The root bridge's _BBN, 0 without one. */
    uint8_t device;     /**< The device number the entry's Address names, 0 to 31. */
    uint8_t pin;        /**< The pin: 0 to 3, INTA to INTD. */
    uint8_t level;      /**< 1 when level-triggered, 0 when edge-triggered, or without an interrupt. */
    uint8_t active_low; /**< 1 when active-low, 0 when active-high, or without an interrupt. */
} AdvancedConfigurationAndPowerInterfaceInterruptRoute_t;

/**
 * @brief A Device below a root bridge that answers, by its _ADR, for a PCI-to-PCI bridge: the ACPI companion Linux
 *        v6.17 gives that bridge (acpi_pci_find_companion), whose _PRT routes the devices of its secondary bus.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInterruptRoutingBridge {
    uint32_t node;    /**< The Device. */
    uint32_t address; /**< The PCI bridge its _ADR names, on the bus of the bridge above it: its device in bits 16 to
                           20, its function in bits 0 to 2. */
    uint16_t segment; /**< Its root bridge's _SEG. */
    uint16_t above;   /**< The bridge it sits below, counted from 1 in the table's below; 0 when it sits right below
                           its root bridge, on the root bus. */
    uint8_t bus;      /**< Its root bridge's _BBN. */
} AdvancedConfigurationAndPowerInterfaceInterruptRoutingBridge_t;

/**
 * @brief A bridge between a PCI function and its root bus, as a lookup goes through it.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInterruptRoutingHop {
    uint8_t device;   /**< The bridge's device, on the bus above it. */
    uint8_t function; /**< Its function. */
    bool cardbus;     /**< A CardBus bridge, whose own pin every function behind it takes, as Linux v6.17 takes it
                           (acpi_pci_irq_lookup); false for a PCI-to-PCI bridge, which swizzles theirs. */
    uint8_t pin;      /**< A CardBus bridge's Interrupt Pin, 1 to 4 for INTA to INTD; 0, none, or a value past 4, which
                           the PCI specification reserves, gives no route. Unread for a PCI-to-PCI bridge. */
} AdvancedConfigurationAndPowerInterfaceInterruptRoutingHop_t;

/**
 * @brief The routes of one boot, and what reading them counted.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable {
    uint8_t model;              /**< The interrupt model firmware's _PRT answers for: an
                                     ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_* value, the one step
                                     5 told firmware, unknown when \_PIC (1) failed. */
    uint32_t count;             /**< Entries published, with an interrupt or without one. */
    uint32_t bridges;           /**< PCI root bridges: Devices whose _HID or _CID names PNP0A03. */
    uint32_t bridges_absent;    /**< Of those, the ones that _STA, theirs or a Device's above them, says neither present
                                     nor functioning, or fails on: their _PRT is not read. */
    uint32_t bridges_failed;    /**< Of those present, the ones whose _SEG or _BBN failed: their _PRT is not read. */
    uint32_t bridges_unrouted;  /**< Of those present, the ones without a _PRT. */
    uint32_t tables_failed;     /**< _PRT evaluations that failed or gave no Package. */
    uint32_t entries;           /**< Elements of the _PRT packages read. */
    uint32_t entries_malformed; /**< Of those, the ones left out: no Package of four, an Address or a Pin that is no
                                     Integer, no Integer SourceIndex either way round, a device past 31, a pin past 3,
                                     or a Source that is neither an Integer nor a name that resolves to a Device. */
    uint32_t entries_repaired;  /**< Of those, the ones whose Source and SourceIndex were read swapped, as ACPICA
                                     repairs an entry whose SourceIndex is no Integer. */
    uint32_t entries_dropped;   /**< Of those, the ones read once the table was full. */
    uint32_t links;             /**< Entries published whose Source names a Device. */
    uint32_t links_unrouted;    /**< Of those, the ones without an interrupt: a SourceIndex other than 0, a Device
                                     that is no link, a link _STA says neither present nor functioning, or no current
                                     interrupt its _CRS gives that the first interrupt descriptor of its _PRS lists. */
    uint32_t links_failed;      /**< Of those, the ones whose link's _STA, _PRS or _CRS failed, gave no Buffer, or gave
                                     a resource template ACPICA refuses. */
    uint32_t links_dropped;     /**< Of those, the ones whose link came past the ones a boot resolves. */
    uint32_t bridges_below;     /**< Bridges below a root bridge published: each one whose _PRT is read, and each one
                                     above such a bridge. */
    uint32_t bridges_below_dropped; /**< Bridges below a root bridge left out, the bridges below them not looked at:
                                         one past the ones a boot publishes, one deeper than a boot goes, or a Device
                                         past the ones of its bridge a boot compares. */
    uint32_t addresses_shared;      /**< _ADR values that two Devices or more of one bridge give, the Device Linux
                                         scores best taken for them. */
    uint32_t children_peak;         /**< The most Devices of one bridge whose _ADR names a PCI function. */
    uint32_t depth_peak;            /**< The most bridges between a root bridge and one published below it, that one
                                         included. */
    AdvancedConfigurationAndPowerInterfaceInterruptRoute_t
        routes[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_ROUTES]; /**< The routes, in the order
                                                                                             their entries were
                                                                                             read. */
    AdvancedConfigurationAndPowerInterfaceInterruptRoutingBridge_t
        below[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_ROUTING_MAX_BRIDGES]; /**< The bridges below a root
                                                                                            bridge, each after the one
                                                                                            it sits below. */
} AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t;

/**
 * @brief Publishes where each PCI interrupt pin goes (§3.11): evaluates the _PRT of each PCI root bridge _STA keeps
 *        in, in declaration order, then that of each bridge below it, and reads each entry as ACPICA and Linux read
 *        it.
 *
 * @details A bridge below a root bridge is the Device Linux v6.17 takes as the ACPI companion of a PCI-to-PCI bridge
 *          (acpi_pci_find_companion, acpi_find_child_device): a child of the root bridge, or of the bridge above it,
 *          whose _ADR names a PCI function; of several children that name the same one, the first Linux scores
 *          best, by their children, _STA and _HID; never a child of the root bridge whose _ADR names function 0 of
 * device 0 and that has a _HID. Such a Device is published when it holds a _PRT or sits above one that does, depth
 *          first in declaration order, right after its root bridge's routes or the bridge above it.
 *          A Source that is an Integer wires the pin to the interrupt its SourceIndex gives, level-triggered and
 *          active-low, as PCI interrupts are (Linux v6.17 acpi_pci_irq_enable). A Source that names an interrupt
 *          link gives the link's current interrupt, as Linux v6.17 reads it (acpi_pci_link_get_current,
 *          acpi_pci_link_allocate): the first interrupt of the first IRQ or Extended Interrupt descriptor of its
 *          _CRS that names one, kept only when the first such descriptor of its _PRS lists it, with that
 *          descriptor's trigger and polarity, each template whole as ACPICA validates it. An entry whose link gives
 *          no interrupt is published without one, since Linux takes the first matching entry whatever its link gives.
 *          Each link is read once. The tables an evaluation loads get their late initialisation as soon as it
 *          returns.
 *
 * @param load           A load whose namespace the initialisation has initialised (§3.8).
 * @param machine        The interrupt model step 5 told firmware.
 * @param initialization The counts the sequence keeps: the _STA evaluations and the failures, per status.
 * @param out_table      Receives the routes, the bridges and the counts, the counts zeroed first; the routes and
 *                       the bridges past their counts are left as they were.
 */
extern void advanced_configuration_and_power_interface_interrupt_routing_publish(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t *machine,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *initialization,
    AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *out_table);

/**
 * @brief Finds where pin @p pin of device @p device goes, on the bus below the @p hop_count bridges @p hops names from
 *        root bus @p bus of segment @p segment down, as Linux v6.17 looks a PCI function up (acpi_pci_irq_lookup).
 *
 * @details The first entry for the device and pin of the _PRT of the bridge above the function's bus decides: the
 *          root bridge's on the root bus, otherwise the one of the bridge published for it below the root bridge.
 *          Without such an entry, or without a bridge published for it, the pin goes through that bridge, swizzled
 *          to (pin + device) mod 4, or replaced by a CardBus bridge's own, and the bridge's device and that pin are
 *          looked up on the bus above, up to the root bridge's _PRT. An entry found ends the lookup, whatever its link
 *          gives.
 *
 * @param table     A published table.
 * @param segment   The PCI segment group.
 * @param bus       The root bus.
 * @param hops      The bridges between the root bus and the function's bus, the one on the root bus first; NULL
 *                  when @p hop_count is 0.
 * @param hop_count How many, 0 for a function on the root bus.
 * @param device    The function's device number, 0 to 31.
 * @param pin       Its pin: 0 to 3, INTA to INTD.
 * @param out_route Receives the route of the entry found.
 * @return false when no _PRT up to the root bridge's holds an entry, when the first one found gives no interrupt, or
 *         when a CardBus bridge on the way has no pin or one past 4.
 */
extern bool advanced_configuration_and_power_interface_interrupt_routing_find(
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *table, uint16_t segment, uint8_t bus,
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingHop_t *hops, uint32_t hop_count, uint8_t device,
    uint8_t pin, AdvancedConfigurationAndPowerInterfaceInterruptRoute_t *out_route);

/**
 * @brief Folds what @p table publishes with FNV-1a: its model and count, then each route's interrupt, segment, bus,
 *        the bridges from its root bridge down to the one whose _PRT holds it, their count then each one's address,
 *        and its device, pin, trigger and polarity, little-endian. The link's node is left out: a node index is no
 *        fixed identity of the firmware.
 *
 * @param table A published table.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_interrupt_routing_fold(
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *table);

/**
 * @brief Whether a PCI function may take its 8259 line: in the PIC model alone, where firmware routes PCI interrupts
 *        to the 8259; never in the APIC model, where firmware may have moved them off it (§3.8 step 5), nor in an
 *        unknown one, after a \_PIC (1) that failed.
 *
 * @param table A published table.
 * @return true in the PIC model.
 */
extern bool advanced_configuration_and_power_interface_interrupt_routing_leaves_the_8259(
    const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *table);

/**
 * @brief The name of interrupt model @p model as the routing records write it.
 *
 * @param model An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_INTERRUPT_MODEL_* value.
 * @return `pic`, `apic` or `unknown`.
 */
extern const char *advanced_configuration_and_power_interface_interrupt_routing_model_name(uint8_t model);

/**
 * @brief The routes the boot published.
 *
 * @return The table, never NULL: no route until advanced_configuration_and_power_interface_machine_language_initialize
 *         runs, and when the boot's namespace could not be opened.
 */
extern const AdvancedConfigurationAndPowerInterfaceInterruptRoutingTable_t *
advanced_configuration_and_power_interface_interrupt_routing(void);

#endif /* KERNEL_ACPI_INTERRUPT_ROUTING_H */
