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
#include <kernel/acpi/tables.h>

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief How the platform-wide \_SB._OSC answered (§3.8 step 6).
 */
typedef enum AdvancedConfigurationAndPowerInterfacePlatformCapabilities {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_ABSENT = 0,  /**< \_SB has no _OSC. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_GRANTED = 1, /**< The query and the commit
                                                                                       returned no error bit. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_REFUSED = 2, /**< One of them returned an error
                                                                                       bit in its DWORD 1. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_FAILED = 3,  /**< One of them failed, or returned
                                                                                       no Buffer of two DWORDs. */
} AdvancedConfigurationAndPowerInterfacePlatformCapabilities_t;

/**
 * @brief What the initialisation needs to know of the machine whose namespace it initialises.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInitializationMachine {
    const AdvancedConfigurationAndPowerInterfaceMadtState_t *madt; /**< The CPU slots processor objects bind to, or
                                                                        NULL: none binds. */
    bool monitor_wait; /**< The processors implement MONITOR/MWAIT, so _CST may name MWAIT hints. */
} AdvancedConfigurationAndPowerInterfaceInitializationMachine_t;

/**
 * @brief What the initialisation of a namespace ran and counted (§3.8 steps 2 to 7).
 */
typedef struct AdvancedConfigurationAndPowerInterfaceInitialization {
    uint32_t initializations;               /**< _INI methods run, \_INI and \_SB._INI included. */
    uint32_t initialization_failed;         /**< Of those, the ones that failed. */
    uint32_t registrations;                 /**< _REG (PCI_Config, 1) run, one per scope that declares such a region. */
    uint32_t region_registration_failed;    /**< Of those, the ones that failed. */
    uint32_t devices;                       /**< Devices, processors and thermal zones in the namespace. */
    uint32_t status_evaluations;            /**< _STA objects evaluated: only in subtrees that hold an _INI. */
    uint32_t status_failed;                 /**< Of those, the ones whose evaluation failed or gave no Integer, String
                                                 or Buffer. */
    uint32_t status_invalid;                /**< Of those, the results ACPI 6.6 calls invalid, walked as any other. */
    uint32_t pic_mode_deferred;             /**< 1 when \_PIC exists and was not called: PCI interrupts go through the
                                                 8259 until #59. */
    uint32_t platform_capabilities;         /**< An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_*
                                                 value. */
    uint32_t processor_capabilities;        /**< Processors that were given the capabilities: through _OSC, or _PDC
                                                 without one. */
    uint32_t processor_capabilities_legacy; /**< Of those, the ones given them through _PDC. */
    uint32_t processor_capabilities_refused; /**< Of those, the ones whose _OSC returned an error bit. */
    uint32_t processor_capabilities_failed;  /**< Of those, the ones whose _OSC or _PDC failed, or whose _OSC
                                                  returned no Buffer of two DWORDs. */
    uint32_t processors_unbound;             /**< Processor objects with an _OSC or a _PDC that bind to no CPU
                                                  slot, and were given nothing. */
    uint32_t statuses[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MACHINE_LANGUAGE_STATUS_COUNT]; /**< The failures,
                                                                                                     per status. */
    uint32_t first_failure_node; /**< The object whose evaluation failed first, or 0. */
    uint8_t first_failure;       /**< Its status. */
} AdvancedConfigurationAndPowerInterfaceInitialization_t;

/**
 * @brief Initialises a loaded namespace as firmware expects (§3.8 steps 2 to 7, and 9): runs \_INI then
 *        \_SB._INI; runs _REG (PCI_Config, 1) once in every scope that declares a PCI_Config region,
 *        after which a region a method creates gets its _REG at once; walks the devices, processors and
 *        thermal zones in declaration order, evaluating _STA only in subtrees that hold an _INI; leaves
 *        \_PIC uncalled; asks \_SB._OSC for nothing, a query then a commit; gives every processor object
 *        bound to a CPU slot the capabilities of its C-states through its _OSC, or its _PDC; then drops the
 *        Notifies the load and the sequence raised.
 *
 * @param load               A load whose tables are decoded, its stacks free.
 * @param machine            The CPU slots and the processor features of the machine.
 * @param out_initialization Receives what the sequence ran and counted; zeroed first.
 */
extern void advanced_configuration_and_power_interface_initialize_namespace(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t *machine,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *out_initialization);

/**
 * @brief The name the telemetry gives a platform _OSC answer.
 *
 * @param capabilities An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PLATFORM_CAPABILITIES_* value.
 * @return "absent", "granted", "refused" or "failed"; "unknown" for any other value.
 */
extern const char *advanced_configuration_and_power_interface_platform_capabilities_name(uint32_t capabilities);

/**
 * @brief Loads this machine's definition blocks at boot, into the boot's load, with the caps of the
 *        build's profile, and initialises the namespace they build (§3.8 steps 1 to 7, and 9), its
 *        processor objects bound to the boot's MADT.
 *
 * @param monitor_wait The processors implement MONITOR/MWAIT.
 */
extern void advanced_configuration_and_power_interface_machine_language_initialize(bool monitor_wait);

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
