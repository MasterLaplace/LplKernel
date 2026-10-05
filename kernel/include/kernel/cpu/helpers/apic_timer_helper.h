/**************************************************************************
 * LplKernel v0.0.0.5 - A Simple C Kernel for Laplace
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
