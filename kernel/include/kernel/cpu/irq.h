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
 * @file irq.h
 * @brief Hardware interrupt requests: the timer tick, spurious lines and line ownership.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-06
 **************************************************************************/

#ifndef KERNEL_CPU_INTERRUPT_REQUEST_H
#define KERNEL_CPU_INTERRUPT_REQUEST_H

#include <kernel/cpu/apic_timer.h>
#include <kernel/cpu/exception.h>
#include <kernel/cpu/isr.h>
#include <kernel/cpu/pic.h>
#include <kernel/cpu/pit.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/rtc.h>
#include <kernel/lib/asmutils.h>

#include <stdint.h>

/**
 * @brief Initialize IRQ runtime for current bring-up stage.
 *
 * This routine remaps the PIC, masks all IRQ lines, installs the IRQ0
 * timer handler, initializes keyboard IRQ1 and RTC support, then enables
 * selected lines and interrupts globally.
 */
extern void interrupt_request_initialize(void);

/**
 * @brief Configure IRQ0 timer target frequency for next initialization.
 */
extern void interrupt_request_set_timer_frequency_hz(uint32_t frequency_hz);

/**
 * @brief Configure RTC periodic IRQ8 policy for next initialization.
 */
extern void interrupt_request_set_realtime_clock_periodic_enabled(uint8_t enabled);

/**
 * @brief Return IRQ0 tick count since IRQ initialization.
 */
extern uint32_t interrupt_request_get_tick_count(void);

/**
 * @brief Return configured PIT frequency used by IRQ0 in Hertz.
 */
extern uint32_t interrupt_request_get_timer_frequency_hz(void);

/**
 * @brief Return number of detected spurious IRQ7 events.
 */
extern uint32_t interrupt_request_get_spurious_irq7_count(void);

/**
 * @brief Return number of detected spurious IRQ15 events.
 */
extern uint32_t interrupt_request_get_spurious_irq15_count(void);

/**
 * @brief Return number of handled periodic RTC IRQ8 events.
 */
extern uint32_t interrupt_request_get_realtime_clock_interrupt_count(void);

/**
 * @brief Return non-zero when periodic RTC IRQ8 mode is enabled.
 */
extern uint8_t interrupt_request_is_realtime_clock_periodic_enabled(void);

/**
 * @brief Select timer interrupt owner for vector 32 handler.
 *
 * 0 => legacy PIT/PIC ownership (default)
 * non-zero => Local APIC ownership
 */
extern void interrupt_request_set_timer_owner_is_apic(uint8_t enabled);

/**
 * @brief Return non-zero when timer vector ownership is APIC.
 */
extern uint8_t interrupt_request_is_timer_owner_apic(void);

/**
 * @brief Select keyboard interrupt ownership mode.
 *
 * 0 => legacy PIC ownership
 * non-zero => IOAPIC delivery with Local APIC EOI
 */
extern void interrupt_request_set_keyboard_owner_is_apic(uint8_t enabled);

/**
 * @brief Return non-zero when keyboard IRQ ownership is APIC/IOAPIC.
 */
extern uint8_t interrupt_request_is_keyboard_owner_apic(void);

/**
 * @brief Records that an ISA line is delivered by the IOAPIC, not the 8259.
 *
 * Per LINE, because the handoff is per line. A handler must acknowledge the
 * controller that delivered its interrupt, and asking "is the keyboard on the
 * APIC?" from a mouse handler is how one ends up sending an APIC EOI for a PIC
 * interrupt — which leaves the 8259 unacknowledged and the line dead after one
 * shot.
 *
 * @param irq_line ISA line, 0..15.
 * @param enabled  Non-zero once the line is routed through the IOAPIC.
 */
extern void interrupt_request_set_line_owner_is_apic(uint8_t irq_line, uint8_t enabled);

/**
 * @brief Whether an ISA line is delivered by the IOAPIC.
 * @param irq_line ISA line, 0..15.
 * @return 1 when the IOAPIC owns it, 0 when the 8259 does.
 */
extern uint8_t interrupt_request_is_line_owner_apic(uint8_t irq_line);

#endif /* KERNEL_CPU_INTERRUPT_REQUEST_H */
