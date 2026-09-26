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
 * @file apic_ipi.h
 * @brief Local APIC inter-processor interrupt (IPI) framework.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_IPI_H
#define KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_IPI_H

#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu_topology.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/paging.h>

#include <stdint.h>

/**
 * @brief Initialize IPI framework with LAPIC MMIO base.
 *
 * Must be called after APIC late-init sets up LAPIC MMIO mapping.
 *
 * @param lapic_virtual_base Mapped virtual address of LAPIC MMIO window.
 */
extern void advanced_pic_ipi_initialize(uint32_t lapic_virtual_base);

/**
 * @brief Return non-zero when LAPIC IPI interface is initialized and usable.
 */
extern uint8_t advanced_pic_ipi_is_ready(void);

/**
 * @brief Enable the Local APIC on the current CPU.
 *
 * Sets the software-enable bit in the Spurious Vector Register (SVR 0x0F0).
 */
extern void advanced_pic_ipi_enable_local_apic(void);

/**
 * @brief Send INIT IPI to reset target AP.
 *
 * INIT causes target AP to:
 * - Reset execution state
 * - Clear local registers (except APIC ID)
 * - Wait for SIPI to specify startup code location
 *
 * @param apic_id Physical APIC ID of target processor.
 * @return Non-zero if delivery succeeded (complete or pending);
 *         zero if timeout waiting for delivery.
 */
extern uint8_t advanced_pic_ipi_send_init(uint8_t apic_id);

/**
 * @brief Send Startup IPI (SIPI) to begin AP execution.
 *
 * SIPI causes target AP to:
 * - Jump to startup address = (startup_vector × 4096) in physical memory
 * - Begin execution in real mode (16-bit, A20 enabled)
 *
 * Must be preceded by INIT. Per IA-32 SDM, two SIPIs are recommended.
 *
 * @param apic_id Physical APIC ID of target processor.
 * @param startup_vector Page number (0x00–0xFF). Actual address
 *                       in physical memory = vector × 4096.
 * @return Non-zero if delivery succeeded; zero on timeout.
 */
extern uint8_t advanced_pic_ipi_send_sipi(uint8_t apic_id, uint8_t startup_vector);

/**
 * @brief Standard AP bootstrap: INIT → delay(200µs) → SIPI → delay(200µs) → SIPI.
 *
 * This is the recommended sequence per Intel IA-32 Software Developer's Manual.
 *
 * @param apic_id Physical APIC ID of target processor.
 * @param startup_vector Page number for AP startup code.
 * @return Non-zero if all steps succeeded; zero if any step fails.
 */
extern uint8_t advanced_pic_ipi_send_startup_sequence(uint8_t apic_id, uint8_t startup_vector);

#define ADVANCED_PIC_IPI_SHORT_NONE     0u
#define ADVANCED_PIC_IPI_SHORT_SELF     1u
#define ADVANCED_PIC_IPI_SHORT_ALL_INCL 2u
#define ADVANCED_PIC_IPI_SHORT_ALL_EXCL 3u

/**
 * @brief Send a fixed-vector IPI to a target or shorthand.
 *
 * @param apic_id Physical APIC ID (ignored if shorthand != SHORT_NONE).
 * @param vector Target interrupt vector (32–255).
 * @param shorthand Destination shorthand (e.g., all including self, all excluding self).
 * @return Non-zero if delivery succeeded; zero on timeout.
 */
extern uint8_t advanced_pic_ipi_send_fixed(uint8_t apic_id, uint8_t vector, uint8_t shorthand);

/**
 * @brief IPI-based TLB shootdown.
 *
 * Broadcasts an IPI to all other CPUs to invalidate a specific virtual address.
 *
 * @note The wait for acknowledgements is bounded: a target that never answers — an
 *       unresponsive or phantom CPU — is given up on and counted by
 *       @ref advanced_pic_ipi_get_tlb_shootdown_timeout_count, so a shootdown can never
 *       hang the kernel. The local TLB is invalidated either way.
 */
extern void advanced_pic_ipi_broadcast_tlb_shootdown(uint32_t virt_addr);

/**
 * @brief IPI-based TLB flush.
 *
 * Broadcasts an IPI to all other CPUs to flush their entire TLB.
 */
extern void advanced_pic_ipi_broadcast_tlb_flush(void);

/**
 * @brief Return number of INIT IPIs attempted.
 */
extern uint32_t advanced_pic_ipi_get_init_attempt_count(void);

/**
 * @brief Return number of SIPI IPIs attempted.
 */
extern uint32_t advanced_pic_ipi_get_sipi_attempt_count(void);

/**
 * @brief Return number of startup sequences attempted.
 */
extern uint32_t advanced_pic_ipi_get_startup_sequence_attempt_count(void);

/**
 * @brief Return number of startup sequences fully delivered.
 */
extern uint32_t advanced_pic_ipi_get_startup_sequence_success_count(void);

/**
 * @brief Return number of TLB shootdowns that gave up waiting for an ACK.
 *
 * Non-zero means a target CPU never acknowledged within the spin bound
 * (unresponsive or phantom CPU); the shootdown was abandoned rather than
 * hanging the kernel.
 */
extern uint32_t advanced_pic_ipi_get_tlb_shootdown_timeout_count(void);

#endif /* KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_IPI_H */
