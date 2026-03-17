/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** APIC Inter-Processor Interrupt (IPI) framework header
*/

#ifndef KERNEL_CPU_APIC_IPI_H_
#define KERNEL_CPU_APIC_IPI_H_

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
 */
extern void advanced_pic_ipi_broadcast_tlb_shootdown(uint32_t virt_addr);
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

#endif /* !KERNEL_CPU_APIC_IPI_H_ */
