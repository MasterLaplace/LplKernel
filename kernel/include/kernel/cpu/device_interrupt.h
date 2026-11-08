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
 * @file device_interrupt.h
 * @brief The vectors a device's interrupt carries to the local APIC of a chosen core: who runs, and how often.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-11-08
 **************************************************************************/

#ifndef KERNEL_CPU_DEVICE_INTERRUPT_H
#define KERNEL_CPU_DEVICE_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @name The vectors device interrupts take
 * @{
 */
#define DEVICE_INTERRUPT_VECTOR_BASE  0x50u /**< The first vector a device's interrupt may carry. */
#define DEVICE_INTERRUPT_VECTOR_COUNT 32u   /**< Vectors from it: 0x50 to 0x6F. */
/** @} */

/**
 * @brief What a device vector runs, on the core the interrupt reaches, before the kernel ends the interrupt.
 *
 * @param context What the driver gave device_interrupt_allocate.
 */
typedef void (*DeviceInterruptHandler_t)(void *context);

/**
 * @brief Gives a free device vector to @p handler.
 *
 * @details Called by one processor at a time, before the vector's source is aimed: nothing locks the table.
 *
 * @param handler    What the vector runs; not NULL.
 * @param context    What @p handler receives.
 * @param out_vector Where the vector goes.
 * @return false when every vector is taken or @p handler is NULL.
 */
extern bool device_interrupt_allocate(DeviceInterruptHandler_t handler, void *context, uint8_t *out_vector);

/**
 * @brief Gives @p vector back, which device_interrupt_allocate gave; its counts are kept.
 *
 * @details Its source must be masked first: a delivery racing the release on another processor may run the handler
 *          with a context already cleared.
 *
 * @param vector The vector; another is ignored.
 */
extern void device_interrupt_release(uint8_t vector);

/**
 * @brief Interrupts @p vector delivered on the processor of logical slot @p slot since boot.
 *
 * @param vector A device vector.
 * @param slot   A logical CPU slot: the one cpu_topology_get_slot_for_apic_id gives for the processor's APIC ID.
 * @return The count, or 0 for a vector or a slot out of range.
 */
extern uint32_t device_interrupt_count(uint8_t vector, uint32_t slot);

/**
 * @brief Whether a device's interrupt can reach a local APIC on this platform.
 *
 * @details Always in xAPIC mode. In x2APIC mode only under a hypervisor: on bare metal, a message or an I/O APIC line
 *          reaches an x2APIC through interrupt remapping alone (Intel SDM vol. 3, 325384-093US, §13.12.6 and
 *          §13.12.7), which this kernel does not program (#108), as Linux states too (try_to_enable_x2apic,
 *          arch/x86/kernel/apic/apic.c).
 *
 * @return true when a device's interrupt may be aimed at a core.
 */
extern bool device_interrupt_platform_delivers(void);

#endif /* KERNEL_CPU_DEVICE_INTERRUPT_H */
