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
 * @file ap_trampoline.h
 * @brief Application processor trampoline in low memory: the real-mode SIPI target.
 *
 * Installs the trampoline below 1 MiB and tracks the acknowledgement of each processor
 * that came through it.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2026-03-17
 **************************************************************************/

#ifndef KERNEL_CPU_APPLICATION_PROCESSOR_TRAMPOLINE_H
#define KERNEL_CPU_APPLICATION_PROCESSOR_TRAMPOLINE_H

#include <stdint.h>

/**
 * @brief Install AP trampoline code in low physical memory.
 *
 * Current target is physical 0x8000 (startup vector 0x08).
 *
 * @return Non-zero when trampoline bytes were copied successfully.
 */
extern uint8_t application_processor_trampoline_install(void);

/**
 * @brief Return non-zero when trampoline is installed.
 */
extern uint8_t application_processor_trampoline_is_installed(void);

/**
 * @brief Return SIPI startup vector for installed trampoline.
 */
extern uint8_t application_processor_trampoline_get_startup_vector(void);

/**
 * @brief Clear trampoline acknowledgement marker.
 */
extern void application_processor_trampoline_reset_acknowledgement(void);

/**
 * @brief Wait for AP trampoline acknowledgement marker.
 *
 * @param spin_limit Maximum spin iterations before timeout.
 * @return Non-zero when acknowledgement marker observed.
 */
extern uint8_t application_processor_trampoline_wait_for_acknowledgement(uint32_t spin_limit);

/**
 * @brief Return last observed raw acknowledgement word.
 */
extern uint16_t application_processor_trampoline_get_acknowledgement_word(void);

/**
 * @brief Configure AP trampoline handoff mailbox for one AP startup attempt.
 *
 * @param apic_id Target AP APIC ID.
 * @param logical_slot Logical slot assigned to this AP.
 * @param stack_top_virtual AP stack top virtual address.
 * @param c_entry_virtual AP C entry virtual address.
 * @param main_loop_virtual AP main loop virtual address.
 * @param cr3_physical Shared kernel CR3 physical address.
 */
extern void application_processor_trampoline_configure_handoff(uint8_t apic_id, uint32_t logical_slot,
                                                               uint32_t stack_top_virtual, uint32_t c_entry_virtual,
                                                               uint32_t main_loop_virtual, uint32_t cr3_physical);

/**
 * @brief Wait for AP C-entry completion marker from trampoline handoff.
 *
 * @param spin_limit Maximum spin iterations before timeout.
 * @return Non-zero when C-entry marker observed.
 */
extern uint8_t application_processor_trampoline_wait_for_c_entry(uint32_t spin_limit);

/**
 * @brief Return last observed raw C-entry marker word.
 */
extern uint16_t application_processor_trampoline_get_c_entry_word(void);

/**
 * @brief Return number of successful trampoline installs.
 */
extern uint32_t application_processor_trampoline_get_install_count(void);

/**
 * @brief Return number of acknowledgement wait attempts.
 */
extern uint32_t application_processor_trampoline_get_ack_wait_count(void);

/**
 * @brief Return number of acknowledgement wait successes.
 */
extern uint32_t application_processor_trampoline_get_ack_success_count(void);

/**
 * @brief Return number of acknowledgement wait timeouts.
 */
extern uint32_t application_processor_trampoline_get_ack_timeout_count(void);

/**
 * @brief Return number of C-entry wait attempts.
 */
extern uint32_t application_processor_trampoline_get_c_entry_wait_count(void);

/**
 * @brief Return number of successful C-entry marker observations.
 */
extern uint32_t application_processor_trampoline_get_c_entry_success_count(void);

/**
 * @brief Return number of C-entry wait timeouts.
 */
extern uint32_t application_processor_trampoline_get_c_entry_timeout_count(void);

#endif /* KERNEL_CPU_APPLICATION_PROCESSOR_TRAMPOLINE_H */
