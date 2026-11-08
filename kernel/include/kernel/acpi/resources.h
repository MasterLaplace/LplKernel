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
 * @file resources.h
 * @brief The zero-copy iterator over a resource template: each descriptor read in place, the Generic
 *        Register and the End Tag decoded, every other descriptor skipped by its length.
 *
 * Layer L6 of the specification (docs/acpi/specification.md, §3.10): it reads the bytes of a Buffer
 * an evaluation returned, and never the interpreter's structures.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-10-29
 **************************************************************************/

#ifndef KERNEL_ACPI_RESOURCES_H
#define KERNEL_ACPI_RESOURCES_H

#include <kernel/acpi/tables.h>

#include <stdbool.h>
#include <stdint.h>

#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END_TAG                                                    \
    0x0Fu /**< The name of the small End Tag descriptor (ACPI 6.6 §6.4.2.9). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_GENERIC_REGISTER                                           \
    0x82u /**< The name of the large Generic Register descriptor, bit 7 set (ACPI 6.6 §6.4.3.7). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_INTERRUPT_REQUEST                                          \
    0x04u /**< The name of the small IRQ descriptor (ACPI 6.6 §6.4.2.1). */
#define ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_EXTENDED_INTERRUPT                                         \
    0x89u /**< The name of the large Extended Interrupt descriptor, bit 7 set (ACPI 6.6 §6.4.3.6). */

/**
 * @brief What reading the next descriptor of a template found.
 */
typedef enum AdvancedConfigurationAndPowerInterfaceResourceStep {
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_DESCRIPTOR = 0, /**< A descriptor other than the End Tag. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_END = 1,        /**< The End Tag, or the end of the bytes. */
    ADVANCED_CONFIGURATION_AND_POWER_INTERFACE_RESOURCE_MALFORMED = 2,  /**< A descriptor whose header or data runs
                                                                             past the bytes. */
} AdvancedConfigurationAndPowerInterfaceResourceStep_t;

/**
 * @brief One descriptor of a template, read in place.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceResource {
    const uint8_t *data; /**< Its bytes after its header, inside the template. */
    uint16_t length;     /**< How many. */
    uint8_t name;        /**< A small descriptor's name (bits 3 to 6 of its tag), a large one's with bit 7 set. */
} AdvancedConfigurationAndPowerInterfaceResource_t;

/**
 * @brief One interrupt an IRQ or an Extended Interrupt descriptor names, and how it signals.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceResourceInterrupt {
    uint32_t number; /**< The interrupt: an ISA IRQ for an IRQ descriptor, any interrupt for an Extended one. */
    bool level;      /**< Level-triggered; edge-triggered otherwise. */
    bool active_low; /**< Active-low; active-high otherwise. */
} AdvancedConfigurationAndPowerInterfaceResourceInterrupt_t;

/**
 * @brief Where the iterator stands in a template.
 */
typedef struct AdvancedConfigurationAndPowerInterfaceResourceIterator {
    const uint8_t *bytes; /**< The template. */
    uint32_t length;      /**< Its length in bytes. */
    uint32_t offset;      /**< Where the next descriptor starts. */
} AdvancedConfigurationAndPowerInterfaceResourceIterator_t;

/**
 * @brief Starts @p iterator at the first descriptor of @p bytes.
 *
 * @param iterator Filled.
 * @param bytes    The template, which outlives the iterator.
 * @param length   Its length in bytes.
 */
extern void advanced_configuration_and_power_interface_resources_begin(
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t *iterator, const uint8_t *bytes, uint32_t length);

/**
 * @brief Reads the descriptor where @p iterator stands and moves it past that descriptor.
 *
 * @param iterator     The iterator.
 * @param out_resource Receives the descriptor when one other than the End Tag was read.
 * @return DESCRIPTOR with @p out_resource filled; END at the End Tag or at the end of the bytes, where
 *         the iterator stays; MALFORMED when the descriptor runs past the bytes.
 */
extern AdvancedConfigurationAndPowerInterfaceResourceStep_t advanced_configuration_and_power_interface_resources_next(
    AdvancedConfigurationAndPowerInterfaceResourceIterator_t *iterator,
    AdvancedConfigurationAndPowerInterfaceResource_t *out_resource);

/**
 * @brief Decodes a Generic Register descriptor: its address space, bit width, bit offset, access size
 *        and 64-bit address.
 *
 * @param resource     A descriptor the iterator read.
 * @param out_register Receives the register when it is one.
 * @return false when the descriptor is no Generic Register, or holds fewer than its 12 bytes.
 */
extern bool advanced_configuration_and_power_interface_resources_generic_register(
    const AdvancedConfigurationAndPowerInterfaceResource_t *resource,
    AdvancedConfigurationAndPowerInterfaceGenericAddress_t *out_register);

/**
 * @brief Decodes interrupt @p index of an IRQ descriptor (ACPI 6.6 §6.4.2.1) or of an Extended Interrupt descriptor
 *        (§6.4.3.6).
 *
 * @details An IRQ descriptor names the bits set in its mask, the lowest first; without its information byte, its
 *          interrupts are edge-triggered and active-high, as §6.4.2.1 says.
 *
 * @param resource      A descriptor the iterator read.
 * @param index         Which of its interrupts, counted from 0.
 * @param out_interrupt Receives the interrupt when there is one.
 * @return false when the descriptor is neither, holds fewer bytes than its form needs, or names fewer than
 *         @p index + 1 interrupts.
 */
extern bool advanced_configuration_and_power_interface_resources_interrupt(
    const AdvancedConfigurationAndPowerInterfaceResource_t *resource, uint32_t index,
    AdvancedConfigurationAndPowerInterfaceResourceInterrupt_t *out_interrupt);

#endif /* KERNEL_ACPI_RESOURCES_H */
