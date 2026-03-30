/*
** EPITECH PROJECT, 2026
** LplKernel [WSL : Ubuntu]
** File description:
** ap_startup_helper
*/

#ifndef KERNEL_CPU_AP_STARTUP_HELPER_H
#define KERNEL_CPU_AP_STARTUP_HELPER_H

#include <kernel/drivers/serial.h>
#include <kernel/cpu/ap_bootstrap.h>

extern void write_ap_bootstrap_init_info(Serial_t *serial, uint8_t ap_bootstrap_ok);
extern void write_ap_startup_skipped_ipi_not_ready_info(Serial_t *serial);
extern void write_ap_startup_skipped_identity_map_unavailable_info(Serial_t *serial);
extern void write_ap_startup_skipped_trampoline_install_failed_info(Serial_t *serial);
extern void write_ap_trampoline_installed_info(Serial_t *serial);
extern void write_ap_startup_dispatch_info(Serial_t *serial, ApplicationProcessorBootstrapEntry_t *entry, uint8_t sequence_ok, uint8_t ack_ok, uint8_t c_entry_ok, uint8_t attempts_used);
extern void write_ap_startup_summary(Serial_t *serial, uint32_t attempted, uint32_t delivered, uint32_t retries_consumed, uint32_t sequence_failures, uint32_t acknowledgement_timeouts, uint32_t c_entry_timeouts);

#endif /* KERNEL_CPU_AP_STARTUP_HELPER_H */
