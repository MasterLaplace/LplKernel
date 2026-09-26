/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
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
 * @file apic_timer_helper.h
 * @brief Serial reports of the APIC timer and interrupt ownership handoffs.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_CPU_APIC_TIMER_HELPER_H
#define KERNEL_CPU_APIC_TIMER_HELPER_H

#include <kernel/drivers/serial.h>

extern void write_apic_late_init_state_info(Serial_t *serial);
extern void write_apic_late_init_skipped_info(Serial_t *serial);

extern void write_ioapic_keyboard_handoff_info(Serial_t *serial, uint8_t success);
extern void write_ioapic_keyboard_policy_fallback_info(Serial_t *serial);

extern void write_apic_calibration_info(Serial_t *serial);
extern void write_apic_calibration_skipped_info(Serial_t *serial);

extern void write_apic_owner_handoff_info(Serial_t *serial, uint8_t success);
extern void write_apic_owner_policy_fallback_info(Serial_t *serial);

#endif /* KERNEL_CPU_APIC_TIMER_HELPER_H */
