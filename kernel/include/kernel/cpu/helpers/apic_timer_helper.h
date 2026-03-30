/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** apic_timer_helper
*/

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
