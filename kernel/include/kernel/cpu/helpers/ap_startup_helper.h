/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2026 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file ap_startup_helper.h
 * @brief Serial reports of application processor startup.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-30
 **************************************************************************/

#ifndef KERNEL_CPU_AP_STARTUP_HELPER_H
#define KERNEL_CPU_AP_STARTUP_HELPER_H

#include <kernel/cpu/ap_bootstrap.h>
#include <kernel/drivers/serial.h>

extern void write_ap_bootstrap_init_info(Serial_t *serial, uint8_t ap_bootstrap_ok);
extern void write_ap_startup_skipped_ipi_not_ready_info(Serial_t *serial);
extern void write_ap_startup_skipped_identity_map_unavailable_info(Serial_t *serial);
extern void write_ap_startup_skipped_trampoline_install_failed_info(Serial_t *serial);
extern void write_ap_trampoline_installed_info(Serial_t *serial);
extern void write_ap_startup_dispatch_info(Serial_t *serial, ApplicationProcessorBootstrapEntry_t *entry,
                                           uint8_t sequence_ok, uint8_t ack_ok, uint8_t c_entry_ok,
                                           uint8_t attempts_used);
extern void write_ap_startup_summary(Serial_t *serial, uint32_t attempted, uint32_t delivered,
                                     uint32_t retries_consumed, uint32_t sequence_failures,
                                     uint32_t acknowledgement_timeouts, uint32_t c_entry_timeouts);

#endif /* KERNEL_CPU_AP_STARTUP_HELPER_H */
