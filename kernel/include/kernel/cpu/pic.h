/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the GNU General
 * Public License v3.0.
 * https://www.gnu.org/licenses/gpl-3.0.html
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 * See the GNU General Public License for more details.
 *
 * @file pic.h
 * @brief 8259 programmable interrupt controller.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-06
 **************************************************************************/

#ifndef KERNEL_CPU_PROGRAMMABLE_INTERRUPT_CONTROLLER_H
#define KERNEL_CPU_PROGRAMMABLE_INTERRUPT_CONTROLLER_H

#include <kernel/lib/asmutils.h>

#include <stdint.h>

#define PIC_VECTOR_OFFSET_MASTER 32u
#define PIC_VECTOR_OFFSET_SLAVE  40u

/**
 * @brief Initialize and remap legacy 8259 PIC to vectors 32-47.
 */
extern void programmable_interrupt_controller_initialize(void);

/**
 * @brief Mask a specific IRQ line.
 */
extern void programmable_interrupt_controller_set_mask(uint8_t irq_line);

/**
 * @brief Unmask a specific IRQ line.
 */
extern void programmable_interrupt_controller_clear_mask(uint8_t irq_line);

/**
 * @brief Send End-Of-Interrupt to the PIC(s) for a served IRQ.
 */
extern void programmable_interrupt_controller_send_end_of_interrupt(uint8_t irq_line);

/**
 * @brief Return whether an IRQ line is currently marked in-service by PIC.
 *
 * This is used to distinguish real IRQ7/IRQ15 from spurious interrupts.
 */
extern uint8_t programmable_interrupt_controller_is_in_service(uint8_t irq_line);

#endif /* KERNEL_CPU_PROGRAMMABLE_INTERRUPT_CONTROLLER_H */
