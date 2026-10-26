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
 * @file initialization.h
 * @brief The initialisation sequence of the ACPI namespace: \_INI and \_SB._INI, the _REG of PCI_Config
 *        regions, the _STA and _INI walk, and the boot's load that runs them.
 *
 * Layer L6 of the specification (docs/acpi/specification.md, §3.8): ordinary kernel code that asks
 * the interpreter to evaluate objects and reads plain results. A failure is counted and the sequence
 * goes on.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-26
 **************************************************************************/

#ifndef KERNEL_ACPI_INITIALIZATION_H
#define KERNEL_ACPI_INITIALIZATION_H

#include <kernel/acpi/machine_language.h>

#include <stdint.h>

/**
 * @brief What the initialisation of a namespace ran and counted (§3.8 steps 2 to 5).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInitialization {
    uint32_t initializations;            /**< _INI methods run, \_INI and \_SB._INI included. */
    uint32_t initialization_failed;      /**< Of those, the ones that failed. */
    uint32_t registrations;              /**< _REG (PCI_Config, 1) run, one per scope that declares such a region. */
    uint32_t region_registration_failed; /**< Of those, the ones that failed. */
    uint32_t devices;                    /**< Devices, processors and thermal zones in the namespace. */
    uint32_t status_evaluations;         /**< _STA objects evaluated: only in subtrees that hold an _INI. */
    uint32_t status_failed;              /**< Of those, the ones whose evaluation failed or gave no Integer, String
                                              or Buffer. */
    uint32_t status_invalid;             /**< Of those, the results ACPI declares invalid. */
    uint32_t pic_mode_deferred;          /**< 1 when \_PIC exists and was not called: PCI interrupts go through the
                                              8259 until #59. */
    uint32_t statuses[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT]; /**< The failures,
                                                                                                     per status. */
    uint32_t first_failure_node; /**< The _INI, _REG or _STA whose evaluation failed first, or 0. */
    uint8_t first_failure;       /**< Its status. */
} AdvancedConfigurationAndPowerInterfaceInitialization_t;

/**
 * @brief Initialises a loaded namespace as firmware expects (§3.8 steps 2 to 5, and 9): runs \_INI then
 *        \_SB._INI; runs _REG (PCI_Config, 1) once in every scope that declares a PCI_Config region,
 *        after which a region a method creates gets its _REG at once; walks the devices, processors and
 *        thermal zones in declaration order, evaluating _STA only in subtrees that hold an _INI; leaves
 *        \_PIC uncalled; then drops the Notifies the load and the sequence raised.
 *
 * @param load               A load whose tables are decoded, its stacks free.
 * @param out_initialization Receives what the sequence ran and counted; zeroed first.
 */
extern void advanced_configuration_and_power_interface_initialize_namespace(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *out_initialization);

/**
 * @brief Loads this machine's definition blocks at boot, into the boot's load, with the caps of the
 *        build's profile, and initialises the namespace they build (§3.8 steps 1 to 5, and 9).
 */
extern void advanced_configuration_and_power_interface_machine_language_initialize(void);

/**
 * @brief The boot's load, which advanced_configuration_and_power_interface_machine_language_initialize fills.
 *
 * @return The load, never NULL.
 */
extern const AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *
advanced_configuration_and_power_interface_machine_language(void);

/**
 * @brief What the boot's initialisation ran and counted.
 *
 * @return The counts, never NULL: zero until advanced_configuration_and_power_interface_machine_language_initialize
 *         runs, and when the boot's namespace could not be opened.
 */
extern const AdvancedConfigurationAndPowerInterfaceInitialization_t *
advanced_configuration_and_power_interface_initialization(void);

#endif /* KERNEL_ACPI_INITIALIZATION_H */
