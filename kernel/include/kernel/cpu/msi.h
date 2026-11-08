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
 * @file msi.h
 * @brief Message-signaled interrupts (MSI-X): a device writes a vector to the local APIC of a chosen core.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-07
 **************************************************************************/

#ifndef KERNEL_CPU_MESSAGE_SIGNALED_INTERRUPT_H
#define KERNEL_CPU_MESSAGE_SIGNALED_INTERRUPT_H

#include <kernel/cpu/pci.h>

#include <stdbool.h>
#include <stdint.h>

/**
 * @name The capabilities message-signaled interrupts are programmed through
 * @{
 */
#define MESSAGE_SIGNALED_INTERRUPT_ADDRESS_BASE        0xFEE00000u /**< Messages go here, the destination in bits 19:12. */
#define MESSAGE_SIGNALED_INTERRUPT_CAPABILITY          0x05u       /**< The MSI capability's identifier. */
#define MESSAGE_SIGNALED_INTERRUPT_CAPABILITY_EXTENDED 0x11u       /**< The MSI-X capability's identifier. */
/** @} */

/**
 * @brief The MSI-X table of one function, mapped, as message_signaled_interrupt_extended_open leaves it.
 */
typedef struct {
    uint8_t bus;        /**< The function's bus. */
    uint8_t device;     /**< Its device. */
    uint8_t function;   /**< Its function. */
    uint8_t capability; /**< The offset of its MSI-X capability. */
    uint16_t entries;   /**< Entries its table holds. */
    uint32_t table;     /**< The virtual address of the table's first entry, or 0 when it is not mapped. */
} MessageSignaledInterruptExtendedTable_t;

/**
 * @brief The address and data of a message that interrupts the local APIC @p apic_id with @p vector.
 *
 * @details Fixed delivery, edge-triggered, physical destination (Intel SDM vol. 3, 325384-093US, §13.11.1 and
 *          §13.11.2). The destination field holds 8 bits and 0xFF in it is a broadcast (§13.6.2.1), so a destination
 *          is 0xFE at most; one past it needs interrupt remapping (#108), which this kernel does not program. A vector
 *          lies from 0x10 to 0xFE (§13.11.2).
 *
 * @param apic_id     The local APIC the message interrupts.
 * @param vector      The vector it delivers.
 * @param out_address Where the address goes.
 * @param out_data    Where the data goes.
 * @return false, with nothing written, when @p apic_id is past 0xFE or @p vector outside 0x10-0xFE.
 */
extern bool message_signaled_interrupt_compose(uint32_t apic_id, uint8_t vector, uint32_t *out_address,
                                               uint32_t *out_data);

/**
 * @brief Finds the MSI-X capability of @p device, maps its table and enables MSI-X with every entry masked.
 *
 * @details The table lies in the memory window its Base Address Register Indicator names, at the offset the
 *          capability gives (PCI Local Bus Specification 3.0, §6.8.2). Every entry is masked and the function mask
 *          set before MSI-X is enabled, then the function mask is cleared: an entry interrupts only once aimed. The
 *          function's pin interrupt is disabled too, as MSI-X replaces it.
 *
 * @param device The function.
 * @param out    Where the table goes; its table field is 0 when this fails.
 * @return false when the function has no MSI-X capability, or its table lies outside a memory window below 4 GiB,
 *         or the window cannot be mapped.
 */
extern bool message_signaled_interrupt_extended_open(const PeripheralComponentInterconnectDevice_t *device,
                                                     MessageSignaledInterruptExtendedTable_t *out);

/**
 * @brief Aims entry @p entry of @p table at the processor of logical slot @p slot with @p vector, and unmasks it.
 *
 * @details The entry is masked before anything else, and unmasked only once aimed. Only the mask bit of Vector Control
 *          changes: its reserved bits are kept (PCI Local Bus Specification 3.0, §6.8.2.9).
 *
 * @param table  A table message_signaled_interrupt_extended_open opened.
 * @param entry  The entry, below the table's count.
 * @param slot   The logical CPU slot whose local APIC the message interrupts.
 * @param vector The vector, from device_interrupt_allocate.
 * @return false, the entry masked, when the slot holds no processor, the message cannot name it
 *         (message_signaled_interrupt_compose), or the platform cannot deliver it (device_interrupt_platform_delivers);
 *         false, nothing written, when the table is not open or the entry is past it.
 */
extern bool message_signaled_interrupt_extended_aim(const MessageSignaledInterruptExtendedTable_t *table,
                                                    uint16_t entry, uint32_t slot, uint8_t vector);

/**
 * @brief Aims the one message of @p device's MSI capability at the processor of logical slot @p slot with @p vector,
 *        and enables MSI.
 *
 * @details One message: Multiple Message Enable stays 0. The 64-bit form gets an upper address of 0, and a per-vector
 *          mask is cleared. MSI is off while the address and data are written, then on (PCI Local Bus Specification
 *          3.0, §6.8.1). Bus mastering is turned on, since the message is a write the function makes, and the pin
 *          interrupt off, as MSI replaces it.
 *
 * @param device The function.
 * @param slot   The logical CPU slot whose local APIC the message interrupts.
 * @param vector The vector, from device_interrupt_allocate.
 * @return false, MSI left as it was, when the function has no MSI capability, the slot holds no processor, the
 *         message cannot name it (message_signaled_interrupt_compose), or the platform cannot deliver it
 *         (device_interrupt_platform_delivers).
 */
extern bool message_signaled_interrupt_enable(const PeripheralComponentInterconnectDevice_t *device, uint32_t slot,
                                              uint8_t vector);

#endif /* KERNEL_CPU_MESSAGE_SIGNALED_INTERRUPT_H */
