/*
** EPITECH PROJECT, 2026
** LplKernel
** File description:
** apic — Universal Local APIC Management (xAPIC & x2APIC)
*/

#ifndef KERNEL_CPU_APIC_H_
#define KERNEL_CPU_APIC_H_

#include <stdbool.h>
#include <stdint.h>

/* LAPIC Register Offsets (for xAPIC MMIO) */
#define LAPIC_REG_ID          0x0020u
#define LAPIC_REG_VERSION     0x0030u
#define LAPIC_REG_TPR         0x0080u
#define LAPIC_REG_EOI         0x00B0u
#define LAPIC_REG_LDR         0x00D0u
#define LAPIC_REG_DFR         0x00E0u
#define LAPIC_REG_SPURIOUS    0x00F0u
#define LAPIC_REG_ESR         0x0280u
#define LAPIC_REG_ICR_LOW     0x0300u
#define LAPIC_REG_ICR_HIGH    0x0310u
#define LAPIC_REG_LVT_TIMER   0x0320u
#define LAPIC_REG_LVT_THERMAL 0x0330u
#define LAPIC_REG_LVT_PERF    0x0340u
#define LAPIC_REG_LVT_LINT0   0x0350u
#define LAPIC_REG_LVT_LINT1   0x0360u
#define LAPIC_REG_LVT_ERROR   0x0370u
#define LAPIC_REG_TIMER_INIT  0x0380u
#define LAPIC_REG_TIMER_CUR   0x0390u
#define LAPIC_REG_TIMER_DIV   0x03E0u

/* x2APIC MSR Range: 0x800 to 0x8ff */
#define X2APIC_MSR_BASE 0x800u

/**
 * @brief Initialize the Local APIC on the current CPU.
 *
 * Detects x2APIC support and enables the best available mode.
 *
 * @param mmio_virtual_base Virtual address where xAPIC MMIO is mapped.
 * @return true if enabled successfully, false otherwise.
 */
bool apic_initialize_on_cpu(uint32_t mmio_virtual_base);

/**
 * @brief Read a Local APIC register.
 *
 * Automatically switches between MMIO (xAPIC) and MSR (x2APIC).
 *
 * @param reg_offset xAPIC-style register offset (e.g. 0x30 for version).
 * @return Register value.
 */
uint32_t apic_read(uint32_t reg_offset);

/**
 * @brief Write to a Local APIC register.
 *
 * Automatically switches between MMIO (xAPIC) and MSR (x2APIC).
 *
 * @param reg_offset xAPIC-style register offset.
 * @param value Value to write.
 */
void apic_write(uint32_t reg_offset, uint32_t value);

/**
 * @brief Write to the Interrupt Command Register (ICR).
 *
 * In xAPIC mode, this performs two 32-bit writes (High then Low).
 * In x2APIC mode, this performs a single 64-bit MSR write.
 *
 * @param high Destination APIC ID and other high-order bits.
 * @param low Vector, delivery mode, and other low-order bits.
 */
void apic_write_icr(uint32_t high, uint32_t low);

/**
 * @brief Send EOI (End of Interrupt) to the Local APIC.
 */
void apic_send_eoi(void);

/**
 * @brief Check if x2APIC mode is currently active.
 */
bool apic_is_x2apic_active(void);

#endif /* !KERNEL_CPU_APIC_H_ */
