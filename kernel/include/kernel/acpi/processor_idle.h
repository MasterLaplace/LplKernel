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
 * @file processor_idle.h
 * @brief The C-states firmware lists for each CPU: processor objects bound to their CPU slot, each
 *        one's _CST evaluated and published per CPU slot as plain data.
 *
 * Layer L6 of the specification (docs/acpi/specification.md, §3.8 steps 8 and 9, and §3.10): ordinary
 * kernel code that asks the interpreter to evaluate objects and reads plain results.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-29
 **************************************************************************/

#ifndef KERNEL_ACPI_PROCESSOR_IDLE_H
#define KERNEL_ACPI_PROCESSOR_IDLE_H

#include <kernel/acpi/initialization.h>
#include <kernel/acpi/tables.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_MAX_STATES                                           \
    8u /**< States one CPU slot publishes, as Linux's ACPI_PROCESSOR_MAX_POWER; the rest are counted (§6.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_NO_SLOT                                              \
    0xFFFFFFFFu /**< What a processor object bound to no CPU slot binds to. */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_CHANGED                                              \
    0x81u /**< The Notify value that says a processor's _CST changed (ACPI 6.6 §8.4.1.1). */

/**
 * @brief How step 8 handed _CST to the operating system through SMI_CMD (§3.8 step 8).
 */
typedef enum AdvancedConfigurationAndPowerInterfaceIdleControl {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_ABSENT = 0,  /**< CST_CNT or SMI_CMD is 0: nothing to
                                                                              write. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_WRITTEN = 1, /**< CST_CNT written to SMI_CMD. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_SKIPPED = 2, /**< The platform is not in ACPI mode, or
                                                                              SMI_CMD is no port the kernel may
                                                                              write: nothing written. */
} AdvancedConfigurationAndPowerInterfaceIdleControl_t;

/**
 * @brief One C-state as firmware lists it (ACPI 6.6 §8.4.1.1): the register read to enter it, then its
 *        type, latency and power.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceIdleState {
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t entry; /**< The Register descriptor: FFixedHW (0x7F)
                                                                       with the MWAIT hint as its address, its
                                                                       vendor and class as its width and offset;
                                                                       a SystemIO port read; or SystemMemory. */
    uint32_t type;    /**< The C-state type: 1, 2 or 3 as ACPI defines them, as firmware gave it. */
    uint32_t latency; /**< The worst-case latency to enter and leave it, in microseconds. */
    uint32_t power;   /**< The processor's average power in it, in milliwatts. */
} AdvancedConfigurationAndPowerInterfaceIdleState_t;

/**
 * @brief The C-states one CPU slot publishes.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceProcessorIdle {
    uint32_t processor; /**< The node of the processor object bound to the slot whose _CST was evaluated, or 0. */
    uint32_t count;     /**< States published: 0 when the evaluation failed or its package is malformed, and the
                             CPU keeps the states the kernel enters without ACPI. */
    AdvancedConfigurationAndPowerInterfaceIdleState_t
        states[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_MAX_STATES]; /**< In _CST's order. */
} AdvancedConfigurationAndPowerInterfaceProcessorIdle_t;

/**
 * @brief The C-states of every CPU slot, in the MADT's slot order (§3.10).
 */
struct AdvancedConfigurationAndPowerInterfaceProcessorIdleTable {
    uint32_t slot_count; /**< The MADT's CPU slots. */
    AdvancedConfigurationAndPowerInterfaceProcessorIdle_t
        slots[ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_MAX_PROCESSOR_COUNT]; /**< One per CPU slot. */
};

typedef struct AdvancedConfigurationAndPowerInterfaceProcessorIdleTable
    AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t;

/**
 * @brief Whether a Device or a Processor is a processor object: a Processor, or a Device whose _HID is
 *        "ACPI0007" (ACPI 6.6 §8.4); a _HID that cannot be read is counted.
 *
 * @param load           The load.
 * @param initialization Where a failure is counted.
 * @param node           The Device or Processor.
 * @return Whether it is one.
 */
extern bool advanced_configuration_and_power_interface_processor_object(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *initialization, uint32_t node);

/**
 * @brief The CPU slot a processor object binds to (§3.10): the first slot of the MADT, which holds
 *        Enabled processors only, whose ACPI Processor UID is the object's, a Processor's ProcessorID or
 *        a Device's _UID converted as an operand is (§5.2); a _UID that cannot be read is counted.
 *
 * @param load           The load.
 * @param machine        The CPU slots.
 * @param initialization Where a failure is counted.
 * @param node           The processor object.
 * @return The slot, or ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_PROCESSOR_IDLE_NO_SLOT when the MADT is
 *         absent, the Device has no _UID or it cannot be read, or no slot has its UID.
 */
extern uint32_t advanced_configuration_and_power_interface_processor_slot(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t *machine,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *initialization, uint32_t node);

/**
 * @brief Steps 8 and 9 of §3.8: writes CST_CNT to SMI_CMD once the platform is in ACPI mode; evaluates
 *        the _CST of each processor object bound to a CPU slot, in declaration order, and publishes its
 *        states in that slot; then takes every Notify queued, a Notify 0x81 on a processor queued after
 *        its evaluation evaluating its _CST once more, every other one dropped.
 *
 * @param load           A load whose namespace steps 2 to 7 initialised, its stacks free.
 * @param machine        The CPU slots, the FADT and how step 0 ended.
 * @param initialization Where the evaluations and their failures are counted.
 * @param out_table      Receives the states of every CPU slot of the MADT, those slots zeroed first, and
 *                       nothing past them, which no reader of the table reads.
 */
extern void advanced_configuration_and_power_interface_processor_idle_publish(
    AdvancedConfigurationAndPowerInterfaceMachineLanguageLoad_t *load,
    const AdvancedConfigurationAndPowerInterfaceInitializationMachine_t *machine,
    AdvancedConfigurationAndPowerInterfaceInitialization_t *initialization,
    AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t *out_table);

/**
 * @brief Folds a published table with FNV-1a, 32 bits, over little-endian bytes: the slot count, then
 *        per slot whether a _CST was evaluated for it, its state count, and each state's register
 *        (space, bit width, bit offset, access size, address), type, latency and power; never a node
 *        index (§8.7).
 *
 * @param table The table.
 * @return The fold.
 */
extern uint32_t advanced_configuration_and_power_interface_processor_idle_fold(
    const AdvancedConfigurationAndPowerInterfaceProcessorIdleTable_t *table);

/**
 * @brief The name the telemetry gives how step 8 handed _CST to the operating system.
 *
 * @param control An ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_IDLE_CONTROL_* value.
 * @return "absent", "written" or "skipped"; "unknown" for any other value.
 */
extern const char *advanced_configuration_and_power_interface_idle_control_name(uint32_t control);

#endif /* KERNEL_ACPI_PROCESSOR_IDLE_H */
