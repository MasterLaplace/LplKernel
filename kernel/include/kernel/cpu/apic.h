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
 * @file apic.h
 * @brief Universal local APIC management (xAPIC and x2APIC).
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H
#define KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H

#include <kernel/drivers/serial.h>
#include <kernel/lib/asmutils.h>

#include <stdbool.h>
#include <stdint.h>

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

#define X2APIC_MSR_BASE 0x800u

/**
 * @brief The mode the local APICs run in, and why.
 */
typedef enum {
    APIC_MODE_NOT_CHOSEN = 0,          /**< apic_initialize_on_cpu has not run yet. */
    APIC_MODE_XAPIC_NOT_OFFERED,       /**< xAPIC: the processor offers no x2APIC. */
    APIC_MODE_XAPIC_WITHOUT_REMAPPING, /**< xAPIC: on bare metal a device interrupt reaches x2APIC mode only through
                                            interrupt remapping (SDM 325384-093US §13.12.6), which the kernel does not
                                            turn on. */
    APIC_MODE_X2APIC_FROM_FIRMWARE,    /**< x2APIC: the firmware handed it over, as it does by default when an APIC
                                            ID is 255 or more (§13.12.7), and the kernel keeps it. */
    APIC_MODE_X2APIC_UNDER_HYPERVISOR, /**< x2APIC: a hypervisor reports itself, and the kernel assumes, as
                                            device_interrupt_platform_delivers does, that it delivers device
                                            interrupts to x2APIC mode without remapping. Linux v6.17 trusts only a
                                          hypervisor whose guest support says so (x86_init.hyper.x2apic_available, false
                                          by default). */
} ApicMode_t;

/**
 * @brief What the processor and the firmware report, from which the mode is chosen.
 */
typedef struct {
    bool x2apic_offered;       /**< CPUID leaf 1, ECX bit 21. */
    bool hypervisor_present;   /**< CPUID leaf 1, ECX bit 31. */
    bool x2apic_from_firmware; /**< IA32_APIC_BASE bit 10 (EXTD), as the firmware left it. */
} ApicPlatform_t;

/**
 * @brief The mode the local APICs must run in on a platform, and why.
 *
 * @details The rules, in order: x2APIC mode the firmware handed over is kept; without x2APIC the mode is xAPIC;
 *          under a hypervisor x2APIC mode is turned on; on bare metal the mode stays xAPIC, since no device
 *          interrupt could reach x2APIC mode without interrupt remapping, as Linux v6.17 decides in
 *          try_to_enable_x2apic (arch/x86/kernel/apic/apic.c). Linux, without remapping, also leaves the firmware's
 *          x2APIC mode unless the bootstrap processor's APIC ID is 255 or more or the firmware locked the mode
 *          (x2apic_disable); the kernel keeps it, as issue #795 decides.
 *
 * @param platform What the processor and the firmware report.
 * @return The mode, never APIC_MODE_NOT_CHOSEN.
 */
ApicMode_t apic_choose_mode(const ApicPlatform_t *platform);

/**
 * @brief The mode the first call of apic_initialize_on_cpu chose, the one every processor runs in.
 *
 * @return The mode, or APIC_MODE_NOT_CHOSEN before that call.
 */
ApicMode_t apic_get_mode(void);

/**
 * @brief The reason of @p mode as the serial line and the machine record write it.
 *
 * @param mode A mode.
 * @return `x2apic-not-offered`, `bare-metal-without-interrupt-remapping`, `handed-over-by-firmware`,
 *         `under-hypervisor` or `not-chosen`; `unknown` for a value outside the enumeration.
 */
const char *apic_mode_reason_name(ApicMode_t mode);

/**
 * @brief Whether the local APICs run in x2APIC mode, or will once apic_initialize_on_cpu has run.
 *
 * @details Before that call the mode is the one apic_choose_mode would choose from what the bootstrap processor reports
 *          now, the firmware's IA32_APIC_BASE included; after it, the mode that call chose. The boot needs it before
 *          the bootstrap processor's local APIC is initialised, to choose the interrupt model it tells firmware (ACPI
 *          specification, §3.8 step 5).
 *
 * @return true when x2APIC mode is, or will be, the mode.
 */
bool apic_runs_x2apic_mode(void);

/**
 * @brief Puts the current processor's local APIC in the mode every processor shares, and software-enables it.
 *
 * @details The first call, the bootstrap processor's, chooses the mode with apic_choose_mode from what its CPUID and
 *          IA32_APIC_BASE report; every later call, an application processor's, applies that mode, leaving x2APIC
 *          mode through the disabled state when its firmware left it there (SDM 325384-093US §13.12.5.1). Each call
 *          prints the mode and its reason.
 *
 * @param mmio_virtual_base Virtual address where xAPIC MMIO is mapped.
 * @return true once the local APIC is enabled.
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

/**
 * @brief Whether apic_initialize_on_cpu has run on the bootstrap processor, after which apic_read and apic_get_id read
 *        the local APIC of the processor that runs the call.
 *
 * @details No other processor runs before that call: each is started through the bootstrap processor's local APIC, and
 *          runs apic_initialize_on_cpu before any code that reads its own register.
 *
 * @return true once the bootstrap processor's local APIC is reachable, through MMIO or its x2APIC registers.
 */
bool apic_is_running(void);

/**
 * @brief The local APIC ID of the processor that runs the call, read from its own ID register.
 *
 * @details Bits 31:24 of the register in xAPIC mode, all 32 bits in x2APIC mode (Intel SDM vol. 3, 325384-093US,
 *          §13.4.6 and §13.12.5.1). cpu_topology_get_local_apic_id gives one identifier for every processor; this
 *          one differs on each.
 *
 * @return The identifier.
 */
uint32_t apic_get_id(void);

#endif /* KERNEL_CPU_ADVANCED_PROGRAMMABLE_INTERRUPT_CONTROLLER_H */
